#include "WorkSpaceWindow.h"
#include "WorkModeCard.h"
#include "skinmgr.h"
#include "AppConfig.h"
#include "UserMgr.h"
#include "UserProfileWidget.h"
#include "LoginWidget.h"
#include "UniversalDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QLinearGradient>
#include <QDebug>
#include <QResizeEvent>
#include <QTimer>
#include <QPainterPath>
#include <QtMath>
#include <algorithm>
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <LiveUIConfig.h>

WorkSpaceWindow::WorkSpaceWindow(QWidget* parent) : QWidget(parent)
{
    this->setAttribute(Qt::WA_TranslucentBackground);
    this->setAttribute(Qt::WA_StaticContents);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window | Qt::WindowSystemMenuHint |
                   Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);

    this->setMinimumSize(1200, 840);
    this->resize(1200, 840);

    m_isScrollAnimating = false;
    m_isFirstInit = true;

    m_businessCtrl = new WorkSpaceBusinessController(this);

    connect(m_businessCtrl,
            &WorkSpaceBusinessController::userInfoUpdated,
            this,
            &WorkSpaceWindow::updateUIUserInfo);
    connect(m_businessCtrl,
            &WorkSpaceBusinessController::logoutProcessed,
            this,
            &WorkSpaceWindow::onLogout);
    connect(m_businessCtrl,
            &WorkSpaceBusinessController::requestViewSwitch,
            this,
            &WorkSpaceWindow::handleViewSwitchRequest);

    connect(m_businessCtrl,
            &WorkSpaceBusinessController::liveStreamReady,
            this,
            &WorkSpaceWindow::handleLiveRoomEntry);
    connect(m_businessCtrl,
            &WorkSpaceBusinessController::liveRoomError,
            this,
            [this](QString const& t, QString const& m) { UniversalDialog::showError(this, t, m); });

    m_scrollThrottleTimer = new QTimer(this);
    m_scrollThrottleTimer->setInterval(100);
    connect(m_scrollThrottleTimer, &QTimer::timeout, this, &WorkSpaceWindow::onProcessScrollBuffer);

    m_resizeTimer = new QTimer(this);
    m_resizeTimer->setSingleShot(true);
    m_resizeTimer->setInterval(50);
    connect(m_resizeTimer, &QTimer::timeout, this, [this]() {
        updateCardStandardSize(false);
        updateCardsGeometry();
        resumeChildRendering();
        m_inResizeLoop = false;
    });

    initUI();
    initTrayIcon();

    SkinMgr::inst()->setSkin(SK_WorkSpace, this);
    m_businessCtrl->init();
}

WorkSpaceWindow::~WorkSpaceWindow()
{
    if (m_trayIcon)
    {
        m_trayIcon->hide();
        delete m_trayIcon;
    }
}

void WorkSpaceWindow::onNavLeftClicked()
{
    if (m_isScrollAnimating)
        return;
    scrollToCardIndex(m_targetIndex - 1);
}

void WorkSpaceWindow::onNavRightClicked()
{
    if (m_isScrollAnimating)
        return;
    scrollToCardIndex(m_targetIndex + 1);
}

void WorkSpaceWindow::onModeCardClicked(QString const& title)
{
    WorkModeCard* senderCard = qobject_cast<WorkModeCard*>(sender());
    if (senderCard)
    {
        int index = m_cardList.indexOf(senderCard);
        int N = m_cardList.size();
        double diff = index - m_scrollValue;
        while (diff > N / 2.0)
            diff -= N;
        while (diff < -N / 2.0)
            diff += N;
        if (qAbs(diff) > 0.1)
        {
            scrollToCardIndex(qRound(m_scrollValue + diff));
            return;
        }
    }
    if (m_isScrollAnimating)
        return;
    m_businessCtrl->handleModeSelection(title);
}

