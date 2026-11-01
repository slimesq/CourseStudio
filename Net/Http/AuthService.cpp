#include "AuthService.h"
#include "HttpManager.h"
#include "ApiConfig.h"
#include "UserMgr.h"
#include "UserContext.h"
#include "AssetCacheManager.h"
#include <QDebug>
#include <QJsonArray>
#include <QDateTime>
#include <QJsonDocument>

AuthService* AuthService::m_instance = nullptr;

AuthService* AuthService::instance()
{
    if (!m_instance)
    {
        m_instance = new AuthService();
    }
    return m_instance;
}

AuthService::AuthService(QObject* parent) : QObject(parent)
{
    connect(HttpManager::instance(), &HttpManager::apiResponse, this, &AuthService::onApiResponse);
}

void AuthService::loginByPhone(QString const& phone, QString const& code)
{
    QJsonObject json;
    json["phone"] = phone;
    json["code"] = code;
    json["type"] = "login";
    // [优化建议] 这里最好也改为 ApiConfig::instance().baseUrl() + "/login"
    HttpManager::instance()->post("http://api.coursestudio.com/login", json);
}

void AuthService::sendVerifyCode(QString const& phone)
{
    QJsonObject json;
    json["phone"] = phone;
    json["type"] = "send_code";
    HttpManager::instance()->post("http://api.coursestudio.com/code", json);
}

void AuthService::loginByQrCode(QString const& code, QString const& dynamicHost)
{
    QString path = "/auth/login";
    QString fullUrl = ApiConfig::instance().getUrl("AUTH_SIMPLE_LOGIN");

    // 参考逻辑：如果有动态 Host（来自扫码回调），则覆盖 IP
    if (!dynamicHost.isEmpty())
    {
        fullUrl = QString("http://%1:8080%2").arg(dynamicHost).arg(path);
    }

    qDebug() << "[AuthService] QR Login URL:" << fullUrl;

    QJsonObject json;
    json["tenantId"] = "000000";
    json["clientId"] = UserMgr::inst()->clientId();
    json["grantType"] = "wework";
    json["source"] = "wework";
    json["socialState"] = "abcde";
    json["socialCode"] = code;

    HttpManager::instance()->post(fullUrl, json);
}

void AuthService::getUserProfile()
{
    QString url = ApiConfig::instance().getUrl("USER_PROFILE");
    HttpManager::instance()->get(url);
}

void AuthService::fetchUserInfo()
{
    QString url = ApiConfig::instance().getUrl("USER_INFO");
    HttpManager::instance()->get(url);
}

void AuthService::getUserList(int page, int size, QString const& nickName)
{
    QString url = ApiConfig::instance().getUrl("USER_LIST");
    QVariantMap params;
    params["pageNum"] = page;
    params["pageSize"] = size;
    if (!nickName.isEmpty())
    {
        params["nickName"] = nickName;
    }
    HttpManager::instance()->get(url, params);
}

// [核心修复] 参考 loginByQrCode 的动态化思路，去除硬编码 IP
void AuthService::loginByPassword(QString const& username,
                                  QString const& password,
                                  QString const& tenantId,
                                  QString const& clientId,
                                  QString const& grantType)
{
    // 1. 动态构建请求地址
    // 方案：从 ApiConfig 获取 BaseURL (例如 http://192.168.6.15:9222)
    // 这样只要配置文件变了，这里就会自动变，不需要 dynamicHost 参数
    QString baseUrl = ApiConfig::instance().baseUrl();

    // 处理末尾斜杠，防止拼接出 "//"
    if (baseUrl.endsWith("/"))
    {
        baseUrl.chop(1);
    }

    // 拼接登录路径 /auth/login
    QString apiUrl = QString("%1/auth/login").arg(baseUrl);

    // 2. 构建 JSON 数据
    QJsonObject json;
    json["tenantId"] = tenantId;
    json["clientId"] = clientId;
    json["grantType"] = grantType;
    json["username"] = username;
    json["password"] = password;

    // [Debug] 详细打印，用于确认最终生成的 URL 是否正确
    qDebug().noquote() << "================ LOGIN REQUEST DEBUG ================";
    qDebug().noquote() << "URL    : " << apiUrl;
    qDebug().noquote() << "Headers: Content-Type: application/json";
    qDebug().noquote() << "Payload: " << QJsonDocument(json).toJson(QJsonDocument::Indented);
    qDebug().noquote() << "=====================================================";

    // 3. 发送请求
    HttpManager::instance()->post(apiUrl, json);
}

