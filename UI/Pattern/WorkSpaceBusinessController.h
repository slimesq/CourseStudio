#ifndef WORKSPACEBUSINESSCONTROLLER_H
#define WORKSPACEBUSINESSCONTROLLER_H

#include <QObject>
#include <QString>
#include <QPixmap>
#include <QList>
#include <QWidget>
#include <QJsonObject>
#include "UserMgr.h"
#include "AssetCacheManager.h"
#include "LiveSelectionController.h"

/**
 * @brief WorkSpace 业务逻辑控制器
 * 职责：处理用户状态、页面路由决策、子系统协调、直播准入逻辑、系统级状态联动
 */
class WorkSpaceBusinessController : public QObject
{
    Q_OBJECT
public:
    explicit WorkSpaceBusinessController(QObject* parent = nullptr);
    ~WorkSpaceBusinessController();

    void init();
    void requestLogout();
    UserInfo getCurrentUser() const;

    // 页面路由决策
    void handleModeSelection(QString const& title);

    // 子系统业务管理
    void registerSubSystem(QWidget* widget);
    void syncRenderingState(bool active);

    // [新增] 处理窗口还原/状态重置业务
    void handleWindowRestore();

    // 直播准入业务接口
    void setupLiveSelection(LiveCourseSelectWidget* view);
    void refreshLiveRooms();

signals:
    void userInfoUpdated(QString const& nickname, QPixmap const& avatar);
    void logoutProcessed();
    void requestViewSwitch(int index, QString const& title);

    // 业务层最终信号：告知 UI 场景数据已完整解析并就绪
    void liveStreamReady(QList<long long> const& layoutIds,
                         QString const& roomTitle,
                         long long roomId);
    void liveRoomError(QString const& title, QString const& msg);

private slots:
    void onUserMgrAvatarReady(QString const& url, QString const& localPath);
    void onRoomDetailFetched(QJsonObject const& detail);

private:
    void updateUserInfo();

private:
    QList<QWidget*> m_subSystems;
    LiveSelectionController* m_selectionCtrl = nullptr;
    long long m_pendingRoomId = 0;
};

#endif  // WORKSPACEBUSINESSCONTROLLER_H
