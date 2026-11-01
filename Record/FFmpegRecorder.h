#ifndef FFMPEGRECORDER_H
#define FFMPEGRECORDER_H

#include <QObject>
#include <QThread>
#include <QImage>
#include <QMutex>
#include <QQueue>
#include <atomic>
#include <memory>
#include <vector>
#include <d3d11.h>
#include <wrl/client.h>
#include "RecorderConfig.h"
#include "AdaptiveStreamController.h"

// --- 前置声明 ---
class VideoEncoder;
class AudioEncoder;
class BaseMuxer;
class AudioDSP;
class StreamSender;
struct AVFrame;
struct AVPacket;

struct VideoFrameData
{
    bool isRaw = false;
    QImage image;
    std::vector<uint8_t> rawData;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    int width = 0;
    int height = 0;
    qint64 debug_arrival_time = 0;
};

class FFmpegRecorder : public QThread
{
    Q_OBJECT
public:
    explicit FFmpegRecorder(QObject* parent = nullptr);
    ~FFmpegRecorder();

    void setD3DDevice(void* device);

    bool startRecord(RecorderConfig const& config);
    void stopRecord();

    void updateAudioConfig(bool mixMic, bool mixSystem);
    void setAudioVolume(int sourceId, float volume);
    void setSavePath(QString const& path);
    qint64 getRecordingDuration() const;

public slots:
    void pushVideoFrame(QImage const& img, qint64 timestamp);
    void pushNV12Frame(std::vector<uint8_t> const& data, int width, int height, qint64 pts);
    void pushTextureFrame(void* texture, int width, int height, qint64 pts);
    void pushAudioData(int sourceId, QByteArray const& data, int sampleRate, int channels);

signals:
    void frameProcessed();
    void errorOccurred(QString msg);

    // [P5] 状态反馈信号
    void statsUpdated(StreamStats stats);
    void stateChanged(RecorderState state);

protected:
    void run() override;

private:
    void initNetwork();
    void cleanUp();
    void processVideo(VideoFrameData const& data);
    void processAudio(bool isFadingOut);

    void emitPacket(AVPacket* pkt, bool isVideo);
    void encodeAndWriteVideo(AVFrame* frame);
    void checkNetworkHealth();
    bool downloadTexture(ID3D11Texture2D* tex,
                         std::vector<uint8_t>& outData,
                         int width,
                         int height);

    bool initScaler(int inW, int inH, int inFmt, int outW, int outH, int outFmt);
    // [P5] 统计计算函数
    void updateStats();

    // 动态调整 x264 码率的辅助函数
    void updateEncoderBitrate(int bitrate);
    void checkFileSplit(bool isKeyFrame, int64_t currentVideoPts);

private:
    RecorderConfig m_config;
    std::atomic<bool> m_isRecording;
    std::atomic<bool> m_softStopRequested;

    Microsoft::WRL::ComPtr<ID3D11Device> m_d3dDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_d3dContext;

    int64_t m_videoPtsCounter = 0;
    int64_t m_totalAudioSamples = 0;
    int64_t m_droppedFrames = 0;
    int64_t m_duplicatedFrames = 0;

    int64_t m_currentBitrate = 0;
    int m_healthCheckCounter = 0;

    std::atomic<int64_t> m_currentDuration{0};

    QQueue<VideoFrameData> m_videoQueue;
    QMutex m_videoMutex;
    QMutex m_configMutex;
    QString m_nextSavePath;

    std::unique_ptr<VideoEncoder> m_vEncoder;
    std::unique_ptr<AudioEncoder> m_aEncoder;
    std::unique_ptr<AudioDSP> m_audioDSP;

    std::unique_ptr<StreamSender> m_sender;

    float* m_mixL = nullptr;
    float* m_mixR = nullptr;
    int m_mixBufferSize = 0;
    AVFrame* m_aFrame = nullptr;

    AVFrame* m_lastEncodedFrame = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_stagingTexture;

    struct SwsContext* m_swsCtx = nullptr;
    AVFrame* m_scaledFrame = nullptr;  // 用于复用缩放后的帧内存

    // [P5] 统计用辅助变量
    qint64 m_lastStatsTime = 0;
    uint64_t m_lastBytesSent = 0;
    int64_t m_lastFrameCount = 0;

    // 自适应控制器
    AdaptiveStreamController* m_adaptiveCtrl = nullptr;
    long long m_frameIdx = 0;
    QElapsedTimer m_elapsedTimer;

    int m_currentSplitIndex = 0;
    qint64 m_lastSplitTime = 0;
};

#endif  // FFMPEGRECORDER_H