void WorkSpaceWindow::handleViewSwitchRequest(int index, QString const& title)
{
    int targetIndex = -1;
    if (index >= 0)
    {
        targetIndex = index;
        if (targetIndex == m_mainStack->indexOf(m_pageWork))
        {
            if (m_workContentLabel)
                m_workContentLabel->setText("这里是 [" + title + "] 的功能界面");
        }
    }
    else
    {
        if (title == "场景设置")
        {
            if (!m_sceneEditor)
            {
                m_sceneEditor =
                    new ScreenWindow(ScreenWindow::WindowMode::SceneEditor, false, this);
                m_mainStack->addWidget(m_sceneEditor);
                m_businessCtrl->registerSubSystem(m_sceneEditor);
            }
            targetIndex = m_mainStack->indexOf(m_sceneEditor);
        }
        else if (title == "直播课程" || title == "单画面课程")
        {
            if (!m_liveCourseSelect)
            {
                m_liveCourseSelect = new LiveCourseSelectWidget(this);
                m_mainStack->addWidget(m_liveCourseSelect);
                m_businessCtrl->registerSubSystem(m_liveCourseSelect);
                m_businessCtrl->setupLiveSelection(m_liveCourseSelect);
            }
            if (m_mainStack->currentWidget() != m_liveCourseSelect)
            {
                m_liveCourseSelect->setUpdatesEnabled(false);
                m_businessCtrl->refreshLiveRooms();
                QTimer::singleShot(100, m_liveCourseSelect, [this]() {
                    m_liveCourseSelect->setUpdatesEnabled(true);
                    m_liveCourseSelect->update();
                });
            }
            targetIndex = m_mainStack->indexOf(m_liveCourseSelect);
        }
    }
    if (targetIndex != -1)
    {
        switchView(targetIndex, title);
        emit modeSelected(title);
    }
}

void WorkSpaceWindow::handleLiveRoomEntry(QList<long long> const& layoutIds,
                                          QString const& roomTitle,
                                          long long roomId)
{
    if (m_liveCourseSelect)
        m_liveCourseSelect->stopCardLoading(roomId);
    if (!m_liveStreamer)
    {
        m_liveStreamer = new ScreenWindow(ScreenWindow::WindowMode::LiveStreaming, false, this);
        m_mainStack->addWidget(m_liveStreamer);
        m_businessCtrl->registerSubSystem(m_liveStreamer);
    }
    m_mainStack->setCurrentWidget(m_liveStreamer);
    switchView(m_mainStack->indexOf(m_liveStreamer), QString("直播中 - %1").arg(roomTitle));
    m_liveStreamer->startLiveCourse(layoutIds, roomId);
}

void WorkSpaceWindow::suspendChildRendering()
{
    m_businessCtrl->syncRenderingState(false);
}

void WorkSpaceWindow::resumeChildRendering()
{
    m_businessCtrl->syncRenderingState(true);
}

void WorkSpaceWindow::switchView(int index, QString const& title)
{
    if (!m_mainStack)
        return;
    QWidget* currentWidget = m_mainStack->currentWidget();
    if (ScreenWindow* sw = qobject_cast<ScreenWindow*>(currentWidget))
        sw->suspendRendering();
    if (LiveCourseSelectWidget* lcs = qobject_cast<LiveCourseSelectWidget*>(currentWidget))
        lcs->suspendRendering();

    QGraphicsEffect* effect = m_mainContainer->graphicsEffect();
    QWidget* targetWidget = m_mainStack->widget(index);
    bool isNativePage = qobject_cast<ScreenWindow*>(targetWidget) != nullptr;

    if (effect)
    {
        if (isNativePage)
        {
            effect->setEnabled(false);
            m_outerLayout->setContentsMargins(0, 0, 0, 0);
        }
        else
        {
            if (!this->isMaximized())
            {
                effect->setEnabled(true);
                m_outerLayout->setContentsMargins(
                    m_shadowMargin, m_shadowMargin, m_shadowMargin, m_shadowMargin);
            }
        }
    }

    if (index == 0)
    {
        m_mainStack->setCurrentIndex(0);
        m_navBarContainer->hide();
        m_navTitleLabel->setText("");
        int targetIdx = qRound(m_scrollValue);
        int N = m_cardList.size();
        if (N > 0)
        {
            targetIdx = (targetIdx % N + N) % N;
            m_scrollValue = targetIdx;
            m_targetIndex = targetIdx;
        }
        QTimer::singleShot(50, this, [this]() {
            this->updateCardStandardSize(true);
            this->updateCardsGeometry();
        });
    }
    else
    {
        m_mainStack->setCurrentIndex(index);
        m_navTitleLabel->setText(title);
        m_navBarContainer->show();
        if (targetWidget)
            targetWidget->updateGeometry();
        if (ScreenWindow* sw = qobject_cast<ScreenWindow*>(targetWidget))
            sw->resumeRendering();
        if (LiveCourseSelectWidget* lcs = qobject_cast<LiveCourseSelectWidget*>(targetWidget))
            lcs->resumeRendering();
    }
}

