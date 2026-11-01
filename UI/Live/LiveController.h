#ifndef LIVECONTROLLER_H
#define LIVECONTROLLER_H
#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <QListWidget>
#include <memory>
#include "AIDataBridge.h"
#include "RecordController.h"
#include "WasapiCapturer.h"
#include "RecorderConfig.h"
#include "LiveService.h"  // [New]

class RecordSettingsDialog;  // Forward Declaration

class LiveController : public QObject
{
    Q_OBJECT
public:
    explicit LiveController(QObject* parent = nullptr);
    ~LiveController();

    bool isLiveRunning() const
    {
        return m_isLiveRunning;
    }

    void setRoomId(long long roomId);

    // Call from UI
    void toggleLiveState();
    void showSettings(QWidget* parent);

    // [New] 初始化模拟数据 (在进入直播模式时调用)
    void initSimulationData();

    void startAudioMonitoring();

    // Data Flow
    void handleYuvFrame(std::vector<uint8_t> const& data, int width, int height, int64_t pts);

    void onRecorderStateChanged(RecorderState state);

    void enableAIAnalysis(bool enable, QString const& savePath);

signals:
    void liveStateChanged(bool isRunning);
    void durationUpdated(QString const& timeStr);
    void errorOccurred(QString const& title, QString const& msg);

    // [New] UI Data Signals (彻底解耦，View 只需要监听这些信号)
    void viewerAdded(QString const& name, int level, bool isAdmin);
    void messageReceived(QString const& user, QString const& msg, int level);

    // [New] 音量电平信号 (0.0 - 1.0)
    void micVolumeLevel(float level);
    void sysVolumeLevel(float level);

private slots:
    void onLiveTimerUpdate();
    void onAudioDataReceived(QByteArray const& data, int sampleRate, int channels);

    // [New] LiveService Callbacks
    void onRoomListFetched(bool success, QJsonArray const& list, QString const& msg);
    void onRoomDetailFetched(bool success, QJsonObject const& detail, QString const& msg);
    void onLiveStatusChanged(bool success, long long roomId, int status, QString const& msg);
    void onRecordUploadFinished(bool success, QString const& url, QString const& msg);

    // [New] 直播异常状态查询回调
    void onLiveStatusFetched(bool success, QJsonObject const& statusData, QString const& msg);

    // [New] 定时器触发轮询
    void onStatusPollTimeout();

private:
    void startLiveProcess();  // [Modified] Renamed from startLive to indicate async process
    void realStartLive();     // [New] The actual hardware/encoder start
    void stopLive();

    // [New] 启动/停止异常检测轮询
    void startStatusPolling();
    void stopStatusPolling();

    // [New] 启动前配置校验
    bool validateConfig();
    bool validateAudioDevices(RecorderConfig const& config);

    bool m_isLiveRunning = false;
    QTimer* m_liveTimer;
    QDateTime m_liveStartTime;

    RecordController* m_recordController = nullptr;
    WasapiCapturer* m_micCapturer = nullptr;
    WasapiCapturer* m_sysCapturer = nullptr;

    // [Fix] Cache the settings dialog to prevent lag
    RecordSettingsDialog* m_settingsDlg = nullptr;

    // [New] Live Room Context
    long long m_currentRoomId = 0;
    QString m_pushUrl;
    bool m_isPreparing = false;  // Prevent double clicks

    long long m_roomId = 0;

    // [New] 持有 AI 桥接器
    std::shared_ptr<AIDataBridge> m_aiBridge;

    // [New] 记录本次直播录制的文件路径，用于后续上传和清理
    QString m_lastRecordingPath;
    bool m_shouldKeepRecord = false;
    long long m_lastRoomId = 0;  // 记录房间ID用于上传

    // [New] 异常检测定时器
    QTimer* m_statusPollTimer = nullptr;

    // [New] 轮询间隔 (毫秒)，默认 10秒 或 30秒
    int m_pollIntervalMs = 1000;
};

#endif  // LIVECONTROLLER_H
