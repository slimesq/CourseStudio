#include "TestDemoWindow.h"
#include "DX11Context.h"
#include "SourceFactory.h"
#include "SourceProvider.h"  // AssetManager
#include <QDebug>
#include <QSettings>
#include <QStandardPaths>
#include "Scene.h"
#include <QThread>

// --- RawSourcePreview 实现 ---

RawSourcePreview::RawSourcePreview(std::shared_ptr<ISource> source, QWidget* parent)
    : QLabel(parent), m_source(source)
{
    setFixedSize(320, 180);  // 缩略图大小
    setStyleSheet("border: 1px solid #666; background: #000;");
    setAlignment(Qt::AlignCenter);
    setText("Waiting for source...");

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &RawSourcePreview::refreshFrame);
    m_timer->start(100);  // 10 FPS 预览足够了
}

RawSourcePreview::~RawSourcePreview()
{
    if (m_timer)
        m_timer->stop();
}

void RawSourcePreview::refreshFrame()
{
    if (!m_source)
        return;

    // 获取纹理句柄 (SRV)
    TextureHandle handle = m_source->getTextureHandle();
    if (!handle)
    {
        setText("No Signal");
        return;
    }

    // 此时 handle 是 ID3D11ShaderResourceView*
    auto srv = static_cast<ID3D11ShaderResourceView*>(handle);
    Microsoft::WRL::ComPtr<ID3D11Resource> res;
    srv->GetResource(&res);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    if (SUCCEEDED(res.As(&tex)))
    {
        int w, h;
        // 从 GPU 读取数据 (耗时操作，仅用于 Demo 调试)
        std::vector<uint8_t> data = DX11Context::instance().readTexture(tex.Get(), w, h);

        if (!data.empty())
        {
            // DX11 数据通常是 BGRA，Qt 需要 ARGB 或 RGB32
            // 这里简单构造 Image，颜色可能会反，主要看有没有画面
            QImage img(data.data(), w, h, w * 4, QImage::Format_ARGB32);
            // 深拷贝一份，否则 data 析构后 img 无效
            QImage displayImg =
                img.copy().scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            setPixmap(QPixmap::fromImage(displayImg));
        }
    }
}

// --- TestDemoWindow 实现 ---

TestDemoWindow::TestDemoWindow(QWidget* parent) : QWidget(parent)
{
    qRegisterMetaType<std::vector<uint8_t>>("std::vector<uint8_t>");
    qRegisterMetaType<int64_t>("int64_t");

    m_lastVideoW = 0;
    m_lastVideoH = 0;

    setupUi();

    m_scene = std::make_shared<Scene>("Test Scene");
    m_compositor = std::make_unique<SceneCompositor>(m_scene);
    connect(m_compositor.get(),
            &SceneCompositor::yuvFrameReady,
            this,
            &TestDemoWindow::onYuvDataReceived);

    m_recordController = std::make_unique<RecordController>(m_compositor.get(), this);
    m_durationTimer = new QTimer(this);
    connect(m_durationTimer, &QTimer::timeout, this, &TestDemoWindow::updateRecordDuration);
}

TestDemoWindow::~TestDemoWindow()
{
    if (m_recordController)
        m_recordController->stopTask();
    if (m_compositor)
        m_compositor->stopRendering();
    m_activeSources.clear();
}

