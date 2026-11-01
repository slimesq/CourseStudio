// TexturePool.cpp
#include "TexturePool.h"
#include "DX11Context.h"
#include "AppConfig.h"
#include "Logger.h"
#include <QDateTime>
#include <algorithm>

using Microsoft::WRL::ComPtr;

class SwapChainStrategy : public ITextureBufferStrategy
{
public:
    void push(ID3D11Texture2D* texture) override
    {
        if (!texture)
            return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_frontBuffer = texture;  // ComPtr AddRef
    }

    // [Fix] 返回 ComPtr，引用计数 +1，确保由调用者持有期间不被释放
    ComPtr<ID3D11Texture2D> peek() override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_frontBuffer;
    }

    void clear() override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_frontBuffer.Reset();
    }

private:
    std::mutex m_mutex;
    ComPtr<ID3D11Texture2D> m_frontBuffer;
};

class RingBufferStrategy : public ITextureBufferStrategy
{
public:
    RingBufferStrategy()
    {
        m_capacity = AppConfig::instance()->ringBufferSize();
        if (m_capacity < 1)
            m_capacity = 3;
    }

    void push(ID3D11Texture2D* texture) override
    {
        if (!texture)
            return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_buffer.push_back(texture);
        if (m_buffer.size() > m_capacity)
        {
            m_buffer.erase(m_buffer.begin());  // 移除最旧的
        }
    }

    ComPtr<ID3D11Texture2D> peek() override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_buffer.empty())
            return nullptr;
        return m_buffer.back();
    }

    void clear() override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_buffer.clear();
    }

private:
    std::mutex m_mutex;
    std::vector<ComPtr<ID3D11Texture2D>> m_buffer;
    size_t m_capacity;
};

TexturePool& TexturePool::instance()
{
    static TexturePool s;
    return s;
}

void TexturePool::updateTexture(std::string const& sourceId,
                                ID3D11Texture2D* texture,
                                BufferStrategyType strategy)
{
    if (sourceId.empty() || !texture)
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    long long now = QDateTime::currentMSecsSinceEpoch();

    auto it = m_pool.find(sourceId);
    if (it == m_pool.end())
    {
        PoolNode node;
        node.lastAccessTime = now;
        node.strategyType = strategy;
        if (strategy == BufferStrategyType::SwapChain)
        {
            node.buffer = std::make_shared<SwapChainStrategy>();
        }
        else
        {
            node.buffer = std::make_shared<RingBufferStrategy>();
        }
        m_pool[sourceId] = node;
        it = m_pool.find(sourceId);
    }

    if (it != m_pool.end() && it->second.buffer)
    {
        it->second.lastAccessTime = now;
        it->second.buffer->push(texture);
    }
}

// [Fix] 返回 ComPtr
ComPtr<ID3D11Texture2D> TexturePool::acquireTexture(std::string const& sourceId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_pool.find(sourceId);
    if (it != m_pool.end() && it->second.buffer)
    {
        it->second.lastAccessTime = QDateTime::currentMSecsSinceEpoch();
        return it->second.buffer->peek();  // 安全拷贝
    }
    return nullptr;
}

void TexturePool::removeSource(std::string const& sourceId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pool.erase(sourceId);
}

void TexturePool::removeTexture(std::string const& sourceId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_pool.find(sourceId);
    if (it != m_pool.end())
    {
        if (it->second.buffer)
        {
            it->second.buffer->clear();  // 清空内部引用
        }
        m_pool.erase(it);
    }
}

void TexturePool::cleanupUnused(int64_t currentTimeMs)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto it = m_pool.begin(); it != m_pool.end();)
    {
        // 检查最后访问时间
        if (currentTimeMs - it->second.lastAccessTime > 5000)
        {  // 5秒超时
            // qInfo() << "[TexturePool] Auto-releasing texture: " << it->first.c_str();

            // 安全释放
            it->second.buffer.reset();  // 释放 ID3D11Texture2D
            it = m_pool.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
