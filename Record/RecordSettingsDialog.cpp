// RecordSettingsDialog.cpp
#include "RecordSettingsDialog.h"
#include "AppConfig.h"
#include <QApplication>
#include <QScreen>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <QListView>
#include <QInputDialog>
#include <QWheelEvent>
#include "ScreenWindow.h"
using Microsoft::WRL::ComPtr;

// ============================================================================
// 内部辅助类：SafeComboBox
// 作用：解决鼠标悬停滚动时误触修改值的问题。
// 逻辑：只有当下拉框弹窗展开时，才响应滚轮事件；否则忽略。
// ============================================================================
class SafeComboBox : public QComboBox
{
public:
    explicit SafeComboBox(QWidget* parent = nullptr) : QComboBox(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
    }

protected:
    void wheelEvent(QWheelEvent* e) override
    {
        // 只有在下拉列表展开时才允许滚动修改
        if (view() && view()->isVisible())
        {
            QComboBox::wheelEvent(e);
        }
        else
        {
            e->ignore();  // 忽略事件，传递给父容器滚动页面
        }
    }
};

// ============================================================================
// 内部辅助类：SafeSpinBox
// 作用：解决鼠标悬停或选中输入框时，滚动滚轮导致数值改变的问题。
// 逻辑：彻底屏蔽滚轮修改数值的功能，只能通过点击按钮或键盘输入修改。
// ============================================================================
class SafeSpinBox : public QSpinBox
{
public:
    explicit SafeSpinBox(QWidget* parent = nullptr) : QSpinBox(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
    }

protected:
    void wheelEvent(QWheelEvent* e) override
    {
        // 彻底忽略滚轮事件，防止误触修改数值
        // 调用 ignore() 可以让事件冒泡给父级（即 ScrollArea），从而实现滚动页面而不是修改值
        e->ignore();
    }
};

// ============================================================================
// 辅助函数
// ============================================================================

int gcd(int a, int b)
{
    return b == 0 ? a : gcd(b, a % b);
}

void applyLabelStyle(QLabel* lbl)
{
    if (lbl)
    {
        lbl->setStyleSheet("color: #606266; font-size: 13px; background: transparent;");
    }
}
void applyComboStyle(QComboBox* cmb)
{
    if (!cmb)
        return;

    cmb->setView(new QListView());

    // [Fix] 更清晰的黑色向下箭头 (Base64 PNG)
    QString arrowIcon =
        "url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAABGdBTUEAALGPC/"
        "xhBQAAADhJREFUOBFjYBgFQAAM4/9/IOAD4jcgFgJiWjAAyM24DEY3GqcbkGwANm/"
        "iMwDZDfQMEA8h4wYGAAB6yQ5j230K+AAAAABJRU5ErkJggg==')";

    // 设置样式表：
    // 1. 增加 padding-right 为 30px，给按钮留足空间
    // 2. 给 ::drop-down 增加 border-left 和 background，使其看起来像个独立的按钮
    cmb->setStyleSheet(
        QString("QComboBox { "
                "   border: 1px solid #DCDFE6; "
                "   border-radius: 4px; "
                "   padding-left: 10px; "
                "   padding-right: 30px; " /* [关键] 必须大于 drop-down 的宽度 */
                "   min-height: 32px; "    /* 高度稍微加大，更易点击 */
                "   background: #FFFFFF; "
                "   color: #606266;"
                "   font-size: 13px;"
                "}"
                "QComboBox:hover { border-color: #C0C4CC; }"
                "QComboBox:focus { border-color: #409EFF; }"

                // --- 下拉按钮区域样式 ---
                "QComboBox::drop-down {"
                "   subcontrol-origin: padding;"
                "   subcontrol-position: top right;"
                "   width: 30px;"                    /* 按钮宽度 */
                "   border-left: 1px solid #DCDFE6;" /* [关键] 左侧分割线，区分输入区和按钮 */
                "   background-color: #F5F7FA;"      /* [关键] 浅灰色背景，突显按钮感 */
                "   border-top-right-radius: 4px;"
                "   border-bottom-right-radius: 4px;"
                "}"
                "QComboBox::drop-down:hover {"
                "   background-color: #E6E8EB;" /* 悬停变色 */
                "}"

                // --- 箭头图标样式 ---
                "QComboBox::down-arrow {"
                "   image: %1;"
                "   width: 14px;"
                "   height: 14px;"
                "   image: url(:/UI/QSS/icon/TeenyiconsDownSolid.png);"
                "}"
                // 如果箭头位置不正，可以用 margin 微调，例如: margin-top: 1px;

                // --- 下拉列表弹窗样式 ---
                "QComboBox QAbstractItemView {"
                "   border: 1px solid #E4E7ED;"
                "   selection-background-color: #F5F7FA;"
                "   selection-color: #409EFF;"
                "   background-color: #FFFFFF;"
                "   outline: none;"
                "   padding: 4px;"
                "}")
            .arg(arrowIcon));
}
void applyEditStyle(QLineEdit* edt)
{
    if (edt)
    {
        edt->setStyleSheet(
            "QLineEdit { border: 1px solid #DCDFE6; border-radius: 4px; padding: 4px; min-height: "
            "24px; background: #FFFFFF; }"
            "QLineEdit:focus { border-color: #409EFF; }");
    }
}
void applySpinStyle(QSpinBox* spn)
{
    if (spn)
    {
        spn->setStyleSheet(
            "QSpinBox { border: 1px solid #DCDFE6; border-radius: 4px; padding: 4px; min-height: "
            "24px; background: #FFFFFF; }"
            "QSpinBox:focus { border-color: #409EFF; }");
    }
}

