#pragma once
#include "IVideoSource.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

class VideoFileSource : public IVideoSource
{
public:
    VideoFileSource();
    ~VideoFileSource() override;

    bool openFile(QString const& filePath);
    void setLoop(bool loop);

    bool start() override;
    void stop() override;

    QString getDeviceName() const override
    {
        return "Video Player";
    }

    // 基础尺寸接口
    float getBaseWidth() const override;
    float getBaseHeight() const override;
    void getSize(int& w, int& h) const override;

    bool hasNewFrame() const override
    {
        return m_hasNewFrame;
    }
    void markFrameProcessed() override
    {
        m_hasNewFrame = false;
    }

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> getTexture(ID3D11DeviceContext* ctx) override;

private:
    void decodeLoop();
    void cleanupFFmpeg();

private:
    QString m_filePath;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_loop{true};
    std::thread m_decodeThread;

    AVFormatContext* m_fmtCtx = nullptr;
    AVCodecContext* m_codecCtx = nullptr;
    SwsContext* m_swsCtx = nullptr;
    AVFrame* m_frame = nullptr;
    int m_videoStreamIndex = -1;

    std::mutex m_bufferMutex;
    std::vector<uint8_t> m_pixelBuffer;

    int m_width = 0;
    int m_height = 0;

    bool m_uploadPending = false;
    bool m_hasNewFrame = false;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
};
