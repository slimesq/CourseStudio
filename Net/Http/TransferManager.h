#pragma once
#include <QObject>
#include <QQueue>
#include <QCryptographicHash>
#include <QThread>
#include <QFileInfo>
#include <QDebug>
#include "Types.h"
#include "UserContext.h"
#include "ResourceService.h"

// 定义上传任务结构
struct UploadTask
{
    AssetModel asset;
    QString groupId;
};

class TransferManager : public QObject
{
    Q_OBJECT
public:
    static TransferManager& instance()
    {
        static TransferManager s;
        return s;
    }

    // 提交上传任务
    void enqueueUpload(AssetModel asset, QString groupId = "")
    {
        UploadTask task;
        task.asset = asset;
        task.groupId = groupId;
        m_uploadQueue.enqueue(task);
        processNextUpload();
    }

signals:
    // [New] 关键信号：通知外部某个本地文件已经变成了云端 URL
    void transferFinished(QString const& localPath, QString const& cloudUrl);

private slots:
    void processNextUpload()
    {
        if (m_uploadQueue.isEmpty() || m_isUploading)
            return;

        m_isUploading = true;
        m_currentTask = m_uploadQueue.dequeue();  // 记录当前任务

        // 异步计算 Hash
        QThread::create([this]() {
            QFile file(m_currentTask.asset.pathOrId);
            if (file.open(QIODevice::ReadOnly))
            {
                QCryptographicHash hash(QCryptographicHash::Md5);
                hash.addData(&file);
                QString md5 = hash.result().toHex();
                QMetaObject::invokeMethod(this, "startUpload", Q_ARG(QString, md5));
            }
            else
            {
                QMetaObject::invokeMethod(this, "onUploadFailed");
            }
        })->start();
    }

    void startUpload(QString hash)
    {
        // 省略秒传检测，直接上传
        qDebug() << "[TransferManager] Starting upload for:" << m_currentTask.asset.name;

        QFileInfo fileInfo(m_currentTask.asset.pathOrId);
        QString suffix = fileInfo.suffix().toLower();
        int mediaType = 2;  // 默认图片
        if (suffix == "mp4" || suffix == "avi" || suffix == "mov" || suffix == "mkv")
        {
            mediaType = 1;
        }

        ResourceService::instance()->uploadMaterialFile(
            m_currentTask.asset.pathOrId, m_currentTask.groupId, mediaType);
    }

    void onUploadFailed()
    {
        m_isUploading = false;
        processNextUpload();
    }

private:
    TransferManager()
    {
        // 1. 监听操作完成 (用于状态复位)
        connect(ResourceService::instance(),
                &ResourceService::operationFinished,
                this,
                [this](QString const& op, bool success, QString const& msg) {
                    if (op.contains("upload"))
                    {
                        m_isUploading = false;
                        if (!success)
                        {
                            qWarning() << "[TransferManager] Upload failed:" << msg;
                        }
                        processNextUpload();
                    }
                });

        // 2. [New] 监听数据返回 (用于获取 Cloud URL)
        connect(ResourceService::instance(),
                &ResourceService::dataReceived,
                this,
                [this](QString const& op, QJsonObject const& data) {
                    if (op.contains("upload") || op == "material_op")
                    {
                        // 检查是否包含上传成功的 URL
                        if (data.contains("mediaUrl") && !m_currentTask.asset.pathOrId.isEmpty())
                        {
                            QString cloudUrl = data["mediaUrl"].toString();
                            qDebug() << "[TransferManager] Success:" << m_currentTask.asset.pathOrId
                                     << "->" << cloudUrl;

                            // 发出信号，通知 SceneManager 进行 ID 替换
                            emit transferFinished(m_currentTask.asset.pathOrId, cloudUrl);
                        }
                    }
                });
    }

    QQueue<UploadTask> m_uploadQueue;
    UploadTask m_currentTask;  // [New] 保存当前正在上传的任务状态
    bool m_isUploading = false;
};
