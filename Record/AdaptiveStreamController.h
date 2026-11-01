#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QMutex>

// 定义网络质量等级
enum class NetworkQuality
{
    Excellent,
    Good,
    Poor,
    Critical
};

// 输出的控制指令
struct StreamConfig
{
    int targetBitrate;     // 目标码率 (bps)
    int targetFps;         // 目标帧率 (用于计算丢帧)
    bool shouldDropFrame;  // 当前帧是否应该丢弃
    NetworkQuality quality;
};

class AdaptiveStreamController : public QObject
{
    Q_OBJECT
public:
    explicit AdaptiveStreamController(QObject* parent = nullptr);

    // 设置初始配置
    void setConstraints(int minBitrate, int maxBitrate, int baseFps);

    // 核心输入：汇报一次发送耗时 (用于评估网络)
    // bytesSent: 发送字节数
    // durationMs: 发送耗时
    // bufferSize: 当前 ffmpeg 内部 buffer 大小 (如果有)
    void reportSentStats(int bytesSent, qint64 durationMs, int bufferSize);

    // 核心输出：获取当前的推流策略
    StreamConfig getCurrentConfig();

private:
    void adjustBitrate(bool isCongested);
    void determineQualityLevel();

private:
    int m_minBitrate = 500000;   // 500 kbps
    int m_maxBitrate = 4000000;  // 4 Mbps
    int m_currentBitrate;
    int m_baseFps = 30;

    // AIMD 算法状态
    int m_consecutiveCongestionCount = 0;
    int m_consecutiveGoodCount = 0;
    qint64 m_lastAdjustmentTime = 0;

    // 网络指标
    double m_avgLatency = 0;
    int m_currentBufferSize = 0;

    NetworkQuality m_currentQuality = NetworkQuality::Good;
    QMutex m_mutex;
};
