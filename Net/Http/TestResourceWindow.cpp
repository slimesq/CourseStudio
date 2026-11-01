#include "TestResourceWindow.h"
#include "ResourceService.h"
#include "UserContext.h"
#include "ApiConfig.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QJsonDocument>
#include <QDateTime>
#include <QScrollArea>
#include <QFileInfo>

TestResourceWindow::TestResourceWindow(QWidget* parent) : QWidget(parent)
{
    initUI();
    connect(ResourceService::instance(),
            &ResourceService::operationFinished,
            this,
            &TestResourceWindow::appendLog);
    connect(ResourceService::instance(),
            &ResourceService::dataReceived,
            this,
            &TestResourceWindow::appendDataLog);
    resize(1000, 700);  // 调大窗口以容纳更多控件
}

void TestResourceWindow::initUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // 1. 顶部配置区
    initConfigArea();
    mainLayout->addWidget(new QGroupBox("全局配置 (BaseURL / Token / ClientID)", this));

    // 2. 四个功能页签
    QTabWidget* tabs = new QTabWidget(this);

    // ================= Tab 1: 场景分类 (Scene Class) =================
    QWidget* tab1 = new QWidget();
    QVBoxLayout* l1 = new QVBoxLayout(tab1);

    l1->addWidget(
        createInputRow("分类ID (Delete/Update/Detail用):", m_scClassId = new QLineEdit()));
    l1->addWidget(createInputRow("分类名称 (Add/Update用):", m_scClassName = new QLineEdit()));

    QHBoxLayout* btnLayout1 = new QHBoxLayout();
    QPushButton* btn1_1 = new QPushButton("新增分类 (POST)");
    connect(btn1_1, &QPushButton::clicked, this, &TestResourceWindow::onBtnSceneClassAdd);
    QPushButton* btn1_2 = new QPushButton("修改分类 (PUT)");
    connect(btn1_2, &QPushButton::clicked, this, &TestResourceWindow::onBtnSceneClassUpdate);
    QPushButton* btn1_3 = new QPushButton("删除分类 (DEL)");
    connect(btn1_3, &QPushButton::clicked, this, &TestResourceWindow::onBtnSceneClassDelete);
    QPushButton* btn1_4 = new QPushButton("查询列表 (GET)");
    connect(btn1_4, &QPushButton::clicked, this, &TestResourceWindow::onBtnSceneClassList);
    QPushButton* btn1_5 = new QPushButton("获取详情 (GET)");
    connect(btn1_5, &QPushButton::clicked, this, &TestResourceWindow::onBtnSceneClassDetail);
    QPushButton* btn1_6 = new QPushButton("导出列表 (POST)");
    connect(btn1_6, &QPushButton::clicked, this, &TestResourceWindow::onBtnSceneClassExport);

    btnLayout1->addWidget(btn1_1);
    btnLayout1->addWidget(btn1_2);
    btnLayout1->addWidget(btn1_3);
    btnLayout1->addWidget(btn1_4);
    btnLayout1->addWidget(btn1_5);
    btnLayout1->addWidget(btn1_6);
    l1->addLayout(btnLayout1);
    l1->addStretch();
    tabs->addTab(tab1, "1. 场景分类");

    // ================= Tab 2: 场景 (Scene) =================
    QWidget* tab2 = new QWidget();
    QVBoxLayout* l2 = new QVBoxLayout(tab2);

    l2->addWidget(createInputRow("场景ID (Del/Upd/Det用):", m_scId = new QLineEdit()));
    l2->addWidget(createInputRow("场景名称 (Add/Upd用):", m_scName = new QLineEdit()));
    l2->addWidget(
        createInputRow("所属分类ID (Add/Upload用):", m_scParentClassId = new QLineEdit()));
    l2->addWidget(createInputRow("备注:", m_scRemark = new QLineEdit()));

    QHBoxLayout* fileLayout2 = new QHBoxLayout();
    m_scFilePath = new QLineEdit();
    m_scFilePath->setPlaceholderText("上传文件路径...");
    QPushButton* btnSelFile2 = new QPushButton("选择文件");
    connect(btnSelFile2, &QPushButton::clicked, [this]() {
        m_scFilePath->setText(QFileDialog::getOpenFileName());
    });
    fileLayout2->addWidget(new QLabel("文件路径:"));
    fileLayout2->addWidget(m_scFilePath);
    fileLayout2->addWidget(btnSelFile2);
    l2->addLayout(fileLayout2);

    QHBoxLayout* btnLayout2 = new QHBoxLayout();
    QPushButton* btn2_1 = new QPushButton("新增场景");
    connect(btn2_1, &QPushButton::clicked, this, &TestResourceWindow::onBtnSceneAdd);
    QPushButton* btn2_2 = new QPushButton("修改场景");
    connect(btn2_2, &QPushButton::clicked, this, &TestResourceWindow::onBtnSceneUpdate);
    QPushButton* btn2_3 = new QPushButton("删除场景");
    connect(btn2_3, &QPushButton::clicked, this, &TestResourceWindow::onBtnSceneDelete);
    QPushButton* btn2_4 = new QPushButton("查询列表");
    connect(btn2_4, &QPushButton::clicked, this, &TestResourceWindow::onBtnSceneList);
    QPushButton* btn2_5 = new QPushButton("获取详情");
    connect(btn2_5, &QPushButton::clicked, this, &TestResourceWindow::onBtnSceneDetail);
    QPushButton* btn2_6 = new QPushButton("导出列表");
    connect(btn2_6, &QPushButton::clicked, this, &TestResourceWindow::onBtnSceneExport);
    QPushButton* btn2_7 = new QPushButton("上传文件");
    connect(btn2_7, &QPushButton::clicked, this, &TestResourceWindow::onBtnSceneUpload);

    btnLayout2->addWidget(btn2_1);
    btnLayout2->addWidget(btn2_2);
    btnLayout2->addWidget(btn2_3);
    btnLayout2->addWidget(btn2_4);
    btnLayout2->addWidget(btn2_5);
    btnLayout2->addWidget(btn2_6);
    btnLayout2->addWidget(btn2_7);
    l2->addLayout(btnLayout2);
    l2->addStretch();
    tabs->addTab(tab2, "2. 场景");

    // ================= Tab 3: 素材分组 (Material Group) =================
    QWidget* tab3 = new QWidget();
    QVBoxLayout* l3 = new QVBoxLayout(tab3);

    l3->addWidget(createInputRow("分组ID (Del/Upd/Det用):", m_mgId = new QLineEdit()));
    l3->addWidget(createInputRow("分组名称 (Add/Upd用):", m_mgName = new QLineEdit()));
    l3->addWidget(createInputRow("父分组ID (Add用,可选):", m_mgParentId = new QLineEdit()));

    QHBoxLayout* btnLayout3 = new QHBoxLayout();
    QPushButton* btn3_1 = new QPushButton("新增分组");
    connect(btn3_1, &QPushButton::clicked, this, &TestResourceWindow::onBtnMatGroupAdd);
    QPushButton* btn3_2 = new QPushButton("修改分组");
    connect(btn3_2, &QPushButton::clicked, this, &TestResourceWindow::onBtnMatGroupUpdate);
    QPushButton* btn3_3 = new QPushButton("删除分组");
    connect(btn3_3, &QPushButton::clicked, this, &TestResourceWindow::onBtnMatGroupDelete);
    QPushButton* btn3_4 = new QPushButton("查询列表");
    connect(btn3_4, &QPushButton::clicked, this, &TestResourceWindow::onBtnMatGroupList);
    QPushButton* btn3_5 = new QPushButton("获取详情");
    connect(btn3_5, &QPushButton::clicked, this, &TestResourceWindow::onBtnMatGroupDetail);
    QPushButton* btn3_6 = new QPushButton("导出列表");
    connect(btn3_6, &QPushButton::clicked, this, &TestResourceWindow::onBtnMatGroupExport);

    btnLayout3->addWidget(btn3_1);
    btnLayout3->addWidget(btn3_2);
    btnLayout3->addWidget(btn3_3);
    btnLayout3->addWidget(btn3_4);
    btnLayout3->addWidget(btn3_5);
    btnLayout3->addWidget(btn3_6);
    l3->addLayout(btnLayout3);
    l3->addStretch();
    tabs->addTab(tab3, "3. 素材分组");

    // ================= Tab 4: 素材 (Material) =================
    QWidget* tab4 = new QWidget();
    QVBoxLayout* l4 = new QVBoxLayout(tab4);

    l4->addWidget(createInputRow("素材ID (Del/Upd/Det用):", m_matId = new QLineEdit()));
    l4->addWidget(createInputRow("素材名称 (Upd/List用):", m_matName = new QLineEdit()));
    l4->addWidget(createInputRow("所属分组ID (List/Upload用):", m_matGroupId = new QLineEdit()));

    QHBoxLayout* fileLayout4 = new QHBoxLayout();
    m_matFilePath = new QLineEdit();
    m_matFilePath->setPlaceholderText("素材文件路径...");
    QPushButton* btnSelFile4 = new QPushButton("选择文件");
    connect(btnSelFile4, &QPushButton::clicked, [this]() {
        m_matFilePath->setText(QFileDialog::getOpenFileName());
    });
    fileLayout4->addWidget(new QLabel("文件路径:"));
    fileLayout4->addWidget(m_matFilePath);
    fileLayout4->addWidget(btnSelFile4);
    l4->addLayout(fileLayout4);

    QHBoxLayout* btnLayout4 = new QHBoxLayout();
    QPushButton* btn4_1 = new QPushButton("查询列表");
    connect(btn4_1, &QPushButton::clicked, this, &TestResourceWindow::onBtnMatList);
    QPushButton* btn4_2 = new QPushButton("获取详情");
    connect(btn4_2, &QPushButton::clicked, this, &TestResourceWindow::onBtnMatDetail);
    QPushButton* btn4_3 = new QPushButton("修改素材");
    connect(btn4_3, &QPushButton::clicked, this, &TestResourceWindow::onBtnMatUpdate);
    QPushButton* btn4_4 = new QPushButton("删除素材");
    connect(btn4_4, &QPushButton::clicked, this, &TestResourceWindow::onBtnMatDelete);
    QPushButton* btn4_5 = new QPushButton("导出列表");
    connect(btn4_5, &QPushButton::clicked, this, &TestResourceWindow::onBtnMatExport);
    QPushButton* btn4_6 = new QPushButton("上传素材");
    connect(btn4_6, &QPushButton::clicked, this, &TestResourceWindow::onBtnMatUpload);

    btnLayout4->addWidget(btn4_1);
    btnLayout4->addWidget(btn4_2);
    btnLayout4->addWidget(btn4_3);
    btnLayout4->addWidget(btn4_4);
    btnLayout4->addWidget(btn4_5);
    btnLayout4->addWidget(btn4_6);
    l4->addLayout(btnLayout4);
    l4->addStretch();
    tabs->addTab(tab4, "4. 素材");

    mainLayout->addWidget(tabs);

    // 3. 日志区
    m_logArea = new QTextEdit(this);
    m_logArea->setReadOnly(true);
    mainLayout->addWidget(new QLabel("操作日志:"));
    mainLayout->addWidget(m_logArea);
}