// ============================================================================
// RecordSettingsDialog 实现
// ============================================================================

RecordSettingsDialog::RecordSettingsDialog(QWidget* parent) : UniversalDialog(parent)
{
    setTitle("推流与录制配置");
    setFixedSize(670, 480);

    setupUI();
    loadFromConfig();
    connect(AppConfig::instance(),
            &AppConfig::configChanged,
            this,
            &RecordSettingsDialog::loadFromConfig);
}

QWidget* RecordSettingsDialog::wrapInScrollArea(QWidget* contentWidget)
{
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidget(contentWidget);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // 修改点：强制设置 ScrollArea 背景为白色，确保统一
    scrollArea->setStyleSheet("QScrollArea { border: none; background-color: #FFFFFF; }");

    return scrollArea;
}

void RecordSettingsDialog::setupUI()
{
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setObjectName("RecordSettingsTab");

    m_tabWidget->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #E4E7ED; border-radius: 4px; top: -1px; background: "
        "#FFFFFF; }"
        "QTabBar::tab { background: #F5F7FA; color: #606266; padding: 8px 20px; border: 1px solid "
        "#E4E7ED; border-bottom: none; border-top-left-radius: 4px; border-top-right-radius: 4px; "
        "margin-right: 2px; }"
        "QTabBar::tab:selected { background: #FFFFFF; color: #409EFF; border-bottom: 1px solid "
        "#FFFFFF; font-weight: bold; }"
        "QTabBar::tab:hover { color: #409EFF; }");

    m_tabWidget->addTab(createVideoTab(), "视频 (Video)");
    m_tabWidget->addTab(createOutputTab(), "输出 (Output)");
    m_tabWidget->addTab(createAdvancedTab(), "高级 (Advanced)");
    m_tabWidget->addTab(createAITab(), "AI 分析 (AI Analysis)");

    this->setContentWidget(m_tabWidget);

    this->addButton("取消", "secondary", [this]() {
        this->reject();
        return true;
    });

    this->addButton("应用配置", "primary", [this]() {
        this->saveAndAccept();
        return false;
    });
}

QWidget* RecordSettingsDialog::createVideoTab()
{
    QWidget* w = new QWidget(this);
    // 修改点：显式设置背景为白色，防止透明导致的颜色不一致
    w->setStyleSheet("background-color: #FFFFFF;");

    QFormLayout* layout = new QFormLayout(w);
    layout->setLabelAlignment(Qt::AlignRight);
    layout->setContentsMargins(30, 25, 30, 25);
    layout->setSpacing(20);

    m_cmbBaseRes = new SafeComboBox(this);
    m_cmbBaseRes->setEditable(true);
    populateResList(m_cmbBaseRes);
    applyComboStyle(m_cmbBaseRes);
    // [新增] 默认选中 1920x1080，如果当前为空
    if (m_cmbBaseRes->currentText().isEmpty())
    {
        m_cmbBaseRes->setCurrentText("1920x1080");
    }
    m_cmbOutputRes = new SafeComboBox(this);
    m_cmbOutputRes->setEditable(true);
    populateResList(m_cmbOutputRes);
    applyComboStyle(m_cmbOutputRes);
    // [新增] 默认选中 1920x1080
    if (m_cmbOutputRes->currentText().isEmpty())
    {
        m_cmbOutputRes->setCurrentText("1920x1080");
    }
    connect(m_cmbOutputRes,
            &QComboBox::currentTextChanged,
            this,
            &RecordSettingsDialog::onResolutionChanged);
    connect(m_cmbBaseRes,
            &QComboBox::currentTextChanged,
            this,
            &RecordSettingsDialog::onResolutionChanged);

    m_lblAspectRatio = new QLabel("Aspect Ratio: -", this);
    applyLabelStyle(m_lblAspectRatio);

    m_cmbDownscaleFilter = new SafeComboBox(this);
    m_cmbDownscaleFilter->addItem("Bilinear (Fastest)", (int)DownscaleFilter::Bilinear);
    m_cmbDownscaleFilter->addItem("Bicubic (Sharpened)", (int)DownscaleFilter::Bicubic);
    m_cmbDownscaleFilter->addItem("Lanczos (Best Detail)", (int)DownscaleFilter::Lanczos);
    applyComboStyle(m_cmbDownscaleFilter);

    m_cmbFps = new SafeComboBox(this);
    populateFPS(m_cmbFps);
    applyComboStyle(m_cmbFps);

    auto addRow = [&](QString label, QWidget* field) {
        QLabel* lbl = new QLabel(label);
        applyLabelStyle(lbl);
        layout->addRow(lbl, field);
    };

    addRow("基础分辨率 (Canvas):", m_cmbBaseRes);
    addRow("输出分辨率 (Scaled):", m_cmbOutputRes);
    addRow("纵横比:", m_lblAspectRatio);
    addRow("帧率 (FPS):", m_cmbFps);
    addRow("缩放过滤器:", m_cmbDownscaleFilter);

    // 直接返回 w，保持第一页无滚动条
    return w;
}

