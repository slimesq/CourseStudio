// WGCCapture.h
#pragma once
#define NOMINMAX
#include <d3d11.h>
#include <dxgi1_2.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <wrl/client.h>
#include <QString>
#include <vector>
#include <atomic>
#include <mutex>
#include <map>
#include "IVideoSource.h"
#include "VideoUtils.h"  // [新增] 包含 WindowInfo, MonitorInfo

class WGCCapture : public IVideoSource
{
public:
    WGCCapture();
    ~WGCCapture() override;

    // 静态方法现在委托给 VideoUtils，但为了保持接口，这里做转发
    static std::vector<WindowInfo> getCapturableWindows()
    {
        return VideoUtils::GetCapturableWindows();
    }
    static std::vector<MonitorInfo> getCapturableMonitors()
    {
        return VideoUtils::GetCapturableMonitors();
    }

    void setTargetWindow(HWND hwnd, ID3D11Device* device);
    void setTargetMonitor(HMONITOR hmonitor, ID3D11Device* device);

    bool start() override;
    void stop() override;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> getTexture(ID3D11DeviceContext* ctx) override;

    void getSize(int& w, int& h) const override;
    QString getDeviceName() const override
    {
        return "WGC Source";
    }

    bool hasNewFrame() const override
    {
        return m_hasNewFrame;
    }
    void markFrameProcessed() override
    {
        m_hasNewFrame = false;
    }

private:
    void onFrameArrived(winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender,
                        winrt::Windows::Foundation::IInspectable const& args);

    bool startSession();

private:
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_framePool{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session{nullptr};

    Microsoft::WRL::ComPtr<ID3D11Device> m_d3dDevice;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_currentSRV;

    std::atomic<bool> m_isCapturing{false};
    std::mutex m_mutex;

    int m_lastW = 0;
    int m_lastH = 0;

    HWND m_targetHwnd = nullptr;
    HMONITOR m_targetMonitor = nullptr;
    bool m_isMonitorMode = false;

    std::atomic<bool> m_hasNewFrame{false};

    std::map<ID3D11Texture2D*, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> m_srvCache;
};
