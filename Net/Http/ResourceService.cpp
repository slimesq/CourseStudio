#include "ResourceService.h"
#include "HttpManager.h"
#include "ApiConfig.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QDebug>
#include <QUrl>
#include <QUrlQuery>

ResourceService* ResourceService::m_instance = nullptr;
ResourceService* ResourceService::instance()
{
    if (!m_instance)
        m_instance = new ResourceService();
    return m_instance;
}

ResourceService::ResourceService(QObject* parent) : QObject(parent)
{
    connect(
        HttpManager::instance(), &HttpManager::apiResponse, this, &ResourceService::onApiResponse);
}

void ResourceService::onApiResponse(QString const& url,
                                    bool success,
                                    QJsonObject const& response,
                                    QString const& msg)
{
    QString opType = "unknown";

    // 1. Scene Class (Category)
    if (url.contains("/layoutType/list"))
    {
        opType = "scene_class_op";
    }
    else if (url.contains("/layoutType"))
    {
        opType = "scene_class_op";
    }
    // 2. Scene List
    else if (url.contains("/layout/list"))
    {
        opType = "scene_list";
    }
    // 3. Scene Detail / Op
    else if (url.contains("/resource/conf/layout"))
    {
        if (!url.contains("/media/"))
        {
            static QRegularExpression reDetail("/layout/\\d+");
            if (reDetail.match(url).hasMatch())
            {
                opType = "scene_detail";
            }
            else
            {
                opType = "scene_op";
            }
            if (url.endsWith("/list"))
                opType = "scene_list";
        }
    }
    // 4. Material Group
    else if (url.contains("mediaGroup"))
    {
        opType = "material_group_op";
    }
    // 5. Material
    else if (url.contains("media"))
    {
        if (url.contains("upload"))
        {
            if (url.contains("layoutCover"))
            {
                opType = "cover_upload";
                if (success && response.contains("data"))
                {
                    QJsonObject data = response["data"].toObject();
                    QString coverUrl = data["url"].toString();
                    emit coverUploaded(true, coverUrl, "OK");
                }
                else
                {
                    emit coverUploaded(false, "", msg);
                }
                return;
            }
            else
            {
                opType = "material_upload";
            }
        }
        else
        {
            opType = "material_op";
            if (success && (response["data"].isNull() || response["data"].isUndefined()))
            {
                QString id = url.section('/', -1);
                bool isId = false;
                id.toLongLong(&isId);
                if (isId)
                {
                    emit materialDeleted(success, id, msg);
                }
            }
        }
    }
    // [New] 6. Layout Preference
    else if (url.contains("layoutPreference"))
    {
        opType = "preference_op";
        if (url.contains("getMine"))
        {
            // [FIX START] ==========================================
            // 修复：如果获取偏好失败（如404或data为空），无法从data获取ID，导致回调ID不匹配
            long long layoutId = 0;
            QString content = "";
            bool finalSuccess = false;

            if (success && response.contains("data") && !response["data"].isNull())
            {
                QJsonObject data = response["data"].toObject();
                layoutId = data["layoutId"].toVariant().toLongLong();
                content = data["content"].toString();
                finalSuccess = true;
            }

            // 如果从 data 解析失败，尝试从 URL 参数中恢复 layoutId
            if (layoutId == 0)
            {
                QUrl qUrl(url);
                QUrlQuery query(qUrl);
                if (query.hasQueryItem("layoutId"))
                {
                    layoutId = query.queryItemValue("layoutId").toLongLong();
                    qDebug() << "[ResourceService] Recovered layoutId from URL:" << layoutId;
                }
                else
                {
                    // [Debug] 打印失败的 URL 以便排查
                    qWarning() << "[ResourceService] FAILED to recover layoutId from URL:" << url;
                }
            }

            // 发送信号（即使失败也要发送，以便调用者能收到结束信号）
            emit preferenceFetched(finalSuccess, layoutId, content, msg);
            // [FIX END] ============================================
            return;
        }
        else if (url.contains("setMine"))
        {
            emit preferenceSaved(success, msg);
            return;
        }
    }

    // [New] 7. Live Record / Live File Upload
    else if (url.contains("/media/liveRecord/upload"))
    {
        opType = "live_record_upload";
        QString fileUrl = "";
        if (success && response.contains("data"))
        {
            fileUrl = response["data"].toObject()["url"].toString();
        }
        emit liveRecordUploaded(success, fileUrl, msg);
        return;  // 处理完毕直接返回
    }
    else if (url.contains("/media/live/upload"))
    {
        opType = "live_file_upload";
        QString fileUrl = "";
        int mediaType = 0;

        // 尝试从 URL 参数中恢复 mediaType (因为 HttpManager 的回调 url 通常包含 query 参数)
        // 例如: .../upload?roomId=123&mediaType=1
        QUrl qUrl(url);
        QUrlQuery query(qUrl);
        if (query.hasQueryItem("mediaType"))
        {
            mediaType = query.queryItemValue("mediaType").toInt();
        }

        if (success && response.contains("data"))
        {
            fileUrl = response["data"].toObject()["url"].toString();
        }
        emit liveFileUploaded(success, fileUrl, mediaType, msg);
        return;  // 处理完毕直接返回
    }

    qDebug() << "[ResourceService] URL:" << url << "-> OpType:" << opType;

    emit operationFinished(opType, success, msg);
    if (success)
    {
        emit dataReceived(opType, response);
    }
}

