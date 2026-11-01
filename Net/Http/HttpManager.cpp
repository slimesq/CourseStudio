#include "HttpManager.h"
#include "UserContext.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QUrlQuery>
#include <QHttpPart>
#include <QFileInfo>
#include <QUuid>
#include <QDebug>
#include <QDateTime>       // For timestamps
#include <QStandardPaths>  // To find a good place for logs
#include <QDir>
#include <QBuffer>
#include <QCoreApplication>

HttpManager* HttpManager::m_instance = nullptr;
HttpManager* HttpManager::instance()
{
    if (!m_instance)
        m_instance = new HttpManager();
    return m_instance;
}

HttpManager::HttpManager(QObject* parent) : QObject(parent)
{
    m_manager = new QNetworkAccessManager(this);
}

// 统一设置 Headers
QNetworkRequest HttpManager::prepareRequest(QString const& urlStr, QString const& contentType)
{
    QNetworkRequest request((QUrl(urlStr)));
    if (!contentType.isEmpty())
    {
        request.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    }

    // 1. Authorization
    QString token = UserContext::instance().token();
    if (!token.isEmpty())
    {
        request.setRawHeader("Authorization", token.toUtf8());
    }

    // 2. ClientId
    QString clientId = UserContext::instance().clientId();
    if (!clientId.isEmpty())
    {
        request.setRawHeader("clientid", clientId.toUtf8());
    }

    return request;
}

// [Helper] Append log to file
void appendLog(QString const& text)
{
    QString logPath = QCoreApplication::applicationDirPath() + "/network_log.txt";
    QFile file(logPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        QTextStream out(&file);
        // Use UTF-8 to support Chinese characters
        out.setEncoding(QStringConverter::Utf8);
        out << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz") << " " << text
            << "\n";
        file.close();
    }
}

void HttpManager::handleReply(QNetworkReply* reply, QString const& originalUrl)
{
    connect(reply, &QNetworkReply::finished, this, [=]() {
        QJsonObject responseObj;
        bool success = false;
        QString errorMsg;

        // 打印 HTTP 状态码，方便调试
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "=== RESPONSE [" << statusCode << "] URL:" << originalUrl << "===";

        QString logEntry =
            QString("=== RESPONSE [%1] URL: %2 ===\n").arg(statusCode).arg(originalUrl);

        if (reply->error() == QNetworkReply::NoError)
        {
            QByteArray bytes = reply->readAll();
            QString responseBody = QString::fromUtf8(bytes);

            // Log the body
            logEntry += "Body: " + responseBody + "\n";
            // Also print to console for immediate visibility if short
            // qDebug() << "Body:" << responseBody;

            // ================= [手术刀日志 START] =================
            // 强制在控制台打印完整的 JSON 响应，即使它很长
            qDebug().noquote() << "--------------------------------------------------";
            qDebug().noquote() << "[HttpManager] 🟢 RESPONSE 200 OK";
            qDebug().noquote() << "[URL] :" << originalUrl;
            qDebug().noquote() << "[BODY]:" << responseBody;
            qDebug().noquote() << "--------------------------------------------------";
            // ================= [手术刀日志 END] =================

            QJsonDocument doc = QJsonDocument::fromJson(bytes);
            responseObj = doc.object();
            success = true;
        }
        else
        {
            errorMsg = reply->errorString();
            QByteArray errorBytes = reply->readAll();
            QString errorBody = QString::fromUtf8(errorBytes);

            logEntry += "Error: " + errorMsg + "\n";
            logEntry += "Error Body: " + errorBody + "\n";

            qDebug() << "Response Error Body:" << errorBody;

            if (!errorBytes.isEmpty())
            {
                QJsonDocument errDoc = QJsonDocument::fromJson(errorBytes);
                if (!errDoc.isNull())
                {
                    responseObj = errDoc.object();
                    if (responseObj.contains("msg"))
                    {
                        errorMsg = responseObj["msg"].toString();
                    }
                }
                else
                {
                    errorMsg += " | Server: " + errorBody;
                }
            }
            success = false;
        }

        logEntry += "========================================\n";
        appendLog(logEntry);  // Write to file

        emit apiResponse(originalUrl, success, responseObj, errorMsg);
        emit requestFinished(success, responseObj, errorMsg);
        reply->deleteLater();
    });
}

// POST (JSON)
void HttpManager::post(QString const& url, QJsonObject const& data)
{
    QNetworkRequest request = prepareRequest(url);
    QString jsonStr = QJsonDocument(data).toJson(QJsonDocument::Compact);

    // Log Request
    appendLog(QString(">>> POST %1\nData: %2").arg(url).arg(jsonStr));

    QNetworkReply* reply = m_manager->post(request, jsonStr.toUtf8());
    handleReply(reply, url);
}