void TestResourceWindow::initConfigArea()
{
    // 简单配置，实际项目中可以用 FormLayout
    // 这里为了演示，假设已经在 main 或 ApiConfig 初始化了
}

QWidget* TestResourceWindow::createInputRow(QString const& label, QLineEdit*& lineEdit)
{
    QWidget* w = new QWidget();
    QHBoxLayout* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(new QLabel(label));
    l->addWidget(lineEdit);
    return w;
}

// ============ Slots Implementation ============

// Tab 1: Scene Class
void TestResourceWindow::onBtnSceneClassAdd()
{
    ResourceService::instance()->addSceneClass(m_scClassName->text());
}
void TestResourceWindow::onBtnSceneClassUpdate()
{
    ResourceService::instance()->updateSceneClass(m_scClassId->text(), m_scClassName->text());
}
void TestResourceWindow::onBtnSceneClassDelete()
{
    ResourceService::instance()->deleteSceneClass(m_scClassId->text());
}
void TestResourceWindow::onBtnSceneClassList()
{
    ResourceService::instance()->getSceneClassList(1, 20);
}
void TestResourceWindow::onBtnSceneClassDetail()
{
    ResourceService::instance()->getSceneClassDetail(m_scClassId->text());
}
void TestResourceWindow::onBtnSceneClassExport()
{
    QVariantMap params;
    params["typeName"] = m_scClassName->text();
    ResourceService::instance()->exportSceneClassList(params);
}