// ... (Rest of the file remains unchanged) ...
// [Modified] Removed authList
void ResourceService::addSceneClass(QString const& name)
{
    QJsonObject json;
    json["typeName"] = name;
    HttpManager::instance()->post(ApiConfig::instance().getUrl("SCENE_CLASS_ADD"), json);
}

void ResourceService::updateSceneClass(QString const& id, QString const& name)
{
    QJsonObject json;
    json["typeId"] = id;
    json["typeName"] = name;
    HttpManager::instance()->put(ApiConfig::instance().getUrl("SCENE_CLASS_UPDATE"), json);
}

void ResourceService::deleteSceneClass(QString const& id)
{
    QString url = ApiConfig::instance().getUrl("SCENE_CLASS_DELETE") + id;
    HttpManager::instance()->del(url);
}
void ResourceService::getSceneClassDetail(QString const& id)
{
    QString url = ApiConfig::instance().getUrl("SCENE_CLASS_DETAIL") + id;
    HttpManager::instance()->get(url);
}
void ResourceService::getSceneClassList(int page, int size)
{
    QVariantMap params;
    params["pageNum"] = page;
    params["pageSize"] = size;
    HttpManager::instance()->get(ApiConfig::instance().getUrl("SCENE_CLASS_LIST"), params);
}
void ResourceService::exportSceneClassList(QVariantMap const& params)
{
    QJsonObject json;
    for (auto it = params.begin(); it != params.end(); ++it)
        json[it.key()] = it.value().toJsonValue();
    HttpManager::instance()->post(ApiConfig::instance().getUrl("SCENE_CLASS_EXPORT"), json);
}

void ResourceService::addScene(QString const& name,
                               QString const& sceneClassId,
                               QString const& content,
                               QString const& remark)
{
    QJsonObject json;
    json["layoutName"] = name;
    json["typeId"] = sceneClassId;
    json["content"] = content;
    json["remark"] = remark;
    HttpManager::instance()->post(ApiConfig::instance().getUrl("SCENE_ADD"), json);
}

void ResourceService::updateScene(QString const& id,
                                  QString const& name,
                                  QString const& content,
                                  QString const& remark)
{
    QJsonObject json;
    json["layoutId"] = id;
    if (!name.isEmpty())
        json["layoutName"] = name;
    if (!content.isEmpty())
        json["content"] = content;
    if (!remark.isEmpty())
        json["remark"] = remark;

    qDebug() << "[ResourceService] Updating Scene Payload:"
             << QJsonDocument(json).toJson(QJsonDocument::Compact);
    HttpManager::instance()->put(ApiConfig::instance().getUrl("SCENE_UPDATE"), json);
}

