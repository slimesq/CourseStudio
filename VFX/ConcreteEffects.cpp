#include "ConcreteEffects.h"
#include <QColor>
#include <opencv2/imgproc/imgproc.hpp>

// =========================================================
// 1. SkinSmoothEffect (磨皮) - 极致优化
// =========================================================
void SkinSmoothEffect::setParameter(std::string const& key, QVariant const& value)
{
    if (key == "level")
        m_level = value.toInt();
}

void SkinSmoothEffect::process(cv::Mat& img)
{
    if (m_level <= 0)
        return;

    // [优化] 降采样倍数：1/4 (性能比 1/2 提升 4 倍，效果几乎一样)
    // 1080P -> 270P 进行处理
    double scale = 0.25;
    cv::Size smallSize(img.cols * scale, img.rows * scale);

    // resize 会自动复用 m_smallImg 的内存，如果尺寸没变不会重新分配
    cv::resize(img, m_smallImg, smallSize, 0, 0, cv::INTER_LINEAR);

    // 参数调整
    double sigma = 10.0 + m_level * 2.0;

    // 双边滤波在小图上跑，速度极快
    cv::bilateralFilter(m_smallImg, m_smallBlurred, 0, sigma, 10.0);

    // 升采样回原图
    cv::resize(m_smallBlurred, m_blurred, img.size(), 0, 0, cv::INTER_LINEAR);

    // 图像融合
    double opacity = m_level / 80.0;
    if (opacity > 0.8)
        opacity = 0.8;

    // In-place addWeighted: dst = src1*alpha + src2*beta + gamma
    // 直接写入 img，避免产生新 Mat
    cv::addWeighted(m_blurred, opacity, img, 1.0 - opacity, 0, img);
}

// =========================================================
// 2. WhitenEffect (美白) - LUT加速
// =========================================================
WhitenEffect::WhitenEffect()
{
    updateLut();
}

void WhitenEffect::setParameter(std::string const& key, QVariant const& value)
{
    if (key == "level")
    {
        int newLevel = value.toInt();
        if (m_level != newLevel)
        {
            m_level = newLevel;
            m_lutDirty = true;
        }
    }
}

void WhitenEffect::updateLut()
{
    if (m_lut.empty())
        m_lut = cv::Mat(1, 256, CV_8UC1);
    uchar* p = m_lut.ptr();
    double beta = (double)m_level / 50.0;
    for (int i = 0; i < 256; i++)
    {
        double val = i / 255.0;
        if (beta > 0.01)
            val = std::log(val * (beta * 5.0 + 1.0) + 1.0) / std::log(beta * 5.0 + 2.0);
        p[i] = cv::saturate_cast<uchar>(static_cast<int>(val * 255.0));
    }
    m_lutDirty = false;
}

void WhitenEffect::process(cv::Mat& img)
{
    if (m_level <= 0)
        return;
    if (m_lutDirty)
        updateLut();
    // LUT 极快，且支持 In-place
    cv::LUT(img, m_lut, img);
}

// =========================================================
// 3. SharpenEffect (锐化) - 缓存优化
// =========================================================
void SharpenEffect::setParameter(std::string const& key, QVariant const& value)
{
    if (key == "level")
        m_amount = value.toFloat();
}

void SharpenEffect::process(cv::Mat& img)
{
    if (m_amount <= 0.01f)
        return;

    // 使用缓存的 m_blur
    cv::GaussianBlur(img, m_blur, cv::Size(0, 0), 3);

    // USM 快速公式
    cv::addWeighted(img, 1.5 + m_amount * 0.5, m_blur, -(0.5 + m_amount * 0.5), 0, img);
}

// =========================================================
// 4. ContrastEffect (对比度)
// =========================================================
void ContrastEffect::setParameter(std::string const& key, QVariant const& value)
{
    if (key == "contrast")
        m_contrast = value.toFloat();
    if (key == "brightness")
        m_brightness = value.toInt();
}

void ContrastEffect::process(cv::Mat& img)
{
    if (std::abs(m_contrast - 1.0f) < 0.01 && m_brightness == 0)
        return;
    // convertTo 是经过 SIMD 优化的
    img.convertTo(img, -1, m_contrast, m_brightness);
}

// =========================================================
// 5. WatermarkEffect (水印)
// =========================================================
void WatermarkEffect::setParameter(std::string const& key, QVariant const& value)
{
    if (key == "text")
        m_text = value.toString().toStdString();
    else if (key == "x")
        m_x = value.toInt();
    else if (key == "y")
        m_y = value.toInt();
    else if (key == "scale")
        m_scale = value.toDouble();
    // [新增] 处理颜色：QColor -> cv::Scalar (BGR)
    else if (key == "color")
    {
        QColor col = value.value<QColor>();
        m_color = cv::Scalar(col.blue(), col.green(), col.red());
    }
    // [新增] 处理字体格式
    else if (key == "font")
    {
        m_fontFace = value.toInt();
    }
}

