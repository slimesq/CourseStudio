// AppConfig.cpp
#include "AppConfig.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>  // [New] Added for JSON Array
#include <QDebug>
#include <QCoreApplication>
#include <QStandardPaths>
#include "Logger.h"
#include <QTimer>

AppConfig* AppConfig::m_instance = nullptr;

AppConfig* AppConfig::instance()
{
    if (!m_instance)
    {
        m_instance = new AppConfig();
    }
    return m_instance;
}

AppConfig::AppConfig(QObject* parent) : QObject(parent)
{
    // --- 1. UI 布局默认值 ---
    m_cardCount = 5;
    m_cardSpacing = 50;
    m_sideMargin = 90;
    m_baseCardSize = QSize(220, 340);
    m_topBarHeight = 60;
    m_titleHeight = 80;
    m_windowDefaultSize = QSize(1280, 860);
    m_snapThreshold = 3;
    m_resizeHandleSize = 8;
    m_enableSnap = true;
    m_hideUIOnInteract = true;
    m_enableBoundary = true;
    m_themeColor = QColor(0, 120, 215);
    m_handleColor = Qt::white;
    m_snapLineColor = QColor(0, 255, 255);
    m_canvasBgColor = QColor(30, 30, 30);

    // --- 2. 采集默认值 ---
    m_cameraDefaultResolution = QSize(1280, 720);
    m_cameraRetryIntervalMs = 500;
    m_wgcFallbackResolution = QSize(1920, 1080);
    m_texturePoolCleanupMs = 5000;
    m_ringBufferSize = 3;

    // --- 3. 初始化 RecorderConfig 默认值 (企业级预设) ---

    // [Video Base] 画布 1080p, 输出 720p (常用直播配置)
    m_recorderConfig.base.baseResolution = QSize(1920, 1080);
    m_recorderConfig.base.outputResolution = QSize(1280, 720);
    m_recorderConfig.base.fps = 30.0;
    m_recorderConfig.base.downscaleFilter = DownscaleFilter::Lanczos;

    // [Streaming] 推流配置 (CBR, 稳定优先)
    m_recorderConfig.streaming.enable = false;
    m_recorderConfig.streaming.encoderName = "h264_nvenc";
    m_recorderConfig.streaming.rateControl = RateControlMode::CBR;
    m_recorderConfig.streaming.bitrateKbps = 4500;       // 720p60 或 1080p30 标准
    m_recorderConfig.streaming.keyframeIntervalSec = 2;  // 强制2秒
    m_recorderConfig.streaming.preset = "p4";            // Medium
    m_recorderConfig.streaming.profile = "high";
    m_recorderConfig.streaming.bFrames = 0;  // 低延迟
    m_recorderConfig.streaming.format = "flv";

    // [New] 初始化空的推流列表
    m_recorderConfig.streaming.pushTargets.clear();

    // [Recording] 录制配置 (CQP, 画质优先)
    m_recorderConfig.recording.enable = true;
    m_recorderConfig.recording.encoderName = "hevc_nvenc";  // H.265
    m_recorderConfig.recording.rateControl = RateControlMode::CQP;
    m_recorderConfig.recording.qualityLevel = 20;  // 高画质
    m_recorderConfig.recording.preset = "p6";      // Better Quality
    m_recorderConfig.recording.format = "mp4";
    // 默认保存路径
    QString moviePath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    m_recorderConfig.recording.pathOrUrl = moviePath + "/output.mp4";

    // [Audio]
    m_recorderConfig.audio.sampleRate = 44100;
    m_recorderConfig.audio.bitrate = 160000;  // 160kbps
    m_recorderConfig.audio.micVolume = 1.0f;
    m_recorderConfig.audio.sysVolume = 1.0f;

    // [Network]
    m_recorderConfig.network.tcpNoDelay = true;
    m_recorderConfig.network.autoReconnect = true;

    // [New] UI 面板默认状态
    m_recorderConfig.showViewerPanel = false;
    m_recorderConfig.showMessagePanel = false;

    // [FIX] 修改 AI 默认配置
    m_recorderConfig.ai.enable = false;
    m_recorderConfig.ai.sampleFps = 1;
    // 0: SeparateFiles, 1: SingleStream, 2: SplitBySecond
    m_recorderConfig.ai.storageMode = 2;  // 默认改为 "按秒分片"
    m_recorderConfig.ai.saveDirName = "ai_data";
    m_recorderConfig.ai.audioSplitDuration = 1;

    // 加载用户配置
    loadConfig();
}

