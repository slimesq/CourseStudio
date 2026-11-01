#ifndef FFMPEGMODULES_H
#define FFMPEGMODULES_H

#include <QImage>
#include <QString>
#include <QMap>
#include <QDebug>
#include <QQueue>
#include <QMutex>
#include <QFile>
#include <QThread>
#include <QWaitCondition>
#include <memory>
#include <vector>
#include <functional>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <d3d11.h>
#include <wrl/client.h>
#include "RecorderConfig.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/time.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}

// 辅助日志宏
#define LOG_MUX(msg) qDebug() << "[MUX_DEBUG] " << msg
#define LOG_IO(msg) qInfo() << "[IO_THREAD] " << msg
#define LOG_ENC(msg) qInfo() << "[ENCODER] " << msg
#define LOG_AUD(msg) qInfo() << "[AUDIO_DSP] " << msg

class FFHelper
{
public:
    static void logError(QString const& tag, int errCode)
    {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(errCode, errBuf, AV_ERROR_MAX_STRING_SIZE);
        qCritical() << "[FFmpeg]" << tag << ":" << errBuf << "(Code:" << errCode << ")";
    }
};

// --- Muxer 基类 ---
class BaseMuxer
{
public:
    virtual ~BaseMuxer()
    {
        close();
    }
    virtual bool init(QString const& path, RecorderConfig const& config) = 0;

    virtual int writePacket(AVPacket* pkt, AVRational const& sourceTimeBase, bool isVideo)
    {
        if (!fmtCtx)
            return -1;
        AVStream* outStream = isVideo ? vStream : aStream;
        if (!outStream)
            return -1;

        AVPacket* localPkt = av_packet_clone(pkt);
        if (!localPkt)
            return -1;

        localPkt->stream_index = outStream->index;
        av_packet_rescale_ts(localPkt, sourceTimeBase, outStream->time_base);

        int ret = av_interleaved_write_frame(fmtCtx, localPkt);
        av_packet_free(&localPkt);
        return ret;
    }

    virtual void close()
    {
        if (fmtCtx)
        {
            av_write_trailer(fmtCtx);
            if (!(fmtCtx->oformat->flags & AVFMT_NOFILE))
                avio_closep(&fmtCtx->pb);
            avformat_free_context(fmtCtx);
            fmtCtx = nullptr;
        }
    }

    virtual bool isNetworkStream() const
    {
        return false;
    }
    virtual bool tryReconnect()
    {
        return false;
    }

    AVFormatContext* getContext()
    {
        return fmtCtx;
    }
    AVStream* addStream(AVCodecContext const* codecCtx)
    {
        if (!fmtCtx)
            return nullptr;
        AVStream* st = avformat_new_stream(fmtCtx, nullptr);
        if (!st)
            return nullptr;
        avcodec_parameters_from_context(st->codecpar, codecCtx);
        return st;
    }
    void setStreams(AVStream* v, AVStream* a)
    {
        vStream = v;
        aStream = a;
    }

    // [New] 添加这些虚函数
    virtual AVStream* getStream(int index)
    {
        if (fmtCtx && index < (int)fmtCtx->nb_streams)
            return fmtCtx->streams[index];
        return nullptr;
    }

    // [New] 必须添加 writeFrame 接口
    virtual int writeFrame(AVPacket* packet)
    {
        if (!fmtCtx)
            return -1;
        QMutexLocker l(&m_mutex);
        return av_interleaved_write_frame(fmtCtx, packet);
    }

    virtual bool rotate(QString const& newPath)
    {
        return false;
    }

protected:
    AVFormatContext* fmtCtx = nullptr;
    AVStream* vStream = nullptr;
    AVStream* aStream = nullptr;
    QString m_path;
    RecorderConfig m_config;  // 保存完整副本以备重连
    QMutex m_mutex;
    int64_t m_videoPtsOffset = 0;
    int64_t m_audioPtsOffset = 0;
};

class FileMuxer : public BaseMuxer
{
public:
    bool init(QString const& path, RecorderConfig const& config) override
    {
        m_path = path;
        m_config = config;
        int ret =
            avformat_alloc_output_context2(&fmtCtx, nullptr, nullptr, path.toStdString().c_str());
        if (ret < 0 || !fmtCtx)
            return false;
        if (!(fmtCtx->oformat->flags & AVFMT_NOFILE))
        {
            ret = avio_open(&fmtCtx->pb, path.toStdString().c_str(), AVIO_FLAG_WRITE);
            if (ret < 0)
                return false;
        }
        return true;
    }

