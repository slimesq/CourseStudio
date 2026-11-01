#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QEvent>  // [新增]
#include <memory>
#include "Scene.h"
#include "SceneCompositor.h"
#include "YUVTestWidget.h"
#include "ISource.h"
#include "RecordController.h"
#include "RecordSettingsDialog.h"

// 辅助小组件：用于显示原始源的画面 (模拟监视器)
class RawSourcePreview : public QLabel
{
    Q_OBJECT
public:
    explicit RawSourcePreview(std::shared_ptr<ISource> source, QWidget* parent = nullptr);
    ~RawSourcePreview();
public slots:
    void refreshFrame();

private:
    std::shared_ptr<ISource> m_source;
    QTimer* m_timer;
};

// 主测试窗口
class TestDemoWindow : public QWidget
{
    Q_OBJECT
public:
    explicit TestDemoWindow(QWidget* parent = nullptr);
    ~TestDemoWindow();

    void initSystem();

protected:
    // [修改] 我们改用 eventFilter 监听子控件，不再依赖主窗口 resizeEvent
    // void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;  // [新增]

private slots:
    void onYuvDataReceived(std::vector<uint8_t> const& data, int width, int height, int64_t pts);
    void onRecordClicked();
    void updateRecordDuration();
    void onSettingsClicked();

private:
    void setupUi();
    void loadSources();

private:
    QWidget* m_topContainer;
    QHBoxLayout* m_topLayout;
    QWidget* m_yuvContainer;
    YUVTestWidget* m_yuvDisplay;
    QLabel* m_statusLabel;

    int m_lastVideoW = 1920;
    int m_lastVideoH = 1080;

    void updateYuvLayout();

    QPushButton* m_btnRecord;
    QPushButton* m_btnSettings;
    QTimer* m_durationTimer;

    std::shared_ptr<Scene> m_scene;
    std::unique_ptr<SceneCompositor> m_compositor;
    std::unique_ptr<RecordController> m_recordController;
    std::vector<std::shared_ptr<ISource>> m_activeSources;
};
