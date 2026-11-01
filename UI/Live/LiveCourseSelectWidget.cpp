#include "LiveCourseSelectWidget.h"
#include "LiveInfoPanel.h"
#include "LiveCarouselView.h"
#include "LiveUIConfig.h"
#include "AssetCacheManager.h"
#include <QResizeEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QDebug>
#include <QTimer>

LiveCourseSelectWidget::LiveCourseSelectWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(0, 0);
    initUI();
    initSignals();
    loadBackground(":/UI/QSS/srs/bg.png");
}

LiveCourseSelectWidget::~LiveCourseSelectWidget()
{
}

QSize LiveCourseSelectWidget::minimumSizeHint() const
{
    return QSize(0, 0);
}

void LiveCourseSelectWidget::initUI()
{
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->setSizeConstraint(QLayout::SetNoConstraint);

    m_infoPanel = new LiveInfoPanel(this);
    mainLayout->addWidget(m_infoPanel, 45);

    m_carousel = new LiveCarouselView(this);
    mainLayout->addWidget(m_carousel, 55);

    resetLayoutConstraints();
}

void LiveCourseSelectWidget::initSignals()
{
    connect(m_carousel,
            &LiveCarouselView::indexChanged,
            this,
            &LiveCourseSelectWidget::onCarouselIndexChanged);
    connect(
        m_carousel, &LiveCarouselView::cardClicked, this, &LiveCourseSelectWidget::onCardSelected);
    connect(
        m_infoPanel, &LiveInfoPanel::enterClicked, this, &LiveCourseSelectWidget::onEnterClicked);
    connect(&AssetCacheManager::instance(),
            &AssetCacheManager::assetReady,
            this,
            &LiveCourseSelectWidget::onAssetDownloaded);
}

void LiveCourseSelectWidget::resetLayoutConstraints()
{
    // 不要 sendEvent 给 m_infoPanel，这会打断正在进行的透明度动画
    // 仅仅通知布局系统需要更新
    updateGeometry();
    if (m_infoPanel)
    {
        m_infoPanel->updateGeometry();
    }
}

void LiveCourseSelectWidget::suspendRendering()
{
}

void LiveCourseSelectWidget::resumeRendering()
{
    m_isNavigating = false;
    if (m_infoPanel)
        m_infoPanel->setButtonLoading(false);
    update();
}

void LiveCourseSelectWidget::loadRooms(QJsonArray const& roomList)
{
    m_courseList.clear();
    m_currentRoomId = -1;
    m_isNavigating = false;

    if (roomList.isEmpty())
    {
        m_courseList.append(CourseEntity::createEmpty());
    }
    else
    {
        int idx = 0;
        for (auto const& val : roomList)
        {
            QJsonObject obj = val.toObject();
            CourseEntity entity = CourseEntity::fromJson(obj, idx++);
            if (entity.isNetworkImage)
            {
                QString local = AssetCacheManager::instance().queryLocalPath(entity.coverUrl);
                if (!local.isEmpty())
                {
                    entity.localCoverPath = local;
                    entity.isNetworkImage = false;
                }
                else
                {
                    AssetCacheManager::instance().cacheAsset(entity.coverUrl, false);
                }
            }
            m_courseList.append(entity);
        }
    }
    m_carousel->setData(m_courseList);
    if (!m_courseList.isEmpty())
    {
        m_currentRoomId = m_courseList[0].roomId;
        m_infoPanel->setData(m_courseList[0], false);
        m_isInitialized = true;
    }
}

