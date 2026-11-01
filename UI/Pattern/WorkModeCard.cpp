#include "WorkModeCard.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QDebug>
#include <QtConcurrent>

// ==========================================================
// [Scheme 3 核心] 静态渲染函数
// 此函数运行在后台线程，绝对不能访问 QWidget 的成员变量(this指针)
// 必须依赖传入的 data 参数
// ==========================================================
struct RenderData
{
    QSize size;
    QString title;
    QString desc;
    QImage rawImage;  // 隐式共享，拷贝成本低
    bool isActive;
    // 字体配置
    QFont titleFont;
    QFont descFont;
    CardStyleConfig style;
};

static QImage renderCardContent(RenderData const& data)
{
    if (data.size.isEmpty())
        return QImage();

    QImage image(data.size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    int w = data.size.width();
    int h = data.size.height();

    // 动态布局计算 (基于 300x420 的比例)
    double refW = 300.0;
    double refH = 420.0;
    double ratio = (double)h / refH;

    // --- 绘制背景 (保持不变) ---
    QRect drawRect(0, 0, w, h);
    int radius = static_cast<int>(20 * ratio);
    QPainterPath path;
    path.addRoundedRect(drawRect, radius, radius);
    p.setClipPath(path);

    if (data.isActive)
    {
        QLinearGradient grad(0, 0, 0, h);
        grad.setColorAt(0.0, QColor(64, 158, 255));  // #409EFF (Ant Design Blue)
        grad.setColorAt(1.0, QColor(51, 117, 240));  // 深一点的蓝
        p.fillRect(drawRect, grad);
    }
    else
    {
        p.fillRect(drawRect, Qt::white);
    }

    // --- 3. 绘制文字 (使用配置参数) ---
    int contentPadding = static_cast<int>(24 * ratio);
    int contentW = w - (contentPadding * 2);

    // 使用配置的起始位置
    int currentY = static_cast<int>(data.style.contentTopMargin * ratio);

    // Title
    p.save();
    QFont tFont = data.titleFont;
    // [修改] 使用参数
    int tSize = data.isActive ? data.style.titleFontSizeActive : data.style.titleFontSizeNormal;
    tFont.setPixelSize(static_cast<int>(tSize * ratio));
    tFont.setBold(true);
    p.setFont(tFont);
    p.setPen(data.isActive ? Qt::white : QColor(51, 51, 51));

    // 稍微增加标题高度容纳大字体
    int titleH = static_cast<int>((tSize + 20) * ratio);
    QRect titleRect(contentPadding, currentY, contentW, titleH);
    p.drawText(titleRect, Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap, data.title);
    currentY += titleH;
    p.restore();

    // Description
    p.save();
    QFont dFont = data.descFont;
    // [修改] 使用参数
    int dSize = data.isActive ? data.style.descFontSizeActive : data.style.descFontSizeNormal;
    dFont.setPixelSize(static_cast<int>(dSize * ratio));
    p.setFont(dFont);
    p.setPen(data.isActive ? QColor(230, 230, 250) : QColor(100, 100, 100));

    currentY += static_cast<int>(10 * ratio);  // 标题和描述的间距
    int descH = static_cast<int>(80 * ratio);  // 增加高度容纳文字
    QRect descRect(contentPadding, currentY, contentW, descH);
    p.drawText(descRect, Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap, data.desc);
    p.restore();

    // --- 4. 绘制图片 (使用配置参数) ---
    // [修改] 使用参数
    int iconSize = static_cast<int>(data.style.iconBaseSize * ratio);

    int iconX = (w - iconSize) / 2;
    int marginBottom = static_cast<int>(data.style.iconBottomMargin * ratio);
    int iconY = h - iconSize - marginBottom;

    // 简单的防重叠保护：如果图片太大挤到了文字，优先保证底部边距，图片上移覆盖文字背景
    // 实际项目中可以调整文字区域高度限制

    QRect iconRect(iconX, iconY, iconSize, iconSize);

    if (!data.rawImage.isNull())
    {
        p.drawImage(iconRect, data.rawImage);
    }
    else
    {
        // [占位符]
        p.save();
        p.setPen(Qt::NoPen);
        if (data.isActive)
        {
            p.setBrush(QColor(255, 255, 255, 50));
        }
        else
        {
            p.setBrush(QColor(240, 240, 245));
        }
        p.drawEllipse(iconRect);
        p.restore();
    }

    return image;
}

// ==========================================================
// WorkModeCard 实现
// ==========================================================

WorkModeCard::WorkModeCard(ModeCardConfig const& config, QWidget* parent)
    : QWidget(parent), m_config(config)
{
    m_targetSize = QSize(260, 380);

    // [初始化参数] 在这里调整你满意的默认值
    m_styleConfig.titleFontSizeActive = 32;  // 激活时标题字号
    m_styleConfig.titleFontSizeNormal = 28;  // 普通时标题字号
    m_styleConfig.descFontSizeActive = 18;   // 激活时描述字号
    m_styleConfig.descFontSizeNormal = 16;   // 普通时描述字号
    m_styleConfig.iconBaseSize = 250;        // 图片大小 (原140，现在改大到200)
    m_styleConfig.contentTopMargin = 45;     // 文字稍微上移一点给图片腾空间
    m_styleConfig.iconBottomMargin = 30;     // 图片距离底部距离

    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_OpaquePaintEvent, false);

    initUI();
    initAsyncSystem();

    // 初始渲染：先用空图片跑一次，显示出背景和文字
    triggerAsyncRender();

    // 启动 IO 加载
    triggerAsyncLoad();
}

