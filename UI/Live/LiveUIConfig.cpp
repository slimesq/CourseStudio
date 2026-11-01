#include "LiveUIConfig.h"
#include <QDebug>
#include <QtMath>

LiveUIConfig& LiveUIConfig::instance()
{
    static LiveUIConfig s_instance;
    return s_instance;
}

LiveUIConfig::LiveUIConfig()
{
    // 初始化逻辑：默认值已在头文件结构体初始化中定义
    // 这些默认值严格匹配原 LiveCourseSelectWidget.cpp 中的硬编码值
    // 例如: scrollDuration = 550, OutQuint, baseWindowWidth = 1200 等
}

void LiveUIConfig::setUiScale(double scale)
{
    QMutexLocker locker(&m_mutex);
    // 增加简单的去抖动或阈值判断，防止微小浮点误差导致重绘
    if (qAbs(m_uiScale - scale) > 0.001)
    {
        // 限制缩放范围，防止 UI 崩溃 (参考原代码逻辑)
        double clampedScale = qBound(0.8, scale, 1.8);

        // 特殊处理：接近 1.0 时强制为 1.0，避免抗锯齿模糊
        if (qAbs(clampedScale - 1.0) < 0.01)
        {
            clampedScale = 1.0;
        }

        if (qAbs(m_uiScale - clampedScale) > 0.001)
        {
            m_uiScale = clampedScale;
            emit configChanged();
        }
    }
}

int LiveUIConfig::scaled(int baseValue) const
{
    // [优化] 使用 qRound 进行四舍五入，减少线性缩放带来的像素对齐误差
    // 相比 static_cast<int> 的直接截断，这能提供更稳定的布局计算结果
    return qRound(baseValue * m_uiScale);
}

double LiveUIConfig::scaled(double baseValue) const
{
    return baseValue * m_uiScale;
}

void LiveUIConfig::loadFromJson(QJsonObject const& json)
{
    QMutexLocker locker(&m_mutex);
    bool changed = false;

    // 1. 加载动画配置
    if (json.contains("anim"))
    {
        QJsonObject j = json["anim"].toObject();
        if (j.contains("scrollDuration"))
        {
            m_anim.scrollDuration = j["scrollDuration"].toInt();
            changed = true;
        }
        if (j.contains("throttleInterval"))
        {
            m_anim.throttleInterval = j["throttleInterval"].toInt();
            changed = true;
        }
    }

    // 2. 加载布局配置
    if (json.contains("layout"))
    {
        QJsonObject j = json["layout"].toObject();
        if (j.contains("cardGap"))
        {
            m_layout.cardGap = j["cardGap"].toInt();
            changed = true;
        }
        if (j.contains("widthRatio"))
        {
            m_layout.widthRatio = j["widthRatio"].toDouble();
            changed = true;
        }
        if (j.contains("centerHeightFactor"))
        {
            m_layout.centerHeightFactor = j["centerHeightFactor"].toDouble();
            changed = true;
        }
    }

    // 3. 加载颜色 (示例：仅展示部分)
    if (json.contains("colors"))
    {
        QJsonObject j = json["colors"].toObject();
        auto loadColor = [](QJsonObject const& o, QString const& k, QColor& target) {
            if (o.contains(k))
                target = QColor(o[k].toString());
        };
        loadColor(j, "btnPrimaryStart", m_colors.btnPrimaryStart);
        loadColor(j, "btnPrimaryEnd", m_colors.btnPrimaryEnd);
        loadColor(j, "textTitle", m_colors.textTitle);
        changed = true;
    }

    if (changed)
    {
        emit configChanged();
    }
}

QJsonObject LiveUIConfig::saveToJson() const
{
    QMutexLocker locker(&m_mutex);
    QJsonObject root;

    // 序列化动画
    QJsonObject jAnim;
    jAnim["scrollDuration"] = m_anim.scrollDuration;
    jAnim["throttleInterval"] = m_anim.throttleInterval;
    root["anim"] = jAnim;

    // 序列化布局
    QJsonObject jLayout;
    jLayout["cardGap"] = m_layout.cardGap;
    jLayout["widthRatio"] = m_layout.widthRatio;
    jLayout["centerHeightFactor"] = m_layout.centerHeightFactor;
    root["layout"] = jLayout;

    // 序列化颜色
    QJsonObject jColors;
    jColors["btnPrimaryStart"] = m_colors.btnPrimaryStart.name();
    jColors["btnPrimaryEnd"] = m_colors.btnPrimaryEnd.name();
    jColors["textTitle"] = m_colors.textTitle.name();
    root["colors"] = jColors;

    return root;
}