QColor AppConfig::parseColor(QString const& hexStr, QColor const& defaultColor)
{
    if (QColor::isValidColor(hexStr))
        return QColor(hexStr);
    return defaultColor;
}

void AppConfig::loadConfig()
{
    QString configPath = QCoreApplication::applicationDirPath() + "/config.json";
    QFile file(configPath);

    if (file.open(QIODevice::ReadOnly))
    {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isObject())
        {
            QJsonObject obj = doc.object();

            // UI Layout
            if (obj.contains("cardCount"))
                m_cardCount = obj["cardCount"].toInt();
            if (obj.contains("spacing"))
                m_cardSpacing = obj["spacing"].toInt();
            // ... (省略部分 UI 简单参数，逻辑同前) ...
            if (obj.contains("windowWidth"))
                m_windowDefaultSize.setWidth(obj["windowWidth"].toInt());
            if (obj.contains("windowHeight"))
                m_windowDefaultSize.setHeight(obj["windowHeight"].toInt());

            // 采集参数
            if (obj.contains("camDefW"))
                m_cameraDefaultResolution.setWidth(obj["camDefW"].toInt());
            if (obj.contains("camDefH"))
                m_cameraDefaultResolution.setHeight(obj["camDefH"].toInt());
            if (obj.contains("poolCleanupMs"))
                m_texturePoolCleanupMs = obj["poolCleanupMs"].toInt();

            // [New] UI Panel Settings
            if (obj.contains("showViewerPanel"))
                m_recorderConfig.showViewerPanel = obj["showViewerPanel"].toBool();
            if (obj.contains("showMessagePanel"))
                m_recorderConfig.showMessagePanel = obj["showMessagePanel"].toBool();

            // --- 核心 Recorder 解析 ---
            if (obj.contains("videoBase"))
                parseVideoBase(obj["videoBase"].toObject());
            if (obj.contains("streaming"))
                parseStreaming(obj["streaming"].toObject());
            if (obj.contains("recording"))
                parseRecording(obj["recording"].toObject());
            if (obj.contains("audio"))
                parseAudio(obj["audio"].toObject());
            if (obj.contains("ai"))
                parseAI(obj["ai"].toObject());

            LOG_INFO("Config loaded successfully from " << configPath);
        }
        file.close();
    }
    else
    {
        saveConfig();  // 生成默认配置
    }
}

void AppConfig::parseVideoBase(QJsonObject const& root)
{
    if (root.isEmpty())
        return;
    auto& c = m_recorderConfig.base;
    if (root.contains("baseW"))
        c.baseResolution.setWidth(root["baseW"].toInt(1920));
    if (root.contains("baseH"))
        c.baseResolution.setHeight(root["baseH"].toInt(1080));
    if (root.contains("outW"))
        c.outputResolution.setWidth(root["outW"].toInt(1280));
    if (root.contains("outH"))
        c.outputResolution.setHeight(root["outH"].toInt(720));
    if (root.contains("fps"))
        c.fps = root["fps"].toDouble(30.0);
    if (root.contains("gpu"))
        c.adapterIndex = root["gpu"].toInt(0);

    QString filter = root["filter"].toString("lanczos").toLower();
    if (filter == "bilinear")
        c.downscaleFilter = DownscaleFilter::Bilinear;
    else if (filter == "bicubic")
        c.downscaleFilter = DownscaleFilter::Bicubic;
    else
        c.downscaleFilter = DownscaleFilter::Lanczos;

    // [P2] 解析色彩配置
    QString space = root["colorSpace"].toString("709").toLower();
    if (space == "601")
        c.colorSpace = ColorSpace::Rec601;
    else
        c.colorSpace = ColorSpace::Rec709;

    QString range = root["colorRange"].toString("limited").toLower();
    if (range == "full")
        c.colorRange = ColorRange::Full;
    else
        c.colorRange = ColorRange::Limited;
}