QWidget* RecordSettingsDialog::createOutputTab()
{
    QWidget* widget = new QWidget();
    // 修改点：显式设置背景为白色，解决 ScrollArea 内部可能显示灰色的问题
    widget->setStyleSheet("background-color: #FFFFFF;");

    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(20);

    // --- Streaming Group ---
    QGroupBox* grpStream = new QGroupBox("直播推流");
    grpStream->setStyleSheet(
        "QGroupBox { font-weight: bold; border: 1px solid #DCDFE6; border-radius: 4px; margin-top: "
        "10px; padding-top: 10px; background-color: #FFFFFF; } QGroupBox::title { "
        "subcontrol-origin: margin; left: 10px; padding: 0 3px; background-color: #FFFFFF; }");
    QVBoxLayout* streamLayout = new QVBoxLayout(grpStream);
    streamLayout->setSpacing(12);
    streamLayout->setContentsMargins(15, 15, 15, 15);

    m_chkEnableStream = new QCheckBox("启用推流功能");
    m_chkEnableStream->setChecked(true);
    m_chkEnableStream->setEnabled(false);
    m_chkEnableStream->setToolTip("直播模式下必须开启推流");
    streamLayout->addWidget(m_chkEnableStream);

    m_listPushTargets = new QListWidget();
    m_listPushTargets->setStyleSheet(
        "QListWidget { border: 1px solid #DCDFE6; border-radius: 4px; background: #FFFFFF; } "
        "QListWidget::item { padding: 5px; } QListWidget::item:selected { background: #ECF5FF; "
        "color: #409EFF; }");
    m_listPushTargets->setFixedHeight(100);
    connect(m_listPushTargets,
            &QListWidget::itemDoubleClicked,
            this,
            &RecordSettingsDialog::onEditPushTarget);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(10);

    auto createSmallBtn = [](QString const& txt) {
        QPushButton* btn = new QPushButton(txt);
        btn->setFixedSize(60, 26);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton { background: #FFFFFF; border: 1px solid #DCDFE6; border-radius: 3px; "
            "font-size: 12px; } QPushButton:hover { color: #409EFF; border-color: #409EFF; }");
        return btn;
    };

    m_btnAddTarget = createSmallBtn("添加");
    m_btnEditTarget = createSmallBtn("编辑");
    m_btnDelTarget = createSmallBtn("删除");

    connect(m_btnAddTarget, &QPushButton::clicked, this, &RecordSettingsDialog::onAddPushTarget);
    connect(m_btnEditTarget, &QPushButton::clicked, this, &RecordSettingsDialog::onEditPushTarget);
    connect(m_btnDelTarget, &QPushButton::clicked, this, &RecordSettingsDialog::onRemovePushTarget);

    btnLayout->addWidget(m_btnAddTarget);
    btnLayout->addWidget(m_btnEditTarget);
    btnLayout->addWidget(m_btnDelTarget);
    btnLayout->addStretch();

    streamLayout->addWidget(new QLabel("推流地址列表 (支持多路):"));
    streamLayout->addWidget(m_listPushTargets);
    streamLayout->addLayout(btnLayout);

    QFormLayout* encLayout = new QFormLayout();
    encLayout->setLabelAlignment(Qt::AlignRight);

    m_cmbStreamEncoder = new SafeComboBox();
    populateEncoders(m_cmbStreamEncoder);
    applyComboStyle(m_cmbStreamEncoder);
    m_spnStreamBitrate = new SafeSpinBox();
    m_spnStreamBitrate->setRange(500, 20000);
    m_spnStreamBitrate->setSuffix(" Kbps");
    m_spnStreamBitrate->setSingleStep(500);
    applySpinStyle(m_spnStreamBitrate);

    auto addEncRow = [&](QString label, QWidget* field) {
        QLabel* lbl = new QLabel(label);
        applyLabelStyle(lbl);
        encLayout->addRow(lbl, field);
    };
    addEncRow("编码器:", m_cmbStreamEncoder);
    addEncRow("视频码率:", m_spnStreamBitrate);
    streamLayout->addLayout(encLayout);

    // --- Recording Group ---
    QGroupBox* grpRec = new QGroupBox("本地录制");
    grpRec->setStyleSheet(grpStream->styleSheet());
    QFormLayout* recLayout = new QFormLayout(grpRec);
    recLayout->setSpacing(10);
    recLayout->setContentsMargins(15, 15, 15, 15);
    recLayout->setLabelAlignment(Qt::AlignRight);

    m_chkEnableRecord = new QCheckBox("录制文件是否保留");

    QHBoxLayout* pathLayout = new QHBoxLayout();
    m_edtSavePath = new QLineEdit();
    applyEditStyle(m_edtSavePath);
    QPushButton* btnBrowse = new QPushButton("...");
    btnBrowse->setFixedSize(30, 26);
    btnBrowse->setStyleSheet(
        "QPushButton { border: 1px solid #DCDFE6; border-radius: 3px; background: #FFFFFF; } "
        "QPushButton:hover { border-color: #409EFF; }");
    connect(btnBrowse, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getSaveFileName(
            this,
            "Save Video",
            QStandardPaths::writableLocation(QStandardPaths::MoviesLocation),
            "MP4 Files (*.mp4);;MKV Files (*.mkv);;FLV Files (*.flv)");
        if (!path.isEmpty())
            m_edtSavePath->setText(path);
    });
    pathLayout->addWidget(m_edtSavePath);
    pathLayout->addWidget(btnBrowse);

    m_cmbRecFormat = new SafeComboBox();
    m_cmbRecFormat->addItems({"mp4", "mkv", "flv", "mov"});
    applyComboStyle(m_cmbRecFormat);
    // [新增] 添加信号连接：当格式改变时，自动更新路径后缀
    connect(m_cmbRecFormat, &QComboBox::currentTextChanged, this, [this](QString const& format) {
        QString path = m_edtSavePath->text();
        if (path.isEmpty())
            return;

        QFileInfo fi(path);
        QString newExt = format.toLower();
        QString currentExt = fi.suffix().toLower();

        // 只有当后缀不一致时才修改
        if (currentExt != newExt)
        {
            QString newPath;
            if (currentExt.isEmpty())
            {
                newPath = path + "." + newExt;
            }
            else
            {
                newPath = path.left(path.lastIndexOf('.')) + "." + newExt;
            }
            m_edtSavePath->setText(newPath);
        }
    });
    m_cmbRecEncoder = new SafeComboBox();
    populateEncoders(m_cmbRecEncoder);
    applyComboStyle(m_cmbRecEncoder);
    m_spnRecBitrate = new SafeSpinBox();
    m_spnRecBitrate->setRange(1000, 50000);
    m_spnRecBitrate->setSuffix(" Kbps");
    m_spnRecBitrate->setSingleStep(1000);
    applySpinStyle(m_spnRecBitrate);

    recLayout->addRow(m_chkEnableRecord);

    auto addRecRow = [&](QString label, QWidget* field) {
        QLabel* lbl = new QLabel(label);
        applyLabelStyle(lbl);
        recLayout->addRow(lbl, field);
    };
    auto addRecLayout = [&](QString label, QLayout* l) {
        QLabel* lbl = new QLabel(label);
        applyLabelStyle(lbl);
        recLayout->addRow(lbl, l);
    };

    addRecLayout("保存路径:", pathLayout);
    addRecRow("录制格式:", m_cmbRecFormat);
    addRecRow("编码器:", m_cmbRecEncoder);
    addRecRow("视频码率:", m_spnRecBitrate);

    layout->addWidget(grpStream);
    layout->addWidget(grpRec);
    layout->addStretch();

    // [New] 定义控件 (需在头文件声明或此处定义)
    m_chkAutoSplit = new QCheckBox("启用自动分片 (Auto Split)");
    m_spnSplitTime = new SafeSpinBox();
    m_spnSplitTime->setRange(1, 1440);  // 1分钟 ~ 24小时
    m_spnSplitTime->setSuffix(" Min");
    m_spnSplitTime->setValue(60);

    // 添加到布局
    recLayout->addRow(m_chkAutoSplit);
    QHBoxLayout* splitLayout = new QHBoxLayout();
    splitLayout->addWidget(new QLabel("分片间隔:"));
    splitLayout->addWidget(m_spnSplitTime);
    splitLayout->addStretch();

    // 简单的联动逻辑：未启用时禁用时间输入
    connect(m_chkAutoSplit, &QCheckBox::toggled, m_spnSplitTime, &QSpinBox::setEnabled);
    m_spnSplitTime->setEnabled(false);  // 默认状态
    recLayout->addRow(splitLayout);

    // 此页面内容较长，保留 ScrollArea
    return wrapInScrollArea(widget);
}

