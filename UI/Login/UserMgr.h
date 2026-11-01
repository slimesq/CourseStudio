#ifndef USERMGR_H
#define USERMGR_H

#include <QObject>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QColor>
#include <QStandardPaths>
#include <QDateTime>
#include <QSet>

struct UserInfo
{
    // 基础鉴权
    QString account;
    QString password;
    QString token;
    qint64 loginTime = 0;
    bool rememberPwd = false;
    bool autoLogin = false;

    // 个人信息 (API 获取)
    qint64 userId = 0;
    QString userName;
    QString nickname;
    QString userType;
    QString email;
    QString phonenumber;
    QString sex;
    QString avatarPath;       // 服务器头像地址
    QString localAvatarPath;  // 本地缓存路径

    // 权限列表
    QSet<QString> permissions;

    bool isTokenValid() const
    {
        if (token.isEmpty() || loginTime == 0)
            return false;
        qint64 current = QDateTime::currentSecsSinceEpoch();
        return (current - loginTime) < 86400;  // 24小时有效期
    }

    QJsonObject toJson() const
    {
        QJsonObject json;
        json["account"] = account;
        json["password"] = password;
        json["token"] = token;
        json["loginTime"] = loginTime;
        json["rememberPwd"] = rememberPwd;
        json["autoLogin"] = autoLogin;
        json["userId"] = userId;
        json["userName"] = userName;
        json["nickname"] = nickname;
        json["userType"] = userType;
        json["email"] = email;
        json["phonenumber"] = phonenumber;
        json["sex"] = sex;
        json["avatarPath"] = avatarPath;
        json["localAvatarPath"] = localAvatarPath;

        // 序列化权限
        QJsonArray permArray;
        for (auto const& p : permissions)
        {
            permArray.append(p);
        }
        json["permissions"] = permArray;

        return json;
    }

    static UserInfo fromJson(QJsonObject const& json)
    {
        UserInfo info;
        info.account = json["account"].toString();
        info.password = json["password"].toString();
        info.token = json["token"].toString();
        info.loginTime =
            json.contains("loginTime") ? json["loginTime"].toVariant().toLongLong() : 0;
        info.rememberPwd = json["rememberPwd"].toBool();
        info.autoLogin = json["autoLogin"].toBool();
        info.userId = json["userId"].toVariant().toLongLong();
        info.userName = json["userName"].toString();
        info.nickname = json["nickname"].toString();
        info.userType = json["userType"].toString();
        info.email = json["email"].toString();
        info.phonenumber = json["phonenumber"].toString();
        info.sex = json["sex"].toString();
        info.avatarPath = json["avatarPath"].toString();
        info.localAvatarPath = json["localAvatarPath"].toString();

        // 反序列化权限
        if (json.contains("permissions"))
        {
            QJsonArray arr = json["permissions"].toArray();
            for (auto const& val : arr)
            {
                info.permissions.insert(val.toString());
            }
        }
        return info;
    }
};

class UserMgr : public QObject
{
    Q_OBJECT
public:
    static UserMgr* inst();
    void init();

    QString clientId() const
    {
        return m_clientId;
    }

    bool verifyUser(QString const& account, QString const& password);
    void saveUser(UserInfo const& info);
    UserInfo lastUser() const;
    UserInfo currentUser() const
    {
        return m_currentUser;
    }
    void setCurrentUser(UserInfo const& info);
    void logout();
    static QPixmap makeRoundAvatar(QString const& path,
                                   int size,
                                   QColor defaultColor = QColor(100, 150, 255));

    // 核心权限检查接口
    bool hasPermission(QString const& permissionKey) const;

    // [New] 调试：打印所有权限
    void debugDumpPermissions() const;

private:
    UserMgr();
    void loadFromFile();
    void saveToFile();

private:
    UserInfo m_currentUser;
    QList<UserInfo> m_userList;
    QString m_filePath;
    QString m_clientId;
};

#endif  // USERMGR_H
