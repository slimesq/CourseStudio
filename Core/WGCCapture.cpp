// WGCCapture.cpp
#include "WGCCapture.h"
#include "Logger.h"
#include "AppConfig.h"   // [新增]
#include "VideoUtils.h"  // [新增]
#include <inspectable.h>

namespace WGC = winrt::Windows::Graphics::Capture;
namespace WGD = winrt::Windows::Graphics::DirectX;
namespace WDD = winrt::Windows::Graphics::DirectX::Direct3D11;

using namespace winrt;

// IID 定义移动到了 VideoUtils，这里不再需要重复定义

WGCCapture::WGCCapture()
{
    // [新增] 从配置读取默认尺寸，避免 0x0
    auto sz = AppConfig::instance()->wgcFallbackResolution();
    m_lastW = sz.width();
    m_lastH = sz.height();
}
WGCCapture::~WGCCapture()
{
    stop();
}

void WGCCapture::setTargetWindow(HWND hwnd, ID3D11Device* device)
{
    if (!device || !hwnd)
        return;
    m_targetHwnd = hwnd;
    m_d3dDevice = device;
    m_isMonitorMode = false;

    RECT rect = {0};
    if (GetWindowRect(hwnd, &rect))
    {
        m_lastW = rect.right - rect.left;
        m_lastH = rect.bottom - rect.top;
        auto defSz = AppConfig::instance()->wgcFallbackResolution();
        if (m_lastW <= 0)
            m_lastW = defSz.width();
        if (m_lastH <= 0)
            m_lastH = defSz.height();
    }
    LOG_INFO("WGC Target Set: Window Mode. Init Size: " << m_lastW << "x" << m_lastH);
}

void WGCCapture::setTargetMonitor(HMONITOR hmonitor, ID3D11Device* device)
{
    if (!device || !hmonitor)
        return;
    m_targetMonitor = hmonitor;
    m_d3dDevice = device;
    m_isMonitorMode = true;

    MONITORINFO mi = {sizeof(mi)};
    if (GetMonitorInfo(hmonitor, &mi))
    {
        m_lastW = abs(mi.rcMonitor.right - mi.rcMonitor.left);
        m_lastH = abs(mi.rcMonitor.bottom - mi.rcMonitor.top);
    }
    auto defSz = AppConfig::instance()->wgcFallbackResolution();
    if (m_lastW <= 0)
        m_lastW = defSz.width();  // 1920
    if (m_lastH <= 0)
        m_lastH = defSz.height();  // 1080
    LOG_INFO("WGC Target Set: Monitor Mode. Init Size: " << m_lastW << "x" << m_lastH);
}

bool WGCCapture::start()
{
    if (m_isCapturing)
        return true;
    if (!m_d3dDevice)
    {
        LOG_ERROR("WGC Start Failed: D3D Device is null");
        return false;
    }
    return startSession();
}