    // [New] 实现 writePacket 处理 PTS 重置
    int writePacket(AVPacket* pkt, AVRational const& sourceTimeBase, bool isVideo) override
    {
        if (!fmtCtx)
            return -1;
        AVStream* outStream = isVideo ? vStream : aStream;
        if (!outStream)
            return -1;

        AVPacket* localPkt = av_packet_clone(pkt);
        if (!localPkt)
            return -1;

        // --- 核心：减去偏移量，实现新文件时间戳归零 ---
        int64_t* offset = isVideo ? &m_videoPtsOffset : &m_audioPtsOffset;

        // 如果是该文件的第一帧，记录当前 PTS 作为偏移量
        // 注意：这里简单的逻辑可能需要根据实际流的情况微调
        // 比如我们可以在 rotate 被调用时设置一个标志位 "need_reset_offset"

        if (localPkt->pts != AV_NOPTS_VALUE)
        {
            localPkt->pts -= *offset;
        }
        if (localPkt->dts != AV_NOPTS_VALUE)
        {
            localPkt->dts -= *offset;
        }

        localPkt->stream_index = outStream->index;
        av_packet_rescale_ts(localPkt, sourceTimeBase, outStream->time_base);

        int ret = av_interleaved_write_frame(fmtCtx, localPkt);
        av_packet_free(&localPkt);
        return ret;
    }

    // [New] 实现文件轮转
    bool rotate(QString const& newPath) override
    {
        if (!fmtCtx)
            return false;

        LOG_IO("Rotating file to: " << newPath);

        // 1. 写尾部并关闭旧文件
        av_write_trailer(fmtCtx);
        if (!(fmtCtx->oformat->flags & AVFMT_NOFILE))
        {
            avio_closep(&fmtCtx->pb);
        }

        // 保存旧的编解码参数，用于创建新 Context
        AVCodecParameters* vPar = nullptr;
        AVCodecParameters* aPar = nullptr;
        if (vStream)
        {
            vPar = avcodec_parameters_alloc();
            avcodec_parameters_copy(vPar, vStream->codecpar);
        }
        if (aStream)
        {
            aPar = avcodec_parameters_alloc();
            avcodec_parameters_copy(aPar, aStream->codecpar);
        }

        // 释放旧 Context
        avformat_free_context(fmtCtx);
        fmtCtx = nullptr;
        vStream = nullptr;
        aStream = nullptr;

        // 2. 创建新 Context
        m_path = newPath;
        int ret =
            avformat_alloc_output_context2(&fmtCtx, nullptr, nullptr, m_path.toStdString().c_str());
        if (ret < 0)
        {
            if (vPar)
                avcodec_parameters_free(&vPar);
            if (aPar)
                avcodec_parameters_free(&aPar);
            return false;
        }

        // 3. 恢复流信息
        if (vPar)
        {
            vStream = avformat_new_stream(fmtCtx, nullptr);
            avcodec_parameters_copy(vStream->codecpar, vPar);
            avcodec_parameters_free(&vPar);
        }
        if (aPar)
        {
            aStream = avformat_new_stream(fmtCtx, nullptr);
            avcodec_parameters_copy(aStream->codecpar, aPar);
            avcodec_parameters_free(&aPar);
        }

        // 4. 打开新文件
        if (!(fmtCtx->oformat->flags & AVFMT_NOFILE))
        {
            ret = avio_open(&fmtCtx->pb, m_path.toStdString().c_str(), AVIO_FLAG_WRITE);
            if (ret < 0)
                return false;
        }

        // 5. 写头部
        avformat_write_header(fmtCtx, nullptr);

        // 6. [关键] 更新偏移量
        // 设置标志位，让 writePacket 在下一次写入时捕获当前 PTS 作为 offset
        // 简单做法：我们不在这里设 offset，而是在 Sender 线程里，
        // 在调用 rotate 的那一刻，把当前的 video pts 传进来作为 offset。

        return true;
    }

    // 辅助：设置偏移量
    void setPtsOffsets(int64_t vOffset, int64_t aOffset)
    {
        m_videoPtsOffset = vOffset;
        m_audioPtsOffset = aOffset;
    }
};

class StreamMuxer : public BaseMuxer
{
public:
    bool init(QString const& url, RecorderConfig const& config) override
    {
        m_path = url;
        m_config = config;
        return connectInternal();
    }

    bool isNetworkStream() const override
    {
        return true;
    }

