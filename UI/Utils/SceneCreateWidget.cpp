#include "SceneCreateWidget.h"
#include "UniversalDialog.h"
#include "skinmgr.h"
#include "ResourceService.h"
#include "PermissionGuard.h"
#include <QListView>
#include <QTimer>
#include <QDebug>
#include <QStyle>
#include <QEvent>
#include <QFileDialog>  // [New]
#include <QFileInfo>    // [New]
#include <QPixmap>      // [New]
#include <QMouseEvent>  // [New]

// ... [Constructor & SetupLayout remain same] ...
SceneCreateWidget::SceneCreateWidget(QWidget* parent) : QWidget(parent)
{
    SkinMgr::inst()->setSkin(SK_SceneCreate, this);
    setupLayout();
}

SceneCreateWidget::~SceneCreateWidget()
{
}

void SceneCreateWidget::setupLayout()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    // [UI 微调] 保持边距，但不再过大：左右 40，上下 25
    mainLayout->setContentsMargins(40, 25, 40, 25);
    mainLayout->setSpacing(5);  // [UI 微调] 减小主布局内部元素间距 (10 -> 5)

    // ============================================================
    // 1. 表单区域 (场景名称、类型、分类)
    // ============================================================
    QWidget* formContainer = new QWidget(this);
    QGridLayout* grid = new QGridLayout(formContainer);
    grid->setVerticalSpacing(18);  // [UI 微调] 行间距适中 (20 -> 18)
    grid->setHorizontalSpacing(12);
    grid->setContentsMargins(0, 0, 0, 0);

    int row = 0;
    auto addRow = [&](QString title, QWidget* w) {
        // ... (addRow 内部逻辑保持不变) ...
        QLabel* lbl = new QLabel(title, this);
        lbl->setProperty("class", "form_label");
        lbl->setFixedSize(80, 34);
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        w->setProperty("class", "form_input");
        w->setFixedHeight(34);
        w->installEventFilter(this);
        if (qobject_cast<QComboBox*>(w))
        {
            w->setFocusPolicy(Qt::StrongFocus);
            w->setAttribute(Qt::WA_Hover);
        }
        grid->addWidget(lbl, row, 0);
        grid->addWidget(w, row, 1);
        row++;
    };

    // 初始化控件
    m_editName = new QLineEdit(this);
    m_editName->setPlaceholderText("请输入场景名称");
    addRow("场景名称", m_editName);

    m_comboType = new QComboBox(this);
    QListView* typeView = new QListView(m_comboType);
    m_comboType->setView(typeView);
    m_comboType->addItem("通用横屏 (16:9)", (int)SceneCategoryType::General);
    m_comboType->addItem("直播教学 (Live)", (int)SceneCategoryType::LiveCourse);
    m_comboType->addItem("游戏直播 (Game)", (int)SceneCategoryType::Gaming);
    m_comboType->addItem("手机竖屏 (9:16)", (int)SceneCategoryType::PortraitMobile);
    addRow("场景类型", m_comboType);

    m_comboCat = new QComboBox(this);
    QListView* catView = new QListView(m_comboCat);
    m_comboCat->setView(catView);
    m_comboCat->addItems(SceneManager::instance().getUniqueCategories());
    addRow("所属分类", m_comboCat);

    m_editNewCat = new QLineEdit(this);
    m_editNewCat->setPlaceholderText("（可选）输入新分类名称");
    addRow("新建分类", m_editNewCat);

    // 将表单添加到主布局
    mainLayout->addWidget(formContainer);

    // ============================================================
    // 2. 间距调整
    // ============================================================
    // [UI 微调] 减小表单与封面之间的距离 (25 -> 15)，让视觉更连贯
    mainLayout->addSpacing(15);

    // ============================================================
    // 3. 封面上传区域
    // ============================================================
    QHBoxLayout* coverLayout = new QHBoxLayout();
    coverLayout->setContentsMargins(0, 0, 0, 0);

    // 左侧弹簧：将内容推向中间
    coverLayout->addStretch(1);

    // 图片容器
    m_lblCoverPreview = new QLabel(this);
    m_lblCoverPreview->setFixedSize(110, 110);
    m_lblCoverPreview->setAlignment(Qt::AlignCenter);
    m_lblCoverPreview->setCursor(Qt::PointingHandCursor);
    m_lblCoverPreview->setText("+");
    m_lblCoverPreview->setObjectName("CoverPreview");

    // ... (样式表保持不变) ...
    m_lblCoverPreview->setStyleSheet(
        "QLabel#CoverPreview {"
        "  border: 2px dashed #DCDFE6;"
        "  border-radius: 6px;"
        "  background-color: #F5F7FA;"
        "  color: #909399;"
        "  font-size: 30px;"
        "  font-weight: bold;"
        "}"
        "QLabel#CoverPreview:hover {"
        "  border-color: #409EFF;"
        "  color: #409EFF;"
        "  background-color: #ECF5FF;"
        "}");
    m_lblCoverPreview->installEventFilter(this);

    coverLayout->addWidget(m_lblCoverPreview);

    // 文字提示 (垂直布局)
    QVBoxLayout* coverHintLayout = new QVBoxLayout();
    coverHintLayout->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    coverHintLayout->setSpacing(4);  // [UI 微调] 减小文字行间距 (6 -> 4)

    QLabel* lblHint1 = new QLabel("设置封面", this);
    lblHint1->setStyleSheet("font-size: 14px; font-weight: bold; color: #303133;");

    QLabel* lblHint2 = new QLabel("支持 JPG/PNG\n小于 5MB", this);
    lblHint2->setStyleSheet(
        "font-size: 12px; color: #909399; line-height: 14px;");  // 增加 line-height 控制

    coverHintLayout->addWidget(lblHint1);
    coverHintLayout->addWidget(lblHint2);

    // [UI 微调] 减小图片和文字之间的间距 (20 -> 15)
    coverLayout->addSpacing(15);
    coverLayout->addLayout(coverHintLayout);

    // 右侧弹簧：将内容推向中间
    coverLayout->addStretch(1);

    // 将封面布局添加到主布局
    mainLayout->addLayout(coverLayout);

    // [UI 微调] 底部留白极小，避免与按钮距离过远
    // 移除之前的 addSpacing(10) 和 addStretch(1)
    // 改为使用 addStretch(0) 仅作弹性填充，但如果空间足够，它不会撑开太多
    mainLayout->addStretch(1);
}

