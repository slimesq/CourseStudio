#include "Scene.h"
#include "SourceFactory.h"
#include "SharedSourceManager.h"
#include "CanvasScalingStrategies.h"
#include "SceneRepository.h"
#include <algorithm>
#include <QTimer>
#include <cmath>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtConcurrent>
#include "AssetCacheManager.h"
#include "SourceProvider.h"
#include "SceneItem.h"
#include "ISource.h"
#include <QFileInfo>  // [New]

// ... (Constructors/Destructors same as before) ...
Scene::Scene(std::string const& name,
             std::string const& category,
             QString const& uuid,
             QObject* parent)
    : QObject(parent), m_name(name), m_category(category)
{
    if (uuid.isEmpty())
        m_uuid = QUuid::createUuid().toString();
    else
        m_uuid = uuid;
    m_canvasRect = QRectF(0, 0, 1920, 1080);
    m_isActive = false;
    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setSingleShot(true);
    m_autoSaveTimer->setInterval(1000);
    connect(m_autoSaveTimer, &QTimer::timeout, this, &Scene::executeAutoSave);
}
Scene::~Scene()
{
    deactivate();
    if (m_autoSaveTimer->isActive())
        executeAutoSave();
}

void Scene::cleanupTrash()
{
    // 定义局部变量用于接管垃圾
    std::vector<std::shared_ptr<SceneItem>> itemsToKill;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_trashBin.empty())
            return;

        // 快速交换：缩短锁持有时间，将析构移出锁外
        itemsToKill.swap(m_trashBin);
    }

    // itemsToKill 在这里析构 -> 引用计数归零 -> Source 析构 -> D3D Release
    // 如果在渲染线程调用此函数，则是绝对安全的。
    if (!itemsToKill.empty())
    {
        qDebug() << "[Scene] Cleaning up" << itemsToKill.size() << "items from trash.";
        itemsToKill.clear();
    }
}

// [Modified] 激活：使用引用计数
void Scene::activate()
{
    if (m_isActive)
        return;
    m_isActive = true;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (auto& item : m_items)
    {
        if (auto source = item->getSource())
        {
            source->retain();  // 引用计数 +1，0->1 时启动硬件
        }
    }
    emit contentChanged();
    qDebug() << "[Scene] Activated:" << m_name.c_str();
}

// [Modified] 休眠：使用引用计数
void Scene::deactivate()
{
    if (!m_isActive)
        return;
    m_isActive = false;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (auto& item : m_items)
    {
        if (auto source = item->getSource())
        {
            source->release();  // 引用计数 -1，1->0 时关闭硬件
        }
    }
    qDebug() << "[Scene] Deactivated:" << m_name.c_str();
}

// ... (Getters/Setters same as before) ...
void Scene::setName(std::string const& name)
{
    if (m_name == name)
        return;
    m_name = name;
    emit nameChanged(QString::fromStdString(name));
    markDirty();
}
std::string Scene::getName() const
{
    return m_name;
}
void Scene::setCategory(std::string const& category)
{
    if (m_category == category)
        return;
    m_category = category;
    emit categoryChanged(QString::fromStdString(category));
    markDirty();
}
std::string Scene::getCategory() const
{
    return m_category;
}
void Scene::setSceneType(SceneCategoryType type)
{
    if (m_type == type)
        return;
    m_type = type;
    markDirty();
}
SceneCategoryType Scene::getSceneType() const
{
    return m_type;
}
QString Scene::getUuid() const
{
    return m_uuid;
}
void Scene::setUuid(QString const& uuid)
{
    m_uuid = uuid;
}

