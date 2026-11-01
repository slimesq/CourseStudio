// WorkSpaceWindow.h 完整修复代码
#ifndef WORKSPACEWINDOW_H
#define WORKSPACEWINDOW_H
#include <QTimer>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QList>
#include <QStackedWidget>
#include <QPropertyAnimation>
#include <QGraphicsDropShadowEffect>
#include <QSystemTrayIcon>
#include <QAction>
#include <QCloseEvent>
#include <QWheelEvent>
#include "ScreenWindow.h"
#include "WorkModeCard.h"
#include "LiveCourseSelectWidget.h"
#include "WorkSpaceBusinessController.h"
#include <QJsonObject>
#include <QWidget>
#include <QMouseEvent>
#include "CustomTrayMenu.h"

class WorkSpaceWindow : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal scrollValue READ scrollValue WRITE setScrollValue)

public:
    WorkSpaceWindow(QWidget* parent = nullptr);
    ~WorkSpaceWindow();

    qreal scrollValue() const
    {
        return m_scrollValue;
    }
    void setScrollValue(qreal value);

    void setCardSpacing(int spacing)
    {
        m_cardSpacing = spacing;
        updateCardsGeometry();
    }

signals:
    void modeSelected(QString const& modeName);
    void logoutTriggered();

private slots:
    void onNavLeftClicked();
    void onNavRightClicked();
    void onModeCardClicked(QString const& title);
    void onUserAvatarClicked();
    void onLogout();

    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);

    // [业务回调接口]
    void updateUIUserInfo(QString const& name, QPixmap const& avatar);
    void onProcessScrollBuffer();
    void onMaximizeClicked();

    // 路由与准入
    void handleViewSwitchRequest(int index, QString const& title);

    // [修复] 声明必须与实现严格匹配：接收解析后的业务数据
    void handleLiveRoomEntry(QList<long long> const& layoutIds,
                             QString const& roomTitle,
                             long long roomId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void initUI();
    void initTopBar(QVBoxLayout* containerLayout);
    void initHomePage(QWidget* pageWidget);
    void updateWindowStyle();
    void updateCardStandardSize(bool forceUpdate = false);
    void initTrayIcon();

    QPushButton* createNavBtn(QString const& text);
    QPushButton* createWindowCtrlBtn(QString const& objName);
    void switchView(int index, QString const& title = "");
    void scrollToCardIndex(int index);
    void updateNavButtons();
    void updateCardsGeometry();
    void suspendChildRendering();
    void resumeChildRendering();
    void initNavBar();

private:
    QWidget* m_mainContainer = nullptr;
    QVBoxLayout* m_outerLayout = nullptr;
    int m_shadowMargin = 20;
    int m_radius = 16;
    QList<WorkModeCard*> m_cardList;
    QStackedWidget* m_mainStack = nullptr;
    QWidget* m_navBarContainer = nullptr;
    QPushButton* m_navBackBtn = nullptr;
    QLabel* m_navTitleLabel = nullptr;
    QLabel* m_homeTitleLabel = nullptr;
    QLabel* m_userAvatar = nullptr;
    QLabel* m_userName = nullptr;
    QWidget* m_cardsContainer = nullptr;
    QHBoxLayout* m_centerLayout = nullptr;
    QPushButton* m_leftBtn = nullptr;
    QPushButton* m_rightBtn = nullptr;
    QWidget* m_pageHome = nullptr;
    QWidget* m_pageWork = nullptr;
    QLabel* m_workContentLabel = nullptr;

    WorkSpaceBusinessController* m_businessCtrl = nullptr;

    ScreenWindow* m_sceneEditor = nullptr;
    ScreenWindow* m_liveStreamer = nullptr;
    LiveCourseSelectWidget* m_liveCourseSelect = nullptr;

    qreal m_scrollValue = 0.0;
    int m_targetIndex = 0;
    int m_cardSpacing = 20;
    QPropertyAnimation* m_cardAnim = nullptr;
    bool m_isScrollAnimating = false;
    int m_wheelAccumulator = 0;
    int m_bufferedScrollSteps = 0;
    QTimer* m_scrollThrottleTimer = nullptr;
    QTimer* m_resizeTimer = nullptr;
    bool m_isFirstInit = true;
    QSystemTrayIcon* m_trayIcon = nullptr;
    CustomTrayMenu* m_trayMenu = nullptr;
    bool m_inResizeLoop = false;
    bool m_isPressed = false;
    QPoint m_dragPosition;
};

#endif  // WORKSPACEWINDOW_H
