#pragma once
#include <QWidget>
#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QPainter>
#include <QMouseEvent>
#include <QPoint>
#include <QThread>
#include <QThreadPool>
#include <QRunnable>
#include <QMetaObject>
#include <QPointer>
#include <QFileInfo>
#include <QLineEdit>
#include <QSet>
#include <QStackedWidget>
#include <QMovie>
#include "Types.h"
#include "SourceProvider.h"
#include "skinmgr.h"
#include "AssetCacheManager.h"
#include <QMessageBox>
#include <QMutexLocker>
#include <QMutex>
#include <QStyledItemDelegate>

class AssetSelectionWidget;

namespace SourceDialogStyle
{
QString const Input =
    "QLineEdit, QComboBox { background-color: #FFFFFF; color: #333333; border: 1px solid #DCDFE6; "
    "border-radius: 4px; padding: 0 10px; min-height: 32px; font-size: 14px; font-family: "
    "'Microsoft YaHei UI'; } QLineEdit:hover, QComboBox:hover { border-color: #C0C4CC; } "
    "QLineEdit:focus, QComboBox:focus { border-color: #409EFF; } QComboBox::drop-down { border: "
    "none; width: 30px; } QComboBox::down-arrow { image: url(:/icons/arrow_down.png); width: 12px; "
    "height: 12px; border-top: 6px solid #909399; border-left: 5px solid transparent; "
    "border-right: 5px solid transparent; margin-right: 5px; } QComboBox QAbstractItemView { "
    "background-color: #FFFFFF; color: #606266; border: 1px solid #E4E7ED; "
    "selection-background-color: #ECF5FF; selection-color: #409EFF; outline: none; padding: 5px; }";
QString const Label = "color: #606266; font-size: 14px; font-weight: bold;";
QString const ListWidget =
    "QListWidget { background: #FFFFFF; border: 1px solid #DCDFE6; border-radius: 4px; outline: "
    "none; } QListWidget::item { color: #606266; border-radius: 4px; border: 1px solid "
    "transparent; } QListWidget::item:selected { background-color: #ECF5FF; border: 1px solid "
    "#409EFF; color: #409EFF; } QListWidget::item:hover { background-color: #F2F6FC; }";
QString const ScrollBar =
    "QScrollBar:vertical { background: transparent; width: 6px; margin: 0; } "
    "QScrollBar::handle:vertical { background: #DCDFE6; min-height: 20px; border-radius: 3px; } "
    "QScrollBar::handle:vertical:hover { background: #C0C4CC; } QScrollBar::add-line:vertical, "
    "QScrollBar::sub-line:vertical { height: 0; } QScrollBar::add-page:vertical, "
    "QScrollBar::sub-page:vertical { background: none; }";
}  // namespace SourceDialogStyle

class AssetListDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit AssetListDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter* painter,
               QStyleOptionViewItem const& option,
               QModelIndex const& index) const override
    {
        if (!index.isValid())
            return;
        // 1. 默认绘制 (图标和文字)
        QStyledItemDelegate::paint(painter, option, index);

        // 2. 检查悬浮状态
        if (option.state & QStyle::State_MouseOver)
        {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);

            // 计算右上角按钮位置
            int btnSize = 24;
            int margin = 4;
            QRect btnRect = getDeleteButtonRect(option.rect);

            // 绘制蓝色圆形背景
            painter->setBrush(QColor("#409EFF"));  // 企业蓝
            painter->setPen(Qt::NoPen);
            painter->drawEllipse(btnRect);

            // 绘制 "X" 或 垃圾桶图标
            painter->setPen(QPen(Qt::white, 2));
            int p = 7;  // padding
            painter->drawLine(
                btnRect.left() + p, btnRect.top() + p, btnRect.right() - p, btnRect.bottom() - p);
            painter->drawLine(
                btnRect.left() + p, btnRect.bottom() - p, btnRect.right() - p, btnRect.top() + p);

            painter->restore();
        }
    }

    bool editorEvent(QEvent* event,
                     QAbstractItemModel* model,
                     QStyleOptionViewItem const& option,
                     QModelIndex const& index) override
    {
        // [优化] 只响应鼠标抬起事件，且必须是左键
        if (event->type() == QEvent::MouseButtonRelease)
        {
            QMouseEvent* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton)
            {
                QRect btnRect = getDeleteButtonRect(option.rect);
                if (btnRect.contains(me->pos()))
                {
                    // [关键] 记录点击状态或立即消费
                    emit deleteRequested(index);
                    return true;  // [关键] 必须返回 true，告诉 Qt 该事件已被处理，不再向下传递
                }
            }
        }
        // [优化] 过滤 MouseMove，减少重绘压力，仅在必要时调用父类
        if (event->type() == QEvent::MouseMove)
        {
            // 仅触发 hover 效果，不需要特别处理
            // 但如果不返回 true，QListWidget 可能会更新选中状态，这对 IconMode 影响不大
        }

        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }

