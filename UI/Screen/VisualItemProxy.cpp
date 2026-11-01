#include "VisualItemProxy.h"
#include "AppConfig.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsScene>
#include <QCursor>
#include <cmath>
#include <QDebug>
#include "ThemeManager.h"
#include "Scene.h"
#include "SceneItem.h"

// --- VisualItemToolbar Implementation ---
VisualItemToolbar::VisualItemToolbar(VisualItemProxy* parentProxy)
    : QGraphicsObject(parentProxy), m_parentProxy(parentProxy)
{
    setFlags(QGraphicsItem::ItemIgnoresTransformations);
    setAcceptHoverEvents(true);
    setZValue(9999);
    m_totalWidth = m_btnWidth * m_btnCount;
}

QRectF VisualItemToolbar::boundingRect() const
{
    if (m_parentProxy && m_parentProxy->isLiveMode())
    {
        return QRectF(-40, 0, 80, m_height);  // Wider single button
    }
    return QRectF(-m_totalWidth / 2, 0, m_totalWidth, m_height);
}

void VisualItemToolbar::paint(QPainter* painter, QStyleOptionGraphicsItem const*, QWidget*)
{
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(40, 44, 52, 240));
    painter->setRenderHint(QPainter::Antialiasing);
    QRectF r = boundingRect();
    painter->drawRoundedRect(r, 6, 6);
    bool isLive = m_parentProxy->isLiveMode();

    if (isLive)
    {
        // --- Live Mode: Only 1 Button (Switch Source) ---
        QRectF btnRect(
            r.left(), r.top(), r.width(), m_height);  // Full width or single button width

        if (m_isHovered)
        {  // Simple hover effect
            painter->setBrush(QColor(255, 255, 255, 40));
            painter->drawRoundedRect(btnRect.adjusted(2, 2, -2, -2), 4, 4);
        }

        painter->setPen(Qt::white);
        // Draw "Switch" icon (using text for simplicity, or load an icon)
        painter->drawText(btnRect, Qt::AlignCenter, "⇄ 切换源");
    }
    else
    {
        // [Mod] 更新图标列表，加入层级图标
        // 顺序: 左旋, 右旋, 水平居中, 垂直居中, 上移, 下移, 删除
        QString const icons[] = {"↺", "↻", "⇹", "⇕", "▲", "▼", "✕"};

        QFont f = painter->font();
        f.setPixelSize(16);  //稍微调小一点以适应更多图标
        f.setBold(true);
        painter->setFont(f);

        for (int i = 0; i < m_btnCount; ++i)
        {
            QRectF btnRect(r.left() + i * m_btnWidth, r.top(), m_btnWidth, m_height);

            // 绘制 hover 背景
            if (m_hoveredBtnIndex == i)
            {
                // 删除按钮(最后一个)显示红色，其他显示半透明白
                QColor hoverCol =
                    (i == m_btnCount - 1) ? QColor(231, 76, 60, 255) : QColor(255, 255, 255, 40);
                painter->setBrush(hoverCol);
                painter->drawRoundedRect(btnRect.adjusted(2, 2, -2, -2), 4, 4);
            }

            // 绘制图标颜色
            painter->setPen((i == m_btnCount - 1 && m_hoveredBtnIndex == i)
                                ? Qt::white
                                : QColor(220, 223, 230));

            // 简单绘制文本图标
            painter->drawText(btnRect, Qt::AlignCenter, icons[i]);

            // 绘制分割线
            if (i < m_btnCount - 1)
            {
                painter->setPen(QColor(255, 255, 255, 30));
                painter->drawLine(
                    btnRect.right(), btnRect.top() + 8, btnRect.right(), btnRect.bottom() - 8);
            }
        }
    }
}

