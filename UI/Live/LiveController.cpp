#include "LiveController.h"
#include "AppConfig.h"
#include "RecordSettingsDialog.h"
#include "skinmgr.h"
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include "ResourceService.h"

LiveController::LiveController(QObject* parent) : QObject(parent)
{
    m_recordController = new RecordController(this);
    m_micCapturer = new WasapiCapturer(this);
    m_sysCapturer = new WasapiCapturer(this);

    connect(
        m_micCapturer, &WasapiCapturer::audioDataReady, this, &LiveController::onAudioDataReceived);
    connect(
        m_sysCapturer, &WasapiCapturer::audioDataReady, this, &LiveController::onAudioDataReceived);

    // [New] 转发音量信号
    connect(
        m_micCapturer, &WasapiCapturer::volumeLevelChanged, this, &LiveController::micVolumeLevel);
    connect(
        m_sysCapturer, &WasapiCapturer::volumeLevelChanged, this, &LiveController::sysVolumeLevel);

    connect(ResourceService::instance(),
            &ResourceService::liveRecordUploaded,
            this,
            &LiveController::onRecordUploadFinished);

    m_liveTimer = new QTimer(this);
    m_liveTimer->setInterval(1000);
    connect(m_liveTimer, &QTimer::timeout, this, &LiveController::onLiveTimerUpdate);

    // [New] 初始化异常轮询定时器
    m_statusPollTimer = new QTimer(this);
    m_statusPollTimer->setInterval(m_pollIntervalMs);
    connect(m_statusPollTimer, &QTimer::timeout, this, &LiveController::onStatusPollTimeout);

    // [New] 连接 LiveService 的查询回调信号
    connect(LiveService::instance(),
            &LiveService::liveStatusFetched,
            this,
            &LiveController::onLiveStatusFetched);

    // [Optimization] Pre-initialize settings dialog to prevent UI freeze on first click
    // ScreenWindow passes 'this' (QWidget) as parent
    if (QWidget* widgetParent = qobject_cast<QWidget*>(parent))
    {
        m_settingsDlg = new RecordSettingsDialog(widgetParent);
        SkinMgr::inst()->setSkin(SK_Screen, m_settingsDlg);
    }

    // [New] Connect LiveService signals
    connect(LiveService::instance(),
            &LiveService::roomListFetched,
            this,
            &LiveController::onRoomListFetched);
    connect(LiveService::instance(),
            &LiveService::roomDetailFetched,
            this,
            &LiveController::onRoomDetailFetched);
    connect(LiveService::instance(),
            &LiveService::statusChanged,
            this,
            &LiveController::onLiveStatusChanged);
    if (m_recordController && m_recordController->getRecorder())
    {
        connect(m_recordController->getRecorder(),
                &FFmpegRecorder::stateChanged,
                this,
                &LiveController::onRecorderStateChanged);

        // Also listen for errors to auto-stop logic
        connect(m_recordController->getRecorder(),
                &FFmpegRecorder::errorOccurred,
                this,
                [this](QString msg) {
                    qWarning() << "[LiveController] Recorder Error:" << msg;
                    // Don't immediately stop, let recorder try to reconnect or finish
                    // But if it's a fatal error, we might need to reset UI
                });
    }
}

LiveController::~LiveController()
{
    stopLive();
}

void LiveController::setRoomId(long long roomId)
{
    m_roomId = roomId;
    if (m_roomId > 0)
    {
        qInfo() << "[LiveController] Room ID set to" << m_roomId
                << ". Fetching details for SYNC...";
        // 主动请求一次详情，确保错过信号也能拿到推流地址
        LiveService::instance()->fetchRoomDetail(m_roomId);
    }
}

void LiveController::initSimulationData()
{
    // 发送初始模拟数据
    emit viewerAdded("榜一大哥", 50, true);
    emit viewerAdded("管理员01", 30, true);
    emit viewerAdded("User_9527", 12, false);
    emit viewerAdded("爱吃西瓜", 5, false);
    emit viewerAdded("路人甲", 1, false);

    emit messageReceived("系统", "欢迎来到直播间！严禁违规内容。", 99);
    emit messageReceived("榜一大哥", "主播好久不见！", 50);
    emit messageReceived("User_9527", "画质不错，清晰度好评", 12);
}

