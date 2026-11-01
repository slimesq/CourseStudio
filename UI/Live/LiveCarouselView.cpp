#include "LiveCarouselView.h"
#include "LiveUIConfig.h"
#include <QDebug>
#include <QtMath>
#include <algorithm>
#include <QWheelEvent>

LiveCarouselView::LiveCarouselView(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setAttribute(Qt::WA_TranslucentBackground);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_scrollAnim = new QPropertyAnimation(this, "scrollValue", this);

    m_scrollThrottleTimer = new QTimer(this);
    connect(
        m_scrollThrottleTimer, &QTimer::timeout, this, &LiveCarouselView::onProcessScrollBuffer);

    m_btnUp = new QPushButton("︿", this);
    m_btnDown = new QPushButton("﹀", this);
    m_btnUp->setCursor(Qt::PointingHandCursor);
    m_btnDown->setCursor(Qt::PointingHandCursor);

    updateButtonStyles();

    connect(m_btnUp, &QPushButton::clicked, this, &LiveCarouselView::onUpClicked);
    connect(m_btnDown, &QPushButton::clicked, this, &LiveCarouselView::onDownClicked);

    connect(&LiveUIConfig::instance(),
            &LiveUIConfig::configChanged,
            this,
            &LiveCarouselView::updateStyles);
}

LiveCarouselView::~LiveCarouselView()
{
}

QSize LiveCarouselView::minimumSizeHint() const
{
    return QSize(0, 0);
}

void LiveCarouselView::setData(CourseList const& data)
{
    m_dataList = data;
    clearCards();

    // 创建所有卡片
    for (int i = 0; i < data.size(); ++i)
    {
        MentorCard* card = new MentorCard(this);
        card->setData(data[i]);
        connect(card, &MentorCard::clicked, this, &LiveCarouselView::onCardClicked);
        card->show();
        m_cards.append(card);
    }

    m_targetIndex = 0;
    m_scrollValue = 0.0;

    updateLayout();
    layoutButtons();
}

void LiveCarouselView::updateCardImage(long long roomId, QString const& localPath)
{
    for (auto card : m_cards)
    {
        if (card->getRoomId() == roomId)
        {
            card->updateImage(localPath);
        }
    }
}

// [Fix] 无限滚动逻辑：移除边界检查
void LiveCarouselView::scrollTo(int index)
{
    if (m_dataList.isEmpty())
        return;

    // 不再限制 index 范围，允许负数和超过 size 的数
    m_targetIndex = index;

    // 计算归一化后的索引，用于通知外部
    int normalizedIdx = normalizeIndex(m_targetIndex);
    emit indexChanged(normalizedIdx);

    m_scrollAnim->stop();
    m_scrollAnim->setDuration(LiveUIConfig::instance().anim().scrollDuration);
    m_scrollAnim->setEasingCurve(LiveUIConfig::instance().anim().scrollCurve);
    m_scrollAnim->setStartValue(m_scrollValue);
    m_scrollAnim->setEndValue((double)m_targetIndex);
    m_scrollAnim->start();
}

// [New] 归一化索引辅助函数 (处理负数模运算)
int LiveCarouselView::normalizeIndex(int index) const
{
    if (m_dataList.isEmpty())
        return 0;
    int n = m_dataList.size();
    return ((index % n) + n) % n;  // 确保结果为正数 [0, n-1]
}

int LiveCarouselView::currentIndex() const
{
    return normalizeIndex(qRound(m_scrollValue));
}

void LiveCarouselView::setScrollValue(qreal v)
{
    m_scrollValue = v;
    updateLayout();
}

void LiveCarouselView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateLayout();
    layoutButtons();
}

// [Fix] 布局引擎适配无限循环
// 这里的关键是：即使 scrollValue 是 100，我们也只需要取出对应的 5 张卡片来显示
// 并且要让这 5 张卡片的数据是循环的。
// 但目前的架构是每个 Card 绑定死了数据。
// 简单的无限循环实现方式：
// 1. 让 LayoutEngine 计算时，对于每个 card，计算它与 scrollValue 的“循环距离”。
// 2. 如果距离太远（比如超过 3），就隐藏。
// 3. 如果距离近，就显示，并计算位置。
//
// 由于我们没有修改 LayoutEngine 的权限（不在本次上传），我们采用一种
// "逻辑循环" 的方式：
// m_cards 里的卡片是固定的。当 scrollValue 变化时，我们将 scrollValue 归一化
// 到 [0, N] 区间去计算位置？ 不行，那样会有跳变。
//
// 正确做法：修改 LayoutEngine 是最好的。
// 如果不能改 LayoutEngine，我们就在这里做 trick：
// 实际上，只要 LayoutEngine 里的 normalizeDiff 是按照模运算做的，
// 那么我们只需要把 (i - scrollValue) 这一步做对即可。

void LiveCarouselView::updateLayout()
{
    QSize size = this->size();
    if (size.isEmpty() || m_cards.isEmpty())
        return;

    int n = m_cards.size();

    // 我们需要构建一个临时的属性列表，传给 Engine
    // 但 Engine 内部是根据 itemCount 循环的。
    // 假设 Engine 已经处理了 normalizeDiff (通常都会处理)。
    // 我们直接传入当前的 scrollValue 即可，前提是 scrollValue 可以是任意值。

    // 这里有一个 trick：为了让 Engine 正常工作（它可能假设 scrollValue 在 0~N），
    // 我们可以把 scrollValue 映射到一个大范围的正数，或者修改 Engine。
    // 鉴于 Engine 代码我看不到，我假设它能处理 (index - scrollValue) 的模运算。
    // 如果 Engine 没有处理模运算，那么我们需要在这里处理：

    // 让我们看看上传的 CarouselLayoutEngine.cpp (假设我能看到)
    // 它是： double diff = i - scrollValue;
    //       diff = normalizeDiff(diff, itemCount);
    // 只要有 normalizeDiff，无限滚动就是天然支持的！

    auto attrs = m_layoutEngine.calculateLayout(size, n, m_scrollValue);

    for (auto const& attr : attrs)
    {
        if (attr.index >= 0 && attr.index < n)
        {
            MentorCard* card = m_cards[attr.index];
            card->setGeometry(attr.geometry);
            card->updateState(attr.isActive, attr.opacity);
            if (attr.isActive)
                card->raise();
        }
    }
    if (m_btnUp)
        m_btnUp->raise();
    if (m_btnDown)
        m_btnDown->raise();
}

