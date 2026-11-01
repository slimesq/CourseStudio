#ifndef CUSTOMTRAYMENU_H
#define CUSTOMTRAYMENU_H

#include <QWidget>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include <QEvent>

// =========================================================================
// [类] CustomMenuItem
// [描述] 菜单中的单行选项 (图标 + 文字)，支持 Hover 高亮
// =========================================================================
class CustomMenuItem : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief 构造函数
     * @param iconPath 图标路径 (推荐 16x16 或 20x20 PNG/SVG)
     * @param text     显示文字
     */
    explicit CustomMenuItem(QString const& iconPath,
                            QString const& text,
                            QWidget* parent = nullptr);
    void setTextColor(QString const& colorCode);

signals:
    void clicked();  // 点击信号

protected:
    void enterEvent(QEnterEvent* event) override;         // 鼠标移入：变色
    void leaveEvent(QEvent* event) override;              // 鼠标移出：恢复
    void mouseReleaseEvent(QMouseEvent* event) override;  // 鼠标松开：触发点击

private:
    QLabel* m_iconLabel;
    QLabel* m_textLabel;
};

// =========================================================================
// [类] CustomTrayMenu
// [描述] 自定义托盘菜单容器 (无边框窗口 + 阴影 + 圆角背景)
// =========================================================================
class CustomTrayMenu : public QWidget
{
    Q_OBJECT
public:
    explicit CustomTrayMenu(QWidget* parent = nullptr);

    // 添加菜单项
    void addMenuItem(CustomMenuItem* item);

    // 添加分割线
    void addSeparator();

    // 在指定位置显示 (通常传 QCursor::pos())
    void showAt(QPoint const& globalPos);

protected:
    // 监听鼠标点击，如果点在窗口外则关闭 (模拟菜单行为)
    bool event(QEvent* event) override;

private:
    QWidget* m_container;       // 白底圆角容器
    QVBoxLayout* m_listLayout;  // 垂直布局
};

#endif  // CUSTOMTRAYMENU_H
