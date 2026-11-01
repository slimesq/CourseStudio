#include "ScreenWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QToolButton>
#include <QShowEvent>
#include <QScrollBar>
#include <QCursor>
#include <QCloseEvent>
#include <QSizeGrip>
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QPainterPath>
#include "skinmgr.h"
#include "DX11Context.h"
#include "UniversalDialog.h"
#include "SceneListDelegate.h"
#include "DraggableListWidget.h"
#include "SceneManager.h"
#include "VisualItemProxy.h"
#include "Logger.h"
#include <QHideEvent>
#include "AppConfig.h"
#include <QApplication>
#include <QScreen>

// 辅助样式命名空间（仅在此文件内部使用，无需拆分）
namespace Style
{
QLabel* createBadge(QString const& text, QString const& bgColor, QWidget* parent)
{
    QLabel* lbl = new QLabel(text, parent);
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setFixedHeight(16);
    int w = 24 + (text.length() > 2 ? (text.length() - 2) * 8 : 0);
    lbl->setFixedWidth(w);
    lbl->setStyleSheet(QString("QLabel { background-color: %1; color: white; border-radius: 4px; "
                               "font-size: 9px; font-weight: bold; padding-bottom: 1px; }")
                           .arg(bgColor));
    return lbl;
}
}  // namespace Style

// ============================================================================
// ScreenWindow Implementation
// ============================================================================

ScreenWindow::ScreenWindow(WindowMode mode, bool isStandalone, QWidget* parent)
    : QWidget(parent), m_mode(mode), m_isStandalone(isStandalone)
{
    if (!isStandalone)
    {
        setWindowFlags(Qt::Widget);
    }
    else
    {
        setWindowFlags(Qt::Window);
    }

    this->setAutoFillBackground(false);
    setObjectName("ScreenWindow");

    m_liveCtrl = new LiveController(this);
    m_sceneCtrl = new SceneViewController(this);

    initUI();
    initStyle();
    setupConnections();

    QTimer* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &ScreenWindow::updateCpuUsage);
    timer->start(1000);

    m_layerHideTimer = new QTimer(this);
    m_layerHideTimer->setInterval(2000);
    m_layerHideTimer->setSingleShot(true);
    connect(m_layerHideTimer, &QTimer::timeout, this, &ScreenWindow::onLayerAutoCloseTimeout);

    connect(AppConfig::instance(), &AppConfig::configChanged, this, &ScreenWindow::onConfigChanged);
}

ScreenWindow::~ScreenWindow()
{
    if (m_compositor)
    {
        m_compositor->stopRendering();
        delete m_compositor;
    }
    if (m_layerWidget)
    {
        delete m_layerWidget;
        m_layerWidget = nullptr;
    }
    m_liveScenes.clear();
}

void ScreenWindow::startLiveCourse(QList<long long> const& layoutIds, long long roomId)
{
    qDebug() << "[ScreenWindow] Starting Live Course with" << layoutIds.size() << "scenes.";

    m_mode = WindowMode::LiveStreaming;

    setWorkMode(m_mode);

    if (m_canvasView)
        m_canvasView->setLiveMode(true);

    m_liveScenes.clear();
    m_sceneListWidget->clear();
    m_currentLiveScene.reset();

    disconnect(m_sceneCtrl,
               &SceneViewController::listNeedsRefresh,
               this,
               &ScreenWindow::onRefreshSceneList);

    connect(m_sceneCtrl,
            &SceneViewController::liveSceneLoaded,
            this,
            &ScreenWindow::onLiveSceneLoaded,
            Qt::UniqueConnection);

    m_sceneCtrl->fetchLiveScenes(layoutIds);

    if (m_liveCtrl)
    {
        m_liveCtrl->initSimulationData();
        m_liveCtrl->startAudioMonitoring();
        m_liveCtrl->setRoomId(roomId);
    }

    this->show();

    if (m_liveCtrl)
    {
        emit m_liveCtrl->liveStateChanged(m_liveCtrl->isLiveRunning());
    }
}

