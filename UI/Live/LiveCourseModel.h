#ifndef LIVECOURSEMODEL_H
#define LIVECOURSEMODEL_H

#include <QString>
#include <QJsonObject>
#include <QVector>
#include <QMetaType>

/**
 * @brief 课程实体数据模型
 * 纯数据结构，不包含任何 UI 逻辑 (如 QColor, QFont)
 * 对应 MVVM 中的 Model 层
 */
struct CourseEntity
{
    long long roomId = 0;

    // 基础信息
    QString title;
    QString subTitle;     // 预留，对应原 tag 或副标题
    QString description;  // 对应 detailedDesc

    // 讲师与时间
    QString lecturerName;
    QString timeStr;  // 格式化后的时间字符串 (如 "02月10日 20:00 直播")

    // 封面资源
    QString coverUrl;             // 网络地址
    QString localCoverPath;       // 本地缓存路径 (由 Controller 填充)
    bool isNetworkImage = false;  // 标记是否需要网络加载

    // 默认构造
    CourseEntity() = default;

    // 数据有效性检查
    bool isValid() const
    {
        return roomId > 0;
    }

    // ==========================================
    // 静态工厂方法 (Factory Methods)
    // ==========================================

    /**
     * @brief 从网络 JSON 数据构建实体
     * 封装了原 Widget 中的字段解析和默认值兜底逻辑
     * @param json 网络返回的单条 Room 数据
     * @param index 数据索引，用于生成默认占位文案 (A/B Test)
     * @return 构造好的 CourseEntity
     */
    static CourseEntity fromJson(QJsonObject const& json, int index = 0);

    /**
     * @brief 创建一个加载中的占位实体
     */
    static CourseEntity createLoading();

    /**
     * @brief 创建一个空实体/错误实体
     */
    static CourseEntity createEmpty(QString const& msg = "暂无数据");
};

// 注册为 Qt 元类型，以便在信号槽中传递
Q_DECLARE_METATYPE(CourseEntity)

// 定义列表类型方便使用
typedef QVector<CourseEntity> CourseList;

#endif  // LIVECOURSEMODEL_H