void ResourceService::deleteScene(QString const& id)
{
    QString url = ApiConfig::instance().getUrl("SCENE_DELETE") + id;
    HttpManager::instance()->del(url);
}
void ResourceService::getSceneDetail(QString const& id)
{
    QString url = ApiConfig::instance().getUrl("SCENE_DETAIL") + id;
    qDebug() << "[ResourceService] GET Detail:" << url;
    HttpManager::instance()->get(url);
}
void ResourceService::getSceneList(int page, int size, QString const& name, QString const& typeId)
{
    QVariantMap params;
    params["pageNum"] = page;
    params["pageSize"] = size;
    if (!name.isEmpty())
        params["layoutName"] = name;
    if (!typeId.isEmpty())
        params["typeId"] = typeId;
    HttpManager::instance()->get(ApiConfig::instance().getUrl("SCENE_LIST"), params);
}
void ResourceService::exportSceneList(QVariantMap const& params)
{
    QJsonObject json;
    for (auto it = params.begin(); it != params.end(); ++it)
        json[it.key()] = it.value().toJsonValue();
    HttpManager::instance()->post(ApiConfig::instance().getUrl("SCENE_EXPORT"), json);
}
void ResourceService::uploadSceneFile(QString const& filePath, QString const& sceneClassId)
{
    QMap<QString, QString> params;
    params["groupId"] = sceneClassId;
    HttpManager::instance()->upload(ApiConfig::instance().getUrl("SCENE_UPLOAD"), filePath, params);
}

void ResourceService::addMaterialGroup(QString const& name, QString const& parentId)
{
    QJsonObject json;
    json["groupName"] = name;
    if (!parentId.isEmpty())
        json["parentId"] = parentId;
    HttpManager::instance()->post(ApiConfig::instance().getUrl("ASSET_GROUP_ADD"), json);
}

void ResourceService::updateMaterialGroup(QString const& id, QString const& name)
{
    QJsonObject json;
    json["groupId"] = id;
    json["groupName"] = name;
    HttpManager::instance()->put(ApiConfig::instance().getUrl("ASSET_GROUP_UPDATE"), json);
}

void ResourceService::deleteMaterialGroup(QString const& id)
{
    QString url = ApiConfig::instance().getUrl("ASSET_GROUP_DELETE") + id;
    HttpManager::instance()->del(url);
}
void ResourceService::getMaterialGroupDetail(QString const& id)
{
    QString url = ApiConfig::instance().getUrl("ASSET_GROUP_DETAIL") + id;
    HttpManager::instance()->get(url);
}
void ResourceService::getMaterialGroupList(int page, int size)
{
    QVariantMap params;
    params["pageNum"] = page;
    params["pageSize"] = size;
    HttpManager::instance()->get(ApiConfig::instance().getUrl("ASSET_GROUP_LIST"), params);
}
void ResourceService::exportMaterialGroupList(QVariantMap const& params)
{
    QJsonObject json;
    for (auto it = params.begin(); it != params.end(); ++it)
        json[it.key()] = it.value().toJsonValue();
    HttpManager::instance()->post(ApiConfig::instance().getUrl("ASSET_GROUP_EXPORT"), json);
}
void ResourceService::getMaterialList(int page,
                                      int size,
                                      QString const& groupId,
                                      QString const& name)
{
    QVariantMap params;
    params["pageNum"] = page;
    params["pageSize"] = size;
    if (!groupId.isEmpty())
        params["groupId"] = groupId;
    if (!name.isEmpty())
        params["mediaTitle"] = name;
    HttpManager::instance()->get(ApiConfig::instance().getUrl("ASSET_LIST"), params);
}
void ResourceService::getMaterialDetail(QString const& id)
{
    QString url = ApiConfig::instance().getUrl("ASSET_DETAIL") + id;
    HttpManager::instance()->get(url);
}
void ResourceService::updateMaterial(QString const& id, QString const& name)
{
    QJsonObject json;
    json["confMediaId"] = id;
    json["mediaTitle"] = name;
    HttpManager::instance()->put(ApiConfig::instance().getUrl("ASSET_UPDATE"), json);
}
void ResourceService::deleteMaterial(QString const& id)
{
    if (id.isEmpty())
    {
        qWarning() << "[ResourceService] Delete aborted: Empty ID";
        return;
    }
    // [Fix] 确保 ID 正确拼接到 URL 后面
    // 假设 ASSET_DELETE 是 "/resource/conf/media/"
    QString baseUrl = ApiConfig::instance().getUrl("ASSET_DELETE");
    if (!baseUrl.endsWith("/"))
        baseUrl += "/";

    QString url = baseUrl + id;
    qDebug() << "[ResourceService] DELETE URL:" << url;  // 添加日志确认
    HttpManager::instance()->del(url);
}
void ResourceService::exportMaterialList(QVariantMap const& params)
{
    QJsonObject json;
    for (auto it = params.begin(); it != params.end(); ++it)
        json[it.key()] = it.value().toJsonValue();
    HttpManager::instance()->post(ApiConfig::instance().getUrl("ASSET_EXPORT"), json);
}
void ResourceService::uploadMaterialFile(QString const& filePath,
                                         QString const& groupId,
                                         int mediaType)
{
    QMap<QString, QString> params;
    params["groupId"] = groupId;
    params["mediaType"] = QString::number(mediaType);
    HttpManager::instance()->upload(ApiConfig::instance().getUrl("ASSET_UPLOAD"), filePath, params);
}

