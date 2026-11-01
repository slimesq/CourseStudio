#pragma once
#include "VideoEffectSystem.h"
#include <cmath>

// =========================================================
// 1. 企业级磨皮特效 (高性能版)
// =========================================================
class SkinSmoothEffect : public IEffect
{
public:
    void process(cv::Mat& img) override;
    void setParameter(std::string const& key, QVariant const& value) override;

private:
    int m_level = 0;
    // [优化] 缓存 buffer，避免每帧 malloc
    cv::Mat m_smallImg, m_smallBlurred, m_blurred;
};

// =========================================================
// 2. LUT 美白特效 (缓存版)
// =========================================================
class WhitenEffect : public IEffect
{
public:
    WhitenEffect();
    void process(cv::Mat& img) override;
    void setParameter(std::string const& key, QVariant const& value) override;

private:
    void updateLut();
    int m_level = 0;
    cv::Mat m_lut;
    bool m_lutDirty = true;
};

// =========================================================
// 3. 智能锐化特效 (USM)
// =========================================================
class SharpenEffect : public IEffect
{
public:
    void process(cv::Mat& img) override;
    void setParameter(std::string const& key, QVariant const& value) override;

private:
    float m_amount = 0.0f;
    cv::Mat m_blur;  // 缓存
};

// =========================================================
// 4. 对比度/饱和度 (无状态，直接计算)
// =========================================================
class ContrastEffect : public IEffect
{
public:
    void process(cv::Mat& img) override;
    void setParameter(std::string const& key, QVariant const& value) override;

private:
    float m_contrast = 1.0f;
    int m_brightness = 0;
};

// 在 WatermarkEffect 类中增加私有成员
class WatermarkEffect : public IEffect
{
public:
    void process(cv::Mat& img) override;
    void setParameter(std::string const& key, QVariant const& value) override;

private:
    std::string m_text = "";
    int m_x = 20;
    int m_y = 50;
    double m_scale = 1.0;
    cv::Scalar m_color = cv::Scalar(255, 255, 255);  // [新增] 默认白色
    int m_fontFace = cv::FONT_HERSHEY_DUPLEX;        // [新增] 默认字体
};

// =========================================================
// 6. 背景模糊 (彻底移除循环，矩阵加速)
// =========================================================
class BackgroundBlurEffect : public IEffect
{
public:
    void process(cv::Mat& img) override;
    void setParameter(std::string const& key, QVariant const& value) override;

private:
    int m_blurRadius = 0;
    // 缓存
    cv::Mat m_blurred;
    cv::Mat m_mask;
    int m_lastCols = 0, m_lastRows = 0;  // 检测尺寸变化
    std::vector<cv::Mat> m_channels;     // 通道分离缓存
};

// =========================================================
// 7. 滤镜特效
// =========================================================
class FilterEffect : public IEffect
{
public:
    void process(cv::Mat& img) override;
    void setParameter(std::string const& key, QVariant const& value) override;

private:
    int m_type = 0;
    std::vector<cv::Mat> m_channels;  // 缓存
};
