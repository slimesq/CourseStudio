#include "CarouselLayoutEngine.h"
#include "LiveUIConfig.h"
#include <QtMath>
#include <algorithm>

double CarouselLayoutEngine::normalizeDiff(double diff, int n)
{
    if (n <= 0)
        return 0.0;
    while (diff > n / 2.0)
        diff -= n;
    while (diff < -n / 2.0)
        diff += n;
    return diff;
}

QVector<LayoutAttributes> CarouselLayoutEngine::calculateLayout(QSize const& containerSize,
                                                                int itemCount,
                                                                double scrollValue)
{
    QVector<LayoutAttributes> results;
    if (itemCount == 0)
        return results;

    // 预分配内存，极致性能
    results.reserve(itemCount);

    // 1. 获取全局配置参数
    auto const& cfg = LiveUIConfig::instance();
    auto const& layoutCfg = cfg.layout();
    auto const& dimens = cfg.dimens();

    int w = containerSize.width();
    int h = containerSize.height();

    // 如果容器未初始化，给予默认值防止除零
    if (w <= 0 || h <= 0)
    {
        w = cfg.scaled(dimens.baseWindowWidth * 0.55);
        h = cfg.scaled(800);
    }

    int centerX = w / 2;
    int centerY = h / 2;

    // 2. 计算卡片基准尺寸 (响应式)
    int gap = cfg.scaled(layoutCfg.cardGap);

    // 预留上下按钮的安全区域 (按钮高度 + margin)
    int btnReserved = cfg.scaled(dimens.navBtnSize + 20);
    int safeHeight = h - (btnReserved * 2);
    if (safeHeight < 100)
        safeHeight = 100;

    // 根据配置比例计算中心卡片尺寸
    // Height = SafeHeight / Factor
    int maxCenterH = static_cast<int>((safeHeight - 2 * gap) / layoutCfg.centerHeightFactor);
    // Width = Height * Ratio
    int targetCenterW = static_cast<int>(maxCenterH * layoutCfg.widthRatio);

    // 限制最大宽度
    int maxW = static_cast<int>(w * layoutCfg.maxContainerWidthRatio);
    if (targetCenterW > maxW)
    {
        targetCenterW = maxW;
        // 反算高度保持比例
        maxCenterH = static_cast<int>(targetCenterW / layoutCfg.widthRatio);
    }

    int targetCenterH = maxCenterH;

    // 侧边卡片尺寸
    int targetSideW = static_cast<int>(targetCenterW * layoutCfg.sideCardScale);
    int targetSideH = static_cast<int>(targetCenterH * layoutCfg.sideCardScale);

    // 3. 遍历计算每个卡片的属性
    for (int i = 0; i < itemCount; ++i)
    {
        LayoutAttributes attr;
        attr.index = i;

        // 计算与当前滚动值的距离 (处理循环)
        double rawDiff = i - scrollValue;
        double diff = normalizeDiff(rawDiff, itemCount);
        double absDiff = qAbs(diff);

        // 3.1 可见性裁剪 (Culling)
        // 阈值宽松一点，避免边缘突然消失
        double visibleThreshold = layoutCfg.visibleCount * 0.6;  // 例如 3 * 0.6 = 1.8
        if (absDiff > visibleThreshold)
        {
            attr.isVisible = false;
            results.append(attr);
            continue;
        }
        attr.isVisible = true;

        // 3.2 视差与 Z轴
        attr.parallaxOffset = diff;  // 传递给 Card 做内部图片偏移
        attr.zValue = -absDiff;      // 越靠近中心(0)，Z值越大(显示在越上层)

        // 3.3 尺寸与位置插值算法
        double currentW, currentH, currentY;

        if (absDiff <= 1.0)
        {
            // 中心区域插值 (0 -> 1)
            // 从 CenterSize 线性过渡到 SideSize
            currentW = targetCenterW - absDiff * (targetCenterW - targetSideW);
            currentH = targetCenterH - absDiff * (targetCenterH - targetSideH);
        }
        else
        {
            // 侧边区域插值 (> 1)
            // 进一步缩小，模拟透视远去
            double factor = (absDiff - 1.0) * 0.1;  // 衰减系数
            currentW = targetSideW * (1.0 - factor);
            currentH = targetSideH * (1.0 - factor);
        }

        if (absDiff <= 0.05)
        {
            // 极小误差直接居中
            currentY = centerY;
        }
        else
        {
            double centerHalfH = targetCenterH / 2.0;
            double sideHalfH = targetSideH / 2.0;
            // Y轴偏移量 = 中心一半 + 间距 + 侧边一半
            double baseOffset = centerHalfH + gap + sideHalfH;

            double extraOffset = 0;
            if (absDiff > 1.0)
            {
                // 如果超过第1个位置，继续累加偏移
                extraOffset = (absDiff - 1.0) * (targetSideH + gap);
            }

            double totalOffset = baseOffset * (absDiff > 1.0 ? 1.0 : absDiff) + extraOffset;

            // diff < 0 表示在上面 (i < scrollValue)，Y 减小
            if (diff < 0)
                currentY = centerY - totalOffset;
            else
                currentY = centerY + totalOffset;
        }

        // 3.4 透明度与状态
        if (absDiff > 1.0)
        {
            attr.opacity = 1.0 - (absDiff - 1.0) / 0.8;  // 线性衰减
            if (attr.opacity < 0)
                attr.opacity = 0;
        }
        else
        {
            attr.opacity = 1.0;
        }

        attr.isActive = (absDiff < 0.15);  // 判定是否为焦点卡片

        // 3.5 最终 Geometry 生成
        int finalW = static_cast<int>(currentW);
        int finalH = static_cast<int>(currentH);
        int finalX = centerX - finalW / 2;
        int finalY = static_cast<int>(currentY - finalH / 2);

        attr.geometry = QRect(finalX, finalY, finalW, finalH);

        results.append(attr);
    }

    return results;
}