    bool tryReconnect() override
    {
        if (fmtCtx)
        {
            if (fmtCtx->pb)
                avio_closep(&fmtCtx->pb);
        }
        LOG_IO("Attempting RTMP reconnect to: " << m_path);

        AVDictionary* opts = nullptr;
        applyNetworkOptions(&opts);

        int ret =
            avio_open2(&fmtCtx->pb, m_path.toStdString().c_str(), AVIO_FLAG_WRITE, nullptr, &opts);
        av_dict_free(&opts);

        if (ret < 0)
        {
            FFHelper::logError("Reconnect Failed", ret);
            return false;
        }
        LOG_IO("Reconnect SUCCESS!");
        return true;
    }

private:
    void applyNetworkOptions(AVDictionary** opts)
    {
        // [优化] 读写超时设置 (微秒)
        av_dict_set_int(opts, "rw_timeout", 3000000, 0);  // 3 seconds is enough

        // [优化] 开启 TCP_NODELAY，禁用 Nagle 算法，对低延迟至关重要
        av_dict_set(opts, "tcp_nodelay", "1", 0);

        // [优化] 遇到 EOF 或错误时尝试重连
        av_dict_set(opts, "reconnect", "1", 0);
        av_dict_set(opts, "reconnect_at_eof", "1", 0);
        av_dict_set(opts, "reconnect_streamed", "1", 0);
        av_dict_set(opts, "reconnect_delay_max", "2", 0);  // 重连等待不要太久

        // [关键优化] 减小 FFmpeg 内部的包缓冲。
        // 原来的 10000 包太大了，会导致网络抖动时 FFmpeg 内部积压数秒数据而不报错
        // 改为 100-200，一旦网络堵塞立即让上层感知
        av_dict_set(opts, "fifo_size", "50", 0);

        // [关键优化] 强制立即发送，不进行微缓冲
        av_dict_set(opts, "flush_packets", "1", 0);

        // [优化] 设置 Socket 发送缓冲区大小
        if (m_config.network.sendBufferSize > 0)
        {
            av_dict_set_int(opts, "send_buffer_size", m_config.network.sendBufferSize, 0);
        }
    }

    bool connectInternal()
    {
        // 默认 flv 用于 RTMP
        // [企业级优化] 设置低延迟推流参数
        AVDictionary* options = nullptr;

        // 1. 强制每包发送，不缓存
        av_dict_set(&options, "flush_packets", "1", 0);

        // 2. 降低 RTMP 内部缓冲
        av_dict_set(&options, "rtmp_buffer", "100", 0);  // 100ms
        av_dict_set(&options, "rtmp_live", "live", 0);

        // 3. TCP 无延迟 (禁用 Nagle 算法)
        av_dict_set(&options, "tcp_nodelay", "1", 0);

        // 4. 超时设置 (避免阻塞太久)
        av_dict_set(&options, "rw_timeout", "3000000", 0);  // 3秒超时

        int ret =
            avformat_alloc_output_context2(&fmtCtx, nullptr, "flv", m_path.toStdString().c_str());
        if (ret < 0 || !fmtCtx)
            return false;

        AVDictionary* opts = nullptr;
        applyNetworkOptions(&opts);

        if (!(fmtCtx->oformat->flags & AVFMT_NOFILE))
        {
            ret = avio_open2(
                &fmtCtx->pb, m_path.toStdString().c_str(), AVIO_FLAG_WRITE, nullptr, &opts);
            if (ret < 0)
            {
                av_dict_free(&opts);
                return false;
            }
        }
        av_dict_free(&opts);
        return true;
    }
};

struct AsyncPacket
{
    AVPacket* pkt = nullptr;
    AVRational timeBase;
    bool isVideo;
    bool isKeyFrame;
    bool triggerSplit = false;  // 该包是否触发分片
    QString splitNewPath = "";  // 分片后的新文件路径
};

class PacketQueue
{
public:
    void push(AsyncPacket p)
    {
        QMutexLocker lock(&m_mutex);
        m_queue.enqueue(p);
        m_cond.wakeOne();
    }

    AsyncPacket pop()
    {
        QMutexLocker lock(&m_mutex);
        while (m_queue.isEmpty() && !m_abort)
        {
            m_cond.wait(&m_mutex);
        }
        if (m_abort && m_queue.isEmpty())
            return {nullptr, {0, 0}, false, false};
        return m_queue.dequeue();
    }

    void abort()
    {
        QMutexLocker lock(&m_mutex);
        m_abort = true;
        m_cond.wakeAll();
    }

    int size()
    {
        QMutexLocker lock(&m_mutex);
        return m_queue.size();
    }

    void clear()
    {
        QMutexLocker lock(&m_mutex);
        while (!m_queue.isEmpty())
        {
            AsyncPacket p = m_queue.dequeue();
            if (p.pkt)
                av_packet_free(&p.pkt);
        }
        LOG_IO("Packet Queue Cleared.");
    }

    int dropNonKeyFrames()
    {
        QMutexLocker lock(&m_mutex);
        int dropped = 0;
        QQueue<AsyncPacket> newQueue;
        while (!m_queue.isEmpty())
        {
            AsyncPacket p = m_queue.dequeue();
            if (!p.isVideo || p.isKeyFrame)
            {
                newQueue.enqueue(p);  // 保留音频和关键帧
            }
            else
            {
                if (p.pkt)
                    av_packet_free(&p.pkt);
                dropped++;
            }
        }
        m_queue = newQueue;
        if (dropped > 0)
            LOG_IO("Smart Drop: " << dropped);
        return dropped;
    }

private:
    QQueue<AsyncPacket> m_queue;
    QMutex m_mutex;
    QWaitCondition m_cond;
    bool m_abort = false;
};

