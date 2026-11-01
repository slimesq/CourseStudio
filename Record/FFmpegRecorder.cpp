// FFmpegRecorder.cpp
#include "FFmpegRecorder.h"
#include "FFmpegModules.h"
#include <QDebug>
#include <QDateTime>
#include <QCoreApplication>
#include <algorithm>
#include "DX11Context.h"
#include <QFileInfo>

// 辅助日志宏
#define LOG_REC(msg) qDebug() << "[RECORDER] " << msg
#define LOG_SYNC(msg) qDebug() << "[AV_SYNC] " << msg
#define LOG_ABR(msg) qDebug() << "[ABR] " << msg

FFmpegRecorder::FFmpegRecorder(QObject* parent) : QThread(parent)
{
    m_isRecording = false;
    m_softStopRequested = false;
    this->moveToThread(this);
    initNetwork();
}

FFmpegRecorder::~FFmpegRecorder()
{
    stopRecord();
    wait();
    cleanUp();
}

void FFmpegRecorder::initNetwork()
{
    avformat_network_init();
}

void FFmpegRecorder::setD3DDevice(void* device)
{
    if (device)
    {
        m_d3dDevice = static_cast<ID3D11Device*>(device);
        m_d3dDevice->GetImmediateContext(&m_d3dContext);
    }
}

void FFmpegRecorder::updateAudioConfig(bool mixMic, bool mixSystem)
{
    QMutexLocker l(&m_configMutex);
    m_config.mixMicrophone = mixMic;
    m_config.mixSystemAudio = mixSystem;
    if (m_audioDSP)
    {
        m_audioDSP->setSourceEnabled(AUDIO_SOURCE_MIC, mixMic);
        m_audioDSP->setSourceEnabled(AUDIO_SOURCE_SYSTEM, mixSystem);
    }
}

void FFmpegRecorder::setAudioVolume(int sourceId, float volume)
{
    QMutexLocker l(&m_configMutex);
    if (sourceId == AUDIO_SOURCE_MIC)
        m_config.audio.micVolume = volume;
    else if (sourceId == AUDIO_SOURCE_SYSTEM)
        m_config.audio.sysVolume = volume;

    if (m_audioDSP)
    {
        m_audioDSP->setSourceVolume(sourceId, volume);
    }
}

void FFmpegRecorder::setSavePath(QString const& path)
{
    QMutexLocker l(&m_configMutex);
    m_nextSavePath = path;
}

bool FFmpegRecorder::startRecord(RecorderConfig const& config)
{
    if (m_isRecording)
        return false;

    m_currentSplitIndex = 0;
    m_lastSplitTime = QDateTime::currentMSecsSinceEpoch();

    // [CRITICAL FIX] 自动补全 D3D 设备，解决 "Hardware: No" 问题
    // 如果外部没有通过 setD3DDevice 设置，则尝试从单例获取
    if (!m_d3dDevice)
    {
        setD3DDevice(DX11Context::instance().device());
        qInfo() << "[RECORDER] Auto-detected D3D11 Device from Context.";
    }

    m_config = config;
    if (!m_nextSavePath.isEmpty())
    {
        m_config.recording.pathOrUrl = m_nextSavePath;
        m_nextSavePath.clear();
    }
    m_isRecording = true;
    m_softStopRequested = false;

    m_videoPtsCounter = 0;
    m_totalAudioSamples = 0;
    m_currentDuration = 0;
    m_droppedFrames = 0;
    m_duplicatedFrames = 0;

    int w = m_config.base.outputResolution.width();
    int h = m_config.base.outputResolution.height();
    int bitrate = m_config.streaming.bitrateKbps;

    qInfo() << "[REC_START] Resolution:" << w << "x" << h << " Bitrate:" << bitrate << "kbps";

    // [Fix] 初始码率设置：如果推流开启，用推流码率；否则用录制码率
    int initialBitrate = 0;
    if (m_config.streaming.enable)
        initialBitrate = m_config.streaming.bitrateKbps * 1000;
    else
        initialBitrate = m_config.recording.bitrateKbps * 1000;
    m_currentBitrate = initialBitrate;

    m_lastStatsTime = QDateTime::currentMSecsSinceEpoch();
    m_lastBytesSent = 0;
    m_lastFrameCount = 0;

    m_videoQueue.clear();
    start();
    emit stateChanged(RecorderState::Recording);
    return true;
}

void FFmpegRecorder::stopRecord()
{
    if (!m_isRecording)
        return;
    m_softStopRequested = true;

    if (this->isRunning())
    {
        // Wait up to 2 seconds for soft stop, then force terminate (or just wait)
        if (!this->wait(2000))
        {
            qWarning() << "[RECORDER] Thread did not stop in time, forcing wait...";
            this->wait();
        }
    }

    // Ensure flags are reset
    m_isRecording = false;
    m_softStopRequested = false;
}

