#include "SceneCompositor.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <QDebug>
#include <QDateTime>
#include <objbase.h>  // [新增] for CoInitializeEx
#include <QtConcurrent>
#include "SharedMemoryPublisher.h"
#include "AppConfig.h"
#include "SceneItem.h"
#include <QElapsedTimer>

// 成员变量建议放在头文件，这里用 static 模拟
static QElapsedTimer s_previewLimiter;

// 辅助函数：获取微秒级时间戳
static int64_t GetMicroseconds()
{
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (counter.QuadPart * 1000000) / freq.QuadPart;
}

SceneCompositor::SceneCompositor(std::shared_ptr<Scene> scene, QObject* parent)
    : QObject(parent), m_scene(scene)
{
    m_vcamPublisher = new SharedMemoryPublisher();
    connect(m_scene.get(), &Scene::contentChanged, this, &SceneCompositor::onSceneContentChanged);
    connect(m_scene.get(), &Scene::itemAdded, this, &SceneCompositor::onSceneContentChanged);
    connect(m_scene.get(), &Scene::itemRemoved, this, &SceneCompositor::onSceneContentChanged);
    connect(m_scene.get(), &Scene::canvasSizeChanged, this, &SceneCompositor::onCanvasSizeChanged);
    // [新增] 连接安全销毁信号 (QueuedConnection 确保跨线程安全传递)
    connect(m_scene.get(),
            &Scene::sourceAboutToBeDestroyed,
            this,
            &SceneCompositor::onSourceRecycle,
            Qt::QueuedConnection);
}

SceneCompositor::~SceneCompositor()
{
    stopRendering();
    if (m_vcamPublisher)
    {
        m_vcamPublisher->stop();
        delete m_vcamPublisher;
        m_vcamPublisher = nullptr;
    }
}

// [New] 切换工作模式
void SceneCompositor::setMode(CompositorMode mode)
{
    if (m_currentMode == mode)
        return;

    // 1. 预计算新模式下的目标分辨率
    // 临时切换一下 mode 变量来计算，算完切回来（或者传递参数）
    CompositorMode oldMode = m_currentMode;
    m_currentMode = mode;  // 暂时设置，用于计算

    int targetW, targetH;
    calculateRenderSize(targetW, targetH);

    // 2. 检查分辨率是否发生变化
    bool needResize = (targetW != m_canvasW || targetH != m_canvasH);

    // 恢复旧值（因为真正的切换在下面，这里只是预判）
    // 其实可以直接赋值 m_currentMode = mode，上面只是为了逻辑严谨

    int targetFps = 25;
    if (mode == CompositorMode::LiveRunning)
    {
        targetFps = AppConfig::instance()->getRecorderConfig().base.fps;
        if (targetFps <= 0)
            targetFps = 30;
    }

    qDebug() << "[Compositor] Mode Switch:" << (oldMode == CompositorMode::Editor ? "Edit" : "Live")
             << "->" << (mode == CompositorMode::LiveRunning ? "Live" : "Edit")
             << " | Res Change:" << needResize << "(" << targetW << "x" << targetH << ")";

    m_renderThread.updateInterval(targetFps);

    // 3. [关键] 如果分辨率变了，触发异步 Resize
    if (needResize)
    {
        // onCanvasSizeChanged 会设置 m_resizePending，在下一帧 renderLoop 中执行 recreateResources
        onCanvasSizeChanged(targetW, targetH);
    }
}

void SceneCompositor::startRendering(int fps)
{
    auto& dx11 = DX11Context::instance();
    calculateRenderSize(m_canvasW, m_canvasH);
    // 虚拟摄像头初始化 (注意：虚拟摄像头可能需要 COM，建议放在 renderLoop 初始化后或确保主线程已初始化)
    // 这里在主线程调用，只要主线程 COM 正常即可
    if (m_vcamPublisher)
    {
        if (!m_vcamPublisher->initialize(L"MyVirtualCamera", m_canvasW, m_canvasH))
        {
            qWarning() << "[Compositor] Virtual Camera Init Failed!";
        }
    }

    qDebug() << "[Compositor] Start Rendering. Mode:"
             << " Target Canvas:" << m_canvasW << "x" << m_canvasH;

    if (!dx11.createOffscreenCanvas(m_canvasW, m_canvasH, &m_mainRTV, &m_mainSRV, &m_mainTex))
    {
        qCritical() << "[Compositor] Failed to create Offscreen Canvas!";
        return;
    }

    dx11.createStagingTexture(m_canvasW, m_canvasH, &m_previewStagingTex);
    initNV12Resources(m_canvasW, m_canvasH);

    syncRenderNodes();
    m_startTimestamp = GetMicroseconds();
    m_frameCount = 0;

    // [Modified] 初始启动 FPS 根据当前模式决定，忽略传入参数或将其作为初始参考
    // 但通常 startRendering 时不知道是否直播，这里我们可以默认按 Editor 启动，后续由 LiveController 调整
    // 或者如果 fps 参数有意义，则使用它
    int actualFps = (m_currentMode == CompositorMode::LiveRunning) ? fps : 25;
    m_renderThread.start(actualFps, std::bind(&SceneCompositor::renderLoop, this));
}