void TestDemoWindow::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // Top Controls
    QHBoxLayout* ctrlLayout = new QHBoxLayout();
    QLabel* topLabel = new QLabel("LAYER 1: Raw Sources", this);
    topLabel->setStyleSheet("font-weight: bold; color: yellow;");
    ctrlLayout->addWidget(topLabel);
    ctrlLayout->addStretch();

    m_btnSettings = new QPushButton("⚙ Config", this);
    m_btnSettings->setFixedWidth(80);
    connect(m_btnSettings, &QPushButton::clicked, this, &TestDemoWindow::onSettingsClicked);
    ctrlLayout->addWidget(m_btnSettings);

    m_btnRecord = new QPushButton("Start Record", this);
    m_btnRecord->setMinimumWidth(120);
    m_btnRecord->setStyleSheet(
        "QPushButton { background-color: #cc3333; color: white; font-weight: bold; padding: 5px; }"
        "QPushButton:checked { background-color: #880000; }");
    m_btnRecord->setCheckable(true);
    connect(m_btnRecord, &QPushButton::clicked, this, &TestDemoWindow::onRecordClicked);
    ctrlLayout->addWidget(m_btnRecord);
    mainLayout->addLayout(ctrlLayout);

    // Scroll Area
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setFixedHeight(220);
    scroll->setWidgetResizable(true);
    m_topContainer = new QWidget();
    m_topLayout = new QHBoxLayout(m_topContainer);
    m_topLayout->setAlignment(Qt::AlignLeft);
    scroll->setWidget(m_topContainer);
    mainLayout->addWidget(scroll);

    // Layer 2
    QLabel* bottomLabel = new QLabel("LAYER 2: SceneCompositor Output (NV12)", this);
    bottomLabel->setStyleSheet("font-weight: bold; color: cyan;");
    mainLayout->addWidget(bottomLabel);

    // [关键] YUV Container
    m_yuvContainer = new QWidget(this);
    m_yuvContainer->setStyleSheet("background-color: black; border: 2px solid #333;");
    m_yuvContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_yuvContainer->setMinimumHeight(400);
    // [Fix] 安装事件过滤器，精准捕获容器大小变化
    m_yuvContainer->installEventFilter(this);
    mainLayout->addWidget(m_yuvContainer);

    m_yuvDisplay = new YUVTestWidget(m_yuvContainer);
    // 初始设为全屏，稍后布局会自动调整
    m_yuvDisplay->setGeometry(0, 0, 100, 100);
    m_yuvDisplay->show();

    m_statusLabel = new QLabel("System Ready", this);
    mainLayout->addWidget(m_statusLabel);

    resize(1280, 800);
    setWindowTitle("CourseStudio Pipeline Debugger");
}

// [Fix] 使用事件过滤器代替 resizeEvent，确保布局同步
bool TestDemoWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_yuvContainer && event->type() == QEvent::Resize)
    {
        updateYuvLayout();
        return true;  // 我们处理了，但也不阻止后续处理
    }
    return QWidget::eventFilter(watched, event);
}

void TestDemoWindow::updateYuvLayout()
{
    if (!m_yuvContainer || !m_yuvDisplay)
        return;

    // 获取容器当前的实际尺寸 (包含边框，我们减去一点点边距)
    QSize containerSize = m_yuvContainer->size();
    if (containerSize.width() <= 10 || containerSize.height() <= 10)
        return;

    // 视频源尺寸
    int videoW = (m_lastVideoW > 0) ? m_lastVideoW : 1920;
    int videoH = (m_lastVideoH > 0) ? m_lastVideoH : 1080;

    // 使用 float 计算长宽比
    float containerRatio = (float)containerSize.width() / (float)containerSize.height();
    float videoRatio = (float)videoW / (float)videoH;

    int targetW, targetH, x, y;

    // 自适应算法 (Fit Inside)
    if (containerRatio > videoRatio)
    {
        // 容器更宽 -> 视频高度占满，宽度居中
        targetH = containerSize.height();
        targetW = static_cast<int>(targetH * videoRatio);
        y = 0;
        x = (containerSize.width() - targetW) / 2;
    }
    else
    {
        // 容器更高 -> 视频宽度占满，高度居中
        targetW = containerSize.width();
        targetH = static_cast<int>(targetW / videoRatio);
        x = 0;
        y = (containerSize.height() - targetH) / 2;
    }

    // [Debug] 输出调试信息，如果位置不对看这里
    // qDebug() << "[Layout] Cont:" << containerSize << " Vid:" << videoW << "x" << videoH
    //          << " -> Target:" << targetW << "x" << targetH << " @ " << x << "," << y;

    // 只有发生变化时才设置，避免频繁重绘
    QRect newRect(x, y, targetW, targetH);
    if (m_yuvDisplay->geometry() != newRect)
    {
        m_yuvDisplay->setGeometry(newRect);
        m_yuvDisplay->raise();
    }
}

void TestDemoWindow::initSystem()
{
    if (!DX11Context::instance().init((void*)this->winId(), 1920, 1080))
    {
        m_statusLabel->setText("Error: DX11 Init Failed!");
        return;
    }
    loadSources();
    m_compositor->startRendering(30);
    m_statusLabel->setText("Rendering Started | Output: NV12");
}