void VisualItemToolbar::hoverEnterEvent(QGraphicsSceneHoverEvent* e)
{
    m_isHovered = true;
    update();
    QGraphicsObject::hoverEnterEvent(e);
}
void VisualItemToolbar::hoverLeaveEvent(QGraphicsSceneHoverEvent* e)
{
    m_isHovered = false;
    m_hoveredBtnIndex = -1;
    update();
    QGraphicsObject::hoverLeaveEvent(e);
}
void VisualItemToolbar::hoverMoveEvent(QGraphicsSceneHoverEvent* e)
{
    qreal localX = e->pos().x() + m_totalWidth / 2;
    int idx = (int)(localX / m_btnWidth);
    if (idx < 0)
        idx = 0;
    if (idx >= m_btnCount)
        idx = m_btnCount - 1;
    if (m_hoveredBtnIndex != idx)
    {
        m_hoveredBtnIndex = idx;
        update();
    }
    QGraphicsObject::hoverMoveEvent(e);
}
void VisualItemToolbar::mousePressEvent(QGraphicsSceneMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && m_parentProxy)
    {
        e->accept();
        if (m_parentProxy->isLiveMode())
        {
            // [New] Live Mode Click
            QTimer::singleShot(0, m_parentProxy, [this]() {
                emit m_parentProxy->switchSourceRequested(m_parentProxy->getCoreItem());
            });
        }
        else
        {
            switch (m_hoveredBtnIndex)
            {
                case 0:
                    QTimer::singleShot(0, m_parentProxy, &VisualItemProxy::rotateLeft);
                    break;
                case 1:
                    QTimer::singleShot(0, m_parentProxy, &VisualItemProxy::rotateRight);
                    break;
                case 2:
                    QTimer::singleShot(0, m_parentProxy, &VisualItemProxy::centerHorizontally);
                    break;
                case 3:
                    QTimer::singleShot(0, m_parentProxy, &VisualItemProxy::centerVertically);
                    break;
                case 4:
                    QTimer::singleShot(0, m_parentProxy, &VisualItemProxy::layerRaise);
                    break;  // [New] 上移
                case 5:
                    QTimer::singleShot(0, m_parentProxy, &VisualItemProxy::layerLower);
                    break;  // [New] 下移
                case 6:
                    QTimer::singleShot(0, m_parentProxy, &VisualItemProxy::requestDelete);
                    break;
            }
        }
    }
}

// --- VisualItemProxy Implementation ---

VisualItemProxy::VisualItemProxy(std::shared_ptr<SceneItem> coreItem)
    : m_coreItem(coreItem), m_isPendingDelete(false)
{
    m_canvasRect = QRectF(0, 0, 1920, 1080);

    m_snapX.active = false;
    m_snapX.pos = 0.0f;
    m_snapY.active = false;
    m_snapY.pos = 0.0f;

    m_isDragging = false;
    m_isResizing = false;
    m_resizeDir = GeometryHelper::None;

    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemSendsGeometryChanges |
             QGraphicsItem::ItemUsesExtendedStyleOption);
    setAcceptHoverEvents(true);

    m_toolbar = new VisualItemToolbar(this);
    m_toolbar->setVisible(false);

    connect(
        m_coreItem.get(), &SceneItem::transformChanged, this, &VisualItemProxy::onModelDataChanged);
    connect(ThemeManager::inst(),
            &ThemeManager::themeChanged,
            this,
            &VisualItemProxy::updateStyleCache);

    if (m_coreItem)
    {
        auto scenePtr = m_coreItem->getSceneContext();
        if (scenePtr)
        {
            connect(scenePtr, &Scene::contentChanged, this, &VisualItemProxy::onModelDataChanged);
        }
        else
        {
            qCritical()
                << "[VisualItemProxy] CRITICAL: SceneContext is NULL! Layer sync will FAIL.";
        }
    }

    updateStyleCache();
    onModelDataChanged();
}

VisualItemProxy::~VisualItemProxy()
{
    if (scene())
    {
        scene()->removeItem(this);
    }
    if (m_coreItem)
        disconnect(m_coreItem.get(), nullptr, this, nullptr);
}

