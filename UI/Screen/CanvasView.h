#pragma once
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QOpenGLWidget>
#include <memory>
#include <QPixmap>
#include "Scene.h"
#include "SceneGraphicsAdapter.h"

class CanvasView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit CanvasView(QWidget* parent = nullptr);
    ~CanvasView() override;

    // 设置要显示的场景数据模型
    void setSceneModel(std::shared_ptr<Scene> scene);

    // 更新背景图像（视频/采集画面）
    void updateBackgroundImage(QImage const& img, int generation);

    void setLiveMode(bool isLive);
    bool isLiveMode() const
    {
        return m_isLiveMode;
    }

    void rebuildVisualItems();

    int incrementGeneration()
    {
        return ++m_currentGeneration;
    }
    int currentGeneration() const
    {
        return m_currentGeneration;
    }

signals:
    // 信号中继：将内部 Adapter 的请求转发给外部
    void sourceSwitchRequested(std::shared_ptr<SceneItem> item);

public slots:
    // 仅触发重绘
    void triggerRefresh();

protected:
    void drawBackground(QPainter* painter, QRectF const& rect) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private slots:
    void performFitInView();

private:
    // 辅助函数：更新网格缓存
    void updateGridCache(QRectF const& rect);

private:
    QImage m_bgImage;
    QGraphicsScene* m_uiScene;
    std::unique_ptr<SceneGraphicsAdapter> m_adapter;
    bool m_isLiveMode = false;
    bool m_isPainting = false;

    QImage m_pendingFrame;
    bool m_hasPendingFrame = false;

    // 背景网格缓存 (优化空闲时的绘制性能)
    QPixmap m_gridCache;
    QRectF m_lastGridRect;
    std::atomic<int> m_currentGeneration{0};  // [New]
};
