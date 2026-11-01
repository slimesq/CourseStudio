#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>
#include <QJsonObject>
#include <QColor>
#include <QFileSystemWatcher>
#include <QMutex>

class ThemeManager : public QObject
{
    Q_OBJECT
public:
    static ThemeManager* inst();

    // 核心接口：获取配置值
    QColor getColor(QString const& keyPath, QColor const& defaultVal = Qt::white);
    int getInt(QString const& keyPath, int defaultVal = 0);
    double getDouble(QString const& keyPath, double defaultVal = 0.0);
    QString getString(QString const& keyPath, QString const& defaultVal = "");

signals:
    void themeChanged();

private:
    explicit ThemeManager(QObject* parent = nullptr);
    void loadTheme();

    // 辅助：解析 "visual_item.border_color" 这种点分隔的路径
    QJsonValue getValue(QString const& keyPath);

private:
    static ThemeManager* m_instance;
    QJsonObject m_themeData;
    QFileSystemWatcher* m_watcher;
    QString m_configPath;
    QMutex m_mutex;  // 保证多线程读取安全
};

#endif  // THEMEMANAGER_H
