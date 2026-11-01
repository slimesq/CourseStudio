#pragma once
#include <memory>
#include <mutex>
#include <QObject>
#include <QJsonObject>
#include "ISource.h"
#include "Types.h"

class Scene;
class SceneItem : public QObject
{
    Q_OBJECT
public:
    explicit SceneItem(std::shared_ptr<ISource> source, QObject* parent = nullptr);

    void setPosition(float x, float y);
    void setSize(float w, float h);
    void setRotation(float angle);
    void setZOrder(int z);
    void setSource(std::shared_ptr<ISource> source);
    Transform getTransform() const;
    std::shared_ptr<ISource> getSource() const;

    void setSceneContext(Scene* scene)
    {
        m_scene = scene;
    }
    Scene* getSceneContext() const
    {
        return m_scene;
    }

    // --- [New] 窗口唯一标识接口 ---
    // 设置 UUID (通常仅在从文件加载旧数据或同步时使用)
    void setItemUuid(QString const& uuid)
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_itemUuid = uuid;
    }
    // 获取 UUID (用于偏好匹配)
    QString getItemUuid() const
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_itemUuid;
    }

    // --- 核心修改：改为 JSON 序列化接口 ---
    // 返回包含当前 Item 所有状态的 JSON 对象
    QJsonObject toJson() const;
    std::shared_ptr<ISource> detachSource();

signals:
    void transformChanged();

private:
    mutable std::recursive_mutex m_mutex;
    Transform m_transform;
    std::shared_ptr<ISource> m_source;

    Scene* m_scene = nullptr;

    // [New] 窗口的唯一身份证，生成后在生命周期内应当保持不变，用于跨场景/跨用户识别同一个窗口位置
    QString m_itemUuid;
};
