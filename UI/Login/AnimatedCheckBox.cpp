#include "AnimatedCheckBox.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>

AnimatedCheckBox::AnimatedCheckBox(QWidget* parent)
    : QWidget(parent), m_checked(false), m_isHovered(false), m_checkProgress(0.0)
{
    // 设置固定大小，类似于普通复选框的大小
    setFixedSize(22, 22);
    // 设置鼠标为手型
    setCursor(Qt::PointingHandCursor);

    // 初始化默认颜色
    m_uncheckedBorderColor = QColor("#A0A0A0");    //灰色边框
    m_checkedBackgroundColor = QColor("#3498db");  //蓝色背景
    m_checkMarkColor = Qt::white;                  //白色对勾

    // 初始化动画
    m_animation = new QPropertyAnimation(this, "checkProgress", this);
    m_animation->setDuration(250);                         // 动画持续时间 250ms
    m_animation->setEasingCurve(QEasingCurve::InOutQuad);  // 设置平滑曲线
}

AnimatedCheckBox::~AnimatedCheckBox()
{
    // m_animation 作为子对象会被自动清理
}

bool AnimatedCheckBox::isChecked() const
{
    return m_checked;
}

void AnimatedCheckBox::setChecked(bool checked, bool animate)
{
    if (m_checked == checked)
        return;

    m_checked = checked;
    emit toggled(m_checked);

    if (animate)
    {
        // 如果需要动画，设置起始值和结束值并启动
        m_animation->setStartValue(m_checkProgress);
        m_animation->setEndValue(m_checked ? 1.0 : 0.0);
        m_animation->start();
    }
    else
    {
        // 如果不需要动画，直接设置进度并重绘
        setCheckProgress(m_checked ? 1.0 : 0.0);
    }
}

void AnimatedCheckBox::setColors(QColor const& uncheckedBorder,
                                 QColor const& checkedBg,
                                 QColor const& checkMarkBtn)
{
    m_uncheckedBorderColor = uncheckedBorder;
    m_checkedBackgroundColor = checkedBg;
    m_checkMarkColor = checkMarkBtn;
    update();
}

void AnimatedCheckBox::setCheckProgress(qreal progress)
{
    m_checkProgress = progress;
    update();  // 进度改变时触发重绘
}

qreal AnimatedCheckBox::checkProgress() const
{
    return m_checkProgress;
}

void AnimatedCheckBox::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 定义绘制区域，留一点边距
    QRectF rect = this->rect().adjusted(2, 2, -2, -2);
    qreal cornerRadius = 4.0;

    // --- 1. 绘制背景框 ---
    if (m_checked || m_checkProgress > 0)
    {
        // 勾选状态或正在动画中：填充背景色，无边框
        painter.setPen(Qt::NoPen);
        // 根据进度插值计算背景颜色的透明度，实现平滑过渡
        QColor bgColor = m_checkedBackgroundColor;
        // 当从选中变为未选中时，让背景色慢慢淡出
        if (!m_checked && m_animation->state() == QAbstractAnimation::Running)
        {
            bgColor.setAlphaF(m_checkProgress);
        }
        painter.setBrush(bgColor);
        painter.drawRoundedRect(rect, cornerRadius, cornerRadius);
    }
    else
    {
        // 未勾选状态：绘制边框，无填充
        // 鼠标悬停时边框颜色加深
        painter.setPen(QPen(m_isHovered ? m_checkedBackgroundColor : m_uncheckedBorderColor, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect, cornerRadius, cornerRadius);
    }

    // --- 2. 绘制动态对勾 ---
    if (m_checkProgress > 0.01)
    {  // 只有进度大于0时才绘制
        QPainterPath path;
        // 定义对勾的三个关键点 (相对于 rect)
        // P1: 左侧起始点, P2: 底部拐点, P3: 右侧结束点
        QPointF p1(rect.left() + rect.width() * 0.25, rect.top() + rect.height() * 0.50);
        QPointF p2(rect.left() + rect.width() * 0.45, rect.bottom() - rect.height() * 0.25);
        QPointF p3(rect.right() - rect.width() * 0.20, rect.top() + rect.height() * 0.25);

        path.moveTo(p1);

        // 动画逻辑：分两段绘制
        if (m_checkProgress <= 0.5)
        {
            // 前半段动画：绘制从 P1 到 P2 的一部分
            // 将 0.0-0.5 的进度映射到 0.0-1.0
            qreal relativeProgress = m_checkProgress * 2.0;
            QPointF currentP = p1 + (p2 - p1) * relativeProgress;
            path.lineTo(currentP);
        }
        else
        {
            // 后半段动画：已经画完了 P1->P2，现在绘制 P2 到 P3 的一部分
            path.lineTo(p2);
            // 将 0.5-1.0 的进度映射到 0.0-1.0
            qreal relativeProgress = (m_checkProgress - 0.5) * 2.0;
            QPointF currentP = p2 + (p3 - p2) * relativeProgress;
            path.lineTo(currentP);
        }

        // 设置对勾的画笔风格
        QPen checkPen(m_checkMarkColor, 2.5);
        checkPen.setCapStyle(Qt::RoundCap);    // 圆形线帽
        checkPen.setJoinStyle(Qt::RoundJoin);  // 圆形连接点
        painter.setPen(checkPen);
        painter.setBrush(Qt::NoBrush);
        // 绘制计算好的路径
        painter.drawPath(path);
    }
}

void AnimatedCheckBox::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->pos()))
    {
        setChecked(!m_checked, true);  // 点击时切换状态并播放动画
    }
    QWidget::mouseReleaseEvent(event);
}

void AnimatedCheckBox::enterEvent(QEnterEvent* event)
{
    m_isHovered = true;
    if (!m_checked)
        update();  // 仅在未选中时更新以显示悬停边框
    QWidget::enterEvent(event);
}

void AnimatedCheckBox::leaveEvent(QEvent* event)
{
    m_isHovered = false;
    if (!m_checked)
        update();
    QWidget::leaveEvent(event);
}