// Tab 2: Scene
void TestResourceWindow::onBtnSceneAdd()
{
    ResourceService::instance()->addScene(
        m_scName->text(), m_scParentClassId->text(), m_scRemark->text());
}
void TestResourceWindow::onBtnSceneUpdate()
{
    ResourceService::instance()->updateScene(m_scId->text(), m_scName->text(), m_scRemark->text());
}
void TestResourceWindow::onBtnSceneDelete()
{
    ResourceService::instance()->deleteScene(m_scId->text());
}
void TestResourceWindow::onBtnSceneList()
{
    ResourceService::instance()->getSceneList(1, 20, m_scName->text());
}
void TestResourceWindow::onBtnSceneDetail()
{
    ResourceService::instance()->getSceneDetail(m_scId->text());
}
void TestResourceWindow::onBtnSceneExport()
{
    QVariantMap params;
    params["layoutName"] = m_scName->text();
    ResourceService::instance()->exportSceneList(params);
}
void TestResourceWindow::onBtnSceneUpload()
{
    ResourceService::instance()->uploadSceneFile(m_scFilePath->text(), m_scParentClassId->text());
}

// Tab 3: Material Group
void TestResourceWindow::onBtnMatGroupAdd()
{
    ResourceService::instance()->addMaterialGroup(m_mgName->text(), m_mgParentId->text());
}
void TestResourceWindow::onBtnMatGroupUpdate()
{
    ResourceService::instance()->updateMaterialGroup(m_mgId->text(), m_mgName->text());
}
void TestResourceWindow::onBtnMatGroupDelete()
{
    ResourceService::instance()->deleteMaterialGroup(m_mgId->text());
}
void TestResourceWindow::onBtnMatGroupList()
{
    ResourceService::instance()->getMaterialGroupList(1, 20);
}
void TestResourceWindow::onBtnMatGroupDetail()
{
    ResourceService::instance()->getMaterialGroupDetail(m_mgId->text());
}
void TestResourceWindow::onBtnMatGroupExport()
{
    QVariantMap params;
    params["groupName"] = m_mgName->text();
    ResourceService::instance()->exportMaterialGroupList(params);
}

