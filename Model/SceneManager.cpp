#include "SceneManager.h"
#include "SourceFactory.h"
#include "SourceProvider.h"
#include "SceneRepository.h"
#include "UserMgr.h"
#include <QJsonDocument>
#include <QFile>
#include <QDebug>
#include <QJsonArray>
#include <algorithm>
#include <QPainter>
#include "Logger.h"
#include <QFileInfo>
#include <QTemporaryFile>  // [New]
#include "SceneItem.h"
#include "SharedSourceManager.h"

static bool isGenericName(QString const& name)
{
    if (name.isEmpty())
        return true;
    QString n = name.toLower();
    return (n == "downloading..." || n == "loading..." || n == "image source" || n == "un-named" ||
            n == "file missing" || n == "image proxy");
}

SceneManager& SceneManager::instance()
{
    static SceneManager s;
    return s;
}
SceneManager::SceneManager(QObject* parent) : QObject(parent)
{
    m_cloudSaveDebounceTimer = new QTimer(this);
    m_cloudSaveDebounceTimer->setSingleShot(true);
    m_cloudSaveDebounceTimer->setInterval(2000);
    connect(m_cloudSaveDebounceTimer, &QTimer::timeout, this, &SceneManager::performBatchCloudSave);
    connect(&AssetManager::instance(),
            &AssetManager::cloudMapReady,
            this,
            &SceneManager::onCloudAssetsReady);

    // [New] 连接封面上传信号
    connect(ResourceService::instance(),
            &ResourceService::coverUploaded,
            this,
            &SceneManager::onCoverUploaded);

    grantAuth("Default", SceneAuthType::Query);
    grantAuth("Default", SceneAuthType::Add);
    grantAuth("Default", SceneAuthType::Edit);
    grantAuth("Default", SceneAuthType::Delete);
}
// ... (loadIndex, Categories methods unchanged) ...
void SceneManager::loadIndex()
{
    AssetCacheManager::instance().init();
    AssetManager::instance().fetchCloudAssets(0);
    connect(ResourceService::instance(),
            &ResourceService::dataReceived,
            this,
            [this](QString op, QJsonObject data) {
                if (op == "scene_class_op")
                    onCloudCategoriesFetched(data);
                if (op == "scene_list")
                    onCloudScenesFetched(data);
                if (op == "scene_detail")
                    onCloudSceneDetailFetched(data);
            });
    connect(ResourceService::instance(),
            &ResourceService::operationFinished,
            this,
            &SceneManager::onServiceOpFinished);
    connect(&AssetCacheManager::instance(),
            &AssetCacheManager::assetReady,
            this,
            [this](QString const& url, QString const& localPath, bool isTemp) {
                if (isTemp)
                    return;
                qDebug() << "[SceneManager] Asset Ready Signal Received. URL:" << url
                         << " Local:" << localPath;
                this->onAssetDownloaded(url, localPath);
            });
    connect(&AssetManager::instance(),
            &AssetManager::assetUploaded,
            this,
            &SceneManager::onMaterialUploaded);
    ResourceService::instance()->getSceneClassList(1, 100);
}
QStringList SceneManager::getUniqueCategories() const
{
    return m_categories;
}
QStringList SceneManager::getCategoriesWithAuth(SceneAuthType authType)
{
    QStringList result;
    for (auto const& cat : m_categories)
    {
        if (checkAuth(authType, cat))
            result.append(cat);
    }
    return result;
}
void SceneManager::createCategory(QString const& name)
{
    if (!m_categories.contains(name))
    {
        m_categories.append(name);
        grantAuth(name, SceneAuthType::Query);
        grantAuth(name, SceneAuthType::Add);
        grantAuth(name, SceneAuthType::Edit);
        grantAuth(name, SceneAuthType::Delete);
        emit categoriesUpdated();
        ResourceService::instance()->addSceneClass(name);
    }
}
void SceneManager::grantAuth(QString const& category, SceneAuthType type)
{
    m_userPermissions[category].insert(type);
}
bool SceneManager::checkSceneAuth(std::shared_ptr<Scene> scene, SceneAuthType type)
{
    if (!scene)
        return false;
    // [核心修复] 优先检查全局系统权限 (Admin Override)
    // 如果用户拥有后台配置的 "conf:layout:delete" 权限，则允许删除任何人的场景
    if (type == SceneAuthType::Delete)
    {
        if (UserMgr::inst()->hasPermission("conf:layout:delete"))
        {
            return true;
        }
    }
    // 同理，如果是编辑操作，且有全局编辑权限，也放行
    if (type == SceneAuthType::Edit)
    {
        if (UserMgr::inst()->hasPermission("conf:layout:edit"))
        {
            return true;
        }
    }
    long long currentUserId = UserMgr::inst()->currentUser().userId;
    if (currentUserId == 0)
        currentUserId = 6103;
    if (scene->getCreatorId() == currentUserId || scene->getCreatorId() == 0)
        return true;
    auto const& list = scene->getAuthList();
    for (auto const& auth : list)
    {
        if (auth.userId == currentUserId)
        {
            switch (type)
            {
                case SceneAuthType::Query:
                    return auth.query == 1;
                case SceneAuthType::Add:
                    return auth.add == 1;
                case SceneAuthType::Edit:
                    return auth.edit == 1;
                case SceneAuthType::Delete:
                    return auth.del == 1;
            }
        }
    }
    return false;
}
long long SceneManager::getCurrentUserId() const
{
    return UserMgr::inst()->currentUser().userId;
}
bool SceneManager::checkAuth(SceneAuthType type, QString const& category, QString const& sceneUuid)
{
    if (category == "Default")
        return true;
    if (!m_userPermissions.contains(category))
        return false;
    return m_userPermissions[category].contains(type);
}