WorkModeCard::~WorkModeCard()
{
    if (m_loadWatcher)
    {
        m_loadWatcher->disconnect();
        if (m_loadWatcher->isRunning())
            m_loadWatcher->waitForFinished();
        delete m_loadWatcher;
    }
    if (m_renderWatcher)
    {
        m_renderWatcher->disconnect();
        if (m_renderWatcher->isRunning())
            m_renderWatcher->waitForFinished();
        delete m_renderWatcher;
    }
}

void WorkModeCard::initAsyncSystem()
{
    m_loadWatcher = new QFutureWatcher<QImage>(this);
    connect(
        m_loadWatcher, &QFutureWatcher<QImage>::finished, this, &WorkModeCard::onRawImageLoaded);

    m_renderWatcher = new QFutureWatcher<QImage>(this);
    connect(
        m_renderWatcher, &QFutureWatcher<QImage>::finished, this, &WorkModeCard::onRenderFinished);
}

void WorkModeCard::initUI()
{
    // 这里的布局只是为了防止 Widget 没有任何 SizeHint，
    // 实际上我们在 paintEvent 中手动绘制所有内容
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->addStretch();
}

void WorkModeCard::setStandardSize(QSize const& size)
{
    // 如果目标尺寸变了
    if (qAbs(m_targetSize.width() - size.width()) > 2 ||
        qAbs(m_targetSize.height() - size.height()) > 2)
    {
        m_targetSize = size;

        // [关键策略]
        // 这里不调用 update()，也不清空 m_cachePixmap
        // 界面上继续显示旧的 m_cachePixmap (会被 paintEvent 拉伸)
        // 立即触发后台渲染
        triggerAsyncRender();
    }
}

void WorkModeCard::triggerAsyncLoad()
{
    if (m_config.imagePath.isEmpty())
        return;
    if (m_loadWatcher->isRunning())
        return;

    QString path = m_config.imagePath;
    QFuture<QImage> future = QtConcurrent::run([path]() -> QImage {
        QImage img(path);
        if (!img.isNull() && img.format() != QImage::Format_ARGB32_Premultiplied)
        {
            img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        }
        return img;
    });
    m_loadWatcher->setFuture(future);
}

