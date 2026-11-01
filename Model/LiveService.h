#ifndef LIVESERVICE_H
#define LIVESERVICE_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>

class LiveService : public QObject
{
    Q_OBJECT
public:
    static LiveService* instance();

    /**
     * @brief 获取本人有权限的直播间列表
     * @note 对应 GET /resource/live/room/mylist
     */
    void fetchMyRoomList();

    /**
     * @brief 获取直播间详细信息
     * @param roomId 直播间ID
     * @note 对应 GET /resource/live/room/{roomId}
     */
    void fetchRoomDetail(long long roomId);

    /**
     * @brief 修改直播状态 (开播/下播)
     * @param roomId 直播间ID
     * @param status 1=开播, 0=未开播(下播)
     * @note 对应 PUT /resource/live/room/statusChange
     */
    void changeLiveStatus(long long roomId, int status);

    /**
     * @brief 获取直播间异常信息 (查询最近30分钟)
     * @param roomId 直播间ID
     * @note 对应 GET /resource/live/room/status/{roomId}
     */
    void fetchLiveRoomStatus(long long roomId);

signals:
    /**
     * @brief 直播间列表获取完成
     * @param success 是否成功
     * @param list 直播间列表数据 (RListLiveRoomVo.data)
     * @param msg 错误信息或提示
     */
    void roomListFetched(bool success, QJsonArray const& list, QString const& msg);

    /**
     * @brief 直播间详情获取完成
     * @param success 是否成功
     * @param detail 直播间详情数据 (RLiveRoomVo.data)
     * @param msg 错误信息或提示
     */
    void roomDetailFetched(bool success, QJsonObject const& detail, QString const& msg);

    /**
     * @brief 直播状态变更完成
     * @param success 是否成功
     * @param roomId 对应的直播间ID
     * @param newStatus 变更后的状态
     * @param msg 错误信息或提示
     */
    void statusChanged(bool success, long long roomId, int newStatus, QString const& msg);

    /**
     * @brief 直播间异常信息获取完成
     * @param success 是否成功
     * @param statusData 异常状态数据 (LiveStatusVo)
     * @param msg 错误信息或提示
     */
    void liveStatusFetched(bool success, QJsonObject const& statusData, QString const& msg);
private slots:
    void onApiResponse(QString const& url,
                       bool success,
                       QJsonObject const& response,
                       QString const& msg);

private:
    explicit LiveService(QObject* parent = nullptr);
    static LiveService* m_instance;
};

#endif  // LIVESERVICE_H
