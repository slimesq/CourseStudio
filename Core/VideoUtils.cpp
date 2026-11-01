// VideoUtils.cpp
#include "VideoUtils.h"
#include "Logger.h"
#include <dwmapi.h>
#include <inspectable.h>

using namespace winrt;

// COM 接口定义
extern "C"
{
    HRESULT __stdcall CreateDirect3D11DeviceFromDXGIDevice(IDXGIDevice* dxgiDevice,
                                                           IInspectable** graphicsDevice);
}

winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice VideoUtils::CreateDirect3DDevice(
    IDXGIDevice* dxgi_device)
{
    winrt::com_ptr<IInspectable> inspectable;
    HRESULT hr = CreateDirect3D11DeviceFromDXGIDevice(dxgi_device, inspectable.put());
    if (FAILED(hr))
    {
        LOG_ERROR("CreateDirect3D11DeviceFromDXGIDevice Failed: " << std::hex << hr);
        throw winrt::hresult_error(hr);
    }
    return inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
}

bool VideoUtils::IsValidCaptureWindow(HWND hwnd)
{
    if (!IsWindowVisible(hwnd))
        return false;
    if (IsIconic(hwnd))
        return false;

    // 排除特定系统窗口
    char title[256];
    GetWindowTextA(hwnd, title, 256);
    if (strlen(title) == 0)
        return false;

    QString t = QString::fromLocal8Bit(title);

    // 从 AppConfig 读取过滤列表会更好，这里暂时保留硬编码逻辑，实际可扩展
    if (t == "Control Panel" || t == "DX11 Render Canvas" || t == "Output Preview" ||
        t == "Program Manager")
        return false;

    return true;
}

std::vector<WindowInfo> VideoUtils::GetCapturableWindows()
{
    std::vector<WindowInfo> list;
    EnumWindows(
        [](HWND hwnd, LPARAM lParam) -> BOOL {
            if (VideoUtils::IsValidCaptureWindow(hwnd))
            {
                char title[256];
                GetWindowTextA(hwnd, title, 256);
                auto* vec = (std::vector<WindowInfo>*)lParam;
                vec->push_back({hwnd, QString::fromLocal8Bit(title)});
            }
            return TRUE;
        },
        (LPARAM)&list);
    return list;
}

std::vector<MonitorInfo> VideoUtils::GetCapturableMonitors()
{
    std::vector<MonitorInfo> list;
    EnumDisplayMonitors(
        nullptr,
        nullptr,
        [](HMONITOR hMonitor, HDC, LPRECT lprcMonitor, LPARAM dwData) -> BOOL {
            auto* vec = (std::vector<MonitorInfo>*)dwData;
            int index = (int)vec->size() + 1;
            int w = lprcMonitor->right - lprcMonitor->left;
            int h = lprcMonitor->bottom - lprcMonitor->top;
            QString name = QString("Display %1 (%2x%3)").arg(index).arg(w).arg(h);
            vec->push_back({hMonitor, name, *lprcMonitor});
            return TRUE;
        },
        (LPARAM)&list);
    return list;
}

QString VideoUtils::HResultToString(HRESULT hr)
{
    return QString("0x%1").arg(QString::number((uint32_t)hr, 16).toUpper());
}
