#ifndef TESTRESOURCEWINDOW_H
#define TESTRESOURCEWINDOW_H

#include <QWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QGroupBox>
#include <QJsonObject>

class TestResourceWindow : public QWidget
{
    Q_OBJECT
public:
    explicit TestResourceWindow(QWidget* parent = nullptr);

private slots:
    void appendLog(QString const& opType, bool success, QString const& msg);
    void appendDataLog(QString const& opType, QJsonObject const& data);

    // --- Tab 1: 场景分类槽函数 ---
    void onBtnSceneClassAdd();
    void onBtnSceneClassUpdate();
    void onBtnSceneClassDelete();
    void onBtnSceneClassList();
    void onBtnSceneClassDetail();
    void onBtnSceneClassExport();

    // --- Tab 2: 场景槽函数 ---
    void onBtnSceneAdd();
    void onBtnSceneUpdate();
    void onBtnSceneDelete();
    void onBtnSceneList();
    void onBtnSceneDetail();
    void onBtnSceneExport();
    void onBtnSceneUpload();

    // --- Tab 3: 素材分组槽函数 ---
    void onBtnMatGroupAdd();
    void onBtnMatGroupUpdate();
    void onBtnMatGroupDelete();
    void onBtnMatGroupList();
    void onBtnMatGroupDetail();
    void onBtnMatGroupExport();

    // --- Tab 4: 素材槽函数 ---
    void onBtnMatList();
    void onBtnMatUpdate();
    void onBtnMatDelete();
    void onBtnMatDetail();
    void onBtnMatExport();
    void onBtnMatUpload();

private:
    void initUI();
    void initConfigArea();

    // 辅助：创建一组带标签的输入框
    QWidget* createInputRow(QString const& label, QLineEdit*& lineEdit);

    // 全局配置
    QLineEdit* m_editBaseUrl;
    QLineEdit* m_editToken;
    QLineEdit* m_editClientId;
    QTextEdit* m_logArea;

    // Tab 1 Controls (Scene Class)
    QLineEdit* m_scClassId;
    QLineEdit* m_scClassName;

    // Tab 2 Controls (Scene)
    QLineEdit* m_scId;
    QLineEdit* m_scName;
    QLineEdit* m_scParentClassId;  // 关联的分类ID
    QLineEdit* m_scRemark;
    QLineEdit* m_scFilePath;

    // Tab 3 Controls (Material Group)
    QLineEdit* m_mgId;
    QLineEdit* m_mgName;
    QLineEdit* m_mgParentId;

    // Tab 4 Controls (Material)
    QLineEdit* m_matId;
    QLineEdit* m_matName;
    QLineEdit* m_matGroupId;
    QLineEdit* m_matFilePath;
};

#endif  // TESTRESOURCEWINDOW_H