signals:
    void deleteRequested(QModelIndex const& index);

private:
    QRect getDeleteButtonRect(QRect const& itemRect) const
    {
        int btnSize = 24;
        int margin = 6;
        // 定位到 Item 的右上角
        return QRect(
            itemRect.right() - btnSize - margin, itemRect.top() + margin, btnSize, btnSize);
    }
};

class ThumbnailLoadTask : public QRunnable
{
public:
    ThumbnailLoadTask(AssetSelectionWidget* receiver,
                      QString const& path,
                      QString const& identifier);
    void run() override;

private:
    static QImage processThumbnailAsync(QString const& path)
    {
        if (path.isEmpty() || !QFile::exists(path))
        {
            // qWarning() << "[ThumbnailTask] Invalid path:" << path;
            return QImage();
        }
        int const W = 160;
        int const H = 90;
        QImage source;
        source.load(path);

        if (source.isNull())
            return QImage();
        QImage scaled = source.scaled(W, H, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QImage canvas(W, H, QImage::Format_ARGB32);
        canvas.fill(QColor(245, 245, 245));
        QPainter p(&canvas);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        int x = (W - scaled.width()) / 2;
        int y = (H - scaled.height()) / 2;
        p.drawImage(x, y, scaled);
        p.end();
        return canvas;
    }
    QPointer<AssetSelectionWidget> m_receiver;
    QString m_path;
    QString m_id;
};

// ... (DeviceSelectionWidget, TextSelectionWidget 保持不变) ...

class DeviceSelectionWidget : public QWidget
{
    Q_OBJECT
public:
    DeviceSelectionWidget(SourceType type, QWidget* parent = nullptr) : QWidget(parent)
    {
        QVBoxLayout* layout = new QVBoxLayout(this);
        layout->setContentsMargins(20, 10, 20, 20);
        layout->setSpacing(10);

        QLabel* lbl = new QLabel("请选择采集设备/窗口:", this);
        lbl->setStyleSheet(SourceDialogStyle::Label);
        layout->addWidget(lbl);

        m_cmbDevices = new QComboBox(this);
        m_cmbDevices->setStyleSheet(SourceDialogStyle::Input);
        m_cmbDevices->setView(new QListView());

        m_deviceList = AssetManager::instance().getAvailableDevices(type);

        // [FIX] 处理空列表情况，给出明确提示
        if (m_deviceList.empty())
        {
            m_cmbDevices->addItem("未检测到可用设备或窗口", "");
            m_cmbDevices->setEnabled(false);  // 禁用下拉框
        }
        else
        {
            for (auto const& d : m_deviceList)
            {
                m_cmbDevices->addItem(d.name, d.pathOrId);
            }
        }

        layout->addWidget(m_cmbDevices);
        layout->addStretch();
    }
    AssetModel getSelectedAsset() const
    {
        int idx = m_cmbDevices->currentIndex();
        if (idx >= 0 && idx < (int)m_deviceList.size())
            return m_deviceList[idx];
        return AssetModel();
    }

private:
    QComboBox* m_cmbDevices;
    std::vector<AssetModel> m_deviceList;
};

class TextSelectionWidget : public QWidget
{
    Q_OBJECT
public:
    TextSelectionWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        QVBoxLayout* layout = new QVBoxLayout(this);
        layout->setContentsMargins(20, 10, 20, 20);
        layout->setSpacing(10);
        QLabel* lbl = new QLabel("请输入文本内容:", this);
        lbl->setStyleSheet(SourceDialogStyle::Label);
        layout->addWidget(lbl);
        m_lineEdit = new QLineEdit(this);
        m_lineEdit->setPlaceholderText("Text Source");
        m_lineEdit->setStyleSheet(SourceDialogStyle::Input);
        layout->addWidget(m_lineEdit);
        layout->addStretch();
    }
    AssetModel getSelectedAsset() const
    {
        AssetModel asset;
        asset.name = m_lineEdit->text().trimmed();
        if (asset.name.isEmpty())
            asset.name = "Text Source";
        asset.pathOrId = asset.name;
        asset.type = SourceType::Text;
        return asset;
    }

private:
    QLineEdit* m_lineEdit;
};

