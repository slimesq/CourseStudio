#include "VideoEffectSystem.h"
#include "ConcreteEffects.h"  // 必须包含这个头文件以识别特效类
#include <algorithm>
#include <QCoreApplication>
#include <QDir>
#include "AdvancedEffects.h"
#include "StickerEffect.h"
#define ENABLE_FACE 0

// =========================================================
// EffectManager Implementation
// =========================================================

EffectManager::EffectManager()
{
    // 预分配内存，防止第一次运行时卡顿
    m_matBGR.reserve(1920 * 1080 * 3);
}

void EffectManager::registerEffect(std::string const& name,
                                   std::shared_ptr<IEffect> effect,
                                   int order)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // 如果已存在同名，先移除
    auto it = std::remove_if(m_pipeline.begin(), m_pipeline.end(), [&](EffectNode const& node) {
        return node.name == name;
    });
    m_pipeline.erase(it, m_pipeline.end());

    // 插入新节点
    EffectNode node;
    node.name = name;
    node.effect = effect;
    node.order = order;
    m_pipeline.push_back(node);

    // 按 Order 排序 (确保磨皮在锐化前，或者按用户指定顺序)
    std::sort(m_pipeline.begin(), m_pipeline.end(), [&](EffectNode const& a, EffectNode const& b) {
        return a.order < b.order;
    });

    qDebug() << "[EffectManager] Registered:" << name.c_str() << "Order:" << order;
}

void EffectManager::removeEffect(std::string const& name)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::remove_if(m_pipeline.begin(), m_pipeline.end(), [&](EffectNode const& node) {
        return node.name == name;
    });
    m_pipeline.erase(it, m_pipeline.end());
}

void EffectManager::setEffectEnabled(std::string const& name, bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& node : m_pipeline)
    {
        if (node.name == name)
        {
            node.effect->setEnabled(enabled);
            // qDebug() << "[EffectManager] " << name.c_str() << " -> " << enabled;
            return;
        }
    }
}

void EffectManager::setEffectParam(std::string const& name,
                                   std::string const& paramKey,
                                   QVariant const& value)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& node : m_pipeline)
    {
        if (node.name == name)
        {
            node.effect->setParameter(paramKey, value);
            return;
        }
    }
}

void EffectManager::processPipeline(uint8_t* data, int width, int height, int stride)
{
    if (!data)
        return;

    // [优化] 使用 try_lock 避免阻塞采集线程太久？
    // 不，这里必须处理，所以用 lock_guard
    std::lock_guard<std::mutex> lock(m_mutex);

    // 如果没有开启的特效，直接返回，避免任何内存转换开销
    bool hasActiveEffect = false;
    for (auto const& node : m_pipeline)
    {
        if (node.effect->isEnabled())
        {
            hasActiveEffect = true;
            break;
        }
    }
    if (!hasActiveEffect)
        return;

    // 1. 统一构建 BGRA Mat (Zero-Copy)
    cv::Mat matBGRA(height, width, CV_8UC4, data, stride);
    if (m_matBGR.empty() || m_matBGR.size() != matBGRA.size())
    {
        m_matBGR.create(matBGRA.size(), CV_8UC3);
    }
    // 2. 转换为 BGR
    // 如果尺寸变了，m_matBGR 会自动重新分配，否则复用内存
    cv::cvtColor(matBGRA, m_matBGR, cv::COLOR_BGRA2BGR);
    static FaceData lastValidFace;  // 缓存上一帧有效人脸
    static int missingCount = 0;    // 连续丢失计数

    if (m_faceTracker)
    {
        // 策略：每 2 帧检测一次，提升性能
        // 如果正在丢失中，则每帧检测以快速找回
        bool shouldDetect = (missingCount > 0) || (m_frameCounter++ % 2 == 0);

        if (shouldDetect)
        {
            m_faceTracker->detect(m_matBGR);
        }

        FaceData freshData = m_faceTracker->getFaceData();

        if (freshData.hasFace)
        {
            lastValidFace = freshData;
            missingCount = 0;
            // 降频打印，防止刷屏
            static int logC = 0;
            if (logC++ % 60 == 0)
            {
                qDebug() << "[EffectManager] Face Detected! Rect:" << freshData.rect.x << ","
                         << freshData.rect.y << " Pts:" << freshData.landmarks.size();
            }
        }
        else
        {
            missingCount++;
            // 容错：如果丢失不超过 5 帧，沿用上一帧的位置
            // 这能极大消除检测闪烁导致的特效失效
            if (missingCount < 5 && lastValidFace.hasFace)
            {
                // 可选：在这里加上卡尔曼滤波预测位置，更丝滑
                // 简单起见，直接复用
            }
            else
            {
                lastValidFace.hasFace = false;
            }
        }
    }

    // 3. 遍历流水线
    for (auto& node : m_pipeline)
    {
        if (node.effect->isEnabled())
        {
            // 不要在热路径中使用 try-catch，除非你预期它经常崩
            // 生产环境应该保证算法本身健壮
            if (node.name == "reshape")
            {
                // 强制打印 reshape 的执行状态
#if ENABLE_FACE
                qDebug() << "[EffectManager] Executing Reshape. FaceValid:"
                         << lastValidFace.hasFace;
#endif
            }
            node.effect->setFaceData(lastValidFace);
            node.effect->process(m_matBGR);
        }
    }

#if ENABLE_FACE  // 等价于 #if (ENABLE_FACE != 0)
    if (lastValidFace.hasFace)
    {
        cv::rectangle(m_matBGR, lastValidFace.rect, cv::Scalar(0, 255, 0), 2);
        for (auto& pt : lastValidFace.landmarks)
        {
            cv::circle(m_matBGR, pt, 2, cv::Scalar(0, 0, 255), -1);
        }
    }
#endif

    // 4. 转回 BGRA
    cv::cvtColor(m_matBGR, matBGRA, cv::COLOR_BGR2BGRA);
}

