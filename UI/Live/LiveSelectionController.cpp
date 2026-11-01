#include "LiveSelectionController.h"
#include "LiveCourseSelectWidget.h"
#include "LiveService.h"
#include <QDebug>
#include <QJsonDocument>
#include <QDateTime>

// 静态变量：全局防抖时间戳
static qint64 g_lastSuccessTime = 0;

LiveSelectionController::LiveSelectionController(LiveCourseSelectWidget* view, QObject* parent)
    : QObject(parent), m_view(view)
{
    connect(LiveService::instance(),
            &LiveService::roomListFetched,
            this,
            &LiveSelectionController::onRoomListFetched,
            Qt::UniqueConnection);

    // 监听原始数据
    connect(LiveService::instance(),
            &LiveService::roomDetailFetched,
            this,
            &LiveSelectionController::onRoomDetailFetched,
            Qt::UniqueConnection);

    if (m_view)
    {
        connect(m_view,
                &LiveCourseSelectWidget::requestRoomDetail,
                this,
                &LiveSelectionController::onRequestRoomDetail,
                Qt::UniqueConnection);
    }
}

void LiveSelectionController::refreshLiveRooms()
{
    LiveService::instance()->fetchMyRoomList();
}

void LiveSelectionController::onRoomListFetched(bool success,
                                                QJsonArray const& list,
                                                QString const& msg)
{
    if (success && m_view)
        m_view->loadRooms(list);
    else if (m_view)
        m_view->loadRooms(QJsonArray());
}

void LiveSelectionController::onRequestRoomDetail(long long roomId)
{
    m_pendingRoomId = roomId;
    qDebug() << "[LiveSelectionController] Requesting DETAIL for Room:" << roomId;
    LiveService::instance()->fetchRoomDetail(roomId);
}

void LiveSelectionController::onRoomDetailFetched(bool success,
                                                  QJsonObject const& detail,
                                                  QString const& msg)
{
    if (!m_view)
        return;

    long long roomId = detail["roomId"].toVariant().toLongLong();

    // 1. ID 校验
    if (roomId != m_pendingRoomId)
    {
        qWarning() << "[LiveSelectionController] Ignored signal for Room:" << roomId
                   << " (Expected:" << m_pendingRoomId << ")";
        return;
    }

    // 2. 时间戳去重
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (success && (now - g_lastSuccessTime < 1000))
    {
        qWarning() << "[LiveSelectionController] DUPLICATE SIGNAL DETECTED. Ignoring.";
        return;
    }

    m_pendingRoomId = -1;

    if (success)
    {
        g_lastSuccessTime = now;
        qDebug() << "[LiveSelectionController] SUCCESS. Emitting roomReadyToEnter.";

        // 停止 UI Loading
        m_view->stopCardLoading(roomId);

        // [Critical] 发送给 WorkSpaceWindow 的安全信号
        emit roomReadyToEnter(detail);
    }
    else
    {
        qWarning() << "[LiveSelectionController] Failed:" << msg;
        m_view->stopCardLoading(0);
    }
}