void ScreenWindow::setWorkMode(WindowMode mode)
{
    if (m_mode == mode)
        return;

    // Cleanup before switch
    if (m_mode == WindowMode::LiveStreaming)
    {
        if (m_compositor)
        {
            m_compositor->stopRendering();
            delete m_compositor;
            m_compositor = nullptr;
        }
        m_liveScenes.clear();
        m_currentLiveScene.reset();
        if (m_canvasView)
        {
            m_canvasView->setSceneModel(nullptr);
            m_canvasView->updateBackgroundImage(QImage(), 0);
        }
    }

    m_mode = mode;
    bool isLive = (m_mode == WindowMode::LiveStreaming);

    if (isLive)
    {
        this->setFixedSize(this->size());
    }
    else
    {
        this->setMinimumSize(0, 0);
        this->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

        // [CRITICAL FIX] Restore the editor state for the current scene
        reloadCurrentSceneInEditor();
    }

    if (m_sceneListWidget)
    {
        auto delegate = qobject_cast<SceneListDelegate*>(m_sceneListWidget->itemDelegate());
        if (delegate)
        {
            delegate->setLiveMode(isLive);
        }
        m_sceneListWidget->viewport()->update();
    }

    if (m_canvasView)
    {
        m_canvasView->setLiveMode(isLive);
    }

    if (m_cmbCategoryFilter && m_cmbCategoryFilter->parentWidget())
    {
        m_cmbCategoryFilter->parentWidget()->setVisible(!isLive);
    }

    if (m_btnAddScene)
    {
        m_btnAddScene->setVisible(!isLive);
    }

    if (m_btnLiveControl)
    {
        m_btnLiveControl->setVisible(isLive);
    }
    if (m_lblLiveDuration)
    {
        m_lblLiveDuration->setVisible(isLive);
    }
}

void ScreenWindow::onLiveSceneLoaded(std::shared_ptr<Scene> scene)
{
    if (!scene)
        return;

    m_liveScenes.push_back(scene);

    QListWidgetItem* item = new QListWidgetItem(m_sceneListWidget);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    item->setData(Qt::UserRole, QVariant::fromValue(reinterpret_cast<quintptr>(scene.get())));
    item->setText(QString::fromStdString(scene->getName()));

    if (m_liveScenes.size() == 1)
    {
        m_sceneListWidget->setCurrentItem(item);
        onSceneSelected(item);
    }
}

void ScreenWindow::onSceneSelected(QListWidgetItem* item)
{
    if (!item)
        return;

    Scene* ptr = reinterpret_cast<Scene*>(item->data(Qt::UserRole).value<quintptr>());

    if (m_mode == WindowMode::LiveStreaming)
    {
        auto it = std::find_if(
            m_liveScenes.begin(), m_liveScenes.end(), [ptr](auto s) { return s.get() == ptr; });
        if (it != m_liveScenes.end())
        {
            std::shared_ptr<Scene> newScene = *it;

            if (m_currentLiveScene && m_currentLiveScene != newScene)
            {
                m_currentLiveScene->deactivate();
            }
            m_currentLiveScene = newScene;
            m_currentLiveScene->activate();

            if (m_compositor)
            {
                m_compositor->stopRendering();
                delete m_compositor;
                m_compositor = nullptr;
            }
            int currentGen = m_canvasView->incrementGeneration();
            m_canvasView->setSceneModel(m_currentLiveScene);

            m_compositor = new SceneCompositor(m_currentLiveScene, this);
            m_compositor->setMode(SceneCompositor::CompositorMode::LiveRunning);
            connect(
                m_compositor,
                &SceneCompositor::frameReady,
                this,
                [this, currentGen](QImage const& img) {
                    if (m_canvasView)
                    {
                        m_canvasView->updateBackgroundImage(img, currentGen);
                    }
                },
                Qt::QueuedConnection);
            connect(m_compositor,
                    &SceneCompositor::yuvFrameReady,
                    m_liveCtrl,
                    &LiveController::handleYuvFrame,
                    Qt::QueuedConnection);

            m_compositor->startRendering();

            if (m_currentLiveScene->getCloudId() > 0)
            {
                ResourceService::instance()->fetchLayoutPreference(
                    m_currentLiveScene->getCloudId());
            }

            m_lblSceneName->setText(QString::fromStdString(m_currentLiveScene->getName()) +
                                    " (直播中)");
        }
    }
    else
    {
        m_sceneCtrl->onSceneSelectedRequest(ptr);
    }
}

