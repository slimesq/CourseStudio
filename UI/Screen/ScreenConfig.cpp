#include "ScreenConfig.h"
#include <QFile>
#include <QJsonDocument>
#include <QApplication>
#include <QDebug>
#include <QScreen>
#include <QStandardPaths>
#include <QDir>

ScreenConfig& ScreenConfig::instance()
{
    static ScreenConfig s_instance;
    return s_instance;
}

ScreenConfig::ScreenConfig()
{
    // =========================================================
    // 1. 设置默认值 (兜底策略)
    // 这里的数值严格对应原 ScreenWindow 中的硬编码，确保无损替换
    // =========================================================

    // Window Defaults
    m_window.defaultSize = QSize(1280, 860);
    m_window.minSize = QSize(1000, 600);
    m_window.frameless = true;
    m_window.radius = 0;

    // Layout Defaults
    m_layout.titleBarHeight = 80;
    m_layout.bottomBarHeight = 60;
    m_layout.leftPanelWidth = 240;
    m_layout.dockDefaultWidth = 300;
    m_layout.dockDefaultHeight = 400;
    m_layout.resizeHandleSize = 8;

    // Style Defaults
    m_style.titleText = "LIVE STUDIO";
    m_style.logoPath = "";
    m_style.themeColor = "#0078D7";
    m_style.bgColor = "#1E1E1E";

    // Default Icons
    m_style.iconMin = "_";
    m_style.iconMax = "□";
    m_style.iconRestore = "❐";
    m_style.iconClose = "×";
    m_style.iconStartLive = "开始直播";
    m_style.iconStopLive = "结束直播";

    // 计算初始 DPI 缩放
    QScreen* screen = QApplication::primaryScreen();
    if (screen)
    {
        m_uiScale = screen->logicalDotsPerInch() / 96.0;
        if (m_uiScale < 1.0)
            m_uiScale = 1.0;
    }
}

void ScreenConfig::load(QString const& configPath)
{
    QString fullPath = QCoreApplication::applicationDirPath() + "/" + configPath;
    QFile file(fullPath);

    if (!file.exists())
    {
        qDebug() << "[ScreenConfig] Config file not found, using defaults:" << fullPath;
        return;
    }

    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "[ScreenConfig] Failed to open config file.";
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject())
    {
        qWarning() << "[ScreenConfig] Invalid JSON format.";
        return;
    }

    QJsonObject root = doc.object();

    {
        QMutexLocker locker(&m_mutex);
        parseWindow(root.value("window").toObject());
        parseLayout(root.value("layout").toObject());
        parseStyle(root.value("style").toObject());
    }

    qDebug() << "[ScreenConfig] Configuration loaded successfully.";
    emit configChanged();
}

void ScreenConfig::parseWindow(QJsonObject const& root)
{
    if (root.isEmpty())
        return;

    if (root.contains("width") && root.contains("height"))
    {
        m_window.defaultSize = QSize(root["width"].toInt(), root["height"].toInt());
    }
    if (root.contains("minWidth") && root.contains("minHeight"))
    {
        m_window.minSize = QSize(root["minWidth"].toInt(), root["minHeight"].toInt());
    }
    if (root.contains("frameless"))
    {
        m_window.frameless = root["frameless"].toBool();
    }
    if (root.contains("radius"))
    {
        m_window.radius = root["radius"].toInt();
    }
}

void ScreenConfig::parseLayout(QJsonObject const& root)
{
    if (root.isEmpty())
        return;

    if (root.contains("titleBarHeight"))
        m_layout.titleBarHeight = root["titleBarHeight"].toInt();
    if (root.contains("bottomBarHeight"))
        m_layout.bottomBarHeight = root["bottomBarHeight"].toInt();
    if (root.contains("leftPanelWidth"))
        m_layout.leftPanelWidth = root["leftPanelWidth"].toInt();
    if (root.contains("dockWidth"))
        m_layout.dockDefaultWidth = root["dockWidth"].toInt();
    if (root.contains("dockHeight"))
        m_layout.dockDefaultHeight = root["dockHeight"].toInt();
    if (root.contains("resizeHandle"))
        m_layout.resizeHandleSize = root["resizeHandle"].toInt();
}

void ScreenConfig::parseStyle(QJsonObject const& root)
{
    if (root.isEmpty())
        return;

    if (root.contains("titleText"))
        m_style.titleText = root["titleText"].toString();
    if (root.contains("themeColor"))
        m_style.themeColor = root["themeColor"].toString();
    if (root.contains("bgColor"))
        m_style.bgColor = root["bgColor"].toString();

    if (root.contains("icons"))
    {
        QJsonObject icons = root["icons"].toObject();
        if (icons.contains("min"))
            m_style.iconMin = icons["min"].toString();
        if (icons.contains("max"))
            m_style.iconMax = icons["max"].toString();
        if (icons.contains("close"))
            m_style.iconClose = icons["close"].toString();
    }
}

int ScreenConfig::scaled(int value) const
{
    return static_cast<int>(value * m_uiScale);
}