class AssetSelectionWidget : public QWidget
{
    Q_OBJECT
public:
    AssetSelectionWidget(int fixedType, QString const& groupId, QWidget* parent = nullptr)
        : QWidget(parent), m_fixedType(fixedType), m_targetGroupId(groupId)
    {
        QVBoxLayout* mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(20, 10, 20, 20);
        mainLayout->setSpacing(10);

        QHBoxLayout* tools = new QHBoxLayout();
        QLabel* lblHint = new QLabel("仅显示图片文件");
        lblHint->setStyleSheet("color: #909399; font-style: italic; font-size: 12px;");

        QPushButton* btnImport = new QPushButton("从本地导入...");
        btnImport->setCursor(Qt::PointingHandCursor);
        btnImport->setStyleSheet(
            "QPushButton { background-color: #F2F6FC; color: #409EFF; border: 1px solid #DCDFE6; "
            "padding: 6px 12px; border-radius: 4px; } QPushButton:hover { background-color: "
            "#ECF5FF; border-color: #409EFF; }");

        tools->addWidget(lblHint);
        tools->addStretch();
        tools->addWidget(btnImport);
        mainLayout->addLayout(tools);

        m_stack = new QStackedWidget();
        // List
        m_listWidget = new QListWidget();
        m_listWidget->setIconSize(QSize(160, 90));
        m_listWidget->setViewMode(QListWidget::IconMode);
        m_listWidget->setSpacing(10);
        m_listWidget->setResizeMode(QListWidget::Adjust);
        m_listWidget->setMovement(QListView::Static);
        m_listWidget->setGridSize(QSize(180, 135));
        m_listWidget->setUniformItemSizes(true);
        m_listWidget->setWordWrap(true);
        m_listWidget->setTextElideMode(Qt::ElideRight);
        m_listWidget->setStyleSheet(SourceDialogStyle::ListWidget + SourceDialogStyle::ScrollBar);
        m_listWidget->setMouseTracking(true);

        // [New] 设置自定义代理
        AssetListDelegate* delegate = new AssetListDelegate(m_listWidget);
        m_listWidget->setItemDelegate(delegate);

        // [New] 连接删除信号
        connect(delegate,
                &AssetListDelegate::deleteRequested,
                this,
                &AssetSelectionWidget::onItemDeleteRequested);

        // [New] 连接 AssetManager 的删除完成信号
        connect(&AssetManager::instance(),
                &AssetManager::assetDeleted,
                this,
                &AssetSelectionWidget::onAssetDeletedCallback);
        m_stack->addWidget(m_listWidget);

        // Loading
        QWidget* loadingPage = new QWidget();
        QVBoxLayout* loadLay = new QVBoxLayout(loadingPage);
        QLabel* loadLbl = new QLabel("正在加载云端素材...", loadingPage);
        loadLbl->setAlignment(Qt::AlignCenter);
        loadLbl->setStyleSheet("color: #909399; font-size: 14px;");
        loadLay->addStretch();
        loadLay->addWidget(loadLbl);
        loadLay->addStretch();
        m_stack->addWidget(loadingPage);

        // Empty
        QWidget* emptyPage = new QWidget();
        QVBoxLayout* emptyLay = new QVBoxLayout(emptyPage);
        QLabel* emptyLbl = new QLabel("暂无素材，请先导入", emptyPage);
        emptyLbl->setAlignment(Qt::AlignCenter);
        emptyLbl->setStyleSheet("color: #C0C4CC; font-size: 14px;");
        emptyLay->addStretch();
        emptyLay->addWidget(emptyLbl);
        emptyLay->addStretch();
        m_stack->addWidget(emptyPage);

        mainLayout->addWidget(m_stack);
        this->setMinimumSize(625, 450);
        this->resize(625, 450);

        connect(btnImport, &QPushButton::clicked, this, &AssetSelectionWidget::onImportLocal);
        connect(m_listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            m_selectedAsset = item->data(Qt::UserRole).value<AssetModel>();
        });
        connect(m_listWidget,
                &QListWidget::itemDoubleClicked,
                this,
                &AssetSelectionWidget::itemDoubleClicked);

