#ifndef SCENECREATEWIDGET_H
#define SCENECREATEWIDGET_H

#include <QWidget>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QFileInfo>  // [New]
#include "SceneManager.h"

// ============================================================================
// SceneCreateWidget: 新建场景主窗口内容 (Enterprise Optimized)
// ============================================================================
class SceneCreateWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SceneCreateWidget(QWidget* parent = nullptr);
    ~SceneCreateWidget();

    // 执行创建逻辑
    bool executeCreate(QWidget* dialogParent);

    // 获取当前选择或输入的分类名称
    QString getSelectedCategory() const;

protected:
    // [UI优化] 事件过滤器，用于处理控件交互时的样式重置及封面点击
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupLayout();

    // 辅助函数：将控件设置为错误状态
    void setInputErrorState(QWidget* widget, bool isError, QString const& placeholderText = "");

    // [New] 触发封面选择逻辑
    void onSelectCover();

private:
    QLineEdit* m_editName;
    QComboBox* m_comboType;
    QComboBox* m_comboCat;
    QLineEdit* m_editNewCat;

    // [New] 封面选择相关
    QLabel* m_lblCoverPreview;
    QString m_selectedCoverPath;
};

#endif  // SCENECREATEWIDGET_H
