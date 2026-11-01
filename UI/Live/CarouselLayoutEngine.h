#ifndef CAROUSELLAYOUTENGINE_H
#define CAROUSELLAYOUTENGINE_H

#include <QObject>
#include <QRect>
#include <QVector>
#include <QSize>

/**
 * @brief 单个卡片的布局属性计算结果
 * 纯数据结构，View 层拿到这个结构后直接应用到 Widget 上
 */
struct LayoutAttributes
{
    int index = 0;                // 对应数据源索引
    QRect geometry;               // 计算后的坐标和尺寸
    double zValue = 0.0;          // Z轴深度 (用于排序)
    double opacity = 1.0;         // 透明度
    double scale = 1.0;           // 视觉缩放 (用于 View 变换)
    bool isVisible = true;        // 是否在可视范围内 (Culling)
    bool isActive = false;        // 是否处于 C 位焦点
    double parallaxOffset = 0.0;  // 视差偏移量 (用于内部图片偏移)
};

/**
 * @brief 3D 轮播布局核心算法引擎 (纯逻辑类)
 * 职责：根据容器尺寸和滚动值，计算所有卡片的几何属性。
 * 特点：无状态、纯函数式计算、依赖 LiveUIConfig
 */
class CarouselLayoutEngine
{
public:
    CarouselLayoutEngine() = default;

    /**
     * @brief 核心计算函数
     * @param containerSize 容器大小
     * @param itemCount 卡片总数
     * @param scrollValue 当前滚动值 (浮点数，包含整数索引和偏移)
     * @return 所有卡片的布局属性列表
     */
    QVector<LayoutAttributes> calculateLayout(QSize const& containerSize,
                                              int itemCount,
                                              double scrollValue);

private:
    // 辅助：规范化 diff 到 -N/2 ~ N/2 之间，处理循环滚动
    double normalizeDiff(double diff, int n);
};

#endif  // CAROUSELLAYOUTENGINE_H
