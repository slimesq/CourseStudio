#include "LiveDockPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QCursor>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

LiveDockPanel::LiveDockPanel(QString const& title, QWidget* content, QWidget* parent)
    : QWidget(parent), m_dockParent(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setObjectName("LiveDockPanel");
    initUI(title, content);
}

void LiveDockPanel::initUI(QString const& title, QWidget* content)
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(1, 1, 1, 1);
    mainLayout->setSpacing(0);

    m_header = new QWidget(this);
    m_header->setObjectName("DockHeader");
    m_header->setFixedHeight(36);

    QHBoxLayout* headLayout = new QHBoxLayout(m_header);
    headLayout->setContentsMargins(12, 0, 8, 0);

    QLabel* lblTitle = new QLabel(title, m_header);
    lblTitle->setObjectName("DockTitleLabel");

    m_btnFloat = new QPushButton("❐", m_header);
    m_btnFloat->setFixedSize(28, 24);
    m_btnFloat->setCursor(Qt::PointingHandCursor);
    m_btnFloat->setObjectName("DockBtnFloat");

    m_btnClose = new QPushButton("✕", m_header);
    m_btnClose->setFixedSize(28, 24);
    m_btnClose->setCursor(Qt::PointingHandCursor);
    m_btnClose->setObjectName("DockBtnClose");
    m_btnClose->setVisible(false);

    connect(m_btnFloat, &QPushButton::clicked, this, [this]() { setFloating(!m_isFloating); });
    connect(m_btnClose, &QPushButton::clicked, this, [this]() { setFloating(false); });

    headLayout->addWidget(lblTitle);
    headLayout->addStretch();
    headLayout->addWidget(m_btnFloat);
    headLayout->addWidget(m_btnClose);

    mainLayout->addWidget(m_header);

    if (content)
    {
        content->setParent(this);
        mainLayout->addWidget(content, 1);
    }
}

void LiveDockPanel::setAsBottomPanel(bool isBottom)
{
    m_isBottomPanel = isBottom;
    update();
}

void LiveDockPanel::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRect rect = this->rect();
    QRectF bgRect = QRectF(rect).adjusted(1, 1, -1, -1);

    QPainterPath path;
    path.addRoundedRect(bgRect, 8, 8);

    QColor glassColor(252, 253, 255, 245);
    p.setBrush(glassColor);
    p.setPen(QPen(QColor("#EBEEF5"), 1));
    p.drawPath(path);

    QLinearGradient lineGrad(rect.left() + 10, 36, rect.right() - 10, 36);
    lineGrad.setColorAt(0, QColor(0, 0, 0, 0));
    lineGrad.setColorAt(0.5, QColor(0, 0, 0, 20));
    lineGrad.setColorAt(1, QColor(0, 0, 0, 0));

    p.setPen(QPen(QBrush(lineGrad), 1));
    p.drawLine(rect.left() + 10, 36, rect.right() - 10, 36);
}

void LiveDockPanel::setFloating(bool floating)
{
    if (m_isFloating == floating)
        return;

    this->hide();
    this->setGraphicsEffect(nullptr);
    m_isFloating = floating;

    if (m_isFloating)
    {
        this->setParent(nullptr);
        this->setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::Tool);
        this->setAttribute(Qt::WA_TranslucentBackground, true);
        this->setAttribute(Qt::WA_NoSystemBackground, true);

        QPoint targetPos = QCursor::pos();
        targetPos.setX(targetPos.x() - 100);
        targetPos.setY(targetPos.y() - 20);

        this->resize(320, 450);
        this->move(targetPos);
        this->show();

        QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(20);
        shadow->setColor(QColor(0, 0, 0, 150));
        shadow->setOffset(0, 4);
        this->setGraphicsEffect(shadow);

        m_btnFloat->setVisible(false);
        m_btnClose->setVisible(true);
        this->raise();
        this->activateWindow();
    }
    else
    {
        this->setWindowFlags(Qt::Widget);
        this->setAttribute(Qt::WA_TranslucentBackground, true);
        this->setAttribute(Qt::WA_NoSystemBackground, true);
        this->setParent(m_dockParent);
        m_btnFloat->setVisible(true);
        m_btnClose->setVisible(false);
        this->show();
    }
    emit floatStatusChanged(m_isFloating);
}

void LiveDockPanel::closeEvent(QCloseEvent* event)
{
    if (m_isFloating)
    {
        setFloating(false);
        event->ignore();
    }
    else
    {
        QWidget::closeEvent(event);
    }
}

void LiveDockPanel::mousePressEvent(QMouseEvent* event)
{
    if (m_isFloating && event->button() == Qt::LeftButton && event->pos().y() <= 36)
    {
#ifdef Q_OS_WIN
        if (ReleaseCapture())
        {
            QWidget* win = this->window();
            if (win)
            {
                SendMessage(
                    reinterpret_cast<HWND>(win->winId()), WM_SYSCOMMAND, SC_MOVE + HTCAPTION, 0);
            }
        }
        event->ignore();
        return;
#else
        m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
#endif
    }
    QWidget::mousePressEvent(event);
}

void LiveDockPanel::mouseMoveEvent(QMouseEvent* event)
{
#ifndef Q_OS_WIN
    if (m_isFloating && (event->buttons() & Qt::LeftButton) && event->pos().y() <= 36)
    {
        move(event->globalPosition().toPoint() - m_dragPos);
        event->accept();
    }
#endif
    QWidget::mouseMoveEvent(event);
}
