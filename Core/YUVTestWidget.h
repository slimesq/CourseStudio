#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLBuffer>
#include <vector>
#include <mutex>

// 独立的 YUV (NV12) 预览组件
// 继承自 QOpenGLWidget，不依赖 DX11，纯接收二进制数据
class YUVTestWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit YUVTestWidget(QWidget* parent = nullptr) : QOpenGLWidget(parent)
    {
    }
    ~YUVTestWidget() override
    {
        makeCurrent();
        if (m_textureY)
            m_textureY->destroy();
        if (m_textureUV)
            m_textureUV->destroy();
        doneCurrent();
    }

public slots:
    // 接收 NV12 数据槽函数
    void updateTexture(std::vector<uint8_t> const& data, int width, int height)
    {
        if (data.empty())
            return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_yuvData = data;
        m_videoW = width;
        m_videoH = height;
        m_needsUpdate = true;
        update();  // 触发重绘
    }

protected:
    void initializeGL() override
    {
        initializeOpenGLFunctions();
        glClearColor(0.0f, 0.2f, 0.0f, 1.0f);  // 深绿色背景表示 OpenGL 正常

        // 顶点着色器
        char const* vsrc = R"(
            attribute vec4 vertexIn;
            attribute vec2 textureIn;
            varying vec2 textureOut;
            void main(void) {
                gl_Position = vertexIn;
                textureOut = textureIn;
            }
        )";

        // 片段着色器 (NV12 -> RGB)
        char const* fsrc = R"(
            varying vec2 textureOut;
            uniform sampler2D tex_y;
            uniform sampler2D tex_uv;
            void main(void) {
                vec3 yuv;
                vec3 rgb;
                yuv.x = texture2D(tex_y, textureOut).r;
                // NV12 UV 交错存储，rg 分量
                yuv.y = texture2D(tex_uv, textureOut).r - 0.5;
                yuv.z = texture2D(tex_uv, textureOut).g - 0.5;

                rgb.r = yuv.x + 1.403 * yuv.z;
                rgb.g = yuv.x - 0.344 * yuv.y - 0.714 * yuv.z;
                rgb.b = yuv.x + 1.770 * yuv.y;

                gl_FragColor = vec4(rgb, 1.0);
            }
        )";

        m_program = new QOpenGLShaderProgram;
        m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vsrc);
        m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, fsrc);
        m_program->link();

        // 顶点数据 (全屏四边形, 上下翻转纹理坐标以匹配 DX11)
        static GLfloat const points[] = {-1.0f,
                                         -1.0f,
                                         0.0f,
                                         1.0f,
                                         1.0f,
                                         -1.0f,
                                         1.0f,
                                         1.0f,
                                         -1.0f,
                                         1.0f,
                                         0.0f,
                                         0.0f,
                                         1.0f,
                                         1.0f,
                                         1.0f,
                                         0.0f};
        m_vbo.create();
        m_vbo.bind();
        m_vbo.allocate(points, sizeof(points));
    }

    void resizeGL(int w, int h) override
    {
        glViewport(0, 0, w, h);
    }

    void paintGL() override
    {
        glClear(GL_COLOR_BUFFER_BIT);

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_needsUpdate && m_videoW > 0 && !m_yuvData.empty())
        {
            // 上传纹理
            int ySize = m_videoW * m_videoH;

            // Y Plane
            if (!m_textureY)
            {
                m_textureY = new QOpenGLTexture(QOpenGLTexture::Target2D);
                m_textureY->setFormat(QOpenGLTexture::R8_UNorm);
            }
            if (m_textureY->width() != m_videoW || m_textureY->height() != m_videoH)
            {
                m_textureY->setSize(m_videoW, m_videoH);
                m_textureY->allocateStorage(QOpenGLTexture::Red, QOpenGLTexture::UInt8);
            }
            m_textureY->setData(QOpenGLTexture::Red, QOpenGLTexture::UInt8, m_yuvData.data());

            // UV Plane (NV12, Texture 尺寸为 W/2 x H/2, 格式 RG8)
            if (!m_textureUV)
            {
                m_textureUV = new QOpenGLTexture(QOpenGLTexture::Target2D);
                m_textureUV->setFormat(QOpenGLTexture::RG8_UNorm);
            }
            if (m_textureUV->width() != m_videoW / 2 || m_textureUV->height() != m_videoH / 2)
            {
                m_textureUV->setSize(m_videoW / 2, m_videoH / 2);
                m_textureUV->allocateStorage(QOpenGLTexture::RG, QOpenGLTexture::UInt8);
            }
            // UV 数据紧跟 Y 数据之后
            m_textureUV->setData(
                QOpenGLTexture::RG, QOpenGLTexture::UInt8, m_yuvData.data() + ySize);

            m_needsUpdate = false;
        }

        if (m_program && m_textureY && m_textureUV)
        {
            m_program->bind();
            m_vbo.bind();
            m_program->enableAttributeArray("vertexIn");
            m_program->enableAttributeArray("textureIn");
            m_program->setAttributeBuffer("vertexIn", GL_FLOAT, 0, 2, 4 * sizeof(GLfloat));
            m_program->setAttributeBuffer(
                "textureIn", GL_FLOAT, 2 * sizeof(GLfloat), 2, 4 * sizeof(GLfloat));

            m_textureY->bind(0);
            m_textureUV->bind(1);
            m_program->setUniformValue("tex_y", 0);
            m_program->setUniformValue("tex_uv", 1);

            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

            m_textureUV->release();
            m_textureY->release();
            m_program->release();
        }
    }

private:
    std::mutex m_mutex;
    std::vector<uint8_t> m_yuvData;
    int m_videoW = 0;
    int m_videoH = 0;
    bool m_needsUpdate = false;

    QOpenGLShaderProgram* m_program = nullptr;
    QOpenGLTexture* m_textureY = nullptr;
    QOpenGLTexture* m_textureUV = nullptr;
    QOpenGLBuffer m_vbo;
};
