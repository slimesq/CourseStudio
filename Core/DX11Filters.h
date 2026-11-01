#pragma once
#include "DX11FilterBase.h"
#include <QImage>
#include <QPainter>
#include <cmath>

// ================== 1. LUT 滤镜 (色彩分级) ==================
class DX11LutFilter : public DX11FilterBase
{
    // 确保 Constant Buffer 16字节对齐
    struct ALIGN16 CBuffer
    {
        float intensity;
        float padding[3];  // [修复] C++ 中没有 float3，使用数组占位
    };

public:
    DX11LutFilter()
    {
        m_params["intensity"] = 1.0f;
    }

    QString name() const override
    {
        return "LUT Color Grading";
    }

    void setLutImage(QImage const& img)
    {
        if (img.isNull() || !m_device)
            return;

        QImage textureImg = img.convertToFormat(QImage::Format_RGBA8888);
        int w = textureImg.width();
        int h = textureImg.height();

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = textureImg.constBits();
        initData.SysMemPitch = textureImg.bytesPerLine();

        m_lutTexture.Reset();
        m_lutSRV.Reset();
        HRESULT hr = m_device->CreateTexture2D(&desc, &initData, &m_lutTexture);
        if (SUCCEEDED(hr))
        {
            m_device->CreateShaderResourceView(m_lutTexture.Get(), nullptr, &m_lutSRV);
            m_lutReady = true;
        }
    }

protected:
    bool initShaders() override
    {
        char const* hlsl = R"(
            Texture2D inputTex : register(t0);
            Texture2D lutTex   : register(t1);
            SamplerState sam   : register(s0);

            cbuffer Params : register(b0) {
                float intensity;
                float3 padding;
            };

            float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD) : SV_TARGET {
                float4 color = inputTex.Sample(sam, uv);
                float blue = color.b * 63.0;
                float2 quad1;
                quad1.y = floor(floor(blue) / 8.0);
                quad1.x = floor(blue) - (quad1.y * 8.0);
                float2 quad2;
                quad2.y = floor(ceil(blue) / 8.0);
                quad2.x = ceil(blue) - (quad2.y * 8.0);
                float2 texPos1;
                texPos1.x = (quad1.x * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * color.r);
                texPos1.y = (quad1.y * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * color.g);
                float2 texPos2;
                texPos2.x = (quad2.x * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * color.r);
                texPos2.y = (quad2.y * 0.125) + 0.5/512.0 + ((0.125 - 1.0/512.0) * color.g);
                float4 newColor1 = lutTex.Sample(sam, texPos1);
                float4 newColor2 = lutTex.Sample(sam, texPos2);
                float4 lutColor = lerp(newColor1, newColor2, frac(blue));
                return lerp(color, float4(lutColor.rgb, color.a), intensity);
            }
        )";

        D3D11_BUFFER_DESC bd = {
            sizeof(CBuffer), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
        m_device->CreateBuffer(&bd, nullptr, &m_constantBuffer);

        return compileShader(hlsl, "main", &m_pixelShader);
    }

    void updateUniforms(ID3D11DeviceContext* ctx) override
    {
        CBuffer cb;
        cb.intensity = m_params["intensity"].toFloat();
        ctx->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);
    }

    void bindExtraResources(ID3D11DeviceContext* ctx) override
    {
        if (m_lutTexture && m_lutReady)
        {
            ctx->PSSetShaderResources(1, 1, m_lutSRV.GetAddressOf());
        }
    }

private:
    ComPtr<ID3D11Texture2D> m_lutTexture;
    ComPtr<ID3D11ShaderResourceView> m_lutSRV;
    bool m_lutReady = false;
};

// ================== 2. 美颜滤镜 (磨皮) ==================
class DX11BeautyFilter : public DX11FilterBase
{
    struct ALIGN16 CBuffer
    {
        float width;
        float height;
        float intensity;
        float padding;
    };

public:
    DX11BeautyFilter()
    {
        m_params["intensity"] = 0.5f;
    }
    QString name() const override
    {
        return "Skin Smoothing";
    }

protected:
    bool initShaders() override
    {
        char const* hlsl = R"(
            Texture2D tex : register(t0);
            SamplerState sam : register(s0);

            cbuffer Params : register(b0) {
                float width; float height; float intensity; float padding;
            };

            bool isSkin(float3 color) {
                float r = color.r; float g = color.g; float b = color.b;
                return (r > 0.37 && g > 0.21 && b > 0.14 && r > g && r > b && (max(max(r, g), b) - min(min(r, g), b)) > 0.058);
            }

            float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD) : SV_TARGET {
                float4 center = tex.Sample(sam, uv);
                if (intensity <= 0.01) return center;

                float skinFactor = isSkin(center.rgb) ? 1.0 : 0.0;
                if (skinFactor < 0.5) return center;

                float2 step = float2(1.0/width, 1.0/height) * 2.0;
                float3 sum = float3(0,0,0);

                [unroll]
                for(int x=-2; x<=2; x++) {
                    [unroll]
                    for(int y=-2; y<=2; y++) {
                        sum += tex.Sample(sam, uv + float2(x,y) * step).rgb;
                    }
                }
                float3 blur = sum / 25.0;

                float3 finalColor = lerp(center.rgb, blur, intensity * 0.8);
                return float4(finalColor, center.a);
            }
        )";
        D3D11_BUFFER_DESC bd = {
            sizeof(CBuffer), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
        m_device->CreateBuffer(&bd, nullptr, &m_constantBuffer);
        return compileShader(hlsl, "main", &m_pixelShader);
    }

    void updateUniforms(ID3D11DeviceContext* ctx) override
    {
        CBuffer cb;
        cb.width = (float)m_width;
        cb.height = (float)m_height;
        cb.intensity = m_params["intensity"].toFloat();
        ctx->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);
    }
};

