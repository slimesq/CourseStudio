#include "CameraSettingsDialog.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QDebug>
#include <QFile>
#include <QPainter>
#include <QLineEdit>
#include <QComboBox>
#include <QColorDialog>

CameraSettingsDialog::CameraSettingsDialog(std::shared_ptr<EffectManager> manager, QWidget* parent)
    : UniversalDialog(parent), m_manager(manager)
{
    setTitle("美颜与特效设置");
    resize(720, 500);

    initLayout();
    initSidebar();
    initPages();
    initStyle();
}

void CameraSettingsDialog::initStyle()
{
    // 1. 设置 ObjectName，方便 CSS 定位
    this->setObjectName("CameraSettingsDialog");

    // 隐藏父类可能存在的底部按钮栏
    QWidget* btnBar = this->findChild<QWidget*>("DialogButtonBar");
    if (btnBar)
        btnBar->hide();

    // 2. 加载外部 CSS 文件
    // 请确保将 css 文件放入资源系统，路径需根据实际情况修改
    // 建议路径: ":/UI/QSS/camera_settings.css" 或 ":/camera_settings.css"
    QString qssPath = ":/UI/QSS/skin/camera_settings.css";
    QFile file(qssPath);
    if (file.open(QFile::ReadOnly))
    {
        QString styleSheet = QLatin1String(file.readAll());
        this->setStyleSheet(styleSheet);
        file.close();
    }
    else
    {
        qDebug() << "Error: Cannot load QSS file:" << qssPath;
        // 可以在这里写一个简单的兜底样式，防止完全无样式
        this->setStyleSheet("QWidget { background-color: #FFFFFF; color: #333; }");
    }
}

