#ifndef LIVESELECTIONCONTROLLER_H
#define LIVESELECTIONCONTROLLER_H

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>

class LiveCourseSelectWidget;

class LiveSelectionController : public QObject
{
    Q_OBJECT
public:
    explicit LiveSelectionController(LiveCourseSelectWidget* view, QObject* parent = nullptr);

    // 触发刷新列表
    void refreshLiveRooms();

signals:
    // [New] 经过过滤和校验的“准入”信号，UI层只应该监听这个
    void roomReadyToEnter(QJsonObject const& detail);

private slots:
    void onRoomListFetched(bool success, QJsonArray const& list, QString const& msg);
    void onRoomDetailFetched(bool success, QJsonObject const& detail, QString const& msg);
    void onRequestRoomDetail(long long roomId);

private:
    LiveCourseSelectWidget* m_view;
    long long m_pendingRoomId = -1;
};

#endif  // LIVESELECTIONCONTROLLER_H
