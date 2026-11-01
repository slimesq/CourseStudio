#pragma once
#include <QObject>
#include <QMap>
#include <QString>
#include <QStandardPaths>
#include <QDir>
#include <QSet>
#include <QNetworkReply>
#include <QDebug>
#include <QUrl>
#include <QFileInfo>
#include "HttpManager.h"

class AssetCacheManager : public QObject
{
    Q_OBJECT
public:
    static AssetCacheManager& instance()
    {
        static AssetCacheManager s;
        return s;
    }

    ~AssetCacheManager()
    {
        cleanupTemp();
    }

    void init()
    {
        m_persistentDir =
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/media_cache";
        QDir().mkpath(m_persistentDir);

        m_tempDir =
            QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/LiveStudio_Thumbs";
        cleanupTemp();
        QDir().mkpath(m_tempDir);

        qDebug() << "[AssetCacheManager] Init. Persistent:" << m_persistentDir
                 << " Temp:" << m_tempDir;
    }

    // Get path with optional forced name
    QString getLocalPath(QString const& url, QString const& forcedName = "")
    {
        return getFileInternal(url, m_persistentDir, false, forcedName);
    }

    QString getThumbnailPath(QString const& url, QString const& forcedName = "")
    {
        if (url.isEmpty())
            return "";
        return getFileInternal(url, m_tempDir, true, forcedName);
    }

    // Query if exists
    QString queryLocalPath(QString const& url, QString const& forcedName = "")
    {
        if (url.isEmpty())
            return "";

        QString fileName;
        if (!forcedName.isEmpty())
            fileName = forcedName;
        else
            fileName = getFileNameFromUrl(url);

        QString localPath = m_persistentDir + "/" + fileName;

        if (QFile::exists(localPath) && QFileInfo(localPath).size() > 0)
        {
            return localPath;
        }
        return "";
    }

    // Trigger download/cache
    void cacheAsset(QString const& url, bool isThumbnail = false, QString const& forcedName = "")
    {
        if (url.isEmpty())
            return;
        if (isThumbnail)
        {
            getFileInternal(url, m_tempDir, true, forcedName);
        }
        else
        {
            getFileInternal(url, m_persistentDir, false, forcedName);
        }
    }

    // Explicit alias for clarity
    void cacheAssetWithName(QString const& url, QString const& forcedName)
    {
        cacheAsset(url, false, forcedName);
    }

    QString getPersistentDir() const
    {
        return m_persistentDir;
    }

signals:
    void assetReady(QString const& url, QString const& localPath, bool isTemp);

private:
    QString getFileNameFromUrl(QString const& url)
    {
        if (!url.startsWith("http"))
            return QFileInfo(url).fileName();
        QUrl qUrl(url);
        QString fileName = qUrl.fileName();
        if (fileName.isEmpty())
            fileName = "unknown_asset.dat";
        return fileName;
    }

    QString getFileInternal(QString const& url,
                            QString const& dirPath,
                            bool isTemp,
                            QString const& forcedName)
    {
        if (url.isEmpty())
            return "";

        if (QFile::exists(url))
        {
            return url;
        }

        QString filename = forcedName.isEmpty() ? getFileNameFromUrl(url) : forcedName;
        QString localPath = dirPath + "/" + filename;

        // 1. Check Cache
        if (QFile::exists(localPath) && QFileInfo(localPath).size() > 0)
        {
            return localPath;
        }

        // 2. Check Pending
        // [Key] If we are already downloading this URL, we wait.
        // Issue: If previously started without forcedName, it might save to wrong file.
        // Fix: We can't easily change in-flight download target.
        // We assume consistent usage of forcedName across the app.
        if (m_pendingDownloads.contains(url))
        {
            return "";
        }

        // 3. Download
        m_pendingDownloads.insert(url);
        qDebug() << "[AssetCacheManager] Downloading:" << url << " -> " << filename
                 << " (Forced:" << !forcedName.isEmpty() << ")";
        startDownload(url, localPath, isTemp);

        return "";
    }

    void startDownload(QString const& url, QString const& localPath, bool isTemp)
    {
        auto reply = HttpManager::instance()->downloadStream(url, localPath);

        connect(reply, &QNetworkReply::finished, this, [this, url, localPath, isTemp, reply]() {
            m_pendingDownloads.remove(url);

            if (reply->error() == QNetworkReply::NoError)
            {
                if (QFile::exists(localPath) && QFileInfo(localPath).size() > 0)
                {
                    qDebug() << "[AssetCacheManager] Downloaded:" << localPath;
                    emit assetReady(url, localPath, isTemp);
                }
                else
                {
                    qWarning() << "[AssetCacheManager] Empty file:" << localPath;
                    QFile::remove(localPath);
                }
            }
            else
            {
                qWarning() << "[AssetCacheManager] Download Failed:" << url
                           << " Error:" << reply->errorString();
                QFile::remove(localPath);
            }
            reply->deleteLater();
        });
    }

    void cleanupTemp()
    {
        QDir dir(m_tempDir);
        if (dir.exists())
        {
            dir.removeRecursively();
        }
    }

    QString m_persistentDir;
    QString m_tempDir;
    QSet<QString> m_pendingDownloads;
};