bool SceneCreateWidget::eventFilter(QObject* watched, QEvent* event)
{
    // [New] 处理封面点击
    if (watched == m_lblCoverPreview && event->type() == QEvent::MouseButtonRelease)
    {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {
            onSelectCover();
            return true;
        }
    }

    if (event->type() == QEvent::FocusIn)
    {
        QWidget* w = qobject_cast<QWidget*>(watched);
        if (w)
        {
            QVariant classProp = w->property("class");
            if (classProp.isValid() && classProp.toString() == "input_error")
            {
                setInputErrorState(w, false);
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

// [New] 封面选择逻辑
void SceneCreateWidget::onSelectCover()
{
    QString path =
        QFileDialog::getOpenFileName(this, "选择封面图片", "", "Images (*.png *.jpg *.jpeg *.bmp)");
    if (path.isEmpty())
        return;

    QFileInfo info(path);
    // 1. 大小校验 (5MB)
    if (info.size() > 5 * 1024 * 1024)
    {
        UniversalDialog::showError(this->window(), "图片过大", "请选择小于 5MB 的图片文件。");
        return;
    }

    // 2. 加载与显示
    QPixmap pix;
    if (pix.load(path))
    {
        // 保存路径
        m_selectedCoverPath = path;

        // 缩放并显示 (保持比例填满，可能会裁剪)
        QPixmap scaled = pix.scaled(
            m_lblCoverPreview->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);

        // 居中裁剪到正方形
        int w = m_lblCoverPreview->width();
        int h = m_lblCoverPreview->height();
        QPixmap cropped = scaled.copy((scaled.width() - w) / 2, (scaled.height() - h) / 2, w, h);

        m_lblCoverPreview->setPixmap(cropped);

        // 移除虚线边框，保留圆角，防止背景色干扰
        m_lblCoverPreview->setStyleSheet(
            "QLabel#CoverPreview {"
            "  border: 1px solid #DCDFE6;"
            "  border-radius: 6px;"
            "  padding: 0px;"
            "}");
    }
    else
    {
        UniversalDialog::showError(this->window(), "加载失败", "无法识别该图片文件，请重试。");
    }
}

void SceneCreateWidget::setInputErrorState(QWidget* widget,
                                           bool isError,
                                           QString const& placeholderText)
{
    if (!widget)
        return;
    QString className = isError ? "input_error" : "form_input";
    if (widget->property("class").toString() != className)
    {
        widget->setProperty("class", className);
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
    }
    if (!placeholderText.isEmpty())
    {
        if (auto* edit = qobject_cast<QLineEdit*>(widget))
        {
            edit->setPlaceholderText(placeholderText);
        }
    }
}

bool SceneCreateWidget::executeCreate(QWidget* dialogParent)
{
    QString name = m_editName->text().trimmed();

    if (name.isEmpty())
    {
        setInputErrorState(m_editName, true, "场景名称不能为空");
        return false;
    }

    QString cat = m_editNewCat->text().trimmed().isEmpty() ? m_comboCat->currentText()
                                                           : m_editNewCat->text().trimmed();
    SceneCategoryType type = (SceneCategoryType)m_comboType->currentData().toInt();

    // [Permission Check 6]: New Category Logic
    // Requirement: If adding new category, must have LayoutType Add permission
    bool isNewCat = m_editNewCat->text().trimmed().length() > 0;

    if (isNewCat)
    {
        if (!PermissionGuard::canAddCategory(dialogParent))
        {
            return false;
        }
    }

    // [Permission Check 7]: Add Scene Logic (Redundant if checked in ViewController, but good for safety)
    if (!PermissionGuard::canAddScene(dialogParent))
    {
        return false;
    }

    // [Modified] 传入 m_selectedCoverPath
    auto newScene = SceneManager::instance().createScene(
        name.toStdString(), cat.toStdString(), type, m_selectedCoverPath);

    if (newScene)
    {
        if (auto* d = qobject_cast<UniversalDialog*>(dialogParent))
        {
            d->accept();
        }
        else if (dialogParent)
        {
            dialogParent->close();
        }
        return true;
    }

    UniversalDialog::showError(dialogParent, "创建失败", "请检查网络或重试。");
    return false;
}

QString SceneCreateWidget::getSelectedCategory() const
{
    return m_editNewCat->text().trimmed().isEmpty() ? m_comboCat->currentText()
                                                    : m_editNewCat->text().trimmed();
}
