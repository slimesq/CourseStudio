#include "UniversalDialog.h"
#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include <QApplication>
#include <QFile>
#include <QDebug>

UniversalDialog::UniversalDialog(QWidget* parent) : QDialog(parent)
{
    // 1. 窗口标志：无边框 | 对话框 | 顶层保持
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog | Qt::WindowStaysOnTopHint);

    // 2. [关键] 开启透明背景，这是阴影显示的前提
    setAttribute(Qt::WA_TranslucentBackground);

    // 3. 强制根窗口透明，防止出现灰色方块
    this->setStyleSheet("UniversalDialog { background: transparent; border: none; }");

    setMinimumWidth(360);
    setMinimumHeight(190);

    this->setObjectName("UniversalDialog");

    initUI();

    // 4. 加载专用样式表 (加载外部 CSS 覆盖默认样式)
    QFile file(":/UI/QSS/skin/dialog.css");
    if (file.open(QFile::ReadOnly))
    {
        QString style = file.readAll();
        // 将样式应用到当前窗口，这样子控件 #DialogMainFrame 就能读取到 CSS 中的定义
        this->setStyleSheet(this->styleSheet() + style);
        file.close();
    }
    else
    {
        // [调试信息] 如果这里打印了，说明 CSS 路径不对，请检查资源文件 qrc 路径
        qWarning() << "UniversalDialog: Failed to load dialog.css. Using fallback styles.";
    }
}

void UniversalDialog::initUI()
{
    // 1. 根布局
    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    // 这里的 Margin 决定了阴影的大小，留白太小阴影会被切断
    rootLayout->setContentsMargins(15, 15, 15, 15);
    rootLayout->setSpacing(0);

    // 2. 主视觉容器 (白底圆角卡片)
    m_mainFrame = new QFrame(this);
    m_mainFrame->setObjectName("DialogMainFrame");

    // [核心修复 1] 强制开启 StyledBackground，确保 QSS 背景色生效
    m_mainFrame->setAttribute(Qt::WA_StyledBackground, true);

    // [核心修复 2] 设置“兜底”样式。
    // 即使 dialog.css 加载失败，这里也能保证窗口是白色的，而不是透明的。
    // 当 dialog.css 加载成功后，CSS 里的属性会覆盖这里。
    m_mainFrame->setStyleSheet(
        "QFrame#DialogMainFrame {"
        "   background-color: #FFFFFF;" /* 强制纯白背景 */
        "   border-radius: 8px;"
        "   border: 1px solid #E6E6E6;"
        "}");

    // [企业级阴影]
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(24);              // 模糊半径大一点更柔和
    shadow->setColor(QColor(0, 0, 0, 50));  // 阴影颜色
    shadow->setOffset(0, 6);                // 稍微向下偏移
    m_mainFrame->setGraphicsEffect(shadow);

    rootLayout->addWidget(m_mainFrame);

    // 3. 内部布局
    QVBoxLayout* mainLayout = new QVBoxLayout(m_mainFrame);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- 顶部标题栏 ---
    QWidget* titleBar = new QWidget(m_mainFrame);
    titleBar->setObjectName("DialogTitleBar");
    titleBar->setFixedHeight(48);  // 高度 48px 标准触控/桌面高度

    QHBoxLayout* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(20, 0, 15, 0);

    m_titleLabel = new QLabel("提示", titleBar);
    m_titleLabel->setObjectName("DialogTitleLabel");
    // 兜底字体设置
    m_titleLabel->setStyleSheet(
        "font-size: 15px; font-weight: bold; color: #333; font-family: 'Microsoft YaHei UI';");

    QPushButton* closeBtn = new QPushButton(titleBar);
    closeBtn->setObjectName("DialogCloseBtn");
    closeBtn->setFixedSize(30, 30);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setFocusPolicy(Qt::NoFocus);

    // [修复图标] 如果 CSS 挂了，给一个简单的 X 字符作为保底，防止按钮不可见
    // CSS 加载成功后，image 属性会覆盖这个 text
    // closeBtn->setText("×");

    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    titleLayout->addWidget(m_titleLabel);
    titleLayout->addStretch();
    titleLayout->addWidget(closeBtn);

    mainLayout->addWidget(titleBar);

    // --- 内容区域 ---
    m_contentContainer = new QWidget(m_mainFrame);
    m_contentContainer->setObjectName("DialogContent");
    m_contentLayout = new QVBoxLayout(m_contentContainer);
    m_contentLayout->setContentsMargins(30, 20, 30, 20);

    mainLayout->addWidget(m_contentContainer, 1);

    // --- 底部按钮栏 ---
    QWidget* btnBar = new QWidget(m_mainFrame);
    btnBar->setObjectName("DialogButtonBar");
    m_btnLayout = new QHBoxLayout(btnBar);
    m_btnLayout->setContentsMargins(20, 10, 20, 20);
    m_btnLayout->setSpacing(12);
    m_btnLayout->addStretch();

    mainLayout->addWidget(btnBar);
}

// ... (其余函数 setTitle, setMessage, setContentWidget 保持不变)

void UniversalDialog::setTitle(QString const& title)
{
    m_titleLabel->setText(title);
}

