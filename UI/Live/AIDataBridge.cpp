#include "AIDataBridge.h"
#include <QFile>

// Set to 0 to enable Cloud Upload, 1 to enable Local Save
#define DEBUG_SAVE_LOCAL 0

QString getFileName(QString const& fullPath)
{
    return QFileInfo(fullPath).fileName();
}

AIDataBridge::AIDataBridge()
{
}

AIDataBridge::~AIDataBridge()
{
    stop();
}

void AIDataBridge::start(AIBridgeConfig const& config)
{
    if (m_running)
        return;

    m_config = config;

    // 创建目录
    QDir dir;
    if (!dir.exists(m_config.saveDir))
    {
        dir.mkpath(m_config.saveDir);
    }

    // [New] 计算每个切片需要的样本数
    // 例如: 16000 Hz * 2s = 32000 samples
    int splitSec = (config.audioSplitDuration > 0) ? config.audioSplitDuration : 1;
    m_audioChunkSamples = config.targetSampleRate * splitSec;

    // [New] 初始化 FIFO
    // 目标格式固定为: 16k, Mono, S16LE (S16 = 2 bytes)
    // 初始大小给 1秒的缓冲即可，它会自动扩容
    if (m_audioFifo)
        av_audio_fifo_free(m_audioFifo);
    m_audioFifo =
        av_audio_fifo_alloc(AV_SAMPLE_FMT_S16, config.targetChannels, m_audioChunkSamples);

    m_running = true;
    m_workerThread = std::thread(&AIDataBridge::workerLoop, this);
    qInfo() << "[AIDataBridge] Service Started. Dir:" << config.saveDir;
}

void AIDataBridge::stop()
{
    if (!m_running)
        return;
    m_running = false;
    m_cond.notify_all();
    if (m_workerThread.joinable())
    {
        m_workerThread.join();
    }

    // [New] 清理 FIFO
    if (m_audioFifo)
    {
        av_audio_fifo_free(m_audioFifo);
        m_audioFifo = nullptr;
    }

    // 清理 FFmpeg 资源
    if (m_swsCtx)
    {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }
    if (m_swrCtx)
    {
        swr_free(&m_swrCtx);
        m_swrCtx = nullptr;
    }
    if (m_audioOutBuffer)
    {
        av_freep(&m_audioOutBuffer[0]);
        av_freep(&m_audioOutBuffer);
    }

    // 清空队列
    std::lock_guard<std::mutex> lock(m_mutex);
    std::queue<VideoTask> emptyV;
    std::swap(m_videoQueue, emptyV);
    std::queue<AudioTask> emptyA;
    std::swap(m_audioQueue, emptyA);

    qInfo() << "[AIDataBridge] Service Stopped.";
}

void AIDataBridge::pushVideo(
    std::vector<uint8_t> const& data, int width, int height, int format, int64_t pts)
{
    if (!m_running)
        return;

    // [策略] 采样控制：如果距离上次保存时间不足 Interval，直接丢弃，不进队列，节省内存
    if (pts - m_lastVideoSaveTime < m_config.videoIntervalMs)
    {
        return;
    }
    m_lastVideoSaveTime = pts;

    std::lock_guard<std::mutex> lock(m_mutex);
    // [策略] 队列防爆：如果堆积超过 5 帧，丢弃最旧的
    if (m_videoQueue.size() > 5)
    {
        m_videoQueue.pop();
    }

    VideoTask task;
    task.data = data;  // 内存拷贝 (不可避免，保证线程安全)
    task.width = width;
    task.height = height;
    task.format = format;  // AV_PIX_FMT_NV12
    task.pts = pts;

    m_videoQueue.push(task);
    m_cond.notify_one();
}

void AIDataBridge::pushAudio(
    char const* data, int len, int sampleRate, int channels, int format, int64_t pts)
{
    if (!m_running)
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    // 音频一般不丢帧，除非堆积极其严重
    if (m_audioQueue.size() > 100)
    {
        m_audioQueue.pop();  // 紧急丢弃
    }

    AudioTask task;
    task.data.assign(data, data + len);
    task.sampleRate = sampleRate;
    task.channels = channels;
    task.format = format;  // AV_SAMPLE_FMT_FLT
    task.pts = pts;

    m_audioQueue.push(task);
    m_cond.notify_one();
}