void AuthService::onApiResponse(QString const& url,
                                bool success,
                                QJsonObject const& response,
                                QString const& errorMsg)
{
    // 1. 处理个人信息 (Profile)
    if (url.contains("/system/user/profile"))
    {
        if (!success)
        {
            emit profileSynced(false, "Failed to fetch profile: " + errorMsg);
            return;
        }

        if (response.contains("data") && response["data"].isObject())
        {
            QJsonObject data = response["data"].toObject();
            QJsonObject userObj = data["user"].toObject();

            UserInfo info = UserMgr::inst()->currentUser();
            info.userId = userObj["userId"].toVariant().toLongLong();
            info.userName = userObj["userName"].toString();
            info.nickname = userObj["nickName"].toString();
            info.userType = userObj["userType"].toString();
            info.email = userObj["email"].toString();
            info.phonenumber = userObj["phonenumber"].toString();
            info.sex = userObj["sex"].toString();

            QVariant avatarVar = QJsonValue(userObj["avatar"]);
            QString avatarStr = avatarVar.toString();

            if ((avatarVar.type() == QVariant::Double && avatarVar.toLongLong() != 0) ||
                (!avatarStr.isEmpty() && avatarStr != "0"))
            {
                if (!avatarStr.startsWith("http"))
                {
                    info.avatarPath = ApiConfig::instance().baseUrl() + avatarStr;
                }
                else
                {
                    info.avatarPath = avatarStr;
                }
                QString localPath = AssetCacheManager::instance().getLocalPath(info.avatarPath);
                if (!localPath.isEmpty())
                {
                    info.localAvatarPath = localPath;
                }
            }
            else
            {
                info.avatarPath = "";
                info.localAvatarPath = "";
            }

            UserMgr::inst()->saveUser(info);
            emit profileSynced(true, "OK");
        }
        else
        {
            emit profileSynced(false, "Invalid response format");
        }
    }
    // 2. 处理 getInfo (权限解析)
    else if (url.contains("/system/user/getInfo"))
    {
        if (!success)
        {
            emit permissionsSynced(false, errorMsg);
            return;
        }

        if (response.contains("data") && response["data"].isObject())
        {
            QJsonObject data = response["data"].toObject();

            UserInfo info = UserMgr::inst()->currentUser();
            info.permissions.clear();

            if (data.contains("permissions") && data["permissions"].isArray())
            {
                QJsonArray perms = data["permissions"].toArray();
                for (auto const& p : perms)
                {
                    info.permissions.insert(p.toString());
                }
            }

            UserMgr::inst()->saveUser(info);
            UserMgr::inst()->debugDumpPermissions();
            emit permissionsSynced(true, "OK");
        }
        else
        {
            emit permissionsSynced(false, "Invalid data format");
        }
    }
    // 3. 处理扫码登录响应
    else if (url.contains("/auth/login"))
    {
        if (!success)
        {
            emit loginFailed(errorMsg);
            return;
        }

        if (response.contains("data") && response["data"].isObject())
        {
            QString token = response["data"].toObject().value("access_token").toString();
            if (!token.isEmpty())
            {
                QString finalToken = token.startsWith("Bearer ") ? token : ("Bearer " + token);

                UserContext::instance().setToken(finalToken);
                UserInfo info = UserMgr::inst()->currentUser();
                info.token = finalToken;
                info.loginTime = QDateTime::currentSecsSinceEpoch();
                UserMgr::inst()->saveUser(info);

                emit loginSuccess(finalToken);
            }
            else
            {
                emit loginFailed("Token is empty");
            }
        }
        else
        {
            emit loginFailed("Invalid login response");
        }
    }
    // 4. 处理账号密码登录响应
    else if (url.contains("/auth/login"))
    {
        // [修复] 处理 404 等网络层面的失败
        if (!success)
        {
            qWarning() << ">>> 登录API返回失败:" << errorMsg;
            emit loginFailed("服务器连接失败: " + errorMsg);
            return;
        }

        // 解析 Token
        QString token;
        if (response.contains("data") && response["data"].isObject())
        {
            token = response["data"].toObject().value("access_token").toString();
        }
        else if (response.contains("access_token"))
        {
            token = response["access_token"].toString();
        }

        if (!token.isEmpty())
        {
            QString finalToken = token.startsWith("Bearer ") ? token : ("Bearer " + token);
            UserContext::instance().setToken(finalToken);

            UserInfo info = UserMgr::inst()->currentUser();
            info.token = finalToken;
            info.loginTime = QDateTime::currentSecsSinceEpoch();
            UserMgr::inst()->saveUser(info);

            emit loginSuccess(finalToken);
        }
        else
        {
            // 后端业务逻辑拒绝 (例如密码错误)
            QString msg = response["msg"].toString();
            emit loginFailed(msg.isEmpty() ? "登录失败: 账号或密码错误" : msg);
        }
    }
    // 5. 处理用户列表响应
    else if (url.contains("/system/user/list"))
    {
        if (!success)
        {
            emit userListFetched(false, {}, 0, errorMsg);
            return;
        }
        if (response.contains("rows") && response["rows"].isArray())
        {
            QJsonArray rows = response["rows"].toArray();
            int total = response["total"].toInt();
            if (url.contains("nickName=") || url.contains("userName="))
            {
                emit userSearchResult(rows);
            }
            else
            {
                emit userListFetched(true, rows, total, "OK");
            }
        }
        else
        {
            emit userListFetched(false, {}, 0, "Invalid data format");
        }
    }
}

void AuthService::onHttpResponse(bool success, QJsonObject const& data, QString const& errorMsg)
{
    // 旧代码，保留兼容性
    if (!success)
    {
        emit loginFailed("Network Error: " + errorMsg);
        return;
    }
    if (data.contains("token"))
    {
        emit loginSuccess(data["token"].toString());
    }
    else if (data.contains("code_sent"))
    {
        emit codeSent(true, "Code sent successfully");
    }
    else
    {
        emit loginFailed("Invalid credentials");
    }
}
