#pragma once
#include "DX11Context.h"
#include <d3dcompiler.h>  // [修复] 解决 D3DCompile 找不到标识符
#include <QMap>
#include <QVariant>
#include <memory>
#include <vector>

// 16字节对齐的宏
#define ALIGN16 __declspec(align(16))

class DX11FilterBase
{
public:
    virtual ~DX11FilterBase() = default;

    // 初始化：编译 Shader，创建 Buffer
    virtual bool init(ID3D11Device* device)
    {
        m_device = device;
        return initShaders();
    }

    // 调整尺寸
    virtual void resize(int width, int height)
    {
        m_width = width;
        m_height = height;
    }

    // 设置参数
    virtual void setParameter(QString const& key, QVariant const& value)
    {
        m_params[key] = value;
        m_isDirty = true;
    }

    // 核心处理函数
    virtual void process(ID3D11DeviceContext* context,
                         ID3D11ShaderResourceView* inputSRV,
                         ID3D11RenderTargetView* outputRTV)
    {
        if (m_isDirty)
        {
            updateUniforms(context);
            m_isDirty = false;
        }
        render(context, inputSRV, outputRTV);
    }

    virtual QString name() const = 0;

protected:
    virtual bool initShaders() = 0;

    virtual void updateUniforms(ID3D11DeviceContext* context)
    {
    }

    // 通用全屏 Quad 绘制逻辑
    void render(ID3D11DeviceContext* context,
                ID3D11ShaderResourceView* input,
                ID3D11RenderTargetView* output)
    {
        // 1. 设置渲染目标
        context->OMSetRenderTargets(1, &output, nullptr);

        // 2. 设置 Viewport
        D3D11_VIEWPORT vp = {0.0f, 0.0f, (float)m_width, (float)m_height, 0.0f, 1.0f};
        context->RSSetViewports(1, &vp);

        // 3. 设置 Shader
        context->IASetInputLayout(DX11Context::instance().getCommonInputLayout());
        context->VSSetShader(DX11Context::instance().getCommonVertexShader(), nullptr, 0);
        context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

        // 绑定输入纹理
        context->PSSetShaderResources(0, 1, &input);

        // 绑定采样器
        ID3D11SamplerState* samplers[] = {DX11Context::instance().getLinearSampler()};
        context->PSSetSamplers(0, 1, samplers);

        // 绑定常量缓冲
        if (m_constantBuffer)
        {
            context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
        }

        bindExtraResources(context);

        // 4. 绘制
        UINT stride = sizeof(float) * 4;
        UINT offset = 0;

        // [修复] IASetVertexBuffers 需要 ID3D11Buffer** (指针的地址)
        // 不能直接传 getCommonVertexBuffer() 的返回值（那是右值或指针值）
        ID3D11Buffer* vBuffer = DX11Context::instance().getCommonVertexBuffer();
        context->IASetVertexBuffers(0, 1, &vBuffer, &stride, &offset);

        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        context->Draw(4, 0);

        // 5. 清理
        ID3D11ShaderResourceView* nullSRV[] = {nullptr, nullptr, nullptr};
        context->PSSetShaderResources(0, 3, nullSRV);
    }

    virtual void bindExtraResources(ID3D11DeviceContext* context)
    {
    }

    bool compileShader(char const* code, char const* entry, ID3D11PixelShader** shader)
    {
        ComPtr<ID3DBlob> blob, error;
        // 注意：需在 .pro 或链接器设置中链接 d3dcompiler.lib
        HRESULT hr = D3DCompile(
            code, strlen(code), nullptr, nullptr, nullptr, entry, "ps_5_0", 0, 0, &blob, &error);
        if (FAILED(hr))
        {
            if (error)
                qWarning() << "Shader Compile Error:" << (char*)error->GetBufferPointer();
            return false;
        }
        return SUCCEEDED(m_device->CreatePixelShader(
            blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, shader));
    }

protected:
    ID3D11Device* m_device = nullptr;
    ComPtr<ID3D11PixelShader> m_pixelShader;
    ComPtr<ID3D11Buffer> m_constantBuffer;

    QMap<QString, QVariant> m_params;
    int m_width = 1920;
    int m_height = 1080;
    bool m_isDirty = true;
};