void LiveController::startAudioMonitoring()
{
    RecorderConfig config = AppConfig::instance()->getRecorderConfig();

    if (m_micCapturer && !m_micCapturer->isRunning())
    {
        // 默认使用默认麦克风
        m_micCapturer->setDevice("", WasapiCapturer::Microphone);
        m_micCapturer->start();
    }
    if (m_sysCapturer && !m_sysCapturer->isRunning())
    {
        m_sysCapturer->setDevice("", WasapiCapturer::SystemLoopback);
        m_sysCapturer->start();
    }
}

void LiveController::toggleLiveState()
{
    if (m_isLiveRunning)
    {
        stopLive();
    }
    else
    {
        startLiveProcess();
    }
}

// [New] 启动前安检
bool LiveController::validateConfig()
{
    RecorderConfig config = AppConfig::instance()->getRecorderConfig();

    // 1. 检查录制/推流是否至少开启一项
    if (!config.recording.enable && !config.streaming.enable)
    {
        emit errorOccurred("配置错误", "未启用录制或推流，无法启动。请在设置中开启。");
        return false;
    }

    // [Modified] 移除旧的静态 URL 检查，因为 URL 将由 LiveService 动态填充
    // 仅当用户明确禁用了 streaming 时才跳过
    if (config.streaming.enable)
    {
        // 我们将在 onRoomDetailFetched 中进行最终的 URL 检查
    }

    // 3. 音频设备检查
    if (!validateAudioDevices(config))
    {
        return false;
    }

    return true;
}

bool LiveController::validateAudioDevices(RecorderConfig const& config)
{
    // 简单检查：如果需要混音，尝试获取设备列表看是否为空
    if (config.mixMicrophone)
    {
        auto mics = WasapiCapturer::getAvailableDevices(WasapiCapturer::Microphone);
        if (mics.isEmpty())
        {
            // 警告但不阻止（可能是因为隐私权限没给，或者真的没插麦克风）
            qWarning() << "[LiveController] Warning: MixMic enabled but no microphone found.";
        }
    }
    return true;
}

// [Modified] Async Start Process
void LiveController::startLiveProcess()
{
    if (m_isPreparing)
        return;
    m_isPreparing = true;

    // --- 核心修改：第一步，立刻清理内存中的所有旧流地址 ---
    if (m_settingsDlg)
    {
        QMetaObject::invokeMethod(m_settingsDlg, "loadFromConfig", Qt::DirectConnection);
    }

    // [New] Step 1: Pre-flight Check (Local)
    if (!validateConfig())
    {
        m_isPreparing = false;
        return;
    }

    RecorderConfig config = AppConfig::instance()->getRecorderConfig();

    // 如果只开启了录制，没开启推流，直接启动，不需要请求直播间接口
    if (!config.streaming.enable)
    {
        realStartLive();
        return;
    }

    // [CRITICAL FIX] 优先使用当前选中的 Room ID
    // 如果 m_roomId 有效（由 ScreenWindow 传入），直接获取该房间详情，不要去拉列表重置
    if (m_roomId > 0)
    {
        m_currentRoomId = m_roomId;  // 锁定当前房间
        qInfo() << "[LiveController] Starting live for specific Room ID:" << m_currentRoomId;
        LiveService::instance()->fetchRoomDetail(m_currentRoomId);
    }
    else
    {
        // 只有未指定房间时，才走原来的获取列表流程
        qInfo() << "[LiveController] No Room ID set, fetching list to auto-select.";
        LiveService::instance()->fetchMyRoomList();
    }
}

