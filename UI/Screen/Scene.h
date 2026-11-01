#ifndef SCENE_H
#define SCENE_H
#include <vector>
#include <memory>
#include <mutex>
#include <string>
#include <QObject>
#include <QRectF>
#include <QUuid>
#include <QPixmap>
#include <QTimer>
#include <QMap>         // [New]
#include <QJsonObject>  // [New]
#include <QJsonArray>   // [New]
#include "Types.h"

class ISource;
class SceneItem;
// 1. 定义单条权限结构
struct SceneAuthEntry
{
    long long userId;
    QString nickName;
    int query = 0;
    int add = 0;
    int edit = 0;
    int del = 0;
};

class Scene : public QObject, public std::enable_shared_from_this<Scene>
{
    Q_OBJECT
public:
    explicit Scene(std::string const& name,
                   std::string const& category = "Default",
                   QString const& uuid = "",
                   QObject* parent = nullptr);
    ~Scene();

    void cleanupTrash();

    // --- 场景激活/休眠 ---
    void activate();
    void deactivate();
    bool isActive() const
    {
        return m_isActive;
    }

    // --- 基本属性 ---
    void setName(std::string const& name);
    std::string getName() const;

    void setCategory(std::string const& category);
    std::string getCategory() const;

    // 云端 ID 支持
    void setCloudId(long long id)
    {
        m_cloudId = id;
    }
    long long getCloudId() const
    {
        return m_cloudId;
    }

    // 云端分类 ID 支持
    void setCloudTypeId(long long id)
    {
        m_cloudTypeId = id;
    }
    long long getCloudTypeId() const
    {
        return m_cloudTypeId;
    }

    void setSceneType(SceneCategoryType type);
    SceneCategoryType getSceneType() const;

    QString getUuid() const;
    void setUuid(QString const& uuid);

    // [New] 封面 URL 支持
    void setCoverUrl(QString const& url)
    {
        m_coverUrl = url;
    }
    QString getCoverUrl() const
    {
        return m_coverUrl;
    }

    // [Modified] 优先使用 CoverUrl，回退到本地截图
    QPixmap generateThumbnail() const;

    // --- 画布管理 ---
    void setCanvasSize(int width, int height);
    QRectF getCanvasRect() const;

    // --- 资源管理 ---
    std::shared_ptr<SceneItem> addSource(std::shared_ptr<ISource> source);
    void removeSource(std::shared_ptr<SceneItem> item);

    // [New] 图层顺序调整接口
    void bringItemForward(std::shared_ptr<SceneItem> item);  // 上移一层
    void sendItemBackward(std::shared_ptr<SceneItem> item);  // 下移一层

    void clear(bool save = true);
    void replaceSource(std::shared_ptr<SceneItem> item, std::shared_ptr<ISource> newSource);

    // 替换资源的 ID (用于本地转云端同步)
    void updateSourceId(QString const& oldId, QString const& newId);

    std::vector<std::shared_ptr<SceneItem>> getItems() const;

    // 从云端/本地 JSON 字符串还原场景 (数据注入)
    bool loadFromContent(QString const& contentJsonStr);

    // [New] 核心业务：加载共享布局，并应用个人偏好
    // sharedContent: 管理员配置的最新布局（权威布局）
    // prefContent: 用户的个人偏好（源配置）
    bool loadWithPreference(QString const& sharedContent, QString const& prefContent);

    // [Fix] 补全缺失的声明：加载原始内容并应用偏好 (loadWithPreference 的别名)
    bool applyContentWithPreference(QString const& originalContent, QString const& prefContent);

    // [New] 合并个人偏好 (Layout Preference) - 保留旧接口兼容，内部逻辑可复用
    // prefJson: 包含个性化源配置的 JSON (通常也是一个 Scene JSON 结构)
    void mergePreference(QString const& prefJson);

    // [Deprecated] 旧接口保留，现在重定向到 mergePreference
    void applyPreferences(QString const& prefJson)
    {
        mergePreference(prefJson);
    }

    // --- 序列化 (只负责生成数据，不负责 IO) ---
    // [Modified] 增加 includeAuth 参数，用于控制是否序列化权限列表
    QJsonObject toJson(bool includeAuth = false) const;

    void setCreatorId(long long id)
    {
        m_creatorId = id;
    }
    long long getCreatorId() const
    {
        return m_creatorId;
    }

    void setAuthList(QList<SceneAuthEntry> const& list)
    {
        m_authList = list;
    }
    QList<SceneAuthEntry> getAuthList() const
    {
        return m_authList;
    }

    // 3. 辅助：添加单条权限 (用于新建场景时)
    void addAuth(SceneAuthEntry const& entry)
    {
        // 如果存在则覆盖，不存在则添加
        for (int i = 0; i < m_authList.size(); ++i)
        {
            if (m_authList[i].userId == entry.userId)
            {
                m_authList[i] = entry;
                return;
            }
        }
        m_authList.append(entry);
    }

    // [New] 懒加载状态标记
    bool isDetailLoaded() const
    {
        return m_isDetailLoaded;
    }
    void setDetailLoaded(bool loaded)
    {
        m_isDetailLoaded = loaded;
    }

    bool isCreating() const
    {
        return m_isCreating;
    }
    void setCreating(bool val)
    {
        m_isCreating = val;
    }
    void notifyContentChanged()
    {
        emit contentChanged();
    }

signals:
    void itemAdded(std::shared_ptr<SceneItem> item);
    void itemRemoved(std::shared_ptr<SceneItem> item);
    void nameChanged(QString const& newName);
    void categoryChanged(QString const& newCat);
    void canvasSizeChanged(int w, int h);
    void sceneLoaded();
    void contentChanged();
    void dataChangedAndSaved();  // 通知外部：数据变了，请保存
    void sourceAboutToBeDestroyed(std::shared_ptr<ISource> source);

private slots:
    void onItemChanged();
    void executeAutoSave();

private:
    void markDirty();
    void connectItemSignals(std::shared_ptr<SceneItem> item);
    void connectItemSignals();     // [修改] 不带参数，连接所有
    void disconnectItemSignals();  // [新增] 断开所有
    void normalizeZOrder();

    // [New] 辅助：从 JSON 中提取 "itemUuid" -> SourceJson 的映射，用于快速查找
    QMap<QString, QJsonObject> extractSourceMap(QJsonArray const& itemsArr);

private:
    std::string m_name;
    std::string m_category;
    QString m_uuid;
    QString m_coverUrl;  // [New] 封面 URL

    long long m_cloudId = 0;
    long long m_cloudTypeId = 0;

    QTimer* m_autoSaveTimer;
    std::vector<std::shared_ptr<SceneItem>> m_items;
    std::vector<std::shared_ptr<SceneItem>> m_trashBin;
    mutable std::recursive_mutex m_mutex;
    bool m_isActive = false;
    QRectF m_canvasRect = QRectF(0, 0, 1920, 1080);
    SceneCategoryType m_type = SceneCategoryType::General;
    mutable QPixmap m_thumbCache;  // [New] 内存缓存

    long long m_creatorId = 0;         // [New] 创建者ID
    QList<SceneAuthEntry> m_authList;  // [New] 权限列表

    bool m_isDetailLoaded = false;  // [New] 是否已加载详情（懒加载）

    bool m_isCreating = false;
};

#endif  // SCENE_H