// [Modified] 优先使用 CoverUrl，回退到本地截图
QPixmap Scene::generateThumbnail() const
{
    // 1. 【一级缓存】如果内存中已有缓存，直接返回（耗时 ~0ms）
    std::lock_guard<std::recursive_mutex> lock(m_mutex);  // 保证线程安全
    if (!m_thumbCache.isNull())
    {
        return m_thumbCache;
    }

    // 2. 【二级加载】从磁盘加载并存入缓存
    QString path;
    // 优先加载自定义封面
    if (!m_coverUrl.isEmpty())
    {
        QString localCover = AssetCacheManager::instance().queryLocalPath(m_coverUrl);
        if (QFile::exists(localCover))
            path = localCover;
        else if (QFile::exists(m_coverUrl))
            path = m_coverUrl;
    }

    // 回退到默认截图
    if (path.isEmpty())
    {
        path = SceneRepository::getThumbnailPath(m_uuid);
    }

    if (QFile::exists(path))
    {
        // 加载图片
        QPixmap pix(path);
        if (!pix.isNull())
        {
            // [优化] 预缩放：按列表实际显示需求缩放（如宽240），减少内存占用和绘制时的缩放开销
            m_thumbCache = pix.scaled(240, 135, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            return m_thumbCache;
        }
    }

    return QPixmap();  // 返回空图片
}

void Scene::setCanvasSize(int width, int height)
{
    if (m_canvasRect.width() == width && m_canvasRect.height() == height)
        return;
    int oldW = m_canvasRect.width();
    int oldH = m_canvasRect.height();
    m_canvasRect = QRectF(0, 0, width, height);
    CanvasScalingFactory::create(AppSettings::global().scaleMode)
        ->apply(m_items, oldW, oldH, width, height);
    emit canvasSizeChanged(width, height);
    markDirty();
}
QRectF Scene::getCanvasRect() const
{
    return m_canvasRect;
}

// [Modified] 添加源：适配引用计数
std::shared_ptr<SceneItem> Scene::addSource(std::shared_ptr<ISource> source)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto item = std::make_shared<SceneItem>(source, nullptr);
    item->setSceneContext(this);
    float cx = m_canvasRect.width() / 2.0f;
    float cy = m_canvasRect.height() / 2.0f;
    float iw = item->getTransform().width;
    float ih = item->getTransform().height;
    item->setPosition(cx - iw / 2, cy - ih / 2);

    // 如果当前场景是激活的，新添加的源也要立即 retain
    if (m_isActive && source)
        source->retain();

    m_items.push_back(item);
    connectItemSignals(item);
    emit itemAdded(item);
    markDirty();
    return item;
}

// [Modified] 移除源：适配引用计数
void Scene::removeSource(std::shared_ptr<SceneItem> item)
{
    if (!item)
        return;
    std::shared_ptr<SceneItem> keepAlive = item;

    // 1. 分离源
    std::shared_ptr<ISource> sourceToKill = item->detachSource();

    // 2. [核心优化] 不要直接 release，而是交给 Manager 异步销毁
    // 这样 stop() 的 600ms 耗时会发生在后台线程，UI 瞬间响应
    if (sourceToKill)
    {
        qDebug() << "[Scene] Offloading source destruction:" << sourceToKill->getName().c_str();
        SharedSourceManager::instance().releaseSourceAsync(sourceToKill);
    }

    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto it = std::find(m_items.begin(), m_items.end(), item);
        if (it != m_items.end())
        {
            disconnect(it->get(), nullptr, this, nullptr);
            m_items.erase(it);
        }
        else
        {
            return;
        }
    }
    markDirty();
    emit dataChangedAndSaved();
    emit itemRemoved(keepAlive);
}

// ... (ZOrder, clear same as before) ...
void Scene::bringItemForward(std::shared_ptr<SceneItem> item)
{
    if (!item)
        return;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    normalizeZOrder();
    auto sortedItems = m_items;
    std::sort(sortedItems.begin(),
              sortedItems.end(),
              [](std::shared_ptr<SceneItem> const& a, std::shared_ptr<SceneItem> const& b) {
                  return a->getTransform().zOrder < b->getTransform().zOrder;
              });
    auto it = std::find(sortedItems.begin(), sortedItems.end(), item);
    if (it == sortedItems.end())
        return;
    if (it + 1 == sortedItems.end())
        return;
    auto nextItem = *(it + 1);
    int currentZ = item->getTransform().zOrder;
    int nextZ = nextItem->getTransform().zOrder;
    item->setZOrder(nextZ);
    nextItem->setZOrder(currentZ);
    emit contentChanged();
    onItemChanged();
}
void Scene::sendItemBackward(std::shared_ptr<SceneItem> item)
{
    if (!item)
        return;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    normalizeZOrder();
    auto sortedItems = m_items;
    std::sort(sortedItems.begin(),
              sortedItems.end(),
              [](std::shared_ptr<SceneItem> const& a, std::shared_ptr<SceneItem> const& b) {
                  return a->getTransform().zOrder < b->getTransform().zOrder;
              });
    auto it = std::find(sortedItems.begin(), sortedItems.end(), item);
    if (it == sortedItems.end())
        return;
    if (it == sortedItems.begin())
        return;
    auto prevItem = *(it - 1);
    int currentZ = item->getTransform().zOrder;
    int prevZ = prevItem->getTransform().zOrder;
    item->setZOrder(prevZ);
    prevItem->setZOrder(currentZ);
    emit contentChanged();
    onItemChanged();
}