QWidget* RecordSettingsDialog::createAdvancedTab()
{
    QWidget* widget = new QWidget();
    // 修改点：显式设置背景为白色
    widget->setStyleSheet("background-color: #FFFFFF;");

    QFormLayout* layout = new QFormLayout(widget);
    layout->setContentsMargins(30, 25, 30, 25);
    layout->setSpacing(15);
    layout->setLabelAlignment(Qt::AlignRight);

    auto addSection = [&](QString text) {
        QLabel* lbl = new QLabel(text);
        lbl->setStyleSheet(
            "font-weight: bold; font-size: 14px; margin-top: 10px; margin-bottom: 5px; color: "
            "#303133;");
        layout->addRow(lbl);
    };

    auto addRow = [&](QString label, QWidget* field) {
        QLabel* lbl = new QLabel(label);
        applyLabelStyle(lbl);
        layout->addRow(lbl, field);
    };

    addSection("Audio Settings");
    m_chkMixMic = new QCheckBox("混音: 麦克风 (Microphone)");
    m_chkMixSys = new QCheckBox("混音: 系统声音 (Loopback)");
    m_spnAudioBitrate = new SafeSpinBox();
    m_spnAudioBitrate->setRange(64, 320);
    m_spnAudioBitrate->setSuffix(" Kbps");
    m_spnAudioBitrate->setValue(128);
    applySpinStyle(m_spnAudioBitrate);

    layout->addRow(m_chkMixMic);
    layout->addRow(m_chkMixSys);
    addRow("音频码率:", m_spnAudioBitrate);

    // UI Settings
    addSection("Live UI Settings");
    m_chkShowViewers = new QCheckBox("显示在线观众列表面板");
    m_chkShowMessages = new QCheckBox("显示互动消息列表面板");
    layout->addRow(m_chkShowViewers);
    layout->addRow(m_chkShowMessages);

    addSection("Advanced Encoder");
    m_cmbColorSpace = new SafeComboBox();
    m_cmbColorSpace->addItems({"Default", "601", "709"});
    applyComboStyle(m_cmbColorSpace);
    m_cmbColorRange = new SafeComboBox();
    m_cmbColorRange->addItems({"Default", "Partial", "Full"});
    applyComboStyle(m_cmbColorRange);
    m_cmbGPU = new SafeComboBox();
    m_cmbGPU->addItems({"GPU 0", "GPU 1"});
    applyComboStyle(m_cmbGPU);

    m_cmbStreamRateControl = new SafeComboBox();
    m_cmbStreamRateControl->addItem("CBR (Constant)", (int)RateControlMode::CBR);
    m_cmbStreamRateControl->addItem("VBR (Variable)", (int)RateControlMode::VBR);
    applyComboStyle(m_cmbStreamRateControl);
    m_cmbStreamPreset = new SafeComboBox();
    m_cmbStreamPreset->addItems({"veryfast", "faster", "fast", "medium", "slow"});
    applyComboStyle(m_cmbStreamPreset);
    m_cmbRecRateControl = new SafeComboBox();
    m_cmbRecRateControl->addItem("CBR", (int)RateControlMode::CBR);
    m_cmbRecRateControl->addItem("VBR", (int)RateControlMode::VBR);
    m_cmbRecRateControl->addItem("CQP", (int)RateControlMode::CQP);
    applyComboStyle(m_cmbRecRateControl);
    m_spnRecQuality = new SafeSpinBox();
    m_spnRecQuality->setRange(0, 51);
    m_spnRecQuality->setValue(23);
    applySpinStyle(m_spnRecQuality);
    m_spnKeyframeInt = new SafeSpinBox();
    m_spnKeyframeInt->setRange(1, 10);
    m_spnKeyframeInt->setSuffix(" Sec");
    applySpinStyle(m_spnKeyframeInt);

    addRow("推流流控:", m_cmbStreamRateControl);
    addRow("推流预设:", m_cmbStreamPreset);
    addRow("录制流控:", m_cmbRecRateControl);
    addRow("录制质量 (CQP):", m_spnRecQuality);
    addRow("关键帧间隔:", m_spnKeyframeInt);

    // 此页面内容较长，保留 ScrollArea
    return wrapInScrollArea(widget);
}