void WorkSpaceWindow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    // 采用透明背景 + 内部 Container 绘制阴影的方案，此处无需额外逻辑
}

void WorkSpaceWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_isFirstInit)
    {
        m_isFirstInit = false;
        updateWindowStyle();
        updateCardStandardSize(true);
        updateCardsGeometry();
        this->resize(1200, 840);
    }
}

void WorkSpaceWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange)
    {
        if (this->windowState() == Qt::WindowNoState)
        {
            this->setUpdatesEnabled(false);
            m_businessCtrl->handleWindowRestore();
            suspendChildRendering();
            QTimer::singleShot(0, this, [this]() {
                this->resize(1200, 840);
                updateWindowStyle();
                updateCardStandardSize(true);
                updateCardsGeometry();
                resumeChildRendering();
                if (m_liveCourseSelect)
                    m_liveCourseSelect->resetLayoutConstraints();
                this->setUpdatesEnabled(true);
            });
            return;
        }
    }
    QWidget::changeEvent(event);
}

void WorkSpaceWindow::onMaximizeClicked()
{
    QPushButton* btnMax = this->findChild<QPushButton*>("ws_maxBtn");
    if (this->isMaximized())
    {
        this->setUpdatesEnabled(false);
        m_businessCtrl->handleWindowRestore();
        this->showNormal();
        if (btnMax)
        {
            // 设置属性：告诉 CSS 现在是“窗口模式”，显示最大化图标
            btnMax->setProperty("isMaximized", false);
            btnMax->setToolTip("最大化");
        }
    }
    else
    {
        this->showMaximized();
        if (btnMax)
        {
            // 设置属性：告诉 CSS 现在是“最大化模式”，显示还原图标
            btnMax->setProperty("isMaximized", true);
            btnMax->setToolTip("还原");
        }
    }
    updateWindowStyle();
    if (btnMax)
    {
        btnMax->style()->unpolish(btnMax);
        btnMax->style()->polish(btnMax);
    }
}

void WorkSpaceWindow::initUI()
{
    m_outerLayout = new QVBoxLayout(this);
    m_outerLayout->setContentsMargins(
        m_shadowMargin, m_shadowMargin, m_shadowMargin, m_shadowMargin);
    m_mainContainer = new QWidget(this);
    m_mainContainer->setObjectName("WorkSpaceMainContainer");
    m_mainContainer->installEventFilter(this);
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(20);
    shadow->setColor(QColor(0, 0, 0, 80));
    shadow->setOffset(0, 0);
    m_mainContainer->setGraphicsEffect(shadow);
    m_outerLayout->addWidget(m_mainContainer);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_mainContainer);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    initTopBar(mainLayout);
    m_mainStack = new QStackedWidget(this);
    mainLayout->addWidget(m_mainStack);
    m_pageHome = new QWidget(this);
    initHomePage(m_pageHome);
    m_mainStack->addWidget(m_pageHome);
    m_pageWork = new QWidget(this);
    QVBoxLayout* workLayout = new QVBoxLayout(m_pageWork);
    m_workContentLabel = new QLabel("正在初始化模块...", this);
    m_workContentLabel->setAlignment(Qt::AlignCenter);
    m_workContentLabel->setStyleSheet("font-size: 24px; color: #8888AA;");
    workLayout->addWidget(m_workContentLabel);
    m_mainStack->addWidget(m_pageWork);
    m_mainStack->setCurrentIndex(0);
}