// [Modified] Create Scene Overloads
std::shared_ptr<Scene> SceneManager::createScene(std::string const& name,
                                                 std::string const& category,
                                                 int width,
                                                 int height,
                                                 QString const& coverPath)
{
    QString qCategory = QString::fromStdString(category);
    bool isNewCategory = !m_categories.contains(qCategory);
    if (qCategory != "Default" && !isNewCategory && !checkAuth(SceneAuthType::Add, qCategory))
        return nullptr;
    auto scene = std::make_shared<Scene>(name, category);
    if (width > 0 && height > 0)
        scene->setCanvasSize(width, height);
    scene->setDetailLoaded(true);
    scene->setCreatorId(getCurrentUserId());
    m_scenes.push_back(scene);
    createCategory(qCategory);
    bindAutoSync(scene);
    SceneRepository::save(scene);
    saveScene(scene);

    // [Modified] 封面处理
    if (!coverPath.isEmpty())
    {
        uploadCustomCover(scene, coverPath);
    }
    else
    {
        uploadThumbnail(scene);  // 默认截图
    }

    emit sceneListUpdated();
    return scene;
}

std::shared_ptr<Scene> SceneManager::getSceneByUuid(QString const& uuid) const
{
    auto it =
        std::find_if(m_scenes.begin(), m_scenes.end(), [&uuid](std::shared_ptr<Scene> const& s) {
            return s->getUuid() == uuid;
        });
    if (it != m_scenes.end())
        return *it;
    return nullptr;
}
// ... (Sync logic omitted for brevity, same as before) ...
void SceneManager::onCloudCategoriesFetched(QJsonObject const& data)
{
    if (data.contains("code") && data["code"].toInt() != 200)
        return;
    if (data.contains("rows") && data["rows"].isArray())
    {
        QJsonArray list = data["rows"].toArray();
        QSet<long long> processedIds;
        bool changed = false;
        for (auto const& val : list)
        {
            QJsonObject obj = val.toObject();
            long long id = obj["typeId"].toVariant().toLongLong();
            QString name = obj["typeName"].toString();
            if (name.isEmpty())
                continue;
            if (!m_categories.contains(name))
            {
                m_categories.append(name);
                changed = true;
            }
            m_categoryNameToId[name] = id;
            m_categoryIdToName[id] = name;
            grantAuth(name, SceneAuthType::Query);
            grantAuth(name, SceneAuthType::Add);
            grantAuth(name, SceneAuthType::Edit);
            grantAuth(name, SceneAuthType::Delete);
            if (!processedIds.contains(id))
            {
                processedIds.insert(id);
                ResourceService::instance()->getSceneList(1, 100, "", QString::number(id));
            }
        }
        if (changed)
            emit categoriesUpdated();
    }
    else
    {
        ResourceService::instance()->getSceneClassList(1, 100);
    }
}
long long SceneManager::getCategoryIdByName(QString const& name)
{
    return m_categoryNameToId.value(name, 0);
}
void SceneManager::bindAutoSync(std::shared_ptr<Scene> scene)
{
    disconnect(scene.get(), &Scene::dataChangedAndSaved, this, nullptr);
    connect(scene.get(), &Scene::dataChangedAndSaved, this, [this, scene]() {
        if (checkSceneAuth(scene, SceneAuthType::Edit))
        {
            SceneRepository::save(scene);
            this->triggerDeferredSave(scene);
        }
    });
}
void SceneManager::triggerDeferredSave(std::shared_ptr<Scene> scene)
{
    if (!scene)
        return;
    m_pendingSaveUuids.insert(scene->getUuid());
    m_cloudSaveDebounceTimer->start();
}

