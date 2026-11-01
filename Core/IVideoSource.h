#pragma once
#define NOMINMAX
#include "ISource.h"
#include "DX11Context.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <QString>
#include <atomic>

// 视频源基类：连接 ISource (业务层) 和 具体实现 (硬件层)
class IVideoSource : public ISource
{
public:
    virtual ~IVideoSource() = default;

    // --- 实现 ISource 的通用接口 ---

    std::string getName() const override
    {
        return getDeviceName().toStdString();
    }

    TextureHandle getTextureHandle() override
    {
        ID3D11DeviceContext* ctx = DX11Context::instance().context();
        if (!ctx)
            return nullptr;
        auto srv = getTexture(ctx);
        if (srv)
        {
            return (TextureHandle)srv.Get();
        }
        return nullptr;
    }

    float getBaseWidth() const override
    {
        int w = 0, h = 0;
        getSize(w, h);
        return static_cast<float>(w);
    }

    float getBaseHeight() const override
    {
        int w = 0, h = 0;
        getSize(w, h);
        return static_cast<float>(h);
    }

    void setSourceId(std::string const& id)
    {
        m_id = id;
    }
    std::string getSourceId() const override
    {
        return m_id;
    }

    void setSourceType(SourceType type)
    {
        m_type = type;
    }
    SourceType getType() const override
    {
        return m_type;
    }

    // --- 优化点 5: 脏矩形检测接口 ---
    // 返回 true 表示自上次渲染以来有新帧到达
    // 默认返回 true 保证静态图片或模拟源总是被渲染，子类可覆盖优化
    virtual bool hasNewFrame() const
    {
        return true;
    }

    // 渲染完成后调用，通知源重置新帧标志
    virtual void markFrameProcessed()
    {
    }

    // --- 子类必须实现的底层接口 (Hardware Layer) ---

    virtual QString getDeviceName() const = 0;
    virtual Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> getTexture(
        ID3D11DeviceContext* ctx) = 0;
    virtual void getSize(int& w, int& h) const = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;

private:
    std::string m_id;
    SourceType m_type = SourceType::Video;
};
