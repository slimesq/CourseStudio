// TexturePool.h
#pragma once
#include <mutex>
#include <vector>
#include <map>
#include <memory>
#include <d3d11.h>
#include <wrl/client.h>
#include <string>

enum class BufferStrategyType
{
    SwapChain,
    RingBuffer
};

class ITextureBufferStrategy
{
public:
    virtual ~ITextureBufferStrategy() = default;
    virtual void push(ID3D11Texture2D* texture) = 0;
    // [Fix] 返回 ComPtr 以确保线程安全，防止外部使用时被意外释放
    virtual Microsoft::WRL::ComPtr<ID3D11Texture2D> peek() = 0;
    virtual void clear() = 0;
};

class TexturePool
{
public:
    static TexturePool& instance();

    void updateTexture(std::string const& sourceId,
                       ID3D11Texture2D* texture,
                       BufferStrategyType strategy = BufferStrategyType::SwapChain);

    // [Fix] 返回 ComPtr，利用引用计数延长生命周期
    Microsoft::WRL::ComPtr<ID3D11Texture2D> acquireTexture(std::string const& sourceId);

    void removeSource(std::string const& sourceId);
    void removeTexture(std::string const& sourceId);
    void cleanupUnused(long long currentTimeMs);

private:
    TexturePool() = default;

    struct PoolNode
    {
        std::shared_ptr<ITextureBufferStrategy> buffer;
        long long lastAccessTime = 0;
        BufferStrategyType strategyType;
    };

    std::map<std::string, PoolNode> m_pool;
    std::mutex m_mutex;
};
