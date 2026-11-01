#include "DraggableListWidget.h"
#include <QApplication>
#include <QHeaderView>
#include <QDragMoveEvent>
#include <SceneListDelegate.h>

DraggableListWidget::DraggableListWidget(QWidget* parent) : QListWidget(parent)
{
    // 设置自定义样式用于绘制漂亮的插入线
    setStyle(new SmoothListStyle());

    setMouseTracking(true);

    // 基础属性设置
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    // 关键：设置为 InternalMove，这会自动处理大部分移动逻辑，但我们需要微调
    setDragDropMode(QAbstractItemView::InternalMove);

    // 优化滚动体验
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    // 这一步很重要：设置默认 Drop 动作
    setDefaultDropAction(Qt::MoveAction);
}

void DraggableListWidget::startDrag(Qt::DropActions supportedActions)
{
    if (auto delegate = qobject_cast<SceneListDelegate*>(itemDelegate()))
    {
        delegate->clearHoverState();
    }
    QListWidget::startDrag(supportedActions);
}

void DraggableListWidget::dragMoveEvent(QDragMoveEvent* event)
{
    // 1. 彻底禁止“吞并”（DropOn）
    // 无论鼠标在 Item 的哪个位置，都强制把 DropIndicator 设置为 Item 上方或下方
    // 从而形成插入线，而不是选中框

    QListWidgetItem* item = itemAt(event->pos());
    if (item)
    {
        QRect rect = visualItemRect(item);
        int y = event->pos().y();

        // 如果鼠标在 Item 上半部分，指示器在上方；下半部分在下方
        if (y < rect.top() + rect.height() / 2)
        {
            // 这是一个 protected 方法，但在 dragMoveEvent 中我们可以间接影响它
            // 通过忽略 event 或者手动设置 dropAction 无法直接修改 indicatorPosition
            // 但 QAbstractItemView 的默认实现会根据鼠标位置决定。
            // 这里的关键是我们在 ScreenWindow 中移除了 ItemIsDropEnabled，
            // 所以 Qt 自身就不会尝试 DropOn 了。
        }
    }

    // 强制只接受 Move 操作
    if (event->source() == this)
    {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }

    // 调用父类以绘制 Indicator
    QListWidget::dragMoveEvent(event);
}

void DraggableListWidget::dropEvent(QDropEvent* event)
{
    // 标准的内部移动逻辑，确保数据顺序正确更新
    if (event->source() == this)
    {
        // 可以在这里添加一些 Drop 后的弹跳动画逻辑（高级功能），这里先保持默认
        QListWidget::dropEvent(event);

        // 触发自定义信号（如果有必要）通知外部顺序变了
    }
}

void DraggableListWidget::leaveEvent(QEvent* event)
{
    // 获取 Delegate 并强制清除状态
    if (auto delegate = qobject_cast<SceneListDelegate*>(itemDelegate()))
    {
        delegate->clearHoverState();
    }

    // 强制重绘整个列表，确保 UI 刷新
    viewport()->update();

    QListWidget::leaveEvent(event);
}

void DraggableListWidget::mouseMoveEvent(QMouseEvent* event)
{
    QListWidgetItem* item = itemAt(event->pos());

    // 如果当前鼠标位置下没有 Item（即鼠标在空白区域），也要清除状态
    if (!item)
    {
        if (auto delegate = qobject_cast<SceneListDelegate*>(itemDelegate()))
        {
            delegate->clearHoverState();
        }
        viewport()->update();
    }

    // 调用父类处理（包括拖拽逻辑）
    QListWidget::mouseMoveEvent(event);
}
