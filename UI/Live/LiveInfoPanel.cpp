#include "LiveInfoPanel.h"
#include "LiveUIConfig.h"
#include <QPainter>
#include <QDebug>
#include <QGraphicsDropShadowEffect>
#include <QTextBlock>
#include <QStyleOption>
#include <QApplication>

// ============================================================================
// LiveTextWidget Implementation (The Core Fix)
// ============================================================================
LiveTextWidget::LiveTextWidget(QWidget* parent) : QWidget(parent)
{
    m_doc = new QTextDocument(this);
    m_doc->setDocumentMargin(0);
    // 垂直方向由内容决定，水平方向尽可能占满
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_TransparentForMouseEvents);  // 鼠标穿透，像 Label 一样
}

void LiveTextWidget::setText(QString const& text)
{
    m_doc->setPlainText(text);
    recalculateHeight();
    update();
}

// LiveInfoPanel.cpp 中的 LiveTextWidget
void LiveTextWidget::setTextStyle(QFont const& font, QColor const& color, int lineHeight)
{
    bool changed = false;
    if (m_doc->defaultFont() != font)
    {
        m_doc->setDefaultFont(font);
        changed = true;
    }

    QString css =
        QString("body { color: %1; line-height: %2%; }").arg(color.name()).arg(lineHeight);
    if (m_doc->defaultStyleSheet() != css)
    {
        m_doc->setDefaultStyleSheet(css);
        changed = true;
    }

    if (changed)
    {
        // 关键：不要重设 Text，直接标记布局失效即可
        m_doc->markContentsDirty(0, m_doc->characterCount());
        recalculateHeight();
        update();
    }
}
void LiveTextWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // 宽度变化时，立即重算高度
    recalculateHeight();
}

void LiveTextWidget::recalculateHeight()
{
    int w = width();
    if (w <= 0)
        w = 100;
    m_doc->setTextWidth(w);
    int h = static_cast<int>(m_doc->size().height());

    if (h != m_fixedHeight)
    {
        m_fixedHeight = h;
        // 关键：不要在这里 setFixedHeight 或 setMin/Max Height
        // 只需要调用 updateGeometry，Qt 的布局管理器会在处理事件循环时询问 sizeHint
        this->updateGeometry();
    }
}

QSize LiveTextWidget::sizeHint() const
{
    return QSize(100, m_fixedHeight);
}

QSize LiveTextWidget::minimumSizeHint() const
{
    return QSize(0, m_fixedHeight);
}

void LiveTextWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    // 自绘不需要处理复杂的 background 擦除，透明背景直接画
    // p.setRenderHint(QPainter::Antialiasing); // 文本绘制不需要这个，QPainter 默认处理字体抗锯齿

    // 绘制文档内容
    m_doc->drawContents(&p, rect());
}

// ============================================================================
// LiveInfoPanel Implementation
// ============================================================================