qint64 FFmpegRecorder::getRecordingDuration() const
{
    return m_currentDuration.load();
}

void FFmpegRecorder::pushVideoFrame(QImage const& img, qint64 timestamp)
{
    // 暂略
}

void FFmpegRecorder::pushNV12Frame(std::vector<uint8_t> const& data,
                                   int width,
                                   int height,
                                   qint64 pts)
{
    if (!m_isRecording || m_softStopRequested)
        return;
    VideoFrameData frame;
    frame.isRaw = true;
    frame.rawData = data;
    frame.width = width;
    frame.height = height;
    frame.debug_arrival_time = pts;
    QMutexLocker l(&m_videoMutex);
    m_videoQueue.enqueue(frame);
}

void FFmpegRecorder::pushTextureFrame(void* texture, int width, int height, qint64 pts)
{
    if (!m_isRecording || m_softStopRequested || !texture)
        return;
    VideoFrameData frame;
    frame.isRaw = true;
    frame.texture = static_cast<ID3D11Texture2D*>(texture);
    frame.width = width;
    frame.height = height;
    frame.debug_arrival_time = pts;
    QMutexLocker l(&m_videoMutex);
    m_videoQueue.enqueue(frame);
}

void FFmpegRecorder::pushAudioData(int sourceId,
                                   QByteArray const& data,
                                   int sampleRate,
                                   int channels)
{
    if (!m_isRecording || !m_audioDSP)
        return;
    m_audioDSP->pushData(sourceId, data, sampleRate, channels);
}

void FFmpegRecorder::cleanUp()
{
    m_sender.reset();
    m_vEncoder.reset();
    m_aEncoder.reset();
    m_audioDSP.reset();

    if (m_swsCtx)
    {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }
    if (m_scaledFrame)
    {
        av_frame_free(&m_scaledFrame);
        m_scaledFrame = nullptr;
    }

    if (m_mixL)
    {
        delete[] m_mixL;
        m_mixL = nullptr;
    }
    if (m_mixR)
    {
        delete[] m_mixR;
        m_mixR = nullptr;
    }
    if (m_aFrame)
    {
        av_frame_free(&m_aFrame);
        m_aFrame = nullptr;
    }
    if (m_lastEncodedFrame)
    {
        av_frame_free(&m_lastEncodedFrame);
        m_lastEncodedFrame = nullptr;
    }

    m_stagingTexture.Reset();
    m_d3dContext.Reset();
    m_d3dDevice.Reset();
}