void LiveController::onRoomListFetched(bool success, QJsonArray const& list, QString const& msg)
{
    if (!m_isPreparing)
    {
        qWarning() << "[AUDIT] roomListFetched ignored, preparing flag lost.";
        return;  // 只有在启动流程中才处理
    }

    if (!success || list.isEmpty())
    {
        m_isPreparing = false;
        emit errorOccurred("启动失败", "无法获取直播间列表，请检查网络或权限。");
        return;
    }

    // 简单策略：默认选择列表中的第一个直播间
    QJsonObject room = list.first().toObject();
    m_currentRoomId = room["roomId"].toVariant().toLongLong();

    qDebug() << "[LiveController] Selected Room ID:" << m_currentRoomId;

    // Step 3: Fetch Detail (to get push url)
    qInfo() << "[AUDIT] Requesting Detail for Room:" << m_currentRoomId;
    LiveService::instance()->fetchRoomDetail(m_currentRoomId);
}

void LiveController::onRoomDetailFetched(bool success,
                                         QJsonObject const& detail,
                                         QString const& msg)
{
    qInfo() << "[CONTROLLER_AUDIT] onRoomDetailFetched callback triggered!";

    // 1. Basic Validation
    if (!success)
    {
        if (m_isPreparing)
        {
            emit errorOccurred("启动失败", "无法获取直播间详情: " + msg);
            m_isPreparing = false;
        }
        return;
    }

    // [FIX] Update m_currentRoomId safely
    long long fetchedId = 0;
    if (detail.contains("roomId"))
    {
        fetchedId = detail["roomId"].toVariant().toLongLong();
        if (fetchedId > 0)
            m_currentRoomId = fetchedId;
    }

    // 2. Parse Cloud Targets
    QList<PushTarget> cloudTargets;
    if (detail.contains("platformList"))
    {
        QJsonValue val = detail["platformList"];
        if (val.isArray())
        {
            QJsonArray platforms = val.toArray();
            for (auto const& v : platforms)
            {
                QJsonObject p = v.toObject();
                QString url = p["pushUrl"].toString();
                if (!url.isEmpty())
                {
                    PushTarget target;
                    target.url = url;
                    target.enable = true;
                    cloudTargets.append(target);
                    qInfo() << "[AUDIT] Found Cloud URL:" << url;
                }
            }
        }
    }

    // 3. [CRITICAL LOGIC FIX] Sync Strategy
    // The server might return empty list if room is "offline".
    // We must prevent clearing valid local config if we are restarting the same room.

    auto currentConfig = AppConfig::instance()->getRecorderConfig();
    bool localHasTargets = !currentConfig.streaming.pushTargets.isEmpty();

    // Condition A: Cloud returned valid targets -> Always Trust Cloud
    if (!cloudTargets.isEmpty())
    {
        qInfo() << "[AUDIT] Syncing" << cloudTargets.size() << "targets from Cloud.";
        AppConfig::instance()->resetStreamingTargets(cloudTargets);
    }
    // Condition B: Cloud is empty, but we have valid local targets for this session
    // AND we are strictly in the "Start Live" flow for the SAME room.
    else if (localHasTargets && m_isPreparing)
    {
        qWarning() << "[AUDIT] Cloud returned empty list (Room offline?), but local cache exists. "
                      "Preserving local config for restart.";
        // Do NOT clear config. Keep existing targets.
    }
    // Condition C: Cloud is empty, and we have nothing -> Clear it to be safe
    else
    {
        qInfo() << "[AUDIT] Syncing 0 targets from Cloud (Clear).";
        AppConfig::instance()->resetStreamingTargets({});
    }

    // 4. Refresh UI
    if (m_settingsDlg)
    {
        QMetaObject::invokeMethod(m_settingsDlg, "loadFromConfig", Qt::DirectConnection);
    }

    // 5. Proceed to Start
    if (m_isPreparing)
    {
        realStartLive();
    }
}
void LiveController::realStartLive()
{
    RecorderConfig config = AppConfig::instance()->getRecorderConfig();

    m_lastRecordingPath = config.recording.pathOrUrl;
    m_shouldKeepRecord = config.recording.keepFile;
    m_lastRoomId = m_currentRoomId;

    // [CRITICAL FIX] 1. Pre-flight Check for Push Targets
    // 如果开启了推流，必须确保至少有一个有效的推流地址
    if (config.streaming.enable)
    {
        bool hasValidTarget = false;

        // 检查新版多路推流列表
        for (auto const& t : config.streaming.pushTargets)
        {
            if (t.enable && !t.url.isEmpty())
            {
                hasValidTarget = true;
                break;
            }
        }

        // 兼容旧版配置 (Fallback)
        if (!hasValidTarget && !config.streaming.pathOrUrl.isEmpty())
        {
            hasValidTarget = true;
        }

        if (!hasValidTarget)
        {
            m_isPreparing = false;
            // 弹出明确错误提示
            emit errorOccurred("启动失败",
                               "未获取到有效的推流地址。\n请检查：\n1. "
                               "直播间是否已配置推流平台\n2. 网络连接是否正常");
            return;
        }
    }

    if (m_recordController->isRecording())
    {
        qWarning() << "[LiveController] Recorder was still running. Force stopping.";
        m_recordController->stopTask();
    }

    startAudioMonitoring();

    // Step 5: Try Start Recorder
    if (m_recordController)
    {
        bool started = m_recordController->startTask(config);
        if (!started)
        {
            m_isPreparing = false;
            // Provide more detail if possible
            emit errorOccurred("启动失败", "推流服务无法启动(Internal Error)。请重试。");
            return;
        }
    }

    // Step 6: Start Audio Capture
    if (m_micCapturer && config.mixMicrophone)
    {
        m_micCapturer->setDevice("", WasapiCapturer::Microphone);
        m_micCapturer->start();
    }
    if (m_sysCapturer && config.mixSystemAudio)
    {
        m_sysCapturer->setDevice("", WasapiCapturer::SystemLoopback);
        m_sysCapturer->start();
    }

    // [New] 启动异常状态轮询
    startStatusPolling();

    // Step 7: Update State
    m_isLiveRunning = true;
    m_liveStartTime = QDateTime::currentDateTime();
    m_liveTimer->start();
    m_isPreparing = false;  // Reset flag

    emit liveStateChanged(true);
    emit durationUpdated("00:00:00");

    // [New] Step 8: Notify Server (Change Status to ON)
    if (config.streaming.enable && m_currentRoomId > 0)
    {
        LiveService::instance()->changeLiveStatus(m_currentRoomId, 1);
    }
}

