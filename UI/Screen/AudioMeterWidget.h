#ifndef AUDIOMETERWIDGET_H
#define AUDIOMETERWIDGET_H

#include <QWidget>
#include <QString>

class AudioMeterWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AudioMeterWidget(QString const& label, QWidget* parent = nullptr);

    void setLevel(float level);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_label;
    float m_level;
};

#endif  // AUDIOMETERWIDGET_H