void WorkSpaceWindow::initTopBar(QVBoxLayout* containerLayout)
{
    QWidget* topBar = new QWidget(this);
    int barHeight = AppConfig::instance()->topBarHeight();
    topBar->setFixedHeight(barHeight);
    QHBoxLayout* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(10, 0, 10, 0);
    topLayout->setSpacing(0);
    QLabel* logo = new QLabel(this);
    logo->setObjectName("ws_logoLabel");
    logo->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    QPixmap logoPix(":/UI/QSS/icon/log.png");
    if (!logoPix.isNull())
    {
        int targetH = static_cast<int>(barHeight * 0.35);
        qreal dpr = this->devicePixelRatioF();
        QPixmap scaledPix =
            logoPix.scaledToHeight(static_cast<int>(targetH * dpr), Qt::SmoothTransformation);
        scaledPix.setDevicePixelRatio(dpr);
        logo->setPixmap(scaledPix);
    }
    else
    {
        logo->setText("易道云信息技术研究院");
        logo->setStyleSheet("font-size: 18px; font-weight: bold; color: #333;");
    }
    topLayout->addWidget(logo);
    topLayout->addSpacing(20);
    m_navBarContainer = new QWidget(this);
    m_navBarContainer->hide();
    QHBoxLayout* navLayout = new QHBoxLayout(m_navBarContainer);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(8);
    m_navBackBtn = new QPushButton("❮", m_navBarContainer);
    m_navBackBtn->setCursor(Qt::PointingHandCursor);
    m_navBackBtn->setFixedSize(32, 32);
    QLabel* divider = new QLabel("|", m_navBarContainer);
    divider->setStyleSheet("color: #E4E7ED; font-size: 14px; margin-bottom: 2px;");
    m_navTitleLabel = new QLabel("", m_navBarContainer);
    navLayout->addWidget(m_navBackBtn);
    navLayout->addWidget(divider);
    navLayout->addWidget(m_navTitleLabel);
    QString navStyle =
        R"(QPushButton { border: none; background-color: transparent; color: #909399; font-size: 16px; font-weight: bold; border-radius: 16px; } QPushButton:hover { color: #409EFF; background-color: #F2F6FC; } QPushButton:pressed { color: #337ECC; background-color: #EBEEF5; } QLabel { color: #606266; font-size: 14px; font-weight: bold; font-family: "Microsoft YaHei"; })";
    m_navBarContainer->setStyleSheet(navStyle);
    connect(m_navBackBtn, &QPushButton::clicked, this, [this]() {
        QWidget* current = m_mainStack->currentWidget();
        if (current == m_liveStreamer && m_liveStreamer && m_liveCourseSelect)
        {
            int idx = m_mainStack->indexOf(m_liveCourseSelect);
            if (idx != -1)
            {
                switchView(idx, "直播课程");
                return;
            }
        }
        switchView(0, "");
    });
    topLayout->addWidget(m_navBarContainer);
    topLayout->addStretch();
    m_userAvatar = new QLabel(this);
    m_userAvatar->setFixedSize(32, 32);
    m_userAvatar->setObjectName("ws_userAvatar");
    m_userAvatar->setCursor(Qt::PointingHandCursor);
    m_userAvatar->installEventFilter(this);
    m_userName = new QLabel("加载中...", this);
    m_userName->setObjectName("ws_userLabel");
    QPushButton* btnMin = createWindowCtrlBtn("ws_minBtn");
    QPushButton* btnMax = createWindowCtrlBtn("ws_maxBtn");
    btnMax->setProperty("isMaximized", false);
    QPushButton* btnClose = createWindowCtrlBtn("ws_closeBtn");
    connect(btnMin, &QPushButton::clicked, this, &WorkSpaceWindow::showMinimized);
    connect(btnMax, &QPushButton::clicked, this, &WorkSpaceWindow::onMaximizeClicked);
    connect(btnClose, &QPushButton::clicked, this, &WorkSpaceWindow::close);
    topLayout->addWidget(m_userAvatar);
    topLayout->addSpacing(10);
    topLayout->addWidget(m_userName);
    QWidget* line = new QWidget(this);
    line->setFixedSize(1, 16);
    line->setObjectName("ws_vLine");
    topLayout->addSpacing(15);
    topLayout->addWidget(line);
    topLayout->addSpacing(15);
    topLayout->addWidget(btnMin);
    topLayout->addWidget(btnMax);
    topLayout->addWidget(btnClose);
    containerLayout->addWidget(topBar);
}