void FFmpegRecorder::run()
{
    // =======================================================
    // [阶段 1] 初始化自适应控制器 & 强制低延迟配置
    // =======================================================
    if (m_adaptiveCtrl)
    {
        delete m_adaptiveCtrl;
        m_adaptiveCtrl = nullptr;
    }
    m_adaptiveCtrl = new AdaptiveStreamController(this);
    // 设定码率范围：500kbps ~ 4000kbps，基准帧率 30fps (或从 config 读取)
    int baseFps = m_config.base.fps > 0 ? m_config.base.fps : 30;
    m_adaptiveCtrl->setConstraints(500000, 4000000, baseFps);

    // 强制低延迟参数 (仅在推流时生效)
    TranscodeConfig const* activeTranscodeConf = &m_config.recording;
    if (m_config.streaming.enable)
    {
        m_config.streaming.tune = "zerolatency";
        m_config.streaming.profile = "baseline";
        m_config.streaming.preset = "superfast";
        m_config.streaming.gop = baseFps;  // 1秒GOP
        m_config.streaming.max_b_frames = 0;
        activeTranscodeConf = &m_config.streaming;
    }

    // =======================================================
    // [阶段 2] 编码器初始化 (保持原有逻辑)
    // =======================================================
    m_vEncoder = std::make_unique<VideoEncoder>();

    if (!m_vEncoder->init(m_config.base, *activeTranscodeConf, m_d3dDevice.Get()))
    {
        emit errorOccurred("Video Encoder Init Failed");
        emit stateChanged(RecorderState::Stopped);
        return;
    }

    m_aEncoder = std::make_unique<AudioEncoder>();
    if (!m_aEncoder->init(m_config.audio))
    {
        emit errorOccurred("Audio Encoder Init Failed");
        emit stateChanged(RecorderState::Stopped);
        return;
    }

    // =======================================================
    // [阶段 3] Muxer 初始化 (含去重逻辑)
    // =======================================================
    std::vector<std::shared_ptr<BaseMuxer>> muxers;

    // 1. 本地录制
    if (m_config.recording.enable && !m_config.recording.pathOrUrl.isEmpty())
    {
        auto muxer = std::make_shared<FileMuxer>();
        if (muxer->init(m_config.recording.pathOrUrl, m_config))
            muxers.push_back(muxer);
    }

    // 2. 网络推流 (去重)
    if (m_config.streaming.enable)
    {
        QList<QString> addedUrls;  // 去重列表

        // 优先使用新列表
        if (!m_config.streaming.pushTargets.isEmpty())
        {
            for (auto const& target : m_config.streaming.pushTargets)
            {
                if (target.enable && !target.url.isEmpty())
                {
                    QString fullUrl = target.url;
                    if (!target.key.isEmpty())
                    {
                        if (!fullUrl.endsWith("/") && !target.key.startsWith("/"))
                            fullUrl += "/";
                        fullUrl += target.key;
                    }

                    // [Fix] 去重
                    if (addedUrls.contains(fullUrl))
                    {
                        LOG_REC("Duplicate push target ignored: " << fullUrl.toStdString());
                        continue;
                    }
                    addedUrls.append(fullUrl);

                    auto muxer = std::make_shared<StreamMuxer>();
                    if (muxer->init(fullUrl, m_config))
                    {
                        muxers.push_back(muxer);
                        LOG_REC("Added push target: " << fullUrl.toStdString());
                    }
                    else
                    {
                        LOG_REC("Failed to init push target: " << fullUrl.toStdString());
                    }
                }
            }
        }
        // 兼容旧配置
        else if (!m_config.streaming.pathOrUrl.isEmpty())
        {
            auto muxer = std::make_shared<StreamMuxer>();
            QString fullUrl = m_config.streaming.pathOrUrl;
            if (!m_config.streaming.streamKey.isEmpty())
                fullUrl += "/" + m_config.streaming.streamKey;

            if (muxer->init(fullUrl, m_config))
            {
                muxers.push_back(muxer);
                LOG_REC("Added legacy push target: " << fullUrl.toStdString());
            }
        }
    }

    // 空跑保护
    if (muxers.empty())
    {
        emit errorOccurred("No Valid Output Targets (Stream or Record)");
        emit stateChanged(RecorderState::Stopped);
        return;
    }

    // 写头信息
    for (auto& muxer : muxers)
    {
        muxer->setStreams(muxer->addStream(m_vEncoder->getContext()),
                          muxer->addStream(m_aEncoder->getContext()));
        if (avformat_write_header(muxer->getContext(), nullptr) < 0)
        {
            LOG_REC("Warning: Failed to write header for muxer.");
        }
    }

    // =======================================================
    // [阶段 4] 启动发送器与音频DSP
    // =======================================================
    m_sender = std::make_unique<StreamSender>();
    m_sender->setMuxers(muxers);
    m_sender->start();

    m_audioDSP = std::make_unique<AudioDSP>();
    m_audioDSP->init(m_config);
    m_audioDSP->setSourceEnabled(AUDIO_SOURCE_MIC, m_config.mixMicrophone);
    m_audioDSP->setSourceEnabled(AUDIO_SOURCE_SYSTEM, m_config.mixSystemAudio);
    m_audioDSP->setSourceVolume(AUDIO_SOURCE_MIC, m_config.audio.micVolume);
    m_audioDSP->setSourceVolume(AUDIO_SOURCE_SYSTEM, m_config.audio.sysVolume);

    m_mixBufferSize = m_config.audio.sampleRate * 2;
    m_mixL = new float[m_mixBufferSize];
    m_mixR = new float[m_mixBufferSize];

    m_aFrame = av_frame_alloc();
    m_aFrame->nb_samples = m_aEncoder->getContext()->frame_size;
    m_aFrame->format = m_aEncoder->getContext()->sample_fmt;
    av_channel_layout_copy(&m_aFrame->ch_layout, &m_aEncoder->getContext()->ch_layout);
    m_aFrame->sample_rate = m_aEncoder->getContext()->sample_rate;
    av_frame_get_buffer(m_aFrame, 0);

    m_audioDSP->clean();
    LOG_REC("Started.");

    // =======================================================
    // [阶段 5] 主循环 (集成自适应控制)
    // =======================================================

    m_frameIdx = 0;  // 重置帧计数

    while (true)
    {
        // 1. 退出条件检查
        if (m_softStopRequested)
        {
            QMutexLocker l(&m_videoMutex);
            if (m_videoQueue.isEmpty())
                break;
        }

        updateStats();

        // 2. 网络健康检查 (保留你原有的逻辑)
        if (++m_healthCheckCounter > 30)
        {
            m_healthCheckCounter = 0;
            checkNetworkHealth();
        }

        // 3. 从队列取视频帧
        VideoFrameData vFrame;
        bool hasFrame = false;
        {
            QMutexLocker l(&m_videoMutex);
            if (!m_videoQueue.isEmpty())
            {
                vFrame = m_videoQueue.dequeue();
                hasFrame = true;
            }
        }

        // 4. 处理逻辑 (含丢帧控制)
        if (hasFrame)
        {
            // [New] 获取当前自适应策略
            StreamConfig strategy = m_adaptiveCtrl->getCurrentConfig();

            // [New] 动态调整码率
            AVCodecContext* vCtx = m_vEncoder->getContext();
            if (vCtx && abs((int)vCtx->bit_rate - strategy.targetBitrate) > 50000)
            {
                vCtx->bit_rate = strategy.targetBitrate;
                vCtx->rc_max_rate = strategy.targetBitrate;
                vCtx->rc_buffer_size = strategy.targetBitrate / 8;
                LOG_REC("[Adaptive] Adjust Bitrate -> " << strategy.targetBitrate / 1000
                                                        << "kbps | Buf: " << vCtx->rc_buffer_size);
            }

            // [New] 丢帧判断
            bool shouldDrop = false;
            m_frameIdx++;

            // 根据网络质量丢帧
            if (strategy.quality == NetworkQuality::Poor)
            {
                if (m_frameIdx % 3 == 0)
                    shouldDrop = true;  // 30 -> 20 fps
            }
            else if (strategy.quality == NetworkQuality::Critical)
            {
                if (m_frameIdx % 2 == 0)
                    shouldDrop = true;  // 30 -> 15 fps
            }

            if (shouldDrop)
            {
                // [丢帧]：直接丢弃视频帧，不调用 processVideo
                // 但必须继续往下走，去调用 processAudio，保证音频不中断！
                // LOG_REC("[Adaptive] Dropped Frame " << m_frameIdx);
            }
            else
            {
                // [正常]：处理视频 (编码+入队发送)

                // 1. 准备统计数据
                QElapsedTimer netTimer;
                netTimer.start();  // 开始计时

                // 2. 计算真实时间戳 (PTS)
                // [关键] 使用 m_elapsedTimer (真实时间) 修正时间戳
                // 将这个时间写入 vFrame.debug_arrival_time，
                // 并在 VideoEncoder 内部或 processVideo 中使用它来设置 PTS
                qint64 nowUs = m_elapsedTimer.nsecsElapsed() / 1000;  // 微秒
                vFrame.debug_arrival_time = nowUs;

                // 3. 执行视频处理
                // [Fix] 解决编译错误 C2664:
                // 不直接调用 m_vEncoder->encode(vFrame)，而是调用 processVideo
                // processVideo 内部负责将 VideoFrameData 转换为 AVFrame* 并处理编码和发送
                processVideo(vFrame);

                // 4. 上报统计数据 (用于自适应流控)
                // 注意：如果 StreamSender 的队列满了（网络拥塞），processVideo 会阻塞，
                // 此时 netTimer 能捕获到长耗时，从而触发 AdaptiveStreamController 降码率
                qint64 duration = netTimer.elapsed();

                // 估算本帧大小 (用于带宽统计，因为无法从 processVideo 获取确切大小)
                // 估算公式：目标码率(bps) / 8 / FPS
                int estimatedBytes = (m_adaptiveCtrl->getCurrentConfig().targetBitrate) / 8 / 30;

                m_adaptiveCtrl->reportSentStats(estimatedBytes, duration, 0);
            }

            // 无论视频是否被丢弃，音频都要处理！
            processAudio(false);
            emit frameProcessed();
        }
        else
        {
            // 队列无帧，仅处理音频
            processAudio(false);
            QThread::msleep(1);
        }
    }

    // =======================================================
    // [阶段 6] 停止与清理 (保持原有逻辑)
    // =======================================================
    LOG_REC("Stopping...");
    processAudio(true);  // 处理剩余音频

    // Flush Video Encoder
    while (true)
    {
        AVPacket* pkt = m_vEncoder->flush();
        if (!pkt)
            break;
        emitPacket(pkt, true);
        av_packet_free(&pkt);
    }

    // Flush Audio Encoder
    while (true)
    {
        AVPacket* pkt = m_aEncoder->flush();
        if (!pkt)
            break;
        emitPacket(pkt, false);
        av_packet_free(&pkt);
    }

    // 补帧逻辑 (音画对齐)
    if (m_lastEncodedFrame)
    {
        double videoDur = (double)m_videoPtsCounter / m_config.base.fps;
        double audioDur = (double)m_totalAudioSamples / m_config.audio.sampleRate;
        while (videoDur < audioDur)
        {
            m_lastEncodedFrame->pts = m_videoPtsCounter;
            AVPacket* pkt = m_vEncoder->encode(m_lastEncodedFrame);
            if (pkt)
            {
                if (pkt->pts == AV_NOPTS_VALUE)
                    pkt->pts = m_videoPtsCounter;
                if (pkt->dts == AV_NOPTS_VALUE)
                    pkt->dts = m_videoPtsCounter;
                emitPacket(pkt, true);
                av_packet_free(&pkt);
            }
            m_videoPtsCounter++;
            videoDur = (double)m_videoPtsCounter / m_config.base.fps;
        }
    }

    m_sender->stop();
    m_isRecording = false;

    // 清理控制器
    if (m_adaptiveCtrl)
    {
        delete m_adaptiveCtrl;
        m_adaptiveCtrl = nullptr;
    }

    LOG_REC("Finalized.");
    emit stateChanged(RecorderState::Stopped);
}