void LiveCourseSelectWidget::onCarouselIndexChanged(int index)
{
    // [CRITICAL FIX] 增加防抖保护
    // 只有当非导航状态（用户手动拖动或滚轮）时，才通过索引更新 ID
    // 如果是点击卡片触发的自动滚动，以点击事件为准，忽略中间过程的索引变化

    if (m_isNavigating)
        return;  // 如果正在请求进入，忽略滚动带来的 ID 变化

    if (index >= 0 && index < m_courseList.size())
    {
        auto const& entity = m_courseList[index];

        // 如果当前 ID 已经是由点击事件设置的正确 ID，且滚动带来的 index 对应的 ID 也是同一个，
        // 则无事发生。如果是用户手动划动，则更新。
        if (m_currentRoomId != entity.roomId)
        {
            m_currentRoomId = entity.roomId;
            bool shouldAnimate = m_isInitialized && (width() > 100);
            m_infoPanel->setData(entity, shouldAnimate);
            m_infoPanel->setButtonLoading(false);
            m_isInitialized = true;
        }
    }
}

void LiveCourseSelectWidget::onCardSelected(long long roomId)
{
    // [CRITICAL FIX] 直接信任点击的 ID，不再依赖 indexChanged 的滞后更新
    if (m_currentRoomId == roomId)
        return;

    m_currentRoomId = roomId;

    // 立即更新信息面板，不需要等待滚动动画结束
    // 反向查找数据实体
    for (auto const& entity : m_courseList)
    {
        if (entity.roomId == roomId)
        {
            // 强制更新面板，即使动画还在进行中
            m_infoPanel->setData(entity, true);
            break;
        }
    }
}

void LiveCourseSelectWidget::onEnterClicked()
{
    if (m_currentRoomId <= 0)
        return;

    if (m_isNavigating)
    {
        qWarning() << "[UI] Blocked duplicate click for Room:" << m_currentRoomId;
        return;
    }

    m_isNavigating = true;
    m_infoPanel->setButtonLoading(true);

    qDebug() << "[UI] >>> Trigger Request Room:" << m_currentRoomId;
    emit requestRoomDetail(m_currentRoomId);

    // 5秒超时保护
    QTimer::singleShot(5000, this, [this]() {
        if (m_isNavigating)
        {
            m_isNavigating = false;
            if (m_infoPanel)
                m_infoPanel->setButtonLoading(false);
        }
    });
}

void LiveCourseSelectWidget::stopCardLoading(long long roomId)
{
    Q_UNUSED(roomId);
    if (m_infoPanel)
        m_infoPanel->setButtonLoading(false);

    // 延迟解锁，防止瞬间双击
    QTimer::singleShot(500, this, [this]() { m_isNavigating = false; });
}

void LiveCourseSelectWidget::loadBackground(QString const& path)
{
    if (QFile::exists(path))
    {
        m_bgPixmap.load(path);
        update();
    }
}

void LiveCourseSelectWidget::onAssetDownloaded(QString const& url,
                                               QString const& localPath,
                                               bool isTemp)
{
    Q_UNUSED(isTemp);
    for (auto const& course : m_courseList)
    {
        if (course.coverUrl == url)
        {
            m_carousel->updateCardImage(course.roomId, localPath);
        }
    }
}

// LiveCourseSelectWidget.cpp
void LiveCourseSelectWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // 关键修正：只有宽度发生显著变化（通常是拉伸窗口）才允许重算 Scale
    // 如果只是内部高度微调触发的 resize，忽略它
    if (qAbs(event->oldSize().width() - event->size().width()) < 2)
    {
        return;
    }

    int w = width();
    double scale = (double)w / 1200.0;
    if (qAbs(w - 1200) < 10)
        scale = 1.0;

    // 这里会触发 updateStyles()，所以必须严格限制触发频率
    LiveUIConfig::instance().setUiScale(scale);
}
void LiveCourseSelectWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    int r = 16;
    QRectF rect = this->rect();
    QPainterPath path;
    path.addRoundedRect(rect, r, r);
    p.setClipPath(path);

    if (!m_bgPixmap.isNull())
    {
        QSize widgetSize = this->size();
        QPixmap scaledBg =
            m_bgPixmap.scaled(widgetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        int x = (widgetSize.width() - scaledBg.width()) / 2;
        int y = (widgetSize.height() - scaledBg.height()) / 2;
        p.drawPixmap(x, y, scaledBg);
    }
    else
    {
        p.fillPath(path, Qt::white);
    }
}