void SceneCompositor::stopRendering()
{
    m_renderThread.stop();  // 停止线程

    // 释放资源
    m_mainRTV.Reset();
    m_mainSRV.Reset();
    m_mainTex.Reset();
    m_previewStagingTex.Reset();
    m_nv12Texture.Reset();
    m_rtvY.Reset();
    m_rtvUV.Reset();
    m_nv12StagingTex.Reset();

    qDebug() << "[Compositor] Stopped.";
}

void* SceneCompositor::getPreviewHandle()
{
    return (void*)m_mainSRV.Get();
}
void SceneCompositor::setVideoOutputCallback(VideoFrameCallback callback)
{
    m_outputCallback = callback;
}

void SceneCompositor::onSourceRecycle(std::shared_ptr<ISource> source)
{
    if (!source)
        return;
    std::lock_guard<std::mutex> lock(m_renderMutex);  // 复用 renderMutex 或使用新锁
    m_garbageBin.push_back(source);
}

void SceneCompositor::syncRenderNodes()
{
    std::lock_guard<std::mutex> lock(m_renderMutex);
    m_renderList.clear();
    auto items = m_scene->getItems();

    for (auto const& item : items)
    {
        auto source = item->getSource();
        if (source)
        {
            SourceType type = source->getType();
            BufferStrategyType strategy = BufferStrategyType::SwapChain;
            bool isDynamicSource = false;

            if (type == SourceType::Camera || type == SourceType::Video ||
                type == SourceType::Image)
            {
                isDynamicSource = true;
                if (type == SourceType::Camera || type == SourceType::Video)
                {
                    strategy = BufferStrategyType::RingBuffer;
                }
            }

            // [逻辑确认] 只有当 Source 有新帧时才更新 Pool
            // 这意味着如果 RawSourcePreview 抢先拿走了 NewFrame 标记，这里可能拿不到更新。
            // 但 TexturePool 应该缓存了上一帧，acquireTexture 仍能获取到旧画面，不会黑屏。
            if (isDynamicSource && source->hasNewFrame())
            {
                TextureHandle h = source->getTextureHandle();
                if (h)
                {
                    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
                    ID3D11ShaderResourceView* srv = (ID3D11ShaderResourceView*)h;
                    if (srv)
                    {
                        Microsoft::WRL::ComPtr<ID3D11Resource> res;
                        srv->GetResource(&res);
                        res.As(&tex);
                    }
                    if (tex)
                    {
                        TexturePool::instance().updateTexture(
                            source->getSourceId(), tex.Get(), strategy);
                        source->markFrameProcessed();
                    }
                }
            }

            RenderNode node;
            node.sourceId = source->getSourceId();
            Transform t = item->getTransform();
            node.x = t.x;
            node.y = t.y;
            node.w = t.width;
            node.h = t.height;
            node.rotation = t.rotation;
            node.zOrder = t.zOrder;
            node.opacity = 1.0f;
            m_renderList.push_back(node);
        }
    }
    std::sort(m_renderList.begin(),
              m_renderList.end(),
              [](RenderNode const& a, RenderNode const& b) { return a.zOrder < b.zOrder; });
}

void SceneCompositor::onSceneContentChanged()
{ /* Render loop auto-syncs */
}

void SceneCompositor::onCanvasSizeChanged(int w, int h)
{
    // 这里 w, h 是 Scene 传来的原始尺寸
    // 我们需要根据当前 Mode 再次计算一次实际渲染尺寸
    // 例如 Scene 变为了 1080x1920，如果是 Editor 模式，我们希望 Resize 到 540x960

    // 更新 Scene 自身记录的大小（如果有必要），或者直接重新计算
    // 由于 calculateRenderSize 依赖 m_scene->getCanvasRect()，确保 Scene 已经更新了 Rect

    int finalW, finalH;
    calculateRenderSize(finalW, finalH);

    if (m_pendingW == finalW && m_pendingH == finalH && m_canvasW == finalW && m_canvasH == finalH)
        return;

    qDebug() << "[Compositor] Scene Size Changed. Requesting Resize to:" << finalW << "x" << finalH;
    m_pendingW = finalW;
    m_pendingH = finalH;
    m_resizePending = true;
}