// ============================================================================
// 布局初始化
// ============================================================================
void CameraSettingsDialog::initLayout()
{
    // 获取 UniversalDialog 的内容容器
    QWidget* contentContainer = this->findChild<QWidget*>("DialogContent");
    if (!contentContainer)
        return;

    // 清理旧布局
    if (contentContainer->layout())
    {
        delete contentContainer->layout();
    }

    // 调整主框架边距 (DialogMainFrame)
    QWidget* mainFrame = this->findChild<QWidget*>("DialogMainFrame");
    if (mainFrame && mainFrame->layout())
    {
        mainFrame->layout()->setContentsMargins(1, 1, 1, 1);  // 留1px显示边框
        mainFrame->layout()->setSpacing(0);
    }

    // 创建左右分栏布局
    QHBoxLayout* mainLayout = new QHBoxLayout(contentContainer);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 左侧侧边栏
    m_sidebar = new QListWidget(contentContainer);
    m_sidebar->setObjectName("Sidebar");
    m_sidebar->setFixedWidth(160);  // 稍微加宽，符合现代 UI
    m_sidebar->setFocusPolicy(Qt::NoFocus);

    // 右侧内容区
    m_pages = new QStackedWidget(contentContainer);
    m_pages->setObjectName("ContentArea");

    mainLayout->addWidget(m_sidebar);
    mainLayout->addWidget(m_pages);

    // 联动逻辑
    connect(m_sidebar, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
}

void CameraSettingsDialog::initSidebar()
{
    QStringList items = {"基础美颜", "微整形", "滤镜风格", "AR贴纸", "水印设置"};
    for (QString const& text : items)
    {
        QListWidgetItem* item = new QListWidgetItem(text, m_sidebar);
        item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
    m_sidebar->setCurrentRow(0);
}

void CameraSettingsDialog::initPages()
{
    m_pages->addWidget(createBeautyPage());  // 0: 基础美颜
    m_pages->addWidget(createBodyPage());    // 1: 微整形 (复用 BodyPage)
    m_pages->addWidget(createFilterPage());  // 2: 滤镜
    m_pages->addWidget(createStickerPage());
    m_pages->addWidget(createWatermarkPage());
}

QWidget* CameraSettingsDialog::createEffectRow(QString const& label,
                                               QString const& effectName,
                                               QString const& paramKey,
                                               int min,
                                               int max,
                                               int defaultVal,
                                               float scale)
{
    QWidget* row = new QWidget;
    row->setFixedHeight(40);

    QHBoxLayout* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 5, 0);
    layout->setSpacing(10);

    // 1. Create AnimatedCheckBox
    AnimatedCheckBox* chkEnable = new AnimatedCheckBox(row);
    chkEnable->setFixedSize(22, 22);

    // 2. Create Label
    QLabel* lblName = new QLabel(label);
    lblName->setFixedWidth(60);
    lblName->setStyleSheet("color: #303133; font-size: 14px;");

    // 3. Slider
    QSlider* slider = new QSlider(Qt::Horizontal);
    slider->setRange(min, max);
    slider->setValue(defaultVal);
    slider->setEnabled(false);  // Default disabled

    // 4. SpinBox
    QSpinBox* spin = new QSpinBox;
    spin->setRange(min, max);
    spin->setValue(defaultVal);
    spin->setFixedWidth(50);
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin->setEnabled(false);

    // Link Slider & SpinBox
    connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), slider, &QSlider::setValue);

    if (m_manager)
    {
        // [CRITICAL LOGIC CHANGE]
        // Check if this is a "shared" effect (like reshape) where multiple rows control one effect.
        // For shared effects, we DO NOT toggle setEffectEnabled. We only toggle the parameter value (0 vs Value).
        // Since "reshape" handles both slim and eye, enabling/disabling "reshape" toggles BOTH.
        bool isShared = (effectName == "reshape");

        // Initialization
        if (isShared)
        {
            // For shared effects, we always assume the MASTER effect is enabled in backend.
            // The "Active" state of the checkbox depends on whether the *Parameter* is > 0.
            // Since we don't have getEffectParam, we rely on default or internal state.
            // For now, default to unchecked or based on defaultVal > 0?
            // Let's just default to unchecked for safety, or checked if defaultVal > 0.
            bool initChecked = (defaultVal > 0);
            chkEnable->setChecked(initChecked, false);
            slider->setEnabled(initChecked);
            spin->setEnabled(initChecked);

            // Ensure the master effect is enabled if we start checked
            if (initChecked)
                m_manager->setEffectEnabled(effectName.toStdString(), true);
        }
        else
        {
            // Standard behavior for 1-to-1 effects
            bool isActive = m_manager->isEffectEnabled(effectName.toStdString());
            chkEnable->setChecked(isActive, false);
            slider->setEnabled(isActive);
            spin->setEnabled(isActive);
        }

        // Toggle Logic
        connect(chkEnable, &AnimatedCheckBox::toggled, this, [=](bool checked) {
            slider->setEnabled(checked);
            spin->setEnabled(checked);

            qDebug() << "[UI] Toggle:" << label << "(" << paramKey << ") ->" << checked;

            if (isShared)
            {
                // Shared Mode:
                // 1. Always ensure the master effect is ENABLED (so the other sub-effect can work)
                //    We never disable "reshape" via UI, unless we track that ALL params are 0 (too complex here).
                //    Just keep it enabled. The cost is negligible if params are 0.
                m_manager->setEffectEnabled(effectName.toStdString(), true);

                // 2. If Checked -> Send Slider Value. If Unchecked -> Send 0.
                float valToSend = checked ? (float)slider->value() * scale : 0.0f;
                m_manager->setEffectParam(
                    effectName.toStdString(), paramKey.toStdString(), valToSend);
            }
            else
            {
                // Standard Mode:
                // Toggle the master switch
                m_manager->setEffectEnabled(effectName.toStdString(), checked);

                // If turning ON, assume the slider value is applied
                if (checked)
                {
                    float finalVal = (float)slider->value() * scale;
                    m_manager->setEffectParam(
                        effectName.toStdString(), paramKey.toStdString(), finalVal);
                }
            }
        });

        // Value Change Logic
        connect(slider, &QSlider::valueChanged, this, [=](int val) {
            // Only send update if checked. If unchecked, the toggle logic sent 0 already.
            if (chkEnable->isChecked())
            {
                QVariant finalVal;
                if (scale != 1.0f)
                    finalVal = (float)val * scale;
                else
                    finalVal = val;

                // qDebug() << "[UI] Set Param:" << effectName << " = " << val;
                m_manager->setEffectParam(
                    effectName.toStdString(), paramKey.toStdString(), finalVal);
            }
        });
    }

    layout->addWidget(chkEnable);
    layout->addWidget(lblName);
    layout->addWidget(slider);
    layout->addWidget(spin);

    return row;
}