        // 监听资源列表刷新
        connect(&AssetManager::instance(),
                &AssetManager::assetsRefreshed,
                this,
                [this](std::vector<AssetModel> const& cloudAssets) {
                    m_stack->setCurrentIndex(0);
                    m_listWidget->clear();

                    // 1. 先添加正在上传的（本地缓存）
                    auto pending = AssetManager::instance().getPendingAssets(m_fixedType);
                    for (auto const& asset : pending)
                    {
                        addOrUpdateItem(asset);
                    }

                    // 2. 再添加云端的
                    for (auto const& asset : cloudAssets)
                    {
                        addOrUpdateItem(asset);
                    }
                });

        // 监听单个资源上传完成 (用于更新UI中的本地项为云端项)
        connect(&AssetManager::instance(),
                &AssetManager::assetUploaded,
                this,
                [this](QString const& localPath, QString const& cloudUrl) {
                    for (int i = 0; i < m_listWidget->count(); ++i)
                    {
                        QListWidgetItem* item = m_listWidget->item(i);
                        AssetModel model = item->data(Qt::UserRole).value<AssetModel>();
                        if (!model.isCloud && model.pathOrId == localPath)
                        {
                            qDebug()
                                << "[UI] Patching item to cloud:" << localPath << "->" << cloudUrl;
                            model.isCloud = true;
                            model.pathOrId = cloudUrl;
                            model.cloudUrl = cloudUrl;
                            model.thumbnailUrl = cloudUrl;  // 缩略图也指向云端（会有缓存）
                            item->setData(Qt::UserRole, QVariant::fromValue(model));
                            item->setToolTip("已同步: " + cloudUrl);
                            if (m_selectedAsset.pathOrId == localPath)
                            {
                                m_selectedAsset = model;
                            }
                            break;
                        }
                    }
                });

        // 监听缩略图下载完成
        connect(&AssetCacheManager::instance(),
                &AssetCacheManager::assetReady,
                this,
                [this](QString const& url, QString const& localPath, bool isTemp) {
                    if (!isTemp)
                        return;  // 只关心缩略图
                    triggerAsyncUpdate(url, localPath);
                });

