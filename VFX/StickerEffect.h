#pragma once
#include "VideoEffectSystem.h"

class StickerEffect : public IEffect
{
public:
    StickerEffect();
    void process(cv::Mat& img) override;
    void setParameter(std::string const& key, QVariant const& value) override;
    void setFaceData(FaceData const& face) override
    {
        m_face = face;
    }

    // 加载贴纸资源 (PNG路径)
    void loadAsset(std::string const& path);

private:
    FaceData m_face;
    cv::Mat m_stickerImg;  // BGRA 格式
    std::string m_currentAssetPath;

    // 辅助：叠加图片处理透明通道
    void overlayImage(cv::Mat& src, cv::Mat const& overlay, cv::Point const& location);
    // 辅助：旋转图片
    cv::Mat rotateImage(cv::Mat const& src, double angle);
};
