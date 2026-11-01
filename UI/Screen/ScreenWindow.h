#ifndef SCREENWINDOW_H
#define SCREENWINDOW_H
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QToolButton>
#include <memory>
#include <QTimer>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QHideEvent>
#include <QPainter>
#include <QWidget>
#include "SceneCompositor.h"
#include "CanvasView.h"
#include "LiveController.h"
#include "SceneViewController.h"
#include "LayerManagerWidget.h"
#include "AudioMeterWidget.h"  // 引入新文件
#include "LiveDockPanel.h"     // 引入新文件
#include <QPointer>

class ScreenWindow : public QWidget
{
    Q_OBJECT

public:
    enum class WindowMode
    {
        SceneEditor,
        LiveStreaming
    };

    explicit ScreenWindow(WindowMode mode = WindowMode::SceneEditor,
                          bool isStandalone = true,
                          QWidget* parent = nullptr);
    ~ScreenWindow();

    void suspendRendering();
    void resumeRendering();

    // [Modified] 接收场景 ID 列表，启动直播课程
    void startLiveCourse(QList<long long> const& layoutIds, long long roomId);

    // [新增] 切换工作模式接口
    void setWorkMode(WindowMode mode);
    WindowMode windowMode() const
    {
        return m_mode;
    }

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // UI Initialization
    void initUI();
    void initStyle();
    void initLeftPanel(QWidget* parent);
    void initCenterPanel(QWidget* parent);
    void initRightPanel(QWidget* parent);
    void setupConnections();

    void reloadCurrentSceneInEditor();

    // UI Updates
    void appendViewerToUI(QString const& name, int level, bool isAdmin);
    void appendMessageToUI(QString const& name, QString const& msg, int level);

    // Layer Window Management
    void showLayerWindow();
    void updateLayerWindowPosition();

private slots:
    void onRefreshSceneList();
    void onSceneSelected(QListWidgetItem* item);
    void updateCpuUsage();
    void onLayerAutoCloseTimeout();
    void onConfigChanged();

    // [New] 接收加载到的直播场景
    void onLiveSceneLoaded(std::shared_ptr<Scene> scene);

private:
    WindowMode m_mode;

    // UI Elements
    QComboBox* m_cmbCategoryFilter;
    QListWidget* m_sceneListWidget;
    QPushButton* m_btnAddScene;
    QPointer<CanvasView> m_canvasView;
    QLabel* m_lblSceneName;
    QLabel* m_lblCpuUsage;
    QLabel* m_lblFps;
    QPushButton* m_btnLiveControl;
    QLabel* m_lblLiveDuration;

    AudioMeterWidget* m_meterMic = nullptr;
    AudioMeterWidget* m_meterSys = nullptr;

    QWidget* m_rightPanelContainer;
    QVBoxLayout* m_rightPanelLayout;

    // Panels
    LiveDockPanel* m_viewersPanel = nullptr;
    LiveDockPanel* m_messagesPanel = nullptr;
    QListWidget* m_viewerList = nullptr;
    QListWidget* m_msgList = nullptr;

    // Controllers
    LiveController* m_liveCtrl;
    SceneViewController* m_sceneCtrl;

    // Rendering & Layers
    SceneCompositor* m_compositor = nullptr;
    LayerManagerWidget* m_layerWidget = nullptr;
    QTimer* m_layerHideTimer = nullptr;

    bool m_isFirstLoad = true;
    bool m_isWindowMoving = false;

    // [New] 直播模式下持有的临时场景列表
    std::vector<std::shared_ptr<Scene>> m_liveScenes;
    // 当前激活的直播场景
    std::shared_ptr<Scene> m_currentLiveScene;

    // [Fix 2] 添加成员变量记录是否为独立窗口
    bool m_isStandalone = true;
};

#endif  // SCREENWINDOW_H
