#pragma once
#include "Types.h"
#include "SceneItem.h"
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>

// --- 策略接口 (Strategy Interface) ---
class ICanvasScalingStrategy
{
public:
    virtual ~ICanvasScalingStrategy() = default;
    virtual void apply(
        std::vector<std::shared_ptr<SceneItem>>& items, int oldW, int oldH, int newW, int newH) = 0;
};

// --- 策略1: 不缩放 (None) ---
class NoScalingStrategy : public ICanvasScalingStrategy
{
public:
    void apply(std::vector<std::shared_ptr<SceneItem>>& items, int, int, int, int) override
    {
        // 什么都不做，保持绝对坐标
    }
};

// --- 策略2: 拉伸 (Stretch) ---
// 优点：完全可逆。当画布从 16:9 变 4:3 再变回 16:9，物体会完美还原。
// 缺点：由于宽高缩放比例不同，物体会暂时变形（变扁或变瘦）。
class StretchScalingStrategy : public ICanvasScalingStrategy
{
public:
    void apply(std::vector<std::shared_ptr<SceneItem>>& items,
               int oldW,
               int oldH,
               int newW,
               int newH) override
    {
        if (oldW <= 0 || oldH <= 0)
            return;

        float ratioW = (float)newW / oldW;
        float ratioH = (float)newH / oldH;

        for (auto& item : items)
        {
            Transform t = item->getTransform();

            // 简单的线性映射
            float newX = t.x * ratioW;
            float newY = t.y * ratioH;
            float newWidth = t.width * ratioW;
            float newHeight = t.height * ratioH;

            item->setPosition(newX, newY);
            item->setSize(newWidth, newHeight);
        }
    }
};

// --- 策略3: 保持比例适应 (Aspect Fit) ---
// 优点：物体永远不会变形。
// 缺点：非等比调整画布时，为了不超出边界，物体会选择较小的缩放比，导致物体“越来越小”。
class AspectFitScalingStrategy : public ICanvasScalingStrategy
{
public:
    void apply(std::vector<std::shared_ptr<SceneItem>>& items,
               int oldW,
               int oldH,
               int newW,
               int newH) override
    {
        if (oldW <= 0 || oldH <= 0)
            return;

        // 计算整体画布的缩放因子
        float ratioW = (float)newW / oldW;
        float ratioH = (float)newH / oldH;
        float scaleFactor = std::min(ratioW, ratioH);

        // 计算偏移量，确保在新画布比例不同时，物体依然维持在相对视觉中心
        float offsetX = (newW - (oldW * scaleFactor)) / 2.0f;
        float offsetY = (newH - (oldH * scaleFactor)) / 2.0f;

        for (auto& item : items)
        {
            Transform t = item->getTransform();

            // 1. 先将坐标映射到 [0, 1] 标准化坐标系，再乘以 scaleFactor
            // 2. 加上居中偏移量 offsetX/offsetY
            float newX = (t.x * scaleFactor) + offsetX;
            float newY = (t.y * scaleFactor) + offsetY;

            // 尺寸按统一比例缩放
            float newItemW = t.width * scaleFactor;
            float newItemH = t.height * scaleFactor;

            item->setPosition(newX, newY);
            item->setSize(newItemW, newItemH);
        }
    }
};

// --- 简单工厂 (Factory) ---
class CanvasScalingFactory
{
public:
    static std::shared_ptr<ICanvasScalingStrategy> create(CanvasScaleMode mode)
    {
        switch (mode)
        {
            case CanvasScaleMode::Stretch:
                return std::make_shared<StretchScalingStrategy>();
            case CanvasScaleMode::AspectFit:
                return std::make_shared<AspectFitScalingStrategy>();
            case CanvasScaleMode::None:
            default:
                return std::make_shared<NoScalingStrategy>();
        }
    }
};