void WorkSpaceWindow::initHomePage(QWidget* pageWidget)
{
    QVBoxLayout* layout = new QVBoxLayout(pageWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addSpacing(30);
    m_homeTitleLabel = new QLabel("工作模式", pageWidget);
    m_homeTitleLabel->setObjectName("ws_mainTitle");
    m_homeTitleLabel->setAlignment(Qt::AlignCenter);
    m_homeTitleLabel->setFixedHeight(AppConfig::instance()->titleHeight());
    layout->addWidget(m_homeTitleLabel);
    QWidget* centerContainer = new QWidget(pageWidget);
    m_centerLayout = new QHBoxLayout(centerContainer);
    m_centerLayout->setContentsMargins(40, 0, 40, 0);
    m_centerLayout->setSpacing(20);
    m_leftBtn = createNavBtn("❮");
    connect(m_leftBtn, &QPushButton::clicked, this, &WorkSpaceWindow::onNavLeftClicked);
    m_centerLayout->addWidget(m_leftBtn);
    m_cardsContainer = new QWidget(centerContainer);
    m_cardsContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QList<ModeCardConfig> configs;
    configs << ModeCardConfig{"场景设置",
                              "直播、录课、线下、等设置",
                              QColor(),
                              QColor(),
                              ModeIconType::Scene,
                              "E:/live/version26/UI/QSS/img/1.png"};
    configs << ModeCardConfig{"直播课程",
                              "实时直播，干货互动两不误",
                              QColor(),
                              QColor(),
                              ModeIconType::Live,
                              "E:/live/version26/UI/QSS/img/2.png"};
    configs << ModeCardConfig{"单画面课程",
                              "固定画面，内容完整呈现",
                              QColor(),
                              QColor(),
                              ModeIconType::Single,
                              "E:/live/version26/UI/QSS/img/3.png"};
    configs << ModeCardConfig{"多画面课程",
                              "多视角录制，课程细节",
                              QColor(),
                              QColor(),
                              ModeIconType::Multi,
                              "E:/live/version26/UI/QSS/img/4.png"};
    configs << ModeCardConfig{"线下课",
                              "线下聚力，成长加速",
                              QColor(),
                              QColor(),
                              ModeIconType::Single,
                              "E:/live/version26/UI/QSS/img/5.png"};
    configs << ModeCardConfig{"AI 助手",
                              "智能分析课程，生成字幕",
                              QColor(),
                              QColor(),
                              ModeIconType::Interactive,
                              "E:/live/version26/UI/QSS/img/6.png"};
    for (auto const& config : configs)
    {
        WorkModeCard* card = new WorkModeCard(config, m_cardsContainer);
        card->setStandardSize(QSize(300, 420));
        connect(card, &WorkModeCard::clicked, this, &WorkSpaceWindow::onModeCardClicked);
        card->show();
        m_cardList.append(card);
    }
    m_centerLayout->addWidget(m_cardsContainer, 1);
    m_rightBtn = createNavBtn("❯");
    connect(m_rightBtn, &QPushButton::clicked, this, &WorkSpaceWindow::onNavRightClicked);
    m_centerLayout->addWidget(m_rightBtn);
    layout->addWidget(centerContainer, 1);
    layout->addSpacing(40);
    m_cardAnim = new QPropertyAnimation(this, "scrollValue", this);
    m_cardAnim->setDuration(550);
    m_cardAnim->setEasingCurve(QEasingCurve::OutQuint);
    connect(m_cardAnim, &QPropertyAnimation::finished, this, [this]() {
        int N = m_cardList.size();
        double norm = fmod(m_scrollValue, N);
        if (norm < 0)
            norm += N;
        m_scrollValue = norm;
        m_targetIndex = qRound(m_scrollValue) % N;
        for (auto card : m_cardList)
            card->setScrollingState(false);
        m_isScrollAnimating = false;
        updateCardsGeometry();
    });
    m_targetIndex = 2;
    setScrollValue(2.0);
    QTimer::singleShot(50, this, [this]() { this->updateCardsGeometry(); });
    updateNavButtons();
}

void WorkSpaceWindow::initTrayIcon()
{
    // 1. 如果系统不支持托盘，直接返回
    if (!QSystemTrayIcon::isSystemTrayAvailable())
    {
        qWarning() << "System tray is not available.";
        return;
    }

    // 2. 创建自定义菜单
    if (!m_trayMenu)
    {
        m_trayMenu = new CustomTrayMenu(this);

        // --- 添加菜单项 (请在此处替换你的图标路径) ---

        // Item 1: 显示主界面
        CustomMenuItem* itemShow = new CustomMenuItem(":/UI/QSS/icon/CS.png", "主界面", m_trayMenu);
        connect(itemShow, &CustomMenuItem::clicked, this, [this]() {
            this->showNormal();
            this->activateWindow();  // 提到最前
        });
        m_trayMenu->addMenuItem(itemShow);

        // 分割线
        m_trayMenu->addSeparator();

        // Item 3: 退出程序
        CustomMenuItem* itemQuit =
            new CustomMenuItem(":/UI/QSS/icon/UilExit.png", "退出", m_trayMenu);
        itemQuit->setTextColor("#dc2626");
        connect(itemQuit, &CustomMenuItem::clicked, this, [this]() {
            // 安全退出应用
            qApp->quit();
        });
        m_trayMenu->addMenuItem(itemQuit);
    }

    // 3. 创建托盘图标
    if (!m_trayIcon)
    {
        m_trayIcon = new QSystemTrayIcon(this);

        // [重要] 设置托盘图标，请确保路径正确，否则托盘不显示
        // 建议使用你的 App Logo
        m_trayIcon->setIcon(QIcon(":/UI/QSS/icon/CS.png"));
        m_trayIcon->setToolTip("Course Studio");

        // 4. 连接激活信号 (点击/右击)
        connect(
            m_trayIcon, &QSystemTrayIcon::activated, this, &WorkSpaceWindow::onTrayIconActivated);

        // 显示托盘
        m_trayIcon->show();
    }
}

void WorkSpaceWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Context || reason == QSystemTrayIcon::Trigger)
    {
        if (m_trayMenu)
        {
            // 在鼠标当前位置显示菜单
            m_trayMenu->showAt(QCursor::pos());
        }
    }
}

