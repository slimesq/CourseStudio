#include "LiveCourseModel.h"
#include <QDateTime>
#include <QDebug>

CourseEntity CourseEntity::fromJson(QJsonObject const& json, int index)
{
    CourseEntity entity;

    // 1. 基础字段解析
    entity.roomId = json["roomId"].toVariant().toLongLong();
    entity.title = json["title"].toString("未命名直播间");

    // 2. 时间处理 (保留原逻辑：如果没有 startTime，则根据 index 生成伪数据)
    if (json.contains("startTime"))
    {
        entity.timeStr = json["startTime"].toString();
    }
    else
    {
        // Fallback: 生成未来几天的时间
        QString rawTime = QDateTime::currentDateTime().addDays(index).toString("MM月dd日 HH:mm");
        entity.timeStr = rawTime + " 直播";
    }

    // 3. 讲师信息 (保留原逻辑：如果没有 lecturer，则生成 A/B 伪数据)
    if (json.contains("lecturer"))
    {
        entity.lecturerName = json["lecturer"].toString();
    }
    else
    {
        entity.lecturerName = QString("讲师 %1 / 助教 %2")
                                  .arg(QChar('A' + (index % 26)))
                                  .arg(QChar('B' + (index % 26)));
    }

    // 4. 描述信息
    entity.description = json["desc"].toString(
        "本课程深入讲解C++核心技术，包括内存管理、多线程并发、网络编程等实战内容。");

    // 5. 封面 URL 处理
    entity.coverUrl = json["coverUrl"].toString();

    // 注意：原逻辑中的 AssetCacheManager 调用被移除。
    // 原因：Model 应该是被动的纯数据，不应主动触发网络下载或磁盘 IO。
    // 缓存查找逻辑将在 Controller 或 ViewModel 中进行，填充 localCoverPath。
    if (entity.coverUrl.isEmpty())
    {
        // 如果没有 URL，指向默认资源 (这里只存相对路径或资源 ID，UI 负责映射)
        // 对应原逻辑: ":/UI/QSS/srs/%1.png"
        entity.localCoverPath = QString(":/UI/QSS/srs/%1.png").arg((index % 3) + 1);
        entity.isNetworkImage = false;
    }
    else
    {
        entity.isNetworkImage = true;
    }

    return entity;
}

CourseEntity CourseEntity::createLoading()
{
    CourseEntity entity;
    entity.roomId = -1;  // 特殊 ID
    entity.title = "加载中...";
    entity.timeStr = "--";
    entity.lecturerName = "--";
    entity.description = "正在获取课程信息...";
    entity.isNetworkImage = false;
    return entity;
}

CourseEntity CourseEntity::createEmpty(QString const& msg)
{
    CourseEntity entity;
    entity.roomId = 0;
    entity.title = msg;
    entity.timeStr = "";
    entity.lecturerName = "";
    entity.description = "";
    entity.isNetworkImage = false;
    return entity;
}
