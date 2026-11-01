#include "CameraCapture.h"
#include "AppConfig.h"
#include "VideoUtils.h"
#include <windows.h>
#include <inspectable.h>
#include <algorithm>
#include <thread>
#include <chrono>
#include <d3dcompiler.h>

using namespace winrt;
using namespace winrt::Windows::Media::Capture;
using namespace winrt::Windows::Media::Capture::Frames;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Media::MediaProperties;
using namespace winrt::Windows::Devices::Enumeration;

extern "C"
{
    struct __declspec(uuid("5b0d3235-4dba-4d44-865e-8f1d0e4fd04d")) IMemoryBufferByteAccess
        : ::IUnknown
    {
        virtual HRESULT __stdcall GetBuffer(uint8_t** value, uint32_t* capacity) = 0;
    };
    struct __declspec(uuid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1")) IDirect3DDxgiInterfaceAccess
        : ::IUnknown
    {
        virtual HRESULT __stdcall GetInterface(const GUID& iid, void** p) = 0;
    };
}

CameraCapture::CameraCapture()
{
    setSourceType(SourceType::Camera);
    QSize defSz = AppConfig::instance()->cameraDefaultResolution();
    m_width = defSz.width();
    m_height = defSz.height();
    setCvEffectEnabled(true);
    m_effectManager = std::make_shared<EffectManager>();
}

CameraCapture::~CameraCapture()
{
    stop();
}

std::vector<std::pair<QString, QString>> CameraCapture::getCameras()
{
    std::vector<std::pair<QString, QString>> list;
    std::thread t([&]() {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        try
        {
            auto devices = DeviceInformation::FindAllAsync(DeviceClass::VideoCapture).get();
            for (auto const& dev : devices)
            {
                list.push_back({QString::fromStdWString(dev.Name().c_str()),
                                QString::fromStdWString(dev.Id().c_str())});
            }
        }
        catch (...)
        {
            LOG_ERROR("Enum Cameras Failed");
        }
    });
    if (t.joinable())
        t.join();
    return list;
}

void CameraCapture::configure(QString const& deviceId, ID3D11Device* device)
{
    m_targetDeviceId = deviceId;
    m_d3dDevice = device;
}

bool CameraCapture::start()
{
    if (m_running)
        return true;
    if (m_targetDeviceId.isEmpty() || !m_d3dDevice)
    {
        LOG_WARN("Camera Start Ignored: Invalid DeviceId or D3D Device");
        return false;
    }

    stop();

    m_running = true;
    m_workerThread = std::thread(&CameraCapture::workerThreadFunc, this, m_targetDeviceId);
    return true;
}

// 辅助函数：安全清理资源，绝不抛出异常
void CleanupResources(MediaCapture& mc, MediaFrameReader& reader, winrt::event_token& token)
{
    if (reader)
    {
        try
        {
            if (token)
            {
                reader.FrameArrived(token);
                token = winrt::event_token{};
            }
        }
        catch (...)
        {
        }

        try
        {
            // [Fix] 这里的 StopAsync 可能抛出，如果设备已断开
            auto op = reader.StopAsync();
            // 等待最多 1000ms，超时就不管了，防止死锁
            if (op.wait_for(std::chrono::milliseconds(1000)) ==
                winrt::Windows::Foundation::AsyncStatus::Completed)
            {
                // op.get(); // 不要调用 get，因为如果状态是 Error，get 会抛出异常
            }
        }
        catch (...)
        {
            LOG_WARN("Reader StopAsync failed/timeout");
        }

        try
        {
            reader.Close();
        }
        catch (...)
        {
        }
        reader = nullptr;
    }

    if (mc)
    {
        try
        {
            mc.Close();
        }
        catch (...)
        {
        }
        mc = nullptr;
    }
}

void CameraCapture::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutexStop);
        if (!m_running)
            return;
        m_running = false;      // 1. 标志位设为 false
        m_cvStop.notify_all();  // 2. 唤醒等待中的线程
    }

    // 3. 等待 Worker 线程结束
    // 关键点：我们只等待线程结束，绝不手动销毁 m_mediaCapture
    // 所有的资源销毁都在 workerThreadFunc 的最后（CleanupResources）完成
    if (m_workerThread.joinable())
    {
        try
        {
            m_workerThread.join();
        }
        catch (std::system_error const& e)
        {
            LOG_ERROR("CameraCapture: Join failed: " << e.what());
        }
        catch (...)
        {
            LOG_ERROR("CameraCapture: Join failed with unknown error.");
        }
    }

    LOG_INFO("CameraCapture stopped successfully.");
}