void WorkSpaceWindow::updateUIUserInfo(QString const& name, QPixmap const& avatar)
{
    if (m_userName)
        m_userName->setText(QString("你好，%1").arg(name));
    if (m_userAvatar && !avatar.isNull())
    {
        qreal dpr = this->devicePixelRatioF();
        QPixmap scaledPix =
            avatar.scaled(32 * dpr, 32 * dpr, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        scaledPix.setDevicePixelRatio(dpr);
        m_userAvatar->setPixmap(scaledPix);
        m_userAvatar->setScaledContents(true);
        m_userAvatar->setStyleSheet("background-color: transparent;");
    }
}

void WorkSpaceWindow::closeEvent(QCloseEvent* event)
{
    if (m_trayIcon && m_trayIcon->isVisible())
    {
        this->hide();
        event->ignore();
    }
    else
    {
        QWidget::closeEvent(event);
    }
}

void WorkSpaceWindow::updateWindowStyle()
{
    if (!m_outerLayout || !m_mainContainer)
        return;
    QWidget* current = m_mainStack->currentWidget();
    if (qobject_cast<ScreenWindow*>(current))
    {
        if (m_mainContainer->graphicsEffect())
            m_mainContainer->graphicsEffect()->setEnabled(false);
        m_outerLayout->setContentsMargins(0, 0, 0, 0);
        return;
    }
    bool isMax = this->isMaximized();
    if (isMax)
    {
        m_outerLayout->setContentsMargins(0, 0, 0, 0);
        m_radius = 0;
        if (m_mainContainer->graphicsEffect())
            m_mainContainer->graphicsEffect()->setEnabled(false);
    }
    else
    {
        m_outerLayout->setContentsMargins(
            m_shadowMargin, m_shadowMargin, m_shadowMargin, m_shadowMargin);
        m_radius = 16;
        if (m_mainContainer->graphicsEffect())
            m_mainContainer->graphicsEffect()->setEnabled(true);
    }
    m_mainContainer->style()->unpolish(m_mainContainer);
    m_mainContainer->style()->polish(m_mainContainer);
}

void WorkSpaceWindow::updateCardStandardSize(bool forceUpdate)
{
    if (!m_cardsContainer)
        return;
    int containerW = m_cardsContainer->width();
    if (containerW < 100)
        containerW = this->width();
    double scaleFactor = qBound(0.8, (double)containerW / 1200.0, 1.8);
    int targetH = static_cast<int>(380 * scaleFactor);
    int targetW = static_cast<int>(targetH * (300.0 / 420.0));
    QSize newStandardSize(static_cast<int>(targetW * 1.1), static_cast<int>(targetH * 1.1));
    for (auto card : m_cardList)
    {
        card->setStandardSize(newStandardSize);
        if (forceUpdate)
            card->update();
    }
}

bool WorkSpaceWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_userAvatar && event->type() == QEvent::MouseButtonRelease)
    {
        onUserAvatarClicked();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void WorkSpaceWindow::wheelEvent(QWheelEvent* event)
{
    if (m_mainStack->currentIndex() != 0)
    {
        QWidget::wheelEvent(event);
        return;
    }
    int delta = event->angleDelta().y();
    m_wheelAccumulator += delta;
    int const THRESHOLD = 60;
    bool needsTrigger = false;
    while (m_wheelAccumulator >= THRESHOLD)
    {
        m_bufferedScrollSteps--;
        m_wheelAccumulator -= THRESHOLD;
        needsTrigger = true;
    }
    while (m_wheelAccumulator <= -THRESHOLD)
    {
        m_bufferedScrollSteps++;
        m_wheelAccumulator += THRESHOLD;
        needsTrigger = true;
    }
    if (needsTrigger)
    {
        if (m_bufferedScrollSteps > 3)
            m_bufferedScrollSteps = 3;
        if (m_bufferedScrollSteps < -3)
            m_bufferedScrollSteps = -3;
        if (!m_scrollThrottleTimer->isActive())
        {
            onProcessScrollBuffer();
            m_scrollThrottleTimer->start();
        }
    }
    event->accept();
}

void WorkSpaceWindow::onProcessScrollBuffer()
{
    if (m_bufferedScrollSteps == 0)
    {
        m_scrollThrottleTimer->stop();
        return;
    }
    int direction = (m_bufferedScrollSteps > 0) ? 1 : -1;
    m_bufferedScrollSteps -= direction;
    scrollToCardIndex(m_targetIndex + direction);
}

void WorkSpaceWindow::onUserAvatarClicked()
{
    UserProfileWidget* popup = new UserProfileWidget(this);
    if (m_userAvatar)
    {
        QPoint globalPos = m_userAvatar->mapToGlobal(QPoint(0, m_userAvatar->height()));
        popup->move(globalPos.x() - (popup->width() - m_userAvatar->width()) / 2,
                    globalPos.y() + 10);
    }
    popup->updateInfo();
    popup->show();
    connect(popup,
            &UserProfileWidget::logoutRequested,
            m_businessCtrl,
            &WorkSpaceBusinessController::requestLogout);
}

void WorkSpaceWindow::onLogout()
{
    if (m_trayIcon)
        m_trayIcon->hide();
    this->hide();
    this->deleteLater();
    QTimer::singleShot(100, []() {
        LoginWidget* loginWin = new LoginWidget();
        QObject::connect(loginWin, &LoginWidget::loginCompleted, [loginWin]() {
            WorkSpaceWindow* newSpace = new WorkSpaceWindow();
            newSpace->show();
            loginWin->close();
            loginWin->deleteLater();
        });
        loginWin->show();
    });
}

void WorkSpaceWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (!m_inResizeLoop)
    {
        m_inResizeLoop = true;
        suspendChildRendering();
    }
    if (qAbs(event->oldSize().width() - event->size().width()) > 30)
        updateCardsGeometry();
    m_resizeTimer->start(50);
}

