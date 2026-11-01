#include "LoginWidget.h"
#include "AuthService.h"
#include "skinmgr.h"
#include "UserMgr.h"
#include "UserContext.h"
#include <QFrame>
#include <QTimer>
#include <QGraphicsDropShadowEffect>
#include <QMessageBox>
#include <QWebEngineProfile>
#include <QWebEngineCookieStore>
#include <QUrlQuery>
#include <QGraphicsOpacityEffect>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include "UniversalDialog.h"
#include <QStyle>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>

// 正常样式定义
QString const normalInputStyle = R"(
    QLineEdit {
        border: 1px solid #DCDFE6;
        border-radius: 4px;
        padding-left: 12px;
        font-size: 15px;
        color: #303133;
        background: #FFFFFF;
    }
    QLineEdit:focus { border-color: #409EFF; }
)";

// 错误样式
QString const errorInputStyle = R"(
    QLineEdit {
        border: 1px solid #F56C6C;
        background-color: #FFF0F0;
    }
)";

// --- 拦截器实现 ---
LoginUrlInterceptor::LoginUrlInterceptor(QObject* parent) : QWebEngineUrlRequestInterceptor(parent)
{
}

void LoginUrlInterceptor::interceptRequest(QWebEngineUrlRequestInfo& info)
{
    QString url = info.requestUrl().toString();

    if (url.contains("aegis.qq.com") || url.contains("badjs.weixinbridge.com") ||
        url.contains("sentry"))
    {
        info.block(true);
        return;
    }

    if (url.contains("code="))
    {
        qDebug() << ">>> [拦截器] 捕获扫码回调，准备接管:" << url;
        info.block(true);
        emit redirectCaptured(url);
        return;
    }
}

// --- LoginWidget 实现 ---

LoginWidget::LoginWidget(QWidget* parent)
    : QWidget(parent), m_isQrLoaded(false), m_isLoginSuccess(false)
{
    this->setAttribute(Qt::WA_TranslucentBackground);

    UserMgr::inst()->init();
    this->setFixedSize(760, 580);
    this->setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    SkinMgr::inst()->setSkin(SK_Login, this);

    m_refreshDebounceTimer = new QTimer(this);
    m_refreshDebounceTimer->setSingleShot(true);
    m_refreshDebounceTimer->setInterval(300);
    connect(m_refreshDebounceTimer, &QTimer::timeout, this, &LoginWidget::onDebounceTimeout);

    m_qrCheckTimer = new QTimer(this);
    m_qrCheckTimer->setInterval(500);
    connect(m_qrCheckTimer, &QTimer::timeout, this, &LoginWidget::onCheckQrCodeTimer);

    m_loadTimeoutTimer = new QTimer(this);
    m_loadTimeoutTimer->setSingleShot(true);
    m_loadTimeoutTimer->setInterval(15000);
    connect(m_loadTimeoutTimer, &QTimer::timeout, this, &LoginWidget::onLoadTimeout);

    initWebView();
    initUI();
    connectSignals();
    loadUserConfig();
    QTimer::singleShot(200, this, &LoginWidget::checkAutoLogin);
    loadQrCodePage();
}

LoginWidget::~LoginWidget()
{
    if (m_webView)
    {
        m_webView->stop();
        delete m_webView;
        m_webView = nullptr;
    }
}