void AIDataBridge::workerLoop()
{
    while (m_running)
    {
        VideoTask vTask;
        AudioTask aTask;
        bool hasVideo = false;
        bool hasAudio = false;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cond.wait(lock, [this] {
                return !m_running || !m_videoQueue.empty() || !m_audioQueue.empty();
            });

            if (!m_running)
                break;

            if (!m_videoQueue.empty())
            {
                vTask = m_videoQueue.front();
                m_videoQueue.pop();
                hasVideo = true;
            }
            if (!m_audioQueue.empty())
            {
                aTask = m_audioQueue.front();
                m_audioQueue.pop();
                hasAudio = true;
            }
        }

        // 释放锁后处理，避免阻塞 Push
        if (hasVideo)
            processVideo(vTask);
        if (hasAudio)
            processAudio(aTask);
    }
}

void AIDataBridge::flushAudioFifo()
{
    if (!m_audioFifo)
        return;

    // 循环检查：只要 FIFO 里的数据够一个 chunk，就切出来写入文件
    while (av_audio_fifo_size(m_audioFifo) >= m_audioChunkSamples)
    {
        // 1. 准备临时 buffer 读取数据
        // S16LE = 2 bytes per sample
        int bufferSize = m_audioChunkSamples * m_config.targetChannels * 2;
        std::vector<uint8_t> fileBuffer(bufferSize);

        // ffmpeg API 需要 void** 指针
        void* dataPtr = fileBuffer.data();

        // 2. 从 FIFO 头部读取指定数量的样本 (这步会将数据移出 FIFO)
        int readCount = av_audio_fifo_read(m_audioFifo, &dataPtr, m_audioChunkSamples);

        if (readCount != m_audioChunkSamples)
        {
            qWarning() << "[AIDataBridge] FIFO read error. Expected:" << m_audioChunkSamples
                       << " Got:" << readCount;
            break;
        }

        // 3. 生成文件名 (使用 pts)
        // 注意：因为是连续的，下一片的时间 = 当前基准 + 时长
        // 第一次使用 task.pts，后续累加

        QString filename = QString("%1/audio_%2_%3s.pcm")
                               .arg(m_config.saveDir)
                               .arg(m_currentAudioBasePts)  // 使用当前块的起始时间
                               .arg(m_audioChunkSamples / m_config.targetSampleRate);
        QString simpleName = getFileName(filename);
#if DEBUG_SAVE_LOCAL
        // 4. 写入文件 (覆盖写模式 wb，因为是独立切片)
        FILE* f = fopen(filename.toLocal8Bit().constData(), "wb");
        if (f)
        {
            fwrite(fileBuffer.data(), 1, bufferSize, f);
            fclose(f);
            // [调试模式] 仅保留文件，不上传
            qDebug() << "[AIDataBridge] [DEBUG] Saved Audio:" << filename;
        }
#else
        // === 云端上传模式 === \
    // 1. 构造 QByteArray (避免深度拷贝，使用 setRawData 需要小心生命周期，这里直接构造拷贝更安全)
        QByteArray pcmData((char const*)fileBuffer.data(), bufferSize);
        // 检查回调是否存在
        if (m_uploadCallback && m_config.roomId > 0)
        {
            // mediaType = 1 (音频)
            m_uploadCallback(pcmData, simpleName, 1);
            //qDebug() << "[AIDataBridge] Requesting Audio Upload:" << filename;
        }
#endif
        // 5. 更新下一块的时间戳基准 \
    // 增加的时长 (ms) = 秒数 * 1000
        int durationMs = (m_audioChunkSamples / m_config.targetSampleRate) * 1000;
        m_currentAudioBasePts += durationMs;
    }
}