// --- 1. 基础美颜页 ---
QWidget* CameraSettingsDialog::createBeautyPage()
{
    QWidget* page = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 20, 30, 20);
    layout->setSpacing(15);

    layout->addWidget(new QLabel("<b>皮肤管理</b>"));
    // 磨皮 (0-50, 默认20)
    layout->addWidget(createEffectRow("磨皮", "smooth", "level", 0, 50, 20));

    // 美白 (0-50, 默认15)
    layout->addWidget(createEffectRow("美白", "whiten", "level", 0, 50, 15));

    // 对比度 (特殊：Scale 0.01)
    layout->addWidget(createEffectRow("对比度", "contrast", "contrast", 50, 150, 100, 0.01f));

    layout->addWidget(new QLabel("<b>画质增强</b>"));
    // 清晰度 (0-50, Scale 0.05)
    layout->addWidget(createEffectRow("锐化", "sharpen", "level", 0, 50, 10, 0.05f));

    layout->addStretch();
    return page;
}

// --- 2. 微整形页 (复用 createBodyPage) ---
QWidget* CameraSettingsDialog::createBodyPage()
{
    QWidget* page = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 20, 30, 20);
    layout->setSpacing(15);

    QLabel* tip = new QLabel("提示：请确保画面中有人脸");
    tip->setStyleSheet("color: #909399; font-size: 12px;");
    layout->addWidget(tip);

    layout->addWidget(new QLabel("<b>五官微调</b>"));

    // 瘦脸
    layout->addWidget(createEffectRow("瘦脸", "reshape", "slim", 0, 100, 30));
    // 大眼
    layout->addWidget(createEffectRow("大眼", "reshape", "eye", 0, 100, 20));

    // [新增] 恢复美妆部分
    layout->addSpacing(10);
    layout->addWidget(new QLabel("<b>虚拟妆容</b>"));
    // 这里对应 MakeupEffect::setParameter("level", value)
    layout->addWidget(createEffectRow("口红", "makeup", "level", 0, 100, 0));

    layout->addStretch();
    return page;
}

// --- 3. 滤镜页 ---
QWidget* CameraSettingsDialog::createFilterPage()
{
    QWidget* page = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);

    QListWidget* grid = new QListWidget;
    grid->setViewMode(QListWidget::IconMode);
    grid->setResizeMode(QListWidget::Adjust);
    grid->setSpacing(15);
    grid->setIconSize(QSize(80, 80));
    grid->setStyleSheet("border: none; background: transparent;");

    // 定义滤镜列表，对应 FilterEffect 中的 type
    // 0:None, 1:Warm, 2:Cool, 3:Gray
    struct FilterItem
    {
        QString name;
        int type;
        QString color;
    };
    QList<FilterItem> filters = {
        {"原图", 0, "#F0F2F5"},
        {"暖阳", 1, "#FFC069"},  // 暖色
        {"清冷", 2, "#69C0FF"},  // 冷色
        {"黑白", 3, "#595959"}   // 灰度
    };

    for (auto const& f : filters)
    {
        QListWidgetItem* item = new QListWidgetItem(f.name);
        item->setData(Qt::UserRole, f.type);  // 存储 type ID

        // 绘制简单图标
        QPixmap pix(80, 80);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(f.color));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(5, 5, 70, 70, 10, 10);
        item->setIcon(QIcon(pix));

        grid->addItem(item);
    }

    // 点击事件
    connect(grid, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        int type = item->data(Qt::UserRole).toInt();
        if (m_manager)
        {
            // 滤镜只要 type > 0 就开启
            m_manager->setEffectEnabled("filter", type > 0);
            m_manager->setEffectParam("filter", "type", type);
        }
    });

    layout->addWidget(grid);
    return page;
}

