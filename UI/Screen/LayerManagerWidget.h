#pragma once
#include <QWidget>
#include <memory>
#include <vector>
#include "Scene.h"

class LayerStackView;

class LayerManagerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit LayerManagerWidget(QWidget* parent = nullptr);
    void setScene(std::shared_ptr<Scene> scene);
    void refreshList();
    // [New] 设置当前选中的 Item
    void setSelectedItem(std::shared_ptr<SceneItem> item);

protected:
    // [New] 重写绘制事件以支持无边框圆角背景
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onSceneContentChanged();

private:
    LayerStackView* m_stackView;
    std::weak_ptr<Scene> m_currentScene;
    std::weak_ptr<SceneItem> m_selectedItem;
};