LiveInfoPanel::LiveInfoPanel(QWidget* parent) : QWidget(parent)
{
    // 基础属性
    setAttribute(Qt::WA_StyledBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    initUI();

    // 动画
    m_textAnim = new QPropertyAnimation(this, "textOpacity", this);
    m_textAnim->setDuration(250);
    m_textAnim->setEasingCurve(QEasingCurve::InOutQuad);
    connect(m_textAnim, &QPropertyAnimation::finished, this, [this]() {
        if (m_textOpacity > 0.99)
        {
            for (auto effect : m_textEffects)
                effect->setEnabled(false);
        }
    });
    // Loading 定时器
    m_loadingTimer = new QTimer(this);
    m_loadingTimer->setInterval(350);
    connect(m_loadingTimer, &QTimer::timeout, this, &LiveInfoPanel::onLoadingTimer);

    // 配置监听
    connect(&LiveUIConfig::instance(),
            &LiveUIConfig::configChanged,
            this,
            &LiveInfoPanel::updateStyles);

    // 初始化样式
    updateStyles();
}

LiveInfoPanel::~LiveInfoPanel()
{
    if (m_loadingTimer->isActive())
        m_loadingTimer->stop();
}

QSize LiveInfoPanel::minimumSizeHint() const
{
    return QSize(0, 0);
}

void LiveInfoPanel::initUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);
    m_mainLayout->setAlignment(Qt::AlignTop);

    // --- A. 静态顶部 (Tag) ---
    // (代码保持不变) ...
    m_tagContainer = new QWidget(this);
    QHBoxLayout* tagLayout = new QHBoxLayout(m_tagContainer);
    tagLayout->setContentsMargins(0, 0, 0, 0);
    m_lblTag = new QLabel("直播课", this);
    tagLayout->addWidget(m_lblTag);
    tagLayout->addStretch();
    m_mainLayout->addWidget(m_tagContainer);

    m_titleTopSpacer = new QWidget(this);
    m_mainLayout->addWidget(m_titleTopSpacer);

    // --- B. 动态内容区域 ---
    m_dynamicContainer = new QWidget(this);
    QVBoxLayout* dynamicLayout = new QVBoxLayout(m_dynamicContainer);
    dynamicLayout->setContentsMargins(0, 0, 0, 0);
    dynamicLayout->setSpacing(0);
    dynamicLayout->setAlignment(Qt::AlignTop);

    // [Modify] 移除原来的容器级特效设置代码
    // if (m_dynamicContainer) m_dynamicContainer->setGraphicsEffect(...) -> 删除!

    // 1. 标题 (需要动画)
    m_txtTitle = new LiveTextWidget(m_dynamicContainer);
    addOpacityEffect(m_txtTitle);  // <--- 关键：只给标题加特效
    dynamicLayout->addWidget(m_txtTitle);

    dynamicLayout->addSpacing(20);

    // 2. 时间行 (图标不动，文字动)
    m_rowTime = createInfoRow("📅", m_lblTime);
    addOpacityEffect(m_lblTime);  // <--- 关键：只给时间文字加特效
    dynamicLayout->addWidget(m_rowTime);

    // 3. 讲师行 (图标不动，文字动)
    m_rowLecturer = createInfoRow("👤", m_lblLecturer);
    addOpacityEffect(m_lblLecturer);  // <--- 关键：只给讲师文字加特效
    dynamicLayout->addWidget(m_rowLecturer);

    // 4. 描述行 (图标不动，文字动)
    QWidget* rowDesc = new QWidget(m_dynamicContainer);
    QHBoxLayout* descLayout = new QHBoxLayout(rowDesc);
    descLayout->setContentsMargins(0, 0, 0, 0);
    descLayout->setSpacing(10);
    descLayout->setAlignment(Qt::AlignTop);

    QLabel* iconDesc = new QLabel("📄", rowDesc);
    iconDesc->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    iconDesc->setObjectName("IconDesc");
    // 图标 iconDesc 不加特效，所以它会一直显示

    m_txtDesc = new LiveTextWidget(rowDesc);
    addOpacityEffect(m_txtDesc);  // <--- 关键：只给描述文字加特效

    descLayout->addWidget(iconDesc);
    descLayout->addWidget(m_txtDesc);
    dynamicLayout->addWidget(rowDesc);

    m_mainLayout->addWidget(m_dynamicContainer);
    m_mainLayout->addStretch(1);

    // --- C. 静态底部区域 (平台图标 + 按钮) ---

    // 平台图标
    m_platformContainer = new QWidget(this);
    QHBoxLayout* platLayout = new QHBoxLayout(m_platformContainer);
    platLayout->setContentsMargins(0, 0, 0, 0);
    QStringList platforms = {"📺", "▶️", "🎵"};
    for (auto const& p : platforms)
    {
        QLabel* icon = new QLabel(p, this);
        icon->setAlignment(Qt::AlignCenter);
        icon->setObjectName("PlatIcon");
        platLayout->addWidget(icon);
    }
    platLayout->addStretch();
    m_mainLayout->addWidget(m_platformContainer);

    // 按钮
    m_btnContainer = new QWidget(this);
    QVBoxLayout* btnLayout = new QVBoxLayout(m_btnContainer);
    btnLayout->setContentsMargins(0, 0, 0, 0);
    btnLayout->setAlignment(Qt::AlignLeft);

    m_btnEnter = new QPushButton("进入直播间", m_btnContainer);
    m_btnEnter->setObjectName("btnEnterLive");
    m_btnEnter->setCursor(Qt::PointingHandCursor);

    // 阴影保持不变
    QGraphicsDropShadowEffect* btnShadow = new QGraphicsDropShadowEffect(this);
    btnShadow->setBlurRadius(25);
    btnShadow->setColor(QColor(64, 158, 255, 120));
    btnShadow->setOffset(0, 10);
    m_btnEnter->setGraphicsEffect(btnShadow);

    connect(m_btnEnter, &QPushButton::clicked, this, &LiveInfoPanel::enterClicked);
    btnLayout->addWidget(m_btnEnter);

    m_mainLayout->addWidget(m_btnContainer);
}

