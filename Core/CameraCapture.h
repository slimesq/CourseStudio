// CameraCapture.h
#pragma once
#define NOMINMAX
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <mutex>
#include <vector>
#include <atomic>
#include <thread>
#include <condition_variable>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Media.Capture.h>
#include <winrt/Windows.Media.Capture.Frames.h>
#include <winrt/Windows.Media.MediaProperties.h>
#include <winrt/Windows.Graphics.Imaging.h>

#include <QString>
#include "Logger.h"
#include "IVideoSource.h"
#include "VideoEffectSystem.h"

class CameraCapture : public IVideoSource
{
public:
    CameraCapture();
    ~CameraCapture() override;

    static std::vector<std::pair<QString, QString>> getCameras();

    void configure(QString const& deviceId, ID3D11Device* device);

    bool start() override;
    void stop() override;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> getTexture(ID3D11DeviceContext* ctx) override;

    void getSize(int& w, int& h) const override;
    QString getDeviceName() const override
    {
        return "Camera Source";
    }

    bool hasNewFrame() const override
    {
        return m_hasNewFrame;
    }
    void markFrameProcessed() override
    {
        m_hasNewFrame = false;
    }

    void setCvEffectEnabled(bool enabled = false);
    bool isCvEffectEnabled() const;
    std::shared_ptr<EffectManager> getEffectManager() const
    {
        return m_effectManager;
    }

private:
    void onFrameArrived(
        winrt::Windows::Media::Capture::Frames::MediaFrameReader const& sender,
        winrt::Windows::Media::Capture::Frames::MediaFrameArrivedEventArgs const& args);

    void onCaptureFailed(winrt::Windows::Media::Capture::MediaCapture const& sender,
                         winrt::Windows::Media::Capture::MediaCaptureFailedEventArgs const& args);

    void workerThreadFunc(QString deviceId);

private:
    Microsoft::WRL::ComPtr<ID3D11Device> m_d3dDevice;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_cpuTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_cpuSrv;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_gpuTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_gpuSrv;
    bool m_useGpuPath = false;

    winrt::Windows::Media::Capture::MediaCapture m_mediaCapture{nullptr};
    winrt::Windows::Media::Capture::Frames::MediaFrameReader m_frameReader{nullptr};

    std::mutex m_mutex;
    std::vector<uint8_t> m_pixelBuffer;
    int m_width = 1280;
    int m_height = 720;
    std::atomic<bool> m_isDirty{false};
    std::atomic<bool> m_hasNewFrame{false};

    std::atomic<bool> m_running{false};
    std::thread m_workerThread;
    std::condition_variable m_cvStop;
    std::mutex m_mutexStop;

    QString m_targetDeviceId;

    std::mutex m_lifecycleMutex;

    winrt::event_token m_frameArrivedToken;
    std::shared_ptr<EffectManager> m_effectManager;

    std::atomic<bool> m_enableCvEffect{true};
};