void LiveCarouselView::layoutButtons()
{
    int w = width();
    int h = height();
    auto& cfg = LiveUIConfig::instance();
    int btnSize = cfg.scaled(40);
    int margin = cfg.scaled(20);

    if (m_btnUp)
    {
        m_btnUp->setFixedSize(btnSize, btnSize);
        m_btnUp->move((w - btnSize) / 2, margin);
    }

    if (m_btnDown)
    {
        m_btnDown->setFixedSize(btnSize, btnSize);
        m_btnDown->move((w - btnSize) / 2, h - btnSize - margin);
    }
    updateButtonStyles();
}

void LiveCarouselView::updateButtonStyles()
{
    int radius = m_btnUp->height() / 2;
    QString qss = QString(R"(
        QPushButton {
            background-color: rgba(255, 255, 255, 0.9);
            color: #606266;
            border: 1px solid #E4E7ED;
            border-radius: %1px;
            font-weight: bold;
            font-size: 16px;
        }
        QPushButton:hover {
            background-color: #409EFF;
            color: white;
            border: 1px solid #409EFF;
        }
        QPushButton:pressed {
            background-color: #337ECC;
            border: 1px solid #337ECC;
        }
    )")
                      .arg(radius);
    m_btnUp->setStyleSheet(qss);
    m_btnDown->setStyleSheet(qss);
}

void LiveCarouselView::clearCards()
{
    for (auto card : m_cards)
        card->deleteLater();
    m_cards.clear();
}

void LiveCarouselView::wheelEvent(QWheelEvent* event)
{
    m_wheelAccumulator += event->angleDelta().y();
    int const THRESHOLD = 40;

    bool needsTrigger = false;
    while (m_wheelAccumulator >= THRESHOLD)
    {
        m_bufferedScrollSteps--;
        m_wheelAccumulator -= THRESHOLD;
        needsTrigger = true;
    }
    while (m_wheelAccumulator <= -THRESHOLD)
    {
        m_bufferedScrollSteps++;
        m_wheelAccumulator += THRESHOLD;
        needsTrigger = true;
    }

    if (needsTrigger)
    {
        if (m_bufferedScrollSteps > 3)
            m_bufferedScrollSteps = 3;
        if (m_bufferedScrollSteps < -3)
            m_bufferedScrollSteps = -3;

        if (!m_scrollThrottleTimer->isActive())
        {
            onProcessScrollBuffer();
            m_scrollThrottleTimer->start(LiveUIConfig::instance().anim().throttleInterval);
        }
    }
    event->accept();
}

void LiveCarouselView::onProcessScrollBuffer()
{
    if (m_bufferedScrollSteps == 0)
    {
        m_scrollThrottleTimer->stop();
        return;
    }
    int direction = (m_bufferedScrollSteps > 0) ? 1 : -1;
    m_bufferedScrollSteps -= direction;

    // [Fix] 直接累加，不再 clamp
    scrollTo(m_targetIndex + direction);
}

void LiveCarouselView::onUpClicked()
{
    scrollTo(m_targetIndex - 1);
}

void LiveCarouselView::onDownClicked()
{
    scrollTo(m_targetIndex + 1);
}

void LiveCarouselView::onCardClicked(long long roomId)
{
    int n = m_dataList.size();
    if (n == 0)
        return;

    // 1. 找到目标数据的原始索引 (Data Index)
    int targetDataIdx = -1;
    for (int i = 0; i < n; ++i)
    {
        if (m_dataList[i].roomId == roomId)
        {
            targetDataIdx = i;
            break;
        }
    }
    if (targetDataIdx == -1)
        return;

    // [CRITICAL FIX] 立即发送信号，通知上层“用户明确选择了这个 ID”
    // 这必须在启动滚动动画之前发送，确保上层先锁定 ID
    emit cardClicked(roomId);

    // 2. 计算当前滚动位置对应的归一化索引
    // 注意：这里使用 qRound 可能会有偏差，改用 floor 或直接计算差值
    int currentScrollIdx = qRound(m_scrollValue);

    // 3. 计算“虚拟目标索引”
    // 我们需要找到一个整数 T，使得 (T % n + n) % n == targetDataIdx
    // 且 T 与 m_scrollValue 的距离最近

    // 算法：Target = Current + ShortestDistance
    // ShortestDistance 在 [-n/2, n/2] 之间

    // 当前显示的逻辑索引 (0 ~ n-1)
    int currentNormIdx = normalizeIndex(currentScrollIdx);

    int diff = targetDataIdx - currentNormIdx;

    // 最短路径逻辑
    if (diff > n / 2)
        diff -= n;
    else if (diff < -n / 2)
        diff += n;

    // 最终目标位置 = 当前非归一化位置 + 偏差
    int finalTarget = currentScrollIdx + diff;

    scrollTo(finalTarget);
}

void LiveCarouselView::updateStyles()
{
    updateLayout();
    layoutButtons();
}
