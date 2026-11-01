#ifndef SCENELISTDELEGATE_H
#define SCENELISTDELEGATE_H

#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include <QVariantAnimation>
#include <QMap>
#include <QLineEdit>
#include "Scene.h"

class SceneListDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit SceneListDelegate(QObject* parent = nullptr);
    ~SceneListDelegate();

    void paint(QPainter* painter,
               QStyleOptionViewItem const& option,
               QModelIndex const& index) const override;
    QSize sizeHint(QStyleOptionViewItem const& option, QModelIndex const& index) const override;

    // 处理点击事件
    bool editorEvent(QEvent* event,
                     QAbstractItemModel* model,
                     QStyleOptionViewItem const& option,
                     QModelIndex const& index) override;

    // --- 编辑器支持 (用于重命名) ---
    QWidget* createEditor(QWidget* parent,
                          QStyleOptionViewItem const& option,
                          QModelIndex const& index) const override;
    void setEditorData(QWidget* editor, QModelIndex const& index) const override;
    void setModelData(QWidget* editor,
                      QAbstractItemModel* model,
                      QModelIndex const& index) const override;
    void updateEditorGeometry(QWidget* editor,
                              QStyleOptionViewItem const& option,
                              QModelIndex const& index) const override;
    // 强制清除悬停状态
    void clearHoverState();

    void setLiveMode(bool isLive)
    {
        m_isLiveMode = isLive;
    }

signals:
    // 通知外部删除场景
    void deleteRequested(Scene* scene);

private slots:
    void onAnimationValueChanged(QVariant const& value);

    // [New] 监听资源下载完成，触发重绘
    void onAssetReady(QString const& url, QString const& localPath);

private:
    // 辅助计算菜单当前的动态区域
    QRect getMenuButtonRect(QRect const& thumbRect, float progress) const;

    // 启动或反转动画
    void startAnimation(quintptr itemPtr, bool open);

    // 辅助绘制矢量垃圾桶图标
    void drawTrashIcon(QPainter* painter, QRect const& rect, QColor const& color) const;

private:
    // 存储每个场景(指针地址)对应的动画进度 (0.0 = 关闭/三点, 1.0 = 打开/删除)
    mutable QMap<quintptr, float> m_animProgress;

    // 存储正在运行的动画对象
    QMap<quintptr, QVariantAnimation*> m_animations;
    // 记录当前真正悬停的索引 (用于状态判断)
    mutable QModelIndex m_currentHoveredIndex;

    bool m_isLiveMode = false;
};

#endif  // SCENELISTDELEGATE_H