void FFmpegRecorder::updateStats()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastStatsTime >= 1000)
    {
        uint64_t totalBytes = m_sender ? m_sender->getTotalBytesSent() : 0;
        int64_t diffBytes = totalBytes - m_lastBytesSent;
        // int64_t diffFrames = m_videoPtsCounter - m_lastFrameCount; // Unused variable warning fix

        double currentBitrateKbps =
            (double)diffBytes * 8.0 / 1000.0 / (now - m_lastStatsTime) * 1000.0;

        StreamStats stats;
        stats.fps =
            (double)(m_videoPtsCounter - m_lastFrameCount) * 1000.0 / (now - m_lastStatsTime);
        stats.bitrateKbps = currentBitrateKbps;
        stats.droppedFrames = (int)m_droppedFrames;
        stats.cacheQueueSize = m_sender ? m_sender->getQueueSize() : 0;
        stats.totalBytes = totalBytes;

        // [New] 增加调试日志，观察高分辨率下的实际码率和队列堆积情况
        if (m_config.streaming.enable)
        {
            // 每5秒打印一次详细健康日志，或者当出现异常时打印
            static int logCounter = 0;
            if (stats.cacheQueueSize > 5 || ++logCounter % 5 == 0)
            {
                qDebug() << "[REC_STATS] Res:" << m_config.base.outputResolution.width() << "x"
                         << m_config.base.outputResolution.height()
                         << "| Target Bitrate:" << (m_currentBitrate / 1000) << "k"
                         << "| Real Bitrate:" << (int)currentBitrateKbps << "k"
                         << "| Queue:" << stats.cacheQueueSize << "| Drop:" << stats.droppedFrames;
            }
        }

        double videoTime = (double)m_videoPtsCounter / m_config.base.fps;
        double audioTime = (double)m_totalAudioSamples / m_config.audio.sampleRate;
        stats.avSyncDiffMs = (int)((audioTime - videoTime) * 1000);

        emit statsUpdated(stats);

        m_lastStatsTime = now;
        m_lastBytesSent = totalBytes;
        m_lastFrameCount = m_videoPtsCounter;
    }
}

