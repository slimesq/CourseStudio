#ifndef MENTORCARD_H
#define MENTORCARD_H

#include <QWidget>
#include <QPixmap>
#include <QPropertyAnimation>
#include "LiveCourseModel.h"

/**
 * @brief 独立的导师/课程展示卡片组件
 * 职责：只负责单张卡片的渲染和内部交互（点击缩放），不包含任何业务逻辑。
 * 数据源：CourseEntity
 * 样式源：LiveUIConfig
 */
class MentorCard : public QWidget
{
    Q_OBJECT
    // 内部缩放属性 (用于鼠标按下的弹簧动画)
    Q_PROPERTY(qreal cardScale READ cardScale WRITE setCardScale)

public:
    explicit MentorCard(QWidget* parent = nullptr);

    // ==========================================
    // 1. 数据与状态接口 (解耦的核心)
    // ==========================================

    // 注入业务数据
    void setData(CourseEntity const& data);

    // 异步更新图片 (当 AssetCacheManager 下载完成后调用)
    void updateImage(QString const& localPath);

    // 更新视觉状态 (由 CarouselLayoutEngine 驱动)
    // isActive: 是否处于焦点(C位)
    // opacity: 透明度 (0.0 - 1.0)
    void updateState(bool isActive, double opacity);

    // 获取当前关联的房间ID
    long long getRoomId() const;

    // 内部动画属性访问器
    qreal cardScale() const
    {
        return m_cardScale;
    }
    void setCardScale(qreal s);

signals:
    void clicked(long long roomId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    // 绘图辅助函数
    void drawShadow(QPainter& p, QRect const& rect);
    void drawContent(QPainter& p, QRect const& rect);

private:
    CourseEntity m_data;
    QPixmap m_sourceImage;  // 原始加载的图片缓存

    // 视觉状态
    bool m_isActive = false;
    double m_opacity = 1.0;

    // 交互状态
    bool m_isPressed = false;
    qreal m_cardScale = 1.0;  // 仅用于点击时的微缩动画，与布局缩放无关

    QPropertyAnimation* m_scaleAnim = nullptr;
};

#endif  // MENTORCARD_H
