#include "FaceTracker.h"
#include <QDebug>
#include <QFileInfo>
#include <QDebug>
#include <QCoreApplication>

FaceTracker::FaceTracker()
{
}

bool FaceTracker::loadModel(std::string const& modelPath)
{
    // 1. 路径检查与自动补全
    QString qPath = QString::fromStdString(modelPath);
    if (!QFileInfo::exists(qPath))
    {
        // 尝试在应用程序目录下查找
        QString appPath = QCoreApplication::applicationDirPath() + "/" + qPath;
        if (QFileInfo::exists(appPath))
        {
            qPath = appPath;
        }
        else
        {
            qCritical() << "[FaceTracker] Model file NOT found at:" << modelPath.c_str();
            return false;
        }
    }

    try
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // 初始化 YuNet
        // input_size: (320, 320) 是兼顾速度和精度的黄金尺寸
        // conf_threshold: 0.6 (置信度，太低会有误检)
        // nms_threshold: 0.3
        // top_k: 5000
        m_detector = cv::FaceDetectorYN::create(
            qPath.toStdString(), "", cv::Size(320, 320), 0.6f, 0.3f, 5000);

        m_modelLoaded = true;
        qDebug() << "[FaceTracker] YuNet model loaded successfully:" << qPath;
        return true;
    }
    catch (cv::Exception const& e)
    {
        qCritical() << "[FaceTracker] OpenCV Exception loading YuNet:" << e.what();
        return false;
    }
}

void FaceTracker::detect(cv::Mat const& imgBGR)
{
    if (!m_modelLoaded || imgBGR.empty())
        return;

    // 获取图像尺寸
    int w = imgBGR.cols;
    int h = imgBGR.rows;

    // [核心优化] 固定推理尺寸 (320x320)
    // YuNet 在固定小尺寸下运行最快且最稳。
    // 我们计算缩放比例，不进行 Letterbox (为了代码简单，直接缩放会导致变形，但在人脸检测任务中通常可接受)
    // 如果追求极致，应该保持长宽比。这里我们直接缩放，然后还原坐标。
    cv::Size const inferenceSize(320, 320);

    cv::Mat inputBlob;
    cv::resize(imgBGR, inputBlob, inferenceSize);

    // 必须加锁，因为 detector 是成员变量
    std::lock_guard<std::mutex> lock(m_mutex);

    // 确保输入尺寸匹配
    m_detector->setInputSize(inferenceSize);

    cv::Mat faces;
    try
    {
        m_detector->detect(inputBlob, faces);
    }
    catch (...)
    {
        return;  // 推理失败
    }

    // 解析结果
    // YuNet Output: [x, y, w, h, x_re, y_re, x_le, y_le, x_nt, y_nt, x_rcm, y_rcm, x_lcm, y_lcm, score]
    if (faces.rows >= 1)
    {
        // 找到置信度最高的人脸 (YuNet 默认已经按 score 排序了吗？通常是的，或者取第一行)
        // 我们遍历一下找最大的脸 (以防多人)
        int bestIdx = -1;
        float maxArea = 0;

        for (int i = 0; i < faces.rows; ++i)
        {
            float* data = faces.ptr<float>(i);
            float score = data[14];
            if (score < 0.6f)
                continue;  // 再次过滤

            float fw = data[2];
            float fh = data[3];
            float area = fw * fh;
            if (area > maxArea)
            {
                maxArea = area;
                bestIdx = i;
            }
        }

        if (bestIdx >= 0)
        {
            float* faceData = faces.ptr<float>(bestIdx);

            // 计算缩放因子 (从 320x320 还原到 w x h)
            float scaleX = (float)w / 320.0f;
            float scaleY = (float)h / 320.0f;

            m_currentFace.hasFace = true;
            m_currentFace.rect.x = (int)(faceData[0] * scaleX);
            m_currentFace.rect.y = (int)(faceData[1] * scaleY);
            m_currentFace.rect.width = (int)(faceData[2] * scaleX);
            m_currentFace.rect.height = (int)(faceData[3] * scaleY);

            m_currentFace.landmarks.clear();
            // 5 个关键点
            for (int i = 0; i < 5; i++)
            {
                float lx = faceData[4 + 2 * i];
                float ly = faceData[5 + 2 * i];
                m_currentFace.landmarks.push_back(cv::Point2f(lx * scaleX, ly * scaleY));
            }
            return;
        }
    }

    // 未检测到
    qDebug() << "未检测到人脸";
    m_currentFace.hasFace = false;
}

FaceData FaceTracker::getFaceData() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentFace;
}
