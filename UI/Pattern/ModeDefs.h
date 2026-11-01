#ifndef MODEDEFS_H
#define MODEDEFS_H

#include <QString>
#include <QColor>

// 动画类型枚举
enum class CardAnimType
{
    Scale,  // 缩放模式：鼠标悬停时整体变大 (原效果)
    Lift,   // 上浮模式：鼠标悬停时位置上移 + 阴影加深 (不改变大小)
    None    // 无动画
};

// 图标类型枚举
enum class ModeIconType
{
    Scene,        // 场景制作
    Single,       // 单画面
    Multi,        // 多画面
    Interactive,  // 互动
    Live          // 直播
};

// 卡片数据配置结构体
struct ModeCardConfig
{
    QString title;
    QString description;
    QColor colorStart;
    QColor colorEnd;
    ModeIconType iconType;
    // 这里可以扩展更多配置
    QString imagePath;
};

#endif  // MODEDEFS_H