        refreshAssets();
    }

    ~AssetSelectionWidget()
    {
        disconnect(&AssetManager::instance(), &AssetManager::assetsRefreshed, this, nullptr);
    }

    AssetModel getSelectedAsset() const
    {
        return m_selectedAsset;
    }

    Q_INVOKABLE void onThumbnailReady(QString const& uuidOrUrl, QImage const& img)
    {
        if (QThread::currentThread() != qApp->thread())
        {
            QMetaObject::invokeMethod(this,
                                      "onThumbnailReady",
                                      Qt::QueuedConnection,
                                      Q_ARG(QString, uuidOrUrl),
                                      Q_ARG(QImage, img));
            return;
        }
        if (uuidOrUrl.isEmpty())
            return;

        for (int i = 0; i < m_listWidget->count(); ++i)
        {
            QListWidgetItem* item = m_listWidget->item(i);
            AssetModel asset = item->data(Qt::UserRole).value<AssetModel>();
            bool match = (asset.uuid == uuidOrUrl) || (asset.pathOrId == uuidOrUrl) ||
                (asset.thumbnailUrl == uuidOrUrl);
            if (match)
            {
                item->setIcon(QIcon(QPixmap::fromImage(img)));
            }
        }
    }

signals:
    void itemDoubleClicked();

private slots:
    // 1. 修改删除请求槽函数，增加防抖
    void onItemDeleteRequested(QModelIndex const& index)
    {
        if (!index.isValid())
            return;

        QListWidgetItem* item = m_listWidget->item(index.row());
        if (!item)
            return;  // 双重检查

        // [New] 检查是否正在删除中 (防止重复点击)
        if (item->data(Qt::UserRole + 1).toBool())
            return;

        AssetModel asset = item->data(Qt::UserRole).value<AssetModel>();

        auto btn = QMessageBox::question(this,
                                         "确认删除",
                                         QString("确定要删除素材 \"%1\" 吗？").arg(asset.name),
                                         QMessageBox::Yes | QMessageBox::No,
                                         QMessageBox::No);

        if (btn == QMessageBox::Yes)
        {
            // [New] 标记该 Item 正在删除，防止再次触发
            item->setData(Qt::UserRole + 1, true);
            item->setFlags(Qt::NoItemFlags);  // 禁用交互

            // 立即调用 Manager 删除
            AssetManager::instance().deleteAsset(asset.uuid);
        }
    }

    // 2. [核心修复] 重写删除回调，安全地移除 Item
    void onAssetDeletedCallback(QString const& uuid)
    {
        // 倒序遍历是删除列表元素的黄金法则，但这里我们可以更精准
        // 因为 UUID 是唯一的，找到一个删一个

        for (int i = 0; i < m_listWidget->count(); ++i)
        {
            QListWidgetItem* item = m_listWidget->item(i);
            AssetModel asset = item->data(Qt::UserRole).value<AssetModel>();

            if (asset.uuid == uuid)
            {
                // [关键] 安全移除
                // 1. 先从 listWidget 取出所有权
                QListWidgetItem* taken = m_listWidget->takeItem(i);
                // 2. 手动释放内存
                if (taken)
                    delete taken;

                // 找到并删除后，立即退出循环！
                // 否则 i 继续增长可能越界，或者如果有多条相同 UUID (理论不应存在) 会导致逻辑混乱
                return;
            }
        }
    }

