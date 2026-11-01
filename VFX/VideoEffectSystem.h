#pragma once
#include <opencv2/opencv.hpp>
#include <memory>
#include <map>
#include <vector>
#include <string>
#include <mutex>
#include <QVariant>  // 使用 Qt 的 QVariant 传递通用参数
#include <QDebug>
#include "FaceTracker.h"

// =========================================================
// [1] 抽象特效接口 (Interface)
// =========================================================
class IEffect
{
public:
    virtual ~IEffect() = default;

    // 核心处理函数 (输入 BGR 格式，方便算法处理)
    virtual void process(cv::Mat& img) = 0;

    // 通用参数设置接口 (Key-Value)
    virtual void setParameter(std::string const& key, QVariant const& value) = 0;

    virtual void setFaceData(FaceData const& face)
    {
    }

    // 开关控制
    void setEnabled(bool enabled)
    {
        m_enabled = enabled;
    }
    bool isEnabled() const
    {
        return m_enabled;
    }

protected:
    bool m_enabled = false;
};

// =========================================================
// [2] 特效管理器 (Controller)
// =========================================================
class EffectManager
{
public:
    EffectManager();
    ~EffectManager() = default;

    // 注册特效 (热插入)
    // order: 执行顺序 (0最先执行，例如磨皮在前，锐化在后)
    void registerEffect(std::string const& name, std::shared_ptr<IEffect> effect, int order);

    // 移除特效 (热拔出)
    void removeEffect(std::string const& name);

    // 统一控制接口：启用/禁用特效
    void setEffectEnabled(std::string const& name, bool enabled);

    // 统一参数接口：设置某个特效的具体参数
    void setEffectParam(std::string const& name,
                        std::string const& paramKey,
                        QVariant const& value);

    // 核心处理流水线
    // 输入: BGRA 原始内存
    void processPipeline(uint8_t* data, int width, int height, int stride);

    void initFaceTracker(std::string const& detPath);
    bool isEffectEnabled(std::string const& name);

private:
    struct EffectNode
    {
        std::string name;
        std::shared_ptr<IEffect> effect;
        int order;
    };

    std::mutex m_mutex;
    std::vector<EffectNode> m_pipeline;  // 有序的特效链

    // 辅助 buffer，避免重复申请内存
    cv::Mat m_matBGR;

    std::shared_ptr<FaceTracker> m_faceTracker;
    int m_frameCounter = 0;  // 用于隔帧检测
};

class EffectConfigurator
{
public:
    // 配置默认的直播特效流水线
    static void configureDefaultPipeline(std::shared_ptr<EffectManager> manager);
};
