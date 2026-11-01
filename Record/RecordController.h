#ifndef RECORDCONTROLLER_H
#define RECORDCONTROLLER_H

#include <QObject>
#include <QSettings>
#include <QDir>
#include <QDateTime>
#include <memory>
#include <vector>
#include "FFmpegRecorder.h"
#include "RecorderConfig.h"
#include "DX11Context.h"

// RecordController 现在作为一个纯粹的控制器，
// 负责协调 FFmpegRecorder，不再直接持有 Compositor 或 Capturer。
class RecordController : public QObject
{
    Q_OBJECT
public:
    // [Fix 1] 构造函数改为只接受 parent，适配 ScreenWindow::ScreenWindow 中的调用 new RecordController(this)
    explicit RecordController(QObject* parent = nullptr) : QObject(parent)
    {
        m_recorder = new FFmpegRecorder(this);
        // 确保 Recorder 获取到 DX11 设备句柄
        m_recorder->setD3DDevice(DX11Context::instance().device());
    }

    // [Fix 2] startTask 改为返回 bool，适配 ScreenWindow::onLiveButtonClicked 中的 bool started = ...
    bool startTask(RecorderConfig const& config)
    {
        if (!m_recorder)
            return false;

        // 设置初始音量 (根据配置)
        m_recorder->setAudioVolume(AUDIO_SOURCE_MIC, config.audio.micVolume);
        m_recorder->setAudioVolume(AUDIO_SOURCE_SYSTEM, config.audio.sysVolume);

        return m_recorder->startRecord(config);
    }

    FFmpegRecorder* getRecorder() const
    {
        return m_recorder;
    }

    void stopTask()
    {
        if (m_recorder)
            m_recorder->stopRecord();
    }

    bool isRecording() const
    {
        return m_recorder && m_recorder->isRunning();
    }

    // 获取时长字符串 (用于 UI 显示)
    QString getDurationString()
    {
        if (!m_recorder)
            return "00:00";
        qint64 ms = m_recorder->getRecordingDuration();
        qint64 seconds = ms / 1000;
        qint64 m = seconds / 60;
        qint64 s = seconds % 60;
        return QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    }

    void setMicVolume(float volume)
    {
        if (m_recorder)
            m_recorder->setAudioVolume(AUDIO_SOURCE_MIC, volume);
    }

    // [Fix 3] 添加 addVideoData 接口，供 ScreenWindow::onYuvFrameReceived 调用
    void addVideoData(std::vector<uint8_t> const& data, int width, int height, int64_t pts)
    {
        if (m_recorder)
        {
            m_recorder->pushNV12Frame(data, width, height, pts);
        }
    }

    // [Fix 4] 添加 addAudioData 接口，供 ScreenWindow::onAudioDataReceived 调用
    void addAudioData(QByteArray const& data, int sampleRate, int channels)
    {
        // 注意：由于 ScreenWindow 目前将麦克风和系统声音都发到同一个槽函数，
        // 这里暂时统一推送到 SYSTEM 轨道。
        // 如果需要区分，ScreenWindow 端需要区分调用，或者在这里增加 sourceId 参数。
        if (m_recorder)
        {
            // 默认推送到系统音频轨道，混音由 FFmpegRecorder 内部处理
            m_recorder->pushAudioData(AUDIO_SOURCE_SYSTEM, data, sampleRate, channels);
        }
    }

private:
    FFmpegRecorder* m_recorder;
};

#endif  // RECORDCONTROLLER_H