// ... (getCoreItem, rotate, center 等辅助函数保持不变) ...
std::shared_ptr<SceneItem> VisualItemProxy::getCoreItem() const
{
    return m_coreItem;
}
void VisualItemProxy::requestDelete()
{
    // [新增] 防重入保护
    if (m_isPendingDelete)
        return;
    if (!m_coreItem)
        return;

    m_isPendingDelete = true;

    // 隐藏自己，给用户“已删除”的即时反馈
    this->setVisible(false);

    emit deleteRequested(m_coreItem);
}
void VisualItemProxy::rotateLeft()
{
    if (m_coreItem)
        m_coreItem->setRotation(m_coreItem->getTransform().rotation - 90.0f);
}
void VisualItemProxy::rotateRight()
{
    if (m_coreItem)
        m_coreItem->setRotation(m_coreItem->getTransform().rotation + 90.0f);
}
void VisualItemProxy::centerHorizontally()
{
    if (!m_coreItem)
        return;
    float targetX = (m_canvasRect.width() - rect().width()) / 2.0f;
    m_coreItem->setPosition(targetX, m_coreItem->getTransform().y);
}
void VisualItemProxy::centerVertically()
{
    if (!m_coreItem)
        return;
    float targetY = (m_canvasRect.height() - rect().height()) / 2.0f;
    m_coreItem->setPosition(m_coreItem->getTransform().x, targetY);
}
void VisualItemProxy::setCanvasRect(QRectF const& rect)
{
    m_canvasRect = rect;
}
void VisualItemProxy::triggerAutoSelection()
{
    if (this->scene())
    {
        this->scene()->clearSelection();
        this->setSelected(true);
    }
}
void VisualItemProxy::layerRaise()
{
    if (m_coreItem)
    {
        if (auto s = m_coreItem->getSceneContext())
            s->bringItemForward(m_coreItem);
    }
}
void VisualItemProxy::layerLower()
{
    if (m_coreItem)
    {
        if (auto s = m_coreItem->getSceneContext())
            s->sendItemBackward(m_coreItem);
    }
}

void VisualItemProxy::updateStyleCache()
{
    auto tm = ThemeManager::inst();
    QColor brandColor = tm->getColor("theme.primary_color", QColor(64, 158, 255));

    m_styleCache.borderColor = brandColor;
    m_styleCache.hoveringColor = QColor(144, 147, 153);

    // [Fix] 这里的颜色设置其实主要用于 handle，Proxy 本体的刷子在 paint 中硬编码了
    m_styleCache.normalColor = Qt::transparent;
    m_styleCache.backgroundColor = Qt::transparent;

    m_styleCache.textColor = QColor(255, 255, 255, 220);
    m_styleCache.textFont = QFont("Microsoft YaHei UI", 22, QFont::Bold);

    m_styleCache.handleFill = Qt::white;
    m_styleCache.handleBorder = brandColor;
    m_styleCache.snapLineColor = QColor(255, 0, 128);
    m_styleCache.handleSize = 10;
    m_styleCache.borderWidth = 2.0;

    m_styleCache.snapLineWidthNormal = 1.0;
    m_styleCache.snapLineWidthStrong = 2.0;
    m_styleCache.snapThresholdNormal = AppSettings::global().snapThreshold;
    m_styleCache.snapThresholdStrong = 10;

    updateContentCache();
    update();
}

void VisualItemProxy::onModelDataChanged()
{
    if (!m_coreItem)
        return;
    if (m_isDragging || m_isResizing)
        return;

    Transform t = m_coreItem->getTransform();
    prepareGeometryChange();

    bool sizeChanged = (rect().width() != t.width || rect().height() != t.height);

    if (sizeChanged)
    {
        setRect(0, 0, t.width, t.height);
        setTransformOriginPoint(t.width / 2.0, t.height / 2.0);
    }

    setPos(t.x, t.y);
    setRotation(t.rotation);
    setZValue(t.zOrder);

    if (sizeChanged)
    {
        updateContentCache();
    }

    updateToolbarState();
    update();
}

void VisualItemProxy::updateToolbarState()
{
    if (!m_toolbar)
        return;
    bool shouldShow = isSelected() && !m_isDragging && !m_isResizing;
    if (m_toolbar->isVisible() != shouldShow)
    {
        m_toolbar->setVisible(shouldShow);
    }
    if (shouldShow)
    {
        updateToolbarPos();
    }
}