QWidget* CameraSettingsDialog::createStickerPage()
{
    QWidget* page = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);

    QLabel* tip = new QLabel("点击图标应用贴纸（需检测到人脸）");
    tip->setStyleSheet("color: #909399; font-size: 12px; margin-bottom: 10px;");
    layout->addWidget(tip);

    QListWidget* grid = new QListWidget;
    grid->setViewMode(QListWidget::IconMode);
    grid->setResizeMode(QListWidget::Adjust);
    grid->setSpacing(15);
    grid->setIconSize(QSize(80, 80));
    grid->setStyleSheet("border: none; background: transparent;");
    // 设置选中项样式，让用户知道当前选了哪个
    grid->setStyleSheet(grid->styleSheet() +
                        "QListWidget::item:selected { background-color: #E6F7FF; border: 1px solid "
                        "#1890FF; border-radius: 8px; }");

    // 定义贴纸数据
    // 注意：path 必须是你的 assets 目录下的实际图片路径 (png 带透明通道)
    struct StickerItem
    {
        QString name;
        QString path;
        QString iconColor;
    };
    QList<StickerItem> stickers = {
        {"无贴纸", "", "#F0F2F5"},                                            // 空路径表示关闭
        {"猫耳朵", "E:/live/version26/UI/QSS/icon/cat_ears.png", "#FFC0CB"},  // 示例路径
        {"墨镜", "E:/live/version26/UI/QSS/icon/glasses.png", "#333333"},
        {"皇冠", "E:/live/version26/UI/QSS/icon/crown.png", "#FFD700"},
        {"兔耳朵", "E:/live/version26/UI/QSS/icon/rabbit.png", "#FFB6C1"}};

    for (auto const& s : stickers)
    {
        QListWidgetItem* item = new QListWidgetItem(s.name);
        item->setData(Qt::UserRole, s.path);  // 存储文件路径

        // 绘制图标 (实际项目中应使用 QIcon(s.path) 或缩略图)
        // 这里为了演示，用色块代替
        QPixmap pix(80, 80);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);

        // 如果是"无"，画一个禁止符号或灰色块
        if (s.path.isEmpty())
        {
            p.setBrush(QColor(s.iconColor));
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(5, 5, 70, 70, 10, 10);
            p.setPen(QPen(Qt::gray, 2));
            p.drawLine(20, 20, 60, 60);
            p.drawLine(60, 20, 20, 60);
        }
        else
        {
            // 尝试加载图片作为图标，如果加载失败则用色块
            QPixmap iconPix(s.path);
            if (!iconPix.isNull())
            {
                // 保持比例缩放
                iconPix = iconPix.scaled(70, 70, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                int x = (80 - iconPix.width()) / 2;
                int y = (80 - iconPix.height()) / 2;
                p.drawPixmap(x, y, iconPix);
            }
            else
            {
                p.setBrush(QColor(s.iconColor));
                p.setPen(Qt::NoPen);
                p.drawRoundedRect(5, 5, 70, 70, 10, 10);
                p.setPen(Qt::white);
                p.drawText(QRect(0, 0, 80, 80), Qt::AlignCenter, "Asset\nMissing");
            }
        }

        item->setIcon(QIcon(pix));
        grid->addItem(item);
    }

    // 默认选中第一个
    grid->setCurrentRow(0);

    // 点击事件
    connect(grid, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        QString path = item->data(Qt::UserRole).toString();
        if (m_manager)
        {
            if (path.isEmpty())
            {
                // 关闭贴纸
                m_manager->setEffectEnabled("sticker", false);
            }
            else
            {
                // 开启贴纸并设置路径
                // 注意：StickerEffect::loadAsset 是在 setParameter 中调用的
                m_manager->setEffectEnabled("sticker", true);
                m_manager->setEffectParam("sticker", "path", path);
            }
        }
    });

    layout->addWidget(grid);
    return page;
}

