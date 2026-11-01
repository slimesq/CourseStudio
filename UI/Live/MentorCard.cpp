#include "MentorCard.h"
#include "LiveUIConfig.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QFile>
#include <QtMath>

MentorCard::MentorCard(QWidget* parent) : QWidget(parent)
{
    // 性能优化：声明不需要背景自动填充，完全由 paintEvent 接管
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_OpaquePaintEvent, false);

    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);

    // 初始化内部交互动画
    m_scaleAnim = new QPropertyAnimation(this, "cardScale", this);
    // 从配置中心读取动画参数
    m_scaleAnim->setDuration(LiveUIConfig::instance().anim().scaleDuration);
    m_scaleAnim->setEasingCurve(LiveUIConfig::instance().anim().scaleCurve);
}

void MentorCard::setData(CourseEntity const& data)
{
    m_data = data;

    // 尝试加载本地图片缓存
    if (!m_data.localCoverPath.isEmpty() && QFile::exists(m_data.localCoverPath))
    {
        m_sourceImage.load(m_data.localCoverPath);
    }
    else
    {
        m_sourceImage = QPixmap();  // 空图片将触发占位符绘制
    }
    update();
}

void MentorCard::updateImage(QString const& localPath)
{
    // 只有当路径真正改变且文件存在时才重绘
    if (m_data.localCoverPath != localPath && QFile::exists(localPath))
    {
        m_data.localCoverPath = localPath;
        m_data.isNetworkImage = false;
        if (m_sourceImage.load(localPath))
        {
            update();
        }
    }
}

void MentorCard::updateState(bool isActive, double opacity)
{
    bool stateChanged = false;

    if (m_isActive != isActive)
    {
        m_isActive = isActive;
        stateChanged = true;
    }

    // 浮点数差异检测，避免微小变动导致的无效重绘
    if (qAbs(m_opacity - opacity) > 0.005)
    {
        m_opacity = opacity;
        stateChanged = true;
    }

    if (stateChanged)
    {
        update();
    }
}

long long MentorCard::getRoomId() const
{
    return m_data.roomId;
}

void MentorCard::setCardScale(qreal s)
{
    m_cardScale = s;
    update();
}

void MentorCard::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_isPressed = true;
        // 只有激活状态（C位）才响应按压回弹，边缘卡片点击是切换逻辑
        if (m_isActive)
        {
            m_scaleAnim->stop();
            m_scaleAnim->setEndValue(0.95);  // 按下缩小 5%
            m_scaleAnim->start();
        }
    }
    QWidget::mousePressEvent(event);
}

void MentorCard::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_isPressed)
    {
        m_isPressed = false;

        if (m_isActive)
        {
            m_scaleAnim->stop();
            m_scaleAnim->setEndValue(1.0);  // 弹回
            m_scaleAnim->start();
        }

        // 无论是激活还是非激活，点击都发射信号
        // Controller 会根据 ID 决定是进入直播间还是滚动到该卡片
        emit clicked(m_data.roomId);
    }
    QWidget::mouseReleaseEvent(event);
}

void MentorCard::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // 极低透明度时不绘制，节省 GPU 资源
    if (m_opacity < 0.01)
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setOpacity(m_opacity);

    int w = width();
    int h = height();

    // 应用交互缩放 (注意：这是基于当前 Widget 尺寸的局部中心缩放)
    p.translate(w / 2, h / 2);
    p.scale(m_cardScale, m_cardScale);
    p.translate(-w / 2, -h / 2);

    // 阴影边距 (从 Config 获取或计算，保证内容不被裁剪)
    int shadowMargin = 20;
    QRect contentRect(shadowMargin, shadowMargin, w - shadowMargin * 2, h - shadowMargin * 2);

    // 分层绘制
    drawShadow(p, contentRect);
    drawContent(p, contentRect);
}

void MentorCard::drawShadow(QPainter& p, QRect const& rect)
{
    auto const& colors = LiveUIConfig::instance().colors();

    p.save();

    // 根据状态选择阴影颜色和浓度
    QColor shadowColor = m_isActive ? colors.cardShadowActive : colors.cardShadowNormal;

    // 动态调整阴影参数
    // 激活状态：层数多，扩散大，立体感强
    // 非激活状态：层数少，性能优先
    int layers = m_isActive ? 10 : 5;
    int yOffset = m_isActive ? 8 : 4;
    double startAlpha = m_isActive ? 0.15 : 0.08;

    // 优化的阴影绘制算法 (Quadratic Decay)
    for (int i = 0; i < layers; ++i)
    {
        // 计算每一层扩大的矩形
        QRect shadowRect = rect.adjusted(-i, -i + (i / 3) + yOffset, i, i + (i / 3) + 2 + yOffset);

        // Alpha 衰减曲线
        double alphaFactor = (1.0 - (double)i / layers);
        QColor c = shadowColor;
        c.setAlphaF(startAlpha * alphaFactor * alphaFactor);

        p.setBrush(c);
        p.setPen(Qt::NoPen);

        // 圆角随扩散略微增大，保持平滑
        int radius = 16 + i;
        p.drawRoundedRect(shadowRect, radius, radius);
    }
    p.restore();
}

void MentorCard::drawContent(QPainter& p, QRect const& rect)
{
    // 创建圆角剪裁路径
    QPainterPath path;
    path.addRoundedRect(rect, 16, 16);

    p.save();
    p.setClipPath(path);

    // 1. 绘制内容 (图片或占位)
    if (!m_sourceImage.isNull())
    {
        // Aspect Fill 算法：保持比例填满矩形，居中裁剪
        QSize targetSize = rect.size();
        QSize scaledSize = m_sourceImage.size();
        scaledSize.scale(targetSize, Qt::KeepAspectRatioByExpanding);

        int sx = (scaledSize.width() - targetSize.width()) / 2;
        int sy = (scaledSize.height() - targetSize.height()) / 2;

        // 绘制图片 (使用 source rect -> target rect 的绘制方式可能更快，但这里为了逻辑清晰先 scale)
        // 注意：在大图缩小时，QPainter::SmoothPixmapTransform 至关重要
        p.drawPixmap(rect.topLeft().x() - sx,
                     rect.topLeft().y() - sy,
                     scaledSize.width(),
                     scaledSize.height(),
                     m_sourceImage);
    }
    else
    {
        // 暂无封面时的渐变背景
        QLinearGradient bgGrad(rect.topLeft(), rect.bottomRight());
        bgGrad.setColorAt(0.0, LiveUIConfig::instance().colors().backgroundStart);
        bgGrad.setColorAt(1.0, LiveUIConfig::instance().colors().backgroundEnd);
        p.fillPath(path, bgGrad);

        // 绘制占位文字
        p.setPen(LiveUIConfig::instance().colors().textInfoSub);
        p.setFont(QFont("Microsoft YaHei", 14));
        p.drawText(rect, Qt::AlignCenter, "暂无封面");
    }

    p.restore();  // 恢复 Clip 状态

    // 2. 绘制 1px 微边框 (增加精致感，防止背景色相近时边界模糊)
    p.setPen(QPen(QColor(0, 0, 0, 15), 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}
