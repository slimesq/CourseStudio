#pragma once
#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QImage>
#include <QPainter>

class DebugImageWidget : public QWidget
{
public:
    static DebugImageWidget& instance()
    {
        static DebugImageWidget s;
        return s;
    }

    void updateImage(QImage const& img)
    {
        if (img.isNull())
            return;
        m_img = img;
        // Throttle updates to UI
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        if (!m_img.isNull())
        {
            p.drawImage(rect(), m_img);
        }
        else
        {
            p.setPen(Qt::white);
            p.drawText(rect(), Qt::AlignCenter, "Waiting for Frame...");
        }
    }

private:
    DebugImageWidget()
    {
        setWindowTitle("DEBUG FRAME VIEWER");
        resize(640, 360);
        setAttribute(Qt::WA_ShowWithoutActivating);
    }
    QImage m_img;
};
