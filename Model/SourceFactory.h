#pragma once
#include <memory>
#include <string>
#include <QDebug>
#include <QString>
#include <QFileInfo>
#include <QUrl>
#include "ISource.h"
#include "CameraCapture.h"
#include "WGCCapture.h"
#include "SourceProxies.h"
#include "AppUtils.h"
#include "SourceProvider.h"
#include "AssetCacheManager.h"
#include "VideoEffectSystem.h"

class PlaceholderSource : public ISource
{
public:
    PlaceholderSource(std::string const& id)
    {
        setSourceId(id);
        m_customName = "Downloading...";
    }
    std::string getName() const override
    {
        return m_customName;
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
        return 1920.0f / 2;
    }
    float getBaseHeight() const override
    {
        return 1080.0f / 2;
    }
};

class SourceFactory
{
public:
    static std::shared_ptr<ISource> createSource(SourceType type,
                                                 std::string const& stdIdOrName,
                                                 std::string const& fallbackName = "")
    {
        std::shared_ptr<ISource> result = nullptr;

        std::string finalId = stdIdOrName;
        QString idOrName = QString::fromStdString(stdIdOrName);
        QString nameHint = QString::fromStdString(fallbackName);

        SourceType correctedType = type;
        if (idOrName.startsWith("win:"))
        {
            if (type != SourceType::ScreenCapture_WGC)
                correctedType = SourceType::ScreenCapture_WGC;
        }
        else if (idOrName.startsWith("mon:"))
        {
            if (type != SourceType::Monitor)
                correctedType = SourceType::Monitor;
        }

        switch (correctedType)
        {
            case SourceType::Camera:
            {
                auto cam = std::make_shared<CameraCapture>();
                cam->configure(idOrName, DX11Context::instance().device());
                if (cam->getEffectManager())
                {
                    EffectConfigurator::configureDefaultPipeline(cam->getEffectManager());
                }
                result = cam;
                break;
            }
            case SourceType::ScreenCapture_WGC:
            {
                auto wgc = std::make_shared<WGCCapture>();
                bool isMon = false;
                unsigned long long handleVal =
                    SourceRecoveryHelper::parseHandleId(idOrName, &isMon);
                wgc->setTargetWindow((HWND)handleVal, DX11Context::instance().device());
                result = wgc;
                break;
            }
            case SourceType::Monitor:
            {
                auto wgc = std::make_shared<WGCCapture>();
                bool isMon = true;
                unsigned long long handleVal =
                    SourceRecoveryHelper::parseHandleId(idOrName, &isMon);
                wgc->setTargetMonitor((HMONITOR)handleVal, DX11Context::instance().device());
                result = wgc;
                break;
            }
            case SourceType::Image:
            {
                QString path = idOrName;
                QString cacheDir = AssetCacheManager::instance().getPersistentDir();

                // 1. [Fix] ID Empty? Try resolve from AssetManager
                if (path.isEmpty() && !nameHint.isEmpty())
                {
                    qWarning() << "[SourceFactory] ID missing. resolving name:" << nameHint;

                    QString url = AssetManager::instance().findUrlByName(nameHint);
                    if (!url.isEmpty())
                    {
                        qDebug() << " -> Resolved to URL:" << url;
                        path = url;
                    }
                    else
                    {
                        // Fallback to local check
                        QString tryPath = cacheDir + "/" + nameHint;
                        if (QFile::exists(tryPath))
                            path = tryPath;
                        else if (QFile::exists(nameHint))
                            path = nameHint;
                    }
                }

                // 2. If path is URL -> Download
                if (path.startsWith("http://") || path.startsWith("https://"))
                {
                    // [FIX] Always use nameHint for cache lookup if available
                    QString fileName = !nameHint.isEmpty() ? nameHint : QUrl(path).fileName();
                    QString localPath = cacheDir + "/" + fileName;

                    if (QFile::exists(localPath))
                    {
                        qDebug() << "[SourceFactory] Local Cache Found:" << localPath;
                        auto proxy = std::make_shared<ImageSourceProxy>(
                            localPath.toStdString(), path.toStdString(), nameHint.toStdString());
                        result = proxy;
                    }
                    else
                    {
                        qDebug() << "[SourceFactory] Cache Miss. Downloading:" << path << " as "
                                 << fileName;
                        // [FIX] Always pass nameHint to force correct filename
                        AssetCacheManager::instance().cacheAsset(path, false, fileName);

                        auto placeholder = std::make_shared<PlaceholderSource>(stdIdOrName);
                        placeholder->setSourceName(nameHint.isEmpty() ? "Downloading..."
                                                                      : nameHint.toStdString());
                        result = placeholder;
                    }
                }
                // 3. Path is local file
                else
                {
                    QString cleanPath = QDir::fromNativeSeparators(path);
                    if (QFile::exists(cleanPath) && !QFileInfo(cleanPath).isDir())
                    {
                        // 传递 cleanPath
                        auto proxy = std::make_shared<ImageSourceProxy>(
                            cleanPath.toStdString(), path.toStdString(), nameHint.toStdString());
                        result = proxy;
                    }
                    else
                    {
                        auto placeholder = std::make_shared<PlaceholderSource>(stdIdOrName);
                        if (!nameHint.isEmpty())
                            placeholder->setSourceName(nameHint.toStdString());
                        else
                            placeholder->setSourceName("File Missing");
                        result = placeholder;
                    }
                }
                break;
            }
            default:
                break;
        }

        if (!result)
        {
            auto placeholder = std::make_shared<PlaceholderSource>(stdIdOrName);
            if (!nameHint.isEmpty())
                placeholder->setSourceName(nameHint.toStdString());
            else
                placeholder->setSourceName("Loading...");
            result = placeholder;
        }
        if (result->getSourceId().empty())
        {
            if (!finalId.empty())
            {
                result->setSourceId(finalId);
            }
            else if (!nameHint.isEmpty())
            {
                result->setSourceId(nameHint.toStdString());
            }
            else
            {
                // 如果连名字都没有，给一个唯一标识，防止它们被视为“同一类”空资源
                result->setSourceId("temp_" + QUuid::createUuid().toString().toStdString());
            }
        }

        if (result->getName().empty() && !nameHint.isEmpty())
        {
            result->setSourceName(nameHint.toStdString());
        }

        return result;
    }
};
