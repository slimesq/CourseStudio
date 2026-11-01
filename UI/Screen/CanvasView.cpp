#include "CanvasView.h"
#include <QScrollBar>
#include <QResizeEvent>
#include <QDebug>
#include <cmath>
#include <QDateTime>
#include <QDir>
#include <QPainterPath>
#include "ThemeManager.h"
#include "Logger.h"

struct PaintLocker
{
    bool& flag;
    PaintLocker(bool& f) : flag(f)
    {
        flag = true;
    }
    ~PaintLocker()
    {
        flag = false;
    }
};

class PainterStateSaver
{
    QPainter* p;

public:
    PainterStateSaver(QPainter* painter) : p(painter)
    {
        if (p)
            p->save();
    }
    ~PainterStateSaver()
    {
        if (p)
            p->restore();
    }
};

CanvasView::CanvasView(QWidget* parent) : QGraphicsView(parent)
{
    m_uiScene = new QGraphicsScene(this);
    setScene(m_uiScene);

    // [Optimization] 视口更新模式：全视口更新适合视频/动画背景，避免局部更新留下的残影
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    // [Optimization] 关闭缓存，因为背景是动态视频，缓存无意义且消耗内存
    setCacheMode(QGraphicsView::CacheNone);
    // [Optimization] 不保存 Painter 状态，稍微提升性能
    setOptimizationFlag(QGraphicsView::DontSavePainterState);

    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setDragMode(QGraphicsView::NoDrag);

    QColor viewportColor = QColor("#181818");
    setBackgroundBrush(viewportColor);
    setStyleSheet(
        QString("QGraphicsView { border: none; background: %1; }").arg(viewportColor.name()));

    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);

    connect(m_uiScene, &QGraphicsScene::sceneRectChanged, this, [this](QRectF const&) {
        performFitInView();
        m_lastGridRect = QRectF();  // 场景大小改变，缓存失效
    });

    connect(ThemeManager::inst(), &ThemeManager::themeChanged, this, [this]() {
        this->viewport()->update();
    });
}

CanvasView::~CanvasView()
{
    m_adapter.reset();
    if (m_uiScene)
        m_uiScene->clear();
}

void CanvasView::setLiveMode(bool isLive)
{
    m_isLiveMode = isLive;
    if (m_adapter)
        m_adapter->setLiveMode(isLive);
}

void CanvasView::rebuildVisualItems()
{
}

void CanvasView::setSceneModel(std::shared_ptr<Scene> scene)
{
    qDebug() << "[CanvasView] Scene Model Changed. New Generation:" << m_currentGeneration;

    // 1. Reset Adapter & Scene Items
    m_adapter.reset();
    m_uiScene->clear();
    m_lastGridRect = QRectF();

    // 2. [Debug] Log current BG state
    qDebug() << "[CanvasView] Clearing BG Image. Was Null?" << m_bgImage.isNull();

    // 3. Force Clear Background
    m_bgImage = QImage();

    // 4. [Fix] 立即重绘，不等待事件循环
    // 这一步非常关键，它强迫 View 在此时此刻就用 drawBackground 画一次（此时 m_bgImage 为空，应该画黑/网格）
    // 从而冲刷掉显存中的旧内容
    this->viewport()->repaint();

    resetTransform();

    if (horizontalScrollBar())
        horizontalScrollBar()->setValue(0);
    if (verticalScrollBar())
        verticalScrollBar()->setValue(0);

    if (scene)
    {
        qDebug() << "[CanvasView] Setting new scene rect:" << scene->getCanvasRect();
        m_uiScene->setSceneRect(scene->getCanvasRect());
        m_adapter = std::make_unique<SceneGraphicsAdapter>(scene, m_uiScene);
        m_adapter->setLiveMode(m_isLiveMode);
        connect(m_adapter.get(),
                &SceneGraphicsAdapter::requestSourceSwitch,
                this,
                &CanvasView::sourceSwitchRequested);
    }

    m_uiScene->update();
    performFitInView();

    // 再次重绘以适应新尺寸
    this->viewport()->repaint();
}

void CanvasView::updateBackgroundImage(QImage const& img, int generation)
{
    if (!scene())
    {
        // 如果 Scene 还没设置，或者已经被销毁，直接忽略这一帧
        // 这种情况在初始化阶段或程序关闭时很常见
        return;
    }

    // [Fix] 核心防残影逻辑：如果收到的帧属于旧代，直接丢弃
    if (generation != m_currentGeneration)
    {
        // qDebug() << "[CanvasView] Dropped stale frame. Frame Gen:" << generation << " Current Gen:" << m_currentGeneration;
        return;
    }

    if (img.isNull())
    {
        if (!m_bgImage.isNull())
        {
            qDebug() << "[CanvasView] Received NULL frame. Clearing BG.";
            m_bgImage = QImage();
            this->viewport()->repaint();  // 使用 repaint 替代 update
        }
        return;
    }

    // [Debug] Optional: Log frame arrival (comment out if too spammy)
    // qDebug() << "[CanvasView] Frame received. Size:" << img.size();

    // 1. 如果正在绘制，或者还没轮到处理，就把新帧存起来 (覆盖旧的)
    // 这实现了“自动丢帧”，防止队列积压
    m_pendingFrame = img;
    m_hasPendingFrame = true;
    if (!m_isPainting)
    {
        // 使用 update 而不是 repaint (repaint 是同步的，极易死锁)
        this->viewport()->update();
    }
}