void UniversalDialog::setMessage(QString const& message)
{
    clearContent();
    QLabel* msgLabel = new QLabel(message, m_contentContainer);
    msgLabel->setObjectName("DialogMessageLabel");
    msgLabel->setWordWrap(true);
    // 兜底文字样式
    msgLabel->setStyleSheet("font-size: 14px; color: #555;");
    msgLabel->setAlignment(Qt::AlignCenter);
    m_contentLayout->addWidget(msgLabel);
}

void UniversalDialog::setContentWidget(QWidget* widget)
{
    if (!widget)
        return;
    clearContent();
    m_contentLayout->addWidget(widget);
}

void UniversalDialog::addButton(QString const& text,
                                QString const& type,
                                std::function<bool()> callback)
{
    QPushButton* btn = new QPushButton(text, m_mainFrame);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setProperty("type", type);

    // [修复] 默认给一个最小宽度和内边距，防止没有 CSS 时按钮缩成一团
    btn->setMinimumWidth(80);
    btn->setMinimumHeight(32);

    // 动态计算宽度
    QFontMetrics fm(btn->font());
    int textW = fm.horizontalAdvance(text);
    btn->setMinimumWidth(qMax(80, textW + 30));

    if (type.contains("background"))
    {
        btn->setStyleSheet(type);
    }
    // [兜底样式] 如果是 primary 类型且没有加载到 CSS，手动上色
    else if (type == "primary")
    {
        // 如果外部 CSS 没生效，这行代码保证按钮是蓝色的
        // 注意：这不会覆盖外部 CSS，因为外部 CSS 优先级通常更高或后续加载
        if (btn->styleSheet().isEmpty())
        {
            // 仅作演示，最好还是依靠 dialog.css
            // btn->setStyleSheet("background-color: #006EFF; color: white; border-radius: 4px;");
        }
    }

    connect(btn, &QPushButton::clicked, this, [this, callback]() {
        if (callback)
        {
            if (callback())
                this->accept();
        }
        else
        {
            this->accept();
        }
    });

    m_btnLayout->addWidget(btn);
}

void UniversalDialog::clearContent()
{
    QLayoutItem* item;
    while ((item = m_contentLayout->takeAt(0)) != nullptr)
    {
        if (item->widget())
            delete item->widget();
        delete item;
    }
}

// ... (静态方法 showInfo, showError, showConfirm 保持不变)
void UniversalDialog::showInfo(QWidget* parent, QString const& title, QString const& msg)
{
    UniversalDialog dlg(parent);
    dlg.setTitle(title);
    QWidget* content = new QWidget;
    QHBoxLayout* layout = new QHBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(20);
    QLabel* icon = new QLabel(content);
    icon->setFixedSize(40, 40);
    icon->setProperty("type", "info");
    QLabel* text = new QLabel(msg, content);
    text->setObjectName("DialogMessageLabel");
    text->setWordWrap(true);
    text->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(icon, 0, Qt::AlignTop);
    layout->addWidget(text, 1);
    dlg.setContentWidget(content);
    dlg.addButton("确定", "primary");
    dlg.exec();
}
void UniversalDialog::showError(QWidget* parent, QString const& title, QString const& msg)
{
    UniversalDialog dlg(parent);
    dlg.setTitle(title);
    QWidget* content = new QWidget;
    QHBoxLayout* layout = new QHBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(20);
    QLabel* icon = new QLabel(content);
    icon->setFixedSize(40, 40);
    icon->setProperty("type", "error");
    QLabel* text = new QLabel(msg, content);
    text->setObjectName("DialogMessageLabel");
    text->setWordWrap(true);
    text->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(icon, 0, Qt::AlignTop);
    layout->addWidget(text, 1);
    dlg.setContentWidget(content);
    dlg.addButton("关闭", "danger");
    dlg.exec();
}
bool UniversalDialog::showConfirm(QWidget* parent, QString const& title, QString const& msg)
{
    UniversalDialog dlg(parent);
    dlg.setTitle(title);
    QWidget* content = new QWidget;
    QHBoxLayout* layout = new QHBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(20);
    QLabel* icon = new QLabel(content);
    icon->setFixedSize(40, 40);
    icon->setProperty("type", "question");
    QLabel* text = new QLabel(msg, content);
    text->setObjectName("DialogMessageLabel");
    text->setWordWrap(true);
    text->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(icon, 0, Qt::AlignTop);
    layout->addWidget(text, 1);
    dlg.setContentWidget(content);
    bool result = false;
    dlg.addButton("取消", "secondary", [&result]() {
        result = false;
        return true;
    });
    dlg.addButton("确定", "primary", [&result]() {
        result = true;
        return true;
    });
    dlg.exec();
    return result;
}

void UniversalDialog::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        if (event->pos().y() < 50)
        {
            m_isPressed = true;
            m_dragPosition = event->globalPos() - frameGeometry().topLeft();
            event->accept();
        }
    }
}
void UniversalDialog::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isPressed && (event->buttons() & Qt::LeftButton))
    {
        move(event->globalPos() - m_dragPosition);
        event->accept();
    }
}
void UniversalDialog::mouseReleaseEvent(QMouseEvent* event)
{
    m_isPressed = false;
}
void UniversalDialog::paintEvent(QPaintEvent*)
{
}