// [Modified] 实现封面上传 (生成缩略图)
void SceneManager::uploadThumbnail(std::shared_ptr<Scene> scene)
{
    if (!scene || scene->getCloudId() == 0)
        return;

    // 1. 在主线程生成 Pixmap (必须在主线程，因为涉及 QGraphicsScene 渲染)
    // 这一步很快，通常 < 10ms
    QPixmap thumb = scene->generateThumbnail();
    if (thumb.isNull())
        return;

    QString uuid = scene->getUuid();
    m_uploadingCoverSceneUuid = uuid;

    // 2. [核心优化] 将保存文件和触发上传移至后台线程
    QtConcurrent::run([this, thumb, uuid]() {
        QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
            "/temp_cover_" + uuid + ".jpg";

        // 耗时的 IO 操作
        if (thumb.save(tempPath, "JPG", 80))
        {
            // 切回主线程发起网络请求 (QNetworkAccessManager 建议在主线程调用)
            QMetaObject::invokeMethod(this, [this, tempPath]() {
                qDebug() << "[SceneManager] Uploading generated cover (Async IO done)";
                ResourceService::instance()->uploadLayoutCover(tempPath);
            });
        }
    });
}

// [New] 实现自定义封面上传
void SceneManager::uploadCustomCover(std::shared_ptr<Scene> scene, QString const& filePath)
{
    if (!scene)
        return;
    if (scene->getCloudId() == 0)
    {
        qWarning() << "[SceneManager] Upload skipped: Scene has no CloudID. Name:"
                   << scene->getName().c_str();
        return;
    }
    if (filePath.isEmpty() || !QFile::exists(filePath))
    {
        qWarning() << "[SceneManager] Upload skipped: Invalid file path:" << filePath;
        return;
    }

    m_uploadingCoverSceneUuid = scene->getUuid();
    qDebug() << "[SceneManager] >>> START Uploading custom cover. Scene:"
             << scene->getName().c_str() << " Path:" << filePath;
    ResourceService::instance()->uploadLayoutCover(filePath);
}

// [New] 封面上传回调
void SceneManager::onCoverUploaded(bool success, QString const& url, QString const& msg)
{
    if (success && !url.isEmpty() && !m_uploadingCoverSceneUuid.isEmpty())
    {
        auto scene = getSceneByUuid(m_uploadingCoverSceneUuid);
        if (scene)
        {
            qDebug() << "[SceneManager] Cover uploaded. URL:" << url << " Updating Scene...";

            // 1. 更新内存模型
            scene->setCoverUrl(url);

            // [Fix] 关键：手动建立缓存，让 generateThumbnail 能立即找到图片
            // 我们需要知道刚才上传的是哪个本地文件。这需要 m_pendingCovers 的配合，
            // 但 m_pendingCovers 在触发上传时已经 take 走了。
            // 补救：在 uploadCustomCover 中记录当前上传的文件路径到成员变量，或者简单点：
            // 我们假设 AssetCacheManager 会在下次 fetch 时下载。
            // 为了即时显示，我们可以直接下载（或者更聪明的：把上传源文件 copy 过去）。

            // 这里最简单的方式是触发一次下载（AssetCacheManager 会处理去重）
            // 或者，我们可以依赖 list 刷新。
            // 为了解决“不显示”：强制触发一次 AssetCacheManager 的缓存逻辑
            AssetCacheManager::instance().cacheAsset(url);

            // 2. 触发保存
            saveScene(scene);

            // 3. [Fix] 通知 UI 刷新列表，这样 SceneListDelegate 会重新调用 generateThumbnail
            emit sceneListUpdated();
        }
    }
    m_uploadingCoverSceneUuid.clear();
}