void WorkModeCard::onRawImageLoaded()
{
    QImage result = m_loadWatcher->result();
    if (!result.isNull())
    {
        m_rawImage = result;  // 存入 Cache L1
        // 图片到了，重新渲染全卡片
        triggerAsyncRender();
    }
}

void WorkModeCard::triggerAsyncRender()
{
    // 准备数据包
    RenderData data;
    qreal dpr = devicePixelRatioF();
    if (dpr < 1.0)
        dpr = 1.0;

    data.size = m_targetSize * dpr;
    data.title = m_config.title;
    data.desc = m_config.description;
    data.isActive = m_isActive;
    data.rawImage = m_rawImage;
    data.titleFont = this->font();
    data.descFont = this->font();

    // [关键] 将成员变量中的配置传递给渲染线程
    data.style = m_styleConfig;

    QFuture<QImage> future = QtConcurrent::run(renderCardContent, data);
    m_renderWatcher->setFuture(future);
}

void WorkModeCard::onRenderFinished()
{
    QImage result = m_renderWatcher->result();
    if (!result.isNull())
    {
        qreal dpr = devicePixelRatioF();
        if (dpr < 1.0)
            dpr = 1.0;

        result.setDevicePixelRatio(dpr);

        // [双缓冲切换点]
        // 只有当新图生成好了，才替换旧图
        m_cachePixmap = QPixmap::fromImage(result);

        // 触发重绘，此时 paintEvent 会使用新的 m_cachePixmap
        update();
    }
}

void WorkModeCard::setActive(bool active)
{
    if (m_isActive == active)
        return;
    m_isActive = active;
    // 状态改变，必须重绘
    triggerAsyncRender();
}

void WorkModeCard::setScrollingState(bool isScrolling)
{
    if (m_isScrolling == isScrolling)
        return;
    m_isScrolling = isScrolling;
    update();  // 滚动状态只影响阴影绘制，不需要重绘内容，update 即可
}

void WorkModeCard::setStyleConfig(CardStyleConfig const& config)
{
    m_styleConfig = config;
    triggerAsyncRender();  // 参数变了，立即重绘
}

void WorkModeCard::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);  // 关键：平滑缩放

    // 绘制阴影 (独立于 Cache)
    if (!m_isActive || m_isScrolling)
    {
        p.save();
        int shadowOffset = m_isActive ? 8 : 4;
        int shadowAlpha = m_isActive ? 60 : 30;
        // 这里的 rect() 是当前控件的实际大小
        QRect shadowRect = rect().adjusted(4, 4 + shadowOffset, -4, -4 + shadowOffset);
        p.setBrush(QColor(0, 0, 0, shadowAlpha));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(shadowRect, 20, 20);
        p.restore();
    }

    if (!m_cachePixmap.isNull())
    {
        // [Scheme 3 视觉核心]
        // 这里的 m_cachePixmap 可能是旧尺寸的图片
        // 这里的 rect() 是当前最新的尺寸
        // drawPixmap 会自动进行拉伸/缩放
        // 因为开启了 SmoothPixmapTransform，所以拉伸是平滑模糊的，不会出现马赛克
        // 当后台 onRenderFinished 回来后，m_cachePixmap 会变成新尺寸，拉伸消失，变回高清
        p.drawPixmap(rect(), m_cachePixmap);
    }
}

void WorkModeCard::resizeEvent(QResizeEvent* event)
{
    Q_UNUSED(event);
    // 屏蔽默认行为，由 WorkSpaceWindow 统一管理尺寸逻辑
}

void WorkModeCard::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        m_isPressed = true;
    QWidget::mousePressEvent(event);
}

void WorkModeCard::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_isPressed)
    {
        m_isPressed = false;
        emit clicked(m_config.title);
    }
    QWidget::mouseReleaseEvent(event);
}