void WorkSpaceWindow::updateCardsGeometry()
{
    if (m_cardList.isEmpty() || !m_cardsContainer)
        return;
    int containerW = m_cardsContainer->width();
    if (containerW <= 10)
        containerW = static_cast<int>(this->width() * 0.85);
    int containerH = m_cardsContainer->height();
    int centerX = containerW / 2;
    int centerY = containerH / 2;
    int N = m_cardList.size();
    double scaleFactor = qBound(0.8, (double)containerW / 1200.0, 1.8);
    int baseH = static_cast<int>(380 * scaleFactor);
    int baseW = static_cast<int>(baseH * (300.0 / 420.0));
    int gap = qMax(20, static_cast<int>(m_cardSpacing * scaleFactor));
    struct RenderItem
    {
        WorkModeCard* card;
        int w, h, x, y;
        double diff;
        bool active;
    };
    QList<RenderItem> renderItems;
    for (int i = 0; i < N; ++i)
    {
        double diff = i - m_scrollValue;
        while (diff > N / 2.0)
            diff -= N;
        while (diff < -N / 2.0)
            diff += N;
        double absDiff = qAbs(diff);
        double scale = qMax(0.6, 1.0 - (absDiff * 0.20));
        int w = static_cast<int>(baseW * scale);
        int h = static_cast<int>(baseH * scale);
        int y = centerY - (h / 2);
        double xCenter = centerX;
        double displacement = 0;
        int steps = qFloor(absDiff);
        double remainder = absDiff - steps;
        auto getW = [&](double d) { return static_cast<int>(baseW * qMax(0.6, 1.0 - (d * 0.20))); };
        for (int k = 0; k < steps; ++k)
            displacement += (getW(k) / 2.0) + gap + (getW(k + 1) / 2.0);
        if (remainder > 0.001)
            displacement += ((getW(steps) / 2.0) + gap + (getW(steps + 1) / 2.0)) * remainder;
        xCenter = (diff < 0) ? xCenter - displacement : xCenter + displacement;
        renderItems.append(
            {m_cardList[i], w, h, static_cast<int>(xCenter - w / 2), y, absDiff, absDiff < 0.5});
    }
    std::sort(renderItems.begin(), renderItems.end(), [](RenderItem const& a, RenderItem const& b) {
        return a.diff > b.diff;
    });
    for (auto const& item : renderItems)
    {
        item.card->raise();
        item.card->setGeometry(item.x, item.y, item.w, item.h);
        item.card->setActive(item.active);
        if (item.diff > 2.5)
        {
            if (item.card->isVisible())
                item.card->hide();
        }
        else
        {
            if (item.card->isHidden())
                item.card->show();
        }
    }
}

