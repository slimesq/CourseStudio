#ifndef SCENEGRAPHICSADAPTER_H
#define SCENEGRAPHICSADAPTER_H

#include <QObject>
#include <QGraphicsScene>
#include <memory>
#include "Scene.h"

class VisualItemProxy;

/**
 * @brief The Bridge between Data (Scene) and View (QGraphicsScene)
 * * Responsibilities:
 * 1. Listen to Scene changes (Add/Remove Items, Resize Canvas) and update the UI Scene.
 * 2. Manage the lifecycle of VisualItemProxy objects.
 * 3. Forward UI commands (like Delete) back to the Scene model.
 * * Design Pattern: Adapter / Mediator
 */
class SceneGraphicsAdapter : public QObject
{
    Q_OBJECT
public:
    SceneGraphicsAdapter(std::shared_ptr<Scene> model,
                         QGraphicsScene* uiScene,
                         QObject* parent = nullptr);
    ~SceneGraphicsAdapter() override;
    void setLiveMode(bool isLive);

signals:
    // [New] Forward request to ScreenWindow
    void requestSourceSwitch(std::shared_ptr<SceneItem> item);

private slots:
    // --- Model -> View Synchronization ---

    // Called when a new source is added to the Scene
    void onModelItemAdded(std::shared_ptr<SceneItem> item);

    // Called when a source is removed from the Scene
    void onModelItemRemoved(std::shared_ptr<SceneItem> item);

    // Called when the canvas resolution changes (e.g. 1920x1080 -> 1280x720)
    void onModelCanvasSizeChanged(int w, int h);

    // --- View -> Model Interaction ---

    // Called when the user clicks the "Delete" button on the Proxy toolbar
    void onProxyRequestDelete(std::shared_ptr<SceneItem> item);

private:
    // Helper: Update the snapping boundary for all existing proxies
    void updateProxiesCanvasRect(QRectF const& rect);

    std::shared_ptr<Scene> m_model;
    QGraphicsScene* m_uiScene;  // Weak pointer, owned by CanvasView
    bool m_isLiveMode = false;
};

#endif  // SCENEGRAPHICSADAPTER_H