class StreamSender : public QThread
{
public:
    void setMuxers(std::vector<std::shared_ptr<BaseMuxer>> const& muxers)
    {
        m_muxers = muxers;
    }

    void pushPacket(AVPacket* pkt, AVRational tb, bool isVideo)
    {
        AVPacket* clone = av_packet_clone(pkt);
        bool isKey = (pkt->flags & AV_PKT_FLAG_KEY);
        if (clone)
            m_queue.push({clone, tb, isVideo, isKey});
    }

    void stop()
    {
        m_queue.push({nullptr, {0, 0}, false, false});
        wait();
    }

    int getQueueSize()
    {
        return m_queue.size();
    }
    void clearQueue()
    {
        m_queue.clear();
    }
    int smartDrop()
    {
        return m_queue.dropNonKeyFrames();
    }

    uint64_t getTotalBytesSent() const
    {
        return m_bytesSent.load();
    }

    // [New] 外部调用此函数安排分片
    void scheduleSplit(QString const& newPath, int64_t currentVideoPts, int64_t currentAudioPts)
    {
        QMutexLocker lock(&m_splitMutex);
        m_nextFilePath = newPath;
        m_pendingSplitVideoPts = currentVideoPts;
        m_pendingSplitAudioPts = currentAudioPts;
        m_splitRequested = true;
    }

protected:
    void run() override
    {
        LOG_IO("Sender Thread Started.");
        std::vector<bool> muxerBroken(m_muxers.size(), false);
        int reconnectCounter = 0;

        while (true)
        {
            if (m_splitRequested)
            {
                QString newPath;
                int64_t vPts, aPts;
                {
                    QMutexLocker lock(&m_splitMutex);
                    if (m_splitRequested)
                    {
                        newPath = m_nextFilePath;
                        vPts = m_pendingSplitVideoPts;
                        aPts = m_pendingSplitAudioPts;
                        m_splitRequested = false;
                    }
                }

                if (!newPath.isEmpty())
                {
                    for (auto& muxer : m_muxers)
                    {
                        // 只有本地文件 Muxer 需要切片，网络流不需要
                        if (!muxer->isNetworkStream())
                        {
                            // 调用 rotate
                            if (muxer->rotate(newPath))
                            {
                                // 强制转换类型设置偏移
                                auto fileMuxer = std::dynamic_pointer_cast<FileMuxer>(muxer);
                                if (fileMuxer)
                                {
                                    fileMuxer->setPtsOffsets(vPts, aPts);
                                }
                            }
                        }
                    }
                }
            }
            AsyncPacket p = m_queue.pop();
            if (p.pkt == nullptr)
                break;

            if (++reconnectCounter > 30)
            {
                reconnectCounter = 0;
                for (size_t i = 0; i < m_muxers.size(); ++i)
                {
                    if (muxerBroken[i] && m_muxers[i]->isNetworkStream())
                    {
                        if (m_muxers[i]->tryReconnect())
                            muxerBroken[i] = false;
                    }
                }
            }

            for (size_t i = 0; i < m_muxers.size(); ++i)
            {
                if (muxerBroken[i])
                    continue;
                int packetSize = p.pkt->size;
                int ret = m_muxers[i]->writePacket(p.pkt, p.timeBase, p.isVideo);

                if (ret < 0)
                {
                    if (m_muxers[i]->isNetworkStream())
                    {
                        // [Fix] 增加详细错误日志
                        char errBuf[128] = {0};
                        av_strerror(ret, errBuf, sizeof(errBuf));
                        LOG_IO("Network Write Error: " << errBuf << " (" << ret << ")");

                        muxerBroken[i] = true;
                    }
                    else
                    {
                        static bool logOnce = false;
                        if (!logOnce)
                        {
                            FFHelper::logError("File Write", ret);
                            logOnce = true;
                        }
                    }
                }
                else
                {
                    if (m_muxers[i]->isNetworkStream())
                    {
                        m_bytesSent += packetSize;
                    }
                }
            }
            av_packet_free(&p.pkt);
        }
        for (auto& muxer : m_muxers)
            muxer->close();
        LOG_IO("Sender Thread Stopped.");
    }

private:
    PacketQueue m_queue;
    std::vector<std::shared_ptr<BaseMuxer>> m_muxers;
    std::atomic<uint64_t> m_bytesSent{0};
    QMutex m_splitMutex;
    bool m_splitRequested = false;
    QString m_nextFilePath;
    int64_t m_pendingSplitVideoPts = 0;
    int64_t m_pendingSplitAudioPts = 0;
};

