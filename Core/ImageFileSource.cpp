#include "ImageFileSource.h"
#include "Logger.h"

ImageFileSource::ImageFileSource()
{
    setSourceType(SourceType::Image);
}

ImageFileSource::~ImageFileSource()
{
    m_srv.Reset();
    m_texture.Reset();
}

bool ImageFileSource::loadFile(QString const& filePath)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    QImage temp;
    if (!temp.load(filePath))
    {
        LOG_ERROR("ImageFileSource: Failed to load file " << filePath.toStdString());
        return false;
    }

    // 格式转换：Qt Format_ARGB32 内存为 B-G-R-A，对应 DXGI_FORMAT_B8G8R8A8_UNORM
    if (temp.format() != QImage::Format_ARGB32)
    {
        m_imageBuffer = temp.convertToFormat(QImage::Format_ARGB32);
    }
    else
    {
        m_imageBuffer = temp;
    }

    m_width = m_imageBuffer.width();
    m_height = m_imageBuffer.height();
    m_name = QFileInfo(filePath).fileName();
    m_filePath = filePath;

    // 标记脏，触发下一次 getTexture 上传
    m_isDirty = true;

    LOG_INFO("ImageFileSource: Loaded " << filePath.toStdString() << " (" << m_width << "x"
                                        << m_height << ")");
    return true;
}

void ImageFileSource::stop()
{
    return;
}

void ImageFileSource::getSize(int& w, int& h) const
{
    w = m_width;
    h = m_height;
}

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ImageFileSource::getTexture(
    ID3D11DeviceContext* /*ctx*/)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // 如果不脏且已有 SRV，直接返回，避免重复上传
    if (!m_isDirty && m_srv)
        return m_srv;
    if (m_imageBuffer.isNull())
        return nullptr;

    auto device = DX11Context::instance().device();
    if (!device)
        return nullptr;

    // 清理旧资源
    m_srv.Reset();
    m_texture.Reset();

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = m_width;
    desc.Height = m_height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = m_imageBuffer.constBits();
    initData.SysMemPitch = m_imageBuffer.bytesPerLine();

    HRESULT hr = device->CreateTexture2D(&desc, &initData, &m_texture);
    if (FAILED(hr))
    {
        LOG_ERROR("ImageFileSource: Failed to create Texture2D. HR=" << std::hex << hr);
        return nullptr;
    }

    hr = device->CreateShaderResourceView(m_texture.Get(), nullptr, &m_srv);
    if (FAILED(hr))
        return nullptr;

    // m_isDirty 在这里不自动置 false，而是留给 Compositor 调用 markFrameProcessed
    // 或者，因为 getTexture 已经完成了上传，这里置 false 也是安全的。
    // 为了配合 hasNewFrame 的逻辑：
    // 1. Compositor 发现 hasNewFrame() == true
    // 2. Compositor 调用 getTexture() -> 这里执行上传
    // 3. Compositor 调用 markFrameProcessed() -> 设置 m_isDirty = false
    // 因此这里不需要手动置 false，保持逻辑一致性。
    if (!m_srv)
        return nullptr;

    return m_srv;
}
