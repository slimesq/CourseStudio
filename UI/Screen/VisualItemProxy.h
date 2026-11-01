#pragma once
#include <QGraphicsRectItem>
#include <QGraphicsObject>
#include <QObject>
#include <QBrush>
#include <QPen>
#include <memory>
#include <QTimer>
#include <QPainter>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QPointer>
#include <QPixmap>     // [New]
#include "AppUtils.h"  // 引入工具类

class SceneItem;
class VisualItemProxy;

// --- 样式缓存结构体 ---
struct VisualItemStyle
{
    QColor borderColor;
    QColor hoveringColor;
    QColor normalColor;
    QColor backgroundColor;
    QColor handleFill;
    QColor handleBorder;
    QColor snapLineColor;

    // 文本样式
    QColor textColor;
    QFont textFont;

    int handleSize;
    qreal borderWidth;

    // 辅助线宽度分级
    qreal snapLineWidthNormal;
    qreal snapLineWidthStrong;

    // 吸附阈值分级
    int snapThresholdNormal;
    int snapThresholdStrong;

    // 工具栏样式
    QColor toolbarBgColor;
    QColor toolbarBtnHoverColor;
    QColor toolbarIconColor;
};

// --- 工具栏 (UI Upgrade) ---
class VisualItemToolbar : public QGraphicsObject
{
    Q_OBJECT
public:
    explicit VisualItemToolbar(VisualItemProxy* parentProxy);
    QRectF boundingRect() const override;
    void paint(QPainter* painter, QStyleOptionGraphicsItem const* option, QWidget* widget) override;

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

private:
    VisualItemProxy* m_parentProxy;
    bool m_isHovered = false;
    int m_hoveredBtnIndex = -1;

    qreal const m_btnWidth = 36.0;
    qreal const m_height = 36.0;
    int const m_btnCount = 7;
    qreal m_totalWidth;
};

// --- 主视图代理 ---
class VisualItemProxy : public QObject, public QGraphicsRectItem
{
    Q_OBJECT
public:
    explicit VisualItemProxy(std::shared_ptr<SceneItem> coreItem);
    ~VisualItemProxy() override;

    std::shared_ptr<SceneItem> getCoreItem() const;

    // Actions
    void requestDelete();
    void rotateLeft();
    void rotateRight();
    void centerHorizontally();
    void centerVertically();

    // Slots
    void layerRaise();
    void layerLower();

    void setCanvasRect(QRectF const& rect);

    bool getSnapX(float& xVal) const
    {
        return m_snapX.active;
    }
    float getSnapXValue() const
    {
        return m_snapX.pos;
    }
    bool getSnapY(float& yVal) const
    {
        return m_snapY.active;
    }
    float getSnapYValue() const
    {
        return m_snapY.pos;
    }

    // [Fix] 重写 shape() 以确保点击判定覆盖整个矩形
    QPainterPath shape() const override;

    // [New] Set Live Mode state
    void setLiveMode(bool isLive);
    bool isLiveMode() const
    {
        return m_isLiveMode;
    }

signals:
    void deleteRequested(std::shared_ptr<SceneItem> item);
    void interactionFinished();
    void switchSourceRequested(std::shared_ptr<SceneItem> item);

protected:
    QVariant itemChange(GraphicsItemChange change, QVariant const& value) override;
    void paint(QPainter* painter, QStyleOptionGraphicsItem const* option, QWidget* widget) override;
    QRectF boundingRect() const override;

    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private slots:
    void onModelDataChanged();
    void updateStyleCache();
    void triggerAutoSelection();

private:
    void drawSmartGuides(QPainter* painter);
    void updateCursor(QPointF const& pos);
    void updateToolbarPos();
    void updateToolbarState();
    void processSnapping(QPointF& newPos, QRectF const& myRect);
    void commitGeometryToModel();
    QString getSourceIcon(SourceType type) const;

    void updateContentCache();

private:
    std::shared_ptr<SceneItem> m_coreItem;
    QRectF m_canvasRect;
    VisualItemToolbar* m_toolbar = nullptr;

    bool m_isDragging = false;
    bool m_isResizing = false;
    bool m_hasChanged = false;

    GeometryHelper::ResizeHandle m_resizeDir = GeometryHelper::None;

    QPointF m_lastMousePos;
    QPointF m_resizeAnchorScenePos;

    GeometryHelper::SnapResult m_snapX;
    GeometryHelper::SnapResult m_snapY;

    bool m_isPendingDelete = false;  // [新增] 防抖标志位

    VisualItemStyle m_styleCache;

    QPixmap m_contentCache;
    bool m_isLiveMode = false;  // [New] Flag
};