void Scene::clear(bool save)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    // for (auto& item : m_items) {
    //     if (auto src = item->getSource()) {
    //         if (m_isActive) src->release(); // Release instead of stop
    //     }
    // }
    if (!m_items.empty())
    {
        // 1. 将当前的 Items 全部转移到垃圾桶 (TrashBin)
        // 这样 Item 及其 Source 的引用计数不会归零，渲染线程可以安全继续这一帧的渲染
        m_trashBin.insert(m_trashBin.end(), m_items.begin(), m_items.end());
    }
    m_items.clear();
    disconnectItemSignals();
    emit contentChanged();
    if (save)
        markDirty();
}

// [Modified] 替换源：适配引用计数
void Scene::replaceSource(std::shared_ptr<SceneItem> item, std::shared_ptr<ISource> newSource)
{
    if (!item)
        return;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (auto oldSrc = item->getSource())
    {
        // 异步销毁旧源
        SharedSourceManager::instance().releaseSourceAsync(oldSrc);
    }

    item->setSource(newSource);
    if (m_isActive && newSource)
        newSource->retain();

    markDirty();
    emit contentChanged();
}

void Scene::updateSourceId(QString const& oldId, QString const& newId)
{
    if (oldId.isEmpty())
        return;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (auto& item : m_items)
    {
        auto src = item->getSource();
        if (src && src->getSourceId() == oldId.toStdString())
        {
            qDebug() << "[Scene] Update Source ID:" << src->getName().c_str() << " [" << oldId
                     << "] -> [" << newId << "]";
            src->setSourceId(newId.toStdString());
        }
    }
    markDirty();
}

std::vector<std::shared_ptr<SceneItem>> Scene::getItems() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_items;
}
void Scene::markDirty()
{
    if (!m_autoSaveTimer->isActive())
        m_autoSaveTimer->start();
}
void Scene::executeAutoSave()
{
    emit dataChangedAndSaved();
}

// [Modified] 从内容加载：使用 SharedSourceManager 并解析 coverUrl
bool Scene::loadFromContent(QString const& contentJsonStr)
{
    // 默认不带偏好加载，直接调用新的 loadWithPreference，传入空偏好
    return loadWithPreference(contentJsonStr, "");
}

// [New] 辅助函数：将 Items 数组转为 Map，方便按 UUID 查找
QMap<QString, QJsonObject> Scene::extractSourceMap(QJsonArray const& itemsArr)
{
    QMap<QString, QJsonObject> map;
    for (auto const& val : itemsArr)
    {
        QJsonObject itemObj = val.toObject();
        QString uuid = itemObj["itemUuid"].toString();
        // 如果旧数据没有 UUID，暂时忽略或无法匹配
        if (!uuid.isEmpty() && itemObj.contains("source"))
        {
            map.insert(uuid, itemObj["source"].toObject());
        }
    }
    return map;
}

