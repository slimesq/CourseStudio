#pragma once
#include <QObject>
#include <QString>
#include <QDebug>
#include "UserMgr.h"  // 引用 UserMgr

enum class UserLevel
{
    Standard,
    VIP
};

class UserContext : public QObject
{
    Q_OBJECT
public:
    static UserContext& instance()
    {
        static UserContext s;
        return s;
    }

    void setToken(QString const& token)
    {
        m_token = token;
        qDebug() << "token: " << m_token;
    }
    QString token() const
    {
        return m_token;
    }

    // [Modified] 委托给 UserMgr 获取，保证数据源唯一
    QString clientId() const
    {
        return UserMgr::inst()->clientId();
    }

    void setLevel(UserLevel level)
    {
        m_level = level;
    }
    UserLevel level() const
    {
        return m_level;
    }

    bool canUpload(int64_t size) const
    {
        return true;
    }

    int maxParallelDownloads() const
    {
        return (m_level == UserLevel::VIP) ? 5 : 1;
    }

private:
    UserContext()
    {
    }
    QString m_token = "";
    // m_clientId 已移除，逻辑移至 UserMgr
    UserLevel m_level = UserLevel::Standard;
};