void FFmpegRecorder::updateEncoderBitrate(int bitrate)
{
    if (!m_vEncoder || !m_vEncoder->getContext())
        return;

    AVCodecContext* c = m_vEncoder->getContext();

    // 如果目标码率与当前相差超过 10%，则调整
    if (abs(c->bit_rate - bitrate) > (c->bit_rate * 0.1))
    {
        c->bit_rate = bitrate;
        c->rc_max_rate = bitrate;
        c->rc_buffer_size = bitrate / 2;  // buffer size 设为码率的一半，控制延迟
        c->rc_min_rate = bitrate / 2;

        // 这里的 0 表示重置 buffer
        // 注意：并非所有编码器都支持 reconfig，x264 是支持的
        // 如果是硬编码 (nvenc)，通常支持改变 bit_rate

        LOG_REC("[Adaptive] Reconfiguring Encoder Bitrate to: " << bitrate);
    }
}

void FFmpegRecorder::checkFileSplit(bool isKeyFrame, int64_t currentVideoPts)
{
    // 1. 检查配置是否开启
    if (!m_config.recording.enable || !m_config.recording.autoFileSplit)
        return;

    // 2. 必须是关键帧才能切
    if (!isKeyFrame)
        return;

    // 3. 检查时间间隔
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 intervalMs = m_config.recording.fileSplitDurationMin * 60 * 1000;

    if (now - m_lastSplitTime > intervalMs)
    {
        // 触发分片！

        // 生成新文件名
        // 假设原路径是 C:/Videos/record.mp4
        // 新路径改为 C:/Videos/record_001.mp4, record_002.mp4 ...

        // 获取原始基础路径 (在 startRecord 时保存一份原始路径 m_originalPath)
        QFileInfo fileInfo(m_config.recording.pathOrUrl);
        QString baseName = fileInfo.completeBaseName();  // "record"
        // 如果文件名本身已经带了 _001，需要剔除吗？
        // 简单策略：如果用户设定 record.mp4，第一段就是 record.mp4
        // 第二段变成 record_1.mp4，第三段 record_2.mp4

        m_currentSplitIndex++;

        // 这里的逻辑可以根据你的需求定制命名规则
        QString newFileName = QString("%1/%2_%3.%4")
                                  .arg(fileInfo.absolutePath())
                                  .arg(baseName)
                                  .arg(m_currentSplitIndex, 3, 10, QChar('0'))  // record_001
                                  .arg(fileInfo.suffix());

        qInfo() << "[RECORDER] Auto Splitting file to:" << newFileName;

        // 计算音频当前的 PTS (估算)
        // 注意：StreamSender 需要的是基于 TimeBase 的 PTS。
        // Video PTS 已经传入。Audio PTS 我们用 m_totalAudioSamples 转换。
        int64_t currentAudioPts = av_rescale_q(m_totalAudioSamples,
                                               {1, m_config.audio.sampleRate},
                                               m_aEncoder->getContext()->time_base);

        // 通知 Sender
        m_sender->scheduleSplit(newFileName, currentVideoPts, currentAudioPts);

        m_lastSplitTime = now;
    }
}

