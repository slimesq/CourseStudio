#pragma once
#include <QObject>
#include <QImage>
#include <mutex>
#include <vector>
#include <functional>
#include <atomic>  // [新增]
#include "Scene.h"
#include "DX11Context.h"
#include "TexturePool.h"
#include "HighPrecisionThread.h"

class IVirtualCamPublisher;

class SceneCompositor : public QObject
{
    Q_OBJECT
public:
    // [New] 合成器工作模式
    enum class CompositorMode
    {
        Editor,      // 编辑模式：低帧率 (25FPS)，仅 RGB 渲染，跳过 YUV 转换和下载
        LiveRunning  // 直播模式：高帧率 (Config FPS)，全功能管线 (RGB + YUV Output)
    };

    explicit SceneCompositor(std::shared_ptr<Scene> scene, QObject* parent = nullptr);
    ~SceneCompositor();

    void startRendering(int fps = 30);
    void stopRendering();

    // [New] 切换工作模式
    void setMode(CompositorMode mode);
    CompositorMode getMode() const
    {
        return m_currentMode;
    }

    void* getPreviewHandle();

    using VideoFrameCallback =
        std::function<void(uint8_t const* data, int width, int height, int64_t pts)>;
    void setVideoOutputCallback(VideoFrameCallback callback);

signals:
    void frameReady(QImage const& image);
    void yuvFrameReady(std::vector<uint8_t> const& data, int width, int height, int64_t pts);

private slots:
    // [新增] 接收来自 Scene 的销毁请求
    void onSourceRecycle(std::shared_ptr<ISource> source);
    void onSceneContentChanged();
    void onCanvasSizeChanged(int w, int h);

private:
    // [新增] 处理回收队列
    void processGarbage();
    void renderLoop();
    void syncRenderNodes();
    void renderToTexture();
    void downloadPreview();

    bool initNV12Resources(int w, int h);
    void convertToYUVAndDownload();
    void downloadNV12ToCallback();

    // [新增] 内部使用的资源重置函数，仅在渲染线程调用
    void recreateResources();

    void calculateRenderSize(int& outW, int& outH);

private:
    std::shared_ptr<Scene> m_scene;
    HighPrecisionThread m_renderThread;

    // [New] 当前模式
    std::atomic<CompositorMode> m_currentMode{CompositorMode::Editor};

    struct RenderNode
    {
        std::string sourceId;
        float x, y, w, h;
        float rotation;
        int zOrder;
        float opacity;
    };
    std::vector<RenderNode> m_renderList;

    std::mutex m_loopMutex;    // 保护渲染循环的整体生命周期
    std::mutex m_renderMutex;  // 仅保护 m_renderList 数据结构

    // [修改] 宽高使用原子操作或由 loopMutex 保护，这里由 Resize 逻辑控制
    int m_canvasW = 1920;
    int m_canvasH = 1080;

    // [新增] 记录逻辑画布的原始尺寸，用于计算缩放比
    float m_logicalW = 1920.0f;
    float m_logicalH = 1080.0f;

    // [新增] 异步 Resize 变量
    std::atomic<bool> m_resizePending{false};
    std::atomic<int> m_pendingW{0};
    std::atomic<int> m_pendingH{0};
    // [新增] 资源回收队列 (由渲染锁保护)
    std::vector<std::shared_ptr<ISource>> m_garbageBin;

    // [优化] 记录上一帧是否有内容，用于空场景短路优化
    bool m_hasContentLastFrame = false;

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_mainRTV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_mainSRV;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_mainTex;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_previewStagingTex;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_nv12Texture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtvY;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtvUV;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_nv12StagingTex;

    VideoFrameCallback m_outputCallback;
    uint64_t m_frameCount = 0;
    IVirtualCamPublisher* m_vcamPublisher = nullptr;
    int64_t m_startTimestamp = 0;
};