void WatermarkEffect::process(cv::Mat& img)
{
    if (m_text.empty())
        return;
    // putText 消耗极低，无需优化
    cv::putText(img, m_text, cv::Point(m_x, m_y), m_fontFace, m_scale, m_color, 2);
}

// =========================================================
// 6. BackgroundBlurEffect (背景模糊) - 矩阵运算彻底重构
// 之前的 for 循环是卡顿元凶，这里改用矩阵乘法
// =========================================================
void BackgroundBlurEffect::setParameter(std::string const& key, QVariant const& value)
{
    if (key == "radius")
        m_blurRadius = value.toInt();
}

void BackgroundBlurEffect::process(cv::Mat& img)
{
    if (m_blurRadius <= 0)
        return;

    // 1. 生成或更新 Mask (仅当尺寸变化时重新计算)
    if (m_mask.empty() || img.cols != m_lastCols || img.rows != m_lastRows)
    {
        m_lastCols = img.cols;
        m_lastRows = img.rows;
        m_mask.create(img.size(), CV_32FC3);  // 注意：创建3通道Mask，方便直接乘

        cv::Mat singleMask(img.size(), CV_32FC1);
        cv::Point center(img.cols / 2, img.rows / 2);
        float maxDist = std::sqrt(center.x * center.x + center.y * center.y) * 0.8f;

        // 使用 parallel_for_ 加速 Mask 生成 (仅在尺寸变化时执行一次，不影响帧率)
        cv::parallel_for_(cv::Range(0, img.rows), [&](cv::Range const& range) {
            for (int y = range.start; y < range.end; ++y)
            {
                float* pRow = singleMask.ptr<float>(y);
                for (int x = 0; x < img.cols; ++x)
                {
                    float dist = std::sqrt(std::pow(x - center.x, 2) + std::pow(y - center.y, 2));
                    float val = (dist / maxDist);
                    val = val * val * val;  // pow(3)
                    if (val > 1.0f)
                        val = 1.0f;
                    pRow[x] = val;
                }
            }
        });

        // 将单通道 Mask 复制 3 份到 3 通道，方便做矩阵乘法
        cv::cvtColor(singleMask, m_mask, cv::COLOR_GRAY2BGR);
    }

    // 2. 降采样模糊 (极大提升性能)
    cv::Mat small;
    cv::resize(img, small, cv::Size(img.cols / 4, img.rows / 4));

    int k = m_blurRadius;
    if (k % 2 == 0)
        k++;
    if (k < 3)
        k = 3;
    // 在小图上做大半径高斯，速度快效果好
    cv::GaussianBlur(small, small, cv::Size(k, k), 0);

    cv::resize(small, m_blurred, img.size());

    // 3. 矩阵混合 (Vectorized Blending)
    // Formula: Result = Original * (1 - Mask) + Blurred * Mask
    // 必须转为浮点数运算，然后再转回 8U，但这比 for 循环快得多

    cv::Mat imgFloat, blurFloat, resFloat;
    img.convertTo(imgFloat, CV_32F);
    m_blurred.convertTo(blurFloat, CV_32F);

    // inverseMask = 1.0 - m_mask
    cv::Mat inverseMask = cv::Scalar(1.0, 1.0, 1.0) - m_mask;

    // 矩阵逐元素相乘 (Hadamard product)
    cv::multiply(imgFloat, inverseMask, imgFloat);  // img * (1-mask)
    cv::multiply(blurFloat, m_mask, blurFloat);     // blur * mask
    cv::add(imgFloat, blurFloat, resFloat);         // sum

    // 转回 8U
    resFloat.convertTo(img, CV_8U);
}

// =========================================================
// 7. FilterEffect (滤镜) - 通道缓存优化
// =========================================================
void FilterEffect::setParameter(std::string const& key, QVariant const& value)
{
    if (key == "type")
        m_type = value.toInt();
}

void FilterEffect::process(cv::Mat& img)
{
    if (m_type == 0)
        return;

    // 使用成员变量 m_channels 避免每帧 vector 分配
    // split/merge 本身是内存拷贝操作，尽量少用，但做色温调整必须用
    cv::split(img, m_channels);

    if (m_type == 1)
    {  // Warm
        // LUT 查表法比乘法更快，这里暂用乘法
        m_channels[0] *= 0.9;  // B
        m_channels[2] *= 1.1;  // R
    }
    else if (m_type == 2)
    {                          // Cool
        m_channels[0] *= 1.1;  // B
        m_channels[2] *= 0.9;  // R
    }
    else if (m_type == 3)
    {  // Gray
        cv::Mat gray;
        cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
        cv::cvtColor(gray, img, cv::COLOR_GRAY2BGR);
        return;  // Early return for gray
    }

    cv::merge(m_channels, img);
}