QWidget* LiveInfoPanel::createInfoRow(QString const& iconChar, QLabel*& outLabel)
{
    QWidget* w = new QWidget(this);
    QHBoxLayout* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(10);  // 默认间距，updateStyles 会覆盖
    l->setAlignment(Qt::AlignTop);

    QLabel* icon = new QLabel(iconChar, w);
    icon->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    icon->setObjectName("InfoIcon");  // 用于样式定位

    outLabel = new QLabel(w);
    outLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    outLabel->setWordWrap(false);  // 单行文本不需要换行

    l->addWidget(icon);
    l->addWidget(outLabel);
    return w;
}

void LiveInfoPanel::addOpacityEffect(QWidget* target)
{
    if (!target)
        return;
    QGraphicsOpacityEffect* effect = new QGraphicsOpacityEffect(this);
    effect->setOpacity(1.0);
    effect->setEnabled(false);  // 默认禁用
    target->setGraphicsEffect(effect);
    m_textEffects.append(effect);
}

void LiveInfoPanel::setData(CourseEntity const& data, bool animate)
{
    if (m_data.roomId == data.roomId)
        return;
    m_data = data;

    auto doUpdate = [this]() {
        m_txtTitle->setText(m_data.title);
        m_txtDesc->setText(m_data.description);
        m_lblTime->setText(m_data.timeStr);
        m_lblLecturer->setText(m_data.lecturerName);
        m_mainLayout->activate();  // 刷新布局
    };

    if (animate)
    {
        m_textAnim->stop();
        m_textAnim->disconnect(SIGNAL(finished()));

        // 动画开始前，启用所有文字的特效
        for (auto effect : m_textEffects)
            effect->setEnabled(true);

        m_textAnim->setStartValue(m_textOpacity);
        m_textAnim->setEndValue(0.0);
        m_textAnim->setDuration(150);

        connect(m_textAnim, &QPropertyAnimation::finished, this, [this, doUpdate]() {
            doUpdate();
            m_textAnim->disconnect(SIGNAL(finished()));

            m_textAnim->setStartValue(0.0);
            m_textAnim->setEndValue(1.0);
            m_textAnim->setDuration(200);

            // 动画彻底结束后，禁用特效以获得最清晰的文字渲染
            connect(m_textAnim, &QPropertyAnimation::finished, this, [this]() {
                if (m_textOpacity > 0.95)
                {
                    for (auto effect : m_textEffects)
                        effect->setEnabled(false);
                }
            });
            m_textAnim->start();
        });
        m_textAnim->start();
    }
    else
    {
        doUpdate();
        setTextOpacity(1.0);
        // 不动画时直接禁用特效
        for (auto effect : m_textEffects)
            effect->setEnabled(false);
    }
}

void LiveInfoPanel::setTextOpacity(double opacity)
{
    m_textOpacity = opacity;
    // 遍历列表，批量设置
    for (auto effect : m_textEffects)
    {
        // 性能优化：如果不透明，甚至可以 setEnabled(false) (在动画回调里做了，这里只管设置值)
        effect->setOpacity(opacity);
        if (opacity < 0.99 && !effect->isEnabled())
        {
            effect->setEnabled(true);
        }
    }
}

void LiveInfoPanel::setButtonLoading(bool isLoading)
{
    m_isLoading = isLoading;
    m_btnEnter->setEnabled(!isLoading);

    if (isLoading)
    {
        if (!m_loadingTimer->isActive())
        {
            m_loadingDots = 0;
            m_loadingTimer->start();
            onLoadingTimer();
        }
    }
    else
    {
        m_loadingTimer->stop();
        m_btnEnter->setText("进入直播间");
    }
}

void LiveInfoPanel::onLoadingTimer()
{
    m_loadingDots = (m_loadingDots + 1) % 4;
    QString baseText = "正在进入";
    for (int i = 0; i < m_loadingDots; ++i)
        baseText.append(".");
    m_btnEnter->setText(baseText);
}

void LiveInfoPanel::forceLayoutRefresh()
{
    // 强制触发一次 resize 事件给所有子组件
    // 自绘组件会在 resizeEvent 中自动重算高度
    QResizeEvent re(this->size(), this->size());
    QApplication::sendEvent(this, &re);

    if (m_txtTitle)
        QApplication::sendEvent(m_txtTitle, &re);
    if (m_txtDesc)
        QApplication::sendEvent(m_txtDesc, &re);
}

void LiveInfoPanel::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void LiveInfoPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // 这里不需要像之前那样做复杂的 Timer 延迟
    // 因为 LiveTextWidget 内部已经处理了同步的高度计算
}

