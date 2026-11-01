#ifndef RESOURCESERVICE_H
#define RESOURCESERVICE_H

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>

class ResourceService : public QObject
{
    Q_OBJECT
public:
    static ResourceService* instance();

    // ================= 1. 场景分类 (Scene Class) =================
    // [Modified] 移除 authList
    void addSceneClass(QString const& name);
    // [Modified] 移除 authList
    void updateSceneClass(QString const& id, QString const& name);

    void deleteSceneClass(QString const& id);
    void getSceneClassDetail(QString const& id);
    void getSceneClassList(int page, int size);
    void exportSceneClassList(QVariantMap const& params);

    // ================= 2. 场景 (Scene) =================
    // [Modified] 移除 authList
    void addScene(QString const& name,
                  QString const& sceneClassId,
                  QString const& content,
                  QString const& remark);
    // [Modified] 移除 authList
    void updateScene(QString const& id,
                     QString const& name,
                     QString const& content,
                     QString const& remark);

    void deleteScene(QString const& id);
    void getSceneDetail(QString const& id);
    void getSceneList(int page, int size, QString const& name = "", QString const& typeId = "");
    void exportSceneList(QVariantMap const& params);
    void uploadSceneFile(QString const& filePath, QString const& sceneClassId);

    // ================= 3. 素材分组 (Material Group) =================
    // [Modified] 移除 authList
    void addMaterialGroup(QString const& name, QString const& parentId = "");
    // [Modified] 移除 authList
    void updateMaterialGroup(QString const& id, QString const& name);

    void deleteMaterialGroup(QString const& id);
    void getMaterialGroupDetail(QString const& id);
    void getMaterialGroupList(int page, int size);
    void exportMaterialGroupList(QVariantMap const& params);

    // ================= 4. 素材 (Material) =================
    void getMaterialList(int page, int size, QString const& groupId, QString const& name = "");
    void getMaterialDetail(QString const& id);
    void updateMaterial(QString const& id, QString const& name);
    void deleteMaterial(QString const& id);
    void exportMaterialList(QVariantMap const& params);
    void uploadMaterialFile(QString const& filePath, QString const& groupId, int mediaType);

    // ================= 5. [New] 场景封面 (Layout Cover) =================
    /**
     * @brief 上传场景封面
     * @param filePath 本地图片路径
     * @note 对应 POST /resource/media/layoutCover/upload, 参数 mediaType=0 (图片)
     */
    void uploadLayoutCover(QString const& filePath);

    // ================= 6. [New] 个人偏好 (Layout Preference) =================
    /**
     * @brief 获取当前用户的场景偏好
     * @param layoutId 场景ID
     * @note 对应 GET /resource/layoutPreference/getMine
     */
    void fetchLayoutPreference(long long layoutId);

    /**
     * @brief 更新当前用户的场景偏好
     * @param layoutId 场景ID
     * @param content 偏好内容 (JSON字符串)
     * @note 对应 POST /resource/layoutPreference/setMine
     */
    void updateLayoutPreference(long long layoutId, QString const& content);

    // ================= 7. [New] 直播文件上传 =================

    /**
     * @brief 上传直播录制文件 (成品视频)
     * @param filePath 本地文件路径
     * @param roomId 直播间ID
     * @param mediaType 文件类型 (默认为0，具体视业务而定)
     * @note 对应 POST /resource/media/liveRecord/upload
     */
    void uploadLiveRecord(QString const& filePath, long long roomId, int mediaType = 0);

    /**
     * @brief 上传直播过程文件 (PCM音频/YUV视频)
     * @param filePath 本地文件路径
     * @param roomId 直播间ID
     * @param mediaType 1=音频(PCM), 2=视频(YUV)
     * @note 对应 POST /resource/media/live/upload
     */
    void uploadLiveFile(QString const& filePath, long long roomId, int mediaType);

    void uploadLiveFile(QByteArray const& data,
                        QString const& fileName,
                        long long roomId,
                        int mediaType);

signals:
    // opType: 操作标识 (如 scene_list, scene_add)
    void operationFinished(QString const& opType, bool success, QString const& msg);
    // dataReceived 会携带完整的 API 响应
    void dataReceived(QString const& opType, QJsonObject const& data);

    // [New] 封面上传完成
    void coverUploaded(bool success, QString const& url, QString const& msg);

    // [New] 偏好获取完成
    void preferenceFetched(bool success,
                           long long layoutId,
                           QString const& content,
                           QString const& msg);

    // [New] 偏好保存完成
    void preferenceSaved(bool success, QString const& msg);

    void materialDeleted(bool success, QString const& id, QString const& msg);

    // [New] 直播录制文件上传完成
    void liveRecordUploaded(bool success, QString const& url, QString const& msg);

    // [New] 直播过程文件(PCM/YUV)上传完成
    void liveFileUploaded(bool success, QString const& url, int mediaType, QString const& msg);

private slots:
    void onApiResponse(QString const& url,
                       bool success,
                       QJsonObject const& response,
                       QString const& msg);

private:
    explicit ResourceService(QObject* parent = nullptr);
    static ResourceService* m_instance;
};

#endif  // RESOURCESERVICE_H