void VisualItemProxy::updateToolbarPos()
{
    if (m_toolbar)
        m_toolbar->setPos(rect().width() / 2.0, rect().height() + 10.0);
}

QRectF VisualItemProxy::boundingRect() const
{
    QRectF r = rect().adjusted(-20, -20, 20, 20);
    if (m_snapX.active || m_snapY.active)
    {
        QRectF canvasLocal = mapRectFromScene(m_canvasRect);
        return r.united(canvasLocal);
    }
    return r;
}

// [Fix] 新增 shape() 函数
// 强制点击区域为 Item 的矩形范围。即便 paint 中没有刷子，QGraphicsScene 也会根据此区域判断点击。
QPainterPath VisualItemProxy::shape() const
{
    QPainterPath path;

    // [Fix] 修复旋转后无法选中的 Bug
    // QGraphicsItem::shape() 返回的是【局部坐标系】下的形状。
    // 无论 Item 如何旋转、缩放，其局部坐标系下的形状（矩形）是永远不变的。
    // Qt 框架会自动根据 setRotation() 的变换矩阵来处理场景坐标到局部坐标的映射。
    // 之前在这里手动 rotate() 导致了“二次旋转”，使得点击区域与视觉区域分离。

    QRectF r = rect();

    // [优化] 稍微向外扩大一点范围，确保边缘的缩放手柄(Handle)容易被点中
    // 假设手柄大小约为 10px，向外扩 5px 可以包含手柄的一半
    qreal handlePadding = 6.0;
    path.addRect(r.adjusted(-handlePadding, -handlePadding, handlePadding, handlePadding));

    return path;
}

void VisualItemProxy::setLiveMode(bool isLive)
{
    if (m_isLiveMode == isLive)
        return;
    m_isLiveMode = isLive;

    // Hide handles and update toolbar immediately
    update();
    updateToolbarState();
}

QString VisualItemProxy::getSourceIcon(SourceType type) const
{
    return "📦";
}

void VisualItemProxy::updateContentCache()
{
    m_contentCache = QPixmap();
}

void VisualItemProxy::paint(QPainter* painter, QStyleOptionGraphicsItem const* option, QWidget*)
{
    bool isInteracting = (m_isDragging || m_isResizing);

    // [CRITICAL FIX] 解决“看得见但点不中”的问题
    // 原代码使用了 Qt::NoBrush，这在某些情况下会导致点击事件直接穿透 Item。
    // 我们改为使用 Alpha=1 的白色 (几乎完全透明，肉眼不可见，但逻辑存在)。
    // 这样既不会产生“黑块遮挡视频”，又能确保鼠标点击被此 Item 捕获。
    painter->setBrush(QColor(255, 255, 255, 1));

    // 1. 绘制边框
    QPen borderPen;
    borderPen.setCosmetic(true);

    if (isSelected())
    {
        borderPen.setColor(m_styleCache.borderColor);
        borderPen.setWidthF(m_styleCache.borderWidth);
        borderPen.setStyle(Qt::SolidLine);
    }
    else
    {
        if (option->state & QStyle::State_MouseOver)
        {
            borderPen.setColor(m_styleCache.hoveringColor);
            borderPen.setWidthF(1.0);
            borderPen.setStyle(Qt::DashLine);
        }
        else
        {
            borderPen.setStyle(Qt::NoPen);
        }
    }

    painter->setPen(borderPen);
    painter->drawRect(rect());

    // 2. 绘制手柄 (仅选中时)
    if (isSelected() && !isInteracting && !m_isLiveMode)
    {
        painter->setBrush(m_styleCache.handleFill);
        painter->setPen(QPen(m_styleCache.handleBorder, 1.5));
        painter->setRenderHint(QPainter::Antialiasing, true);

        qreal lod = option->levelOfDetailFromTransform(QTransform());
        qreal hSize = (double)m_styleCache.handleSize * ((lod > 0.0001) ? (1.0 / lod) : 1.0);
        qreal w = rect().width();
        qreal h = rect().height();

        auto drawHandle = [&](qreal x, qreal y) {
            painter->drawEllipse(QPointF(x, y), hSize / 2.0, hSize / 2.0);
        };

        // 8个控制点
        drawHandle(0, 0);
        drawHandle(w, 0);
        drawHandle(0, h);
        drawHandle(w, h);
        if (w > hSize * 3)
        {
            drawHandle(w / 2, 0);
            drawHandle(w / 2, h);
        }
        if (h > hSize * 3)
        {
            drawHandle(0, h / 2);
            drawHandle(w, h / 2);
        }

        painter->setRenderHint(QPainter::Antialiasing, false);
    }

    // 3. 绘制辅助线 (仅交互时)
    if (isInteracting)
        drawSmartGuides(painter);
}

