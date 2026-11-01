#ifndef LOGINWIDGET_H
#define LOGINWIDGET_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QWebEngineView>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEnginePage>
#include <QWebEngineCookieStore>
#include <QNetworkCookie>
#include <QTimer>
#include <QMouseEvent>

// [Change] 引入自定义动画复选框
#include "AnimatedCheckBox.h"

class LoginUrlInterceptor : public QWebEngineUrlRequestInterceptor
{
    Q_OBJECT
public:
    explicit LoginUrlInterceptor(QObject* parent = nullptr);
    void interceptRequest(QWebEngineUrlRequestInfo& info) override;
signals:
    void redirectCaptured(QString const& url);
};

class LoginWidget : public QWidget
{
    Q_OBJECT

public:
    LoginWidget(QWidget* parent = nullptr);
    ~LoginWidget();

    void checkAutoLogin();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

signals:
    void loginCompleted();

private slots:
    void onLoginClicked();
    void onGetCodeClicked();
    void onSwitchLoginMode();
    void onDebounceTimeout();

    // --- WebEngine Slots ---
    void onWebLoadProgress(int progress);
    void onWebLoadFinished(bool ok);
    void onCookieAdded(QNetworkCookie const& cookie);
    void onRedirectCaptured(QString const& url);

    void onLoginSuccess(QString const& token);
    void onLoginFailed(QString const& msg);

    void onProfileSynced(bool success, QString const& msg);
    void onPermissionsSynced(bool success, QString const& msg);

    void refreshQrCode();
    void onCheckQrCodeTimer();
    void onLoadTimeout();

private:
    void initUI();
    void connectSignals();
    void loadUserConfig();
    void updateEditStyle(QLineEdit* edit, bool isValid);
    void shakeWidget(QWidget* widget);

    void initWebView();
    void loadQrCodePage();
    void tryExtractQRCode();
    void handleLoginSuccess(QString const& token, QString const& source);

    // UI Controls
    QPushButton* m_closeBtn;
    QLabel* m_titleLabel;
    QLabel* m_leftHeader;
    QStackedWidget* m_inputStack;
    QWidget* m_phoneModeWidget;
    QLineEdit* m_phoneEdit;
    QLineEdit* m_codeEdit;
    QPushButton* m_getCodeBtn;
    QWidget* m_pwdModeWidget;
    QLineEdit* m_accountEdit;
    QLineEdit* m_passwordEdit;

    // [Change] 使用自定义控件和配套 Label
    AnimatedCheckBox* m_autoLoginCheck;
    QLabel* m_autoLoginLabel;

    AnimatedCheckBox* m_rememberPwdCheck;
    QLabel* m_rememberPwdLabel;

    QLabel* m_switchModeLabel;
    QLabel* m_agreementLabel;
    QPushButton* m_loginBtn;
    QLabel* m_rightHeader;

    QLabel* m_qrCodeImg;
    QLabel* m_qrHintLabel;

    QWebEngineView* m_webView;

    bool m_isQrLoaded;
    bool m_isLoginSuccess;

    QTimer* m_refreshDebounceTimer;
    QTimer* m_qrCheckTimer;
    QTimer* m_loadTimeoutTimer;

    bool m_isPasswordMode = true;
    QString const m_targetUrl =
        "https://open.work.weixin.qq.com/wwopen/sso/"
        "qrConnect?appid=ww9d31ba407779669a&redirect_uri=http%3A%2F%2F192.168.6.15%3A9222%2Flogin&"
        "agentid=1000022&state=wework";

    bool m_isPressed = false;
    QPoint m_dragPosition;
};

#endif  // LOGINWIDGET_H