void EffectManager::initFaceTracker(std::string const& detPath)
{
    m_faceTracker = std::make_shared<FaceTracker>();
    m_faceTracker->loadModel(detPath);
}

bool EffectManager::isEffectEnabled(std::string const& name)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto const& node : m_pipeline)
    {
        if (node.name == name)
        {
            return node.effect->isEnabled();
        }
    }
    return false;  // 默认未找到则为关
}

void EffectConfigurator::configureDefaultPipeline(std::shared_ptr<EffectManager> manager)
{
    if (!manager)
        return;

    // 1. 初始化人脸检测模型 (路径去硬编码)
    // 建议将模型放在可执行文件同级目录的 assets/models 下
    QString appPath = QCoreApplication::applicationDirPath();
    QString modelPath = QDir(appPath).filePath("assets/models/face_detection_yunet_2023mar.onnx");
    qDebug() << "modelPath: " << modelPath;

    // 如果文件不存在，可以使用绝对路径作为开发环境的回退，但在生产环境应依赖相对路径
    manager->initFaceTracker(modelPath.toStdString());

    // 2. 注册特效 (定义顺序和默认值)
    // Order 0-100: 预处理 -> 磨皮 -> 脸部形变 -> 调色 -> 后处理

    // [磨皮]
    auto smooth = std::make_shared<SkinSmoothEffect>();
    manager->registerEffect("smooth", smooth, 10);
    // 默认参数业务逻辑
    manager->setEffectParam("smooth", "level", 0);  // 默认关闭，由用户开启

    // [美白]
    manager->registerEffect("whiten", std::make_shared<WhitenEffect>(), 20);

    // [微整形] - 瘦脸大眼
    auto reshape = std::make_shared<FaceReshapeEffect>();
    manager->registerEffect("reshape", reshape, 25);
    // 业务默认值
    manager->setEffectParam("reshape", "slim", 30);
    manager->setEffectParam("reshape", "eye", 0);

    auto makeup = std::make_shared<MakeupEffect>();
    manager->registerEffect("makeup", makeup, 28);
    manager->setEffectParam("makeup", "level", 0);  // 默认关闭

    // [基础调色]
    manager->registerEffect("contrast", std::make_shared<ContrastEffect>(), 30);

    // [滤镜]
    manager->registerEffect("filter", std::make_shared<FilterEffect>(), 40);

    // [背景模糊]
    manager->registerEffect("blur", std::make_shared<BackgroundBlurEffect>(), 50);

    // [锐化]
    manager->registerEffect("sharpen", std::make_shared<SharpenEffect>(), 60);

    auto sticker = std::make_shared<StickerEffect>();
    manager->registerEffect("sticker", sticker, 80);

    // [水印]
    manager->registerEffect("watermark", std::make_shared<WatermarkEffect>(), 70);

    // 3. 全局开关控制 (业务决定默认开启哪些)
    manager->setEffectEnabled("reshape", true);  // 默认开启整形模块(参数控制强度)
    manager->setEffectEnabled("smooth", true);
    manager->setEffectEnabled("makeup", true);    // 开启特效节点，强度由 level 控制
    manager->setEffectEnabled("sticker", false);  // 默认不显示贴纸
}
