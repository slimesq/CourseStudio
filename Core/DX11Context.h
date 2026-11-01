#pragma once
#define NOMINMAX
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <mutex>
#include <vector>
#include <functional>  // [新增]
#include "RecorderConfig.h"

using Microsoft::WRL::ComPtr;

// 顶点变换缓冲
struct VS_CONSTANT_BUFFER
{
    float x, y, w, h;        // 目标矩形
    float canvasW, canvasH;  // 画布尺寸
    float rotation;
    float padding;
};

// [P2] 色彩转换矩阵与缩放参数缓冲
struct PS_COLOR_BUFFER
{
    // RGB -> YUV 变换矩阵 (3x3 + 偏移)
    float yCoeff[4];
    float uCoeff[4];
    float vCoeff[4];

    // 纹理尺寸信息
    float texWidth;
    float texHeight;
    float padding[2];
};

class DX11Context
{
public:
    static DX11Context& instance();

    bool init(void* hwnd, int width, int height);
    void resize(int width, int height);

    bool createOffscreenCanvas(int w,
                               int h,
                               ID3D11RenderTargetView** rtv,
                               ID3D11ShaderResourceView** srv,
                               ID3D11Texture2D** tex);
    bool createStagingTexture(int w, int h, ID3D11Texture2D** tex);

    void beginScene(ID3D11RenderTargetView* targetRTV = nullptr, float clearColor[4] = nullptr);
    void endScene();

    ID3D11Device* device() const
    {
        return m_device.Get();
    }
    // [修改] 移除直接暴露 context()，强制通过安全接口访问
    ID3D11DeviceContext* context() const
    {
        return m_context.Get();
    }

    void drawTexture(ID3D11ShaderResourceView* srv,
                     float x,
                     float y,
                     float w,
                     float h,
                     float rotation,
                     float canvasW,
                     float canvasH);

    // --- [P2] 高级 RGB 转 NV12 接口 ---
    void convertRGBtoNV12(ID3D11ShaderResourceView* inputTex,
                          ID3D11RenderTargetView* targetY,
                          ID3D11RenderTargetView* targetUV,
                          int targetW,
                          int targetH,
                          ColorSpace space,
                          ColorRange range,
                          DownscaleFilter filter);

    std::vector<uint8_t> readTexture(ID3D11Texture2D* texture, int& w, int& h);

    // [新增] 线程安全的资源复制
    void copyResource(ID3D11Resource* dst, ID3D11Resource* src);

    // [新增] 线程安全的 Map 读取
    // callback 在锁内执行，必须快速返回
    bool mapRead(ID3D11Resource* res,
                 std::function<void(const D3D11_MAPPED_SUBRESOURCE&)> callback);

    void execSafe(std::function<void(ID3D11DeviceContext*)> task);

    ID3D11VertexShader* getCommonVertexShader() const
    {
        return m_vs.Get();
    }
    ID3D11InputLayout* getCommonInputLayout() const
    {
        return m_layout.Get();
    }
    ID3D11Buffer* getCommonVertexBuffer() const
    {
        return m_vbuffer.Get();
    }
    ID3D11SamplerState* getLinearSampler() const
    {
        return m_samplerLinear.Get();
    }

private:
    DX11Context() = default;
    void initShaders();
    void createRenderTarget();
    void updateColorMatrix(ColorSpace space, ColorRange range, int texW, int texH);
    void renderQuad(
        float x, float y, float w, float h, float rotation, float canvasW, float canvasH);

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain1> m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_mainRtv;

    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader> m_ps;

    // [P2] 不同算法的 Shader
    ComPtr<ID3D11PixelShader> m_psRGB2Y_Bilinear;
    ComPtr<ID3D11PixelShader> m_psRGB2UV_Bilinear;
    ComPtr<ID3D11PixelShader> m_psRGB2Y_Bicubic;
    ComPtr<ID3D11PixelShader> m_psRGB2UV_Bicubic;

    ComPtr<ID3D11Buffer> m_vbuffer;
    ComPtr<ID3D11Buffer> m_cbuffer;
    ComPtr<ID3D11Buffer> m_colorBuffer;

    ComPtr<ID3D11SamplerState> m_samplerLinear;

    ComPtr<ID3D11BlendState> m_blendState;
    ComPtr<ID3D11BlendState> m_blendStateDisabled;
    ComPtr<ID3D11InputLayout> m_layout;

    int m_width = 0;
    int m_height = 0;
    std::mutex m_mutex;  // 核心互斥锁
};
