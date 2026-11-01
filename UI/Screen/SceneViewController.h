#ifndef SCENEVIEWCONTROLLER_H
#define SCENEVIEWCONTROLLER_H
#include <QObject>
#include <QListWidget>
#include <QComboBox>
#include <memory>
#include "Scene.h"
#include <QLabel>
#include "Types.h"
#include "VideoEffectSystem.h"

class SceneViewController : public QObject
{
    Q_OBJECT
public:
    explicit SceneViewController(QObject* parent = nullptr);

    // Initialization
    void initData();

    // UI Helpers (Called by View)
    void refreshListUI(QListWidget* listWidget, QComboBox* filterCombo, QLabel* nameLabel);

    // Actions (Called by View)
    void createScene(QWidget* parentDialog);
    void deleteScene(QWidget* parentDialog, Scene* scene);

    // 统一的资源添加入口
    void addResource(QWidget* parentDialog, SourceType type);

    // 资源替换入口
    void switchResourceForItem(QWidget* parentDialog, std::shared_ptr<SceneItem> item);

    void editPermissions(QWidget* parentDialog, Scene* scene);
    void handleOrderChange(std::vector<std::string> const& newOrderUuids);
    void filterCategory(QString const& category);

    // State Access
    std::shared_ptr<Scene> getCurrentScene() const
    {
        return m_currentScene;
    }

    // [New] 批量获取直播场景数据
    void fetchLiveScenes(QList<long long> const& layoutIds);

    bool openCameraSettings(QWidget* parentDialog);
public slots:
    // Lazy Load Callback
    void onSceneSelectedRequest(Scene* rawPtr);

signals:
    void sceneSwitched(std::shared_ptr<Scene> scene);
    void listNeedsRefresh();
    void sceneNameChanged(QString const& name);

    // [New] 单个直播场景加载完成信号
    void liveSceneLoaded(std::shared_ptr<Scene> scene);

private:
    void performSceneSwitch(Scene* ptr);

    // [New] 辅助函数：加载场景并应用偏好
    // layoutId: 云端场景ID
    // targetScene: 要加载数据的本地 Scene 对象
    void loadSceneWithPreference(long long layoutId, std::shared_ptr<Scene> targetScene);

    std::shared_ptr<Scene> m_currentScene;

    std::shared_ptr<EffectManager> getEffectManagerFromItem(std::shared_ptr<SceneItem> item);
};

#endif  // SCENEVIEWCONTROLLER_H
