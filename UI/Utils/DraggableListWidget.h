#ifndef DRAGGABLELISTWIDGET_H
#define DRAGGABLELISTWIDGET_H

#include <QListWidget>
#include <QDrag>
#include <QMimeData>
#include <QPainter>
#include <QProxyStyle>

// 自定义样式以优化 DropIndicator（插入线）的绘制
class SmoothListStyle : public QProxyStyle
{
public:
    void drawPrimitive(PrimitiveElement element,
                       QStyleOption const* option,
                       QPainter* painter,
                       QWidget const* widget = nullptr) const override
    {
        // 自定义绘制拖拽插入线 (Drop Indicator)
        if (element == PE_IndicatorItemViewItemDrop)
        {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);

            // 绘制一条带发光的蓝色线条，而不是默认的黑色虚线
            QColor lineColor(0, 120, 215);  // Win10/11 风格蓝
            QPen pen(lineColor);
            pen.setWidth(2);
            painter->setPen(pen);

            // 只是绘制一条横线
            if (option->rect.height() == 0)
            {
                painter->drawLine(option->rect.topLeft(), option->rect.topRight());
            }
            else
            {
                painter->drawRect(option->rect);
            }

            painter->restore();
        }
        else
        {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
        }
    }
};

class DraggableListWidget : public QListWidget
{
    Q_OBJECT
    Q_PROPERTY(QColor hoverColor READ hoverColor WRITE setHoverColor)
    Q_PROPERTY(QColor selectedColor READ selectedColor WRITE setSelectedColor)
    Q_PROPERTY(QColor barColor READ barColor WRITE setBarColor)
    Q_PROPERTY(QColor iconColor READ iconColor WRITE setIconColor)
    Q_PROPERTY(QColor separatorColor READ separatorColor WRITE setSeparatorColor)
public:
    explicit DraggableListWidget(QWidget* parent = nullptr);
    QColor hoverColor() const
    {
        return m_hoverColor;
    }
    void setHoverColor(QColor const& c)
    {
        m_hoverColor = c;
        update();
    }

    QColor selectedColor() const
    {
        return m_selectedColor;
    }
    void setSelectedColor(QColor const& c)
    {
        m_selectedColor = c;
        update();
    }

    QColor barColor() const
    {
        return m_barColor;
    }
    void setBarColor(QColor const& c)
    {
        m_barColor = c;
        update();
    }

    QColor iconColor() const
    {
        return m_iconColor;
    }
    void setIconColor(QColor const& c)
    {
        m_iconColor = c;
        update();
    }

    QColor separatorColor() const
    {
        return m_separatorColor;
    }
    void setSeparatorColor(QColor const& c)
    {
        m_separatorColor = c;
        update();
    }

protected:
    // 重写拖拽开始：生成高质量的半透明截图
    void startDrag(Qt::DropActions supportedActions) override;

    // 重写拖拽移动：强制只能在 Item 之间插入，严禁 Item 之上
    void dragMoveEvent(QDragMoveEvent* event) override;

    // 重写 Drop：确保数据正确移动
    void dropEvent(QDropEvent* event) override;

    // [New] 重写离开和移动事件
    void leaveEvent(QEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    QColor m_hoverColor = QColor("#F5F7FA");
    QColor m_selectedColor = QColor("#ECF5FF");
    QColor m_barColor = QColor("#006EFF");
    QColor m_iconColor = QColor("#606266");
    QColor m_separatorColor = QColor("#EBEEF5");
};

#endif  // DRAGGABLELISTWIDGET_H
