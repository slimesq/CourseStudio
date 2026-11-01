#include "StickerEffect.h"
#include <cmath>

StickerEffect::StickerEffect()
{
}

void StickerEffect::loadAsset(std::string const& path)
{
    if (path == m_currentAssetPath)
        return;

    m_stickerImg = cv::imread(path, cv::IMREAD_UNCHANGED);
    if (m_stickerImg.empty())
    {
        qDebug() << "Failed to load sticker:" << path.c_str();
        return;
    }

    // [新增修复] 如果贴纸始终是倒着的，在这里进行垂直翻转
    // 参数 0 表示绕 X 轴翻转（垂直翻转）
    cv::flip(m_stickerImg, m_stickerImg, 0);

    // 确保是 BGRA
    if (m_stickerImg.channels() != 4)
    {
        cv::cvtColor(m_stickerImg, m_stickerImg, cv::COLOR_BGR2BGRA);
    }

    m_currentAssetPath = path;
}

void StickerEffect::setParameter(std::string const& key, QVariant const& value)
{
    if (key == "path")
        loadAsset(value.toString().toStdString());
}

cv::Mat StickerEffect::rotateImage(cv::Mat const& src, double angle)
{
    cv::Point2f center(src.cols / 2.0f, src.rows / 2.0f);
    cv::Mat rot = cv::getRotationMatrix2D(center, angle, 1.0);
    cv::Rect2f bbox = cv::RotatedRect(cv::Point2f(), src.size(), angle).boundingRect2f();
    rot.at<double>(0, 2) += bbox.width / 2.0 - src.cols / 2.0;
    rot.at<double>(1, 2) += bbox.height / 2.0 - src.rows / 2.0;

    cv::Mat dst;
    cv::warpAffine(
        src, dst, rot, bbox.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0, 0));
    return dst;
}

void StickerEffect::overlayImage(cv::Mat& src, cv::Mat const& overlay, cv::Point const& loc)
{
    for (int y = 0; y < overlay.rows; ++y)
    {
        int fy = y + loc.y;
        if (fy < 0 || fy >= src.rows)
            continue;

        for (int x = 0; x < overlay.cols; ++x)
        {
            int fx = x + loc.x;
            if (fx < 0 || fx >= src.cols)
                continue;

            cv::Vec4b const& pixel = overlay.at<cv::Vec4b>(y, x);
            float alpha = pixel[3] / 255.0f;
            if (alpha < 0.01f)
                continue;

            cv::Vec3b& bg = src.at<cv::Vec3b>(fy, fx);
            // 简单的 Alpha Blending: Out = Overlay * A + BG * (1-A)
            bg[0] = cv::saturate_cast<uchar>(pixel[0] * alpha + bg[0] * (1 - alpha));
            bg[1] = cv::saturate_cast<uchar>(pixel[1] * alpha + bg[1] * (1 - alpha));
            bg[2] = cv::saturate_cast<uchar>(pixel[2] * alpha + bg[2] * (1 - alpha));
        }
    }
}

void StickerEffect::process(cv::Mat& img)
{
    if (!m_face.hasFace || m_stickerImg.empty() || m_face.landmarks.size() < 2)
        return;

    // 1. 计算人脸角度 (根据左右眼)
    cv::Point2f leftEye = m_face.landmarks[1];   // YuNet index 1
    cv::Point2f rightEye = m_face.landmarks[0];  // YuNet index 0

    double dy = rightEye.y - leftEye.y;
    double dx = rightEye.x - leftEye.x;
    double angle = -std::atan2(dy, dx) * 180.0 / CV_PI;

    // 2. 计算缩放比例 (以猫耳朵为例，宽度约为两眼距离的 2.5 倍)
    double eyeDist = std::sqrt(dx * dx + dy * dy);
    double targetWidth = eyeDist * 2.5;
    double scale = targetWidth / m_stickerImg.cols;

    // 3. 缩放贴纸
    cv::Mat resizedSticker;
    cv::resize(m_stickerImg, resizedSticker, cv::Size(), scale, scale);

    // 4. 旋转贴纸
    cv::Mat rotatedSticker = rotateImage(resizedSticker, angle);

    // 5. 计算贴图位置 (猫耳朵应该在两眼中心上方)
    cv::Point2f center = (leftEye + rightEye) * 0.5f;
    // 偏移量：向上偏移人脸高度的一部分
    int yOffset = (int)(eyeDist * 1.5);

    int x = (int)(center.x - rotatedSticker.cols / 2);
    int y = (int)(center.y - rotatedSticker.rows / 2 - yOffset);

    // 6. 叠加
    overlayImage(img, rotatedSticker, cv::Point(x, y));
}