// --- VideoEncoder ---
class VideoEncoder
{
public:
    ~VideoEncoder()
    {
        close();
    }
    void close()
    {
        if (ctx)
        {
            avcodec_free_context(&ctx);
            ctx = nullptr;
        }
        if (pkt)
        {
            av_packet_free(&pkt);
            pkt = nullptr;
        }
        if (hwFramesRef)
        {
            av_buffer_unref(&hwFramesRef);
            hwFramesRef = nullptr;
        }
        if (hwDeviceRef)
        {
            av_buffer_unref(&hwDeviceRef);
            hwDeviceRef = nullptr;
        }
        m_isHardware = false;
    }

    // [Fix] 接收 VideoBaseConfig 和 TranscodeConfig
    bool init(VideoBaseConfig const& baseConf,
              TranscodeConfig const& transConf,
              ID3D11Device* d3dDevice = nullptr)
    {
        close();
        std::vector<QString> candidates;
        candidates.push_back(transConf.encoderName);
        if (transConf.hardwareFallback)
        {
            if (transConf.encoderName != "h264_nvenc")
                candidates.push_back("h264_nvenc");
            if (transConf.encoderName != "h264_amf")
                candidates.push_back("h264_amf");
            candidates.push_back("libx264");
        }
        for (auto const& name : candidates)
        {
            if (tryInitEncoder(name, baseConf, transConf, d3dDevice))
                return true;
            close();
        }
        return false;
    }

