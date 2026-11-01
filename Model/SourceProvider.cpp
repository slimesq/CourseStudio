#include "SourceProvider.h"
#include "WGCCapture.h"
#include "CameraCapture.h"
#include "TransferManager.h"
#include "ResourceService.h"
#include <QUuid>
#include <QFileInfo>
#include <QJsonArray>
#include <QDebug>
#include <QTimer>

static QString getPendingTasksPath()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(path);
    return path + "/pending_uploads.json";
}

AssetManager::AssetManager() : QObject(nullptr)
{
    connect(ResourceService::instance(),
            &ResourceService::dataReceived,
            this,
            &AssetManager::onApiData);
    connect(ResourceService::instance(),
            &ResourceService::operationFinished,
            this,
            &AssetManager::onUploadFinished);
    connect(&TransferManager::instance(),
            &TransferManager::transferFinished,
            this,
            &AssetManager::assetUploaded);
    connect(&TransferManager::instance(),
            &TransferManager::transferFinished,
            this,
            [this](QString const& localPath, QString const& cloudUrl) {
                bool changed = false;
                for (auto& asset : m_pendingUploads)
                {
                    if (asset.pathOrId == localPath)
                    {
                        asset.isCloud = true;
                        asset.cloudUrl = cloudUrl;
                        changed = true;
                    }
                }
                if (changed)
                    savePendingTasks();
                emit assetUploaded(localPath, cloudUrl);
            });
    connect(ResourceService::instance(),
            &ResourceService::materialDeleted,
            this,
            &AssetManager::onMaterialDeleted);
    loadPendingTasks();
}

void AssetManager::savePendingTasks()
{
    QJsonArray arr;
    for (auto const& asset : m_pendingUploads)
    {
        QJsonObject obj;
        obj["uuid"] = asset.uuid;
        obj["name"] = asset.name;
        obj["path"] = asset.pathOrId;
        obj["type"] = (int)asset.type;
        obj["thumb"] = asset.thumbnailUrl;
        arr.append(obj);
    }
    QFile file(getPendingTasksPath());
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(QJsonDocument(arr).toJson());
        file.close();
    }
}

void AssetManager::loadPendingTasks()
{
    QFile file(getPendingTasksPath());
    if (file.open(QIODevice::ReadOnly))
    {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        QJsonArray arr = doc.array();
        for (auto const& val : arr)
        {
            QJsonObject obj = val.toObject();
            AssetModel asset;
            asset.uuid = obj["uuid"].toString();
            asset.name = obj["name"].toString();
            asset.pathOrId = obj["path"].toString();
            asset.type = (SourceType)obj["type"].toInt();
            asset.thumbnailUrl = obj["thumb"].toString();
            asset.isCloud = false;
            if (QFile::exists(asset.pathOrId))
                m_pendingUploads.append(asset);
        }
        file.close();
    }
}

void AssetManager::fetchCloudAssets(int mediaType)
{
    m_currentFetchType = mediaType;
    qDebug() << "[AssetManager] Fetching cloud assets. Type:" << mediaType;
    ResourceService::instance()->getMaterialList(1, 100, "", "");
}

AssetModel AssetManager::importLocalFile(QString const& filePath, QString const& groupId)
{
    AssetModel asset;
    asset.uuid = QUuid::createUuid().toString();
    asset.name = QFileInfo(filePath).fileName();
    asset.pathOrId = filePath;
    asset.fileSize = QFileInfo(filePath).size();
    asset.type = SourceType::Image;
    asset.thumbnailUrl = filePath;
    asset.isCloud = false;

    m_pendingUploads.append(asset);
    savePendingTasks();
    TransferManager::instance().enqueueUpload(asset, groupId.isEmpty() ? "1" : groupId);
    return asset;
}

void AssetManager::onMaterialDeleted(bool success, QString const& id, QString const& msg)
{
    if (success)
    {
        qDebug() << "[AssetManager] Asset deleted:" << id;

        // 从云端缓存中移除
        // m_cloudAssetsMap 是 name -> model，需要遍历移除 (低频操作可接受)
        for (auto it = m_cloudAssetsMap.begin(); it != m_cloudAssetsMap.end();)
        {
            if (it.value().uuid == id)
            {
                it = m_cloudAssetsMap.erase(it);
            }
            else
            {
                ++it;
            }
        }

        emit assetDeleted(id);
    }
    else
    {
        qWarning() << "[AssetManager] Delete failed:" << msg;
    }
}

void AssetManager::onApiData(QString const& opType, QJsonObject const& data)
{
    if (opType == "material_list" || opType == "material_op")
    {
        std::vector<AssetModel> result;
        QJsonArray rows = data["rows"].toArray();
        if (rows.isEmpty() && data.contains("data"))
            rows = data["data"].toArray();

        for (auto const& val : rows)
        {
            QJsonObject obj = val.toObject();
            QString url = obj["mediaUrl"].toString();
            QString name = obj["mediaTitle"].toString();
            if (name.isEmpty())
                name = QFileInfo(url).fileName();

            int rawType = obj["mediaType"].toInt();
            int normalizedType = (rawType == 1) ? 1 : 2;
            if (normalizedType == 1)
                continue;

            AssetModel asset;
            asset.uuid = obj["confMediaId"].toString();
            asset.name = name;
            asset.pathOrId = url;
            asset.isCloud = true;
            asset.cloudUrl = url;
            asset.type = SourceType::Image;
            asset.thumbnailUrl = url;

            // [New] Populate Map
            m_cloudAssetsMap[name] = asset;

            if (m_currentFetchType != 0 && normalizedType != m_currentFetchType)
                continue;
            result.push_back(asset);
        }

        qDebug() << "[AssetManager] Cloud Assets Cached. Count:" << m_cloudAssetsMap.size();
        emit assetsRefreshed(result);
        emit cloudMapReady();
    }
}

void AssetManager::onUploadFinished(QString const& opType, bool success, QString const&)
{
}
