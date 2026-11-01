#include "VideoFileSource.h"
#include <QFileInfo>
#include <QDebug>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
}

VideoFileSource::VideoFileSource()
{
    setSourceType(SourceType::Video);
}

VideoFileSource::~VideoFileSource()
{
    stop();
}

bool VideoFileSource::openFile(QString const& filePath)
{
    stop();
    m_filePath = filePath;

    if (m_filePath.isEmpty() || m_filePath.startsWith("win:") || m_filePath.startsWith("mon:"))
    {
        qWarning() << "[VideoSource] Invalid file path ignored:" << filePath;
        return false;
    }

    // --- 同步探测分辨率 (保持之前的逻辑) ---
    AVFormatContext* fmtCtx = avformat_alloc_context();
    if (avformat_open_input(&fmtCtx, m_filePath.toStdString().c_str(), nullptr, nullptr) == 0)
    {
        if (avformat_find_stream_info(fmtCtx, nullptr) >= 0)
        {
            for (unsigned int i = 0; i < fmtCtx->nb_streams; i++)
            {
                if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                {
                    m_width = fmtCtx->streams[i]->codecpar->width;
                    m_height = fmtCtx->streams[i]->codecpar->height;
                    qDebug() << "[VideoSource] Probed resolution:" << m_width << "x" << m_height;
                    break;
                }
            }
        }
        avformat_close_input(&fmtCtx);
    }

    // 默认值兜底
    if (m_width == 0 || m_height == 0)
    {
        m_width = 1280;
        m_height = 720;
    }

    return true;
}

void VideoFileSource::setLoop(bool loop)
{
    m_loop = loop;
}

bool VideoFileSource::start()
{
    if (m_running)
        return true;
    if (m_filePath.isEmpty())
        return false;

    m_running = true;
    m_decodeThread = std::thread(&VideoFileSource::decodeLoop, this);
    return true;
}

void VideoFileSource::stop()
{
    m_running = false;
    if (m_decodeThread.joinable())
    {
        m_decodeThread.join();
    }
    cleanupFFmpeg();
}

void VideoFileSource::cleanupFFmpeg()
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    if (m_frame)
    {
        av_frame_free(&m_frame);
        m_frame = nullptr;
    }
    if (m_codecCtx)
    {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }
    if (m_fmtCtx)
    {
        avformat_close_input(&m_fmtCtx);
        m_fmtCtx = nullptr;
    }
    if (m_swsCtx)
    {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }
}

void VideoFileSource::decodeLoop()
{
    m_fmtCtx = avformat_alloc_context();
    if (avformat_open_input(&m_fmtCtx, m_filePath.toStdString().c_str(), nullptr, nullptr) != 0)
    {
        qCritical() << "[VideoSource] FFmpeg: Could not open file" << m_filePath;
        m_running = false;
        return;
    }

    if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0)
    {
        m_running = false;
        return;
    }

    m_videoStreamIndex = -1;
    for (unsigned int i = 0; i < m_fmtCtx->nb_streams; i++)
    {
        if (m_fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            m_videoStreamIndex = i;
            break;
        }
    }

    if (m_videoStreamIndex == -1)
    {
        m_running = false;
        return;
    }

    AVCodecParameters* codecPar = m_fmtCtx->streams[m_videoStreamIndex]->codecpar;
    AVCodec const* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec)
    {
        m_running = false;
        return;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_codecCtx, codecPar);

    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0)
    {
        m_running = false;
        return;
    }

    m_frame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();

    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        m_width = m_codecCtx->width;
        m_height = m_codecCtx->height;
    }

    while (m_running)
    {
        int ret = av_read_frame(m_fmtCtx, packet);
        if (ret < 0)
        {
            if (ret == AVERROR_EOF && m_loop)
            {
                av_seek_frame(m_fmtCtx, m_videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
                avcodec_flush_buffers(m_codecCtx);
                continue;
            }
            else
            {
                break;
            }
        }

        if (packet->stream_index == m_videoStreamIndex)
        {
            if (avcodec_send_packet(m_codecCtx, packet) == 0)
            {
                while (avcodec_receive_frame(m_codecCtx, m_frame) == 0)
                {
                    std::lock_guard<std::mutex> lock(m_bufferMutex);

                    // [Fix] 格式改为 BGRA，匹配 DXGI_FORMAT_B8G8R8A8_UNORM
                    if (!m_swsCtx)
                    {
                        m_swsCtx = sws_getContext(m_width,
                                                  m_height,
                                                  m_codecCtx->pix_fmt,
                                                  m_width,
                                                  m_height,
                                                  AV_PIX_FMT_BGRA,
                                                  SWS_BILINEAR,
                                                  nullptr,
                                                  nullptr,
                                                  nullptr);
                    }

                    int stride = m_width * 4;
                    // [Fix] 崩溃修复核心：额外分配 64 字节 Padding
                    // 防止 sws_scale SIMD 优化写入溢出
                    size_t requiredSize = (size_t)(m_height * stride) + 64;
                    if (m_pixelBuffer.size() < requiredSize)
                    {
                        m_pixelBuffer.resize(requiredSize);
                    }

                    uint8_t* dest[4] = {m_pixelBuffer.data(), nullptr, nullptr, nullptr};
                    int destLinesize[4] = {stride, 0, 0, 0};

                    sws_scale(m_swsCtx,
                              m_frame->data,
                              m_frame->linesize,
                              0,
                              m_height,
                              dest,
                              destLinesize);

                    m_uploadPending = true;
                    m_hasNewFrame = true;
                }
            }
        }
        av_packet_unref(packet);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    av_packet_free(&packet);
}

float VideoFileSource::getBaseWidth() const
{
    return (float)(m_width > 0 ? m_width : 1280);
}
float VideoFileSource::getBaseHeight() const
{
    return (float)(m_height > 0 ? m_height : 720);
}

void VideoFileSource::getSize(int& w, int& h) const
{
    w = m_width;
    h = m_height;
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> VideoFileSource::getTexture(
    ID3D11DeviceContext* ctx)
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    if (!ctx || m_width <= 0 || m_height <= 0 || m_pixelBuffer.empty())
        return m_srv;

    if (m_uploadPending)
    {
        if (!m_texture || !m_srv)
        {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = m_width;
            desc.Height = m_height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            // [Fix] 明确使用 BGRA 格式，与 ffmpeg 转换一致
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = 0;

            ID3D11Device* dev = nullptr;
            ctx->GetDevice(&dev);
            if (dev)
            {
                D3D11_SUBRESOURCE_DATA initData = {};
                initData.pSysMem = m_pixelBuffer.data();
                initData.SysMemPitch = m_width * 4;

                HRESULT hr =
                    dev->CreateTexture2D(&desc, &initData, m_texture.ReleaseAndGetAddressOf());
                if (SUCCEEDED(hr))
                {
                    dev->CreateShaderResourceView(
                        m_texture.Get(), nullptr, m_srv.ReleaseAndGetAddressOf());
                }
                dev->Release();
            }
        }
        else
        {
            ctx->UpdateSubresource(
                m_texture.Get(), 0, nullptr, m_pixelBuffer.data(), m_width * 4, 0);
        }
        m_uploadPending = false;
    }
    return m_srv;
}
