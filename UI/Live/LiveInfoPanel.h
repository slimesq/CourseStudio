#ifndef LIVEINFOPANEL_H
#define LIVEINFOPANEL_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QTimer>
#include <QTextDocument>
#include "LiveCourseModel.h"

// [New Scheme] 自绘文本组件
// 优势：完全解耦布局计算与渲染，杜绝 heightForWidth 滞后问题
class LiveTextWidget : public QWidget
{
    Q_OBJECT
public:
    explicit LiveTextWidget(QWidget* parent = nullptr);

    // 设置文本 (支持普通文本，自动转为 HTML 保证格式)
    void setText(QString const& text);
    // 设置样式 (直接控制文档字体和颜色)
    void setTextStyle(QFont const& font, QColor const& color, int lineHeight = 120);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void recalculateHeight();

private:
    QTextDocument* m_doc;
    int m_fixedHeight = 0;
};

class LiveInfoPanel : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(double textOpacity READ textOpacity WRITE setTextOpacity)

public:
    explicit LiveInfoPanel(QWidget* parent = nullptr);
    ~LiveInfoPanel();

    void setData(CourseEntity const& data, bool animate = true);
    void setButtonLoading(bool isLoading);

    // 适配父容器接口
    QSize minimumSizeHint() const override;

    // 强制刷新接口
    void forceLayoutRefresh();

signals:
    void enterClicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void updateStyles();
    void onLoadingTimer();

private:
    void initUI();

    // 辅助构建简单的详情行 (图标 + 单行文本)
    QWidget* createInfoRow(QString const& iconChar, QLabel*& outLabel);

    void addOpacityEffect(QWidget* target);

    double textOpacity() const
    {
        return m_textOpacity;
    }
    void setTextOpacity(double opacity);

private:
    // --- 布局结构 ---
    QVBoxLayout* m_mainLayout = nullptr;

    // 1. Tag 区域
    QWidget* m_tagContainer = nullptr;
    QLabel* m_lblTag = nullptr;

    QWidget* m_dynamicContainer = nullptr;

    // [New] 新增：用于将标题向下推 80px 的占位符
    QWidget* m_titleTopSpacer = nullptr;

    // 2. 核心内容区域 (无需中间容器，直接入主布局)
    LiveTextWidget* m_txtTitle = nullptr;  // 标题

    // 详情行
    QWidget* m_rowTime = nullptr;
    QLabel* m_lblTime = nullptr;

    QWidget* m_rowLecturer = nullptr;
    QLabel* m_lblLecturer = nullptr;

    // 描述 (最容易出问题的部分，现在用自绘组件)
    LiveTextWidget* m_txtDesc = nullptr;

    // 3. 底部区域
    QWidget* m_platformContainer = nullptr;
    QWidget* m_btnContainer = nullptr;
    QPushButton* m_btnEnter = nullptr;

    // --- 状态 ---
    CourseEntity m_data;
    bool m_isLoading = false;
    int m_loadingDots = 0;

    QPropertyAnimation* m_textAnim = nullptr;
    double m_textOpacity = 1.0;
    // 使用 QGraphicsOpacityEffect 统一控制透明度
    QList<QGraphicsOpacityEffect*> m_textEffects;  // 新增这一行

    QTimer* m_loadingTimer = nullptr;
};

#endif  // LIVEINFOPANEL_H
