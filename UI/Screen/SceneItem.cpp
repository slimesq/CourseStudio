#include "SceneItem.h"
#include <QUuid>  // [New] 用于生成唯一标识

SceneItem::SceneItem(std::shared_ptr<ISource> source, QObject* parent)
    : QObject(parent), m_source(source)
{
    // [New] 初始化唯一标识
    // 每一个新创建的 Item 都会获得一个随机 UUID
    // 如果是从文件加载的，稍后 Scene::loadFromContent 会调用 setItemUuid 覆盖此值
    m_itemUuid = QUuid::createUuid().toString();

    if (m_source)
    {
        m_transform.width = m_source->getBaseWidth();
        m_transform.height = m_source->getBaseHeight();
    }
}

void SceneItem::setPosition(float x, float y)
{
    bool changed = false;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_transform.x != x || m_transform.y != y)
        {
            m_transform.x = x;
            m_transform.y = y;
            changed = true;
        }
    }
    if (changed)
    {
        emit transformChanged();
    }
}

void SceneItem::setSize(float w, float h)
{
    bool changed = false;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_transform.width == w && m_transform.height == h)
            return;
        m_transform.width = w;
        m_transform.height = h;
        changed = true;
    }
    if (changed)
    {
        emit transformChanged();
    }
}

void SceneItem::setRotation(float angle)
{
    bool changed = false;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_transform.rotation = angle;
        changed = true;
    }
    if (changed)
    {
        emit transformChanged();
    }
}

void SceneItem::setZOrder(int z)
{
    bool changed = false;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_transform.zOrder != z)
        {
            m_transform.zOrder = z;
            changed = true;
        }
    }
    if (changed)
    {
        emit transformChanged();
    }
}

// [Modified] 移除内部 stop 调用，生命周期交由 Scene 管理
void SceneItem::setSource(std::shared_ptr<ISource> source)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    // 我们不再这里 stop 旧源。Scene::replaceSource 已经处理了 release。
    m_source = source;
}

Transform SceneItem::getTransform() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_transform;
}

std::shared_ptr<ISource> SceneItem::getSource() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_source;
}

QJsonObject SceneItem::toJson() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    QJsonObject json;

    // [New] 持久化保存 UUID，这是实现个人偏好合并的关键 Key
    json["itemUuid"] = m_itemUuid;

    json["x"] = m_transform.x;
    json["y"] = m_transform.y;
    json["width"] = m_transform.width;
    json["height"] = m_transform.height;
    json["rotation"] = m_transform.rotation;
    json["zOrder"] = m_transform.zOrder;

    QJsonObject sourceJson;
    if (m_source)
    {
        sourceJson["id"] = QString::fromStdString(m_source->getSourceId());
        sourceJson["name"] = QString::fromStdString(m_source->getName());
        sourceJson["type"] = (int)m_source->getType();
    }
    json["source"] = sourceJson;

    return json;
}

std::shared_ptr<ISource> SceneItem::detachSource()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto ptr = m_source;
    m_source = nullptr;
    return ptr;
}