void WorkSpaceWindow::setScrollValue(qreal value)
{
    m_scrollValue = value;
    updateCardsGeometry();
}

QPushButton* WorkSpaceWindow::createNavBtn(QString const& text)
{
    QPushButton* btn = new QPushButton(text, this);
    btn->setFixedSize(40, 40);
    btn->setCursor(Qt::PointingHandCursor);
    QString navBtnStyle =
        R"(QPushButton { background-color: rgba(255, 255, 255, 0.9); border: 1px solid #E4E7ED; border-radius: 20px; color: #606266; font-size: 16px; font-weight: bold; font-family: "Microsoft YaHei"; padding-bottom: 2px; } QPushButton:hover { background-color: #409EFF; border: 1px solid #409EFF; color: #FFFFFF; } QPushButton:pressed { background-color: #337ECC; border-color: #337ECC; color: #FFFFFF; })";
    btn->setStyleSheet(navBtnStyle);
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(btn);
    shadow->setOffset(0, 2);
    shadow->setColor(QColor(0, 0, 0, 30));
    shadow->setBlurRadius(8);
    btn->setGraphicsEffect(shadow);
    return btn;
}

QPushButton* WorkSpaceWindow::createWindowCtrlBtn(QString const& objName)
{
    QPushButton* btn = new QPushButton(this);
    btn->setFixedSize(45, 30);
    btn->setObjectName(objName);
    btn->setProperty("class", "winCtrlBtn");
    btn->setCursor(Qt::PointingHandCursor);
    return btn;
}

void WorkSpaceWindow::scrollToCardIndex(int index)
{
    if (!m_isScrollAnimating)
    {
        for (auto card : m_cardList)
            card->setScrollingState(true);
    }
    m_isScrollAnimating = true;
    m_cardAnim->stop();
    double startVal = m_scrollValue;
    double endVal = (qreal)index;
    double dist = qAbs(endVal - startVal);
    int duration = qBound(300, static_cast<int>(550 * std::sqrt(dist)), 700);
    m_targetIndex = index;
    m_cardAnim->setDuration(duration);
    m_cardAnim->setStartValue(startVal);
    m_cardAnim->setEndValue(endVal);
    m_cardAnim->setEasingCurve(QEasingCurve::OutQuint);
    m_cardAnim->start();
}

void WorkSpaceWindow::updateNavButtons()
{
    if (m_leftBtn)
        m_leftBtn->setEnabled(true);
    if (m_rightBtn)
        m_rightBtn->setEnabled(true);
}

void WorkSpaceWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && event->pos().y() < 60)
    {
        if (!isMaximized())
        {
            m_isPressed = true;
            m_dragPosition = event->globalPosition().toPoint() - this->frameGeometry().topLeft();
            event->accept();
        }
    }
    QWidget::mousePressEvent(event);
}

void WorkSpaceWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isPressed && (event->buttons() & Qt::LeftButton))
    {
        this->move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    }
    QWidget::mouseMoveEvent(event);
}

void WorkSpaceWindow::mouseReleaseEvent(QMouseEvent* event)
{
    m_isPressed = false;
    QWidget::mouseReleaseEvent(event);
}

void WorkSpaceWindow::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && event->pos().y() < 60)
    {
        onMaximizeClicked();
        event->accept();
    }
    QWidget::mouseDoubleClickEvent(event);
}
