#pragma once
#include "IVideoSource.h"
#include <QImage>
#include <QFileInfo>
#include <mutex>

class ImageFileSource : public IVideoSource
{
public:
    ImageFileSource();
    ~ImageFileSource() override;

    bool loadFile(QString const& filePath);

    // --- IVideoSource 接口 ---
    bool start() override
    {
        return true;
    }
    void stop() override;

    QString getDeviceName() const override
    {
        return m_name;
    }
    void getSize(int& w, int& h) const override;

    // 只要有图片数据，就返回 true，确保 Compositor 会在初始化时拉取
    // 实际的 GPU 上传只在 m_isDirty 时发生
    bool hasNewFrame() const override
    {
        return m_isDirty;
    }
    void markFrameProcessed() override
    {
        m_isDirty = false;
    }

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> getTexture(ID3D11DeviceContext* ctx) override;

private:
    std::mutex m_mutex;
    QString m_filePath;
    QString m_name = "Image";

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;

    QImage m_imageBuffer;
    bool m_isDirty = false;
    int m_width = 0;
    int m_height = 0;
};
