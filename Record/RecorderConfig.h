#ifndef RECORDERCONFIG_H
#define RECORDERCONFIG_H

#include <QString>
#include <QSize>
#include <QMap>
#include <QList>  // [New] Added for QList

// --- 音频源 ID ---
constexpr int AUDIO_SOURCE_SYSTEM = 0;
constexpr int AUDIO_SOURCE_MIC = 1;

// --- 枚举定义 ---

// [P2] 色彩空间标准
enum class ColorSpace
{
    Rec601,  // SDTV (常用)
    Rec709   // HDTV (推荐)
};

// [P2] 色彩范围
enum class ColorRange
{
    Limited,  // 16-235 (电视标准, 默认)
    Full      // 0-255 (PC标准)
};

// [P1] 缩放算法
enum class DownscaleFilter
{
    Bilinear,  // 线性插值 (快)
    Bicubic,   // 三次卷积 (清晰)
    Lanczos    // 兰索斯 (锐利, 推荐)
};

// [P1] 码率控制模式
enum class RateControlMode
{
    CBR,  // 恒定码率
    VBR,  // 可变码率
    CQP   // 恒定质量
};

// [P5] 推流器状态枚举
enum class RecorderState
{
    Stopped,
    Recording,
    Paused,
    Reconnecting
};

// [P5] 实时统计数据
struct StreamStats
{
    double fps = 0.0;
    double bitrateKbps = 0.0;
    int droppedFrames = 0;
    int cacheQueueSize = 0;
    uint64_t totalBytes = 0;
    int avSyncDiffMs = 0;

    QString toString() const
    {
        return QString("FPS: %1 | Bitrate: %2 kbps | Drop: %3 | Queue: %4")
            .arg(fps, 0, 'f', 1)
            .arg(bitrateKbps, 0, 'f', 0)
            .arg(droppedFrames)
            .arg(cacheQueueSize);
    }
};

// --- 核心配置结构 ---

// 1. 基础视频架构 (渲染层)
struct VideoBaseConfig
{
    QSize baseResolution = {1920, 1080};   // 画布原始分辨率 (Canvas)
    QSize outputResolution = {1280, 720};  // 最终编码分辨率 (Output)
    double fps = 30.0;

    // [P2] 画质增强参数
    DownscaleFilter downscaleFilter = DownscaleFilter::Lanczos;
    ColorSpace colorSpace = ColorSpace::Rec709;
    ColorRange colorRange = ColorRange::Limited;

    // [P3] GPU 选择
    int adapterIndex = 0;
};

// [New] 推流目标结构体
struct PushTarget
{
    QString url;         // 推流服务器地址 (rtmp://...)
    QString key;         // 推流码
    bool enable = true;  // 是否启用
};

// 2. 通用转码/编码配置
struct TranscodeConfig
{
    bool enable = false;
    QString encoderName = "h264_nvenc";
    bool hardwareFallback = true;
    RateControlMode rateControl = RateControlMode::CBR;
    int bitrateKbps = 6000;
    int maxBitrateKbps = 8000;
    int qualityLevel = 23;
    int keyframeIntervalSec = 1;
    int bFrames = 0;
    QString preset = "veryfast";
    QString profile = "baseline";
    QString format = "flv";
    QString tune = "zerolatency";  // 核心：关闭 B 帧，优化切片发送
    int max_b_frames = 0;          // 双重保险：禁止 B 帧
    int gop = 30;

    // [Deprecated] 旧的单一地址字段，保留以兼容未迁移代码，后续阶段将逻辑迁移到 pushTargets
    QString pathOrUrl = "";
    QString streamKey = "";

    // [New] 多路推流目标列表
    QList<PushTarget> pushTargets;

    QMap<QString, QString> extraOptions;

    bool autoFileSplit = false;     // 是否启用自动分片
    int fileSplitDurationMin = 60;  // 分片间隔 (分钟)
    bool keepFile = false;
};

// 3. 音频配置
struct AudioConfig
{
    int sampleRate = 44100;
    int channels = 2;
    int bitrate = 128000;
    QString codecName = "aac";
    float micVolume = 1.0f;
    float sysVolume = 1.0f;
    int globalSyncOffsetMs = 0;
    bool enableSoftLimiter = true;
    int trackMode = 1;
};

// 4. 网络传输配置
struct NetworkConfig
{
    int rwTimeout = 3000000;
    int connectTimeout = 5000000;
    bool tcpNoDelay = true;
    int sendBufferSize = 64 * 1024;
    bool autoReconnect = true;
    int retryCount = 10;
    int retryIntervalMs = 2000;
};

// [New] AI 分析配置
struct AIConfig
{
    bool enable = false;  // 全局开关
    int sampleFps = 1;    // 采样率 (帧/秒)
    int storageMode;
    QString saveDirName = "ai_data";  // 保存目录名
    int audioSplitDuration = 1;
};

// --- 总配置对象 ---
struct RecorderConfig
{
    VideoBaseConfig base;
    TranscodeConfig streaming;
    TranscodeConfig recording;
    AudioConfig audio;
    NetworkConfig network;

    AIConfig ai;

    bool mixSystemAudio = true;
    bool mixMicrophone = true;

    // [New] UI 面板状态控制
    bool showViewerPanel = false;
    bool showMessagePanel = false;
};

#endif  // RECORDERCONFIG_H