bool WGCCapture::startSession()
{
    // [Fix 1] 必须在 try 块之外检查设备，防止空指针
    if (!m_d3dDevice)
    {
        LOG_ERROR("WGC Start Failed: D3D Device is null");
        return false;
    }

    try
    {
        // 1. 获取 WinRT 激活工厂
        auto activationFactory = winrt::get_activation_factory<WGC::GraphicsCaptureItem>();
        auto interopFactory = activationFactory.as<IGraphicsCaptureItemInterop>();

        m_item = nullptr;
        HRESULT hr = E_FAIL;

        // 2. 创建 CaptureItem (窗口或显示器)
        if (m_isMonitorMode)
        {
            hr = interopFactory->CreateForMonitor(
                m_targetMonitor,
                winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
                winrt::put_abi(m_item));
        }
        else
        {
            // [Fix 2] 二次检查窗口句柄有效性
            if (!IsWindow(m_targetHwnd))
            {
                LOG_WARN("WGC Start Failed: Invalid Window Handle (Target window closed?)");
                return false;
            }
            hr = interopFactory->CreateForWindow(
                m_targetHwnd,
                winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
                winrt::put_abi(m_item));
        }

        if (FAILED(hr) || !m_item)
        {
            // [Fix 3] 详细输出 HRESULT 以便调试
            LOG_ERROR("WGC Create Item Failed: HR=" << std::hex << hr);
            return false;
        }

        // [Critical Fix 4] 致命崩溃点修复：检查 Item 尺寸
        // 如果目标窗口最小化，Size 会变成 (0,0)，此时创建 FramePool 会直接引发崩溃性异常
        auto size = m_item.Size();
        if (size.Width <= 0 || size.Height <= 0)
        {
            LOG_WARN(
                "WGC Start Aborted: Target size is invalid (Width/Height <= 0). Target might be "
                "minimized.");
            return false;
        }

        // 3. 准备 Direct3D 设备 (WinRT Wrapper)
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        HRESULT hrDxgi = m_d3dDevice.As(&dxgiDevice);
        if (FAILED(hrDxgi))
        {
            LOG_ERROR("WGC Start Failed: Cannot get IDXGIDevice");
            return false;
        }
        auto winrtDevice = VideoUtils::CreateDirect3DDevice(dxgiDevice.Get());

        // 4. 创建 FramePool
        // 使用 CreateFreeThreaded 以获得更好的性能，并防止 UI 线程阻塞
        m_framePool = WGC::Direct3D11CaptureFramePool::CreateFreeThreaded(
            winrtDevice, WGD::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);

        // 注册帧回调
        m_framePool.FrameArrived({this, &WGCCapture::onFrameArrived});

        // 5. 创建 Session [这是最容易崩的一行]
        m_session = m_framePool.CreateCaptureSession(m_item);

        // 可选：是否捕获鼠标
        // m_session.IsCursorCaptureEnabled(false);

        m_session.StartCapture();
        m_isCapturing = true;

        // 更新尺寸记录
        m_lastW = size.Width;
        m_lastH = size.Height;
        m_srvCache.clear();

        LOG_INFO("WGC Session Started. Size: " << m_lastW << "x" << m_lastH);
    }
    catch (winrt::hresult_error const& ex)
    {
        // [Fix 5] 捕获 WinRT 特有异常 (如 0x80070057 E_INVALIDARG)
        LOG_ERROR("WGC Start Exception (WinRT): 0x" << std::hex << ex.code() << " - "
                                                    << winrt::to_string(ex.message()));
        stop();  // 确保清理状态
        return false;
    }
    catch (std::exception const& ex)
    {
        LOG_ERROR("WGC Start Std Exception: " << ex.what());
        stop();
        return false;
    }
    catch (...)
    {
        LOG_ERROR("WGC Unknown Exception during Start");
        stop();
        return false;
    }
    return true;
}

void WGCCapture::stop()
{
    if (!m_isCapturing)
        return;

    try
    {
        if (m_session)
        {
            m_session.Close();
            m_session = nullptr;
        }
        if (m_framePool)
        {
            m_framePool.Close();
            m_framePool = nullptr;
        }
    }
    catch (...)
    {
    }
    m_item = nullptr;
    m_isCapturing = false;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_currentSRV.Reset();
    m_srvCache.clear();
    m_hasNewFrame = false;
    LOG_INFO("WGC Session Stopped.");
}

extern "C"
{
    // VideoUtils 中已经不需要导出这个IID，这里为了编译通过，使用 VideoUtils 的引用
    struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1")) IDirect3DDxgiInterfaceAccess
        : ::IUnknown
    {
        virtual HRESULT __stdcall GetInterface(const GUID& iid, void** p) = 0;
    };
}

void WGCCapture::onFrameArrived(WGC::Direct3D11CaptureFramePool const& sender,
                                winrt::Windows::Foundation::IInspectable const&)
{
    try
    {
        auto frame = sender.TryGetNextFrame();
        if (!frame)
            return;

        auto surface = frame.Surface();
        auto access = surface.as<IDirect3DDxgiInterfaceAccess>();
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        HRESULT hr = access->GetInterface(IID_PPV_ARGS(&texture));

        if (SUCCEEDED(hr) && texture && m_d3dDevice)
        {
            D3D11_TEXTURE2D_DESC tDesc;
            texture->GetDesc(&tDesc);
            m_lastW = tDesc.Width;
            m_lastH = tDesc.Height;

            std::lock_guard<std::mutex> lock(m_mutex);

            auto it = m_srvCache.find(texture.Get());
            if (it != m_srvCache.end())
            {
                m_currentSRV = it->second;
            }
            else
            {
                D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
                desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                desc.Texture2D.MipLevels = 1;

                Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> newSrv;
                HRESULT createHr =
                    m_d3dDevice->CreateShaderResourceView(texture.Get(), &desc, &newSrv);
                if (SUCCEEDED(createHr) && newSrv)
                {
                    m_srvCache[texture.Get()] = newSrv;
                    m_currentSRV = newSrv;
                }
                else
                {
                    LOG_WARN("WGC Create SRV Failed: "
                             << VideoUtils::HResultToString(createHr).toStdString());
                }
            }

            m_hasNewFrame = true;
        }
    }
    catch (...)
    {
        // 忽略帧到达时的异常，避免 crash
    }
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> WGCCapture::getTexture(
    ID3D11DeviceContext* /*ctx*/)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentSRV;
}

void WGCCapture::getSize(int& w, int& h) const
{
    w = m_lastW;
    h = m_lastH;
}
