#ifndef HTTPMANAGER_H
#define HTTPMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QFile>
#include <QJsonObject>
#include <QMap>

class HttpManager : public QObject
{
    Q_OBJECT
public:
    static HttpManager* instance();

    // 通用 REST 接口
    void post(QString const& url, QJsonObject const& data);
    void put(QString const& url, QJsonObject const& data);
    void get(QString const& url, QVariantMap const& params = {});
    void del(QString const& url, QVariantMap const& params = {});

    // 文件上传接口 (Form-Data)
    // filePath: 本地文件绝对路径
    // params: 需要随文件一起提交的文本字段 (如 groupid)
    void upload(QString const& url, QString const& filePath, QMap<QString, QString> const& params);
    void upload(QString const& url,
                QByteArray const& data,
                QString const& virtualFileName,
                QMap<QString, QString> const& params);

    // 大文件下载接口
    QNetworkReply* downloadStream(QString const& url,
                                  QString const& savePath,
                                  int64_t startByte = 0);

signals:
    // 原始信号 (兼容旧代码)
    void requestFinished(bool success, QJsonObject const& response, QString const& errorMsg);

    // **新信号**：带 URL 参数，用于 ResourceService 区分是哪个接口返回的数据
    void apiResponse(QString const& url,
                     bool success,
                     QJsonObject const& response,
                     QString const& msg);

    void uploadProgress(qint64 sent, qint64 total);
    void downloadProgress(qint64 received, qint64 total);

private:
    explicit HttpManager(QObject* parent = nullptr);
    QNetworkAccessManager* m_manager;
    static HttpManager* m_instance;

    QNetworkRequest prepareRequest(QString const& url,
                                   QString const& contentType = "application/json");
    void handleReply(QNetworkReply* reply, QString const& originalUrl);
};

#endif  // HTTPMANAGER_H