void ScreenWindow::setupConnections()
{
    connect(m_sceneCtrl,
            &SceneViewController::listNeedsRefresh,
            this,
            &ScreenWindow::onRefreshSceneList);
    connect(m_sceneCtrl, &SceneViewController::sceneNameChanged, m_lblSceneName, &QLabel::setText);

    if (m_btnAddScene)
    {
        connect(m_btnAddScene, &QPushButton::clicked, [this]() { m_sceneCtrl->createScene(this); });
    }

    connect(m_cmbCategoryFilter, &QComboBox::currentTextChanged, [this](QString const& t) {
        m_sceneCtrl->filterCategory(t);
    });
    connect(m_sceneListWidget, &QListWidget::itemPressed, this, &ScreenWindow::onSceneSelected);

    connect(
        m_sceneCtrl,
        &SceneViewController::sceneSwitched,
        this,
        [this](std::shared_ptr<Scene> scene) {
            if (m_mode == WindowMode::LiveStreaming)
                return;

            qDebug() << "[ScreenWindow] Scene Switched to:"
                     << (scene ? scene->getName().c_str() : "NULL");
            if (m_compositor)
            {
                m_compositor->stopRendering();
                disconnect(m_compositor, nullptr, nullptr, nullptr);
                delete m_compositor;
                m_compositor = nullptr;
            }

            qDebug() << "[ScreenWindow] Forcing canvas clear...";
            int currentGen = m_canvasView->incrementGeneration();
            m_canvasView->setSceneModel(scene);

            if (scene)
            {
                qDebug() << "[ScreenWindow] Creating new compositor for scene:"
                         << scene->getName().c_str();
                m_compositor = new SceneCompositor(scene, this);

                connect(
                    m_compositor,
                    &SceneCompositor::frameReady,
                    this,
                    [this, currentGen](QImage const& img) {
                        if (m_canvasView)
                        {
                            m_canvasView->updateBackgroundImage(img, currentGen);
                        }
                    },
                    Qt::QueuedConnection);
                connect(m_compositor,
                        &SceneCompositor::yuvFrameReady,
                        m_liveCtrl,
                        &LiveController::handleYuvFrame,
                        Qt::QueuedConnection);

                if (m_liveCtrl && m_liveCtrl->isLiveRunning())
                {
                    m_compositor->setMode(SceneCompositor::CompositorMode::LiveRunning);
                }
                else
                {
                    m_compositor->setMode(SceneCompositor::CompositorMode::Editor);
                }

                m_compositor->startRendering();
                qDebug() << "[ScreenWindow] New compositor started.";
            }
            else
            {
                qDebug() << "[ScreenWindow] No scene selected. Compositor not created.";
            }

            if (m_mode == WindowMode::SceneEditor)
            {
                if (m_layerWidget)
                    m_layerWidget->setScene(scene);
                if (scene)
                    connect(
                        scene.get(), &Scene::contentChanged, this, [this]() { showLayerWindow(); });
            }
            else
            {
                if (m_layerWidget)
                    m_layerWidget->hide();
            }

            if (m_canvasView && m_canvasView->scene())
            {
                connect(m_canvasView->scene(), &QGraphicsScene::selectionChanged, this, [this]() {
                    if (m_mode != WindowMode::SceneEditor)
                        return;
                    QList<QGraphicsItem*> selectedItems = m_canvasView->scene()->selectedItems();
                    if (!selectedItems.isEmpty())
                    {
                        showLayerWindow();
                        if (auto proxy = dynamic_cast<VisualItemProxy*>(selectedItems.first()))
                        {
                            auto coreItem = proxy->getCoreItem();
                            if (m_layerWidget && coreItem)
                                m_layerWidget->setSelectedItem(coreItem);
                        }
                    }
                    else
                    {
                        if (m_layerWidget)
                            m_layerWidget->setSelectedItem(nullptr);
                    }
                });
            }
        });

    connect(m_canvasView,
            &CanvasView::sourceSwitchRequested,
            this,
            [this](std::shared_ptr<SceneItem> item) {
                m_sceneCtrl->switchResourceForItem(this, item);
            });

    SceneListDelegate* delegate = new SceneListDelegate(m_sceneListWidget);
    m_sceneListWidget->setItemDelegate(delegate);
    if (m_mode == WindowMode::LiveStreaming)
    {
        delegate->setLiveMode(true);
    }
    connect(delegate, &SceneListDelegate::deleteRequested, [this](Scene* s) {
        m_sceneCtrl->deleteScene(this, s);
    });

    connect(m_sceneListWidget->model(),
            &QAbstractItemModel::rowsMoved,
            [this](QModelIndex const&, int, int, QModelIndex const&, int) {
                std::vector<std::string> uuids;
                for (int i = 0; i < m_sceneListWidget->count(); ++i)
                {
                    Scene* s = reinterpret_cast<Scene*>(
                        m_sceneListWidget->item(i)->data(Qt::UserRole).value<quintptr>());
                    if (s)
                        uuids.push_back(s->getUuid().toStdString());
                }
                m_sceneCtrl->handleOrderChange(uuids);
            });

    auto updateLiveBtnState = [this](bool isRunning) {
        if (isRunning)
        {
            m_btnLiveControl->setText("停止直播");
            m_btnLiveControl->setProperty("liveState", true);
            if (m_mode == WindowMode::LiveStreaming)
                m_lblLiveDuration->setVisible(true);
        }
        else
        {
            m_btnLiveControl->setText("开始直播");
            m_btnLiveControl->setProperty("liveState", false);
            m_lblLiveDuration->setVisible(false);
        }
        m_btnLiveControl->style()->unpolish(m_btnLiveControl);
        m_btnLiveControl->style()->polish(m_btnLiveControl);

        if (m_compositor)
        {
            if (isRunning)
            {
                m_compositor->setMode(SceneCompositor::CompositorMode::LiveRunning);
            }
            else
            {
                m_compositor->setMode(SceneCompositor::CompositorMode::Editor);
            }
        }

        if (m_liveCtrl)
        {
            // 定义数据保存目录 (例如: exe目录/ai_data)
            QString aiSaveDir = QCoreApplication::applicationDirPath() + "/ai_data";

            // isRunning 为 true 时启动，为 false 时停止
            m_liveCtrl->enableAIAnalysis(isRunning, aiSaveDir);
        }
    };

    connect(m_liveCtrl, &LiveController::liveStateChanged, this, updateLiveBtnState);
    updateLiveBtnState(m_liveCtrl->isLiveRunning());

    connect(m_liveCtrl, &LiveController::durationUpdated, m_lblLiveDuration, &QLabel::setText);
    connect(m_liveCtrl,
            &LiveController::errorOccurred,
            this,
            [this](QString const& t, QString const& m) { UniversalDialog::showError(this, t, m); });

    connect(m_liveCtrl, &LiveController::viewerAdded, this, &ScreenWindow::appendViewerToUI);
    connect(m_liveCtrl, &LiveController::messageReceived, this, &ScreenWindow::appendMessageToUI);

    auto mapLevel = [](float rms) -> float { return std::min(1.0f, std::sqrt(rms) * 1.2f); };

    connect(m_liveCtrl, &LiveController::micVolumeLevel, this, [this, mapLevel](float l) {
        if (m_meterMic)
            m_meterMic->setLevel(mapLevel(l));
    });
    connect(m_liveCtrl, &LiveController::sysVolumeLevel, this, [this, mapLevel](float l) {
        if (m_meterSys)
            m_meterSys->setLevel(mapLevel(l));
    });

    connect(m_btnLiveControl, &QPushButton::clicked, m_liveCtrl, &LiveController::toggleLiveState);
}