// 文件: SceneCompositor.cpp

void SceneCompositor::processGarbage()
{
    // 定义一个局部容器来接管要删除的资源
    std::vector<std::shared_ptr<ISource>> itemsToKill;
    {
        std::lock_guard<std::mutex> lock(m_renderMutex);
        if (m_garbageBin.empty())
            return;

        // 快速交换，缩短锁的持有时间
        itemsToKill.swap(m_garbageBin);
    }

    // [核心修复] 移除 QtConcurrent::run。
    // 改为在当前线程（渲染线程）同步销毁。
    // 原因：D3D资源的释放涉及底层设备上下文操作，如果在后台线程释放，
    // 而渲染线程正在使用 DeviceContext 绘图，会导致底层驱动崩溃 (Access Violation)。
    if (!itemsToKill.empty())
    {
        // itemsToKill 在这里析构，触发 source 的 release/stop
        itemsToKill.clear();
    }
}

void SceneCompositor::recreateResources()
{
    int w = m_pendingW;
    int h = m_pendingH;
    if (w <= 0 || h <= 0)
        return;
    qDebug() << "[Compositor] Performing Async Resize to:" << w << "x" << h;
    m_canvasW = w;
    m_canvasH = h;
    m_mainRTV.Reset();
    m_mainSRV.Reset();
    m_mainTex.Reset();
    m_previewStagingTex.Reset();
    m_nv12Texture.Reset();
    m_rtvY.Reset();
    m_rtvUV.Reset();
    m_nv12StagingTex.Reset();

    auto& dx11 = DX11Context::instance();
    dx11.createOffscreenCanvas(m_canvasW, m_canvasH, &m_mainRTV, &m_mainSRV, &m_mainTex);
    dx11.createStagingTexture(m_canvasW, m_canvasH, &m_previewStagingTex);
    initNV12Resources(m_canvasW, m_canvasH);
    if (m_vcamPublisher)
    {
        m_vcamPublisher->stop();
        m_vcamPublisher->initialize(L"MyVirtualCamera", m_canvasW, m_canvasH);
    }
}

void SceneCompositor::calculateRenderSize(int& outW, int& outH)
{
    // 1. 获取场景的逻辑尺寸 (这是唯一的真理)
    QRectF sceneRect = m_scene->getCanvasRect();
    m_logicalW = sceneRect.width();
    m_logicalH = sceneRect.height();
    if (m_logicalW <= 0)
        m_logicalW = 1920;
    if (m_logicalH <= 0)
        m_logicalH = 1080;

    int baseW = static_cast<int>(m_logicalW);
    int baseH = static_cast<int>(m_logicalH);

    // 防止无效尺寸
    if (baseW <= 0 || baseH <= 0)
    {
        baseW = 1920;
        baseH = 1080;
    }

    if (m_currentMode == CompositorMode::LiveRunning)
    {
        // [直播模式]：1:1 输出，保证最高画质
        outW = baseW;
        outH = baseH;
    }
    else
    {
        // [编辑模式]：降采样优化
        // 策略：限制长边最大为 1280 (720p级别)，保持纵横比
        int const MAX_DIM = 1280;

        if (baseW > MAX_DIM || baseH > MAX_DIM)
        {
            float ratio = (float)baseW / baseH;
            if (baseW >= baseH)
            {
                // 横屏
                outW = MAX_DIM;
                outH = static_cast<int>(outW / ratio);
            }
            else
            {
                // 竖屏 (9:16) -> 宽变小，高限制在 1280
                outH = MAX_DIM;
                outW = static_cast<int>(outH * ratio);
            }

            // 2字节对齐 (避免某些显卡/编码器对奇数分辨率过敏)
            if (outW % 2 != 0)
                outW++;
            if (outH % 2 != 0)
                outH++;
        }
        else
        {
            outW = baseW;
            outH = baseH;
        }
    }
}