void LiveInfoPanel::updateStyles()
{
    auto& cfg = LiveUIConfig::instance();
    auto const& d = cfg.dimens();
    auto const& c = cfg.colors();
    int marginH = cfg.scaled(d.leftPanelMarginH);

    // 1. 全局布局间距
    // 底部边距维持 130 (保持之前平台和按钮上移的效果)
    m_mainLayout->setContentsMargins(marginH, cfg.scaled(90), cfg.scaled(50), cfg.scaled(130));
    m_mainLayout->setSpacing(cfg.scaled(18));

    if (m_dynamicContainer->layout())
    {
        m_dynamicContainer->layout()->setSpacing(cfg.scaled(18));
    }
    m_mainLayout->setSpacing(cfg.scaled(10));
    // [Modify] 修改点1: 标题上移 50px
    // 之前是 80，现在减去 50 变为 30
    if (m_titleTopSpacer)
    {
        m_titleTopSpacer->setFixedHeight(cfg.scaled(30));
    }

    // 2. Tag 样式
    QFont fontTag("SimHei", d.fontTag);
    fontTag.setPixelSize(cfg.scaled(d.fontTag));
    fontTag.setWeight(QFont::Black);
    m_lblTag->setFont(fontTag);
    m_lblTag->setStyleSheet(QString("color: %1;").arg(c.textTag.name()));

    // 3. 标题样式
    QFont fontTitle("Microsoft YaHei");
    fontTitle.setPixelSize(cfg.scaled(d.fontTitle));
    fontTitle.setWeight(QFont::Black);
    m_txtTitle->setTextStyle(fontTitle, c.textTitle, 130);

    // 4. 图标样式
    int iconSize = cfg.scaled(d.fontInfoIcon + 8);
    int iconFont = cfg.scaled(d.fontInfoIcon);
    QString iconStyle =
        QString("QLabel { color: %1; font-size: %2px; }").arg(c.textInfoSub.name()).arg(iconFont);

    QList<QLabel*> icons = this->findChildren<QLabel*>();
    for (auto ico : icons)
    {
        if (ico->objectName().contains("Icon"))
        {
            ico->setFixedSize(iconSize, iconSize);
            ico->setStyleSheet(iconStyle);
            ico->setAlignment(Qt::AlignCenter);
        }
    }

    // 5. 详情文本样式
    QFont fontInfo("Microsoft YaHei");
    fontInfo.setPixelSize(cfg.scaled(d.fontInfoText));
    m_lblTime->setFont(fontInfo);
    m_lblLecturer->setFont(fontInfo);
    QString infoStyle = QString("color: %1;").arg(c.textInfoMain.name());
    m_lblTime->setStyleSheet(infoStyle);
    m_lblLecturer->setStyleSheet(infoStyle);

    m_txtDesc->setTextStyle(fontInfo, c.textInfoMain, 140);

    // 6. 平台图标
    int platSize = cfg.scaled(40);
    int platFont = cfg.scaled(22);
    m_platformContainer->setFixedHeight(platSize + 10);
    m_platformContainer->layout()->setSpacing(cfg.scaled(25));
    QList<QLabel*> platIcons = m_platformContainer->findChildren<QLabel*>("PlatIcon");
    for (auto ico : platIcons)
    {
        ico->setFixedSize(platSize, platSize);
        ico->setStyleSheet(QString("background-color: rgba(255,255,255,0.7); border-radius: 10px; "
                                   "color: #FF6600; border: 1px solid #E5E6EB; font-size: %1px;")
                               .arg(platFont));
    }

    // 7. 按钮样式
    QSize btnSize = d.enterBtnSize * cfg.uiScale();
    m_btnEnter->setFixedSize(btnSize);
    m_btnEnter->setStyleSheet(QString(R"(
        QPushButton#btnEnterLive {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %1, stop:1 %2);
            color: %3;
            font-size: %4px;
            font-weight: bold; border-radius: 12px; border: none;
        }
        QPushButton#btnEnterLive:hover {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %5, stop:1 %6);
        }
        QPushButton#btnEnterLive:pressed { background-color: #337ECC; }
        QPushButton#btnEnterLive:disabled { background: #A0CFFF; color: #E6E6E6; }
    )")
                                  .arg(c.btnPrimaryEnd.name())
                                  .arg(c.btnPrimaryStart.name())
                                  .arg(c.btnText.name())
                                  .arg(cfg.scaled(d.fontBtn))
                                  .arg(c.btnHover.name())
                                  .arg(c.btnPrimaryEnd.name()));

    // [Modify] 修改点2: 按钮容器增加底部边距
    // 增加 25px 的底部空间 (第4个参数)，防止按钮的阴影被布局裁切导致显示不全
    if (m_btnContainer->layout())
        m_btnContainer->layout()->setContentsMargins(0, cfg.scaled(30), 0, cfg.scaled(25));
}
