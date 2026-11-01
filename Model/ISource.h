#pragma once
#include <string>
#include <atomic>  // [New]
#include "Types.h"
#include <future>
#include <QtConcurrent>
#include <QThreadPool>

// 采集源顶层接口 (UI/业务逻辑层使用)
class ISource : public std::enable_shared_from_this<ISource>
{
public:
    virtual ~ISource() = default;

    // 获取通用名称 (UI显示用)
    virtual std::string getName() const = 0;

    // 获取源类型
    virtual SourceType getType() const = 0;

    // 获取唯一标识
    virtual std::string getSourceId() const = 0;

    // 基础属性设置
    virtual void setSourceId(std::string const& id)
    {
        m_storedId = id;
    }

    virtual void setSourceName(std::string const& name)
    {
        m_customName = name;
    }

    std::string getStoredCustomName() const
    {
        return m_customName;
    }
    std::string getStoredId() const
    {
        return m_storedId;
    }

    // --- [Modified] 生命周期控制 ---

    // 底层启动接口 (通常由 activate 调用)
    virtual bool start() = 0;

    // 底层停止接口 (通常由 deactivate 调用)
    virtual void stop() = 0;

    // [New] 激活：申请硬件资源、启动解码线程
    virtual void activate()
    {
        retain();  // 改为调用引用计数增加
    }

    // [New] 休眠：释放硬件资源、暂停解码线程
    virtual void deactivate()
    {
        release();  // 改为调用引用计数减少
    }

    // [New] 引用计数机制：请求使用资源
    void retain()
    {
        std::lock_guard<std::mutex> lock(m_opMutex);  // 加锁防止竞态
        if (m_activeRefCount.fetch_add(1) == 0)
        {
            // 0 -> 1: 启动
            // 如果之前有正在进行的异步 stop，这里其实很危险。
            // 但加上锁之后，start 会等待之前的操作完成（如果 stop 也在锁里）。
            // 然而我们希望 stop 异步且不阻塞。

            // 策略：start 必须同步执行以确保画面立即出来。
            // 如果此时硬件正在关闭中，start 可能会失败或需要重置。
            // 大多数 Capture 实现（如 WGC/OpenCV）支持重入或快速失败。
            this->start();
        }
    }
    // [New] 引用计数机制：释放资源
    void release()
    {
        if (m_activeRefCount.fetch_sub(1) == 1)
        {
            this->stop();  // 只有当这是最后一个引用者时，才关闭硬件/释放资源
        }
    }
    // 获取纹理句柄
    virtual TextureHandle getTextureHandle() = 0;

    // 基础尺寸
    virtual float getBaseWidth() const = 0;
    virtual float getBaseHeight() const = 0;

    // 帧同步接口
    virtual bool hasNewFrame() const
    {
        return true;
    }

    // 渲染完成后调用，通知源重置新帧标志
    virtual void markFrameProcessed()
    {
    }

protected:
    std::string m_storedId;
    std::string m_customName;

    // [New] 活跃引用计数 (原子操作确保线程安全)
    std::atomic<int> m_activeRefCount{0};

    std::mutex m_opMutex;
};

// --- 以下模拟类保持不变 ---

class DesktopSource : public ISource
{
public:
    DesktopSource(std::string const& id)
    {
        setSourceId(id);
    }
    std::string getName() const override
    {
        return m_customName.empty() ? ("Display: " + m_storedId) : m_customName;
    }
    SourceType getType() const override
    {
        return SourceType::ScreenCapture_WGC;
    }
    std::string getSourceId() const override
    {
        return m_storedId;
    }
    bool start() override
    {
        return true;
    }
    void stop() override
    {
    }
    TextureHandle getTextureHandle() override
    {
        return nullptr;
    }
    float getBaseWidth() const override
    {
        return 1920.0f;
    }
    float getBaseHeight() const override
    {
        return 1080.0f;
    }
};

class CameraSource : public ISource
{
public:
    CameraSource(std::string const& id)
    {
        setSourceId(id);
    }
    std::string getName() const override
    {
        return m_customName.empty() ? ("Cam: " + m_storedId) : m_customName;
    }
    SourceType getType() const override
    {
        return SourceType::Camera;
    }
    std::string getSourceId() const override
    {
        return m_storedId;
    }
    bool start() override
    {
        return true;
    }
    void stop() override
    {
    }
    TextureHandle getTextureHandle() override
    {
        return nullptr;
    }
    float getBaseWidth() const override
    {
        return 1280.0f;
    }
    float getBaseHeight() const override
    {
        return 720.0f;
    }
};

class ImageSource : public ISource
{
public:
    ImageSource(std::string const& path)
    {
        setSourceId(path);
    }
    std::string getName() const override
    {
        return m_customName.empty() ? ("Img: " + m_storedId) : m_customName;
    }
    SourceType getType() const override
    {
        return SourceType::Image;
    }
    std::string getSourceId() const override
    {
        return m_storedId;
    }
    bool start() override
    {
        return true;
    }
    void stop() override
    {
    }
    TextureHandle getTextureHandle() override
    {
        return nullptr;
    }
    float getBaseWidth() const override
    {
        return 500.0f;
    }
    float getBaseHeight() const override
    {
        return 500.0f;
    }
};
