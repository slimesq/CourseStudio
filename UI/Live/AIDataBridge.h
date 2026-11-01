#pragma once

#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <QString>
#include <QDateTime>
#include <QDir>
#include <QDebug>

extern "C"
{
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/audio_fifo.h>
}

enum class AIStorageMode
{
    SeparateFiles,  // 策略1: 单帧独立
    SingleStream,   // 策略2: 全局合并
    SplitBySecond   // 策略3: 按秒分片
};

// 配置结构体
struct AIBridgeConfig
{
    QString saveDir;               // 保存目录
    int videoIntervalMs = 1000;    // 视频采样间隔 (默认1秒存一帧)
    int targetSampleRate = 16000;  // AI常用音频采样率
    int targetChannels = 1;        // AI常用单声道
    AIStorageMode storageMode = AIStorageMode::SeparateFiles;
    int audioSplitDuration = 1;
    long long roomId = 0;
};

class AIDataBridge
{
public:
    AIDataBridge();
    ~AIDataBridge();

    // --- 热插拔控制 ---
    void start(AIBridgeConfig const& config);
    void stop();

    // --- 数据输入 (非阻塞，极速返回) ---
    // data: 通常是 NV12 (来自 Camera/WGC)
    void pushVideo(
        std::vector<uint8_t> const& data, int width, int height, int format, int64_t pts);

    // data: 通常是 Float32 (来自 Wasapi)
    void pushAudio(
        char const* data, int len, int sampleRate, int channels, int format, int64_t pts);

    using UploadCallback = std::function<void(QByteArray const&, QString const&, int)>;
    void setUploadCallback(UploadCallback cb)
    {
        m_uploadCallback = cb;
    }

private:
    // [Fix] 将结构体定义移到前面，确保函数声明时类型已知
    struct VideoTask
    {
        std::vector<uint8_t> data;
        int width;
        int height;
        int format;  // AVPixelFormat
        int64_t pts;
    };

    struct AudioTask
    {
        std::vector<char> data;
        int sampleRate;
        int channels;
        int format;  // AVSampleFormat
        int64_t pts;
    };

    void workerLoop();

    void flushAudioFifo();

    // [Fix] 现在可以直接使用 VideoTask&，不需要 struct 关键字
    void processVideo(VideoTask& task);
    void processAudio(AudioTask& task);

    // FFmpeg 资源管理
    bool initSws(int srcW, int srcH, int srcFmt, int dstW, int dstH, int dstFmt);
    bool initSwr(int srcRate, int srcCh, int srcFmt, int dstRate, int dstCh, int dstFmt);

private:
    std::atomic<bool> m_running{false};
    std::thread m_workerThread;
    std::mutex m_mutex;
    std::condition_variable m_cond;

    // 数据队列
    std::queue<VideoTask> m_videoQueue;
    std::queue<AudioTask> m_audioQueue;

    // 配置
    AIBridgeConfig m_config;
    int64_t m_lastVideoSaveTime = 0;

    // FFmpeg Contexts (复用以提升性能)
    SwsContext* m_swsCtx = nullptr;
    SwrContext* m_swrCtx = nullptr;

    // 缓存参数用于检测是否需要重新初始化
    int m_lastSrcW = 0, m_lastSrcH = 0, m_lastSrcFmt = -1;
    int m_lastASrcRate = 0, m_lastASrcCh = 0, m_lastASrcFmt = -1;

    // 音频转换中间缓存
    uint8_t** m_audioOutBuffer = nullptr;
    int m_audioOutLinesize = 0;
    int m_audioMaxDstNbSamples = 0;

    int64_t m_currentSecondBasePts = 0;  // 当前这一秒的起始 PTS
    QString m_currentSecondFileName;     // 当前这一秒的文件名

    // [New] 音频 FIFO 缓冲区
    AVAudioFifo* m_audioFifo = nullptr;

    // [New] 每一片的目标样本数 (例如 16000 * 3 = 48000)
    int m_audioChunkSamples = 0;

    // [New] 用于计算切片文件名的时间戳
    int64_t m_currentAudioBasePts = 0;
    UploadCallback m_uploadCallback;
};