void ResourceService::uploadLayoutCover(QString const& filePath)
{
    QMap<QString, QString> params;
    params["mediaType"] = "0";
    HttpManager::instance()->upload(
        ApiConfig::instance().getUrl("LAYOUT_COVER_UPLOAD"), filePath, params);
}

void ResourceService::fetchLayoutPreference(long long layoutId)
{
    // [FIX] 强制手动拼接 URL 参数，确保 httpManager 不会丢失 Query
    // 原始：HttpManager::get(url, params)
    // 修改：url + "?layoutId=xxx"

    QString baseUrl = ApiConfig::instance().getUrl("PREF_GET");
    QString finalUrl = QString("%1?layoutId=%2").arg(baseUrl).arg(layoutId);

    // 传入空 params，因为参数已经在 URL 里了
    HttpManager::instance()->get(finalUrl, {});
}

void ResourceService::updateLayoutPreference(long long layoutId, QString const& content)
{
    QString url = ApiConfig::instance().getUrl("PREF_SET");
    QJsonObject json;
    json["layoutId"] = layoutId;
    json["content"] = content;
    HttpManager::instance()->post(url, json);
}

void ResourceService::uploadLiveRecord(QString const& filePath, long long roomId, int mediaType)
{
    // 文档要求 roomId 和 mediaType 是 Query 参数
    QString baseUrl = ApiConfig::instance().getUrl("LIVE_RECORD_UPLOAD");
    QString urlWithQuery = QString("%1?roomId=%2").arg(baseUrl).arg(roomId);
    if (mediaType != 0)
    {
        urlWithQuery += QString("&mediaType=%1").arg(mediaType);
    }

    // 上传时 body 不需要额外参数，传空 Map
    HttpManager::instance()->upload(urlWithQuery, filePath, {});
}

void ResourceService::uploadLiveFile(QString const& filePath, long long roomId, int mediaType)
{
    // 文档要求 roomId 和 mediaType 是 Query 参数
    QString baseUrl = ApiConfig::instance().getUrl("LIVE_FILE_UPLOAD");
    QString urlWithQuery =
        QString("%1?roomId=%2&mediaType=%3").arg(baseUrl).arg(roomId).arg(mediaType);

    // 上传时 body 不需要额外参数，传空 Map
    HttpManager::instance()->upload(urlWithQuery, filePath, {});
}

void ResourceService::uploadLiveFile(QByteArray const& data,
                                     QString const& fileName,
                                     long long roomId,
                                     int mediaType)
{
    // 拼接 Query 参数
    QString baseUrl = ApiConfig::instance().getUrl("LIVE_FILE_UPLOAD");
    QString urlWithQuery =
        QString("%1?roomId=%2&mediaType=%3").arg(baseUrl).arg(roomId).arg(mediaType);

    // 调用内存上传接口
    HttpManager::instance()->upload(urlWithQuery, data, fileName, {});
}
