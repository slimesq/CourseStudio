#include "CustomTrayMenu.h"
#include <QScreen>
#include <QApplication>
#include <QFrame>

// ==========================================
// CustomMenuItem 实现
// ==========================================
CustomMenuItem::CustomMenuItem(QString const& iconPath, QString const& text, QWidget* parent)
    : QWidget(parent)
{
    // [UI] 行高设置：企业级菜单通常在 40px - 48px 之间
    setFixedHeight(44);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_StyledBackground);  // 关键：允许 QWidget 使用 QSS 背景色

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 0, 16, 0);  // 左右内边距
    layout->setSpacing(12);                    // 图标与文字间距

    // 1. 图标
    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(20, 20);
    m_iconLabel->setScaledContents(true);

    QPixmap pix(iconPath);
    if (!pix.isNull())
    {
        m_iconLabel->setPixmap(pix);
    }
    else
    {
        // 占位样式 (如果图片加载失败)
        m_iconLabel->setStyleSheet("background-color: #E0E0E0; border-radius: 4px;");
    }

    // 2. 文字
    m_textLabel = new QLabel(text, this);
    m_textLabel->setStyleSheet(
        "QLabel {"
        "  color: #333333;"   // 深灰字体
        "  font-size: 14px;"  // 标准字号
        "  font-family: 'Microsoft YaHei';"
        "  font-weight: 500;"
        "}");

    layout->addWidget(m_iconLabel);
    layout->addWidget(m_textLabel);
    layout->addStretch();
}

void CustomMenuItem::setTextColor(QString const& colorCode)
{
    // 重新设置样式表，替换颜色部分
    // 这里保留了字体大小和字体族，只修改 color 属性
    m_textLabel->setStyleSheet(QString("QLabel {"
                                       "  color: %1;"  // 使用传入的颜色
                                       "  font-size: 14px;"
                                       "  font-family: 'Microsoft YaHei';"
                                       "  font-weight: 500;"
                                       "}")
                                   .arg(colorCode));
}

void CustomMenuItem::enterEvent(QEnterEvent* event)
{
    // [UI] Hover 效果：淡蓝色背景 + 圆角
    this->setStyleSheet("CustomMenuItem { background-color: #F2F7FF; border-radius: 6px; }");
    QWidget::enterEvent(event);
}

void CustomMenuItem::leaveEvent(QEvent* event)
{
    // [UI] 恢复透明
    this->setStyleSheet("CustomMenuItem { background-color: transparent; }");
    QWidget::leaveEvent(event);
}

void CustomMenuItem::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        emit clicked();
    }
    QWidget::mouseReleaseEvent(event);
}

// ==========================================
// CustomTrayMenu 实现
// ==========================================
CustomTrayMenu::CustomTrayMenu(QWidget* parent) : QWidget(parent)
{
    // [核心] Popup 属性让窗口像菜单一样点击外部自动关闭
    // Frameless 去掉系统边框
    // NoDropShadow 去掉系统自带的丑阴影 (我们要自己画好看的)
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);  // 背景透明用于画阴影

    // 1. 外层布局：留出阴影空间
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    int shadowSize = 16;
    mainLayout->setContentsMargins(shadowSize, shadowSize, shadowSize, shadowSize);

    // 2. 实体容器 (白卡片)
    m_container = new QWidget(this);
    m_container->setObjectName("MenuBg");
    m_container->setAttribute(Qt::WA_StyledBackground);
    m_container->setFixedWidth(125);
    m_container->setStyleSheet(
        "#MenuBg {"
        "  background-color: #FFFFFF;"  // 纯白背景
        "  border: 1px solid #EBEEF5;"  // 极淡的边框
        "  border-radius: 12px;"        // 大圆角
        "}");

    // 3. 高级阴影
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setOffset(0, 5);                // 向下偏移
    shadow->setColor(QColor(0, 0, 0, 40));  // 黑色 40% 透明度
    shadow->setBlurRadius(20);              // 模糊半径
    m_container->setGraphicsEffect(shadow);

    mainLayout->addWidget(m_container);

    // 4. 列表布局
    m_listLayout = new QVBoxLayout(m_container);
    m_listLayout->setContentsMargins(6, 8, 6, 8);  // 内部留白
    m_listLayout->setSpacing(2);                   // Item 间距
}

void CustomTrayMenu::addMenuItem(CustomMenuItem* item)
{
    m_listLayout->addWidget(item);
    // 点击任意项后关闭菜单
    connect(item, &CustomMenuItem::clicked, this, &CustomTrayMenu::close);
}

void CustomTrayMenu::addSeparator()
{
    QFrame* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);
    // 浅灰分割线
    line->setStyleSheet("background-color: #F0F0F0; margin: 4px 10px;");
    m_listLayout->addWidget(line);
}

void CustomTrayMenu::showAt(QPoint const& globalPos)
{
    // 强制布局计算以获取真实大小
    this->adjustSize();

    int w = this->width();
    int h = this->height();

    // 默认：显示在鼠标上方居中 (适应底部托盘)
    int x = globalPos.x() - w / 2;
    int y = globalPos.y() - h + 10;  // +10 是为了覆盖一点阴影区域，视觉更紧凑

    // [边界检测]
    QScreen* screen = QGuiApplication::screenAt(globalPos);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    QRect screenRect = screen->availableGeometry();

    // 如果上方放不下 (比如任务栏在顶部)，就放下面
    if (y < screenRect.top())
    {
        y = globalPos.y() + 10;
    }

    // 防止右侧溢出
    if (x + w > screenRect.right())
    {
        x = screenRect.right() - w - 10;
    }
    // 防止左侧溢出
    if (x < screenRect.left())
    {
        x = screenRect.left() + 10;
    }

    this->move(x, y);
    this->show();
}

bool CustomTrayMenu::event(QEvent* event)
{
    // 再次确保点击外部关闭 (Qt::Popup 也就是做这个的，这里是双重保险)
    if (event->type() == QEvent::MouseButtonPress)
    {
        if (!rect().contains(static_cast<QMouseEvent*>(event)->pos()))
        {
            close();
            return true;
        }
    }
    return QWidget::event(event);
}