void AppConfig::parseStreaming(QJsonObject const& root)
{
    if (root.isEmpty())
        return;
    auto& c = m_recorderConfig.streaming;
    if (root.contains("enable"))
        c.enable = root["enable"].toBool();
    if (root.contains("encoder"))
        c.encoderName = root["encoder"].toString("h264_nvenc");
    if (root.contains("bitrate"))
        c.bitrateKbps = root["bitrate"].toInt(4500);
    if (root.contains("gop"))
        c.keyframeIntervalSec = root["gop"].toInt(2);
    if (root.contains("preset"))
        c.preset = root["preset"].toString("p4");
    // 默认为 CBR
    c.rateControl = RateControlMode::CBR;
}

void AppConfig::parseRecording(QJsonObject const& root)
{
    if (root.isEmpty())
        return;
    auto& c = m_recorderConfig.recording;
    if (root.contains("enable"))
        c.enable = root["enable"].toBool();
    if (root.contains("encoder"))
        c.encoderName = root["encoder"].toString("hevc_nvenc");
    if (root.contains("cq"))
        c.qualityLevel = root["cq"].toInt(20);
    if (root.contains("path"))
        c.pathOrUrl = root["path"].toString();
    if (root.contains("keepFile"))
        c.keepFile = root["keepFile"].toBool();

    QString rc = root["rc"].toString("cqp").toLower();
    if (rc == "vbr")
        c.rateControl = RateControlMode::VBR;
    else if (rc == "cbr")
        c.rateControl = RateControlMode::CBR;
    else
        c.rateControl = RateControlMode::CQP;
}

void AppConfig::parseAudio(QJsonObject const& root)
{
    if (root.isEmpty())
        return;
    auto& c = m_recorderConfig.audio;
    if (root.contains("micVol"))
        c.micVolume = (float)root["micVol"].toDouble(1.0);
    if (root.contains("sysVol"))
        c.sysVolume = (float)root["sysVol"].toDouble(1.0);
    if (root.contains("bitrate"))
        c.bitrate = root["bitrate"].toInt(128000);
}

QJsonObject AppConfig::serializeAI() const
{
    QJsonObject obj;
    auto& c = m_recorderConfig.ai;
    obj["enable"] = c.enable;
    obj["fps"] = c.sampleFps;
    obj["merge"] = c.storageMode;
    obj["dir"] = c.saveDirName;
    obj["audioDuration"] = c.audioSplitDuration;
    return obj;
}

void AppConfig::parseAI(QJsonObject const& root)
{
    if (root.isEmpty())
        return;
    auto& c = m_recorderConfig.ai;
    if (root.contains("enable"))
        c.enable = root["enable"].toBool();
    if (root.contains("fps"))
        c.sampleFps = root["fps"].toInt(1);
    if (root.contains("storageMode"))
    {
        c.storageMode = root["storageMode"].toInt(2);
    }
    else if (root.contains("merge"))
    {
        // 兼容旧配置：如果旧的 merge 为 true，对应模式 1 (SingleStream)
        bool merge = root["merge"].toBool();
        c.storageMode = merge ? 1 : 2;
    }
    else
    {
        c.storageMode = 2;
    }
    if (root.contains("dir"))
        c.saveDirName = root["dir"].toString("ai_data");
    if (root.contains("audioDuration"))
    {
        c.audioSplitDuration = root["audioDuration"].toInt(1);
    }
}