    bool isHardware() const
    {
        return m_isHardware;
    }
    void setBitrate(int64_t bitrate)
    {
        if (!ctx)
            return;
        ctx->bit_rate = bitrate;
        ctx->rc_min_rate = bitrate;
        ctx->rc_max_rate = bitrate;
        ctx->rc_buffer_size = (int)bitrate;
    }
    AVCodecContext* getContext()
    {
        return ctx;
    }
    AVBufferRef* getHWFramesRef()
    {
        return hwFramesRef;
    }
    AVPacket* encode(AVFrame* frame)
    {
        return sendAndReceive(frame);
    }
    AVPacket* flush()
    {
        return sendAndReceive(nullptr);
    }

private:
    bool tryInitEncoder(QString const& name,
                        VideoBaseConfig const& baseConf,
                        TranscodeConfig const& transConf,
                        ID3D11Device* d3dDevice)
    {
        // [AUDIT LOG] 开始尝试初始化编码器
        qInfo() << "[ENC_AUDIT] Trying Encoder:" << name;

        AVCodec const* codec = avcodec_find_encoder_by_name(name.toStdString().c_str());
        if (!codec)
        {
            qWarning() << "[ENC_AUDIT] Codec not found:" << name;
            return false;
        }

        ctx = avcodec_alloc_context3(codec);
        ctx->width = baseConf.outputResolution.width();
        ctx->height = baseConf.outputResolution.height();
        ctx->time_base = {1, (int)(baseConf.fps + 0.5)};
        ctx->framerate = {(int)(baseConf.fps + 0.5), 1};
        ctx->pix_fmt = AV_PIX_FMT_NV12;

        int64_t bitrate = (int64_t)transConf.bitrateKbps * 1000;
        int64_t maxBitrate = (int64_t)transConf.maxBitrateKbps * 1000;

        ctx->bit_rate = bitrate;
        ctx->gop_size = (int)(baseConf.fps * transConf.keyframeIntervalSec);

        // [企业级优化 1] 强制关闭 B 帧，这是 RTMP 低延迟的核心
        // 任何 B 帧都会导致播放器必须等待后续 P 帧到达才能解码，直接引入延迟
        ctx->max_b_frames = 0;

        // [企业级优化 2] 线程数设置
        // 设为 0 (自动) 在某些软编情况下可能开启 Frame Threading 导致一帧延迟
        // 如果追求极致延迟，可设为 1，但为了性能平衡，这里设为 0 配合 zerolatency 标志使用
        ctx->thread_count = 0;

        // [企业级优化 3] 缩减码率控制缓冲区 (VBV Buffer)
        // 默认 buffer 往往是 1-2秒，导致网络波动时数据在编码器内部积压。
        // 设为 0.5 秒或更小，迫使编码器在带宽受限时快速降低画质而不是堆积数据。
        ctx->rc_buffer_size = bitrate / 2;

        // [AUDIT LOG] 打印流控策略
        QString rcModeStr;
        if (transConf.rateControl == RateControlMode::CBR)
        {
            rcModeStr = "CBR";
            ctx->rc_min_rate = bitrate;
            ctx->rc_max_rate = bitrate;
            // CBR 下 buffer 可以稍大一点容忍波动，但为了低延迟这里依然保持较小
            ctx->rc_buffer_size = bitrate / 2;
        }
        else if (transConf.rateControl == RateControlMode::VBR)
        {
            rcModeStr = "VBR";
            ctx->rc_max_rate = maxBitrate;
            ctx->rc_buffer_size = maxBitrate;  // VBR下 buffer 也要限制
        }
        else if (transConf.rateControl == RateControlMode::CQP)
        {
            rcModeStr = "CQP";
            ctx->flags |= AV_CODEC_FLAG_QSCALE;
            ctx->global_quality = transConf.qualityLevel * FF_QP2LAMBDA;

            // [CRITICAL FIX] 针对 NVENC 显式开启 CQP 模式
            if (name.contains("nvenc"))
            {
                av_opt_set(ctx->priv_data, "rc", "constqp", 0);
                av_opt_set_int(ctx->priv_data, "qp", transConf.qualityLevel, 0);
                // 开启空间自适应量化，提升画质
                av_opt_set_int(ctx->priv_data, "spatial-aq", 1, 0);
            }

            // 保底码率：防止某些驱动下 CQP 失效回退到 0 码率
            if (ctx->bit_rate == 0)
                ctx->bit_rate = 10000000;  // 10Mbps
        }

        ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        // ----------------------------------------------------------------------
        // [企业级优化 4] 针对不同编码器的低延迟参数深度调优
        // ----------------------------------------------------------------------

        // >>> 1. x264 软编优化
        if (name == "libx264")
        {
            // "zerolatency" 是核心，它会禁用 B 帧，去除 Lookahead，通过牺牲压缩率换取最低延迟
            av_opt_set(ctx->priv_data, "tune", "zerolatency", 0);

            // preset: superfast 够快且画质尚可，veryfast 也可以。ultrafast 画质损失较大。
            QString preset = transConf.preset.isEmpty() ? "veryfast" : transConf.preset;
            av_opt_set(ctx->priv_data, "preset", preset.toStdString().c_str(), 0);

            // 强制关键帧间隔，防止 scenecut (场景切换检测) 导致关键帧不可控，影响流畅度
            // x264-params: keyint=GOP:min-keyint=GOP:scenecut=0
            int gop = ctx->gop_size;
            if (gop <= 0)
                gop = 30;  // 保底
            QString x264Params = QString("keyint=%1:min-keyint=%1:scenecut=0").arg(gop);
            av_opt_set(ctx->priv_data, "x264-params", x264Params.toStdString().c_str(), 0);
        }
        // >>> 2. NVENC 硬编优化 (NVIDIA)
        else if (name.contains("nvenc"))
        {
            // 预设: p4 (medium) 平衡画质和速度，p1最快，p7画质最好
            av_opt_set(ctx->priv_data, "preset", "p4", 0);

            // 调优: ull (Ultra Low Latency) 是 NVENC 专门为直播设计的低延迟模式
            av_opt_set(ctx->priv_data, "tune", "ull", 0);

            // 强制 0 延迟缓冲
            av_opt_set(ctx->priv_data, "delay", "0", 0);

            // 如果是 CBR，开启严格 CBR 模式，防止码率突发
            if (transConf.rateControl == RateControlMode::CBR)
            {
                av_opt_set_int(ctx->priv_data, "cbr", 1, 0);
            }
        }
        // >>> 3. AMF 硬编优化 (AMD)
        else if (name.contains("amf"))
        {
            // 开启 AMD 的超低延迟模式
            av_opt_set(ctx->priv_data, "usage", "ultralowlatency", 0);
        }
        // >>> 4. QSV 硬编优化 (Intel)
        else if (name.contains("qsv"))
        {
            // 异步深度设为1，减少缓冲
            av_opt_set(ctx->priv_data, "async_depth", "1", 0);
        }

        // ----------------------------------------------------------------------

        // 硬件初始化部分 (保持不变)
        if (d3dDevice && name != "libx264")
        {
            int ret =
                av_hwdevice_ctx_create(&hwDeviceRef, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
            if (ret >= 0)
            {
                hwFramesRef = av_hwframe_ctx_alloc(hwDeviceRef);
                if (hwFramesRef)
                {
                    AVHWFramesContext* framesCtx = (AVHWFramesContext*)hwFramesRef->data;
                    framesCtx->format = AV_PIX_FMT_D3D11;
                    framesCtx->sw_format = AV_PIX_FMT_NV12;
                    framesCtx->width = ctx->width;
                    framesCtx->height = ctx->height;
                    framesCtx->initial_pool_size = 20;
                    if (av_hwframe_ctx_init(hwFramesRef) >= 0)
                    {
                        ctx->hw_frames_ctx = av_buffer_ref(hwFramesRef);
                        ctx->pix_fmt = AV_PIX_FMT_D3D11;
                        m_isHardware = true;
                    }
                }
            }
        }
        if (!m_isHardware)
            ctx->pix_fmt = AV_PIX_FMT_NV12;

        // 设置通用 Profile (保持不变)
        if (!transConf.profile.isEmpty())
            av_opt_set(ctx->priv_data, "profile", transConf.profile.toStdString().c_str(), 0);

        // [AUDIT LOG] 详细输出最终参数
        qInfo() << "========== [ENC_AUDIT] CONFIG ==========";
        qInfo() << "  Codec:      " << name;
        qInfo() << "  Resolution: " << ctx->width << "x" << ctx->height;
        qInfo() << "  RC Mode:    " << rcModeStr;
        qInfo() << "  Bitrate:    " << ctx->bit_rate << " bps";
        qInfo() << "  MaxRate:    " << ctx->rc_max_rate;
        qInfo() << "  BufSize:    " << ctx->rc_buffer_size;
        qInfo() << "  GOP:        " << ctx->gop_size;
        qInfo() << "  Quality:    " << ctx->global_quality;
        qInfo() << "  Hardware:   " << (m_isHardware ? "Yes (D3D11)" : "No");
        qInfo() << "========================================";

        // Color Space 设置 (保持不变)
        if (baseConf.colorSpace == ColorSpace::Rec709)
        {
            ctx->colorspace = AVCOL_SPC_BT709;
            ctx->color_primaries = AVCOL_PRI_BT709;
            ctx->color_trc = AVCOL_TRC_BT709;
        }
        else
        {
            ctx->colorspace = AVCOL_SPC_SMPTE170M;
            ctx->color_primaries = AVCOL_PRI_SMPTE170M;
            ctx->color_trc = AVCOL_TRC_SMPTE170M;
        }
        if (baseConf.colorRange == ColorRange::Full)
        {
            ctx->color_range = AVCOL_RANGE_JPEG;
        }
        else
        {
            ctx->color_range = AVCOL_RANGE_MPEG;
        }

        int ret = avcodec_open2(ctx, codec, nullptr);
        if (ret < 0)
        {
            char errBuf[128];
            av_strerror(ret, errBuf, 128);
            qWarning() << "[ENC_AUDIT] Open Failed:" << errBuf;
            return false;
        }

        pkt = av_packet_alloc();
        qInfo() << "[ENC_AUDIT] Encoder Opened Successfully.";
        return true;
    }

    AVPacket* sendAndReceive(AVFrame* frame)
    {
        if (!ctx)
            return nullptr;
        int ret = avcodec_send_frame(ctx, frame);
        if (ret < 0 && ret != AVERROR_EOF)
            return nullptr;
        ret = avcodec_receive_packet(ctx, pkt);
        if (ret == 0)
            return av_packet_clone(pkt);
        return nullptr;
    }
    AVCodecContext* ctx = nullptr;
    AVPacket* pkt = nullptr;
    AVBufferRef* hwDeviceRef = nullptr;
    AVBufferRef* hwFramesRef = nullptr;
    bool m_isHardware = false;
};

class AudioEncoder
{
public:
    ~AudioEncoder()
    {
        if (ctx)
            avcodec_free_context(&ctx);
        if (pkt)
            av_packet_free(&pkt);
    }