QWidget* RecordSettingsDialog::createAITab()
{
    QWidget* widget = new QWidget();
    widget->setStyleSheet("background-color: #FFFFFF;");

    QFormLayout* layout = new QFormLayout(widget);
    layout->setContentsMargins(30, 25, 30, 25);
    layout->setSpacing(15);
    layout->setLabelAlignment(Qt::AlignRight);

    auto addRow = [&](QString label, QWidget* field) {
        QLabel* lbl = new QLabel(label);
        applyLabelStyle(lbl);
        layout->addRow(lbl, field);
    };

    m_chkEnableAI = new QCheckBox("启用 AI 数据采集 (旁路)");

    m_spnAIFps = new SafeSpinBox();
    m_spnAIFps->setRange(1, 30);
    m_spnAIFps->setSuffix(" FPS");
    m_spnAIFps->setValue(1);
    applySpinStyle(m_spnAIFps);

    m_cmbAIStorageMode = new SafeComboBox();
    m_cmbAIStorageMode->addItem("单帧独立文件 (Separate Files)", 0);  // 策略1
    m_cmbAIStorageMode->addItem("合并为单文件 (Single Stream)", 1);   // 策略2
    m_cmbAIStorageMode->addItem("按秒分片存储 (Split By Second)", 2);
    applyComboStyle(m_cmbAIStorageMode);

    // [New] 创建下拉框
    m_cmbAIAudioSplit = new SafeComboBox();
    m_cmbAIAudioSplit->addItem("1 秒 (1s Chunk)", 1);
    m_cmbAIAudioSplit->addItem("2 秒 (2s Chunk)", 2);
    m_cmbAIAudioSplit->addItem("3 秒 (3s Chunk)", 3);
    m_cmbAIAudioSplit->addItem("5 秒 (5s Chunk)", 5);  // 额外增加一个选项
    applyComboStyle(m_cmbAIAudioSplit);

    layout->addRow(m_chkEnableAI);
    addRow("视频采样率:", m_spnAIFps);
    addRow("YUV存储策略:", m_cmbAIStorageMode);
    addRow("音频分片时长:", m_cmbAIAudioSplit);

    // 简单的联动：未开启时禁用下方选项
    connect(m_chkEnableAI, &QCheckBox::toggled, m_spnAIFps, &QWidget::setEnabled);
    connect(m_chkEnableAI, &QCheckBox::toggled, m_cmbAIStorageMode, &QWidget::setEnabled);
    connect(m_chkEnableAI, &QCheckBox::toggled, m_cmbAIAudioSplit, &QWidget::setEnabled);

    return wrapInScrollArea(widget);
}

