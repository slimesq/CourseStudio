#ifndef LIVEUICONFIG_H
#define LIVEUICONFIG_H
#include <QObject>
#include <QColor>
#include <QFont>
#include <QSize>
#include <QEasingCurve>
#include <QJsonObject>
#include <QMutex>

/**
 * @brief 直播课程选择页面的全局配置中心 (单例)
 * 支持热插拔（JSON加载）、高内聚（参数分组）、响应式布局计算
 */
class LiveUIConfig : public QObject
{
    Q_OBJECT
public:
    // ==========================================
    // 1. 配置子模块定义 (高内聚分组)
    // ==========================================

    // 颜色定义 (支持深色/浅色模式切换的基础)
    struct Colors
    {
        QColor backgroundStart = QColor(245, 247, 250);
        QColor backgroundEnd = QColor(230, 235, 240);
        QColor cardShadowActive = QColor(0, 0, 0, 50);
        QColor cardShadowNormal = QColor(0, 0, 0, 15);

        // 字体颜色
        QColor textTitle = QColor("#000000");
        QColor textTag = QColor("#333333");
        QColor textInfoMain = QColor("#1D2129");  // 讲师、时间
        QColor textInfoSub = QColor("#86909C");   // 图标

        // 按钮颜色
        QColor btnPrimaryStart = QColor("#53A8FF");
        QColor btnPrimaryEnd = QColor("#409EFF");
        QColor btnHover = QColor("#66b1ff");
        QColor btnText = Qt::white;

        // 导航按钮
        QColor navBtnBg = QColor(255, 255, 255, 230);  // 0.9 alpha
        QColor navBtnBorder = QColor("#EBEEF5");
        QColor navBtnIcon = QColor("#606266");
    };

    // 基础尺寸定义 (基于 1200px 宽度的基准值)
    struct Dimensions
    {
        double baseWindowWidth = 1200.0;  // 缩放基准

        // 字体基准大小 (px)
        int fontTag = 30;
        int fontTitle = 46;
        int fontInfoIcon = 28;  // Emoji icon size
        int fontInfoText = 16;
        int fontBtn = 20;

        // 间距基准 (px)
        int leftPanelMarginH = 80;
        int leftPanelMarginV = 50;
        int leftPanelSpacing = 35;

        // 组件尺寸
        QSize enterBtnSize = QSize(220, 60);
        int navBtnSize = 40;
    };

    // 3D 轮播布局参数 (核心算法参数)
    struct CarouselLayout
    {
        int cardGap = 30;                      // 卡片间距
        double centerHeightFactor = 2.2;       // 控制主卡片高度占比 (ContainerH / factor)
        double widthRatio = 1.8;               // 宽高比 (Width = Height * ratio)
        double maxContainerWidthRatio = 0.95;  // 卡片最大宽度占容器比例
        double sideCardScale = 0.85;           // 侧边卡片缩放比例
        int visibleCount = 3;                  // 可见卡片数量逻辑阈值
    };

    // 动画参数
    struct Animation
    {
        int scrollDuration = 550;  // 毫秒
        int scaleDuration = 100;   // 毫秒
        QEasingCurve::Type scrollCurve = QEasingCurve::OutQuint;
        QEasingCurve::Type scaleCurve = QEasingCurve::OutQuad;
        int throttleInterval = 100;  // 滚动节流间隔
    };

public:
    static LiveUIConfig& instance();

    // ==========================================
    // 2. 核心操作接口
    // ==========================================

    // 设置全局 UI 缩放比例 (Responsive Layout)
    void setUiScale(double scale);
    double uiScale() const
    {
        return m_uiScale;
    }

    // 获取经过缩放计算后的数值 (Helper)
    int scaled(int baseValue) const;
    double scaled(double baseValue) const;

    // 热更接口
    void loadFromJson(QJsonObject const& json);
    QJsonObject saveToJson() const;

    // ==========================================
    // 3. 属性访问 (直接只读访问以保证极致性能)
    // ==========================================
    Colors const& colors() const
    {
        return m_colors;
    }
    Dimensions const& dimens() const
    {
        return m_dimens;
    }
    CarouselLayout const& layout() const
    {
        return m_layout;
    }
    Animation const& anim() const
    {
        return m_anim;
    }

signals:
    // 当配置加载或 Scale 变化时触发，UI 组件需连接此信号刷新 geometry/style
    void configChanged();

private:
    LiveUIConfig();  // Private Constructor
    Q_DISABLE_COPY(LiveUIConfig)

    double m_uiScale = 1.0;

    // 配置实例
    Colors m_colors;
    Dimensions m_dimens;
    CarouselLayout m_layout;
    Animation m_anim;

    mutable QMutex m_mutex;
};

#endif  // LIVEUICONFIG_H