void ScreenWindow::reloadCurrentSceneInEditor()
{
    auto scene = m_sceneCtrl->getCurrentScene();
    if (!scene)
        return;

    qDebug() << "[ScreenWindow] Force reloading scene for Editor:" << scene->getName().c_str();

    // 1. Clean up old compositor
    if (m_compositor)
    {
        m_compositor->stopRendering();
        delete m_compositor;
        m_compositor = nullptr;
    }

    // 2. Setup Canvas
    int currentGen = m_canvasView->incrementGeneration();
    m_canvasView->setSceneModel(scene);

    // 3. Create new Compositor
    m_compositor = new SceneCompositor(scene, this);
    m_compositor->setMode(SceneCompositor::CompositorMode::Editor);

    connect(
        m_compositor,
        &SceneCompositor::frameReady,
        this,
        [this, currentGen](QImage const& img) {
            if (m_canvasView)
            {
                m_canvasView->updateBackgroundImage(img, currentGen);
            }
        },
        Qt::QueuedConnection);

    // Note: No need to connect yuvFrameReady in Editor mode usually, unless you allow recording in editor.

    m_compositor->startRendering();

    // 4. Setup Layer Widget
    if (m_layerWidget)
    {
        m_layerWidget->setScene(scene);
    }

    // 5. Ensure signals are connected (using UniqueConnection to be safe)
    connect(
        scene.get(),
        &Scene::contentChanged,
        this,
        [this]() { showLayerWindow(); },
        Qt::UniqueConnection);
}

void ScreenWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // 重新安装过滤器
    if (QWidget* topLevel = this->window())
    {
        if (topLevel != this)
        {
            topLevel->removeEventFilter(this);  // 先移除以防重复
            topLevel->installEventFilter(this);
        }
    }

    if (QWidget* topLevel = this->window())
    {
        topLevel->removeEventFilter(this);
        topLevel->installEventFilter(this);
    }

    if (m_mode == WindowMode::SceneEditor)
    {
        m_sceneCtrl->initData();
    }

    if (m_isFirstLoad)
    {
        m_isFirstLoad = false;
        onConfigChanged();
    }

    if (m_mode == WindowMode::LiveStreaming && m_layerWidget)
        m_layerWidget->hide();
    if (m_mode == WindowMode::LiveStreaming)
    {
        emit m_viewersPanel->floatStatusChanged(m_viewersPanel->isFloating());
    }
}

void ScreenWindow::onConfigChanged()
{
    if (m_mode != WindowMode::LiveStreaming)
        return;

    bool showViewers = AppConfig::instance()->showViewerPanel();
    bool showMessages = AppConfig::instance()->showMessagePanel();

    if (m_viewersPanel)
        m_viewersPanel->setVisible(showViewers);
    if (m_messagesPanel)
        m_messagesPanel->setVisible(showMessages);

    if (!showViewers && !showMessages)
    {
        m_rightPanelContainer->setFixedWidth(0);
        m_rightPanelContainer->hide();
    }
    else
    {
        m_rightPanelContainer->setFixedWidth(320);
        m_rightPanelContainer->show();
    }
}