    // [Fix] 接收 AudioConfig
    bool init(AudioConfig const& aConf)
    {
        AVCodec const* codec = avcodec_find_encoder_by_name(aConf.codecName.toStdString().c_str());
        if (!codec)
            codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!codec)
            return false;
        ctx = avcodec_alloc_context3(codec);
        ctx->sample_rate = aConf.sampleRate;
        av_channel_layout_default(&ctx->ch_layout, aConf.channels);
        ctx->sample_fmt = codec->sample_fmts ? codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        ctx->bit_rate = aConf.bitrate;
        ctx->time_base = {1, ctx->sample_rate};
        ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(ctx, codec, nullptr) < 0)
            return false;
        pkt = av_packet_alloc();
        return true;
    }
    AVCodecContext* getContext()
    {
        return ctx;
    }
    AVPacket* encode(AVFrame* frame)
    {
        return sendAndReceive(frame);
    }
    AVPacket* flush()
    {
        return sendAndReceive(nullptr);
    }

private:
    AVPacket* sendAndReceive(AVFrame* frame)
    {
        if (!ctx)
            return nullptr;
        int ret = avcodec_send_frame(ctx, frame);
        if (ret < 0 && ret != AVERROR_EOF)
            return nullptr;
        ret = avcodec_receive_packet(ctx, pkt);
        if (ret == 0)
            return av_packet_clone(pkt);
        return nullptr;
    }
    AVCodecContext* ctx = nullptr;
    AVPacket* pkt = nullptr;
};

// --- AudioDSP ---
class AudioDSP
{
    struct SourceCtx
    {
        AVAudioFifo* fifo = nullptr;
        SwrContext* swr = nullptr;
        uint8_t** resampleBuffer = nullptr;
        int lastInputRate = 0;
        int lastInputChannels = 0;
        float volume = 1.0f;
    };

public:
    ~AudioDSP()
    {
        clean();
    }

