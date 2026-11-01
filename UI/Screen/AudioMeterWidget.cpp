#include "AudioMeterWidget.h"
#include <QPainter>
#include <QLinearGradient>
#include <QColor>

AudioMeterWidget::AudioMeterWidget(QString const& label, QWidget* parent)
    : QWidget(parent), m_label(label), m_level(0.0f)
{
    setFixedSize(120, 10);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void AudioMeterWidget::setLevel(float level)
{
    if (m_level != level)
    {
        m_level = level;
        update();
    }
}

void AudioMeterWidget::paintEvent(QPaintEvent* event)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(240, 242, 245));
    p.drawRoundedRect(rect(), 4, 4);

    int contentH = 6;
    int yOffset = (height() - contentH) / 2;

    p.setPen(QColor(96, 98, 102));
    p.setFont(QFont("Arial", 8, QFont::Bold));
    QRect textRect(5, 0, 25, height());
    p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, m_label);

    int barX = 32;
    int barW = width() - barX - 5;

    p.setBrush(QColor(220, 223, 230));
    p.drawRoundedRect(barX, yOffset, barW, contentH, contentH / 2, contentH / 2);

    float level = qBound(0.0f, m_level, 1.0f);
    if (level > 0.01f)
    {
        int fillW = static_cast<int>(barW * level);
        QLinearGradient grad(barX, 0, barX + barW, 0);
        grad.setColorAt(0.0, QColor("#67C23A"));
        grad.setColorAt(0.6, QColor("#E6A23C"));
        grad.setColorAt(1.0, QColor("#F56C6C"));

        p.setBrush(grad);
        p.drawRoundedRect(barX, yOffset, fillW, contentH, contentH / 2, contentH / 2);
    }
}
