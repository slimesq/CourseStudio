#pragma once
#include <opencv2/opencv.hpp>
#include <mutex>

// YuNet 的头文件在 objdetect 下
#include <opencv2/objdetect.hpp>

struct FaceData
{
    bool hasFace = false;
    cv::Rect rect;
    // YuNet 输出 5 个关键点：右眼, 左眼, 鼻尖, 右嘴角, 左嘴角
    // 如果后续上了 FaceMesh，这里可以扩展为 468 点
    std::vector<cv::Point2f> landmarks;
};

class FaceTracker
{
public:
    FaceTracker();
    ~FaceTracker() = default;

    // 加载 YuNet ONNX 模型
    bool loadModel(std::string const& modelPath);

    void detect(cv::Mat const& imgBGR);
    FaceData getFaceData() const;

private:
    mutable std::mutex m_mutex;
    FaceData m_currentFace;

    cv::Ptr<cv::FaceDetectorYN> m_detector;
    bool m_modelLoaded = false;
};