void RecordSettingsDialog::populateResList(QComboBox* box)
{
    if (!box)
        return;

    box->clear();

    // 定义常用分辨率列表
    // 注意：保持格式为 "宽x高"，这样 getUIConfig 里的 split('x') 逻辑依然有效
    QStringList resList = {
        // --- 16:9 主流横屏 ---
        "1920x1080",  // FHD (最常用)
        "1280x720",   // HD
        "2560x1440",  // 2K
        "3840x2160",  // 4K

        // --- 16:10 办公/Mac ---
        "1920x1200",
        "2560x1600",

        // --- 老式/笔记本 ---
        "1366x768",
        "1600x900",
        "1024x768",

        // --- 9:16 手机竖屏 (直播/短视频) ---
        "1080x1920",
        "720x1280",

        // --- 特殊比例 ---
        "1080x1080"  // 正方形
    };

    box->addItems(resList);

    // 设置最大可见项数，防止列表太长遮挡界面
    box->setMaxVisibleItems(10);
}
void RecordSettingsDialog::populateFPS(QComboBox* box)
{
    box->addItem("30");
    box->addItem("60");
    box->addItem("24");
    box->addItem("25");
    box->addItem("50");
}
void RecordSettingsDialog::populateEncoders(QComboBox* box)
{
    box->addItem("Software (x264)", "libx264");
    box->addItem("Hardware (NVIDIA NVENC)", "h264_nvenc");
    box->addItem("Hardware (AMD AMF)", "h264_amf");
    box->addItem("Hardware (Intel QSV)", "h264_qsv");
}

// [文件] RecordSettingsDialog.cpp

void RecordSettingsDialog::loadFromConfig()
{
    // 增加防重入判断，避免死循环
    static bool isUpdating = false;
    if (isUpdating)
        return;
    isUpdating = true;

    RecorderConfig c = AppConfig::instance()->getRecorderConfig();

    m_chkEnableAI->setChecked(c.ai.enable);
    m_spnAIFps->setValue(c.ai.sampleFps);
    int modeIdx = c.ai.storageMode;
    if (modeIdx < 0 || modeIdx >= m_cmbAIStorageMode->count())
        modeIdx = 2;  // UI容错默认设为2
    m_cmbAIStorageMode->setCurrentIndex(modeIdx);

    // [New] 加载音频分片设置
    int duration = c.ai.audioSplitDuration;
    int idx = m_cmbAIAudioSplit->findData(duration);
    if (idx >= 0)
        m_cmbAIAudioSplit->setCurrentIndex(idx);
    else
        m_cmbAIAudioSplit->setCurrentIndex(0);  // 默认 1s

    m_cmbAIAudioSplit->setEnabled(c.ai.enable);

    m_spnAIFps->setEnabled(c.ai.enable);
    m_cmbAIStorageMode->setEnabled(c.ai.enable);
    m_chkAutoSplit->setChecked(c.recording.autoFileSplit);
    m_spnSplitTime->setValue(c.recording.fileSplitDurationMin);
    m_chkEnableRecord->setChecked(c.recording.keepFile);

    // 详细审计
    qInfo() << "[UI_AUDIT] RE-LOADING UI. Targets in memory:" << c.streaming.pushTargets.size();

    m_tempPushTargets = c.streaming.pushTargets;
    refreshPushList();

    isUpdating = false;
}
// RecordSettingsDialog.cpp 修改处