// PUT (JSON)
void HttpManager::put(QString const& url, QJsonObject const& data)
{
    QNetworkRequest request = prepareRequest(url);
    QString jsonStr = QJsonDocument(data).toJson(QJsonDocument::Compact);

    // Log Request
    appendLog(QString(">>> PUT %1\nData: %2").arg(url).arg(jsonStr));

    QNetworkReply* reply = m_manager->put(request, jsonStr.toUtf8());
    handleReply(reply, url);
}

// GET (Params)
// void HttpManager::get(const QString &url, const QVariantMap &params) {
//     QUrl qUrl(url);
//     if (!params.isEmpty()) {
//         QUrlQuery query;
//         for(auto it = params.begin(); it != params.end(); ++it) {
//             query.addQueryItem(it.key(), it.value().toString());
//         }
//         qUrl.setQuery(query);
//     }

//     // Log Request
//     appendLog(QString(">>> GET %1").arg(qUrl.toString()));

//     QNetworkRequest request = prepareRequest(qUrl.toString(), "");
//     QNetworkReply *reply = m_manager->get(request);
//     handleReply(reply, url);
// }
void HttpManager::get(QString const& url, QVariantMap const& params)
{
    QUrl qUrl(url);
    if (!params.isEmpty())
    {
        QUrlQuery query;
        for (auto it = params.begin(); it != params.end(); ++it)
        {
            query.addQueryItem(it.key(), it.value().toString());
        }
        qUrl.setQuery(query);
    }

    // 准备完整的请求日志
    QString logContent;

    // 1. 打印请求方法和URL
    logContent += QString(">>> GET %1\n").arg(qUrl.toString());

    // 2. 构建请求并获取请求头信息
    QNetworkRequest request = prepareRequest(qUrl.toString(), "");

    // 3. 打印所有请求头
    logContent += ">>> Request Headers:\n";
    QList<QByteArray> headerKeys = request.rawHeaderList();
    foreach (QByteArray const& key, headerKeys)
    {
        logContent +=
            QString(">>>   %1: %2\n").arg(QString(key)).arg(QString(request.rawHeader(key)));
    }

    // 4. 打印请求参数（可选，URL中已有，但单独打印更清晰）
    if (!params.isEmpty())
    {
        logContent += ">>> Request Params:\n";
        for (auto it = params.begin(); it != params.end(); ++it)
        {
            logContent += QString(">>>   %1: %2\n").arg(it.key()).arg(it.value().toString());
        }
    }

    // 输出完整日志
    appendLog(logContent);

    QNetworkReply* reply = m_manager->get(request);
    handleReply(reply, url);
}

// DELETE (Params)
void HttpManager::del(QString const& url, QVariantMap const& params)
{
    QUrl qUrl(url);
    if (!params.isEmpty())
    {
        QUrlQuery query;
        for (auto it = params.begin(); it != params.end(); ++it)
        {
            query.addQueryItem(it.key(), it.value().toString());
        }
        qUrl.setQuery(query);
    }

    // Log Request
    appendLog(QString(">>> DELETE %1").arg(qUrl.toString()));

    QNetworkRequest request = prepareRequest(qUrl.toString(), "");
    QNetworkReply* reply = m_manager->deleteResource(request);
    handleReply(reply, url);
}