void LiveController::stopLive()
{
    if (!m_isLiveRunning)
        return;

    m_isLiveRunning = false;
    if (m_recordController)
        m_recordController->stopTask();
    if (m_micCapturer)
        m_micCapturer->stop();
    if (m_sysCapturer)
        m_sysCapturer->stop();
    m_liveTimer->stop();

    // [New] 停止轮询
    stopStatusPolling();

    emit liveStateChanged(false);

    // [New] Notify Server (Change Status to OFF)
    RecorderConfig config = AppConfig::instance()->getRecorderConfig();
    if (config.streaming.enable && m_currentRoomId > 0)
    {
        LiveService::instance()->changeLiveStatus(m_currentRoomId, 0);
    }

    QTimer::singleShot(2000, this, [this]() {
        if (QFile::exists(m_lastRecordingPath))
        {
            qInfo() << "[LiveController] Starting Record Upload:" << m_lastRecordingPath;

            // 调用上传接口
            // 注意：这里需要 roomId，且 mediaType 默认为 0 (根据 ResourceService 定义)
            ResourceService::instance()->uploadLiveRecord(m_lastRecordingPath, m_lastRoomId, 0);
        }
        else
        {
            qWarning() << "[LiveController] Record file not found:" << m_lastRecordingPath;
        }
    });
}

void LiveController::startStatusPolling()
{
    if (m_statusPollTimer && !m_statusPollTimer->isActive() && m_currentRoomId > 0)
    {
        qInfo() << "[LiveController] Starting live status polling for Room:" << m_currentRoomId;
        // 立即执行一次，不要等第一个周期
        onStatusPollTimeout();
        m_statusPollTimer->start();
    }
}

void LiveController::stopStatusPolling()
{
    if (m_statusPollTimer)
    {
        m_statusPollTimer->stop();
    }
}

