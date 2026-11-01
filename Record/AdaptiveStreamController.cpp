#include "AdaptiveStreamController.h"
#include <QDebug>
#include <algorithm>
#include <QDateTime>

AdaptiveStreamController::AdaptiveStreamController(QObject* parent) : QObject(parent)
{
    m_currentBitrate = 2500000;  // 默认 2.5 Mbps
}

void AdaptiveStreamController::setConstraints(int minBitrate, int maxBitrate, int baseFps)
{
    m_minBitrate = minBitrate;
    m_maxBitrate = maxBitrate;
    m_baseFps = baseFps;
    m_currentBitrate = std::clamp(m_currentBitrate, m_minBitrate, m_maxBitrate);
}

void AdaptiveStreamController::reportSentStats(int bytesSent, qint64 durationMs, int bufferSize)
{
    QMutexLocker lock(&m_mutex);

    // 指数加权移动平均 (EWMA) 计算平均延迟，平滑抖动
    double const alpha = 0.3;
    m_avgLatency = alpha * durationMs + (1.0 - alpha) * m_avgLatency;
    m_currentBufferSize = bufferSize;

    // --- 核心算法：判断拥塞 ---
    // 判据1: socket 发送阻塞超过 100ms (说明带宽满了)
    // 判据2: 平均延迟持续升高
    bool isCongested = (durationMs > 200) || (m_avgLatency > 150);

    adjustBitrate(isCongested);
    determineQualityLevel();
}

void AdaptiveStreamController::adjustBitrate(bool isCongested)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 限制调整频率，避免震荡 (至少间隔 1秒 调一次，除非严重拥塞)
    if (now - m_lastAdjustmentTime < 1000 && !isCongested)
        return;

    if (isCongested)
    {
        // [乘法减小] Multiplicative Decrease
        m_consecutiveCongestionCount++;
        m_consecutiveGoodCount = 0;

        // 拥塞越严重，降得越快
        float dropFactor = (m_consecutiveCongestionCount > 2) ? 0.5f : 0.7f;
        m_currentBitrate = static_cast<int>(m_currentBitrate * dropFactor);

        qWarning() << "[Adaptive] Network Congestion! Dropping Bitrate to:"
                   << m_currentBitrate / 1000 << "kbps";
        m_lastAdjustmentTime = now;
    }
    else
    {
        // [加法增大] Additive Increase
        m_consecutiveCongestionCount = 0;
        m_consecutiveGoodCount++;

        // 网络稳定 3秒后才开始试探性增加
        if (m_consecutiveGoodCount > 3)
        {
            m_currentBitrate += 50000;  // 每次加 50kbps
            m_lastAdjustmentTime = now;
        }
    }

    // 钳制范围
    m_currentBitrate = std::clamp(m_currentBitrate, m_minBitrate, m_maxBitrate);
}

void AdaptiveStreamController::determineQualityLevel()
{
    // 根据当前码率决定 QoS 等级，进而决定 FPS
    if (m_currentBitrate >= 2500000)
    {
        m_currentQuality = NetworkQuality::Excellent;
    }
    else if (m_currentBitrate >= 1500000)
    {
        m_currentQuality = NetworkQuality::Good;
    }
    else if (m_currentBitrate >= 800000)
    {
        m_currentQuality = NetworkQuality::Poor;
    }
    else
    {
        m_currentQuality = NetworkQuality::Critical;
    }
}

StreamConfig AdaptiveStreamController::getCurrentConfig()
{
    QMutexLocker lock(&m_mutex);
    StreamConfig config;
    config.targetBitrate = m_currentBitrate;
    config.quality = m_currentQuality;

    // 根据等级计算目标 FPS 和是否丢帧
    switch (m_currentQuality)
    {
        case NetworkQuality::Excellent:
        case NetworkQuality::Good:
            config.targetFps = m_baseFps;
            config.shouldDropFrame = false;
            break;
        case NetworkQuality::Poor:
            config.targetFps = m_baseFps * 2 / 3;  // 30 -> 20 fps
            // 简单的丢帧策略：通过时间戳取模或计数器在外部控制
            config.shouldDropFrame = false;  // 具体丢帧由外部循环控制
            break;
        case NetworkQuality::Critical:
            config.targetFps = m_baseFps / 2;  // 30 -> 15 fps
            config.shouldDropFrame = false;
            break;
    }
    return config;
}
