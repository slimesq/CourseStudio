#pragma once
#include <QString>
#include <vector>
#include <QList>
#include <QDebug>
#include <QUuid>
#include <QFileInfo>
#include <QObject>
#include <QMap>
#include "Types.h"
#include "WGCCapture.h"
#include "CameraCapture.h"
#include "TransferManager.h"

class AssetManager : public QObject
{
    Q_OBJECT
public:
    static AssetManager& instance()
    {
        static AssetManager s;
        return s;
    }

    std::vector<AssetModel> getAvailableDevices(SourceType type)
    {
        std::vector<AssetModel> list;
        int typeId = (int)type;
        qDebug() << "[AssetManager] Requesting devices for Type ID:" << typeId;

        // 1. 摄像头
        if (type == SourceType::Camera)
        {
            auto cams = CameraCapture::getCameras();
            qDebug() << "  -> Found Cameras:" << cams.size();
            for (auto const& cam : cams)
            {
                list.push_back({cam.second, cam.first, cam.second, "", SourceType::Camera, false});
            }
        }
        // 2. 窗口采集 (兼容 Type ID 3)
        // [Fix] 这里的 3 对应您旧数据或逻辑中的 Window 类型
        else if (type == SourceType::ScreenCapture_WGC || typeId == 3)
        {
            auto windows = WGCCapture::getCapturableWindows();
            qDebug() << "  -> Found WGC Windows:" << windows.size();
            for (auto const& win : windows)
            {
                QString idStr = "win:" + QString::number((unsigned long long)win.hwnd);
                // 注意：这里返回的 type 统一为 ScreenCapture_WGC，这样切换后类型会自动修正
                list.push_back(
                    {idStr, "[Win] " + win.title, idStr, "", SourceType::ScreenCapture_WGC, false});
            }
        }
        // 3. 显示器采集 (兼容 Type ID 4, 防止 Monitor 枚举值也不匹配)
        else if (type == SourceType::Monitor || typeId == 4)
        {
            auto monitors = WGCCapture::getCapturableMonitors();
            qDebug() << "  -> Found WGC Monitors:" << monitors.size();
            for (auto const& mon : monitors)
            {
                QString idStr = "mon:" + QString::number((unsigned long long)mon.hmonitor);
                list.push_back({idStr, "[Mon] " + mon.name, idStr, "", SourceType::Monitor, false});
            }
        }
        else
        {
            qWarning() << "  -> Unknown/Unsupported SourceType for device listing:" << typeId;
        }

        return list;
    }

    void fetchCloudAssets(int mediaType);
    AssetModel importLocalFile(QString const& filePath, QString const& groupId);

    std::vector<AssetModel> getPendingAssets(int mediaType)
    {
        std::vector<AssetModel> result;
        for (auto const& asset : m_pendingUploads)
        {
            int assetTypeInt = (asset.type == SourceType::Video) ? 1 : 2;
            if (assetTypeInt == mediaType)
                result.push_back(asset);
        }
        return result;
    }

    bool isPendingUpload(QString const& pathOrId)
    {
        for (auto const& asset : m_pendingUploads)
        {
            if (asset.pathOrId == pathOrId)
                return true;
        }
        return false;
    }

    // [New] Find URL by Name (Priority: Cloud > Pending)
    QString findUrlByName(QString const& name)
    {
        if (name.isEmpty())
            return "";

        // 1. Check Cloud Cache
        if (m_cloudAssetsMap.contains(name))
        {
            return m_cloudAssetsMap[name].cloudUrl;
        }

        // 2. Check Pending Uploads
        for (auto const& asset : m_pendingUploads)
        {
            if (asset.name == name)
                return asset.pathOrId;  // Return local path if pending
        }
        return "";
    }

    void deleteAsset(QString const& uuid)
    {
        // 1. 如果是正在上传的任务，直接从队列移除
        bool removedFromPending = false;
        for (auto it = m_pendingUploads.begin(); it != m_pendingUploads.end();)
        {
            if (it->uuid == uuid)
            {
                it = m_pendingUploads.erase(it);
                removedFromPending = true;
            }
            else
            {
                ++it;
            }
        }

        if (removedFromPending)
        {
            savePendingTasks();
            emit assetDeleted(uuid);  // 本地任务直接完成
            return;
        }

        // 2. 如果是云端资源，发起网络请求
        // 注意：这里需要 confMediaId，而 uuid 存储的就是 confMediaId
        ResourceService::instance()->deleteMaterial(uuid);
    }

    void onMaterialDeleted(bool success, QString const& id, QString const& msg);

signals:
    void assetsRefreshed(std::vector<AssetModel> const& assets);
    void assetUploaded(QString const& localPath, QString const& cloudUrl);
    // [New] Notification when cloud map is ready (for retry logic)
    void cloudMapReady();
    void assetDeleted(QString const& uuid);

private slots:
    void onApiData(QString const& opType, QJsonObject const& data);
    void onUploadFinished(QString const& opType, bool success, QString const& msg);

private:
    AssetManager();
    void savePendingTasks();
    void loadPendingTasks();
    int m_currentFetchType = 0;
    QList<AssetModel> m_pendingUploads;

    // [New] Cache for Name->Asset mapping
    QMap<QString, AssetModel> m_cloudAssetsMap;
};
