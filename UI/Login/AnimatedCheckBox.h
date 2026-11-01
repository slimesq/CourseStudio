#ifndef ANIMATEDCHECKBOX_H
#define ANIMATEDCHECKBOX_H

#include <QWidget>
#include <QPropertyAnimation>
#include <QColor>

class AnimatedCheckBox : public QWidget
{
    Q_OBJECT
    // 声明一个属性用于动画，范围从 0.0 (未勾选) 到 1.0 (完全勾选)
    Q_PROPERTY(qreal checkProgress READ checkProgress WRITE setCheckProgress)

public:
    explicit AnimatedCheckBox(QWidget* parent = nullptr);
    ~AnimatedCheckBox();

    // 获取当前状态
    bool isChecked() const;
    // 设置状态，可选是否播放动画
    void setChecked(bool checked, bool animate = true);

    // 设置颜色配置 (可选，用于自定义外观)
    void setColors(QColor const& uncheckedBorder,
                   QColor const& checkedBg,
                   QColor const& checkMarkBtn);

signals:
    // 状态改变时发送信号
    void toggled(bool checked);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // 动画属性的 setter 和 getter
    void setCheckProgress(qreal progress);
    qreal checkProgress() const;

private:
    bool m_checked;
    bool m_isHovered;
    qreal m_checkProgress;  // 动画进度 0.0 - 1.0
    QPropertyAnimation* m_animation;

    // 颜色配置
    QColor m_uncheckedBorderColor;
    QColor m_checkedBackgroundColor;
    QColor m_checkMarkColor;
};

#endif  // ANIMATEDCHECKBOX_H