void SceneCompositor::renderLoop()
{
    // [CRITICAL FIX] 初始化 COM 库
    // 许多 Windows 图形 API (WGC, Virtual Camera, Media Foundation) 在后台线程使用时必须初始化 COM
    static thread_local bool comInited = false;
    if (!comInited)
    {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr))
        {
            qCritical() << "[Compositor] CoInitializeEx failed in RenderThread! hr=" << hr;
        }
        else
        {
            qInfo() << "[Compositor] RenderThread COM Initialized.";
        }
        comInited = true;
    }

    std::lock_guard<std::mutex> lock(m_loopMutex);

    processGarbage();

    if (m_resizePending)
    {
        m_resizePending = false;
        recreateResources();
        syncRenderNodes();
    }

    m_frameCount++;
    syncRenderNodes();

    // [Optimization] 空场景短路逻辑
    // 如果渲染列表为空，直接发送清除信号并休眠，跳过后续昂贵的渲染流程
    {
        std::lock_guard<std::mutex> lock(m_renderMutex);
        if (m_renderList.empty())
        {
            // 只有当状态从"有内容"变为"无内容"时，才发送一次空帧以清除 UI 残留
            if (m_hasContentLastFrame)
            {
                emit frameReady(QImage());  // 发送空图片
                m_hasContentLastFrame = false;
                // qDebug() << "[Compositor] Scene empty, clearing UI and entering sleep mode.";
            }

            // 核心优化：直接休眠，跳过昂贵的 GPU 操作
            // 编辑模式下可以睡久一点 (40ms 对应约 25fps 的间隔，实际上会叠加线程本身的间隔)
            // 这会让线程处于低功耗等待状态
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            return;  // 直接返回，跳过 renderToTexture
        }
        m_hasContentLastFrame = true;
    }

    renderToTexture();  // 始终渲染 RGB (用于 UI 预览和虚拟摄像头)

    // [New] 性能优化分支
    if (m_currentMode == CompositorMode::LiveRunning)
    {
        // 直播模式：执行昂贵的 YUV 转换和 MapRead
        convertToYUVAndDownload();
    }

    downloadPreview();
    TexturePool::instance().cleanupUnused(QDateTime::currentMSecsSinceEpoch());
}

void SceneCompositor::renderToTexture()
{
    auto& dx11 = DX11Context::instance();
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    dx11.beginScene(m_mainRTV.Get(), clearColor);

    std::vector<RenderNode> localList;
    {
        std::lock_guard<std::mutex> lock(m_renderMutex);
        localList = m_renderList;
    }

    // [核心修复] 计算缩放系数
    // 渲染宽度 (m_canvasW) / 逻辑宽度 (m_logicalW)
    // 例如：1280 / 1920 = 0.666
    float scaleX = (float)m_canvasW / m_logicalW;
    float scaleY = (float)m_canvasH / m_logicalH;

    for (auto const& node : localList)
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        auto poolTexComPtr = TexturePool::instance().acquireTexture(node.sourceId);
        if (poolTexComPtr)
        {
            tex = poolTexComPtr;
        }

        if (tex)
        {
            D3D11_TEXTURE2D_DESC desc;
            tex->GetDesc(&desc);
            if (desc.Width == 0 || desc.Height == 0)
                continue;

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = desc.Format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;

            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> tempSRV;
            HRESULT hr = dx11.device()->CreateShaderResourceView(tex.Get(), &srvDesc, &tempSRV);

            if (SUCCEEDED(hr) && tempSRV)
            {
                // [核心修复] 将逻辑坐标转换为物理渲染坐标
                float renderX = node.x * scaleX;
                float renderY = node.y * scaleY;
                float renderW = node.w * scaleX;
                float renderH = node.h * scaleY;

                // 使用转换后的坐标进行绘制
                dx11.drawTexture(tempSRV.Get(),
                                 renderX,
                                 renderY,
                                 renderW,
                                 renderH,
                                 node.rotation,
                                 (float)m_canvasW,
                                 (float)m_canvasH  // 画布总大小也必须是物理大小
                );
            }
        }
    }

    ID3D11RenderTargetView* nullRTV[] = {nullptr};
    dx11.context()->OMSetRenderTargets(1, nullRTV, nullptr);
}

bool SceneCompositor::initNV12Resources(int w, int h)
{
    auto device = DX11Context::instance().device();
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &m_nv12Texture)))
        return false;

    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = DXGI_FORMAT_R8_UNORM;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    if (FAILED(device->CreateRenderTargetView(m_nv12Texture.Get(), &rtvDesc, &m_rtvY)))
        return false;

    rtvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
    if (FAILED(device->CreateRenderTargetView(m_nv12Texture.Get(), &rtvDesc, &m_rtvUV)))
        return false;

    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &m_nv12StagingTex));
}

