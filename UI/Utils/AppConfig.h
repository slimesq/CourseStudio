// AppConfig.h
#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <QObject>
#include <QSize>
#include <QColor>
#include <QString>
#include "RecorderConfig.h"  // 包含新的结构体定义

class AppConfig : public QObject
{
    Q_OBJECT
public:
    static AppConfig* instance();

    void loadConfig();
    void saveConfig();

    // --- 获取完整的业务配置对象 ---
    // (提供给 RecordController/SettingsDialog 使用)
    RecorderConfig getRecorderConfig() const;

    // 设置部分配置 (用于 UI 更新)
    void setVideoBase(VideoBaseConfig const& cfg);
    void setStreaming(TranscodeConfig const& cfg);
    void setRecording(TranscodeConfig const& cfg);
    void setAudio(AudioConfig const& cfg);
    void setAIConfig(AIConfig const& cfg);

    void resetStreamingToEmpty();  // 新增
    void resetStreamingTargets(QList<PushTarget> const& newTargets);

    // --- 原有 UI 布局参数 (保持不变) ---
    int cardCount() const;
    int cardSpacing() const;
    int sideMargin() const;
    QSize baseCardSize() const;
    int topBarHeight() const;
    int titleHeight() const;
    QSize windowDefaultSize() const;

    int snapThreshold() const;
    int resizeHandleSize() const;
    bool enableSnap() const;
    bool hideUIOnInteract() const;
    bool enableBoundary() const;

    QColor themeColor() const;
    QColor handleColor() const;
    QColor snapLineColor() const;
    QColor canvasBgColor() const;

    // --- 辅助 Getter (用于 UI 初始化显示) ---
    VideoBaseConfig videoBase() const
    {
        return m_recorderConfig.base;
    }
    TranscodeConfig streamingConfig() const
    {
        return m_recorderConfig.streaming;
    }
    TranscodeConfig recordingConfig() const
    {
        return m_recorderConfig.recording;
    }
    AudioConfig audioConfig() const
    {
        return m_recorderConfig.audio;
    }

    // 兼容旧接口 (重定向到新的 VideoBase)
    int videoTargetFPS() const
    {
        return (int)m_recorderConfig.base.fps;
    }
    QSize videoBaseResolution() const
    {
        return m_recorderConfig.base.baseResolution;
    }

    // --- 摄像头与采集参数 ---
    QSize cameraDefaultResolution() const;
    int cameraRetryIntervalMs() const;
    QSize wgcFallbackResolution() const;
    int texturePoolCleanupMs() const;
    int ringBufferSize() const;

    double cardAspectRatio() const;

    // --- [New] UI Panel Configuration (Phase 1) ---
    bool showViewerPanel() const
    {
        return m_recorderConfig.showViewerPanel;
    }
    void setShowViewerPanel(bool show)
    {
        if (m_recorderConfig.showViewerPanel != show)
        {
            m_recorderConfig.showViewerPanel = show;
            saveConfig();  // 立即保存
            emit configChanged();
        }
    }

    bool showMessagePanel() const
    {
        return m_recorderConfig.showMessagePanel;
    }
    void setShowMessagePanel(bool show)
    {
        if (m_recorderConfig.showMessagePanel != show)
        {
            m_recorderConfig.showMessagePanel = show;
            saveConfig();  // 立即保存
            emit configChanged();
        }
    }

signals:
    // [New] 配置变更信号，用于 UI 热更新
    void configChanged();

private:
    explicit AppConfig(QObject* parent = nullptr);
    static AppConfig* m_instance;

    QColor parseColor(QString const& hexStr, QColor const& defaultColor);

    // 辅助 JSON 解析函数
    void parseVideoBase(QJsonObject const& root);
    void parseStreaming(QJsonObject const& root);
    void parseRecording(QJsonObject const& root);
    void parseAudio(QJsonObject const& root);
    QJsonObject serializeAI() const;
    void parseAI(QJsonObject const& root);

    QJsonObject serializeVideoBase() const;
    QJsonObject serializeTranscode(TranscodeConfig const& cfg) const;
    QJsonObject serializeAudio() const;

    // --- 数据存储 ---
    RecorderConfig m_recorderConfig;  // 核心数据源

    // UI 变量
    int m_cardCount;
    int m_cardSpacing;
    int m_sideMargin;
    QSize m_baseCardSize;
    int m_topBarHeight;
    int m_titleHeight;
    QSize m_windowDefaultSize;

    int m_snapThreshold;
    int m_resizeHandleSize;
    bool m_enableSnap;
    bool m_hideUIOnInteract;
    bool m_enableBoundary;

    QColor m_themeColor;
    QColor m_handleColor;
    QColor m_snapLineColor;
    QColor m_canvasBgColor;

    // 采集与池化配置
    QSize m_cameraDefaultResolution;
    int m_cameraRetryIntervalMs;
    QSize m_wgcFallbackResolution;
    int m_texturePoolCleanupMs;
    int m_ringBufferSize;
};

#endif  // APPCONFIG_H
