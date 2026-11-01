#ifndef WORKMODECARD_H
#define WORKMODECARD_H

#include <QWidget>
#include <QLabel>
#include <QGraphicsDropShadowEffect>
#include <QVBoxLayout>
#include <QPixmap>
#include <QImage>
#include <QFutureWatcher>
#include "ModeDefs.h"

// [新增] 卡片样式配置参数结构体
struct CardStyleConfig
{
    // 字体大小 (逻辑像素，会根据卡片缩放比例自动调整)
    int titleFontSizeActive = 30;  // 选中标题字号 (原 24) -> 调大
    int titleFontSizeNormal = 26;  // 普通标题字号 (原 22) -> 调大
    int descFontSizeActive = 17;   // 选中描述字号 (原 15) -> 调大
    int descFontSizeNormal = 15;   // 普通描述字号 (原 14) -> 调大

    // 图片大小
    int iconBaseSize = 220;  // 图片基准大小 (原 140) -> 大幅调大

    // 间距调整 (可选)
    int contentTopMargin = 50;  // 文字顶部起始位置
    int iconBottomMargin = 40;  // 图片底部边距
};

class WorkModeCard : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(bool active READ isActive WRITE setActive)

public:
    explicit WorkModeCard(ModeCardConfig const& config, QWidget* parent = nullptr);
    ~WorkModeCard();

    // 设置标准参考尺寸
    void setStandardSize(QSize const& size);

    void setActive(bool active);
    bool isActive() const
    {
        return m_isActive;
    }

    void setScrollingState(bool isScrolling);

    // [新增] 允许外部动态修改样式参数
    void setStyleConfig(CardStyleConfig const& config);

signals:
    void clicked(QString const& title);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void initUI();
    void initAsyncSystem();

    // 触发原始图片加载 (IO)
    void triggerAsyncLoad();

    // [Scheme 3 核心] 触发全卡片后台渲染 (CPU)
    void triggerAsyncRender();

private slots:
    void onRawImageLoaded();
    void onRenderFinished();

private:
    ModeCardConfig m_config;
    bool m_isPressed = false;
    bool m_isActive = false;
    bool m_isScrolling = false;

    // 目标尺寸（我们希望渲染成的尺寸）
    QSize m_targetSize;

    // [Cache L1] 原始图片数据 (IO读取缓存)
    QImage m_rawImage;

    // [Cache L2] 前端显示纹理 (可能与当前尺寸不一致，不一致时会被拉伸)
    QPixmap m_cachePixmap;

    // 异步监视器
    QFutureWatcher<QImage>* m_loadWatcher = nullptr;
    QFutureWatcher<QImage>* m_renderWatcher = nullptr;

    // 内部组件指针 (仅用于初始占位，实际绘制由 paintEvent 接管)
    QVBoxLayout* m_mainLayout = nullptr;

    // [新增] 保存当前样式配置
    CardStyleConfig m_styleConfig;
};

#endif  // WORKMODECARD_H
