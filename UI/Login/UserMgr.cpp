#include "UserMgr.h"
#include <QCoreApplication>
#include <QDebug>
#include <QPainter>
#include <QPainterPath>

UserMgr* UserMgr::inst()
{
    static UserMgr instance;
    return &instance;
}

UserMgr::UserMgr()
{
    m_filePath = QCoreApplication::applicationDirPath() + "/user_data.json";
    m_clientId = "dccfc371cc6d6bd1b9acc2c7263c6e85";
}

void UserMgr::init()
{
    loadFromFile();
    if (m_userList.isEmpty())
    {
        UserInfo admin;
        admin.account = "admin";
        admin.password = "123456";
        admin.nickname = "管理员";
        admin.userId = 1;
        admin.permissions.insert("*:*:*");
        m_userList.append(admin);
        saveToFile();
    }
}

bool UserMgr::verifyUser(QString const& account, QString const& password)
{
    for (auto const& user : m_userList)
    {
        if (user.account == account && user.password == password)
        {
            m_currentUser = user;
            m_currentUser.loginTime = QDateTime::currentSecsSinceEpoch();
            saveUser(m_currentUser);
            return true;
        }
    }
    return false;
}

void UserMgr::saveUser(UserInfo const& info)
{
    // [Fix 1] 防止保存无效的空用户数据到 JSON 中
    if (info.account.isEmpty() && info.userId == 0)
    {
        return;
    }

    m_currentUser = info;

    // [Fix 2] 查找并移除旧记录，以便稍后添加到末尾 (实现 "最近活跃" 逻辑)
    // 这样 m_userList.last() 永远是最近登录的用户
    for (int i = 0; i < m_userList.size(); ++i)
    {
        if ((!info.account.isEmpty() && m_userList[i].account == info.account) ||
            (info.userId != 0 && m_userList[i].userId == info.userId))
        {
            m_userList.removeAt(i);
            break;
        }
    }

    // [Fix 3] 添加到末尾
    m_userList.append(info);

    // [Fix 4] 处理自动登录互斥逻辑 (保证只有一个账号开启自动登录)
    if (info.autoLogin)
    {
        // 因为刚刚append到了最后，所以遍历除最后一个之外的所有人
        for (int i = 0; i < m_userList.size() - 1; ++i)
        {
            m_userList[i].autoLogin = false;
        }
    }

    saveToFile();
}

UserInfo UserMgr::lastUser() const
{
    // 1. 优先返回开启了自动登录的用户
    for (auto const& user : m_userList)
    {
        if (user.autoLogin && !user.account.isEmpty())
            return user;
    }

    // 2. 如果没有自动登录，返回列表中最后一个 "有效" 的用户
    // 倒序遍历，找到第一个账号不为空的记录
    if (!m_userList.isEmpty())
    {
        for (int i = m_userList.size() - 1; i >= 0; --i)
        {
            if (!m_userList[i].account.isEmpty())
            {
                return m_userList[i];
            }
        }
    }

    return UserInfo();
}

void UserMgr::setCurrentUser(UserInfo const& info)
{
    m_currentUser = info;
}

bool UserMgr::hasPermission(QString const& permissionKey) const
{
    if (m_currentUser.permissions.contains("*:*:*"))
        return true;
    return m_currentUser.permissions.contains(permissionKey);
}

// [New] 打印权限列表
void UserMgr::debugDumpPermissions() const
{
    qDebug() << "================ User Permissions Dump ================";
    qDebug() << "User:" << m_currentUser.nickname << "(ID:" << m_currentUser.userId << ")";
    qDebug() << "Total Permissions:" << m_currentUser.permissions.size();
    for (QString const& p : m_currentUser.permissions)
    {
        qDebug() << "  [PERM] ->" << p;
    }
    qDebug() << "=======================================================";
}

void UserMgr::logout()
{
    // [Fix 5] 彻底清理状态
    m_currentUser.autoLogin = false;
    m_currentUser.rememberPwd = false;  // 关键：取消记住密码
    m_currentUser.password = "";        // 关键：清空密码
    m_currentUser.token = "";
    m_currentUser.loginTime = 0;
    m_currentUser.permissions.clear();

    // 调用 saveUser 将状态写入 json，这样下次启动时，
    // lastUser() 读到的就是这个已经被清除状态的记录
    saveUser(m_currentUser);
}

QPixmap UserMgr::makeRoundAvatar(QString const& path, int size, QColor defaultColor)
{
    QPixmap result(size, size);
    result.fill(Qt::transparent);

    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    QPixmap src(path);

    if (src.isNull())
    {
        p.setBrush(defaultColor);
        p.setPen(Qt::NoPen);
        p.drawEllipse(0, 0, size, size);
    }
    else
    {
        QPainterPath clipPath;
        clipPath.addEllipse(0, 0, size, size);
        p.setClipPath(clipPath);

        QPixmap scaled =
            src.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);

        int x = (size - scaled.width()) / 2;
        int y = (size - scaled.height()) / 2;
        p.drawPixmap(x, y, scaled);
    }
    return result;
}

void UserMgr::loadFromFile()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly))
        return;

    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonArray array = doc.array();

    m_userList.clear();
    for (auto const& val : array)
    {
        m_userList.append(UserInfo::fromJson(val.toObject()));
    }
    file.close();
}

void UserMgr::saveToFile()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly))
        return;

    QJsonArray array;
    for (auto const& user : m_userList)
    {
        array.append(user.toJson());
    }
    QJsonDocument doc(array);
    file.write(doc.toJson());
    file.close();
}