void ScreenWindow::onRefreshSceneList()
{
    if (m_mode == WindowMode::LiveStreaming)
        return;

    m_sceneCtrl->refreshListUI(m_sceneListWidget, m_cmbCategoryFilter, m_lblSceneName);

    if (m_sceneListWidget->count() > 0)
    {
        if (m_sceneListWidget->selectedItems().isEmpty())
        {
            m_sceneListWidget->setCurrentRow(0);
            onSceneSelected(m_sceneListWidget->item(0));
        }
    }
}

void ScreenWindow::appendViewerToUI(QString const& name, int level, bool isAdmin)
{
    if (!m_viewerList)
        return;
    QListWidgetItem* item = new QListWidgetItem(m_viewerList);
    QWidget* widget = new QWidget;
    widget->setFixedHeight(40);
    widget->setStyleSheet("background: transparent;");
    QHBoxLayout* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(10, 5, 10, 5);
    layout->setSpacing(8);
    QLabel* avatar = new QLabel(widget);
    avatar->setFixedSize(28, 28);
    avatar->setStyleSheet(
        "background-color: #4C4D4F; border-radius: 14px; color: #FFF; font-size: 10px;");
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setText(name.left(1));
    layout->addWidget(avatar);
    QVBoxLayout* textLayout = new QVBoxLayout;
    textLayout->setSpacing(2);
    textLayout->setContentsMargins(0, 2, 0, 2);
    QHBoxLayout* nameRow = new QHBoxLayout;
    nameRow->setSpacing(5);
    QLabel* nameLbl = new QLabel(name, widget);
    nameLbl->setStyleSheet(isAdmin ? "color: #FF7043; font-weight: bold;" : "color: #606266;");
    nameRow->addWidget(nameLbl);
    if (isAdmin)
        nameRow->addWidget(Style::createBadge("管", "#F56C6C", widget));
    nameRow->addWidget(Style::createBadge("Lv." + QString::number(level), "#409EFF", widget));
    nameRow->addStretch();
    textLayout->addLayout(nameRow);
    layout->addLayout(textLayout);
    item->setSizeHint(widget->sizeHint());
    m_viewerList->setItemWidget(item, widget);
}

void ScreenWindow::appendMessageToUI(QString const& name, QString const& msg, int level)
{
    if (!m_msgList)
        return;
    QListWidgetItem* item = new QListWidgetItem(m_msgList);
    QWidget* widget = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(4);
    QHBoxLayout* header = new QHBoxLayout;
    header->setSpacing(6);
    QLabel* badge = Style::createBadge(
        "Lv." + QString::number(level), level > 40 ? "#E6A23C" : "#909399", widget);
    header->addWidget(badge);
    QLabel* nameLbl = new QLabel(name + ":", widget);
    nameLbl->setStyleSheet("color: #909399; font-size: 12px;");
    header->addWidget(nameLbl);
    header->addStretch();
    layout->addLayout(header);
    QLabel* msgLbl = new QLabel(msg, widget);
    msgLbl->setWordWrap(true);
    msgLbl->setStyleSheet("color: #303133; font-size: 13px; line-height: 120%; margin-left: 2px;");
    layout->addWidget(msgLbl);
    widget->adjustSize();
    item->setSizeHint(QSize(0, widget->height()));
    m_msgList->setItemWidget(item, widget);
    m_msgList->scrollToBottom();
}

void ScreenWindow::initUI()
{
    QFrame* mainFrame = new QFrame(this);
    mainFrame->setObjectName("ScreenMainFrame");
    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(mainFrame);
    QVBoxLayout* mainLayout = new QVBoxLayout(mainFrame);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    QWidget* bodyWidget = new QWidget(mainFrame);
    bodyWidget->setObjectName("BodyWidget");
    QHBoxLayout* bodyLayout = new QHBoxLayout(bodyWidget);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    QWidget* leftPanel = new QWidget(bodyWidget);
    leftPanel->setObjectName("LeftPanel");
    leftPanel->setFixedWidth(220);
    initLeftPanel(leftPanel);
    bodyLayout->addWidget(leftPanel);

    QWidget* centerPanel = new QWidget(bodyWidget);
    centerPanel->setObjectName("CenterPanel");
    initCenterPanel(centerPanel);
    bodyLayout->addWidget(centerPanel, 1);

    m_rightPanelContainer = new QWidget(bodyWidget);
    m_rightPanelContainer->setObjectName("RightPanel");
    int rightWidth = (m_mode == WindowMode::LiveStreaming) ? 320 : 80;
    m_rightPanelContainer->setFixedWidth(rightWidth);
    initRightPanel(m_rightPanelContainer);
    bodyLayout->addWidget(m_rightPanelContainer);

    mainLayout->addWidget(bodyWidget, 1);
    DX11Context::instance().init((void*)this->winId(), 1920, 1080);
}