// ---------------------------------------------------------
// 视频处理：NV12 -> YUV420P / RGB 并保存
// ---------------------------------------------------------
void AIDataBridge::processVideo(VideoTask& task)
{
    int dstFmt = AV_PIX_FMT_YUV420P;

    // [IMPORTANT] 1. 强制定义目标分辨率为 480x480
    // 即使 task.width/height 是 1920x1080，这里也要强制改为 480
    int dstW = 480;
    int dstH = 480;

    // [IMPORTANT] 2. 初始化 SWS 时使用 dstW, dstH
    // 第4、5参数必须是 dstW, dstH，而不是 task.width, task.height
    if (initSws(task.width, task.height, task.format, dstW, dstH, dstFmt))
    {
        uint8_t const* srcSlice[4] = {0};
        int srcStride[4] = {0};
        srcSlice[0] = task.data.data();
        srcSlice[1] = task.data.data() + task.width * task.height;
        srcStride[0] = task.width;
        srcStride[1] = task.width;

        // [IMPORTANT] 3. 分配内存使用 dstW, dstH
        uint8_t* dstSlice[4] = {0};
        int dstStride[4] = {0};
        int ret = av_image_alloc(dstSlice, dstStride, dstW, dstH, (AVPixelFormat)dstFmt, 1);

        if (ret >= 0)
        {
            // [IMPORTANT] 4. 执行缩放
            // sws_scale 会自动将 1920x1080 的源数据缩放到 480x480
            sws_scale(m_swsCtx, srcSlice, srcStride, 0, task.height, dstSlice, dstStride);

            // [IMPORTANT] 5. 生成文件名时使用 dstW, dstH
            QString filename;
            QString openMode = "wb";

            if (m_config.storageMode == AIStorageMode::SingleStream)
            {
                // 策略2: 全局合并
                filename =
                    QString("%1/video_stream_%2x%3.yuv").arg(m_config.saveDir).arg(dstW).arg(dstH);
                openMode = "ab";
            }
            else if (m_config.storageMode == AIStorageMode::SplitBySecond)
            {
                // 策略3: 按秒分片 [New]

                // 1. 计算当前帧属于哪一秒 (PTS单位为毫秒)
                int64_t currentSecond = task.pts / 1000;
                int64_t lastSecond = m_currentSecondBasePts / 1000;

                // 2. 如果跨秒了，或者还没初始化文件名
                if (currentSecond != lastSecond || m_currentSecondFileName.isEmpty())
                {
                    m_currentSecondBasePts = task.pts;  // 更新基准

                    // 生成新文件名 (例如 video_1710001234_480x480.yuv)
                    m_currentSecondFileName = QString("%1/video_%2_%3x%4.yuv")
                                                  .arg(m_config.saveDir)
                                                  .arg(task.pts)  // 使用这一秒第一帧的时间戳
                                                  .arg(dstW)
                                                  .arg(dstH);
                }

                filename = m_currentSecondFileName;
                openMode = "ab";  // 这一秒内的后续帧都是追加
            }
            else
            {
                // 策略1: 单帧独立 (默认)
                filename = QString("%1/video_%2_%3x%4.yuv")
                               .arg(m_config.saveDir)
                               .arg(task.pts)
                               .arg(dstW)
                               .arg(dstH);
                openMode = "wb";
            }

#if DEBUG_SAVE_LOCAL
            QString nativePath = QDir::toNativeSeparators(filename);
            FILE* f = fopen(nativePath.toLocal8Bit().constData(), openMode.toStdString().c_str());

            if (f)
            {
                // [IMPORTANT] 6. 写入数据使用 dstW, dstH
                // 必须按 dstH 循环写入，否则数据量会超出

                // Y Plane
                for (int i = 0; i < dstH; i++)
                {
                    fwrite(dstSlice[0] + i * dstStride[0], 1, dstW, f);
                }
                // U Plane
                for (int i = 0; i < dstH / 2; i++)
                {
                    fwrite(dstSlice[1] + i * dstStride[1], 1, dstW / 2, f);
                }
                // V Plane
                for (int i = 0; i < dstH / 2; i++)
                {
                    fwrite(dstSlice[2] + i * dstStride[2], 1, dstW / 2, f);
                }

                fclose(f);
            }
#else
            // === 云端上传模式 === \
    // 1. 将 YUV 数据打包成连续内存 (QByteArray)
            QString simpleName = getFileName(filename);
            int ySize = dstW * dstH;
            int uvSize = dstW * dstH / 4;
            QByteArray yuvData;
            yuvData.resize(ySize + uvSize * 2);
            char* ptr = yuvData.data();

            // Copy Y
            for (int i = 0; i < dstH; i++)
            {
                memcpy(ptr, dstSlice[0] + i * dstStride[0], dstW);
                ptr += dstW;
            }
            // Copy U
            for (int i = 0; i < dstH / 2; i++)
            {
                memcpy(ptr, dstSlice[1] + i * dstStride[1], dstW / 2);
                ptr += dstW / 2;
            }
            // Copy V
            for (int i = 0; i < dstH / 2; i++)
            {
                memcpy(ptr, dstSlice[2] + i * dstStride[2], dstW / 2);
                ptr += dstW / 2;
            }

            // 检查回调是否存在
            if (m_uploadCallback && m_config.roomId > 0)
            {
                // mediaType = 1 (音频)
                m_uploadCallback(yuvData, simpleName, 2);
                //qDebug() << "[AIDataBridge] Requesting Audio Upload:" << filename;
            }
#endif
            av_freep(&dstSlice[0]);
        }
    }
}