void RecordSettingsDialog::refreshPushList()
{
    qInfo() << "[UI_AUDIT] refreshPushList executing. Item count to render:"
            << m_tempPushTargets.size();
    m_listPushTargets->clear();

    // 判断当前是否处于直播运行模式
    bool isLiveMode = false;
    // 逻辑：如果父窗口是 ScreenWindow 且处于直播模式，则锁定列表
    if (QWidget* p = this->parentWidget())
    {
        if (auto sw = qobject_cast<ScreenWindow*>(p))
        {
            isLiveMode = (sw->windowMode() == ScreenWindow::WindowMode::LiveStreaming);
        }
    }
    if (!m_tempPushTargets.isEmpty())
    {
        for (auto const& t : m_tempPushTargets)
        {
            QListWidgetItem* item = new QListWidgetItem(t.url);
            if (isLiveMode)
            {
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);  // Read-only
                item->setForeground(QColor("#606266"));                    // Normal color
            }
            else
            {
                item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                item->setCheckState(t.enable ? Qt::Checked : Qt::Unchecked);
            }
            m_listPushTargets->addItem(item);
        }
    }

    // 云端数据下发后，禁止在配置界面手动增删
    if (isLiveMode)
    {
        m_btnAddTarget->hide();
        m_btnEditTarget->hide();
        m_btnDelTarget->hide();
    }
    else
    {
        m_btnAddTarget->show();
        m_btnEditTarget->show();
        m_btnDelTarget->show();
    }
}

void RecordSettingsDialog::onAddPushTarget()
{
    UniversalDialog dlg(this);
    dlg.setTitle("添加推流地址");

    QWidget* content = new QWidget();
    QFormLayout* layout = new QFormLayout(content);
    QLineEdit* edtUrl = new QLineEdit();
    edtUrl->setPlaceholderText("rtmp://...");
    applyEditStyle(edtUrl);
    QLineEdit* edtKey = new QLineEdit();
    edtKey->setPlaceholderText("(可选) 流密钥");
    applyEditStyle(edtKey);
    layout->addRow("服务器 URL:", edtUrl);
    layout->addRow("串流密钥:", edtKey);

    dlg.setContentWidget(content);
    dlg.addButton("取消");
    dlg.addButton("确定", "primary", [&]() {
        if (edtUrl->text().trimmed().isEmpty())
            return false;
        PushTarget t;
        t.url = edtUrl->text().trimmed();
        t.key = edtKey->text().trimmed();
        t.enable = true;
        m_tempPushTargets.append(t);
        refreshPushList();
        return true;
    });
    dlg.exec();
}

void RecordSettingsDialog::onEditPushTarget()
{
    int row = m_listPushTargets->currentRow();
    if (row < 0 || row >= m_tempPushTargets.size())
        return;

    PushTarget& t = m_tempPushTargets[row];
    UniversalDialog dlg(this);
    dlg.setTitle("编辑推流地址");

    QWidget* content = new QWidget();
    QFormLayout* layout = new QFormLayout(content);
    QLineEdit* edtUrl = new QLineEdit(t.url);
    applyEditStyle(edtUrl);
    QLineEdit* edtKey = new QLineEdit(t.key);
    applyEditStyle(edtKey);
    layout->addRow("服务器 URL:", edtUrl);
    layout->addRow("串流密钥:", edtKey);

    dlg.setContentWidget(content);
    dlg.addButton("取消");
    dlg.addButton("确定", "primary", [&]() {
        if (edtUrl->text().trimmed().isEmpty())
            return false;
        t.url = edtUrl->text().trimmed();
        t.key = edtKey->text().trimmed();
        refreshPushList();
        return true;
    });
    dlg.exec();
}

void RecordSettingsDialog::onRemovePushTarget()
{
    int row = m_listPushTargets->currentRow();
    if (row >= 0 && row < m_tempPushTargets.size())
    {
        m_tempPushTargets.removeAt(row);
        refreshPushList();
    }
}

void RecordSettingsDialog::saveAndAccept()
{
    RecorderConfig c = getUIConfig();

    AppConfig::instance()->setVideoBase(c.base);
    AppConfig::instance()->setStreaming(c.streaming);
    AppConfig::instance()->setRecording(c.recording);
    AppConfig::instance()->setAudio(c.audio);
    AppConfig::instance()->setAIConfig(c.ai);

    // UI Panel Settings
    AppConfig::instance()->setShowViewerPanel(c.showViewerPanel);
    AppConfig::instance()->setShowMessagePanel(c.showMessagePanel);
    AppConfig::instance()->saveConfig();
    accept();
}