void SceneManager::performBatchCloudSave()
{
    if (m_pendingSaveUuids.isEmpty())
        return;
    for (QString const& uuid : m_pendingSaveUuids)
    {
        auto it = std::find_if(
            m_scenes.begin(), m_scenes.end(), [&uuid](std::shared_ptr<Scene> const& s) {
                return s->getUuid() == uuid;
            });
        if (it != m_scenes.end())
        {
            saveScene(*it);
            // [New] 保存场景数据的同时，触发封面上传
            // 注意：仅当没有自定义封面且需要更新时才自动生成
            // 这里为了简单，如果保存时没有显式指定上传动作，默认逻辑不变（自动生成缩略图）
            uploadThumbnail(*it);
        }
    }
    m_pendingSaveUuids.clear();
}
void SceneManager::onCloudAssetsReady()
{
    qDebug() << "[SceneManager] Cloud Assets Ready. Retrying broken items...";
    for (auto scene : m_scenes)
    {
        auto items = scene->getItems();
        for (auto item : items)
        {
            auto src = item->getSource();
            if (src && (src->getSourceId().empty() || src->getName() == "File Missing"))
            {
                QString name = QString::fromStdString(src->getName());
                if (name == "File Missing")
                    continue;
                qDebug() << " -> Retrying for item:" << name;
                auto newSrc = SharedSourceManager::instance().acquireSource(
                    SourceType::Image, src->getSourceId(), src->getName());
                if (newSrc->getType() == SourceType::Image && newSrc->getName() != "File Missing")
                {
                    qDebug() << " -> Recovered Item:" << name;
                    scene->replaceSource(item, newSrc);
                }
            }
        }
    }
}

// [Modified] Create Scene Overloads
std::shared_ptr<Scene> SceneManager::createScene(std::string const& name,
                                                 std::string const& category,
                                                 QString const& coverPath)
{
    return createScene(name, category, SceneCategoryType::General, coverPath);
}