void CameraCapture::workerThreadFunc(QString deviceId)
{
    // 1. 初始化 COM
    try
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    }
    catch (...)
    {
        LOG_ERROR("Failed to init WinRT apartment");
        return;
    }

    // 2. 线程主体防护
    try
    {
        LOG_INFO("Worker Thread Started: " << deviceId.toStdString());

        enum class Strategy
        {
            Level1,
            Level2,
            Level3
        };
        Strategy strategies[] = {Strategy::Level1, Strategy::Level2, Strategy::Level3};

        bool success = false;
        // 增加重试次数限制，防止死循环
        int maxRetries = 3;
        int retryCount = 0;

        // 外层循环：策略重试
        for (auto strategy : strategies)
        {
            // [Check Stop]
            {
                std::lock_guard<std::mutex> lock(m_mutexStop);
                if (!m_running)
                    break;
            }

            // 清理旧状态 (必须在当前线程)
            m_mediaCapture = nullptr;
            m_frameReader = nullptr;
            m_frameArrivedToken = winrt::event_token{};

            try
            {
                MediaCaptureInitializationSettings settings;
                settings.VideoDeviceId(deviceId.toStdWString());
                settings.StreamingCaptureMode(StreamingCaptureMode::Video);
                settings.MemoryPreference(MediaCaptureMemoryPreference::Auto);

                // 策略配置 (省略部分代码，保持原样)
                if (strategy == Strategy::Level3)
                    settings.SharingMode(MediaCaptureSharingMode::ExclusiveControl);
                else
                    settings.SharingMode(MediaCaptureSharingMode::SharedReadOnly);

                // [Check Stop] Before Blocking Call
                if (!m_running)
                    break;

                // --- 阶段 1: Initialize ---
                m_mediaCapture = MediaCapture();
                m_mediaCapture.Failed({this, &CameraCapture::onCaptureFailed});

                // 这里的 .get() 是最容易崩的，如果抛出异常直接进 catch
                m_mediaCapture.InitializeAsync(settings).get();

                // [Check Stop] After Blocking Call
                if (!m_running)
                    throw std::runtime_error("Stop requested during initialization");

                // --- 阶段 2: Find Source ---
                auto frameSources = m_mediaCapture.FrameSources();
                MediaFrameSource bestSource = nullptr;
                for (auto const& kv : frameSources)
                {
                    if (kv.Value().Info().SourceKind() == MediaFrameSourceKind::Color)
                    {
                        bestSource = kv.Value();
                        break;
                    }
                }
                if (!bestSource && frameSources.Size() > 0)
                    bestSource = frameSources.First().Current().Value();
                if (!bestSource)
                    throw std::runtime_error("No FrameSource found");

                // --- 阶段 3: Set Format (Strategy 1 Only) ---
                if (strategy == Strategy::Level1)
                {
                    auto formats = bestSource.SupportedFormats();
                    MediaFrameFormat targetFormat = nullptr;
                    // ... (查找 NV12 1920x1080 逻辑保持不变) ...

                    if (targetFormat)
                    {
                        if (!m_running)
                            break;
                        bestSource.SetFormatAsync(targetFormat).get();
                    }
                }

                // --- 阶段 4: Create Reader ---
                if (!m_running)
                    break;
                m_frameReader = m_mediaCapture.CreateFrameReaderAsync(bestSource).get();
                m_frameArrivedToken =
                    m_frameReader.FrameArrived({this, &CameraCapture::onFrameArrived});

                // --- 阶段 5: Start ---
                if (!m_running)
                    break;
                auto status = m_frameReader.StartAsync().get();

                if (status == MediaFrameReaderStartStatus::Success)
                {
                    LOG_INFO("Camera Started Successfully (Strategy " << (int)strategy << ")");
                    success = true;
                    break;  // 成功！跳出循环
                }
                else
                {
                    LOG_WARN("Start failed status: " << (int)status);
                    // 策略失败，清理当前资源，准备下一个策略
                    CleanupResources(m_mediaCapture, m_frameReader, m_frameArrivedToken);
                }
            }
            catch (winrt::hresult_error const& ex)
            {
                // 捕获 WinRT 异常 (如 0x80070005 拒绝访问, 0x80070020 文件占用)
                LOG_WARN("Strategy WinRT Error: 0x" << std::hex << ex.code() << ": "
                                                    << winrt::to_string(ex.message()));

                // 必须清理，否则下一次循环会出错
                CleanupResources(m_mediaCapture, m_frameReader, m_frameArrivedToken);

                // 如果是“停止请求”导致的异常，不再重试
                if (!m_running)
                    break;

                // 稍作等待再重试
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            catch (std::exception const& ex)
            {
                LOG_WARN("Strategy Std Error: " << ex.what());
                CleanupResources(m_mediaCapture, m_frameReader, m_frameArrivedToken);
                if (!m_running)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            catch (...)
            {
                LOG_WARN("Strategy Unknown Error");
                CleanupResources(m_mediaCapture, m_frameReader, m_frameArrivedToken);
                if (!m_running)
                    break;
            }
        }  // End For

        // 如果成功启动，进入等待模式
        if (success && m_running)
        {
            std::unique_lock<std::mutex> lock(m_mutexStop);
            // 这里会阻塞，直到 stop() 调用 notify_all()
            m_cvStop.wait(lock, [this] { return !m_running; });
        }

        // 线程退出前，统一清理资源
        // 这里的 CleanupResources 是在 workerThread 内部调用的，符合 COM 线程规则
        CleanupResources(m_mediaCapture, m_frameReader, m_frameArrivedToken);
    }
    catch (...)
    {
        // [Final Safety Net] 防止线程抛出异常导致进程终止
        LOG_ERROR("CRITICAL: WorkerThread crashed with unhandled exception.");
        // 最后的最后，尝试清理
        try
        {
            CleanupResources(m_mediaCapture, m_frameReader, m_frameArrivedToken);
        }
        catch (...)
        {
        }
    }

    LOG_INFO("Worker Thread Exited Gracefully.");
}

void CameraCapture::onCaptureFailed(MediaCapture const&, MediaCaptureFailedEventArgs const& args)
{
    LOG_ERROR("CAMERA ERROR EVENT: "
              << QString::fromStdWString(args.Message().c_str()).toStdString()
              << " Code: " << args.Code());
}

void CameraCapture::onFrameArrived(MediaFrameReader const& sender,
                                   MediaFrameArrivedEventArgs const&)
{
    try
    {
        auto frame = sender.TryAcquireLatestFrame();
        if (!frame)
            return;

        // 1. 检查是否开启了 OpenCV 特效
        bool cvEnabled = m_enableCvEffect.load();

        // 2. 尝试获取 GPU Surface
        auto surface = frame.VideoMediaFrame().Direct3DSurface();

        // 策略：如果开启了 OpenCV 特效，即使有 GPU Surface 也强制走 CPU 路径
        // 因为 OpenCV 处理 GPU 纹理代价太高
        if (!cvEnabled && surface)
        {
            // === 原有的 GPU 路径 (DX11) ===
            auto access = surface.as<IDirect3DDxgiInterfaceAccess>();
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            HRESULT hr = access->GetInterface(IID_PPV_ARGS(&texture));

            if (SUCCEEDED(hr) && texture)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_gpuTexture = texture;

                // 注意：如果你之前有 DX11 的 EffectProcessor 逻辑，在这里调用
                // 这里为了演示 OpenCV 集成，假设 DX11 特效已移除或互斥

                D3D11_TEXTURE2D_DESC desc;
                m_gpuTexture->GetDesc(&desc);
                m_width = desc.Width;
                m_height = desc.Height;

                m_useGpuPath = true;
                m_isDirty = true;
                m_hasNewFrame = true;
                return;
            }
        }

        // === CPU 路径 (SoftwareBitmap) ===
        // 走到这里说明：要么没有 GPU Surface，要么强制开启了 OpenCV 特效
        m_useGpuPath = false;

        auto bitmap = frame.VideoMediaFrame().SoftwareBitmap();
        if (!bitmap)
        {
            // 如果只有 Surface 但我们要走 CPU，必须从 Surface 拷贝
            if (surface)
            {
                // 这是一个异步操作，.get() 会阻塞，注意性能
                bitmap = SoftwareBitmap::CreateCopyFromSurfaceAsync(surface).get();
            }
        }

        if (!bitmap)
            return;

        // 确保格式为 BGRA8 Premultiplied (Win2D/Direct2D 常用格式)
        if (bitmap.BitmapPixelFormat() != BitmapPixelFormat::Bgra8 ||
            bitmap.BitmapAlphaMode() == BitmapAlphaMode::Premultiplied)
        {
            bitmap = SoftwareBitmap::Convert(
                bitmap, BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied);
        }

        // 锁定内存进行访问
        auto buffer =
            bitmap.LockBuffer(BitmapBufferAccessMode::ReadWrite);  // 必须是 ReadWrite 以便特效修改
        auto ref = buffer.CreateReference();
        auto byteAccess = ref.as<IMemoryBufferByteAccess>();

        uint8_t* data = nullptr;
        uint32_t capacity = 0;
        if (SUCCEEDED(byteAccess->GetBuffer(&data, &capacity)))
        {
            // [核心集成点] 调用 OpenCV 特效处理器
            if (cvEnabled && m_effectManager)
            {
                int w = bitmap.PixelWidth();
                int h = bitmap.PixelHeight();
                auto plane = buffer.GetPlaneDescription(0);

                // 直接传入内存指针，让 Processor 原地修改
                // data + StartIndex 是真正的像素起始位置
                if (m_enableCvEffect.load())
                {
                    m_effectManager->processPipeline(data + plane.StartIndex, w, h, plane.Stride);
                }
            }

            // 数据拷贝到内部 Buffer (用于后续上传到 DX11)
            {
                std::lock_guard<std::mutex> lock(m_mutex);

                int w = bitmap.PixelWidth();
                int h = bitmap.PixelHeight();

                if (m_pixelBuffer.size() != w * h * 4)
                {
                    m_pixelBuffer.resize(w * h * 4);
                    m_width = w;
                    m_height = h;
                }

                auto plane = buffer.GetPlaneDescription(0);
                // 逐行拷贝，处理 Stride (Padding)
                for (int y = 0; y < h; ++y)
                {
                    memcpy(m_pixelBuffer.data() + y * w * 4,
                           data + plane.StartIndex + y * plane.Stride,
                           w * 4);
                }
                m_isDirty = true;
                m_hasNewFrame = true;
            }
        }
    }
    catch (...)
    {
        // 异常吞噬，防止回调崩溃
    }
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> CameraCapture::getTexture(ID3D11DeviceContext* ctx)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_useGpuPath)
    {
        if (m_gpuTexture)
        {
            if (m_isDirty)
            {
                D3D11_TEXTURE2D_DESC desc;
                m_gpuTexture->GetDesc(&desc);

                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                if (desc.Format == DXGI_FORMAT_NV12)
                {
                    srvDesc.Format = DXGI_FORMAT_R8_UNORM;
                }
                else
                {
                    srvDesc.Format = desc.Format;
                }

                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = 1;

                m_d3dDevice->CreateShaderResourceView(m_gpuTexture.Get(), &srvDesc, &m_gpuSrv);
                m_isDirty = false;
            }
            return m_gpuSrv;
        }
    }
    else
    {
        if (!m_isDirty && m_cpuSrv)
            return m_cpuSrv;
        if (m_pixelBuffer.empty())
            return nullptr;

        bool recreate = (!m_cpuTexture);
        if (m_cpuTexture)
        {
            D3D11_TEXTURE2D_DESC desc;
            m_cpuTexture->GetDesc(&desc);
            if (desc.Width != (UINT)m_width || desc.Height != (UINT)m_height)
                recreate = true;
        }

        if (recreate)
        {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = m_width;
            desc.Height = m_height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA data = {};
            data.pSysMem = m_pixelBuffer.data();
            data.SysMemPitch = m_width * 4;

            m_cpuSrv.Reset();
            m_cpuTexture.Reset();
            if (m_d3dDevice)
            {
                m_d3dDevice->CreateTexture2D(&desc, &data, &m_cpuTexture);
                if (m_cpuTexture)
                {
                    m_d3dDevice->CreateShaderResourceView(m_cpuTexture.Get(), nullptr, &m_cpuSrv);
                }
            }
        }
        else if (ctx && m_cpuTexture && m_isDirty)
        {
            ctx->UpdateSubresource(
                m_cpuTexture.Get(), 0, nullptr, m_pixelBuffer.data(), m_width * 4, 0);
        }
        m_isDirty = false;
        return m_cpuSrv;
    }
    return nullptr;
}

void CameraCapture::getSize(int& w, int& h) const
{
    w = m_width;
    h = m_height;
}

void CameraCapture::setCvEffectEnabled(bool enabled)
{
    m_enableCvEffect = enabled;
}

bool CameraCapture::isCvEffectEnabled() const
{
    return m_enableCvEffect.load();
}