void ScreenWindow::initRightPanel(QWidget* parent)
{
    m_rightPanelLayout = new QVBoxLayout(parent);
    if (m_mode == WindowMode::SceneEditor)
    {
        m_rightPanelLayout->setContentsMargins(0, 15, 0, 15);
        m_rightPanelLayout->setSpacing(15);
        m_rightPanelLayout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        auto addBtn = [&](QString const& txt, SourceType type) {
            QToolButton* btn = new QToolButton(parent);
            btn->setText(txt);
            btn->setObjectName("RightToolBtn");
            btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            btn->setFixedSize(60, 60);
            QPixmap pix(32, 32);
            pix.fill(Qt::transparent);
            QPainter p(&pix);
            p.setPen(QPen(QColor(96, 98, 102), 2));
            p.drawRoundedRect(4, 4, 24, 24, 4, 4);
            btn->setIcon(QIcon(pix));
            btn->setIconSize(QSize(28, 28));
            connect(btn, &QToolButton::clicked, [this, type]() {
                m_sceneCtrl->addResource(this, type);
            });
            m_rightPanelLayout->addWidget(btn);
        };
        addBtn("摄像头", SourceType::Camera);
        addBtn("图片", SourceType::Image);
        addBtn("显示器", SourceType::Monitor);
        addBtn("窗口", SourceType::ScreenCapture_WGC);
        // ============================================================
        // [Modified] 特效按钮 (简化版)
        // ============================================================

        // 分割线
        QFrame* line = new QFrame(parent);
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet("color: #E4E7ED; margin: 10px 5px;");
        m_rightPanelLayout->addWidget(line);

        // 特效按钮 - 直接作为一级控件
        QToolButton* btnEffect = new QToolButton(parent);
        btnEffect->setText("特效设置");
        btnEffect->setObjectName("BtnEffect");
        btnEffect->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btnEffect->setFixedSize(60, 60);
        btnEffect->setCursor(Qt::PointingHandCursor);

        // 样式优化：去掉边框，增加悬停效果
        btnEffect->setStyleSheet(
            "QToolButton { border: none; background: transparent; color: #606266; font-size: 12px; "
            "}"
            "QToolButton:hover { background-color: #F2F6FC; border-radius: 6px; color: #409EFF; }"
            "QToolButton:pressed { background-color: #EBEEF5; }");

        // 图标
        QPixmap pix(32, 32);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#FAAD14"), 2));  // 金色线条
        p.drawLine(8, 24, 24, 8);
        p.setBrush(QColor("#FAAD14"));
        p.setPen(Qt::NoPen);
        p.drawEllipse(22, 6, 3, 3);
        p.drawEllipse(18, 12, 2, 2);
        btnEffect->setIcon(QIcon(pix));
        btnEffect->setIconSize(QSize(28, 28));

        // 点击直接打开
        connect(
            btnEffect, &QToolButton::clicked, [this]() { m_sceneCtrl->openCameraSettings(this); });

        m_rightPanelLayout->addWidget(btnEffect, 0, Qt::AlignHCenter);  // 居中添加
    }
    else
    {
        m_rightPanelLayout->setContentsMargins(10, 10, 20, 10);
        m_rightPanelLayout->setSpacing(10);
        m_viewerList = new QListWidget(parent);
        m_viewerList->setObjectName("ViewerList");
        m_viewerList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        m_viewersPanel = new LiveDockPanel("在线观众", m_viewerList, parent);
        m_msgList = new QListWidget(parent);
        m_msgList->setObjectName("MsgList");
        m_msgList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        m_messagesPanel = new LiveDockPanel("互动消息", m_msgList, parent);
        auto updateLayout = [this](bool) {
            if (m_mode != WindowMode::LiveStreaming)
                return;
            bool vFloat = m_viewersPanel->isFloating();
            bool mFloat = m_messagesPanel->isFloating();
            if (vFloat)
                m_rightPanelLayout->removeWidget(m_viewersPanel);
            else if (m_rightPanelLayout->indexOf(m_viewersPanel) == -1 &&
                     m_viewersPanel->isVisible())
                m_rightPanelLayout->insertWidget(0, m_viewersPanel, 1);
            if (mFloat)
                m_rightPanelLayout->removeWidget(m_messagesPanel);
            else if (m_rightPanelLayout->indexOf(m_messagesPanel) == -1 &&
                     m_messagesPanel->isVisible())
                m_rightPanelLayout->addWidget(m_messagesPanel, 2);
        };
        connect(m_viewersPanel, &LiveDockPanel::floatStatusChanged, this, updateLayout);
        connect(m_messagesPanel, &LiveDockPanel::floatStatusChanged, this, updateLayout);
        m_rightPanelLayout->addWidget(m_viewersPanel, 1);
        m_rightPanelLayout->addWidget(m_messagesPanel, 2);
    }
}