void TestDemoWindow::loadSources()
{
    auto cameras = AssetManager::instance().getAvailableDevices(SourceType::Camera);
    auto monitors = AssetManager::instance().getAvailableDevices(SourceType::Monitor);

    if (!cameras.empty())
    {
        auto& info = cameras[0];
        auto source = SourceFactory::createSource(
            SourceType::Camera, info.pathOrId.toStdString(), info.name.toStdString());
        if (source)
        {
            m_activeSources.push_back(source);
            auto item = m_scene->addSource(source);
            if (item)
            {
                item->setPosition(50, 1080 - 300 - 50);
                item->setSize(400, 300);
                item->setZOrder(2);
            }
            m_topLayout->addWidget(new RawSourcePreview(source, m_topContainer));
        }
    }

    if (!monitors.empty())
    {
        auto& info = monitors[0];
        auto source = SourceFactory::createSource(
            SourceType::Monitor, info.pathOrId.toStdString(), info.name.toStdString());
        if (source)
        {
            m_activeSources.push_back(source);
            auto item = m_scene->addSource(source);
            if (item)
            {
                item->setPosition(0, 0);
                item->setSize(1920, 1080);
                item->setZOrder(0);
            }
            m_topLayout->addWidget(new RawSourcePreview(source, m_topContainer));
        }
    }
}

void TestDemoWindow::onYuvDataReceived(std::vector<uint8_t> const& data,
                                       int width,
                                       int height,
                                       int64_t pts)
{
    if (width != m_lastVideoW || height != m_lastVideoH)
    {
        m_lastVideoW = width;
        m_lastVideoH = height;
        updateYuvLayout();
        qDebug() << "[UI] Video Source Changed: " << width << "x" << height;
    }

    // 确保显示
    if (m_yuvDisplay->isHidden())
        m_yuvDisplay->show();

    // 双重保险：如果 YUV 窗口尺寸异常（例如被压缩成0），强制重新布局
    if (m_yuvDisplay->width() < 10)
        updateYuvLayout();

    m_yuvDisplay->updateTexture(data, width, height);

    static int counter = 0;
    if (counter++ % 30 == 0)
    {
        QString info = QString("Output: %1x%2 | PTS: %3 ms").arg(width).arg(height).arg(pts);
        if (m_recordController && m_recordController->isRecording())
        {
            info += " | REC " + m_recordController->getDurationString();
        }
        m_statusLabel->setText(info);
    }
}

void TestDemoWindow::onSettingsClicked()
{
    RecordSettingsDialog dlg(this);
    dlg.exec();
}

void TestDemoWindow::onRecordClicked()
{
    if (m_btnRecord->isChecked())
    {
        m_btnSettings->setEnabled(false);

        // 1. 读取 QSettings (UI 上次保存的值)
        QSettings s("CourseStudio", "Recorder");
        RecorderConfig config;

        // A. Video Base
        config.base.baseResolution = {1920, 1080};
        config.base.outputResolution = {1920, 1080};
        config.base.fps = 30.0;
        config.base.colorSpace = ColorSpace::Rec709;  // [Fix] 显式设置
        config.base.colorRange = ColorRange::Full;

        // B. Recording Config
        config.recording.enable = s.value("enableRecord", true).toBool();
        config.recording.pathOrUrl =
            s.value(
                 "savePath",
                 QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/output.mp4")
                .toString();
        config.recording.encoderName = "hevc_nvenc";
        config.recording.rateControl = RateControlMode::CQP;
        config.recording.qualityLevel = 20;

        // [潜在问题点] CQP 模式下，通常最好也给 bitrateKbps 设置一个值，防止编码器回退到 0 码率
        // 这里我们先故意不改，让日志暴露问题，或者你可以解除注释下面这行来修复
        config.recording.bitrateKbps = 10000;  // 10Mbps 兜底

        // C. Streaming Config
        config.streaming.enable = s.value("enableStream", false).toBool();
        // ... (其他参数保持不变)

        // [LOG] 打印 UI 层生成的配置
        qDebug() << "============ [UI CONFIG AUDIT] ============";
        qDebug() << "Rec Enable: " << config.recording.enable;
        qDebug() << "Encoder:    " << config.recording.encoderName;
        qDebug() << "RC Mode:    " << (int)config.recording.rateControl << " (0=CBR, 2=CQP)";
        qDebug() << "Quality:    " << config.recording.qualityLevel;
        qDebug() << "Bitrate:    " << config.recording.bitrateKbps << " Kbps";
        qDebug() << "Resolution: " << config.base.outputResolution;
        qDebug() << "===========================================";

        // 2. 启动
        m_recordController->startTask(config);

        m_btnRecord->setText("Starting...");
        m_durationTimer->start(500);
    }
    else
    {
        m_recordController->stopTask();
        m_durationTimer->stop();
        m_btnRecord->setText("Start Record");
        m_statusLabel->setText("Recording Stopped.");
        m_btnSettings->setEnabled(true);
    }
}

void TestDemoWindow::updateRecordDuration()
{
    if (m_recordController && m_recordController->isRecording())
    {
        m_btnRecord->setText("REC " + m_recordController->getDurationString());
    }
}