void AppConfig::saveConfig()
{
    QString configPath = QCoreApplication::applicationDirPath() + "/config.json";
    QFile file(configPath);
    if (file.open(QIODevice::WriteOnly))
    {
        QJsonObject obj;

        // UI
        obj["cardCount"] = m_cardCount;
        obj["spacing"] = m_cardSpacing;
        obj["windowWidth"] = m_windowDefaultSize.width();
        obj["windowHeight"] = m_windowDefaultSize.height();

        // [New] UI Panel Settings
        obj["showViewerPanel"] = m_recorderConfig.showViewerPanel;
        obj["showMessagePanel"] = m_recorderConfig.showMessagePanel;

        // Core
        obj["camDefW"] = m_cameraDefaultResolution.width();
        obj["camDefH"] = m_cameraDefaultResolution.height();
        obj["poolCleanupMs"] = m_texturePoolCleanupMs;

        // Nested Configs
        obj["videoBase"] = serializeVideoBase();
        obj["streaming"] = serializeTranscode(m_recorderConfig.streaming);
        obj["recording"] = serializeTranscode(m_recorderConfig.recording);
        obj["audio"] = serializeAudio();
        obj["ai"] = serializeAI();
        file.write(QJsonDocument(obj).toJson());
        file.close();
        LOG_INFO("Config saved to " << configPath);

        // [New] 通知 UI 刷新
        emit configChanged();
    }
}

QJsonObject AppConfig::serializeVideoBase() const
{
    QJsonObject obj;
    auto& c = m_recorderConfig.base;
    obj["baseW"] = c.baseResolution.width();
    obj["baseH"] = c.baseResolution.height();
    obj["outW"] = c.outputResolution.width();
    obj["outH"] = c.outputResolution.height();
    obj["fps"] = c.fps;
    obj["gpu"] = c.adapterIndex;

    QString filter = "lanczos";
    if (c.downscaleFilter == DownscaleFilter::Bilinear)
        filter = "bilinear";
    else if (c.downscaleFilter == DownscaleFilter::Bicubic)
        filter = "bicubic";
    obj["filter"] = filter;

    // [P2] 保存色彩配置
    obj["colorSpace"] = (c.colorSpace == ColorSpace::Rec601) ? "601" : "709";
    obj["colorRange"] = (c.colorRange == ColorRange::Full) ? "full" : "limited";

    return obj;
}

QJsonObject AppConfig::serializeTranscode(TranscodeConfig const& cfg) const
{
    QJsonObject obj;
    obj["enable"] = cfg.enable;
    obj["keepFile"] = cfg.keepFile;
    obj["encoder"] = cfg.encoderName;
    obj["bitrate"] = cfg.bitrateKbps;
    obj["gop"] = cfg.keyframeIntervalSec;
    obj["preset"] = cfg.preset;
    obj["targets"] = QJsonArray();
    // Recording 模式下仍使用单路径
    obj["path"] = cfg.pathOrUrl;
    if (cfg.rateControl == RateControlMode::CBR)
        obj["rc"] = "cbr";
    else if (cfg.rateControl == RateControlMode::VBR)
        obj["rc"] = "vbr";
    else
    {
        obj["rc"] = "cqp";
        obj["cq"] = cfg.qualityLevel;
    }
    return obj;
}

QJsonObject AppConfig::serializeAudio() const
{
    QJsonObject obj;
    obj["micVol"] = m_recorderConfig.audio.micVolume;
    obj["sysVol"] = m_recorderConfig.audio.sysVolume;
    obj["bitrate"] = m_recorderConfig.audio.bitrate;
    return obj;
}

// Getters & Setters
RecorderConfig AppConfig::getRecorderConfig() const
{
    return m_recorderConfig;
}

void AppConfig::setVideoBase(VideoBaseConfig const& cfg)
{
    m_recorderConfig.base = cfg;
}
void AppConfig::setStreaming(TranscodeConfig const& cfg)
{
    m_recorderConfig.streaming = cfg;
    qInfo() << "[MEM_AUDIT] AppConfig::m_recorderConfig.streaming updated.";
    qInfo() << "[MEM_AUDIT] pushTargets size:" << m_recorderConfig.streaming.pushTargets.size();
    qInfo() << "[MEM_AUDIT] pathOrUrl value:" << m_recorderConfig.streaming.pathOrUrl;

    // [重要建议]：如果这里被其他地方的 loadConfig() 覆盖了，云端数据就丢了
    // 如果你有 saveConfig，不要在这里调用，除非你确定要存本地
}

