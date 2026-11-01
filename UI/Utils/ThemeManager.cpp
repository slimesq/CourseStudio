#include "ThemeManager.h"
#include <QFile>
#include <QJsonDocument>
#include <QCoreApplication>
#include <QDebug>
#include <QStringList>
#include <QTimer>

ThemeManager* ThemeManager::m_instance = nullptr;

ThemeManager* ThemeManager::inst()
{
    if (!m_instance)
    {
        m_instance = new ThemeManager();
    }
    return m_instance;
}

ThemeManager::ThemeManager(QObject* parent) : QObject(parent)
{
    // 设定配置文件路径
    m_configPath = QCoreApplication::applicationDirPath() + "/theme.json";

    m_watcher = new QFileSystemWatcher(this);
    // 如果文件不存在，先创建一个默认的（或者手动创建）
    if (!QFile::exists(m_configPath))
    {
        qWarning() << "Theme file not found, creating default at:" << m_configPath;
        QFile file(m_configPath);
        if (file.open(QIODevice::WriteOnly))
        {
            file.write("{}");
            file.close();
        }
    }

    m_watcher->addPath(m_configPath);

    // [优化] 使用防抖 Timer，避免瞬间读到空文件或编辑器锁定
    QTimer* debounceTimer = new QTimer(this);
    debounceTimer->setSingleShot(true);
    debounceTimer->setInterval(100);  // 100ms 延迟

    connect(m_watcher,
            &QFileSystemWatcher::fileChanged,
            this,
            [this, debounceTimer](QString const& path) {
                qDebug() << "File changed detected, waiting to reload...";
                debounceTimer->start();  // 重新计时
            });

    connect(debounceTimer, &QTimer::timeout, this, &ThemeManager::loadTheme);

    loadTheme();
}

void ThemeManager::loadTheme()
{
    // 1. 先读取文件到临时变量 (IO操作不需要锁)
    QJsonObject newData;
    bool loadOk = false;

    QFile file(m_configPath);
    if (file.open(QIODevice::ReadOnly))
    {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isNull() && doc.isObject())
        {
            newData = doc.object();
            loadOk = true;
        }
        file.close();
    }

    if (loadOk)
    {
        {
            // 2. 仅在更新内存数据时加锁
            QMutexLocker locker(&m_mutex);
            m_themeData = newData;

            // 某些编辑器保存时会先删除再创建，导致监控丢失，需要重新添加
            if (!m_watcher->files().contains(m_configPath))
            {
                m_watcher->addPath(m_configPath);
            }
        }  // <--- 关键点：大括号结束，locker 析构，锁在这里释放！

        // 3. 锁释放后，再发送信号
        // 此时槽函数回调回来调用 getColor 时，锁是空闲的，不会卡死
        qDebug() << "Theme loaded successfully.";
        emit themeChanged();
    }
}

QJsonValue ThemeManager::getValue(QString const& keyPath)
{
    // 支持 "section.key" 格式
    QStringList keys = keyPath.split(".");
    QJsonObject currentObj = m_themeData;

    for (int i = 0; i < keys.size() - 1; ++i)
    {
        QString k = keys[i];
        if (currentObj.contains(k) && currentObj[k].isObject())
        {
            currentObj = currentObj[k].toObject();
        }
        else
        {
            return QJsonValue::Undefined;
        }
    }

    return currentObj[keys.last()];
}

QColor ThemeManager::getColor(QString const& keyPath, QColor const& defaultVal)
{
    QMutexLocker locker(&m_mutex);
    QJsonValue val = getValue(keyPath);
    if (val.isString())
    {
        QColor c(val.toString());
        if (c.isValid())
            return c;
    }
    return defaultVal;
}

int ThemeManager::getInt(QString const& keyPath, int defaultVal)
{
    QMutexLocker locker(&m_mutex);
    QJsonValue val = getValue(keyPath);
    if (val.isDouble())
        return val.toInt();
    return defaultVal;
}

double ThemeManager::getDouble(QString const& keyPath, double defaultVal)
{
    QMutexLocker locker(&m_mutex);
    QJsonValue val = getValue(keyPath);
    if (val.isDouble())
        return val.toDouble();
    return defaultVal;
}

QString ThemeManager::getString(QString const& keyPath, QString const& defaultVal)
{
    QMutexLocker locker(&m_mutex);
    QJsonValue val = getValue(keyPath);
    if (val.isString())
        return val.toString();
    return defaultVal;
}
