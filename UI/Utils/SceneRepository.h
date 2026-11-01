#ifndef SCENEREPOSITORY_H
#define SCENEREPOSITORY_H

#include <QString>
#include <memory>
#include "Scene.h"

// 负责场景数据的持久化存储（本地文件 IO）
class SceneRepository
{
public:
    // 保存场景到本地 JSON 文件
    static bool save(std::shared_ptr<Scene> const& scene);

    // 删除场景对应的本地文件和缩略图
    static void deleteScene(std::shared_ptr<Scene> const& scene);

    // 获取缩略图绝对路径
    static QString getThumbnailPath(QString const& uuid);

    // 获取场景文件绝对路径
    static QString getFilePath(QString const& uuid);

private:
    // 纯静态工具类，禁用构造
    SceneRepository() = delete;
};

#endif  // SCENEREPOSITORY_H