void LiveController::onLiveStatusChanged(bool success,
                                         long long roomId,
                                         int status,
                                         QString const& msg)
{
    if (!success)
    {
        qWarning() << "[LiveController] Failed to sync live status to server:" << msg;
        // Optionally emit a non-blocking warning to UI
    }
    else
    {
        qDebug() << "[LiveController] Live status synced. Room:" << roomId << " Status:" << status;
    }
}

void LiveController::onRecordUploadFinished(bool success, QString const& url, QString const& msg)
{
    if (success)
    {
        qInfo() << "[LiveController] Record Upload Success:" << url;

        // [核心逻辑] 根据用户配置决定去留
        if (!m_shouldKeepRecord)
        {
            if (QFile::remove(m_lastRecordingPath))
            {
                qInfo() << "[LiveController] Temp record file deleted:" << m_lastRecordingPath;
            }
            else
            {
                qWarning() << "[LiveController] Failed to delete temp record:"
                           << m_lastRecordingPath;
            }
        }
        else
        {
            qInfo() << "[LiveController] Record file kept locally as requested.";
        }
    }
    else
    {
        qWarning() << "[LiveController] Record Upload Failed:" << msg;
        // 上传失败时，通常建议保留文件以防丢失，或者提示用户
    }
}

void LiveController::onLiveStatusFetched(bool success,
                                         QJsonObject const& statusData,
                                         QString const& msg)
{
    // 如果查询失败（网络抖动等），暂不处理，等待下一次轮询
    if (!success)
    {
        qWarning() << "[LiveController] Status poll failed:" << msg;
        return;
    }

    // 校验房间 ID 是否匹配 (防止处理旧的回调)
    long long roomId = statusData["roomId"].toVariant().toLongLong();
    if (roomId != m_currentRoomId)
        return;

    // 获取状态字段 (0=正常, 1=有异常)
    int status = statusData["status"].toInt();

    if (status == 1)
    {
        qCritical() << "[LiveController] CRITICAL: Live Room Exception Detected! Stopping Live...";

        // 解析异常详情 (以便提示用户)
        QString errorDetail = "直播间检测到异常，已自动停止。";
        if (statusData.contains("checkList"))
        {
            QJsonArray list = statusData["checkList"].toArray();
            if (!list.isEmpty())
            {
                QJsonObject firstErr = list.first().toObject();
                // 拼接具体的错误信息，例如 "视频流中断" 或 "内容违规"
                errorDetail += "\n原因: " + firstErr["checkContent"].toString();
            }
        }

        // 1. 强制停止直播
        stopLive();

        // 2. 弹窗通知用户 (emit signal to UI)
        emit errorOccurred("直播异常中断", errorDetail);
    }
    else
    {
        // 状态正常，无操作
        // qDebug() << "[LiveController] Room status OK.";
    }
}

void LiveController::onStatusPollTimeout()
{
    if (m_isLiveRunning && m_currentRoomId > 0)
    {
        // 调用 Service 发起查询
        LiveService::instance()->fetchLiveRoomStatus(m_currentRoomId);
    }
}

void LiveController::showSettings(QWidget* parent)
{
    // Ensure it exists (fallback if parent wasn't valid in constructor)
    if (!m_settingsDlg)
    {
        m_settingsDlg = new RecordSettingsDialog(parent);
        SkinMgr::inst()->setSkin(SK_Screen, m_settingsDlg);
    }

    // Force data refresh
    QMetaObject::invokeMethod(m_settingsDlg, "loadFromConfig");
    m_settingsDlg->exec();
}

void LiveController::onLiveTimerUpdate()
{
    if (!m_isLiveRunning)
        return;
    qint64 seconds = m_liveStartTime.secsTo(QDateTime::currentDateTime());
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;
    QString timeStr = QString("%1:%2:%3")
                          .arg(h, 2, 10, QChar('0'))
                          .arg(m, 2, 10, QChar('0'))
                          .arg(s, 2, 10, QChar('0'));
    emit durationUpdated(timeStr);
}