// ... (itemChange, updateCursor, events 等其余代码保持不变) ...

QVariant VisualItemProxy::itemChange(GraphicsItemChange change, QVariant const& value)
{
    if (change == ItemSelectedHasChanged)
    {
        bool sel = value.toBool();
        updateToolbarState();
        if (m_coreItem)
        {
            // 选中时置顶，便于操作
            setZValue(m_coreItem->getTransform().zOrder);
        }
    }
    return QGraphicsRectItem::itemChange(change, value);
}

void VisualItemProxy::updateCursor(QPointF const& pos)
{
    if (m_isLiveMode)
    {
        setCursor(Qt::ArrowCursor);  // Or PointingHandCursor
        return;
    }
    if (!isSelected() || m_isResizing)
        return;
    GeometryHelper::ResizeHandle h = GeometryHelper::getHandleAt(rect(), pos);
    setCursor(GeometryHelper::getCursorForHandle(h));
    if (h == GeometryHelper::None)
        setCursor(Qt::SizeAllCursor);
}

void VisualItemProxy::hoverMoveEvent(QGraphicsSceneHoverEvent* e)
{
    updateCursor(e->pos());
    QGraphicsRectItem::hoverMoveEvent(e);
}

void VisualItemProxy::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    // [New] Disable Resize/Drag in Live Mode
    if (m_coreItem)
    {
        Transform t = m_coreItem->getTransform();
        QPointF scenePos = event->scenePos();
        QPointF itemPos = event->pos();  // 相对 Item 自身的坐标

        qDebug() << QString(
                        "[InteractionLog] ItemUUID: %1 | ClickScenePos: (%2, %3) | CoreTransform: "
                        "X=%4, Y=%5, W=%6, H=%7")
                        .arg(m_coreItem->getItemUuid())
                        .arg(scenePos.x())
                        .arg(scenePos.y())
                        .arg(t.x)
                        .arg(t.y)
                        .arg(t.width)
                        .arg(t.height);

        // 检查点击是否在逻辑矩形内
        if (!rect().contains(itemPos))
        {
            qWarning()
                << "[InteractionLog] Click detected OUTSIDE of logic rect! Possible offset issue.";
        }
    }
    if (m_isLiveMode)
    {
        if (event->button() == Qt::LeftButton)
        {
            // Only allow Selection, NO resizing/dragging
            setSelected(true);
            updateToolbarState();
            event->accept();
            return;
        }
    }

    if (event->button() == Qt::LeftButton)
    {
        m_resizeDir = GeometryHelper::getHandleAt(
            rect(), event->pos());  //分析鼠标是点击的区域位于窗口边角或者是窗口内部
        m_lastMousePos = event->scenePos();
        m_hasChanged = false;

        if (m_resizeDir != GeometryHelper::None)
        {
            m_isResizing = true;
            m_isDragging = false;

            QRectF r = rect();
            if (m_resizeDir == GeometryHelper::TopLeft)
                m_resizeAnchorScenePos = mapToScene(r.bottomRight());
            else if (m_resizeDir == GeometryHelper::BottomRight)
                m_resizeAnchorScenePos = mapToScene(r.topLeft());
            else if (m_resizeDir == GeometryHelper::TopRight)
                m_resizeAnchorScenePos = mapToScene(r.bottomLeft());
            else if (m_resizeDir == GeometryHelper::BottomLeft)
                m_resizeAnchorScenePos = mapToScene(r.topRight());
            else if (m_resizeDir == GeometryHelper::Top)
                m_resizeAnchorScenePos = mapToScene(r.bottomLeft());
            else if (m_resizeDir == GeometryHelper::Bottom)
                m_resizeAnchorScenePos = mapToScene(r.topLeft());
            else if (m_resizeDir == GeometryHelper::Left)
                m_resizeAnchorScenePos = mapToScene(r.topRight());
            else if (m_resizeDir == GeometryHelper::Right)
                m_resizeAnchorScenePos = mapToScene(r.topLeft());
            else
                m_resizeAnchorScenePos = mapToScene(r.center());
        }
        else
        {
            m_isResizing = false;
            m_isDragging = true;
        }

        setSelected(true);
        updateToolbarState();
        update();
        event->accept();
    }
    else
    {
        QGraphicsRectItem::mousePressEvent(event);
    }
}

