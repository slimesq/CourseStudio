#include "UserProfileWidget.h"
#include "UserMgr.h"
#include "AssetCacheManager.h"
#include <QVBoxLayout>
#include <QPainter>
#include <QGraphicsDropShadowEffect>
#include <QEvent>
#include <QPainterPath>
#include <QDebug>

UserProfileWidget::UserProfileWidget(QWidget* parent) : QWidget(parent)
{
    // [调整 1] 尺寸优化：宽度保持 220，高度减小到 230，比例更协调
    setFixedSize(220, 220);

    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    initUI();

    connect(&AssetCacheManager::instance(),
            &AssetCacheManager::assetReady,
            this,
            &UserProfileWidget::onAssetReady);
}

void UserProfileWidget::updateInfo()
{
    UserInfo info = UserMgr::inst()->currentUser();

    QString displayName = info.nickname;
    if (displayName.isEmpty())
        displayName = info.userName;
    if (displayName.isEmpty())
        displayName = info.account;
    m_nameLabel->setText(displayName);

    QString avatarPath;
    if (!info.avatarPath.isEmpty())
    {
        QString cachedPath = AssetCacheManager::instance().getLocalPath(info.avatarPath);
        avatarPath = cachedPath.isEmpty() ? info.avatarPath : cachedPath;
    }
    else
    {
        avatarPath = info.avatarPath;
    }

    // [调整 2] 头像生成尺寸回调为 50，配合界面缩小
    m_avatar->setPixmap(UserMgr::makeRoundAvatar(avatarPath, 50));

    m_descLabel->setText(info.userType.isEmpty() ? "普通用户" : info.userType);
}

void UserProfileWidget::onAssetReady(QString const& url, QString const& localPath, bool isTemp)
{
    UserInfo info = UserMgr::inst()->currentUser();
    if (info.avatarPath == url)
    {
        info.localAvatarPath = localPath;
        UserMgr::inst()->saveUser(info);
        // 异步下载完成也同步为 size 50
        m_avatar->setPixmap(UserMgr::makeRoundAvatar(localPath, 50));
    }
}

void UserProfileWidget::initUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);  // 阴影预留

    QWidget* bg = new QWidget(this);
    bg->setObjectName("ProfileBg");
    bg->setStyleSheet(
        "QWidget#ProfileBg {"
        "   background-color: #FFFFFF;"
        "   border-radius: 8px;"  // 圆角稍微改小一点点，显得更精致
        "   border: 1px solid #EBEEF5;"
        "}");

    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(18);
    shadow->setColor(QColor(0, 0, 0, 30));  // 阴影再淡一点
    shadow->setOffset(0, 3);
    bg->setGraphicsEffect(shadow);

    QVBoxLayout* layout = new QVBoxLayout(bg);
    // [调整 3] 布局紧凑化：间距从 8 改为 5，Margin 减少
    layout->setSpacing(5);
    layout->setContentsMargins(15, 20, 15, 15);

    m_avatar = new QLabel(bg);
    // [调整 4] 控件尺寸改为 50x50
    m_avatar->setFixedSize(50, 50);
    m_avatar->setAlignment(Qt::AlignCenter);
    m_avatar->setStyleSheet("background-color: transparent; border-radius: 25px;");

    QWidget* avatarContainer = new QWidget(bg);
    QHBoxLayout* avatarLayout = new QHBoxLayout(avatarContainer);
    avatarLayout->setContentsMargins(0, 0, 0, 0);
    avatarLayout->addStretch();
    avatarLayout->addWidget(m_avatar);
    avatarLayout->addStretch();

    m_nameLabel = new QLabel("User", bg);
    m_nameLabel->setAlignment(Qt::AlignCenter);
    // 名字字体微调
    m_nameLabel->setStyleSheet(
        "color: #303133; font-size: 14px; font-weight: bold; margin-top: 2px;");

    m_descLabel = new QLabel("加载中...", bg);
    m_descLabel->setAlignment(Qt::AlignCenter);
    m_descLabel->setStyleSheet("color: #606266; font-size: 12px;");

    m_statsLabel = new QLabel("粉丝 0 | 关注 0", bg);
    m_statsLabel->setAlignment(Qt::AlignCenter);
    m_statsLabel->setStyleSheet("color: #909399; font-size: 11px;");

    m_logoutBtn = new QPushButton("退出登录", bg);
    m_logoutBtn->setCursor(Qt::PointingHandCursor);
    m_logoutBtn->setFixedHeight(30);  // 按钮高度微调回 30
    m_logoutBtn->setStyleSheet(
        "QPushButton {"
        "   background-color: #F4F4F5;"
        "   color: #606266;"
        "   font-size: 12px;"
        "   border-radius: 15px;"
        "   border: 1px solid #DCDFE6;"
        "}"
        "QPushButton:hover {"
        "   background-color: #FFFFFF;"
        "   color: #F56C6C;"
        "   border-color: #F56C6C;"
        "}"
        "QPushButton:pressed {"
        "   background-color: #FEF0F0;"
        "}");

    connect(m_logoutBtn, &QPushButton::clicked, this, [this]() {
        UserMgr::inst()->logout();
        this->close();
        emit logoutRequested();
    });

    layout->addWidget(avatarContainer);
    layout->addWidget(m_nameLabel);
    layout->addWidget(m_descLabel);
    layout->addWidget(m_statsLabel);
    layout->addStretch();
    layout->addWidget(m_logoutBtn);

    mainLayout->addWidget(bg);
}

bool UserProfileWidget::event(QEvent* event)
{
    return QWidget::event(event);
}
void UserProfileWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
}
