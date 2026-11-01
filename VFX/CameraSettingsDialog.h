#ifndef CAMERASETTINGSDIALOG_H
#define CAMERASETTINGSDIALOG_H

#include "UniversalDialog.h"
#include <QListWidget>
#include <QStackedWidget>
#include <QSlider>
#include <QSpinBox>
#include <QLabel>
#include "VideoEffectSystem.h"
#include "AnimatedCheckBox.h"  // [New] 引入自定义复选框

class CameraSettingsDialog : public UniversalDialog
{
    Q_OBJECT
public:
    explicit CameraSettingsDialog(std::shared_ptr<EffectManager> manager,
                                  QWidget* parent = nullptr);

private:
    void initStyle();
    void initLayout();
    void initSidebar();
    void initPages();

    // [Modified] 辅助构建函数
    QWidget* createEffectRow(QString const& label,
                             QString const& effectName,
                             QString const& paramKey,
                             int min,
                             int max,
                             int defaultVal,
                             float scale = 1.0f);

    QWidget* createBeautyPage();
    QWidget* createBodyPage();
    QWidget* createFilterPage();
    QWidget* createStickerPage();
    QWidget* createWatermarkPage();

private:
    QListWidget* m_sidebar;
    QStackedWidget* m_pages;
    std::shared_ptr<EffectManager> m_manager;
};

#endif  // CAMERASETTINGSDIALOG_H