// ================== 3. 叠加滤镜 (水印 & 字幕) ==================
class DX11OverlayFilter : public DX11FilterBase
{
    struct ALIGN16 CBuffer
    {
        float x;
        float y;
        float scaleX;
        float scaleY;
        float opacity;
        float padding[3];  // [修复] C++ 中没有 float3
    };

public:
    DX11OverlayFilter()
    {
        m_params["x"] = 0.05f;
        m_params["y"] = 0.05f;
        m_params["scale"] = 0.2f;
        m_params["opacity"] = 1.0f;
    }

    QString name() const override
    {
        return "Subtitle/Marquee";
    }

    void setImage(QImage const& img)
    {
        if (img.isNull() || !m_device)
            return;

        QImage textureImg = img.convertToFormat(QImage::Format_RGBA8888);
        m_imgWidth = textureImg.width();
        m_imgHeight = textureImg.height();

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = m_imgWidth;
        desc.Height = m_imgHeight;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = textureImg.constBits();
        initData.SysMemPitch = textureImg.bytesPerLine();

        m_overlayTexture.Reset();
        m_overlaySRV.Reset();
        HRESULT hr = m_device->CreateTexture2D(&desc, &initData, &m_overlayTexture);
        if (SUCCEEDED(hr))
        {
            m_device->CreateShaderResourceView(m_overlayTexture.Get(), nullptr, &m_overlaySRV);
            m_hasTexture = true;
        }
    }

    void setText(QString const& text,
                 QFont const& font = QFont("Microsoft YaHei", 40, QFont::Bold),
                 QColor const& color = Qt::white,
                 QColor const& bgColor = QColor(0, 0, 0, 128),
                 int padding = 20)
    {
        // 1. 计算文字所需的尺寸
        QFontMetrics fm(font);
        int textW = fm.horizontalAdvance(text);
        int textH = fm.height();

        // 图片宽高 = 文字宽高 + 内边距
        int imgW = textW + padding * 2;
        int imgH = textH + padding * 2;

        // 2. 创建透明背景图片
        QImage image(imgW, imgH, QImage::Format_RGBA8888);
        image.fill(Qt::transparent);  // 关键：先全透明

        // 3. 使用 QPainter 绘制
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);

        // 绘制背景 (如果是圆角矩形更好看)
        if (bgColor.alpha() > 0)
        {
            p.setPen(Qt::NoPen);
            p.setBrush(bgColor);
            p.drawRoundedRect(0, 0, imgW, imgH, 10, 10);
        }

        // 绘制文字 (居中)
        p.setPen(color);
        p.setFont(font);
        p.drawText(image.rect(), Qt::AlignCenter, text);
        p.end();

        // 4. 上传纹理 (调用父类方法)
        // 注意：这里我们动态调整 scaleX 和 scaleY 以保持像素 1:1 或固定比例
        setImage(image);

        // 记录原始图片比例，用于后续修正
        m_aspectRatio = (float)imgW / imgH;
    }

protected:
    bool initShaders() override
    {
        char const* hlsl = R"(
            Texture2D baseTex : register(t0);
            Texture2D overTex : register(t1);
            SamplerState sam : register(s0);

            cbuffer Params : register(b0) {
                float x; float y; float scaleX; float scaleY; float opacity; float3 padding;
            };

            float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD) : SV_TARGET {
                float4 baseColor = baseTex.Sample(sam, uv);
                float2 overUV;
                overUV.x = (uv.x - x) / scaleX;
                overUV.y = (uv.y - y) / scaleY;

                if (overUV.x >= 0.0 && overUV.x <= 1.0 && overUV.y >= 0.0 && overUV.y <= 1.0) {
                    float4 overColor = overTex.Sample(sam, overUV);
                    float alpha = overColor.a * opacity;
                    float3 rgb = lerp(baseColor.rgb, overColor.rgb, alpha);
                    return float4(rgb, baseColor.a);
                }
                return baseColor;
            }
        )";
        D3D11_BUFFER_DESC bd = {
            sizeof(CBuffer), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
        m_device->CreateBuffer(&bd, nullptr, &m_constantBuffer);
        return compileShader(hlsl, "main", &m_pixelShader);
    }

    void updateUniforms(ID3D11DeviceContext* ctx) override
    {
        if (!m_hasTexture || m_imgWidth == 0 || m_imgHeight == 0)
            return;

        float canvasAspect = (float)m_width / m_height;
        float imgAspect = (float)m_imgWidth / m_imgHeight;
        float scale = m_params["scale"].toFloat();

        float finalScaleX = scale;
        float finalScaleY = scale * canvasAspect / imgAspect;

        CBuffer cb;
        cb.x = m_params["x"].toFloat();
        cb.y = m_params["y"].toFloat();
        cb.scaleX = finalScaleX;
        cb.scaleY = finalScaleY;
        cb.opacity = m_params["opacity"].toFloat();

        ctx->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);
    }

    void bindExtraResources(ID3D11DeviceContext* ctx) override
    {
        if (m_hasTexture)
        {
            ctx->PSSetShaderResources(1, 1, m_overlaySRV.GetAddressOf());
        }
    }

private:
    ComPtr<ID3D11Texture2D> m_overlayTexture;
    ComPtr<ID3D11ShaderResourceView> m_overlaySRV;
    int m_imgWidth = 0;
    int m_imgHeight = 0;
    float m_aspectRatio = 1.0f;
    bool m_hasTexture = false;
};
