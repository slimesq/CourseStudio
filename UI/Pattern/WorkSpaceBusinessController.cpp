#include "WorkSpaceBusinessController.h"
#include "ScreenWindow.h"
#include "LiveCourseSelectWidget.h"
#include "LiveUIConfig.h"
#include <QApplication>
#include <QStringList>

WorkSpaceBusinessController::WorkSpaceBusinessController(QObject* parent) : QObject(parent)
{
    connect(&AssetCacheManager::instance(),
            &AssetCacheManager::assetReady,
            this,
            &WorkSpaceBusinessController::onUserMgrAvatarReady);
}

WorkSpaceBusinessController::~WorkSpaceBusinessController()
{
}

void WorkSpaceBusinessController::init()
{
    updateUserInfo();
}

UserInfo WorkSpaceBusinessController::getCurrentUser() const
{
    return UserMgr::inst()->currentUser();
}

void WorkSpaceBusinessController::updateUserInfo()
{
    UserInfo info = UserMgr::inst()->currentUser();
    QString displayName = info.nickname.isEmpty() ? info.account : info.nickname;

    QPixmap avatarPix;
    if (!info.avatarPath.isEmpty())
    {
        QString localPath = AssetCacheManager::instance().getLocalPath(info.avatarPath);
        if (!localPath.isEmpty())
        {
            avatarPix = UserMgr::makeRoundAvatar(localPath, 64);
        }
        else
        {
            AssetCacheManager::instance().cacheAsset(info.avatarPath, false);
        }
    }
    emit userInfoUpdated(displayName, avatarPix);
}

void WorkSpaceBusinessController::onUserMgrAvatarReady(QString const& url, QString const& localPath)
{
    if (UserMgr::inst()->currentUser().avatarPath == url)
    {
        updateUserInfo();
    }
}

void WorkSpaceBusinessController::requestLogout()
{
    UserMgr::inst()->logout();
    emit logoutProcessed();
}

void WorkSpaceBusinessController::handleModeSelection(QString const& title)
{
    if (title == "场景设置")
    {
        emit requestViewSwitch(-1, title);
    }
    else if (title == "直播课程" || title == "单画面课程")
    {
        emit requestViewSwitch(-1, title);
    }
    else
    {
        emit requestViewSwitch(1, title);
    }
}

void WorkSpaceBusinessController::registerSubSystem(QWidget* widget)
{
    if (widget && !m_subSystems.contains(widget))
    {
        m_subSystems.append(widget);
    }
}

void WorkSpaceBusinessController::syncRenderingState(bool active)
{
    for (QWidget* widget : m_subSystems)
    {
        if (!widget)
            continue;
        if (auto sw = qobject_cast<ScreenWindow*>(widget))
        {
            if (active)
            {
                if (sw->isVisible())
                    sw->resumeRendering();
            }
            else
            {
                sw->suspendRendering();
            }
        }
        else if (auto lcs = qobject_cast<LiveCourseSelectWidget*>(widget))
        {
            if (active)
            {
                if (lcs->isVisible())
                    lcs->resumeRendering();
            }
            else
            {
                lcs->suspendRendering();
            }
        }
    }
}

// [新增] 核心业务联动：处理窗口从最大化还原时的业务逻辑
void WorkSpaceBusinessController::handleWindowRestore()
{
    // 1. 重置 UI 缩放比例为基准值
    LiveUIConfig::instance().setUiScale(1.0);

    // 2. 可以在此处增加其他业务重置逻辑，如重置滚动位置、刷新子页面布局状态等
    // 目前保持原需求：仅重置 Scale。
}

void WorkSpaceBusinessController::setupLiveSelection(LiveCourseSelectWidget* view)
{
    if (!view || m_selectionCtrl)
        return;
    m_selectionCtrl = new LiveSelectionController(view, this);
    connect(m_selectionCtrl,
            &LiveSelectionController::roomReadyToEnter,
            this,
            &WorkSpaceBusinessController::onRoomDetailFetched);
}

void WorkSpaceBusinessController::refreshLiveRooms()
{
    if (m_selectionCtrl)
        m_selectionCtrl->refreshLiveRooms();
}

void WorkSpaceBusinessController::onRoomDetailFetched(QJsonObject const& detail)
{
    long long roomId = detail["roomId"].toVariant().toLongLong();
    m_pendingRoomId = roomId;

    QString roomTitle = detail["title"].toString("未命名直播间");
    QString layoutIdsStr = detail["layoutIds"].toString();

    QList<long long> layoutIds;
    if (!layoutIdsStr.isEmpty())
    {
        QStringList parts = layoutIdsStr.split(',');
        for (auto const& p : parts)
        {
            bool ok;
            long long id = p.trimmed().toLongLong(&ok);
            if (ok && id > 0)
                layoutIds.append(id);
        }
    }

    if (layoutIds.isEmpty())
    {
        emit liveRoomError("无法启动", "该直播间未关联任何场景，请联系管理员配置。");
        return;
    }

    emit liveStreamReady(layoutIds, roomTitle, roomId);
}
