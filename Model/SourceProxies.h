#pragma once
#include <QObject>
#include "IVideoSource.h"
#include "ImageFileSource.h"
#include <QTimer>
#include <QDateTime>
#include <memory>
#include <QDebug>
#include <atomic>
#include <QDateTime>
#include <QFileInfo>
#include <mutex>
#include "AssetCacheManager.h"  // 必须引用，用于监听下载状态

// --- Automatic Lifecycle Management & Async Loading Proxy ---
// 状态代理模式：内部自动处理从“下载中”到“已加载”的转换，外部无需感知
class ImageSourceProxy : public QObject, public IVideoSource
{
    Q_OBJECT
public:
    ImageSourceProxy(std::string const& pathOrUrl,
                     std::string const& id,
                     std::string const& name = "")
        : QObject(nullptr), m_pathOrUrl(QString::fromStdString(pathOrUrl))
    {
        setSourceId(id);

        if (!name.empty())
        {
            m_customName = name;
        }
        else
        {
            QString fileName = QFileInfo(m_pathOrUrl).fileName();
            if (!fileName.isEmpty())
                m_customName = fileName.toStdString();
            else
                m_customName = "Image Proxy";
        }

        // 1. 初始化自动卸载定时器
        m_idleTimer = new QTimer(this);
        m_idleTimer->setInterval(5000);  // 5秒无渲染请求则卸载内存
        m_idleTimer->setSingleShot(true);
        connect(m_idleTimer, &QTimer::timeout, this, &ImageSourceProxy::unloadRealSource);

        // 2. 检查本地缓存或监听下载
        checkLocalOrWait();
    }

    ~ImageSourceProxy() override
    {
        // 析构时确保释放资源
        std::lock_guard<std::mutex> lock(m_mutex);
        m_realSource.reset();
    }

    // --- IVideoSource Interface ---

    QString getDeviceName() const override
    {
        return QString::fromStdString(m_customName);
    }

    void getSize(int& w, int& h) const override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_status == Status::Ready && m_realSource)
        {
            m_realSource->getSize(w, h);
        }
        else
        {
            // 下载中或失败时的默认尺寸 (16:9 比例)
            w = 1920 / 2;
            h = 1080 / 2;
        }
    }

    // [Core Rendering Logic]
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> getTexture(ID3D11DeviceContext* ctx) override
    {
        if (!m_enabled)
            return nullptr;

        // Reset idle timer on UI thread (Thread Affinity safety)
        QMetaObject::invokeMethod(m_idleTimer, "start", Qt::QueuedConnection);

        std::lock_guard<std::mutex> lock(m_mutex);  // Critical Section

        // Case A: 已就绪，但为了节省内存被卸载了 -> 重新加载
        if (m_status == Status::Ready && !m_realSource)
        {
            // 尝试重新加载本地文件
            loadRealSourceInternal();
        }

        // Case B: 有真实源 -> 返回纹理
        if (m_realSource)
        {
            return m_realSource->getTexture(ctx);
        }

        // Case C: 下载中 -> 返回空 (或者可以在这里集成一个静态的 Loading 纹理)
        return nullptr;
    }

    // --- ISource Interface ---

    bool start() override
    {
        m_enabled = true;
        return true;
    }

    void stop() override
    {
        m_enabled = false;
        // Trigger unload on UI thread to respect timer affinity
        QMetaObject::invokeMethod(this, "unloadRealSource", Qt::QueuedConnection);
    }

    SourceType getType() const override
    {
        return SourceType::Image;
    }

    // [New] 强制重新检查路径 (用于某些特殊的热更场景)
    void updatePath(QString const& newPath)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_pathOrUrl != newPath)
        {
            m_pathOrUrl = newPath;
            m_status = Status::Downloading;  // 重置状态
            m_realSource.reset();
            // 在主线程执行检查逻辑
            QMetaObject::invokeMethod(this, "checkLocalOrWait", Qt::QueuedConnection);
        }
    }

