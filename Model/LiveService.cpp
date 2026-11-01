#include "LiveService.h"
#include "HttpManager.h"
#include "ApiConfig.h"
#include <QDebug>
#include <QJsonDocument>
#include <QRegularExpression>

LiveService* LiveService::m_instance = nullptr;

LiveService* LiveService::instance()
{
    if (!m_instance)
    {
        m_instance = new LiveService();
    }
    return m_instance;
}

LiveService::LiveService(QObject* parent) : QObject(parent)
{
    connect(HttpManager::instance(), &HttpManager::apiResponse, this, &LiveService::onApiResponse);
}

void LiveService::fetchMyRoomList()
{
    QString url = ApiConfig::instance().getUrl("LIVE_ROOM_LIST");
    HttpManager::instance()->get(url);
}

void LiveService::fetchRoomDetail(long long roomId)
{
    // 拼接 roomId 到 URL 路径中
    QString baseUrl = ApiConfig::instance().getUrl("LIVE_ROOM_DETAIL");
    // 确保 baseUrl 结尾没有多余斜杠，避免双斜杠问题， though ApiConfig usually handles base paths
    // 这里假定 ApiConfig 返回如 "/resource/live/room/"
    QString url = baseUrl + QString::number(roomId);
    HttpManager::instance()->get(url);
}

void LiveService::changeLiveStatus(long long roomId, int status)
{
    QString url = ApiConfig::instance().getUrl("LIVE_ROOM_STATUS");

    // 构建 LiveRoomBo 部分必需字段用于状态更新
    // 注意：根据接口文档，PUT 请求通常需要包含主键 roomId 和变更字段 status
    QJsonObject json;
    json["roomId"] = roomId;
    json["status"] = status;

    // 根据文档，可能需要补全其他必填项，或者后端支持部分更新。
    // 这里假设只传变更项即可，如果后端强校验其他字段，可能需要先 fetchDetail 再全量提交。
    // 暂时按最小集提交。

    qDebug() << "[LiveService] Changing Status -> Room:" << roomId << " Status:" << status;
    HttpManager::instance()->put(url, json);
}

void LiveService::fetchLiveRoomStatus(long long roomId)
{
    // 拼接 URL: /resource/live/room/status/12345
    QString baseUrl = ApiConfig::instance().getUrl("LIVE_ROOM_EXCEPTION_STATUS");
    QString url = baseUrl + QString::number(roomId);

    // [New] 打印请求日志
    qDebug().noquote() << "[LiveService] Polling Room Status -> URL:" << url;

    HttpManager::instance()->get(url);
}

// [文件: LiveService.cpp]

void LiveService::onApiResponse(QString const& url,
                                bool success,
                                QJsonObject const& response,
                                QString const& msg)
{
    // 1. 处理获取直播间列表
    if (url.contains("/resource/live/room/mylist"))
    {
        if (!success)
        {
            emit roomListFetched(false, {}, msg);
            return;
        }
        if (response.contains("data") && response["data"].isArray())
        {
            emit roomListFetched(true, response["data"].toArray(), "OK");
        }
        else
        {
            emit roomListFetched(false, {}, "Invalid response format");
        }
    }
    // 2. 处理状态变更
    else if (url.contains("/resource/live/room/statusChange"))
    {
        // [FIX START] 增加业务状态码校验
        // HttpManager 仅校验 HTTP 状态码(200)，需要进一步校验后端业务 code (0 表示成功)
        bool finalSuccess = success;
        QString finalMsg = msg;

        if (success)
        {
            if (response.contains("code") && response["code"].toInt() != 0)
            {
                finalSuccess = false;
                finalMsg = response["msg"].toString();
                // 如果 msg 为空，提供默认错误
                if (finalMsg.isEmpty())
                    finalMsg = "Unknown business error";
            }
        }
        // [FIX END]

        // 由于 PUT 请求通常不返回 roomId，我们无法从响应中获取
        // 维持原样，发送 0 和 -1，依靠 LiveController 上下文处理
        long long roomId = 0;
        int status = -1;

        emit statusChanged(finalSuccess, roomId, status, finalMsg);
    }
    // 3. [New] 处理直播间异常查询响应
    // URL 格式类似 /resource/live/room/status/12345
    else if (url.contains("/resource/live/room/status/"))
    {
        // [New] 打印响应摘要
        qDebug().noquote() << "[LiveService] Poll Response (" << (success ? "OK" : "FAIL")
                           << ") :" << QJsonDocument(response).toJson(QJsonDocument::Compact);
        if (!success)
        {
            emit liveStatusFetched(false, {}, msg);
            return;
        }

        // 解析响应数据 (RLiveStatusVo.data -> LiveStatusVo)
        if (response.contains("data") && response["data"].isObject())
        {
            QJsonObject data = response["data"].toObject();

            // [New] 打印关键状态信息
            int roomId = data["roomId"].toInt();
            int status = data["status"].toInt();
            qDebug() << "[LiveService] Room:" << roomId << " Exception Status:" << status
                     << "(0=Normal, 1=Exception)";

            emit liveStatusFetched(true, data, "OK");
        }
        else
        {
            emit liveStatusFetched(
                false, {}, "Invalid response format: data is missing or not an object");
        }
    }
    // 4. 处理获取直播间详情
    // URL 格式类似 /resource/live/room/12345
    // 使用正则匹配或包含判断
    else if (url.contains("/resource/live/room/"))
    {
        // 排除掉 mylist 和 statusChange，确保是详情接口
        if (!url.contains("mylist") && !url.contains("statusChange"))
        {
            if (!success)
            {
                emit roomDetailFetched(false, {}, msg);
                return;
            }

            // [Fix Start] 修复无法获取流地址的问题
            // 问题根源：如果 platformList 为 null，toObject() 后的 QJsonValue 类型不是 Array，
            // 导致 LiveController 中的 isArray() 检查失败。

            QJsonObject data = response["data"].toObject();

            // 强制转换：无论后端返回 null 还是数组，这里都通过 toArray() 统一转为 QJsonArray
            // QJsonValue::toArray() 的特性是：如果是 null/undefined，则返回空数组 []
            QJsonArray platforms = data["platformList"].toArray();

            // 关键步骤：将处理过后的 数组 重新赋值回 data 对象
            // 这样 LiveController 收到 data["platformList"] 时，它一定是 Array 类型
            data["platformList"] = platforms;

            // 打印日志方便调试
            qInfo() << "[SERVICE_AUDIT] Room Detail Processed. PlatformList Size:"
                    << platforms.size();
            if (!platforms.isEmpty())
            {
                qInfo() << "[SERVICE_AUDIT] First PushURL:"
                        << platforms.first().toObject()["pushUrl"].toString();
            }

            emit roomDetailFetched(true, data, "OK");
            // [Fix End]
        }
    }
}