QWidget* CameraSettingsDialog::createWatermarkPage()
{
    QWidget* page = new QWidget;
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 20, 30, 20);
    layout->setSpacing(15);

    // --- 1. 基本配置与内容 (保留原有逻辑) ---
    layout->addWidget(new QLabel("<b>水印内容</b>"));
    QLineEdit* txtInput = new QLineEdit;
    txtInput->setPlaceholderText("请输入水印文字...");
    txtInput->setFixedHeight(35);
    layout->addWidget(txtInput);

    // --- 2. 格式与颜色 (新功能) ---
    layout->addWidget(new QLabel("<b>字体与颜色</b>"));

    QHBoxLayout* formatLayout = new QHBoxLayout();

    // 字体下拉框
    QComboBox* fontCombo = new QComboBox;
    fontCombo->addItem("标准 (Simplex)", cv::FONT_HERSHEY_SIMPLEX);
    fontCombo->addItem("加粗 (Duplex)", cv::FONT_HERSHEY_DUPLEX);
    fontCombo->addItem("复杂 (Complex)", cv::FONT_HERSHEY_COMPLEX);
    fontCombo->addItem("三线 (Triplex)", cv::FONT_HERSHEY_TRIPLEX);
    fontCombo->addItem("手写 (Script)", cv::FONT_HERSHEY_SCRIPT_SIMPLEX);
    fontCombo->setFixedWidth(150);
    fontCombo->setFixedHeight(30);

    // 颜色拾取按钮
    QPushButton* colorBtn = new QPushButton("选择颜色");
    colorBtn->setFixedWidth(100);
    colorBtn->setFixedHeight(30);
    // 实时预览当前颜色的色块 (QLabel)
    QLabel* colorPreview = new QLabel;
    colorPreview->setFixedSize(30, 30);
    colorPreview->setStyleSheet(
        "background-color: white; border: 1px solid #DCDFE6; border-radius: 4px;");

    formatLayout->addWidget(new QLabel("字体:"));
    formatLayout->addWidget(fontCombo);
    formatLayout->addSpacing(20);
    formatLayout->addWidget(new QLabel("颜色:"));
    formatLayout->addWidget(colorPreview);
    formatLayout->addWidget(colorBtn);
    formatLayout->addStretch();
    layout->addLayout(formatLayout);

    // --- 3. 位置微调 (保留原有逻辑) ---
    layout->addWidget(new QLabel("<b>外观与位置</b>"));
    layout->addWidget(createEffectRow("缩放大小", "watermark", "scale", 5, 30, 10, 0.1f));
    layout->addWidget(createEffectRow("水平位置", "watermark", "x", 0, 1920, 20));
    layout->addWidget(createEffectRow("垂直位置", "watermark", "y", 0, 1080, 50));

    // --- 4. 逻辑绑定 ---
    if (m_manager)
    {
        // 绑定文字
        connect(txtInput, &QLineEdit::textChanged, this, [=](QString const& text) {
            m_manager->setEffectParam("watermark", "text", text);
        });

        // 绑定字体选择
        connect(
            fontCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int index) {
                int fontFace = fontCombo->itemData(index).toInt();
                m_manager->setEffectParam("watermark", "font", fontFace);
            });

        // 绑定颜色拾取器 (包含拾取窗口颜色功能)
        connect(colorBtn, &QPushButton::clicked, this, [=]() {
            // QColorDialog 默认支持“拾取屏幕颜色”工具 (Pick Screen Color)
            QColor color = QColorDialog::getColor(
                Qt::white, this, "选择水印颜色", QColorDialog::ShowAlphaChannel);
            if (color.isValid())
            {
                // 更新 UI 预览
                colorPreview->setStyleSheet(
                    QString("background-color: %1; border: 1px solid #DCDFE6;").arg(color.name()));
                // 下发参数
                m_manager->setEffectParam("watermark", "color", color);
            }
        });
    }

    layout->addStretch();
    return page;
}