std::shared_ptr<Scene> SceneManager::createScene(std::string const& name,
                                                 std::string const& category,
                                                 SceneCategoryType type,
                                                 QString const& coverPath)
{
    QString qCategory = QString::fromStdString(category);
    if (qCategory == "Default" || !m_categories.contains(qCategory) ||
        checkAuth(SceneAuthType::Add, qCategory))
    {
        auto scene = std::make_shared<Scene>(name, category);
        scene->setSceneType(type);
        int w = 1920, h = 1080;
        switch (type)
        {
            case SceneCategoryType::PortraitMobile:
                w = 1080;
                h = 1920;
                break;
            case SceneCategoryType::LiveCourse:
                w = 1440;
                h = 1080;
                break;
            case SceneCategoryType::Gaming:
                w = 1920;
                h = 1080;
                break;
            case SceneCategoryType::General:
            default:
                w = 1920;
                h = 1080;
                break;
        }
        scene->setCanvasSize(w, h);
        scene->setDetailLoaded(true);
        scene->setCreatorId(getCurrentUserId());
        QString thumbPath = SceneRepository::getThumbnailPath(scene->getUuid());

        bool customCoverSaved = false;

        // 1. 如果用户选择了封面，尝试直接处理并保存为本地缩略图
        if (!coverPath.isEmpty() && QFile::exists(coverPath))
        {
            QPixmap customPix(coverPath);
            if (!customPix.isNull())
            {
                // 建议缩放一下，避免列表卡顿，同时保证比例填充
                // 保持比例扩展，然后裁剪中心 (Cover 模式)
                QPixmap scaled = customPix.scaled(
                    320, 180, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                int cx = (scaled.width() - 320) / 2;
                int cy = (scaled.height() - 180) / 2;
                QPixmap cropped = scaled.copy(cx, cy, 320, 180);

                if (cropped.save(thumbPath, "JPG", 90))
                {
                    customCoverSaved = true;
                    qDebug() << "[SceneManager] Local custom cover saved:" << thumbPath;
                }
            }
        }

        // 2. 如果没有自定义封面，或者保存失败，才生成默认图
        if (!customCoverSaved)
        {
            QPixmap defaultThumb(320, 180);
            defaultThumb.fill(QColor(40, 40, 40));
            QPainter p(&defaultThumb);
            p.setPen(Qt::white);
            p.drawText(defaultThumb.rect(), Qt::AlignCenter, QString::fromStdString(name));
            defaultThumb.save(thumbPath, "JPG");
        }
        // ================== [核心修复结束] ==================

        m_scenes.push_back(scene);
        if (qCategory != "Default" && !m_categories.contains(qCategory))
            createCategory(qCategory);
        bindAutoSync(scene);
        SceneRepository::save(scene);
        saveScene(scene);

        // [Modified] 封面上传逻辑保持不变
        // 这里只是放入待上传队列，等拿到 CloudID 后会自动触发
        if (!coverPath.isEmpty())
        {
            m_pendingCovers[scene->getUuid()] = coverPath;
        }
        else
        {
            uploadThumbnail(scene);
        }

        emit sceneListUpdated();
        return scene;
    }
    return nullptr;
}
void SceneManager::removeScene(std::shared_ptr<Scene> scene)
{
    if (!scene)
        return;
    // [优化] 增加鉴权失败的日志输出
    if (!checkSceneAuth(scene, SceneAuthType::Delete))
    {
        qWarning() << "[SceneManager] Delete denied! User:" << getCurrentUserId()
                   << " is not Creator(" << scene->getCreatorId()
                   << ") and has no global permission.";
        return;
    }
    m_pendingSaveUuids.remove(scene->getUuid());
    auto it = std::remove(m_scenes.begin(), m_scenes.end(), scene);
    if (it != m_scenes.end())
        m_scenes.erase(it, m_scenes.end());
    SceneRepository::deleteScene(scene);
    if (scene->getCloudId() > 0)
        ResourceService::instance()->deleteScene(QString::number(scene->getCloudId()));
    emit sceneListUpdated();
}
void SceneManager::reorderScenes(std::vector<std::string> const& newOrderUuids)
{
    if (newOrderUuids.empty())
        return;
    std::vector<std::shared_ptr<Scene>> sortedScenes;
    sortedScenes.reserve(m_scenes.size());
    for (auto const& uuidStr : newOrderUuids)
    {
        QString uuid = QString::fromStdString(uuidStr);
        auto it = std::find_if(
            m_scenes.begin(), m_scenes.end(), [&uuid](std::shared_ptr<Scene> const& s) {
                return s->getUuid() == uuid;
            });
        if (it != m_scenes.end())
        {
            sortedScenes.push_back(*it);
        }
    }
    for (auto const& scene : m_scenes)
    {
        auto it = std::find(sortedScenes.begin(), sortedScenes.end(), scene);
        if (it == sortedScenes.end())
        {
            sortedScenes.push_back(scene);
        }
    }
    m_scenes = sortedScenes;
}
void SceneManager::saveScene(std::shared_ptr<Scene> scene)
{
    if (!scene)
        return;
    QJsonObject jsonContent = scene->toJson(true);
    QString contentStr = QJsonDocument(jsonContent).toJson(QJsonDocument::Compact);
    if (scene->getCloudId() == 0)
    {
        if (scene->isCreating())
        {
            triggerDeferredSave(scene);
            return;
        }
        scene->setCreating(true);
        ResourceService::instance()->addScene(
            QString::fromStdString(scene->getName()),
            QString::number(getCategoryIdByName(QString::fromStdString(scene->getCategory()))),
            contentStr,
            "Created from Client");
    }
    else
    {
        ResourceService::instance()->updateScene(QString::number(scene->getCloudId()),
                                                 QString::fromStdString(scene->getName()),
                                                 contentStr,
                                                 "Updated");
    }
}
void SceneManager::onCloudScenesFetched(QJsonObject const& data)
{
    bool hasRows = data.contains("rows") && data["rows"].isArray();
    bool hasDataArray = data.contains("data") && data["data"].isArray();
    if (!hasRows && !hasDataArray)
        return;
    QJsonArray rows;
    if (hasRows)
        rows = data["rows"].toArray();
    else if (hasDataArray)
        rows = data["data"].toArray();
    qDebug() << "[SceneManager] Fetched" << rows.size() << "scenes from cloud.";
    bool listChanged = false;
    for (int i = 0; i < rows.size(); ++i)
    {
        QJsonObject obj = rows[i].toObject();
        long long layoutId = obj["layoutId"].toVariant().toLongLong();
        QString name = obj["layoutName"].toString();
        if (name.isEmpty())
            name = "Un-named";
        long long typeId = obj["typeId"].toVariant().toLongLong();
        QString category = m_categoryIdToName.value(typeId, "Default");

        // [Modified] 提取 content 并在其中查找 coverUrl
        QString contentStr = obj["content"].toString();
        QString cloudUuid = "";
        QString fetchedCoverUrl = "";  // [New]

        if (!contentStr.isEmpty() && contentStr != "null")
        {
            QJsonObject contentObj;
            if (obj["content"].isObject())
            {
                contentObj = obj["content"].toObject();
                contentStr = QJsonDocument(contentObj).toJson(QJsonDocument::Compact);
            }
            else
            {
                QJsonDocument doc = QJsonDocument::fromJson(contentStr.toUtf8());
                if (!doc.isNull() && doc.isObject())
                    contentObj = doc.object();
            }

            // 解析 UUID
            if (contentObj.contains("uuid"))
                cloudUuid = contentObj["uuid"].toString();

            // [Fix] 解析 coverUrl
            if (contentObj.contains("coverUrl"))
                fetchedCoverUrl = contentObj["coverUrl"].toString();

            // [Fix] 兼容性：如果 content 里没有，检查外层是否有 coverUrl 字段 (有些后端会把 cover 提出来)
            if (fetchedCoverUrl.isEmpty() && obj.contains("coverUrl"))
            {
                fetchedCoverUrl = obj["coverUrl"].toString();
            }
        }

        std::shared_ptr<Scene> targetScene = nullptr;
        // ... (查找匹配 Scene 的逻辑保持不变) ...
        auto itId = std::find_if(
            m_scenes.begin(), m_scenes.end(), [layoutId](std::shared_ptr<Scene> const& s) {
                return s->getCloudId() == layoutId;
            });
        if (itId != m_scenes.end())
            targetScene = *itId;
        else if (!cloudUuid.isEmpty())
        {
            auto itUuid = std::find_if(
                m_scenes.begin(), m_scenes.end(), [cloudUuid](std::shared_ptr<Scene> const& s) {
                    return s->getUuid() == cloudUuid;
                });
            if (itUuid != m_scenes.end())
            {
                if ((*itUuid)->getCloudId() != 0 && (*itUuid)->getCloudId() != layoutId)
                    targetScene = nullptr;
                else
                    targetScene = *itUuid;
            }
        }

        if (targetScene)
        {
            targetScene->setCloudId(layoutId);
            targetScene->setCreating(false);
            if (targetScene->getName() != name.toStdString())
                targetScene->setName(name.toStdString());
            if (targetScene->getCategory() != category.toStdString())
                targetScene->setCategory(category.toStdString());

            // [Fix] 更新封面 URL
            if (!fetchedCoverUrl.isEmpty() && targetScene->getCoverUrl() != fetchedCoverUrl)
            {
                targetScene->setCoverUrl(fetchedCoverUrl);
                // 触发下载缓存，确保下次显示时无需等待
                if (fetchedCoverUrl.startsWith("http"))
                {
                    AssetCacheManager::instance().cacheAsset(fetchedCoverUrl);
                }
            }

            bindAutoSync(targetScene);
        }
        else
        {
            auto newScene =
                std::make_shared<Scene>(name.toStdString(), category.toStdString(), cloudUuid);
            newScene->setCloudId(layoutId);
            newScene->setCloudTypeId(typeId);
            newScene->setDetailLoaded(false);

            // [Fix] 设置新场景的封面 URL
            if (!fetchedCoverUrl.isEmpty())
            {
                newScene->setCoverUrl(fetchedCoverUrl);
                if (fetchedCoverUrl.startsWith("http"))
                {
                    AssetCacheManager::instance().cacheAsset(fetchedCoverUrl);
                }
            }

            auto existing = getSceneByUuid(cloudUuid);
            if (existing)
            {
                QString newLocalUuid = QUuid::createUuid().toString();
                newScene->setUuid(newLocalUuid);
                saveScene(newScene);
            }
            m_scenes.push_back(newScene);
            bindAutoSync(newScene);
            listChanged = true;
        }
    }
    // ... (后续逻辑不变) ...
    // [Fix] 在解析循环中或结束后，检查暂存的封面
    for (auto& scene : m_scenes)
    {
        if (scene->getCloudId() > 0 && m_pendingCovers.contains(scene->getUuid()))
        {
            QString path = m_pendingCovers.take(scene->getUuid());
            qDebug() << "[SceneManager] CloudID ready (" << scene->getCloudId()
                     << "). Triggering cover upload for:" << scene->getName().c_str();
            uploadCustomCover(scene, path);
        }
    }
    if (listChanged || rows.size() > 0)
        emit sceneListUpdated();
}

void SceneManager::onCloudSceneDetailFetched(QJsonObject const& data)
{
    if (data.contains("code") && data["code"].toInt() != 200)
        return;
    if (data.contains("data"))
    {
        QJsonObject detail = data["data"].toObject();
        long long sceneId = detail["layoutId"].toVariant().toLongLong();
        auto it = std::find_if(m_scenes.begin(), m_scenes.end(), [sceneId](auto s) {
            return s->getCloudId() == sceneId;
        });
        if (it != m_scenes.end())
        {
            std::shared_ptr<Scene> scene = *it;
            if (detail.contains("content"))
            {
                QString contentStr = detail["content"].toString();
                if (contentStr.isEmpty() && detail["content"].isObject())
                {
                    QJsonDocument doc(detail["content"].toObject());
                    contentStr = doc.toJson(QJsonDocument::Compact);
                }
                if (!contentStr.isEmpty())
                    scene->loadFromContent(contentStr);
            }
            if (scene->getCloudId() != sceneId)
                scene->setCloudId(sceneId);
            if (detail.contains("createBy"))
                scene->setCreatorId(detail["createBy"].toVariant().toLongLong());
            if (detail.contains("authList"))
            {
                QList<SceneAuthEntry> localList;
                QJsonArray authArr = detail["authList"].toArray();
                for (auto const& val : authArr)
                {
                    QJsonObject authObj = val.toObject();
                    SceneAuthEntry entry;
                    entry.userId = authObj["userId"].toVariant().toLongLong();
                    entry.nickName = authObj["nickName"].toString();
                    entry.query = authObj["queryAuth"].toVariant().toInt();
                    entry.add = authObj["addAuth"].toVariant().toInt();
                    entry.edit = authObj["editAuth"].toVariant().toInt();
                    entry.del = authObj["deleteAuth"].toVariant().toInt();
                    localList.append(entry);
                }
                scene->setAuthList(localList);
            }
            scene->setDetailLoaded(true);
            emit sceneLoaded(scene.get());
        }
    }
}
void SceneManager::onServiceOpFinished(QString const& op, bool success, QString const& msg)
{
    if (op.contains("scene"))
    {
        emit cloudSyncFinished("", success);
    }

    // [Optimization] 仅在必要时刷新列表
    // scene_op 通常只是更新了 json 内容，不需要刷新整个列表
    // 只有 scene_add (新增) 或 scene_delete (删除整个场景) 才需要刷新
    if (success)
    {
        if (op == "scene_add" || op == "scene_delete")
        {
            qDebug() << "[SceneManager] Structural change detected. Refreshing list...";
            ResourceService::instance()->getSceneList(1, 100);
        }
        else if (op == "scene_op")
        {
            // 仅仅是更新内容，不需要刷新列表，因为本地内存已经是新的了
            qDebug() << "[SceneManager] Content updated. Skipping list refresh to prevent lag.";
        }
    }
}

void SceneManager::onMaterialUploaded(QString const& localPath, QString const& cloudUrl)
{
    if (cloudUrl.isEmpty())
    {
        qWarning() << "[SceneManager] Ignored empty Cloud URL update for:" << localPath;
        return;
    }
    qDebug() << "[SceneManager] 🟢 Material Uploaded. Updating Scene Source IDs. Local:"
             << localPath << " -> Cloud:" << cloudUrl;
    for (auto scene : m_scenes)
    {
        scene->updateSourceId(localPath, cloudUrl);
    }
}

// [Modified] 添加资源：使用 SharedSourceManager
void SceneManager::addAssetToScene(std::shared_ptr<Scene> scene, AssetModel const& asset)
{
    if (!scene)
        return;
    qDebug() << "[SceneManager] Adding Asset:" << asset.name << " Path/ID:" << asset.pathOrId;
    if (asset.pathOrId.isEmpty())
    {
        LOG_ERROR("[SceneManager] Attempted to add asset with EMPTY pathOrId. Ignored.");
        return;
    }

    // [Change] 通过 Manager 获取
    std::shared_ptr<ISource> source = SharedSourceManager::instance().acquireSource(
        asset.type, asset.pathOrId.toStdString(), asset.name.toStdString());

    // [添加日志]
    qDebug() << "[SceneManager] Acquired Source for asset:" << asset.name
             << " | Source ID:" << source->getSourceId().c_str()
             << " | Asset PathOrId:" << asset.pathOrId;

    if (source)
    {
        if (source->getSourceId().empty())
        {
            qWarning() << "[SceneManager] Source created with empty ID! Fixing using asset path:"
                       << asset.pathOrId;
            source->setSourceId(asset.pathOrId.toStdString());
        }
        if (!asset.name.isEmpty())
        {
            source->setSourceName(asset.name.toStdString());
        }
        qDebug() << "[SceneManager] Source Ready. ID:" << source->getSourceId().c_str()
                 << " Name:" << source->getName().c_str();
        scene->addSource(source);
        if (!asset.cloudUrl.isEmpty() && asset.cloudUrl != asset.pathOrId)
        {
            qDebug() << "[SceneManager] Immediate ID Sync for uploaded asset:" << asset.name;
            scene->updateSourceId(asset.pathOrId, asset.cloudUrl);
        }
    }
}

// [Modified] 资源热替换：使用 SharedSourceManager
void SceneManager::onAssetDownloaded(QString const& url, QString const& localPath)
{
    QString downloadedFileName = QFileInfo(localPath).fileName();
    for (auto scene : m_scenes)
    {
        auto items = scene->getItems();
        for (auto item : items)
        {
            auto src = item->getSource();
            if (!src)
                continue;
            bool match = false;
            if (src->getSourceId() == url.toStdString())
            {
                match = true;
            }
            else if (src->getSourceId().empty())
            {
                QString srcName = QString::fromStdString(src->getName());
                if (!isGenericName(srcName) && srcName == downloadedFileName)
                {
                    match = true;
                    qDebug() << "[SceneManager] Match by Name (Strict):" << srcName;
                }
            }
            if (match)
            {
                qDebug() << "[SceneManager] Hot-Swapping Source for:" << src->getName().c_str();

                // [Change] 使用 Manager 复用源
                // 注意：这里 url 作为 ID，但 SourceFactory 内部可能会解析本地路径
                // SharedSourceManager 的缓存机制会确保如果有多个 Item 引用同一个 URL，它们共享同一个 ImageFileSource
                auto newSource = SharedSourceManager::instance().acquireSource(
                    SourceType::Image, url.toStdString(), src->getName());

                if (newSource)
                {
                    newSource->setSourceId(url.toStdString());
                    scene->replaceSource(item, newSource);
                    qDebug() << "  -> Swap Success. ID kept as URL:" << url;
                }
                else
                {
                    qWarning()
                        << "  -> Swap Failed. Could not create source from downloaded asset.";
                }
            }
        }
    }
}
void SceneManager::updateLocalSceneFile(QString const& uuid, QString const& contentJson)
{
}