// Tab 4: Material
void TestResourceWindow::onBtnMatList()
{
    ResourceService::instance()->getMaterialList(1, 20, m_matGroupId->text(), m_matName->text());
}
void TestResourceWindow::onBtnMatDetail()
{
    ResourceService::instance()->getMaterialDetail(m_matId->text());
}
void TestResourceWindow::onBtnMatUpdate()
{
    ResourceService::instance()->updateMaterial(m_matId->text(), m_matName->text());
}
void TestResourceWindow::onBtnMatDelete()
{
    ResourceService::instance()->deleteMaterial(m_matId->text());
}
void TestResourceWindow::onBtnMatExport()
{
    QVariantMap params;
    if (!m_matName->text().isEmpty())
        params["mediaTitle"] = m_matName->text();
    ResourceService::instance()->exportMaterialList(params);
}
void TestResourceWindow::onBtnMatUpload()
{
    QString filePath = m_matFilePath->text();
    if (filePath.isEmpty())
        return;

    // 简单的自动判断逻辑：默认图片(2)，如果是常见视频后缀则为视频(1)
    QFileInfo info(filePath);
    QString suffix = info.suffix().toLower();
    int mediaType = 2;
    if (suffix == "mp4" || suffix == "avi" || suffix == "mov" || suffix == "mkv" || suffix == "wmv")
    {
        mediaType = 1;
    }

    // 传入计算出的 mediaType
    ResourceService::instance()->uploadMaterialFile(filePath, m_matGroupId->text(), mediaType);
}
// Logs
void TestResourceWindow::appendLog(QString const& opType, bool success, QString const& msg)
{
    QString t = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_logArea->append(QString("[%1] %2 %3: %4").arg(t, success ? "OK" : "FAIL", opType, msg));
}
void TestResourceWindow::appendDataLog(QString const& opType, QJsonObject const& data)
{
    m_logArea->append(
        QString("DATA [%1]: %2")
            .arg(opType, QString(QJsonDocument(data).toJson(QJsonDocument::Compact))));
}
