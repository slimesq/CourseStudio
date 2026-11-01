#ifndef AUTHSERVICE_H
#define AUTHSERVICE_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>

class AuthService : public QObject
{
    Q_OBJECT
public:
    static AuthService* instance();

    // 业务接口
    void loginByPhone(QString const& phone, QString const& code);
    void sendVerifyCode(QString const& phone);

    // 扫码登录接口
    void loginByQrCode(QString const& code, QString const& dynamicHost = "");

    // 获取个人基础信息 (Profile)
    void getUserProfile();

    // [New] 获取详细用户信息 (含权限) - 对应 /system/user/getInfo
    void fetchUserInfo();

    // 获取用户列表
    void getUserList(int page = 1, int size = 100, QString const& nickName = "");

    // 添加账号密码登录接口
    void loginByPassword(QString const& username,
                         QString const& password,
                         QString const& tenantId,
                         QString const& clientId,
                         QString const& grantType);
signals:
    void loginSuccess(QString const& token);
    void loginFailed(QString const& message);
    void codeSent(bool success, QString const& message);

    // 个人信息同步完成
    void profileSynced(bool success, QString const& msg);

    // [New] 权限信息同步完成
    void permissionsSynced(bool success, QString const& msg);

    // 用户列表获取完成
    void userListFetched(bool success, QJsonArray const& users, int total, QString const& msg);

    void userSearchResult(QJsonArray const& users);

public slots:
    void onApiResponse(QString const& url,
                       bool success,
                       QJsonObject const& response,
                       QString const& errorMsg);

private:
    explicit AuthService(QObject* parent = nullptr);
    static AuthService* m_instance;

    void onHttpResponse(bool success, QJsonObject const& data, QString const& errorMsg);
};

#endif  // AUTHSERVICE_H