void LiveController::handleYuvFrame(std::vector<uint8_t> const& data,
                                    int width,
                                    int height,
                                    int64_t pts)
{
    if (m_isLiveRunning && m_recordController)
    {
        m_recordController->addVideoData(data, width, height, pts);
    }

    // [New] AI 旁路 Hook (热插拔)
    // 格式: NV12 对应 FFmpeg 的 AV_PIX_FMT_NV12 (枚举值为 23，具体视版本而定，建议包含头文件引用)
    if (m_aiBridge)
    {
        // AV_PIX_FMT_NV12 在 libavutil/pixfmt.h 中，通常值为 23
        // 确保你的项目包含了 ffmpeg 头文件，或者直接传 23
        m_aiBridge->pushVideo(data, width, height, AV_PIX_FMT_NV12, pts);
    }
}

void LiveController::onRecorderStateChanged(RecorderState state)
{
    if (state == RecorderState::Stopped)
    {
        // If we think we are live, but recorder stopped, we must sync state
        if (m_isLiveRunning)
        {
            qWarning() << "[LiveController] Recorder stopped unexpectedly. Resetting state.";
            stopLive();  // This cleans up timers, audio, and UI
        }
    }
}

void LiveController::enableAIAnalysis(bool enable, QString const& savePath)
{
    // 1. 获取最新配置
    RecorderConfig appConfig = AppConfig::instance()->getRecorderConfig();

    // 2. 判断逻辑：传入的 enable 是直播状态，但我们还要看用户有没有勾选 "开启AI"
    bool finalEnable = enable && appConfig.ai.enable;

    if (finalEnable)
    {
        if (!m_aiBridge)
        {
            m_aiBridge = std::make_shared<AIDataBridge>();

            // [重点] 设置回调
            // 捕获 this 或直接捕获 roomId
            long long currentRoomId = m_currentRoomId;
            m_aiBridge->setUploadCallback(
                [currentRoomId](QByteArray const& data, QString const& name, int type) {
                    // 必须切换到主线程执行 ResourceService，因为 AIDataBridge 在工作线程
                    // ResourceService::instance() 是 QObject，可以直接用 invokeMethod 跨线程
                    QMetaObject::invokeMethod(
                        ResourceService::instance(),
                        [=]() {
                            ResourceService::instance()->uploadLiveFile(
                                data, name, currentRoomId, type);
                        },
                        Qt::QueuedConnection);
                });

            AIBridgeConfig config;
            config.saveDir = savePath;

            // [New] 从配置中计算间隔 (毫秒)
            int fps = appConfig.ai.sampleFps > 0 ? appConfig.ai.sampleFps : 1;
            config.videoIntervalMs = 1000 / fps;

            // [New] 存储策略
            int modeIdx = appConfig.ai.storageMode;
            if (modeIdx == 1)
                config.storageMode = AIStorageMode::SingleStream;
            else if (modeIdx == 2)
                config.storageMode = AIStorageMode::SplitBySecond;
            else
                config.storageMode = AIStorageMode::SeparateFiles;

            config.targetSampleRate = 16000;
            config.targetChannels = 1;
            config.audioSplitDuration = appConfig.ai.audioSplitDuration;
            config.roomId = m_currentRoomId;
            m_aiBridge->start(config);
        }
    }
    else
    {
        if (m_aiBridge)
        {
            m_aiBridge->stop();
            m_aiBridge = nullptr;
            qDebug() << "[LiveController] AI Bridge Disabled.";
        }
    }
}

void LiveController::onAudioDataReceived(QByteArray const& data, int sampleRate, int channels)
{
    if (m_isLiveRunning && m_recordController)
    {
        m_recordController->addAudioData(data, sampleRate, channels);
    }

    // [New] AI 旁路 Hook (热插拔)
    if (m_aiBridge)
    {
        // WasapiCapturer 输出通常是 Float32 (AV_SAMPLE_FMT_FLT = 3)
        // 这里的 pts 我们临时用当前时间，因为 Wasapi 回调里没带 pts
        int64_t pts = QDateTime::currentMSecsSinceEpoch();

        // AV_SAMPLE_FMT_FLT 通常为 3
        m_aiBridge->pushAudio(
            data.constData(), data.size(), sampleRate, channels, AV_SAMPLE_FMT_FLT, pts);
    }
}