    void init(RecorderConfig const& config)
    {
        m_outRate = config.audio.sampleRate;
        m_outChannels = config.audio.channels;
    }

    void setSourceEnabled(int id, bool enable)
    {
        QMutexLocker lock(&m_mutex);
        m_enabledSources[id] = enable;
    }

    void setSourceVolume(int id, float volume)
    {
        QMutexLocker lock(&m_mutex);
        sources[id].volume = volume;
    }

    void pushData(int sourceId, QByteArray const& data, int inRate, int inChannels)
    {
        QMutexLocker lock(&m_mutex);
        if (!m_enabledSources.value(sourceId, false))
            return;

        SourceCtx& ctx = sources[sourceId];
        if (!ctx.swr || ctx.lastInputRate != inRate || ctx.lastInputChannels != inChannels)
        {
            if (ctx.swr)
                swr_free(&ctx.swr);
            AVChannelLayout outLayout;
            av_channel_layout_default(&outLayout, m_outChannels);
            AVChannelLayout inLayout;
            av_channel_layout_default(&inLayout, inChannels);
            swr_alloc_set_opts2(&ctx.swr,
                                &outLayout,
                                AV_SAMPLE_FMT_FLT,
                                m_outRate,
                                &inLayout,
                                AV_SAMPLE_FMT_FLT,
                                inRate,
                                0,
                                nullptr);
            if (swr_init(ctx.swr) < 0)
                return;
            if (ctx.fifo)
                av_audio_fifo_free(ctx.fifo);
            ctx.fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLT, m_outChannels, 4096);
            ctx.lastInputRate = inRate;
            ctx.lastInputChannels = inChannels;
            if (ctx.resampleBuffer)
                av_freep(&ctx.resampleBuffer[0]);
            av_samples_alloc_array_and_samples(
                &ctx.resampleBuffer, nullptr, m_outChannels, 4096, AV_SAMPLE_FMT_FLT, 0);
        }
        int inSamples = data.size() / (inChannels * sizeof(float));
        uint8_t const* inputData[] = {(uint8_t const*)data.constData()};
        int outSamplesMax = av_rescale_rnd(
            swr_get_delay(ctx.swr, inRate) + inSamples, m_outRate, inRate, AV_ROUND_UP);
        int convertedSamples =
            swr_convert(ctx.swr, ctx.resampleBuffer, outSamplesMax, inputData, inSamples);
        if (convertedSamples > 0)
            av_audio_fifo_write(ctx.fifo, (void**)ctx.resampleBuffer, convertedSamples);
    }

    int popMixedData(float* outL, float* outR, int nbSamples)
    {
        QMutexLocker lock(&m_mutex);
        memset(outL, 0, nbSamples * sizeof(float));
        memset(outR, 0, nbSamples * sizeof(float));
        if (tmpSize < nbSamples)
        {
            if (tmpBuf)
                delete[] tmpBuf;
            tmpBuf = new float[nbSamples * m_outChannels];
            tmpSize = nbSamples;
        }

        bool hasOutput = false;
        for (auto key : sources.keys())
        {
            if (!m_enabledSources[key])
                continue;
            auto& s = sources[key];
            if (!s.fifo)
                continue;
            if (av_audio_fifo_size(s.fifo) >= nbSamples)
            {
                void* bufPtr = tmpBuf;
                av_audio_fifo_read(s.fifo, &bufPtr, nbSamples);
                for (int i = 0; i < nbSamples; ++i)
                {
                    outL[i] += tmpBuf[i * 2] * s.volume;
                    outR[i] += tmpBuf[i * 2 + 1] * s.volume;
                }
                hasOutput = true;
            }
        }
        if (!hasOutput)
            return 0;

        for (int i = 0; i < nbSamples; ++i)
        {
            outL[i] = std::tanh(outL[i]);
            outR[i] = std::tanh(outR[i]);
        }
        return nbSamples;
    }

    void clean()
    {
        QMutexLocker lock(&m_mutex);
        for (auto key : sources.keys())
        {
            if (sources[key].fifo)
            {
                av_audio_fifo_free(sources[key].fifo);
                sources[key].fifo = nullptr;
            }
            if (sources[key].swr)
            {
                swr_free(&sources[key].swr);
                sources[key].swr = nullptr;
            }
            if (sources[key].resampleBuffer)
            {
                av_freep(&sources[key].resampleBuffer[0]);
                av_freep(&sources[key].resampleBuffer);
            }
        }
        if (tmpBuf)
        {
            delete[] tmpBuf;
            tmpBuf = nullptr;
        }
        tmpSize = 0;
    }

private:
    QMap<int, SourceCtx> sources;
    QMap<int, bool> m_enabledSources;
    QMutex m_mutex;
    int m_outRate;
    int m_outChannels;
    float* tmpBuf = nullptr;
    int tmpSize = 0;
};

#endif  // FFMPEGMODULES_H