void VisualItemProxy::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_isLiveMode)
        return;
    if (!isSelected() || (!m_isDragging && !m_isResizing))
        return;

    QPointF curScenePos = event->scenePos();
    if ((curScenePos - m_lastMousePos).manhattanLength() < 0.5)
        return;

    m_hasChanged = true;

    if (m_isResizing)
    {
        qreal newW = rect().width();
        qreal newH = rect().height();
        QPointF newPos = pos();

        GeometryHelper::calculateResize(
            m_resizeDir, curScenePos, m_resizeAnchorScenePos, newW, newH, newPos);

        if (m_resizeDir == GeometryHelper::Right || m_resizeDir == GeometryHelper::Left)
            newH = rect().height();
        if (m_resizeDir == GeometryHelper::Top || m_resizeDir == GeometryHelper::Bottom)
            newW = rect().width();

        newW = std::max(10.0, newW);
        newH = std::max(10.0, newH);

        prepareGeometryChange();
        setRect(0, 0, newW, newH);
        setTransformOriginPoint(newW / 2.0, newH / 2.0);
        setPos(newPos);
        updateContentCache();
    }
    else if (m_isDragging)
    {
        QPointF delta = curScenePos - m_lastMousePos;
        QPointF newPos = pos() + delta;

        processSnapping(newPos, rect());
        setPos(newPos);
    }

    m_lastMousePos = curScenePos;
    update();
}

void VisualItemProxy::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_hasChanged)
    {
        commitGeometryToModel();
        emit interactionFinished();
        m_hasChanged = false;
    }

    m_isResizing = false;
    m_isDragging = false;
    m_resizeDir = GeometryHelper::None;

    if (m_snapX.active || m_snapY.active)
    {
        prepareGeometryChange();
    }
    m_snapX.active = false;
    m_snapY.active = false;

    updateToolbarState();
    updateCursor(event->pos());
    update();
    QGraphicsRectItem::mouseReleaseEvent(event);
}