void ScreenWindow::initLeftPanel(QWidget* parent)
{
    QVBoxLayout* layout = new QVBoxLayout(parent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    QWidget* filterWidget = new QWidget(parent);
    filterWidget->setObjectName("FilterWidget");
    filterWidget->setFixedHeight(56);

    QHBoxLayout* filterLayout = new QHBoxLayout(filterWidget);
    filterLayout->setContentsMargins(15, 10, 15, 10);
    m_cmbCategoryFilter = new QComboBox(filterWidget);
    m_cmbCategoryFilter->setObjectName("ComboFilter");

    // [企业级修改 1] 高度调小：由原来的 28px 左右调小一半，实际体感高度设为 24px
    m_cmbCategoryFilter->setFixedHeight(24);
    m_cmbCategoryFilter->setMaxVisibleItems(5);

    // [企业级修改 2] 定制弹出视图
    QListView* listView = new QListView(m_cmbCategoryFilter);
    m_cmbCategoryFilter->setView(listView);

    // [企业级修改 3] 关键：由于我们要圆角和阴影，必须让视图容器透明
    QWidget* container = listView->window();  // 获取下拉弹出窗体
    if (container)
    {
        // 开启无边框和透明支持，这样阴影才能显示出来
        container->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    }

    m_cmbCategoryFilter->setCursor(Qt::PointingHandCursor);
    filterLayout->addWidget(m_cmbCategoryFilter, 1);
    layout->addWidget(filterWidget);
    if (m_mode == WindowMode::LiveStreaming)
    {
        filterWidget->setVisible(false);
        filterWidget->setFixedHeight(0);
    }
    m_sceneListWidget = new DraggableListWidget(parent);
    m_sceneListWidget->setObjectName("SceneList");
    m_sceneListWidget->setSpacing(2);
    m_sceneListWidget->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_sceneListWidget, 1);

    if (m_mode == WindowMode::SceneEditor)
    {
        m_btnAddScene = new QPushButton("+ 新建场景", parent);
        m_btnAddScene->setObjectName("BtnAddScene");
        m_btnAddScene->setFixedHeight(48);
        layout->addWidget(m_btnAddScene);
    }
    else
    {
        m_btnAddScene = nullptr;
    }
}
void ScreenWindow::initCenterPanel(QWidget* parent)
{
    QVBoxLayout* layout = new QVBoxLayout(parent);
    layout->setContentsMargins(0, 0, 0, 0);
    QWidget* topBar = new QWidget(parent);
    topBar->setObjectName("CenterTopBar");
    topBar->setFixedHeight(40);
    QHBoxLayout* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(20, 0, 20, 0);
    m_lblSceneName = new QLabel("当前场景", topBar);
    m_lblSceneName->setObjectName("SceneNameLabel");
    topLayout->addWidget(m_lblSceneName);
    topLayout->addStretch();
    layout->addWidget(topBar);
    m_canvasView = new CanvasView(parent);
    m_canvasView->setObjectName("MainCanvas");
    m_canvasView->setLiveMode(m_mode == WindowMode::LiveStreaming);
    layout->addWidget(m_canvasView, 1);
    QWidget* bottomBar = new QWidget(parent);
    bottomBar->setObjectName("BottomBar");
    bottomBar->setFixedHeight(50);
    QHBoxLayout* botLayout = new QHBoxLayout(bottomBar);
    botLayout->setContentsMargins(20, 0, 20, 0);
    botLayout->setSpacing(15);
    botLayout->addStretch();
    m_lblCpuUsage = new QLabel("CPU: 0.00%", bottomBar);
    m_lblFps = new QLabel("帧率: 30", bottomBar);
    botLayout->addWidget(m_lblCpuUsage);
    botLayout->addWidget(m_lblFps);

    if (m_mode == WindowMode::LiveStreaming)
    {
        m_meterMic = new AudioMeterWidget("MIC", bottomBar);
        m_meterSys = new AudioMeterWidget("SYS", bottomBar);
        botLayout->addWidget(m_meterMic);
        botLayout->addWidget(m_meterSys);
    }

    m_lblLiveDuration = new QLabel("00:00:00", bottomBar);
    m_lblLiveDuration->setObjectName("LiveDurationLabel");
    m_lblLiveDuration->setVisible(false);
    m_btnLiveControl = new QPushButton("开始直播", bottomBar);
    m_btnLiveControl->setObjectName("BtnLiveControl");
    m_btnLiveControl->setFixedSize(90, 30);
    if (m_mode == WindowMode::LiveStreaming)
    {
        QFrame* line = new QFrame(bottomBar);
        line->setFrameShape(QFrame::VLine);
        line->setFrameShadow(QFrame::Sunken);
        line->setFixedHeight(20);
        line->setStyleSheet("color: #555;");
        botLayout->addWidget(line);
        botLayout->addWidget(m_lblLiveDuration);
        botLayout->addWidget(m_btnLiveControl);
        m_btnLiveControl->show();
    }
    else
    {
        m_lblLiveDuration->hide();
        m_btnLiveControl->hide();
    }
    QPushButton* btnSet = new QPushButton("⚙", bottomBar);
    btnSet->setFixedSize(30, 30);
    btnSet->setObjectName("BtnSetting");
    if (m_mode == WindowMode::SceneEditor)
    {
        btnSet->hide();
    }
    connect(btnSet, &QPushButton::clicked, [this]() {
        if (m_liveCtrl)
            m_liveCtrl->showSettings(this);
    });
    botLayout->addWidget(btnSet);
    layout->addWidget(bottomBar);
}
void ScreenWindow::initStyle()
{
    SkinMgr::inst()->setSkin(SK_Screen, this);
}
void ScreenWindow::updateCpuUsage()
{
    static int counter = 0;
    if (++counter % 2 == 0)
    {
        m_lblCpuUsage->setText(QString("CPU: %1%").arg(0.5 + (rand() % 10) / 10.0));
        m_lblFps->setText(QString("帧率: 30"));
    }
}
void ScreenWindow::showLayerWindow()
{
    if (m_mode != WindowMode::SceneEditor)
        return;
    if (!this->isVisible())
        return;
    if (!m_layerWidget)
    {
        m_layerWidget = new LayerManagerWidget(nullptr);
        if (auto scene = m_sceneCtrl->getCurrentScene())
            m_layerWidget->setScene(scene);
    }
    updateLayerWindowPosition();
    if (m_layerWidget->isHidden())
        m_layerWidget->show();
    m_layerWidget->raise();
    if (m_layerHideTimer)
        m_layerHideTimer->start();
}
void ScreenWindow::updateLayerWindowPosition()
{
    if (!m_layerWidget)
        return;
    int rightOffset = (m_rightPanelContainer && m_rightPanelContainer->isVisible())
        ? m_rightPanelContainer->width()
        : 80;
    if (m_mode == WindowMode::LiveStreaming && m_rightPanelContainer &&
        !m_rightPanelContainer->isVisible())
        rightOffset = 10;
    QPoint globalPos = this->mapToGlobal(QPoint(0, 0));
    int bottomBarHeight = 50;
    int margin = 10;
    int targetX = globalPos.x() + this->width() - rightOffset - m_layerWidget->width() - margin;
    int targetY =
        globalPos.y() + this->height() - bottomBarHeight - m_layerWidget->height() - margin;
    m_layerWidget->move(targetX, targetY);
}
void ScreenWindow::onLayerAutoCloseTimeout()
{
    if (m_mode != WindowMode::SceneEditor)
        return;
    if (m_layerWidget && m_layerWidget->isVisible())
    {
        if (m_layerWidget->geometry().contains(QCursor::pos()))
        {
            m_layerHideTimer->start();
            return;
        }
        m_layerWidget->hide();
    }
}
void ScreenWindow::suspendRendering()
{
    if (m_compositor)
        m_compositor->stopRendering();
    if (m_layerWidget)
        m_layerWidget->hide();
}
void ScreenWindow::resumeRendering()
{
    if (m_compositor)
        m_compositor->startRendering(30);
    else if (!m_sceneCtrl->getCurrentScene() && m_sceneListWidget->count() > 0)
        onSceneSelected(m_sceneListWidget->item(0));
}
bool ScreenWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == this->window())
    {
        if (event->type() == QEvent::Move || event->type() == QEvent::Resize)
        {
            if (m_mode == WindowMode::SceneEditor && m_layerWidget && m_layerWidget->isVisible())
                updateLayerWindowPosition();
        }

        // [Fix] 窗口失焦（如切换到其他App或被顶置窗口覆盖）时强制隐藏图层
        if (event->type() == QEvent::WindowDeactivate)
        {
            if (m_layerWidget)
                m_layerWidget->hide();
        }

        else if (event->type() == QEvent::Hide || event->type() == QEvent::Close)
        {
            if (m_layerWidget)
                m_layerWidget->hide();
            if (m_viewersPanel && m_viewersPanel->isFloating())
                m_viewersPanel->close();
            if (m_messagesPanel && m_messagesPanel->isFloating())
                m_messagesPanel->close();
        }
    }
    return QWidget::eventFilter(watched, event);
}
void ScreenWindow::hideEvent(QHideEvent* event)
{
    // [Fix 3] 隐藏时移除父窗口的事件过滤器，防止干扰
    if (QWidget* topLevel = this->window())
    {
        if (topLevel != this)
        {
            topLevel->removeEventFilter(this);
        }
    }

    if (m_layerWidget)
        m_layerWidget->hide();
    QWidget::hideEvent(event);
}