void AppConfig::setRecording(TranscodeConfig const& cfg)
{
    m_recorderConfig.recording = cfg;
}
void AppConfig::setAudio(AudioConfig const& cfg)
{
    m_recorderConfig.audio = cfg;
}

void AppConfig::setAIConfig(AIConfig const& cfg)
{
    m_recorderConfig.ai = cfg;
    // 可以在这里加日志验证
    qInfo() << "[AppConfig] AI Config Updated. Enable:" << cfg.enable
            << " Merge:" << cfg.storageMode;
}

void AppConfig::resetStreamingToEmpty()
{
    m_recorderConfig.streaming.pushTargets.clear();
    m_recorderConfig.streaming.pathOrUrl = "";
    m_recorderConfig.streaming.streamKey = "";
    qInfo() << "[MEM_AUDIT] AppConfig Streaming has been FORCED to EMPTY.";
    emit configChanged();
}

void AppConfig::resetStreamingTargets(QList<PushTarget> const& newTargets)
{
    // This correctly replaces the list. If newTargets is empty, m_recorderConfig... is empty.
    m_recorderConfig.streaming.pushTargets = newTargets;
    m_recorderConfig.streaming.pathOrUrl = "";

    qInfo() << "[MEM_AUDIT] FORCED SYNC. New Targets Count:"
            << m_recorderConfig.streaming.pushTargets.size();

    // Ensure UI update
    QTimer::singleShot(0, this, [this]() { emit configChanged(); });
}

// Old UI Getters (Same as before)
int AppConfig::cardCount() const
{
    return m_cardCount;
}
int AppConfig::cardSpacing() const
{
    return m_cardSpacing;
}
int AppConfig::sideMargin() const
{
    return m_sideMargin;
}
QSize AppConfig::baseCardSize() const
{
    return m_baseCardSize;
}
int AppConfig::topBarHeight() const
{
    return m_topBarHeight;
}
int AppConfig::titleHeight() const
{
    return m_titleHeight;
}
QSize AppConfig::windowDefaultSize() const
{
    return m_windowDefaultSize;
}
int AppConfig::snapThreshold() const
{
    return m_snapThreshold;
}
int AppConfig::resizeHandleSize() const
{
    return m_resizeHandleSize;
}
bool AppConfig::enableSnap() const
{
    return m_enableSnap;
}
bool AppConfig::hideUIOnInteract() const
{
    return m_hideUIOnInteract;
}
bool AppConfig::enableBoundary() const
{
    return m_enableBoundary;
}
QColor AppConfig::themeColor() const
{
    return m_themeColor;
}
QColor AppConfig::handleColor() const
{
    return m_handleColor;
}
QColor AppConfig::snapLineColor() const
{
    return m_snapLineColor;
}
QColor AppConfig::canvasBgColor() const
{
    return m_canvasBgColor;
}

// Camera / Pool Getters
QSize AppConfig::cameraDefaultResolution() const
{
    return m_cameraDefaultResolution;
}
int AppConfig::cameraRetryIntervalMs() const
{
    return m_cameraRetryIntervalMs;
}
QSize AppConfig::wgcFallbackResolution() const
{
    return m_wgcFallbackResolution;
}
int AppConfig::texturePoolCleanupMs() const
{
    return m_texturePoolCleanupMs;
}
int AppConfig::ringBufferSize() const
{
    return m_ringBufferSize;
}

double AppConfig::cardAspectRatio() const
{
    if (m_baseCardSize.height() == 0)
        return 0.647;
    return (double)m_baseCardSize.width() / (double)m_baseCardSize.height();
}
