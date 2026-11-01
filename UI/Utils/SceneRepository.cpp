#include "SceneRepository.h"
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

QString SceneRepository::getFilePath(QString const& uuid)
{
    QString dir = QCoreApplication::applicationDirPath() + "/scenes";
    QDir().mkpath(dir);
    return dir + "/" + uuid + ".json";
}

QString SceneRepository::getThumbnailPath(QString const& uuid)
{
    QString dirPath = QCoreApplication::applicationDirPath() + "/thumbs";
    QDir dir(dirPath);
    if (!dir.exists())
        dir.mkpath(".");
    return dirPath + "/" + uuid + ".jpg";
}

bool SceneRepository::save(std::shared_ptr<Scene> const& scene)
{
    if (!scene)
        return false;

    // 1. 获取纯数据 JSON (Scene 自身只负责生成数据，不负责写文件)
    QJsonObject json = scene->toJson();

    // 2. 写入文件
    QString path = getFilePath(scene->getUuid());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(QJsonDocument(json).toJson());
        file.close();
        // qDebug() << "[SceneRepository] Saved local file:" << path;
        return true;
    }
    qWarning() << "[SceneRepository] Failed to save file:" << path;
    return false;
}

void SceneRepository::deleteScene(std::shared_ptr<Scene> const& scene)
{
    if (!scene)
        return;
    QString uuid = scene->getUuid();

    QFile::remove(getFilePath(uuid));
    QFile::remove(getThumbnailPath(uuid));
    qDebug() << "[SceneRepository] Deleted local files for:" << uuid;
}
