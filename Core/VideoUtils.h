// VideoUtils.h
#pragma once
#define NOMINMAX
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <QString>
#include <vector>

// 窗口/显示器信息结构体定义移动到这里，方便复用
struct WindowInfo
{
    HWND hwnd;
    QString title;
};

struct MonitorInfo
{
    HMONITOR hmonitor;
    QString name;
    RECT rect;
};

class VideoUtils
{
public:
    // --- Direct3D & WinRT 互操作 ---
    // 从 DXGI Device 创建 WinRT IDirect3DDevice
    static winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice CreateDirect3DDevice(
        IDXGIDevice* dxgi_device);

    // --- 窗口与显示器工具 ---
    // 判断窗口是否为有效的捕获目标
    static bool IsValidCaptureWindow(HWND hwnd);

    // 获取所有可捕获的窗口
    static std::vector<WindowInfo> GetCapturableWindows();

    // 获取所有显示器
    static std::vector<MonitorInfo> GetCapturableMonitors();

    // --- 字符串/辅助 ---
    static QString HResultToString(HRESULT hr);
};
