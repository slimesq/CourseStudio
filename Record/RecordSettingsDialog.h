// RecordSettingsDialog.h
#ifndef RECORDSETTINGSDIALOG_H
#define RECORDSETTINGSDIALOG_H

#include "UniversalDialog.h"
#include <QTabWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QLabel>
#include <QGroupBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QStandardPaths>
#include <QListWidget>
#include <QScrollArea>
#include "RecorderConfig.h"

// Inherit from UniversalDialog
class RecordSettingsDialog : public UniversalDialog
{
    Q_OBJECT
public:
    explicit RecordSettingsDialog(QWidget* parent = nullptr);

    // 获取当前 UI 配置 (保存时调用)
    RecorderConfig getUIConfig() const;

private slots:
    void loadFromConfig();
    void saveAndAccept();
    void onResolutionChanged();  // 自动计算纵横比

    // 推流列表操作槽
    void onAddPushTarget();
    void onEditPushTarget();
    void onRemovePushTarget();

private:
    void setupUI();

    // Tabs 构建
    QWidget* createVideoTab();
    QWidget* createOutputTab();
    QWidget* createAdvancedTab();
    QWidget* createAITab();

    // 辅助：为 Tab 内容包裹滚动区域
    QWidget* wrapInScrollArea(QWidget* contentWidget);

    // 数据填充辅助
    void populateEncoders(QComboBox* box);
    void populateResList(QComboBox* box);
    void populateFPS(QComboBox* box);

    // 刷新推流列表显示
    void refreshPushList();

private:
    QTabWidget* m_tabWidget;

    // --- [Video] Tab Controls ---
    QComboBox* m_cmbBaseRes;
    QComboBox* m_cmbOutputRes;
    QLabel* m_lblAspectRatio;  // 显示如 "16:9"
    QComboBox* m_cmbFps;
    QComboBox* m_cmbDownscaleFilter;

    // --- [Output] Tab Controls ---
    QComboBox* m_cmbOutputMode;

    // > Streaming
    QCheckBox* m_chkEnableStream;

    // 多路推流列表控件
    QListWidget* m_listPushTargets;
    QPushButton* m_btnAddTarget;
    QPushButton* m_btnEditTarget;
    QPushButton* m_btnDelTarget;

    // 临时存储列表数据
    QList<PushTarget> m_tempPushTargets;

    QComboBox* m_cmbStreamEncoder;
    QSpinBox* m_spnStreamBitrate;
    QSpinBox* m_spnKeyframeInt;
    QComboBox* m_cmbStreamPreset;
    QComboBox* m_cmbStreamRateControl;

    // > Recording
    QCheckBox* m_chkEnableRecord;
    QLineEdit* m_edtSavePath;
    QComboBox* m_cmbRecFormat;
    QComboBox* m_cmbRecEncoder;
    QComboBox* m_cmbRecRateControl;
    QSpinBox* m_spnRecQuality;
    QSpinBox* m_spnRecBitrate;

    // > Audio
    QCheckBox* m_chkMixMic;
    QCheckBox* m_chkMixSys;
    QSpinBox* m_spnAudioBitrate;

    // --- [Advanced] Tab Controls ---
    QComboBox* m_cmbColorSpace;
    QComboBox* m_cmbColorRange;
    QComboBox* m_cmbGPU;

    // UI Panel Settings
    QCheckBox* m_chkShowViewers;
    QCheckBox* m_chkShowMessages;

    QCheckBox* m_chkAutoSplit;
    QSpinBox* m_spnSplitTime;

    // --- [AI] Tab Controls ---
    QCheckBox* m_chkEnableAI;
    QSpinBox* m_spnAIFps;
    QComboBox* m_cmbAIStorageMode;
    QComboBox* m_cmbAIAudioSplit;  // [New] 音频分片下拉框
};

#endif  // RECORDSETTINGSDIALOG_H