void SceneCompositor::convertToYUVAndDownload()
{
    if (!m_mainSRV || !m_nv12Texture || !m_rtvY || !m_rtvUV)
        return;
    auto conf = AppConfig::instance()->getRecorderConfig().base;
    D3D11_TEXTURE2D_DESC desc;
    m_nv12Texture->GetDesc(&desc);

    // [Fix] 内部已加锁
    DX11Context::instance().convertRGBtoNV12(m_mainSRV.Get(),
                                             m_rtvY.Get(),
                                             m_rtvUV.Get(),
                                             desc.Width,
                                             desc.Height,
                                             conf.colorSpace,
                                             conf.colorRange,
                                             conf.downscaleFilter);
    downloadNV12ToCallback();
}

void SceneCompositor::downloadPreview()
{
    // 1. 初始化计时器
    if (!s_previewLimiter.isValid())
    {
        s_previewLimiter.start();
    }

    // 2. [核心修复] 暴力节流：如果距离上次发送不足 33ms (约30FPS)，直接丢弃
    // 这能拦截 90% 的多余信号，挽救 UI 线程于水火
    if (s_previewLimiter.elapsed() < 33)
    {
        return;
    }

    // 重置计时器，准备下一帧
    s_previewLimiter.restart();

    // 3. 安全检查 (保持之前的修复)
    if (!m_previewStagingTex || !m_mainTex)
        return;

    D3D11_TEXTURE2D_DESC desc;
    m_previewStagingTex->GetDesc(&desc);

    int safeW = static_cast<int>(desc.Width);
    int safeH = static_cast<int>(desc.Height);

    if (safeW <= 0 || safeH <= 0)
        return;

    // 4. 执行复制与发送
    DX11Context::instance().copyResource(m_previewStagingTex.Get(), m_mainTex.Get());
    DX11Context::instance().mapRead(
        m_previewStagingTex.Get(), [this, safeW, safeH](const D3D11_MAPPED_SUBRESOURCE& map) {
            if (!map.pData)
                return;

            QImage img((uchar const*)map.pData, safeW, safeH, map.RowPitch, QImage::Format_ARGB32);
            QImage deepCopy = img.copy();  // 深拷贝
            emit frameReady(deepCopy);
        });
}

void SceneCompositor::downloadNV12ToCallback()
{
    if (!m_nv12StagingTex || !m_nv12Texture)
        return;

    // [Fix] 安全复制
    DX11Context::instance().copyResource(m_nv12StagingTex.Get(), m_nv12Texture.Get());

    int64_t currentPts = GetMicroseconds() - m_startTimestamp;
    D3D11_TEXTURE2D_DESC desc;
    m_nv12Texture->GetDesc(&desc);
    int w = desc.Width;
    int h = desc.Height;

    // [Fix] 安全读取 + 错误日志
    bool success = DX11Context::instance().mapRead(
        m_nv12StagingTex.Get(), [this, w, h, currentPts](const D3D11_MAPPED_SUBRESOURCE& map) {
            int yHeight = h;
            int uvHeight = h / 2;
            int ySize = w * yHeight;
            int uvSize = w * uvHeight;

            // 确保 buffer 大小正确
            std::vector<uint8_t> buffer(ySize + uvSize);
            uint8_t* pSrcY = (uint8_t*)map.pData;
            uint8_t* pDstY = buffer.data();
            uint8_t* pSrcUV = pSrcY + (map.RowPitch * yHeight);
            uint8_t* pDstUV = buffer.data() + ySize;

            if (map.RowPitch == (UINT)w)
            {
                memcpy(pDstY, pSrcY, ySize);
                memcpy(pDstUV, pSrcUV, uvSize);
            }
            else
            {
                for (int i = 0; i < yHeight; ++i)
                    memcpy(pDstY + i * w, pSrcY + i * map.RowPitch, w);
                for (int i = 0; i < uvHeight; ++i)
                    memcpy(pDstUV + i * w, pSrcUV + i * map.RowPitch, w);
            }

            // 发出信号
            emit yuvFrameReady(buffer, w, h, currentPts / 1000);
            if (m_outputCallback)
                m_outputCallback(buffer.data(), w, h, currentPts);
        });

    if (!success)
    {
        // 如果这里打印了日志，说明 mapRead 失败（可能是 Staging 纹理创建失败或被占用）
        if (m_frameCount % 60 == 0)
            qWarning() << "[Compositor] Failed to Map NV12 Staging Texture!";
    }
}