public slots:
    // [Logic] 检查文件是否存在，不存在则监听下载信号
    void checkLocalOrWait()
    {
        QFileInfo fileInfo(m_pathOrUrl);
        if (fileInfo.exists() && fileInfo.isFile())
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_localAbsPath = m_pathOrUrl;  // 直接使用原始路径
            m_status = Status::Ready;
            loadRealSourceInternal();
            return;
        }

        // 1. 尝试从缓存管理器获取本地绝对路径
        // 如果是 URL，它会返回缓存路径；如果是本地路径，直接返回
        QString localPath = AssetCacheManager::instance().getLocalPath(m_pathOrUrl);

        // 2. 检查文件是否存在
        if (QFile::exists(localPath) && QFileInfo(localPath).size() > 0)
        {
            // 命中缓存：直接标记为就绪，等待下一次 getTexture 时懒加载
            std::lock_guard<std::mutex> lock(m_mutex);
            m_localAbsPath = localPath;
            m_status = Status::Ready;
            // 立即加载一次以获取尺寸信息(可选)
            loadRealSourceInternal();
        }
        else
        {
            // 3. 未命中：状态设为下载中，并连接信号
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_status = Status::Downloading;
            }

            // 使用 UniqueConnection 防止重复连接
            connect(&AssetCacheManager::instance(),
                    &AssetCacheManager::assetReady,
                    this,
                    &ImageSourceProxy::onAssetReady,
                    Qt::UniqueConnection);

            // 注意：这里不需要主动触发下载，因为上层的 UI (LiveCourseSelectWidget)
            // 或者 AssetManager 通常已经触发了下载。
            // 如果需要由 Proxy 触发下载，可以调用:
            // AssetCacheManager::instance().cacheAsset(m_pathOrUrl, false);
        }
    }

private slots:
    void unloadRealSource()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_realSource)
        {
            // qDebug() << "[ImageProxy] Unloading idle source RAM:" << m_customName.c_str();
            m_realSource.reset();  // Release shared_ptr safely
        }
    }

    // 响应资源下载完成信号
    void onAssetReady(QString const& url, QString const& localPath, bool isTemp)
    {
        Q_UNUSED(isTemp);
        // 检查是否是本 Proxy 关注的 URL
        if (url == m_pathOrUrl)
        {
            qDebug() << "[ImageProxy] Asset Downloaded for:" << m_customName.c_str();

            std::lock_guard<std::mutex> lock(m_mutex);
            m_localAbsPath = localPath;
            m_status = Status::Ready;

            // 立即加载，这样下一帧渲染就能看到图片
            loadRealSourceInternal();

            // 断开连接，避免后续无关信号干扰
            disconnect(&AssetCacheManager::instance(),
                       &AssetCacheManager::assetReady,
                       this,
                       &ImageSourceProxy::onAssetReady);
        }
    }

private:
    // Helper called within lock
    void loadRealSourceInternal()
    {
        if (m_localAbsPath.isEmpty())
            return;

        auto img = std::make_shared<ImageFileSource>();
        if (img->loadFile(m_localAbsPath))
        {
            img->setSourceId(m_storedId);
            img->start();
            m_realSource = img;
            // qDebug() << "[ImageProxy] Real source loaded:" << m_localAbsPath;
        }
        else
        {
            qWarning() << "[ImageProxy] Failed to load file:" << m_localAbsPath;
            m_status = Status::Error;
        }
    }

    enum class Status
    {
        Downloading,
        Ready,
        Error
    };

    QString m_pathOrUrl;     // 原始 URL 或 路径
    QString m_localAbsPath;  // 下载完成后的本地绝对路径

    bool m_enabled = true;
    Status m_status = Status::Downloading;  // 默认为下载中

    std::shared_ptr<ImageFileSource> m_realSource;
    QTimer* m_idleTimer;
    mutable std::mutex m_mutex;
};