// [New] 核心业务：加载共享布局，并应用个人偏好
bool Scene::loadWithPreference(QString const& sharedContent, QString const& prefContent)
{
    // 1. 解析共享场景 (Layout Authority)
    QJsonDocument docShared = QJsonDocument::fromJson(sharedContent.toUtf8());
    if (!docShared.isObject())
        return false;
    QJsonObject rootShared = docShared.object();

    // 加载基础属性
    if (rootShared.contains("layoutId"))
        m_cloudId = rootShared["layoutId"].toInt();
    if (rootShared.contains("typeId"))
        m_cloudTypeId = rootShared["typeId"].toInt();
    if (rootShared.contains("coverUrl"))
        m_coverUrl = rootShared["coverUrl"].toString();

    if (rootShared.contains("canvasWidth") && rootShared.contains("canvasHeight"))
    {
        m_canvasRect.setWidth(rootShared["canvasWidth"].toInt());
        m_canvasRect.setHeight(rootShared["canvasHeight"].toInt());
    }

    QJsonArray sharedItems = rootShared["items"].toArray();

    // 2. 解析个人偏好 (Source Preference)
    QMap<QString, QJsonObject> prefSourceMap;
    if (!prefContent.isEmpty())
    {
        QJsonDocument docPref = QJsonDocument::fromJson(prefContent.toUtf8());
        if (docPref.isObject())
        {
            QJsonObject rootPref = docPref.object();
            // 提取偏好中的源配置
            if (rootPref.contains("items"))
            {
                prefSourceMap = extractSourceMap(rootPref["items"].toArray());
            }
        }
    }

    cleanupTrash();
    // 3. 基础加载：先清理当前场景
    bool wasActive = m_isActive;
    if (wasActive)
        deactivate();
    clear(false);

    // 4. [核心] 遍历共享 Item，决策使用哪个源
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        for (auto const& val : sharedItems)
        {
            QJsonObject itemLayout = val.toObject();
            QString uuid = itemLayout["itemUuid"].toString();

            // 默认使用共享场景中的源
            QJsonObject finalSourceObj = itemLayout["source"].toObject();

            // 5. [差异分析] 检查是否有偏好
            if (!uuid.isEmpty() && prefSourceMap.contains(uuid))
            {
                QJsonObject prefSourceObj = prefSourceMap[uuid];

                // [关键需求] 校验资源类型是否一致
                // 如果管理员把窗口从“摄像头”改成了“文本”，用户的“摄像头”偏好应失效
                int sharedType = finalSourceObj["type"].toInt();
                int prefType = prefSourceObj["type"].toInt();

                if (sharedType == prefType)
                {
                    // 类型匹配，使用用户的偏好内容
                    // qDebug() << "[Merge] Apply Preference for Item:" << uuid;
                    finalSourceObj = prefSourceObj;
                }
                else
                {
                    // 类型不匹配，回退到共享内容
                    qDebug() << "[Merge] Type Mismatch (Shared:" << sharedType
                             << " Pref:" << prefType << "). Ignoring Preference.";
                }
            }

            // 6. 实例化 Item
            std::string srcId = finalSourceObj["id"].toString().toStdString();
            std::string srcName = finalSourceObj["name"].toString().toStdString();
            int rawType = finalSourceObj["type"].toInt();
            SourceType type = (SourceType)rawType;

            // 调用 SharedSourceManager 获取或复用源
            auto source = SharedSourceManager::instance().acquireSource(type, srcId, srcName);

            if (source)
            {
                if (!srcId.empty() && source->getSourceId().empty())
                {
                    source->setSourceId(srcId);
                }
                if ((source->getName().empty() || source->getName() == "Un-named") &&
                    !srcName.empty())
                {
                    source->setSourceName(srcName);
                }
            }

            auto item = std::make_shared<SceneItem>(source, nullptr);

            // [关键] 设置 UUID，确保下次能匹配上
            if (!uuid.isEmpty())
            {
                item->setItemUuid(uuid);
            }

            item->setSceneContext(this);
            item->setPosition(itemLayout["x"].toDouble(), itemLayout["y"].toDouble());
            item->setSize(itemLayout["width"].toDouble(), itemLayout["height"].toDouble());
            item->setRotation(itemLayout["rotation"].toDouble());
            item->setZOrder(itemLayout["zOrder"].toInt());

            m_items.push_back(item);
            connectItemSignals(item);
        }
        normalizeZOrder();
    }

    if (wasActive)
        activate();
    emit sceneLoaded();
    return true;
}

// [New] 合并个人偏好 (Layout Preference)
// 这里的逻辑主要是为了兼容旧代码调用，实际推荐直接用 loadWithPreference
// 如果必须动态合并（例如场景已经加载，后面才获取到偏好），可以使用此逻辑
void Scene::mergePreference(QString const& prefJson)
{
    if (prefJson.isEmpty())
        return;

    QJsonDocument doc = QJsonDocument::fromJson(prefJson.toUtf8());
    if (!doc.isObject())
        return;

    QJsonObject root = doc.object();
    if (!root.contains("items"))
        return;

    // 提取偏好 Map
    QMap<QString, QJsonObject> prefSourceMap = extractSourceMap(root["items"].toArray());

    qDebug() << "[Scene] Merging User Preferences. Pref Map Size:" << prefSourceMap.size();

    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    // 遍历当前已加载的 Item，尝试应用偏好
    for (auto& item : m_items)
    {
        QString uuid = item->getItemUuid();
        if (uuid.isEmpty())
            continue;  // 没有 UUID 的无法匹配

        if (prefSourceMap.contains(uuid))
        {
            QJsonObject prefSrcObj = prefSourceMap[uuid];

            // 类型校验
            int prefTypeRaw = prefSrcObj["type"].toInt();
            SourceType prefType = (SourceType)prefTypeRaw;

            auto currentSrc = item->getSource();
            // 如果当前源存在，且类型不匹配，跳过（说明管理员改了布局类型）
            if (currentSrc && currentSrc->getType() != prefType)
            {
                qDebug() << "[Merge] Runtime Type Mismatch for Item:" << uuid;
                continue;
            }

            // 检查内容是否真的变了
            std::string newSrcId = prefSrcObj["id"].toString().toStdString();
            std::string newSrcName = prefSrcObj["name"].toString().toStdString();

            bool changed = false;
            if (currentSrc)
            {
                if (currentSrc->getSourceId() != newSrcId)
                    changed = true;
            }
            else
            {
                if (!newSrcId.empty())
                    changed = true;
            }

            if (changed)
            {
                qDebug() << "[Merge] Runtime Applying preference for item" << uuid << "->"
                         << newSrcName.c_str();
                auto newSource =
                    SharedSourceManager::instance().acquireSource(prefType, newSrcId, newSrcName);
                if (newSource)
                {
                    if (!newSrcId.empty() && newSource->getSourceId().empty())
                        newSource->setSourceId(newSrcId);
                    replaceSource(item, newSource);
                }
            }
        }
    }

    emit contentChanged();
}

