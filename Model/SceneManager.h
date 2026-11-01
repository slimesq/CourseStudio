#pragma once
#include <QObject>
#include <memory>
#include <vector>
#include <QMap>
#include <QSet>
#include <QTimer>
#include "Scene.h"
#include "ResourceService.h"
#include "AssetCacheManager.h"

// 权限类型枚举
enum class SceneAuthType
{
    Query,  // 查看权限
    Add,    // 添加权限
    Edit,   // 编辑权限
    Delete  // 删除权限
};

class SceneManager : public QObject
{
    Q_OBJECT
public:
    static SceneManager& instance();

    void loadIndex();

    // 获取所有分类名称
    QStringList getUniqueCategories() const;

    // 根据权限获取分类列表
    QStringList getCategoriesWithAuth(SceneAuthType authType);

    void createCategory(QString const& name);

    // [Modified] 增加 coverPath 参数
    std::shared_ptr<Scene> createScene(std::string const& name,
                                       std::string const& category,
                                       int width,
                                       int height,
                                       QString const& coverPath = "");
    std::shared_ptr<Scene> createScene(std::string const& name,
                                       std::string const& category,
                                       QString const& coverPath = "");
    std::shared_ptr<Scene> createScene(std::string const& name,
                                       std::string const& category,
                                       SceneCategoryType type,
                                       QString const& coverPath = "");

    void saveScene(std::shared_ptr<Scene> scene);
    void removeScene(std::shared_ptr<Scene> scene);
    void addAssetToScene(std::shared_ptr<Scene> scene, AssetModel const& asset);

    void reorderScenes(std::vector<std::string> const& newOrderUuids);

    std::vector<std::shared_ptr<Scene>> getAllScenes() const
    {
        return m_scenes;
    }

    std::shared_ptr<Scene> getSceneByUuid(QString const& uuid) const;

    long long getCategoryIdByName(QString const& name);

    bool checkAuth(SceneAuthType type, QString const& category, QString const& sceneUuid = "");
    void grantAuth(QString const& category, SceneAuthType type);

    // 参数 type: 想要检查的权限类型 (Edit, Delete)
    bool checkSceneAuth(std::shared_ptr<Scene> scene, SceneAuthType type);

    // [New] 获取当前登录用户ID (用于对比 creatorId)
    long long getCurrentUserId() const;

signals:
    void categoriesUpdated();
    void sceneLoaded(Scene* scene);
    void sceneListUpdated();
    void cloudSyncFinished(QString const& uuid, bool success);

private slots:
    void onCloudCategoriesFetched(QJsonObject const& data);
    void onCloudScenesFetched(QJsonObject const& data);
    void onCloudSceneDetailFetched(QJsonObject const& data);
    void onAssetDownloaded(QString const& url, QString const& localPath);
    void onServiceOpFinished(QString const& op, bool success, QString const& msg);
    void onMaterialUploaded(QString const& localPath, QString const& cloudUrl);

    // [New] 封面上传完成回调
    void onCoverUploaded(bool success, QString const& url, QString const& msg);

    void performBatchCloudSave();
    void onCloudAssetsReady();

private:
    explicit SceneManager(QObject* parent = nullptr);
    void updateLocalSceneFile(QString const& uuid, QString const& contentJson);
    void bindAutoSync(std::shared_ptr<Scene> scene);
    void triggerDeferredSave(std::shared_ptr<Scene> scene);

    // [New] 缩略图上传逻辑 (生成截图)
    void uploadThumbnail(std::shared_ptr<Scene> scene);

    // [New] 自定义封面上传逻辑 (使用用户文件)
    void uploadCustomCover(std::shared_ptr<Scene> scene, QString const& filePath);

    std::vector<std::shared_ptr<Scene>> m_scenes;
    QStringList m_categories;

    QMap<QString, long long> m_categoryNameToId;
    QMap<long long, QString> m_categoryIdToName;
    QMap<QString, QSet<SceneAuthType>> m_userPermissions;

    QTimer* m_cloudSaveDebounceTimer;
    QSet<QString> m_pendingSaveUuids;

    // [New] 记录当前正在上传封面的 Scene UUID，用于回调时匹配
    QString m_uploadingCoverSceneUuid;

    QMap<QString, QString> m_pendingCovers;
};
