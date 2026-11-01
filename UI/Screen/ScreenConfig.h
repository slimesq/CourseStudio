#ifndef SCREENCONFIG_H
#define SCREENCONFIG_H

#include <QObject>
#include <QSize>
#include <QString>
#include <QColor>
#include <QJsonObject>
#include <QMutex>

// 窗口基础布局参数
struct WindowParams
{
    QSize defaultSize;  // 默认窗口大小
    QSize minSize;      // 最小窗口大小
    bool frameless;     // 是否无边框
    int radius;         // 窗口圆角
};

// 内部组件尺寸参数
struct LayoutParams
{
    int titleBarHeight;     // 标题栏高度
    int bottomBarHeight;    // 底部控制栏高度
    int leftPanelWidth;     // 左侧场景列表宽度
    int dockDefaultWidth;   // 悬浮窗默认宽度
    int dockDefaultHeight;  // 悬浮窗默认高度
    int resizeHandleSize;   // 边缘缩放触发区域大小
};

// 样式与资源路径参数
struct StyleParams
{
    QString titleText;   // 窗口标题文字
    QString logoPath;    // Logo 图片路径
    QString themeColor;  // 主题色 (Hex)
    QString bgColor;     // 背景色 (Hex)

    // 图标路径配置
    QString iconMin;
    QString iconMax;
    QString iconRestore;
    QString iconClose;
    QString iconStartLive;
    QString iconStopLive;
};

class ScreenConfig : public QObject
{
    Q_OBJECT
public:
    static ScreenConfig& instance();

    // 加载配置 (默认读取 screen_config.json)
    void load(QString const& configPath = "screen_config.json");

    // 获取参数组
    WindowParams const& window() const
    {
        return m_window;
    }
    LayoutParams const& layout() const
    {
        return m_layout;
    }
    StyleParams const& style() const
    {
        return m_style;
    }

    // 获取缩放后的像素值 (支持高分屏)
    int scaled(int value) const;

signals:
    void configChanged();

private:
    ScreenConfig();
    Q_DISABLE_COPY(ScreenConfig)

    void parseWindow(QJsonObject const& root);
    void parseLayout(QJsonObject const& root);
    void parseStyle(QJsonObject const& root);

private:
    WindowParams m_window;
    LayoutParams m_layout;
    StyleParams m_style;

    double m_uiScale = 1.0;
    mutable QMutex m_mutex;
};

#endif  // SCREENCONFIG_H