// [New] 加载原始内容并应用偏好 - 此函数现在只是 loadWithPreference 的别名
bool Scene::applyContentWithPreference(QString const& originalContent, QString const& prefContent)
{
    return loadWithPreference(originalContent, prefContent);
}

// [Modified] 序列化：增加 coverUrl
QJsonObject Scene::toJson(bool includeAuth) const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    QJsonObject json;
    json["uuid"] = m_uuid;
    json["name"] = QString::fromStdString(m_name);
    json["category"] = QString::fromStdString(m_category);
    json["type"] = (int)m_type;
    json["layoutId"] = m_cloudId;
    json["typeId"] = m_cloudTypeId;
    json["canvasWidth"] = m_canvasRect.width();
    json["canvasHeight"] = m_canvasRect.height();
    json["coverUrl"] = m_coverUrl;  // [New] 保存封面 URL

    QJsonArray itemsArr;
    for (auto const& item : m_items)
    {
        QJsonObject itemJson = item->toJson();
        // 确保 Item JSON 中包含 source 信息
        if (auto src = item->getSource())
        {
            QJsonObject s = itemJson["source"].toObject();
            QString jsonId = s["id"].toString();
            QString memId = QString::fromStdString(src->getSourceId());
            if (jsonId.isEmpty() && !memId.isEmpty())
            {
                s["id"] = memId;
                itemJson["source"] = s;
            }
        }
        itemsArr.append(itemJson);
    }
    json["items"] = itemsArr;

    if (includeAuth)
    {
        QJsonArray authArray;
        for (auto const& entry : m_authList)
        {
            if (entry.userId == 0)
                continue;
            QJsonObject authObj;
            authObj["userId"] = QString::number(entry.userId);
            authObj["queryAuth"] = entry.query;
            authObj["addAuth"] = entry.add;
            authObj["editAuth"] = entry.edit;
            authObj["deleteAuth"] = entry.del;
            authArray.append(authObj);
        }
        json["authList"] = authArray;
    }
    return json;
}

void Scene::onItemChanged()
{
    markDirty();
    emit contentChanged();
}
void Scene::connectItemSignals(std::shared_ptr<SceneItem> item)
{
    connect(item.get(), &SceneItem::transformChanged, this, &Scene::onItemChanged);
}
void Scene::connectItemSignals()
{
    for (auto& item : m_items)
    {
        disconnect(item.get(), &SceneItem::transformChanged, this, &Scene::onItemChanged);
        connect(item.get(), &SceneItem::transformChanged, this, &Scene::onItemChanged);
    }
}
void Scene::disconnectItemSignals()
{
    for (auto& item : m_items)
    {
        disconnect(item.get(), &SceneItem::transformChanged, this, &Scene::onItemChanged);
    }
}
void Scene::normalizeZOrder()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_items.empty())
        return;
    std::stable_sort(m_items.begin(),
                     m_items.end(),
                     [](std::shared_ptr<SceneItem> const& a, std::shared_ptr<SceneItem> const& b) {
                         return a->getTransform().zOrder < b->getTransform().zOrder;
                     });
    for (size_t i = 0; i < m_items.size(); ++i)
        m_items[i]->setZOrder(static_cast<int>(i));
}