// ---------------------------------------------------------
// 音频处理：Float32 -> S16LE 16k Mono 并追加保存
// ---------------------------------------------------------
void AIDataBridge::processAudio(AudioTask& task)
{
    // 目标格式: 16k, Mono, S16LE
    int dstFmt = AV_SAMPLE_FMT_S16;
    int dstRate = m_config.targetSampleRate;
    int dstCh = m_config.targetChannels;

    // A. 重采样 (Resample)
    if (initSwr(task.sampleRate, task.channels, task.format, dstRate, dstCh, dstFmt))
    {
        uint8_t const* srcData[] = {(uint8_t const*)task.data.data()};
        int srcNbSamples = task.data.size() /
            (task.channels * av_get_bytes_per_sample((AVSampleFormat)task.format));

        // 计算转换后的样本数
        int maxDstNbSamples =
            av_rescale_rnd(swr_get_delay(m_swrCtx, task.sampleRate) + srcNbSamples,
                           dstRate,
                           task.sampleRate,
                           AV_ROUND_UP);

        // 扩容转换缓存
        if (maxDstNbSamples > m_audioMaxDstNbSamples)
        {
            if (m_audioOutBuffer)
                av_freep(&m_audioOutBuffer[0]);
            av_samples_alloc_array_and_samples(&m_audioOutBuffer,
                                               &m_audioOutLinesize,
                                               dstCh,
                                               maxDstNbSamples,
                                               (AVSampleFormat)dstFmt,
                                               0);
            m_audioMaxDstNbSamples = maxDstNbSamples;
        }

        // 执行转换
        int ret = swr_convert(m_swrCtx, m_audioOutBuffer, maxDstNbSamples, srcData, srcNbSamples);

        if (ret > 0)
        {
            // [Core Step 1] 将转换好的数据写入 FIFO
            // av_audio_fifo_write 会自动管理内存扩容
            if (m_audioFifo)
            {
                // 如果 FIFO 空间不够，ffmpeg 会自动 realloc，但为了保险可以先检查
                int space = av_audio_fifo_space(m_audioFifo);
                if (space < ret)
                {
                    av_audio_fifo_realloc(m_audioFifo, av_audio_fifo_size(m_audioFifo) + ret);
                }
                av_audio_fifo_write(m_audioFifo, (void**)m_audioOutBuffer, ret);

                // 如果是第一帧，记录 PTS 作为基准
                if (m_currentAudioBasePts == 0)
                {
                    m_currentAudioBasePts = task.pts;
                }
            }
        }
    }

    // [Core Step 2] 检查 FIFO 是否够写一个文件
    flushAudioFifo();
}

bool AIDataBridge::initSws(int srcW, int srcH, int srcFmt, int dstW, int dstH, int dstFmt)
{
    if (m_swsCtx && m_lastSrcW == srcW && m_lastSrcH == srcH && m_lastSrcFmt == srcFmt)
    {
        return true;
    }
    // 重新初始化
    m_swsCtx = sws_getCachedContext(m_swsCtx,
                                    srcW,
                                    srcH,
                                    (AVPixelFormat)srcFmt,
                                    dstW,
                                    dstH,
                                    (AVPixelFormat)dstFmt,
                                    SWS_BICUBIC,
                                    nullptr,
                                    nullptr,
                                    nullptr);
    if (m_swsCtx)
    {
        m_lastSrcW = srcW;
        m_lastSrcH = srcH;
        m_lastSrcFmt = srcFmt;
        return true;
    }
    return false;
}

bool AIDataBridge::initSwr(int srcRate, int srcCh, int srcFmt, int dstRate, int dstCh, int dstFmt)
{
    if (m_swrCtx && m_lastASrcRate == srcRate && m_lastASrcCh == srcCh && m_lastASrcFmt == srcFmt)
    {
        return true;
    }

    if (m_swrCtx)
        swr_free(&m_swrCtx);

    AVChannelLayout srcLayout, dstLayout;
    av_channel_layout_default(&srcLayout, srcCh);
    av_channel_layout_default(&dstLayout, dstCh);

    int ret = swr_alloc_set_opts2(&m_swrCtx,
                                  &dstLayout,
                                  (AVSampleFormat)dstFmt,
                                  dstRate,
                                  &srcLayout,
                                  (AVSampleFormat)srcFmt,
                                  srcRate,
                                  0,
                                  nullptr);
    if (ret >= 0 && swr_init(m_swrCtx) >= 0)
    {
        m_lastASrcRate = srcRate;
        m_lastASrcCh = srcCh;
        m_lastASrcFmt = srcFmt;
        return true;
    }
    return false;
}
