#pragma once
#include <cstdint>
#include <QString>
#include <QMap>
#include <QColor>
#include <QMetaType>

// --- 1. 基础数据模型 (Pure Data Models) ---

// 场景分类枚举
enum class SceneCategoryType
{
    General,         // 通用
    LiveCourse,      // 直播课
    Gaming,          // 游戏
    PortraitMobile,  // 竖屏手机
    JustChatting     // 闲聊
};

// 变换数据 (位置、大小、旋转、层级)
// 修复：改回 Transform 以匹配 SceneItem 中的引用
struct Transform
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 300.0f;
    float height = 200.0f;
    float rotation = 0.0f;
    int zOrder = 0;
};

// 源类型定义
enum class SourceType
{
    Camera,
    ScreenCapture_WGC,
    Image,
    Video,
    Text,
    Monitor
};

// 素材/资源数据模型 (Asset Model)
// 描述一个可被添加到场景中的“资源”，不包含运行时状态
struct AssetModel
{
    QString uuid;             // 唯一ID
    QString name;             // 显示名称
    QString pathOrId;         // 物理路径 或 设备ID (如 "mon:1", "C:/img.png")
    QString thumbnailUrl;     // 缩略图
    SourceType type;          // 类型
    bool isCloud;             // 是否为云端资源
    QColor placeholderColor;  // 占位色

    QString localHash;       // 用于秒传的 MD5/SHA1
    QString cloudUrl;        // 云端真实下载地址
    int64_t fileSize = 0;    // 文件大小
    bool isVIPOnly = false;  // 权限标记
};
Q_DECLARE_METATYPE(AssetModel)

// 场景元数据模型 (Scene Metadata Model)
// 仅包含场景的描述信息，不包含具体Item
struct SceneMetaModel
{
    QString uuid;
    QString name;
    QString category;
    SceneCategoryType type;
    int width;
    int height;
    QString thumbnailPath;
};

// --- 2. 配置与枚举 ---

// 画布缩放策略
enum class CanvasScaleMode
{
    None,      // 不缩放
    Stretch,   // 拉伸
    AspectFit  // 保持比例
};

// 画布预设
struct CanvasConfig
{
    int width;
    int height;
    QString name;
    float aspectRatio;
};

static QMap<QString, CanvasConfig> const RESOLUTION_PRESETS = {
    {"横屏 16:9", {1920, 1080, "Landscape (1920x1080)", 16.0f / 9.0f}},
    {"横屏 4:3", {1280, 960, "Landscape (1280x960)", 4.0f / 3.0f}},
    {"竖屏 9:16", {1080, 1920, "Portrait (1080x1920)", 9.0f / 16.0f}}};

using TextureHandle = void*;

// --- 3. 全局设置 (Settings Model) ---
struct AppSettings
{
    int snapThreshold = 2;
    bool enableBoundary = true;
    bool enableSnap = true;
    bool hideUIOnInteracting = true;
    CanvasScaleMode scaleMode = CanvasScaleMode::Stretch;

    int handleSize = 8;
    QColor themeColor = QColor(0, 120, 215);
    QColor handleColor = Qt::white;
    QColor guideColor = QColor(255, 100, 100);
    QColor snapLineColor = QColor(0, 255, 255);

    static AppSettings& global()
    {
        static AppSettings s;
        return s;
    }
};
