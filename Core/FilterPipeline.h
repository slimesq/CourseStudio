// FilterPipeline.h
#pragma once
#include "DX11Context.h"
#include "DX11Filters.h"
#include <vector>
#include <mutex>

class FilterPipeline
{
public:
    FilterPipeline() = default;
    ~FilterPipeline()
    {
        cleanup();
    }

    void init(int width, int height)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_width = width;
        m_height = height;

        // 释放旧资源
        m_pingTex.Reset();
        m_pingSRV.Reset();
        m_pingRTV.Reset();
        m_pongTex.Reset();
        m_pongSRV.Reset();
        m_pongRTV.Reset();

        // 创建新资源
        initBuffers();  // 内部调用 createOffscreenCanvas 使用新的 m_width/height

        // 通知所有滤镜 Resize
        for (auto& f : m_filters)
        {
            f->resize(width, height);
        }
    }

    // 动态添加滤镜
    void addFilter(std::shared_ptr<DX11FilterBase> filter)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (filter->init(DX11Context::instance().device()))
        {
            filter->resize(m_width, m_height);
            m_filters.push_back(filter);
        }
    }

    // 查找并设置参数
    void setFilterParam(QString const& filterName, QString const& key, QVariant const& val)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& f : m_filters)
        {
            if (f->name() == filterName)
            {
                f->setParameter(key, val);
                break;
            }
        }
    }

    // 核心：处理链
    // inputTex: 原始输入
    // outputTex: 最终输出 (可以是同一个，但建议分开)
    ID3D11ShaderResourceView* process(ID3D11ShaderResourceView* inputSRV)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 1. [Bypass] 如果没有开启任何滤镜，直接透传原始画面，性能损耗为 0
        if (m_filters.empty())
        {
            return inputSRV;
        }

        auto ctx = DX11Context::instance().context();

        // 2. 准备 Ping-Pong 资源
        // 假设 m_pingSRV/RTV 和 m_pongSRV/RTV 已经在 resize/init 中创建好
        // inputSRV -> [Filter 0] -> Ping
        // Ping     -> [Filter 1] -> Pong
        // Pong     -> [Filter 2] -> Ping ...

        ID3D11ShaderResourceView* currentInput = inputSRV;
        ID3D11RenderTargetView* currentTarget = m_pingRTV.Get();
        ID3D11ShaderResourceView* currentOutputSRV = m_pingSRV.Get();

        for (size_t i = 0; i < m_filters.size(); ++i)
        {
            // 决定目标缓冲区
            bool isEven = (i % 2 == 0);
            currentTarget = isEven ? m_pingRTV.Get() : m_pongRTV.Get();

            // 执行滤镜处理
            m_filters[i]->process(ctx, currentInput, currentTarget);

            // 更新下一次的输入为当前的输出
            currentInput = isEven ? m_pingSRV.Get() : m_pongSRV.Get();

            // 记录最终输出
            currentOutputSRV = currentInput;
        }

        // 解绑 SRV 以防止 D3D 警告（Input 和 Output 不能绑定同一个资源）
        ID3D11ShaderResourceView* nullSRV[] = {nullptr};
        ctx->PSSetShaderResources(0, 1, nullSRV);

        return currentOutputSRV;
    }

    void resize(int w, int h)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_width = w;
        m_height = h;
        initBuffers();
        for (auto& f : m_filters)
            f->resize(w, h);
    }

    // [新增] 移除指定名称的滤镜
    void removeFilter(QString const& name)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_filters.begin(); it != m_filters.end();)
        {
            if ((*it)->name() == name)
            {
                it = m_filters.erase(it);  // 删除并获取下一个迭代器
            }
            else
            {
                ++it;
            }
        }
    }

    // [新增] 检查滤镜是否存在
    bool hasFilter(QString const& name)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& f : m_filters)
        {
            if (f->name() == name)
                return true;
        }
        return false;
    }

private:
    void initBuffers()
    {
        // 创建 m_pingTex, m_pongTex 及其 RTV/SRV
        // 必须是 D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE
        auto& dx = DX11Context::instance();
        dx.createOffscreenCanvas(m_width, m_height, &m_pingRTV, &m_pingSRV, &m_pingTex);
        dx.createOffscreenCanvas(m_width, m_height, &m_pongRTV, &m_pongSRV, &m_pongTex);
    }

    // 辅助：Lazy 创建 View
    void ensureSRV(ID3D11Texture2D* tex, ComPtr<ID3D11ShaderResourceView>& srv)
    {
        if (!srv)
            DX11Context::instance().device()->CreateShaderResourceView(tex, nullptr, &srv);
    }
    void ensureRTV(ID3D11Texture2D* tex, ComPtr<ID3D11RenderTargetView>& rtv)
    {
        if (!rtv)
            DX11Context::instance().device()->CreateRenderTargetView(tex, nullptr, &rtv);
    }

    void cleanup()
    {
        m_filters.clear();
        m_pingTex.Reset();
        m_pingSRV.Reset();
        m_pingRTV.Reset();
        m_pongTex.Reset();
        m_pongSRV.Reset();
        m_pongRTV.Reset();
    }

    int m_width, m_height;
    std::mutex m_mutex;
    std::vector<std::shared_ptr<DX11FilterBase>> m_filters;

    ComPtr<ID3D11Texture2D> m_pingTex, m_pongTex;
    ComPtr<ID3D11RenderTargetView> m_pingRTV, m_pongRTV;
    ComPtr<ID3D11ShaderResourceView> m_pingSRV, m_pongSRV;

    // 缓存外部纹理的 View
    ComPtr<ID3D11ShaderResourceView> m_inputSRV;
    ComPtr<ID3D11RenderTargetView> m_outputRTV;
};