void FFmpegRecorder::emitPacket(AVPacket* pkt, bool isVideo)
{
    if (!m_sender)
        return;
    AVRational tb =
        isVideo ? m_vEncoder->getContext()->time_base : m_aEncoder->getContext()->time_base;
    m_sender->pushPacket(pkt, tb, isVideo);
}

void FFmpegRecorder::checkNetworkHealth()
{
    if (!m_sender || !m_vEncoder)
        return;
    if (!m_config.streaming.enable)
        return;

    int queueSize = m_sender->getQueueSize();

    // [调优] 放宽丢帧阈值，高分辨率下包可能会更多
    int const THRESHOLD_DROP = 30;        // 原 30 -> 100 (给予更多缓冲时间)
    int const THRESHOLD_CONGESTION = 20;  // 原 10 -> 20

    int const BITRATE_MIN = 500000;
    int targetBitrate = m_config.streaming.bitrateKbps * 1000;

    // 1. 严重拥塞处理
    if (queueSize > THRESHOLD_DROP)
    {
        LOG_ABR("CRITICAL CONGESTION (Queue=" << queueSize << "). Smart dropping frames!");

        int dropped = m_sender->smartDrop();

        if (dropped > 0)
        {
            int64_t panicBitrate = m_currentBitrate * 0.7;
            if (panicBitrate < BITRATE_MIN)
                panicBitrate = BITRATE_MIN;

            if (panicBitrate != m_currentBitrate)
            {
                m_currentBitrate = panicBitrate;
                m_vEncoder->setBitrate(m_currentBitrate);
                LOG_ABR("Panic bitrate reduction to " << (m_currentBitrate / 1000) << " kbps");
            }
        }
    }
    // 2. 轻微拥塞处理
    else if (queueSize > THRESHOLD_CONGESTION)
    {
        int64_t newBitrate = m_currentBitrate * 0.95;
        if (newBitrate < BITRATE_MIN)
            newBitrate = BITRATE_MIN;

        if (newBitrate != m_currentBitrate)
        {
            m_currentBitrate = newBitrate;
            m_vEncoder->setBitrate(m_currentBitrate);
            LOG_ABR("Congestion detected. Reducing to " << (m_currentBitrate / 1000) << " kbps");
        }
    }
    // 3. 恢复逻辑 (保持不变)
    else if (queueSize == 0)
    {
        if (m_currentBitrate < targetBitrate)
        {
            int64_t newBitrate = m_currentBitrate * 1.02;
            if (newBitrate > targetBitrate)
                newBitrate = targetBitrate;
            m_currentBitrate = newBitrate;
            m_vEncoder->setBitrate(m_currentBitrate);
        }
    }
}

bool FFmpegRecorder::downloadTexture(ID3D11Texture2D* tex,
                                     std::vector<uint8_t>& outData,
                                     int width,
                                     int height)
{
    if (!m_d3dDevice || !m_d3dContext)
        return false;

    if (!m_stagingTexture)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        tex->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        if (FAILED(m_d3dDevice->CreateTexture2D(&desc, nullptr, &m_stagingTexture)))
            return false;
    }

    m_d3dContext->CopyResource(m_stagingTexture.Get(), tex);
    D3D11_MAPPED_SUBRESOURCE map;
    if (FAILED(m_d3dContext->Map(m_stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &map)))
        return false;

    int ySize = width * height;
    int uvSize = width * height / 2;
    outData.resize(ySize + uvSize);

    uint8_t* src = (uint8_t*)map.pData;
    uint8_t* dstY = outData.data();
    uint8_t* dstUV = outData.data() + ySize;

    for (int i = 0; i < height; ++i)
    {
        memcpy(dstY + i * width, src + i * map.RowPitch, width);
    }
    for (int i = 0; i < height / 2; ++i)
    {
        memcpy(dstUV + i * width, src + ySize + i * map.RowPitch, width);
    }

    m_d3dContext->Unmap(m_stagingTexture.Get(), 0);
    return true;
}

