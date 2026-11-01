#ifndef UNIVERSALDIALOG_H
#define UNIVERSALDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <functional>

// 继承自 QDialog 以获得模态支持
class UniversalDialog : public QDialog
{
    Q_OBJECT
public:
    explicit UniversalDialog(QWidget* parent = nullptr);

    // --- 核心配置接口 ---
    void setTitle(QString const& title);
    void setMessage(QString const& message);  // 简单文本模式
    void setContentWidget(QWidget* widget);   // 高级自定义模式

    // 添加按钮
    // text: 按钮文字
    // style: 样式表 (可空，空则使用默认)
    // callback: 点击后的回调 (返回 true 则关闭窗口)
    void addButton(QString const& text,
                   QString const& style = "",
                   std::function<bool()> callback = nullptr);

    // --- 静态便捷方法 ---
    static void showInfo(QWidget* parent, QString const& title, QString const& msg);
    static void showError(QWidget* parent, QString const& title, QString const& msg);
    static bool showConfirm(QWidget* parent,
                            QString const& title,
                            QString const& msg);  // 返回 true 表示点击了确定

protected:
    // 实现无边框拖拽
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void initUI();
    void clearContent();  // 新增
private:
    QLabel* m_titleLabel;
    QWidget* m_contentContainer;
    QVBoxLayout* m_contentLayout;
    QHBoxLayout* m_btnLayout;
    QFrame* m_mainFrame;

    // 拖拽相关
    bool m_isPressed = false;
    QPoint m_dragPosition;
};

#endif  // UNIVERSALDIALOG_H