private:
    void onImportLocal()
    {
        QString filter = "Images (*.png *.jpg *.jpeg *.bmp)";
        QString path = QFileDialog::getOpenFileName(this, "导入本地素材", "", filter);

        if (!path.isEmpty())
        {
            QFileInfo info(path);
            // 1. 调用 Manager 开始上传
            AssetModel asset = AssetManager::instance().importLocalFile(path, m_targetGroupId);

            // 2. 立即显示在列表
            m_stack->setCurrentIndex(0);
            addOrUpdateItem(asset);

            QListWidgetItem* item = m_listWidget->item(m_listWidget->count() - 1);
            m_listWidget->scrollToBottom();
            m_listWidget->setCurrentItem(item);
            m_selectedAsset = asset;
        }
    }

    void refreshAssets()
    {
        m_stack->setCurrentIndex(1);  // Loading state
        AssetManager::instance().fetchCloudAssets(m_fixedType);
    }

    void addOrUpdateItem(AssetModel const& asset)
    {
        QString key = asset.pathOrId;
        QListWidgetItem* targetItem = nullptr;

        for (int i = 0; i < m_listWidget->count(); ++i)
        {
            QListWidgetItem* it = m_listWidget->item(i);
            AssetModel existing = it->data(Qt::UserRole).value<AssetModel>();
            if (existing.pathOrId == key ||
                (!existing.isCloud && asset.isCloud && existing.name == asset.name))
            {
                targetItem = it;
                break;
            }
        }

        if (!targetItem)
            targetItem = new QListWidgetItem(m_listWidget);

        QString displayName = asset.name;
        if (displayName.length() > 12)
            displayName = displayName.left(10) + "...";
        targetItem->setText(displayName);
        targetItem->setData(Qt::UserRole, QVariant::fromValue(asset));
        QString status = asset.isCloud ? "云端存储" : "正在同步...";
        targetItem->setToolTip(QString("%1\n%2").arg(asset.name).arg(status));

        // 缩略图处理
        if (targetItem->icon().isNull())
        {
            QPixmap placeholder(160, 90);
            placeholder.fill(asset.isCloud ? QColor(240, 240, 240) : QColor(230, 240, 255));
            QPainter p(&placeholder);
            p.setPen(Qt::gray);
            p.drawText(placeholder.rect(), Qt::AlignCenter, "LOADING");
            targetItem->setIcon(QIcon(placeholder));
            checkAndLoadThumbnail(asset);
        }
    }

    void checkAndLoadThumbnail(AssetModel const& asset)
    {
        if (!asset.isCloud)
        {
            // 本地文件直接读取
            QThreadPool::globalInstance()->start(
                new ThumbnailLoadTask(this, asset.pathOrId, asset.pathOrId));
            return;
        }

        QString thumbUrl = asset.thumbnailUrl.isEmpty() ? asset.pathOrId : asset.thumbnailUrl;

        // 查询本地是否有缓存 (isTemp=true)
        QString localPath =
            AssetCacheManager::instance().getThumbnailPath(thumbUrl);  // 此函数会触发下载如果不存在

        // 如果文件已存在，加载之
        if (!localPath.isEmpty() && QFile::exists(localPath) && QFileInfo(localPath).size() > 0)
        {
            QThreadPool::globalInstance()->start(
                new ThumbnailLoadTask(this, localPath, asset.pathOrId));
        }
        // 否则等待 assetReady 信号
    }

    void triggerAsyncUpdate(QString const& url, QString const& localPath)
    {
        if (!url.isEmpty())
            startAsyncLoad(localPath, url);
    }

    void startAsyncLoad(QString const& path, QString const& identifier)
    {
        if (identifier.isEmpty())
            return;
        ThumbnailLoadTask* task = new ThumbnailLoadTask(this, path, identifier);
        QThreadPool::globalInstance()->start(task);
    }

    QStackedWidget* m_stack;
    QListWidget* m_listWidget;
    AssetModel m_selectedAsset;
    int m_fixedType;
    QString m_targetGroupId;
};

inline ThumbnailLoadTask::ThumbnailLoadTask(AssetSelectionWidget* receiver,
                                            QString const& path,
                                            QString const& identifier)
    : m_receiver(receiver), m_path(path), m_id(identifier)
{
    setAutoDelete(true);
}

inline void ThumbnailLoadTask::run()
{
    if (!m_receiver)
        return;
    QImage processedImg = processThumbnailAsync(m_path);
    if (m_receiver && !processedImg.isNull())
    {
        QMetaObject::invokeMethod(m_receiver.data(),
                                  "onThumbnailReady",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, m_id),
                                  Q_ARG(QImage, processedImg));
    }
}