bool FFmpegRecorder::initScaler(int inW, int inH, int inFmt, int outW, int outH, int outFmt)
{
    // 如果上下文已存在且参数未变，直接复用（sws_getCachedContext 会自动处理，但手动检查更可控）
    m_swsCtx = sws_getCachedContext(m_swsCtx,
                                    inW,
                                    inH,
                                    (AVPixelFormat)inFmt,
                                    outW,
                                    outH,
                                    (AVPixelFormat)outFmt,
                                    SWS_BICUBIC,
                                    nullptr,
                                    nullptr,
                                    nullptr);

    if (!m_swsCtx)
    {
        qCritical() << "[RECORDER] Failed to init SwsContext";
        return false;
    }

    // 准备缩放后的 AVFrame 容器
    if (!m_scaledFrame)
    {
        m_scaledFrame = av_frame_alloc();
    }
    // 如果分辨率或格式变了，重新分配 buffer
    if (m_scaledFrame->width != outW || m_scaledFrame->height != outH ||
        m_scaledFrame->format != outFmt)
    {
        av_frame_unref(m_scaledFrame);
        m_scaledFrame->format = outFmt;
        m_scaledFrame->width = outW;
        m_scaledFrame->height = outH;
        if (av_frame_get_buffer(m_scaledFrame, 32) < 0)
        {
            qCritical() << "[RECORDER] Failed to alloc scaled frame buffer";
            return false;
        }
    }
    return true;
}

void FFmpegRecorder::processVideo(VideoFrameData const& data)
{
    if (!data.isRaw)
        return;
    if (!m_vEncoder || !m_vEncoder->getContext())
        return;

    // 1. 获取编码器期望的目标分辨率
    AVCodecContext* encCtx = m_vEncoder->getContext();
    int targetW = encCtx->width;
    int targetH = encCtx->height;

    // 2. 判断是否需要缩放
    bool needScale = (data.width != targetW || data.height != targetH);

    // 如果是硬件编码且不需要缩放，尝试走纯 GPU 路径
    bool canUseHW = (data.texture && m_vEncoder->isHardware() && !needScale);

    AVFrame* frameToEncode = nullptr;
    AVFrame* srcFrame = av_frame_alloc();  // 临时持有输入数据

    // ---------------------------------------------------------
    // 步骤 A: 准备源数据 (srcFrame)
    // ---------------------------------------------------------
    if (canUseHW)
    {
        // 纯 GPU 路径：直接绑定 Texture
        srcFrame->format = AV_PIX_FMT_D3D11;
        srcFrame->width = data.width;
        srcFrame->height = data.height;
        srcFrame->data[0] = (uint8_t*)data.texture.Get();
        srcFrame->data[1] = (uint8_t*)0;
        srcFrame->hw_frames_ctx = av_buffer_ref(m_vEncoder->getHWFramesRef());
        frameToEncode = srcFrame;
    }
    else
    {
        // CPU 路径 (需要下载或已经是 RawData)
        srcFrame->format = AV_PIX_FMT_NV12;
        srcFrame->width = data.width;
        srcFrame->height = data.height;

        bool downloadSuccess = false;
        if (data.texture)
        {
            // 需要回读 GPU 数据的情况：
            // 1. 编码器是软件编码器 (libx264)
            // 2. 需要缩放 (sws_scale 只能处理 CPU 内存)
            std::vector<uint8_t> fallbackData;
            if (downloadTexture(data.texture.Get(), fallbackData, data.width, data.height))
            {
                av_image_fill_arrays(srcFrame->data,
                                     srcFrame->linesize,
                                     fallbackData.data(),
                                     AV_PIX_FMT_NV12,
                                     data.width,
                                     data.height,
                                     1);
                downloadSuccess = true;
            }
        }
        else
        {
            // 本身就是 CPU 数据
            av_image_fill_arrays(srcFrame->data,
                                 srcFrame->linesize,
                                 data.rawData.data(),
                                 AV_PIX_FMT_NV12,
                                 data.width,
                                 data.height,
                                 1);
            downloadSuccess = true;
        }

        if (!downloadSuccess)
        {
            av_frame_free(&srcFrame);
            return;  // 数据无效丢弃
        }

        // ---------------------------------------------------------
        // 步骤 B: 执行缩放 (如果需要)
        // ---------------------------------------------------------
        if (needScale)
        {
            // 初始化缩放器： NV12 -> NV12
            if (initScaler(
                    data.width, data.height, AV_PIX_FMT_NV12, targetW, targetH, AV_PIX_FMT_NV12))
            {
                // 执行缩放
                sws_scale(m_swsCtx,
                          srcFrame->data,
                          srcFrame->linesize,
                          0,
                          data.height,
                          m_scaledFrame->data,
                          m_scaledFrame->linesize);

                // 此时 frameToEncode 指向已缩放的帧
                frameToEncode = m_scaledFrame;

                // 重要：拷贝时间戳等属性
                frameToEncode->pts = srcFrame->pts;
                frameToEncode->color_range = srcFrame->color_range;
            }
            else
            {
                // 缩放初始化失败，强制丢帧防止崩溃
                av_frame_free(&srcFrame);
                return;
            }
        }
        else
        {
            // 不需要缩放，直接使用源数据
            frameToEncode = srcFrame;
        }
    }

    // ---------------------------------------------------------
    // 步骤 C: 处理丢帧/补帧逻辑 (基于 frameToEncode)
    // ---------------------------------------------------------
    bool dropFrame = false;
    int duplicateCount = 0;
    bool isAudioActive =
        (m_config.mixMicrophone || m_config.mixSystemAudio) && (m_totalAudioSamples > 0);

    if (isAudioActive)
    {
        double audioSec = (double)m_totalAudioSamples / m_config.audio.sampleRate;
        double videoSec = (double)m_videoPtsCounter / m_config.base.fps;
        double frameDur = 1.0 / m_config.base.fps;
        double diff = audioSec - videoSec;

        if (diff > frameDur)
        {
            duplicateCount = (int)(diff / frameDur);
            if (duplicateCount > 5)
                duplicateCount = 5;
            m_duplicatedFrames += duplicateCount;
        }
        else if (diff < -frameDur)
        {
            dropFrame = true;
            m_droppedFrames++;
        }
    }

    if (dropFrame)
    {
        // 如果 srcFrame 没有被 frameToEncode 引用（即发生了缩放），srcFrame 总是需要释放
        // 如果 frameToEncode == srcFrame，最后统一释放 srcFrame 即可
        av_frame_free(&srcFrame);
        return;
    }

    // ---------------------------------------------------------
    // 步骤 D: 编码
    // ---------------------------------------------------------

    // 处理补帧
    for (int i = 0; i < duplicateCount; ++i)
    {
        frameToEncode->pts = m_videoPtsCounter;
        encodeAndWriteVideo(frameToEncode);
        m_videoPtsCounter++;
    }

    // 处理当前帧
    frameToEncode->pts = m_videoPtsCounter;

    // 更新 LastEncodedFrame 用于停止时的补尾
    if (m_lastEncodedFrame)
        av_frame_free(&m_lastEncodedFrame);

    if (canUseHW)
    {
        // 纯硬件帧需要 clone 引用
        m_lastEncodedFrame = av_frame_clone(frameToEncode);
    }
    else
    {
        // 软件帧 (或缩放后的软件帧) 分配新内存并拷贝
        m_lastEncodedFrame = av_frame_alloc();
        m_lastEncodedFrame->format = AV_PIX_FMT_NV12;
        m_lastEncodedFrame->width = frameToEncode->width;
        m_lastEncodedFrame->height = frameToEncode->height;
        av_frame_get_buffer(m_lastEncodedFrame, 32);
        av_frame_copy(m_lastEncodedFrame, frameToEncode);
    }

    encodeAndWriteVideo(frameToEncode);

    // 更新计数器
    m_videoPtsCounter++;
    m_currentDuration.store(m_videoPtsCounter * 1000 / m_config.base.fps);

    // 释放源帧 (注意：m_scaledFrame 是复用的成员变量，不需要释放，只需释放 srcFrame)
    av_frame_free(&srcFrame);
}

