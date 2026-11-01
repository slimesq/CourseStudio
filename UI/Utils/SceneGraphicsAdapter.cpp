#include "SceneGraphicsAdapter.h"
#include "VisualItemProxy.h"
#include <QDebug>
#include <QGraphicsItem>

SceneGraphicsAdapter::SceneGraphicsAdapter(std::shared_ptr<Scene> model,
                                           QGraphicsScene* uiScene,
                                           QObject* parent)
    : QObject(parent), m_model(model), m_uiScene(uiScene)
{
    if (!m_model || !m_uiScene)
        return;

    // 1. Initial Sync: Canvas Size
    QRectF r = m_model->getCanvasRect();
    m_uiScene->setSceneRect(r);

    // 2. Initial Sync: Existing Items
    auto items = m_model->getItems();
    for (auto const& item : items)
    {
        onModelItemAdded(item);
    }

    // [新增修复] 初始化后，自动选中最顶层的 Item
    // QGraphicsScene::items() 默认按 Z-Order 降序返回 (index 0 就是最上面的)
    QList<QGraphicsItem*> allUiItems = m_uiScene->items();
    if (!allUiItems.isEmpty())
    {
        // 尝试找到第一个是 VisualItemProxy 类型的 Item (过滤掉背景或其他辅助线)
        for (QGraphicsItem* gItem : allUiItems)
        {
            if (auto proxy = dynamic_cast<VisualItemProxy*>(gItem))
            {
                // 1. 选中它
                proxy->setSelected(true);
                // 2. 赋予焦点 (确保键盘事件直接可用)
                proxy->setFocus();
                // 3. 确保图层窗口能收到通知并高亮
                // VisualItemProxy 内部选中有逻辑触发 update，但为了稳妥，也可以手动触发一次
                break;  // 只选中最顶层的一个
            }
        }
    }

    // 3. Establish Signals
    connect(m_model.get(), &Scene::itemAdded, this, &SceneGraphicsAdapter::onModelItemAdded);
    connect(m_model.get(), &Scene::itemRemoved, this, &SceneGraphicsAdapter::onModelItemRemoved);
    connect(m_model.get(),
            &Scene::canvasSizeChanged,
            this,
            &SceneGraphicsAdapter::onModelCanvasSizeChanged);
}

SceneGraphicsAdapter::~SceneGraphicsAdapter()
{
    // When adapter is destroyed, we disconnect from the model
    if (m_model)
    {
        m_model->disconnect(this);
    }
    // Note: We do NOT clear the m_uiScene here because CanvasView manages the scene's lifecycle.
    // However, usually CanvasView clears the scene before creating a new Adapter.
}

void SceneGraphicsAdapter::setLiveMode(bool isLive)
{
    m_isLiveMode = isLive;
    // Update existing items
    QList<QGraphicsItem*> items = m_uiScene->items();
    for (auto* item : items)
    {
        if (auto* proxy = dynamic_cast<VisualItemProxy*>(item))
        {
            proxy->setLiveMode(isLive);
        }
    }
}

void SceneGraphicsAdapter::onModelItemAdded(std::shared_ptr<SceneItem> item)
{
    if (!item || !m_uiScene)
        return;

    // [Layered Rendering]
    // We create a "Ghost" proxy that is transparent but handles interaction.
    // The VisualItemProxy constructor handles connecting the "transformChanged" signal
    // from the item to itself to update its own position.
    auto proxy = new VisualItemProxy(item);

    // Pass the current canvas bounds to the proxy so it knows where to snap
    proxy->setCanvasRect(m_uiScene->sceneRect());
    // [New] Set initial state
    proxy->setLiveMode(m_isLiveMode);

    // [New] Connect Switch Signal
    connect(proxy,
            &VisualItemProxy::switchSourceRequested,
            this,
            &SceneGraphicsAdapter::requestSourceSwitch);

    // Listen for delete requests originating from the View (Toolbar/Right-click)
    connect(proxy,
            &VisualItemProxy::deleteRequested,
            this,
            &SceneGraphicsAdapter::onProxyRequestDelete);

    // Add to the graphics scene
    m_uiScene->addItem(proxy);
}

void SceneGraphicsAdapter::onModelItemRemoved(std::shared_ptr<SceneItem> item)
{
    if (!item || !m_uiScene)
        return;

    QList<QGraphicsItem*> uiItems = m_uiScene->items();
    for (QGraphicsItem* uiItem : uiItems)
    {
        VisualItemProxy* proxy = dynamic_cast<VisualItemProxy*>(uiItem);
        if (proxy && proxy->getCoreItem() == item)
        {
            // [FIX] 安全移除流程

            // 1. 立即取消选中状态，防止 Scene 内部状态表持有它
            proxy->setSelected(false);

            // 2. 清除焦点
            if (proxy->hasFocus())
                proxy->clearFocus();

            // 3. 禁用并隐藏
            // 这会通知 Scene 该 Item 不再参与碰撞检测，从 BSP 树中安全标记为 dirty
            proxy->setEnabled(false);
            proxy->setVisible(false);

            // 4. 从场景移除
            m_uiScene->removeItem(proxy);

            // 5. 延迟内存释放
            proxy->deleteLater();

            break;
        }
    }
}

void SceneGraphicsAdapter::onModelCanvasSizeChanged(int w, int h)
{
    if (!m_uiScene)
        return;

    QRectF newRect(0, 0, w, h);

    // 1. Update the QGraphicsScene boundary
    // This triggers CanvasView to re-center/fit the view
    m_uiScene->setSceneRect(newRect);

    // 2. Propagate new boundary to all proxies
    // This ensures that dragging limits and snapping grids update immediately
    updateProxiesCanvasRect(newRect);
}

void SceneGraphicsAdapter::updateProxiesCanvasRect(QRectF const& rect)
{
    if (!m_uiScene)
        return;

    QList<QGraphicsItem*> items = m_uiScene->items();
    for (auto* uiItem : items)
    {
        if (auto* proxy = dynamic_cast<VisualItemProxy*>(uiItem))
        {
            proxy->setCanvasRect(rect);
        }
    }
}

void SceneGraphicsAdapter::onProxyRequestDelete(std::shared_ptr<SceneItem> item)
{
    // The View requested a delete (e.g. user clicked 'X' on toolbar).
    // We forward this intent to the Model.
    // The Model will remove the item, triggering 'itemRemoved', which leads back to 'onModelItemRemoved',
    // completing the cycle.
    if (m_model)
    {
        m_model->removeSource(item);
    }
}