void LoginWidget::initUI()
{
    this->setAttribute(Qt::WA_TranslucentBackground);

    QString inputStyle = R"(
        QLineEdit {
            border: 1px solid #DCDFE6;
            border-radius: 4px;
            padding-left: 12px;
            font-size: 16px;
            font-weight: bold;
            color: #303133;
            background: #FFFFFF;
            height: 48px;
            transition: border-color 0.2s;
        }
        QLineEdit:focus { border-color: #409EFF; }
        QLineEdit::placeholder { color: #C0C4CC; font-weight: normal; font-size: 14px; }
        QLineEdit[isError="true"] { border: 1px solid #F56C6C; background-color: #FFF5F5; }
    )";

    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(20, 20, 20, 20);
    rootLayout->setSpacing(0);

    QFrame* mainFrame = new QFrame(this);
    mainFrame->setObjectName("LoginMainFrame");

    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(24);
    shadow->setColor(QColor(0, 0, 0, 50));
    shadow->setOffset(0, 6);
    mainFrame->setGraphicsEffect(shadow);

    rootLayout->addWidget(mainFrame);

    QVBoxLayout* mainLayout = new QVBoxLayout(mainFrame);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- 顶部标题栏 ---
    QWidget* topContainer = new QWidget(mainFrame);
    topContainer->setFixedHeight(80);
    QHBoxLayout* topLayout = new QHBoxLayout(topContainer);
    topLayout->setContentsMargins(24, 0, 15, 0);

    m_titleLabel = new QLabel("Course Studio 2026", mainFrame);
    m_titleLabel->setObjectName("m_titleLabel");

    m_closeBtn = new QPushButton(mainFrame);
    m_closeBtn->setObjectName("m_closeBtn");
    m_closeBtn->setFixedSize(28, 28);
    m_closeBtn->setCursor(Qt::PointingHandCursor);

    topLayout->addStretch(1);
    topLayout->addWidget(m_titleLabel);
    topLayout->addStretch(1);

    QWidget* placeholder = new QWidget(mainFrame);
    placeholder->setFixedSize(28, 28);
    topLayout->insertWidget(0, placeholder);
    topLayout->addWidget(m_closeBtn);

    mainLayout->addWidget(topContainer);

    // --- 中间内容区 ---
    QWidget* centerContainer = new QWidget(mainFrame);
    QHBoxLayout* centerLayout = new QHBoxLayout(centerContainer);
    centerLayout->setContentsMargins(50, 10, 50, 20);
    centerLayout->setSpacing(40);

    // === 左侧表单区域 ===
    QWidget* leftWidget = new QWidget(mainFrame);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(10, 0, 10, 0);
    leftLayout->setSpacing(0);

    m_leftHeader = new QLabel("账号登录", mainFrame);

    m_inputStack = new QStackedWidget(mainFrame);
    m_inputStack->setFixedHeight(140);

    // Mode A: 手机号
    m_phoneModeWidget = new QWidget(mainFrame);
    QVBoxLayout* phoneLayout = new QVBoxLayout(m_phoneModeWidget);
    phoneLayout->setContentsMargins(0, 0, 0, 0);
    phoneLayout->setSpacing(24);

    m_phoneEdit = new QLineEdit(mainFrame);
    m_phoneEdit->setPlaceholderText("请输入手机号");
    m_phoneEdit->setFixedHeight(48);
    m_phoneEdit->setProperty("kind", "phone");
    m_phoneEdit->setStyleSheet(inputStyle);

    QWidget* codeContainer = new QWidget(mainFrame);
    QHBoxLayout* codeLayout = new QHBoxLayout(codeContainer);
    codeLayout->setContentsMargins(0, 0, 0, 0);
    codeLayout->setSpacing(12);

    m_codeEdit = new QLineEdit(mainFrame);
    m_codeEdit->setPlaceholderText("验证码");
    m_codeEdit->setFixedHeight(48);
    m_codeEdit->setProperty("kind", "code");
    m_codeEdit->setStyleSheet(inputStyle);

    m_getCodeBtn = new QPushButton("获取验证码", mainFrame);
    m_getCodeBtn->setObjectName("m_getCodeBtn");
    m_getCodeBtn->setFixedSize(110, 48);
    m_getCodeBtn->setCursor(Qt::PointingHandCursor);

    codeLayout->addWidget(m_codeEdit);
    codeLayout->addWidget(m_getCodeBtn);
    phoneLayout->addWidget(m_phoneEdit);
    phoneLayout->addWidget(codeContainer);

    // Mode B: 账号密码
    m_pwdModeWidget = new QWidget(mainFrame);
    QVBoxLayout* pwdLayout = new QVBoxLayout(m_pwdModeWidget);
    pwdLayout->setContentsMargins(0, 0, 0, 0);
    pwdLayout->setSpacing(24);

    m_accountEdit = new QLineEdit(mainFrame);
    m_accountEdit->setPlaceholderText("账号 / 邮箱");
    m_accountEdit->setFixedHeight(48);
    m_accountEdit->setProperty("kind", "user");
    m_accountEdit->setStyleSheet(inputStyle);

    m_passwordEdit = new QLineEdit(mainFrame);
    m_passwordEdit->setPlaceholderText("密码");
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setFixedHeight(48);
    m_passwordEdit->setProperty("kind", "password");
    m_passwordEdit->setStyleSheet(inputStyle);

    pwdLayout->addWidget(m_accountEdit);
    pwdLayout->addWidget(m_passwordEdit);

    m_inputStack->addWidget(m_phoneModeWidget);
    m_inputStack->addWidget(m_pwdModeWidget);
    m_inputStack->setCurrentIndex(1);

    // --- 辅助选项 ---
    QWidget* optionContainer = new QWidget(mainFrame);
    QHBoxLayout* optionLayout = new QHBoxLayout(optionContainer);
    optionLayout->setContentsMargins(0, 0, 0, 0);
    optionLayout->setSpacing(0);

    // 1. 自动登录组合
    m_autoLoginCheck = new AnimatedCheckBox(mainFrame);
    m_autoLoginLabel = new QLabel("自动登录", mainFrame);
    m_autoLoginLabel->setCursor(Qt::PointingHandCursor);
    m_autoLoginLabel->setStyleSheet("color: #909399; font-size: 13px; margin-left: 5px;");
    m_autoLoginLabel->installEventFilter(this);

    // 2. 记住密码组合
    m_rememberPwdCheck = new AnimatedCheckBox(mainFrame);
    m_rememberPwdLabel = new QLabel("记住密码", mainFrame);
    m_rememberPwdLabel->setCursor(Qt::PointingHandCursor);
    m_rememberPwdLabel->setStyleSheet("color: #909399; font-size: 13px; margin-left: 5px;");
    m_rememberPwdLabel->installEventFilter(this);

    // 状态联动
    connect(m_autoLoginCheck, &AnimatedCheckBox::toggled, this, [this](bool checked) {
        if (checked)
            m_rememberPwdCheck->setChecked(true);
    });
    connect(m_rememberPwdCheck, &AnimatedCheckBox::toggled, this, [this](bool checked) {
        if (!checked)
            m_autoLoginCheck->setChecked(false);
    });

    optionLayout->addWidget(m_autoLoginCheck);
    optionLayout->addWidget(m_autoLoginLabel);

    optionLayout->addSpacing(20);

    optionLayout->addWidget(m_rememberPwdCheck);
    optionLayout->addWidget(m_rememberPwdLabel);

    optionLayout->addStretch();

    m_switchModeLabel = new QLabel("手机验证码登录", mainFrame);
    m_switchModeLabel->setStyleSheet(
        "QLabel { color: #337ECC; font-size: 13px; font-weight: 500; background: transparent; }"
        "QLabel:hover { color: #66B1FF; cursor: pointer; text-decoration: underline; }");
    m_switchModeLabel->setCursor(Qt::PointingHandCursor);
    // [关键] 必须安装过滤器才能响应 Label 点击
    m_switchModeLabel->installEventFilter(this);
    optionLayout->addWidget(m_switchModeLabel);

    m_agreementLabel = new QLabel("登录即代表同意用户协议和用户隐私政策", mainFrame);
    m_agreementLabel->setStyleSheet("color: #C0C4CC; font-size: 12px; margin-top: 5px;");
    m_agreementLabel->setAlignment(Qt::AlignCenter);

    m_loginBtn = new QPushButton("登  录", mainFrame);
    m_loginBtn->setObjectName("m_loginBtn");
    m_loginBtn->setFixedHeight(50);
    m_loginBtn->setCursor(Qt::PointingHandCursor);

    // === 左侧布局组装 ===
    leftLayout->addSpacing(40);
    leftLayout->addWidget(m_leftHeader);
    leftLayout->addSpacing(30);
    leftLayout->addWidget(m_inputStack);
    leftLayout->addSpacing(20);
    leftLayout->addWidget(optionContainer);
    leftLayout->addSpacing(35);
    leftLayout->addWidget(m_loginBtn);
    leftLayout->addSpacing(20);
    leftLayout->addWidget(m_agreementLabel);
    leftLayout->addStretch(1);

    // --- 中间分割线 ---
    QFrame* vLine = new QFrame(mainFrame);
    vLine->setObjectName("vSplitLine");
    vLine->setFixedWidth(1);

    // === 右侧扫码区域 ===
    QWidget* rightWidget = new QWidget(mainFrame);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(20, 0, 0, 0);
    rightLayout->setSpacing(0);

    m_rightHeader = new QLabel("扫码登录", mainFrame);
    m_rightHeader->setAlignment(Qt::AlignCenter);

    m_qrCodeImg = new QLabel(mainFrame);
    m_qrCodeImg->setObjectName("QrCard");
    m_qrCodeImg->setFixedSize(190, 190);
    m_qrCodeImg->setAlignment(Qt::AlignCenter);
    m_qrCodeImg->setText("正在加载...");
    m_qrCodeImg->setCursor(Qt::PointingHandCursor);
    m_qrCodeImg->installEventFilter(this);

    m_qrHintLabel = new QLabel("使用微信扫一扫登录 “易道云课堂”", mainFrame);
    m_qrHintLabel->setObjectName("m_qrHintLabel");
    m_qrHintLabel->setAlignment(Qt::AlignCenter);
    m_qrHintLabel->setWordWrap(true);

    QHBoxLayout* qrCenterLayout = new QHBoxLayout();
    qrCenterLayout->addStretch();
    qrCenterLayout->addWidget(m_qrCodeImg);
    qrCenterLayout->addStretch();

    rightLayout->addSpacing(40);
    rightLayout->addWidget(m_rightHeader);
    rightLayout->addSpacing(30);
    rightLayout->addLayout(qrCenterLayout);
    rightLayout->addSpacing(15);
    rightLayout->addWidget(m_qrHintLabel);
    rightLayout->addStretch(1);

    centerLayout->addWidget(leftWidget, 5);
    centerLayout->addWidget(vLine);
    centerLayout->addWidget(rightWidget, 4);

    mainLayout->addWidget(centerContainer);
}

void LoginWidget::initWebView()
{
    m_webView = new QWebEngineView(this);
    m_webView->setGeometry(-800, 0, 800, 600);
    m_webView->lower();

    QWebEngineProfile* profile = m_webView->page()->profile();
    profile->setHttpUserAgent(
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/114.0.0.0 Safari/537.36");

    connect(profile->cookieStore(),
            &QWebEngineCookieStore::cookieAdded,
            this,
            &LoginWidget::onCookieAdded);

    LoginUrlInterceptor* interceptor = new LoginUrlInterceptor(this);
    connect(interceptor,
            &LoginUrlInterceptor::redirectCaptured,
            this,
            &LoginWidget::onRedirectCaptured);
    profile->setUrlRequestInterceptor(interceptor);

    connect(m_webView, &QWebEngineView::loadProgress, this, &LoginWidget::onWebLoadProgress);
    connect(m_webView, &QWebEngineView::loadFinished, this, &LoginWidget::onWebLoadFinished);

    connect(m_webView,
            &QWebEngineView::renderProcessTerminated,
            this,
            [this](QWebEnginePage::RenderProcessTerminationStatus status, int code) {
                qDebug() << ">>> 渲染进程崩溃! Code:" << code;
                if (!m_isLoginSuccess)
                {
                    QTimer::singleShot(1000, [this]() {
                        if (m_webView)
                            m_webView->reload();
                    });
                }
            });

    m_webView->load(QUrl("about:blank"));
}

void LoginWidget::onRedirectCaptured(QString const& url)
{
    if (m_isLoginSuccess)
        return;

    qDebug() << ">>> [V18] 主线程捕获跳转，调用 AuthService 登录";

    if (m_webView)
    {
        m_webView->stop();
        m_webView->load(QUrl("about:blank"));
    }

    QUrl capturedUrl(url);
    QUrlQuery query(capturedUrl);
    QString code = query.queryItemValue("code");

    if (code.isEmpty())
    {
        qWarning() << ">>> 未找到 Code，登录失败";
        return;
    }

    QString dynamicHost = capturedUrl.host();
    AuthService::instance()->loginByQrCode(code, dynamicHost);
}

void LoginWidget::onLoginSuccess(QString const& token)
{
    handleLoginSuccess(token, "API_Direct_Via_AuthService");
}

void LoginWidget::onLoginFailed(QString const& msg)
{
    if (m_isLoginSuccess)
        return;
    qDebug() << ">>> 登录请求失败:" << msg;

    UniversalDialog::showError(this, "登录失败", msg);

    m_loginBtn->setText("登  录");
    m_loginBtn->setEnabled(true);

    refreshQrCode();
}

void LoginWidget::onProfileSynced(bool success, QString const& msg)
{
    if (!m_isLoginSuccess)
        return;

    if (success)
    {
        qDebug() << ">>> 用户信息同步完成，开始获取权限信息...";
        m_qrCodeImg->setText("正在同步权限...");
        AuthService::instance()->fetchUserInfo();
    }
    else
    {
        qWarning() << ">>> 用户信息同步失败:" << msg;
        emit loginCompleted();
        this->close();
    }
}

void LoginWidget::onPermissionsSynced(bool success, QString const& msg)
{
    if (!m_isLoginSuccess)
        return;
    if (success)
    {
        qDebug() << ">>> 权限信息同步完成，登录流程结束";
    }
    else
    {
        qWarning() << ">>> 权限信息同步失败 (不影响登录):" << msg;
    }
    emit loginCompleted();
    this->close();
}

void LoginWidget::loadQrCodePage()
{
    if (m_isLoginSuccess)
        return;

    m_isQrLoaded = false;
    m_qrCodeImg->setText("正在刷新...");
    m_qrCodeImg->setProperty("state", "loading");
    m_qrCodeImg->style()->polish(m_qrCodeImg);

    if (m_webView)
    {
        m_webView->stop();
        disconnect(m_webView, &QWebEngineView::loadFinished, this, nullptr);

        connect(m_webView, &QWebEngineView::loadFinished, this, [this](bool ok) {
            if (m_webView->url().toString() == "about:blank")
            {
                disconnect(m_webView, &QWebEngineView::loadFinished, this, nullptr);
                connect(m_webView,
                        &QWebEngineView::loadFinished,
                        this,
                        &LoginWidget::onWebLoadFinished);

                QString timestamp = QString::number(QDateTime::currentMSecsSinceEpoch());
                m_webView->load(QUrl(m_targetUrl + "&_t=" + timestamp));
                m_loadTimeoutTimer->start(15000);
            }
        });

        m_webView->load(QUrl("about:blank"));
    }
}

void LoginWidget::refreshQrCode()
{
    if (m_qrCodeImg->property("state").toString() == "loading")
    {
        shakeWidget(m_qrCodeImg);
        return;
    }
    m_refreshDebounceTimer->stop();
    m_refreshDebounceTimer->start();
}

void LoginWidget::onCheckQrCodeTimer()
{
    if (m_isQrLoaded || m_isLoginSuccess)
    {
        m_qrCheckTimer->stop();
        return;
    }
    tryExtractQRCode();
}

void LoginWidget::onLoadTimeout()
{
    qDebug() << ">>> 加载超时，强制解锁状态";
    m_qrCodeImg->setText("加载失败\n请点击重试");
    m_qrCodeImg->setProperty("state", "error");
    m_qrCodeImg->style()->polish(m_qrCodeImg);
    m_isQrLoaded = false;
}

void LoginWidget::onWebLoadProgress(int progress)
{
    if (!m_isQrLoaded && progress > 10)
    {
        if (m_webView && m_webView->url().toString() != "about:blank")
        {
            tryExtractQRCode();
        }
    }
}

void LoginWidget::onWebLoadFinished(bool ok)
{
    if (!ok)
        return;
    if (m_webView && m_webView->url().toString() == "about:blank")
    {
        return;
    }
    if (!m_isQrLoaded && !m_isLoginSuccess)
    {
        m_qrCheckTimer->start();
    }
}

void LoginWidget::tryExtractQRCode()
{
    if (m_isLoginSuccess)
        return;
    if (m_webView && m_webView->url().toString() == "about:blank")
        return;

    QString jsCode = R"JSCODE(
        (function() {
            var img = document.querySelector('.wrp_code img');
            if (!img) img = document.querySelector('img.qrcode');
            if (!img) img = document.querySelector('.qrcode img');
            if (!img) return "WAIT";
            if (img.complete && img.naturalWidth === 0) return "ERROR:BrokenImage";
            if (img.complete && img.naturalWidth > 0) {
                try {
                    var canvas = document.createElement('canvas');
                    canvas.width = img.naturalWidth;
                    canvas.height = img.naturalHeight;
                    var ctx = canvas.getContext('2d');
                    ctx.drawImage(img, 0, 0);
                    return "OK:" + canvas.toDataURL('image/png');
                } catch(e) { return "SRC:" + img.src; }
            }
            return "WAIT";
        })();
    )JSCODE";

    m_webView->page()->runJavaScript(jsCode, [this](QVariant const& v) {
        if (m_isLoginSuccess)
            return;
        QString res = v.toString();

        if (res.startsWith("ERROR:"))
        {
            qWarning() << ">>> [WebEngine] 检测到二维码加载失败 (Broken Image)，立即刷新...";
            QTimer::singleShot(0, this, &LoginWidget::loadQrCodePage);
            return;
        }

        if (res.startsWith("OK:"))
        {
            m_qrCheckTimer->stop();
            m_loadTimeoutTimer->stop();

            QString base64Data = res.mid(3).section(',', 1);
            QPixmap pix;
            if (pix.loadFromData(QByteArray::fromBase64(base64Data.toLatin1())))
            {
                m_isQrLoaded = true;
                m_qrCodeImg->setPixmap(
                    pix.scaled(m_qrCodeImg->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                m_qrCodeImg->setText("");
                m_qrCodeImg->setProperty("state", "");
                m_qrCodeImg->style()->polish(m_qrCodeImg);
            }
        }
    });
}

void LoginWidget::onCookieAdded(QNetworkCookie const& cookie)
{
    if (m_isLoginSuccess)
        return;
    QString name = cookie.name();
    QString value = cookie.value();
    if (value.startsWith("eyJ") && value.length() > 50)
    {
        handleLoginSuccess(value, "Cookie: " + name);
    }
}

void LoginWidget::handleLoginSuccess(QString const& token, QString const& source)
{
    if (m_isLoginSuccess)
        return;
    if (!token.startsWith("eyJ") && !token.startsWith("Bearer"))
        return;

    m_isLoginSuccess = true;
    if (m_webView)
        m_webView->stop();

    bool isRemember = m_rememberPwdCheck->isChecked();
    bool isAuto = m_autoLoginCheck->isChecked();
    UserInfo info = UserMgr::inst()->currentUser();
    if (m_isPasswordMode)
    {
        info.account = m_accountEdit->text().trimmed();
        if (isRemember)
        {
            info.password = m_passwordEdit->text();
        }
        else
        {
            info.password = "";
        }
    }

    qDebug() << ">>> 登录成功! 来源:" << source;
    QString finalToken = token.startsWith("Bearer ") ? token : ("Bearer " + token);
    info.token = finalToken;
    info.rememberPwd = isRemember;
    info.autoLogin = isAuto;
    info.loginTime = QDateTime::currentSecsSinceEpoch();

    UserMgr::inst()->saveUser(info);
    UserContext::instance().setToken(finalToken);

    m_qrCodeImg->clear();
    m_qrCodeImg->setText("登录成功\n正在获取用户信息...");
    m_qrCodeImg->setProperty("state", "success");
    m_qrCodeImg->style()->polish(m_qrCodeImg);

    AuthService::instance()->getUserProfile();
}

void LoginWidget::connectSignals()
{
    connect(m_closeBtn, &QPushButton::clicked, this, &LoginWidget::close);
    connect(m_loginBtn, &QPushButton::clicked, this, &LoginWidget::onLoginClicked);
    connect(m_getCodeBtn, &QPushButton::clicked, this, &LoginWidget::onGetCodeClicked);

    connect(
        AuthService::instance(), &AuthService::profileSynced, this, &LoginWidget::onProfileSynced);
    connect(AuthService::instance(),
            &AuthService::permissionsSynced,
            this,
            &LoginWidget::onPermissionsSynced);
    connect(
        AuthService::instance(), &AuthService::loginSuccess, this, &LoginWidget::onLoginSuccess);
    connect(AuthService::instance(), &AuthService::loginFailed, this, &LoginWidget::onLoginFailed);

    connect(m_accountEdit, &QLineEdit::textChanged, this, [this](QString const& text) {
        // [修改] 允许以数字或字母开头，允许纯数字，长度放宽到 1-30 位
        // 这样 2111300002 (10位纯数字) 就可以通过校验了
        QRegularExpression re("^[a-zA-Z0-9_]{1,30}$");
        bool isValid = re.match(text).hasMatch() || text.isEmpty();
        updateEditStyle(m_accountEdit, isValid);
    });

    connect(m_phoneEdit, &QLineEdit::textChanged, this, [this](QString const& text) {
        QRegularExpression re("^1[3-9]\\d{9}$");
        bool isValid = re.match(text).hasMatch() || text.isEmpty();
        updateEditStyle(m_phoneEdit, isValid);
    });
}

// [修复重点]
bool LoginWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonRelease)
    {
        // 1. 处理自动登录 Label 点击
        if (watched == m_autoLoginLabel)
        {
            m_autoLoginCheck->setChecked(!m_autoLoginCheck->isChecked());
            return true;
        }
        // 2. 处理记住密码 Label 点击
        if (watched == m_rememberPwdLabel)
        {
            m_rememberPwdCheck->setChecked(!m_rememberPwdCheck->isChecked());
            return true;
        }
        // 3. [修复] 处理切换登录模式 Label 点击
        if (watched == m_switchModeLabel)
        {
            onSwitchLoginMode();
            return true;
        }
        // 4. 处理二维码刷新
        if (watched == m_qrCodeImg)
        {
            QString state = m_qrCodeImg->property("state").toString();
            if (state == "loading")
                return true;
            refreshQrCode();
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void LoginWidget::resizeEvent(QResizeEvent* event)
{
    QFrame* frame = this->findChild<QFrame*>("LoginMainFrame");
    if (frame && m_closeBtn)
    {
        m_closeBtn->move(frame->width() - 40, 10);
    }
    QWidget::resizeEvent(event);
}

void LoginWidget::onSwitchLoginMode()
{
    m_isPasswordMode = !m_isPasswordMode;
    if (m_isPasswordMode)
    {
        m_inputStack->setCurrentIndex(1);
        m_leftHeader->setText("账号登录");
        m_switchModeLabel->setText("手机验证码登录");

        m_rememberPwdCheck->show();
        m_rememberPwdLabel->show();
    }
    else
    {
        m_inputStack->setCurrentIndex(0);
        m_leftHeader->setText("手机登录");
        m_switchModeLabel->setText("账号密码登录");

        m_rememberPwdCheck->hide();
        m_rememberPwdLabel->hide();
    }
}

void LoginWidget::onDebounceTimeout()
{
    this->loadQrCodePage();
}

void LoginWidget::loadUserConfig()
{
    UserInfo last = UserMgr::inst()->lastUser();
    if (last.account.isEmpty())
        return;

    m_accountEdit->setText(last.account);

    if (last.rememberPwd)
    {
        m_passwordEdit->setText(last.password);
        m_rememberPwdCheck->setChecked(true, false);
    }
    else
    {
        m_rememberPwdCheck->setChecked(false, false);
    }

    m_autoLoginCheck->setChecked(last.autoLogin, false);
}

void LoginWidget::updateEditStyle(QLineEdit* edit, bool isValid)
{
    if (isValid)
    {
        edit->setProperty("isError", false);
        edit->setToolTip("");
    }
    else
    {
        edit->setProperty("isError", true);
        edit->setToolTip("格式输入不正确");
    }
    edit->style()->unpolish(edit);
    edit->style()->polish(edit);
}

void LoginWidget::shakeWidget(QWidget* widget)
{
    if (!widget)
        return;
    QPoint oldPos = widget->pos();
    QSequentialAnimationGroup* group = new QSequentialAnimationGroup(widget);
    int range = 5;
    int duration = 40;

    auto addShakeStep = [&](int dx) {
        QPropertyAnimation* ani = new QPropertyAnimation(widget, "pos");
        ani->setDuration(duration);
        ani->setStartValue(widget->pos());
        ani->setEndValue(oldPos + QPoint(dx, 0));
        ani->setEasingCurve(QEasingCurve::InOutQuad);
        group->addAnimation(ani);
    };

    addShakeStep(-range);
    addShakeStep(range);
    addShakeStep(-range);
    addShakeStep(0);

    connect(group, &QSequentialAnimationGroup::finished, group, &QObject::deleteLater);
    group->start();
}

void LoginWidget::checkAutoLogin()
{
    UserInfo user = UserMgr::inst()->lastUser();
    if (user.autoLogin)
    {
        if (user.isTokenValid())
        {
            qDebug() << ">>> 自动登录: Token 有效";
            QString finalToken =
                user.token.startsWith("Bearer ") ? user.token : ("Bearer " + user.token);
            UserContext::instance().setToken(finalToken);
            UserMgr::inst()->setCurrentUser(user);
            emit loginCompleted();
            this->close();
        }
        else
        {
            qDebug() << ">>> 自动登录: Token 已过期，请重新登录";
            UserMgr::inst()->logout();
        }
    }
}

void LoginWidget::onLoginClicked()
{
    if (m_isPasswordMode)
    {
        bool accountError = m_accountEdit->property("isError").toBool() ||
            m_accountEdit->text().trimmed().isEmpty();
        bool passwordError = m_passwordEdit->text().isEmpty();

        if (accountError || passwordError)
        {
            if (accountError)
                shakeWidget(m_accountEdit);
            if (passwordError)
                shakeWidget(m_passwordEdit);

            m_loginBtn->setEnabled(true);
            m_loginBtn->setText("登  录");
            return;
        }

        QString account = m_accountEdit->text().trimmed();
        QString password = m_passwordEdit->text();
        AuthService::instance()->loginByPassword(
            account, password, "000000", UserMgr::inst()->clientId(), "password");
    }
    else
    {
        bool phoneError =
            m_phoneEdit->property("isError").toBool() || m_phoneEdit->text().trimmed().isEmpty();
        bool codeError = m_codeEdit->text().isEmpty();

        if (phoneError || codeError)
        {
            if (phoneError)
                shakeWidget(m_phoneEdit);
            if (codeError)
                shakeWidget(m_codeEdit);
            m_loginBtn->setEnabled(true);
            m_loginBtn->setText("登  录");
            return;
        }

        if (m_codeEdit->text() == "1234")
        {
            handleLoginSuccess("Bearer mock_token", "Phone_Mock");
        }
        else
        {
            onLoginFailed("验证码错误");
        }
    }
}

void LoginWidget::onGetCodeClicked()
{
    m_codeEdit->setText("1234");
}

void LoginWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && event->pos().y() < 80)
    {
        m_isPressed = true;
        m_dragPosition = event->globalPosition().toPoint() - this->frameGeometry().topLeft();
        event->accept();
    }
    QWidget::mousePressEvent(event);
}

void LoginWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isPressed && (event->buttons() & Qt::LeftButton))
    {
        this->move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    }
    QWidget::mouseMoveEvent(event);
}

void LoginWidget::mouseReleaseEvent(QMouseEvent* event)
{
    m_isPressed = false;
    QWidget::mouseReleaseEvent(event);
}