// Upload (Multipart/Form-Data) - 调试增强版
void HttpManager::upload(QString const& url,
                         QString const& filePath,
                         QMap<QString, QString> const& params)
{
    QHttpMultiPart* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    // 1. 生成纯净的 Boundary (去除花括号 {})
    QString uuid = QUuid::createUuid().toString();
    uuid.remove('{').remove('}').remove('-');
    QString boundary = "boundary_" + uuid;

    multiPart->setBoundary(boundary.toLatin1());

    // 2. 添加普通文本参数
    QString paramLog = "";
    for (auto it = params.begin(); it != params.end(); ++it)
    {
        QHttpPart textPart;
        textPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                           QString("form-data; name=\"%1\"").arg(it.key()));
        textPart.setBody(it.value().toUtf8());
        multiPart->append(textPart);
        paramLog += it.key() + "=" + it.value() + "; ";
    }

    // 3. 添加文件
    QFile* file = new QFile(filePath);
    if (!file->open(QIODevice::ReadOnly))
    {
        delete file;
        delete multiPart;
        qDebug() << "Error: File open failed:" << filePath;
        emit apiResponse(url, false, {}, "File open failed: " + filePath);
        return;
    }

    QHttpPart filePart;
    QFileInfo fileInfo(filePath);
    QString disposition =
        QString("form-data; name=\"file\"; filename=\"%1\"").arg(fileInfo.fileName());
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader, disposition);

    // 自动判断 MimeType
    QString mimeType = "application/octet-stream";
    QString suffix = fileInfo.suffix().toLower();
    if (suffix == "png")
        mimeType = "image/png";
    else if (suffix == "jpg" || suffix == "jpeg")
        mimeType = "image/jpeg";
    else if (suffix == "mp4")
        mimeType = "video/mp4";
    else if (suffix == "txt")
        mimeType = "text/plain";

    filePart.setHeader(QNetworkRequest::ContentTypeHeader, mimeType);
    filePart.setBodyDevice(file);
    file->setParent(multiPart);
    multiPart->append(filePart);

    // 4. 准备请求头
    QNetworkRequest request = prepareRequest(url, "");
    QString contentTypeHeader = "multipart/form-data; boundary=" + boundary;
    request.setHeader(QNetworkRequest::ContentTypeHeader, contentTypeHeader);

    // Log Upload
    appendLog(QString(">>> UPLOAD %1\nFile: %2\nParams: %3").arg(url).arg(filePath).arg(paramLog));

    QNetworkReply* reply = m_manager->post(request, multiPart);
    multiPart->setParent(reply);

    connect(reply, &QNetworkReply::uploadProgress, this, &HttpManager::uploadProgress);
    handleReply(reply, url);
}

void HttpManager::upload(QString const& url,
                         QByteArray const& data,
                         QString const& virtualFileName,
                         QMap<QString, QString> const& params)
{
    QHttpMultiPart* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    // 1. Boundary
    QString uuid = QUuid::createUuid().toString().remove('{').remove('}').remove('-');
    QString boundary = "boundary_" + uuid;
    multiPart->setBoundary(boundary.toLatin1());

    // 2. 文本参数
    QString paramLog = "";
    for (auto it = params.begin(); it != params.end(); ++it)
    {
        QHttpPart textPart;
        textPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                           QString("form-data; name=\"%1\"").arg(it.key()));
        textPart.setBody(it.value().toUtf8());
        multiPart->append(textPart);
        paramLog += it.key() + "=" + it.value() + "; ";
    }

    // 3. 文件部分 (使用 QBuffer)
    QHttpPart filePart;
    QString disposition = QString("form-data; name=\"file\"; filename=\"%1\"").arg(virtualFileName);
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader, disposition);

    // 自动判断 MimeType
    QString mimeType = "application/octet-stream";
    if (virtualFileName.endsWith(".yuv"))
        mimeType = "video/x-raw-yuv";
    else if (virtualFileName.endsWith(".pcm"))
        mimeType = "audio/raw";

    filePart.setHeader(QNetworkRequest::ContentTypeHeader, mimeType);

    // 关键点：将 QByteArray 设为 Body
    filePart.setBody(data);

    multiPart->append(filePart);

    // 4. 发送请求
    QNetworkRequest request = prepareRequest(url, "");
    QString contentTypeHeader = "multipart/form-data; boundary=" + boundary;
    request.setHeader(QNetworkRequest::ContentTypeHeader, contentTypeHeader);

    appendLog(QString(">>> MEM-UPLOAD %1\nFile: %2 (%3 bytes)\nParams: %4")
                  .arg(url)
                  .arg(virtualFileName)
                  .arg(data.size())
                  .arg(paramLog));

    QNetworkReply* reply = m_manager->post(request, multiPart);
    multiPart->setParent(reply);  // 让 reply 管理 multiPart 的生命周期

    connect(reply, &QNetworkReply::uploadProgress, this, &HttpManager::uploadProgress);
    handleReply(reply, url);
}

// Download Stream
QNetworkReply* HttpManager::downloadStream(QString const& url,
                                           QString const& savePath,
                                           int64_t startByte)
{
    QNetworkRequest request = prepareRequest(url, "");
    if (startByte > 0)
    {
        request.setRawHeader("Range", QString("bytes=%1-").arg(startByte).toUtf8());
    }

    appendLog(QString(">>> DOWNLOAD %1 -> %2").arg(url).arg(savePath));

    QNetworkReply* reply = m_manager->get(request);
    QFile* file = new QFile(savePath);

    if (!file->open(QIODevice::WriteOnly | QIODevice::Append))
    {
        delete file;
        return reply;
    }

    connect(reply, &QNetworkReply::readyRead, this, [reply, file]() {
        if (file->isOpen())
            file->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::downloadProgress, this, &HttpManager::downloadProgress);
    connect(reply, &QNetworkReply::finished, this, [reply, file]() {
        file->close();
        file->deleteLater();
        reply->deleteLater();
    });
    return reply;
}