void FFmpegRecorder::encodeAndWriteVideo(AVFrame* frame)
{
    AVPacket* pkt = m_vEncoder->encode(frame);
    if (pkt)
    {
        if (pkt->pts == AV_NOPTS_VALUE)
            pkt->pts = frame->pts;
        if (pkt->dts == AV_NOPTS_VALUE)
            pkt->dts = frame->pts;

        // [New] 分片检测逻辑
        bool isKeyFrame = (pkt->flags & AV_PKT_FLAG_KEY);
        checkFileSplit(isKeyFrame, pkt->pts);

        emitPacket(pkt, true);
        av_packet_free(&pkt);
    }
}

void FFmpegRecorder::processAudio(bool isFadingOut)
{
    int frameSize = m_aEncoder->getContext()->frame_size;

    while (true)
    {
        int samplesRead = m_audioDSP->popMixedData(m_mixL, m_mixR, frameSize);
        if (samplesRead < frameSize)
        {
            if (!isFadingOut)
                break;
            if (samplesRead == 0)
                break;
            for (int i = samplesRead; i < frameSize; ++i)
            {
                m_mixL[i] = 0;
                m_mixR[i] = 0;
            }
        }

        if (av_frame_make_writable(m_aFrame) < 0)
            return;

        float* dstL = (float*)m_aFrame->data[0];
        float* dstR = (float*)m_aFrame->data[1];
        memcpy(dstL, m_mixL, frameSize * sizeof(float));
        memcpy(dstR, m_mixR, frameSize * sizeof(float));

        int64_t ptsOffset = av_rescale_q(
            m_config.audio.globalSyncOffsetMs, {1, 1000}, m_aEncoder->getContext()->time_base);
        m_aFrame->pts = m_totalAudioSamples + ptsOffset;

        m_totalAudioSamples += frameSize;

        AVPacket* pkt = m_aEncoder->encode(m_aFrame);
        if (pkt)
        {
            emitPacket(pkt, false);
            av_packet_free(&pkt);
        }
    }
}
