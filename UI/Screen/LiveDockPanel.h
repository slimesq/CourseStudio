#ifndef LIVEDOCKPANEL_H
#define LIVEDOCKPANEL_H

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QPoint>

class LiveDockPanel : public QWidget
{
    Q_OBJECT
public:
    explicit LiveDockPanel(QString const& title, QWidget* content, QWidget* parent = nullptr);

    bool isFloating() const
    {
        return m_isFloating;
    }
    void setFloating(bool floating);
    void setAsBottomPanel(bool isBottom);

signals:
    void floatStatusChanged(bool isFloating);

protected:
    void closeEvent(QCloseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void initUI(QString const& title, QWidget* content);

    bool m_isFloating = false;
    bool m_isBottomPanel = false;
    QWidget* m_dockParent = nullptr;
    QPushButton* m_btnFloat;
    QPushButton* m_btnClose;
    QPoint m_dragPos;
    QWidget* m_header;
};

#endif  // LIVEDOCKPANEL_H