void CanvasView::triggerRefresh()
{
    if (scene())
        scene()->update();
}

void CanvasView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    performFitInView();
}

void CanvasView::performFitInView()
{
    if (!m_uiScene || m_uiScene->sceneRect().isEmpty())
        return;
    fitInView(m_uiScene->sceneRect(), Qt::KeepAspectRatio);
    scale(0.95, 0.95);
    centerOn(m_uiScene->sceneRect().center());
}

void CanvasView::updateGridCache(QRectF const& rect)
{
    QSize targetSize = rect.size().toSize();
    qreal dpr = viewport()->devicePixelRatioF();
    QSize pixelSize = (rect.size() * dpr).toSize();

    if (!m_gridCache.isNull() && m_gridCache.size() == pixelSize && m_lastGridRect == rect)
    {
        return;
    }

    m_gridCache = QPixmap(pixelSize);
    m_gridCache.setDevicePixelRatio(dpr);
    m_gridCache.fill(Qt::transparent);

    QPainter p(&m_gridCache);
    p.setRenderHint(QPainter::Antialiasing, true);

    int const gridSize = 40;
    QColor dotColor(255, 255, 255, 30);
    p.setPen(Qt::NoPen);
    p.setBrush(dotColor);

    for (int x = 0; x <= rect.width(); x += gridSize)
    {
        for (int y = 0; y <= rect.height(); y += gridSize)
        {
            p.drawEllipse(QPointF(x, y), 1.5, 1.5);
        }
    }

    QPen centerPen(QColor(255, 255, 255, 50));
    centerPen.setWidth(1);
    centerPen.setStyle(Qt::DashLine);
    p.setPen(centerPen);
    QPointF center(rect.width() / 2.0, rect.height() / 2.0);
    float crossSize = 100.0f;
    p.drawLine(QPointF(center.x(), center.y() - crossSize),
               QPointF(center.x(), center.y() + crossSize));
    p.drawLine(QPointF(center.x() - crossSize, center.y()),
               QPointF(center.x() + crossSize, center.y()));

    p.setPen(QColor(255, 255, 255, 20));
    QFont font("Arial", 24, QFont::Bold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 5);
    p.setFont(font);
    p.drawText(QRectF(0, 0, rect.width(), rect.height()), Qt::AlignCenter, "WAITING FOR SOURCE");

    p.end();
    m_lastGridRect = rect;
}

void CanvasView::drawBackground(QPainter* painter, QRectF const& rect)
{
    // 防重入
    if (m_isPainting)
        return;
    PaintLocker lock(m_isPainting);

    // [核心逻辑] 获取最新的一帧
    QImage frameToDraw;
    if (m_hasPendingFrame)
    {
        frameToDraw = m_pendingFrame;
        m_bgImage = m_pendingFrame;  // 更新缓存
        m_hasPendingFrame = false;   // 清空标志
    }
    else
    {
        frameToDraw = m_bgImage;  // 使用缓存
    }

    if (!painter)
        return;

    painter->save();
    try
    {
        painter->fillRect(rect, QColor(20, 20, 20));

        if (!m_bgImage.isNull() && m_bgImage.width() > 0 && m_bgImage.height() > 0)
        {
            // 计算缩放逻辑
            QRectF sceneR = this->sceneRect();
            if (sceneR.isValid())
            {
                qreal scale = qMin(sceneR.width() / (qreal)m_bgImage.width(),
                                   sceneR.height() / (qreal)m_bgImage.height());

                qreal w = m_bgImage.width() * scale;
                qreal h = m_bgImage.height() * scale;
                qreal x = (sceneR.width() - w) / 2.0;
                qreal y = (sceneR.height() - h) / 2.0;

                // [关键] 限制绘制区域，防止浮点数溢出
                QRectF targetRect(x, y, w, h);
                if (targetRect.isValid())
                {
                    painter->drawImage(targetRect, m_bgImage);
                }
            }
        }
    }
    catch (...)
    {
    }
    painter->restore();

    // [补充] 如果在绘制期间又来了新帧，立即请求下一次更新
    if (m_hasPendingFrame)
    {
        QMetaObject::invokeMethod(this->viewport(), "update", Qt::QueuedConnection);
    }
}

void CanvasView::wheelEvent(QWheelEvent* event)
{
    event->ignore();
}
