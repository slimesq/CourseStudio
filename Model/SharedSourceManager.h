#pragma once
#include <mutex>
#include <map>
#include <memory>
#include <string>
#include <QDebug>
#include "ISource.h"
#include "SourceFactory.h"

class SharedSourceManager
{
public:
    static SharedSourceManager& instance()
    {
        static SharedSourceManager s;
        return s;
    }

    // 获取或创建源（享元模式入口）
    std::shared_ptr<ISource> acquireSource(SourceType type,
                                           std::string const& id,
                                           std::string const& name)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        qDebug() << "[SharedSourceManager] Requesting Source. Input ID:" << id.c_str()
                 << " Name:" << name.c_str();

        // 1. 尝试从缓存获取
        // 对于摄像头、WGC等设备，ID 是唯一的。对于图片，路径即 ID。
        // 如果 ID 为空（例如文本源），我们生成一个临时的 Key 或跳过缓存直接创建（视业务而定）
        // 这里假设 ID 必须有效才能复用。
        std::string key = id;

        if (!key.empty())
        {
            auto it = m_sources.find(key);
            if (it != m_sources.end())
            {
                // 检查 weak_ptr 是否有效 (如果是 map 存的是 shared_ptr 则一直有效，这里我们存 shared_ptr 以实现缓存)
                // 如果需要更激进的内存回收，可以存 weak_ptr，这里按需求做"懒加载且复用"，所以持有 shared_ptr
                qDebug() << "[SharedSourceManager] Hit Cache for:" << key.c_str();
                return it->second;
            }
        }

        // 2. 创建新实例
        qDebug() << "[SharedSourceManager] Creating New Source:" << id.c_str();
        auto source = SourceFactory::createSource(type, id, name);

        if (source)
        {
            // 确保 ID 一致
            if (source->getSourceId().empty() && !key.empty())
            {
                source->setSourceId(key);
            }
            std::string finalKey = source->getSourceId();
            if (!finalKey.empty())
            {
                m_sources[finalKey] = source;
            }
        }
        return source;
    }

    // 可选：清理不再使用的源 (垃圾回收)
    // 可以在内存紧张或关闭项目时调用
    void garbageCollect()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_sources.begin(); it != m_sources.end();)
        {
            // use_count == 1 说明只有 SharedSourceManager 持有它，没有 SceneItem 在使用
            if (it->second.use_count() == 1)
            {
                qDebug() << "[SharedSourceManager] GC Releasing (Unused):" << it->first.c_str();
                // 注意：不要显式调用 stop()，让 shared_ptr 的析构函数去处理，
                // 这样如果其他地方意外持有（虽然 use_count=1 不太可能），也不会导致野指针逻辑错误
                it = m_sources.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void releaseSourceAsync(std::shared_ptr<ISource> source)
    {
        if (!source)
            return;
        std::string id = source->getSourceId();
        if (id.empty())
            return;

        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_sources.find(id);
        if (it != m_sources.end() && it->second == source)
        {
            // 只有当引用计数表明它是最后一个使用者时（Manager + Caller = 2），才考虑清理
            // 但为了安全起见，我们只做 Map 移除，绝不强制 Stop。
            // 真正的 Stop 会在所有 shared_ptr 销毁时由析构函数触发。
            if (source.use_count() <= 2)
            {
                qDebug() << "[SharedSourceManager] Removing from cache:" << id.c_str();
                m_sources.erase(it);
            }
            else
            {
                qDebug() << "[SharedSourceManager] Release requested but Source is shared. Keeping "
                            "alive. Ref:"
                         << source.use_count();
            }
        }
    }

private:
    SharedSourceManager() = default;
    ~SharedSourceManager() = default;

    // 禁止拷贝
    SharedSourceManager(SharedSourceManager const&) = delete;
    SharedSourceManager& operator=(SharedSourceManager const&) = delete;

    std::map<std::string, std::shared_ptr<ISource>> m_sources;
    std::mutex m_mutex;
};