void VisualItemProxy::processSnapping(QPointF& newPos, QRectF const& myRect)
{
    bool wasSnapX = m_snapX.active;
    bool wasSnapY = m_snapY.active;

    GeometryHelper::calculateSnap(
        myRect, m_canvasRect, newPos, m_snapX, m_snapY, m_styleCache.snapThresholdNormal);

    // ... (Snapping logic continues as is) ...
    if (!m_snapX.active)
    {
        float myLeft = newPos.x();
        float myRight = newPos.x() + myRect.width();
        float myCenterX = newPos.x() + myRect.width() / 2.0f;
        float canvasLeft = m_canvasRect.left();
        float canvasRight = m_canvasRect.right();
        float canvasCenterX = m_canvasRect.center().x();
        float threshold = m_styleCache.snapThresholdStrong;

        if (std::abs(myLeft - canvasLeft) < threshold)
        {
            newPos.setX(canvasLeft);
            m_snapX.active = true;
            m_snapX.pos = canvasLeft;
        }
        else if (std::abs(myRight - canvasRight) < threshold)
        {
            newPos.setX(canvasRight - myRect.width());
            m_snapX.active = true;
            m_snapX.pos = canvasRight;
        }
        else if (std::abs(myCenterX - canvasCenterX) < threshold)
        {
            newPos.setX(canvasCenterX - myRect.width() / 2.0f);
            m_snapX.active = true;
            m_snapX.pos = canvasCenterX;
        }
    }

    if (!m_snapY.active)
    {
        float myTop = newPos.y();
        float myBottom = newPos.y() + myRect.height();
        float myCenterY = newPos.y() + myRect.height() / 2.0f;
        float canvasTop = m_canvasRect.top();
        float canvasBottom = m_canvasRect.bottom();
        float canvasCenterY = m_canvasRect.center().y();
        float threshold = m_styleCache.snapThresholdStrong;

        if (std::abs(myTop - canvasTop) < threshold)
        {
            newPos.setY(canvasTop);
            m_snapY.active = true;
            m_snapY.pos = canvasTop;
        }
        else if (std::abs(myBottom - canvasBottom) < threshold)
        {
            newPos.setY(canvasBottom - myRect.height());
            m_snapY.active = true;
            m_snapY.pos = canvasBottom;
        }
        else if (std::abs(myCenterY - canvasCenterY) < threshold)
        {
            newPos.setY(canvasCenterY - myRect.height() / 2.0f);
            m_snapY.active = true;
            m_snapY.pos = canvasCenterY;
        }
    }

    if (wasSnapX != m_snapX.active || wasSnapY != m_snapY.active)
    {
        prepareGeometryChange();
    }
}

void VisualItemProxy::drawSmartGuides(QPainter* painter)
{
    QPen pen;
    pen.setColor(m_styleCache.snapLineColor);
    pen.setCosmetic(true);

    QRectF localCanvas = mapRectFromScene(m_canvasRect);
    float const EPSILON = 1.0f;

    if (m_snapX.active)
    {
        bool isStrong = (std::abs(m_snapX.pos - m_canvasRect.left()) < EPSILON) ||
            (std::abs(m_snapX.pos - m_canvasRect.right()) < EPSILON) ||
            (std::abs(m_snapX.pos - m_canvasRect.center().x()) < EPSILON);

        if (isStrong)
        {
            pen.setStyle(Qt::SolidLine);
            pen.setWidthF(m_styleCache.snapLineWidthStrong);
        }
        else
        {
            pen.setStyle(Qt::DashLine);
            pen.setWidthF(m_styleCache.snapLineWidthNormal);
        }

        painter->setPen(pen);
        QPointF localPt = mapFromScene(QPointF(m_snapX.pos, 0));
        painter->drawLine(QPointF(localPt.x(), localCanvas.top()),
                          QPointF(localPt.x(), localCanvas.bottom()));
    }

    if (m_snapY.active)
    {
        bool isStrong = (std::abs(m_snapY.pos - m_canvasRect.top()) < EPSILON) ||
            (std::abs(m_snapY.pos - m_canvasRect.bottom()) < EPSILON) ||
            (std::abs(m_snapY.pos - m_canvasRect.center().y()) < EPSILON);

        if (isStrong)
        {
            pen.setStyle(Qt::SolidLine);
            pen.setWidthF(m_styleCache.snapLineWidthStrong);
        }
        else
        {
            pen.setStyle(Qt::DashLine);
            pen.setWidthF(m_styleCache.snapLineWidthNormal);
        }

        painter->setPen(pen);
        QPointF localPt = mapFromScene(QPointF(0, m_snapY.pos));
        painter->drawLine(QPointF(localCanvas.left(), localPt.y()),
                          QPointF(localCanvas.right(), localPt.y()));
    }
}

void VisualItemProxy::commitGeometryToModel()
{
    if (!m_coreItem)
        return;
    m_coreItem->setSize(rect().width(), rect().height());
    m_coreItem->setPosition(pos().x(), pos().y());
}