RecorderConfig RecordSettingsDialog::getUIConfig() const
{
    RecorderConfig c;

    auto parseRes = [](QString const& s) {
        QStringList parts = s.split('x');
        if (parts.size() == 2)
            return QSize(parts[0].toInt(), parts[1].toInt());
        return QSize(1920, 1080);
    };

    // Video
    c.base.baseResolution = parseRes(m_cmbBaseRes->currentText());
    c.base.outputResolution = parseRes(m_cmbOutputRes->currentText());
    c.base.fps = m_cmbFps->currentText().toInt();
    c.base.downscaleFilter = (DownscaleFilter)m_cmbDownscaleFilter->currentData().toInt();

    // Streaming
    c.streaming.enable = true;

    // 1. 获取当前选中的格式 (mp4, mkv, flv, mov)
    QString selectedFormat = m_cmbRecFormat->currentText().toLower();

    // 2. 获取当前的路径
    QString rawPath = m_edtSavePath->text();

    // 3. [关键修复] 强制修正文件后缀名
    if (!rawPath.isEmpty())
    {
        QFileInfo fi(rawPath);
        QString currentExt = fi.suffix().toLower();

        // 如果当前后缀与选中格式不一致，或者没有后缀
        if (currentExt != selectedFormat)
        {
            // 如果原来没有后缀，直接追加
            if (currentExt.isEmpty())
            {
                rawPath += "." + selectedFormat;
            }
            // 如果原来有后缀，替换掉
            else
            {
                // left 截取到最后一个点之前，然后拼接新后缀
                rawPath = rawPath.left(rawPath.lastIndexOf('.')) + "." + selectedFormat;
            }
        }
    }

    c.recording.pathOrUrl = rawPath;      // 存入修正后的路径
    c.recording.format = selectedFormat;  // 存入格式字符串 (虽然底层主要看后缀，但存下来保持一致)

    c.streaming.pushTargets.clear();
    if (!m_tempPushTargets.isEmpty())
    {
        for (int i = 0; i < m_listPushTargets->count(); ++i)
        {
            // 双重保险：防止索引越界
            if (i >= m_tempPushTargets.size())
                break;

            PushTarget t = m_tempPushTargets[i];

            // 注意：占位符 Item 可能没有 CheckBox，但因为这里有 isEmpty 判断，逻辑是安全的
            QListWidgetItem* uiItem = m_listPushTargets->item(i);
            if (uiItem)
            {
                t.enable = (uiItem->checkState() == Qt::Checked);
            }
            c.streaming.pushTargets.append(t);
        }
    }

    c.streaming.encoderName = m_cmbStreamEncoder->currentData().toString();
    c.streaming.rateControl = (RateControlMode)m_cmbStreamRateControl->currentData().toInt();
    c.streaming.bitrateKbps = m_spnStreamBitrate->value();
    c.streaming.keyframeIntervalSec = m_spnKeyframeInt->value();
    c.streaming.preset = m_cmbStreamPreset->currentData().toString();

    // Recording
    c.recording.enable = true;
    c.recording.pathOrUrl = m_edtSavePath->text();
    c.recording.format = m_cmbRecFormat->currentText();
    c.recording.encoderName = m_cmbRecEncoder->currentData().toString();
    c.recording.rateControl = (RateControlMode)m_cmbRecRateControl->currentData().toInt();
    c.recording.qualityLevel = m_spnRecQuality->value();
    c.recording.bitrateKbps = m_spnRecBitrate->value();
    c.recording.autoFileSplit = m_chkAutoSplit->isChecked();
    c.recording.fileSplitDurationMin = m_spnSplitTime->value();
    c.recording.keepFile = m_chkEnableRecord->isChecked();

    // Audio
    c.mixMicrophone = m_chkMixMic->isChecked();
    c.mixSystemAudio = m_chkMixSys->isChecked();
    c.audio.bitrate = m_spnAudioBitrate->value() * 1000;

    c.showViewerPanel = m_chkShowViewers->isChecked();
    c.showMessagePanel = m_chkShowMessages->isChecked();

    // [New] Save AI Config
    c.ai.enable = m_chkEnableAI->isChecked();
    c.ai.sampleFps = m_spnAIFps->value();
    c.ai.storageMode = m_cmbAIStorageMode->currentIndex();
    c.ai.audioSplitDuration = m_cmbAIAudioSplit->currentData().toInt();
    return c;
}

void RecordSettingsDialog::onResolutionChanged()
{
    QString txt = m_cmbOutputRes->currentText();
    QStringList parts = txt.split("x");
    if (parts.size() == 2)
    {
        int w = parts[0].toInt();
        int h = parts[1].toInt();
        int d = gcd(w, h);
        if (d > 0)
            m_lblAspectRatio->setText(QString("%1:%2").arg(w / d).arg(h / d));
    }
}
