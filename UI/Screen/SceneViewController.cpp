#include "SceneViewController.h"
#include "SceneManager.h"
#include "ResourceService.h"
#include "UniversalDialog.h"
#include "SceneCreateWidget.h"
#include "AuthService.h"
#include "PermissionGuard.h"
#include <QDebug>
#include <QTimer>
#include "SourceDialogs.h"
#include "SceneItem.h"
#include "SourceFactory.h"
#include "UniversalDialog.h"
#include "SharedSourceManager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include "CameraSettingsDialog.h"  // 引入设置窗口
#include "CameraCapture.h"         // 引入具体类以进行 cast
#include <QInputDialog>

// [New] Helper struct to track async loading state
struct SceneLoadState
{
    QString originalContent;
    QString prefContent;
    bool detailLoaded = false;
    bool prefLoaded = false;
    bool prefFailed = false;  // Even if failed, we proceed with original
};

SceneViewController::SceneViewController(QObject* parent) : QObject(parent)
{
    connect(&SceneManager::instance(),
            &SceneManager::sceneListUpdated,
            this,
            &SceneViewController::listNeedsRefresh);
    connect(&SceneManager::instance(),
            &SceneManager::categoriesUpdated,
            this,
            &SceneViewController::listNeedsRefresh);
}

void SceneViewController::initData()
{
    SceneManager::instance().loadIndex();
    emit listNeedsRefresh();
}

void SceneViewController::refreshListUI(QListWidget* list, QComboBox* combo, QLabel* nameLabel)
{
    if (!PermissionGuard::canQueryScenes())
    {
        list->clear();
        return;
    }
    QString currentFilter = combo->currentText();
    combo->blockSignals(true);
    combo->clear();
    combo->addItem("全部");
    combo->addItems(SceneManager::instance().getUniqueCategories());
    int idx = combo->findText(currentFilter);
    if (idx != -1)
        combo->setCurrentIndex(idx);
    combo->blockSignals(false);
    list->clear();
    auto const& scenes = SceneManager::instance().getAllScenes();
    currentFilter = combo->currentText();
    bool currentStillExists = false;
    for (auto const& scene : scenes)
    {
        if (!scene)
            continue;
        QString cat = QString::fromStdString(scene->getCategory());
        if (currentFilter != "全部" && cat != currentFilter)
            continue;
        QListWidgetItem* item = new QListWidgetItem(list);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        if (PermissionGuard::check(PermissionGuard::PERM_LAYOUT_EDIT))
        {
            item->setFlags(item->flags() | Qt::ItemIsEditable);
        }
        item->setData(Qt::UserRole, QVariant::fromValue(reinterpret_cast<quintptr>(scene.get())));
        item->setText(QString::fromStdString(scene->getName()));
        if (m_currentScene && scene == m_currentScene)
        {
            list->setCurrentItem(item);
            currentStillExists = true;
            bool canEdit = PermissionGuard::check(PermissionGuard::PERM_LAYOUT_EDIT);
            QString display = QString::fromStdString(scene->getName()) + (canEdit ? "" : " (只读)");
            if (nameLabel)
                nameLabel->setText(display);
        }
    }
    if (!currentStillExists && m_currentScene)
    {
        m_currentScene->deactivate();
        m_currentScene = nullptr;
        emit sceneSwitched(nullptr);
    }
}

// [Modified] 切换资源：使用 SharedSourceManager 并保存偏好
void SceneViewController::switchResourceForItem(QWidget* parentDialog,
                                                std::shared_ptr<SceneItem> item)
{
    if (!item || !item->getSource())
        return;
    auto source = item->getSource();
    SourceType type = source->getType();

    // 记录原始 ID (作为偏好 Map 的 Key - 旧逻辑，现在直接保存完整快照)
    QString originalSourceId = QString::fromStdString(source->getSourceId());

    QString sourceId = originalSourceId;
    qDebug() << "[SwitchResource] Switching item:" << QString::fromStdString(source->getName())
             << " Type ID:" << (int)type << " SourceID:" << sourceId;
    if (type == SourceType::ScreenCapture_WGC || (int)type == 3)
    {
        if (sourceId.startsWith("mon:"))
        {
            type = SourceType::Monitor;
        }
    }

    UniversalDialog dlg(parentDialog);
    dlg.setTitle("切换信号源");
    QWidget* widget = nullptr;
    if (type == SourceType::Image)
    {
        widget = new AssetSelectionWidget(2, "");
    }
    else if (type == SourceType::Text)
    {
        UniversalDialog::showError(parentDialog, "提示", "文本组件请直接编辑内容");
        return;
    }
    else
    {
        widget = new DeviceSelectionWidget(type);
    }
    if (!widget)
        return;
    dlg.setContentWidget(widget);
    dlg.addButton("取消");
    dlg.addButton("确定", "primary", [&]() {
        AssetModel asset;
        if (auto w = qobject_cast<DeviceSelectionWidget*>(widget))
        {
            asset = w->getSelectedAsset();
        }
        else if (auto w = qobject_cast<AssetSelectionWidget*>(widget))
        {
            asset = w->getSelectedAsset();
        }
        if (asset.pathOrId.isEmpty())
            return true;

        auto newSource = SharedSourceManager::instance().acquireSource(
            type, asset.pathOrId.toStdString(), asset.name.toStdString());

        if (newSource)
        {
            if (auto scene = item->getSceneContext())
            {
                // 1. 替换源 (内存更新，立即生效)
                scene->replaceSource(item, newSource);

                // 2. [Step 4 Implement] 保存偏好 (云端更新)
                // 只有云端场景（拥有 CloudID）才需要同步偏好
                if (scene->getCloudId() > 0)
                {
                    long long layoutId = scene->getCloudId();

                    // 生成完整快照作为偏好数据
                    // 这样最简单且健壮，后端存储后，下次加载 loadWithPreference 时
                    // 会使用这里的 source 信息覆盖原始布局的 source（如果 UUID 匹配且类型一致）
                    // 即使布局位置没变，我们也把这一刻的完整“内容映射”存下来
                    QJsonObject snapshot = scene->toJson();
                    QString contentStr = QJsonDocument(snapshot).toJson(QJsonDocument::Compact);

                    qDebug() << "[SceneViewController] Saving Preference for Scene:" << layoutId;

                    // 调用 ResourceService 更新偏好
                    ResourceService::instance()->updateLayoutPreference(layoutId, contentStr);
                }
            }
        }
        return true;
    });
    dlg.exec();
}

void SceneViewController::createScene(QWidget* parent)
{
    if (!PermissionGuard::canAddScene(parent))
        return;
    UniversalDialog dlg(parent);
    dlg.setTitle("新建场景");
    dlg.resize(500, 560);

    SceneCreateWidget* createWidget = new SceneCreateWidget(&dlg);
    dlg.setContentWidget(createWidget);

    QString createStyle =
        "QPushButton { background-color: #0078D7; color: white; border-radius: 6px; padding: 8px "
        "24px; }";
    dlg.addButton("取消", "", [&]() { return true; });
    dlg.addButton(
        "创建", createStyle, [createWidget, &dlg]() { return createWidget->executeCreate(&dlg); });
    dlg.exec();
}

void SceneViewController::deleteScene(QWidget* parent, Scene* scenePtr)
{
    if (!scenePtr)
        return;
    if (!PermissionGuard::canDeleteScene(parent))
        return;
    if (UniversalDialog::showConfirm(
            parent,
            "确认删除",
            QString("确定要删除场景“%1”吗？").arg(QString::fromStdString(scenePtr->getName()))))
    {
        auto scenes = SceneManager::instance().getAllScenes();
        auto it = std::find_if(
            scenes.begin(), scenes.end(), [scenePtr](auto s) { return s.get() == scenePtr; });
        if (it != scenes.end())
        {
            if (m_currentScene.get() == scenePtr)
            {
                m_currentScene = nullptr;
                emit sceneSwitched(nullptr);
            }
            SceneManager::instance().removeScene(*it);
            emit listNeedsRefresh();
        }
    }
}

void SceneViewController::onSceneSelectedRequest(Scene* rawPtr)
{
    if (!rawPtr)
        return;
    if (!PermissionGuard::canQueryScenes())
        return;

    // 检查是否是 SceneManager 管理的本地场景
    auto scenes = SceneManager::instance().getAllScenes();
    auto it =
        std::find_if(scenes.begin(), scenes.end(), [rawPtr](auto s) { return s.get() == rawPtr; });

    // 如果是本地场景，走标准切换逻辑
    if (it != scenes.end())
    {
        std::shared_ptr<Scene> target = *it;
        if (!target->isDetailLoaded())
        {
            emit sceneNameChanged("加载中...");

            // [Modified] 如果是云端场景，使用新流程加载（含偏好）
            if (target->getCloudId() > 0)
            {
                loadSceneWithPreference(target->getCloudId(), target);
            }
            else
            {
                // 本地新建的未同步场景，直接加载
                performSceneSwitch(target.get());
            }
            return;
        }
        performSceneSwitch(target.get());
    }
    // 如果不是本地场景（例如直播模式下的临时场景），直接切换，不走 SceneManager 检查
    else
    {
        performSceneSwitch(rawPtr);
    }
}

// [New] 加载场景并应用偏好 (Step 3 核心修改)
void SceneViewController::loadSceneWithPreference(long long layoutId,
                                                  std::shared_ptr<Scene> targetScene)
{
    if (!targetScene)
        return;

    // 创建状态对象 (使用 shared_ptr 管理生命周期)
    auto state = std::make_shared<SceneLoadState>();

    // 创建完成检查器
    auto checkCompletion = [this, layoutId, targetScene, state]() {
        // 只有当两个请求都返回了（成功或失败）才继续
        if (state->detailLoaded && (state->prefLoaded || state->prefFailed))
        {
            qDebug() << "[SceneViewController] All data ready for Scene:" << layoutId;

            // [Step 3 Implement] 使用 Scene 内部的智能合并接口
            // 无论偏好是否获取成功，都传入 prefContent (失败为空字符串)，Scene 内部会处理空值回退
            bool ok = targetScene->loadWithPreference(state->originalContent, state->prefContent);

            if (ok)
            {
                // 3. 标记为已加载并切换
                targetScene->setDetailLoaded(true);
                performSceneSwitch(targetScene.get());
            }
            else
            {
                UniversalDialog::showError(nullptr, "加载失败", "场景数据解析错误");
            }
        }
    };

    // 并行发起两个请求

    // 连接 SceneManager 信号 (获取共享场景详情)
    auto connScene = std::make_shared<QMetaObject::Connection>();
    *connScene = connect(
        &SceneManager::instance(),
        &SceneManager::sceneLoaded,
        this,
        [this, layoutId, targetScene, state, checkCompletion, connScene](Scene* s) {
            if (s == targetScene.get())
            {
                QObject::disconnect(*connScene);
                state->detailLoaded = true;
                // 注意：targetScene 此时被 SceneManager 填充了基础数据，但我们需要的是原始 Content 字符串
                // 鉴于不修改 SceneManager，我们可以直接从 targetScene 序列化出来作为 originalContent
                // 这样做是安全的，因为此时 targetScene 刚被 SceneManager 用共享数据填充。
                QJsonObject sharedJson = targetScene->toJson();
                state->originalContent = QJsonDocument(sharedJson).toJson(QJsonDocument::Compact);

                checkCompletion();
            }
        });

    // 连接 ResourceService 偏好信号 (获取用户偏好)
    auto connPref = std::make_shared<QMetaObject::Connection>();
    *connPref = connect(ResourceService::instance(),
                        &ResourceService::preferenceFetched,
                        this,
                        [layoutId, state, checkCompletion, connPref](
                            bool success, long long id, QString const& content, QString const&) {
                            if (id == layoutId)
                            {
                                QObject::disconnect(*connPref);
                                if (success)
                                {
                                    state->prefLoaded = true;
                                    state->prefContent = content;
                                }
                                else
                                {
                                    state->prefFailed = true;
                                    // 失败则 content 为空
                                    state->prefContent = "";
                                }
                                checkCompletion();
                            }
                        });

    // 发起请求
    // 注意：SceneManager::getSceneDetail 会触发 sceneLoaded 信号
    ResourceService::instance()->getSceneDetail(QString::number(layoutId));
    ResourceService::instance()->fetchLayoutPreference(layoutId);
}

std::shared_ptr<EffectManager> SceneViewController::getEffectManagerFromItem(
    std::shared_ptr<SceneItem> item)
{
    if (!item)
        return nullptr;
    auto source = item->getSource();
    if (!source)
        return nullptr;

    // 只有 CameraCapture 有特效管理器
    // 使用 dynamic_pointer_cast 进行安全转换
    if (source->getType() == SourceType::Camera)
    {
        auto cam = std::dynamic_pointer_cast<CameraCapture>(source);
        if (cam)
        {
            return cam->getEffectManager();
        }
    }
    return nullptr;
}

// [New] 批量获取直播场景数据
void SceneViewController::fetchLiveScenes(QList<long long> const& layoutIds)
{
    if (layoutIds.isEmpty())
        return;

    qDebug() << "[SceneViewController] Fetching live scenes. Count:" << layoutIds.size();

    // 遍历 ID 发起请求
    for (long long id : layoutIds)
    {
        // 为每个请求建立一个独立的一次性连接
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(
            ResourceService::instance(),
            &ResourceService::dataReceived,
            [this, conn, id](QString const& opType, QJsonObject const& data) {
                if (opType == "scene_detail")
                {
                    QJsonObject detail = data.contains("data") ? data["data"].toObject() : data;

                    // 校验 ID
                    if (detail["layoutId"].toVariant().toLongLong() != id)
                        return;

                    QObject::disconnect(*conn);

                    // 解析场景
                    QString content = detail["content"].toString();
                    if (content.isEmpty() && detail["content"].isObject())
                    {
                        QJsonDocument doc(detail["content"].toObject());
                        content = doc.toJson(QJsonDocument::Compact);
                    }

                    QString name = detail["layoutName"].toString();
                    if (name.isEmpty())
                        name = "未命名场景";

                    // 创建临时 Scene 对象
                    auto tempScene = std::make_shared<Scene>(name.toStdString(), "Live");
                    tempScene->setCloudId(id);  // 设置 ID 以便后续加载封面或偏好

                    // 如果 JSON 中有 coverUrl，Scene::loadFromContent 会自动处理
                    if (!content.isEmpty())
                    {
                        bool loadOk = tempScene->loadFromContent(content);
                        if (loadOk)
                        {
                            qDebug() << "[SceneViewController] Live Scene Loaded:" << name;
                            // 加载成功，发出信号
                            emit liveSceneLoaded(tempScene);
                        }
                        else
                        {
                            qWarning()
                                << "[SceneViewController] Failed to parse content for scene:" << id;
                        }
                    }
                }
            });

        // 发起 API 请求
        ResourceService::instance()->getSceneDetail(QString::number(id));
    }
}

bool SceneViewController::openCameraSettings(QWidget* parentDialog)
{
    auto scene = m_currentScene;
    if (!scene)
    {
        UniversalDialog::showError(parentDialog, "提示", "请先选择一个场景");
        return false;
    }

    std::shared_ptr<EffectManager> targetMgr = nullptr;
    QString targetName;

    // [Modified] 策略修改：不再弹出选择框
    // 逻辑：遍历场景 Item，只要找到任意一个摄像头，就打开它的设置。
    // 原因：底层 SharedSourceManager 保证了同一个物理摄像头在内存中只有一个 CameraCapture 实例。
    // 所以，调节 SceneItem A 的摄像头特效，SceneItem B (如果是同一个摄像头) 也会自动生效。

    auto items = scene->getItems();
    for (auto& item : items)
    {
        auto mgr = getEffectManagerFromItem(item);
        if (mgr)
        {
            targetMgr = mgr;
            targetName = QString::fromStdString(item->getSource()->getName());
            break;  // 找到第一个就停止，直接使用
        }
    }

    if (!targetMgr)
    {
        UniversalDialog::showError(parentDialog, "提示", "当前场景中没有找到摄像头，无法设置特效");
        return false;
    }

    // 直接打开窗口
    CameraSettingsDialog dlg(targetMgr, parentDialog);
    dlg.setWindowTitle("摄像头特效管理");
    dlg.exec();

    return true;
}

void SceneViewController::performSceneSwitch(Scene* ptr)
{
    // 如果切换到同一个场景，不处理
    if (m_currentScene && m_currentScene.get() == ptr)
        return;

    auto oldScene = m_currentScene;

    // 尝试在 SceneManager 中查找 shared_ptr (用于保持引用计数)
    auto scenes = SceneManager::instance().getAllScenes();
    auto it = std::find_if(scenes.begin(), scenes.end(), [ptr](auto s) { return s.get() == ptr; });

    if (it != scenes.end())
    {
        // 是本地管理的场景
        m_currentScene = *it;
    }
    else
    {
        // 是临时的直播场景，需要自己想办法持有 (在 ScreenWindow 中管理)
    }

    if (ptr)
    {
        ptr->activate();
    }
    if (oldScene)
    {
        oldScene->deactivate();
    }

    if (it != scenes.end())
    {
        emit sceneSwitched(m_currentScene);
        bool canEdit = PermissionGuard::check(PermissionGuard::PERM_LAYOUT_EDIT);
        QString name =
            QString::fromStdString(m_currentScene->getName()) + (canEdit ? "" : " (只读)");
        emit sceneNameChanged(name);
    }
}

void SceneViewController::editPermissions(QWidget* parent, Scene* scene)
{ /* Legacy */
}
void SceneViewController::handleOrderChange(std::vector<std::string> const& newOrder)
{
    if (PermissionGuard::check(PermissionGuard::PERM_LAYOUT_EDIT))
    {
        SceneManager::instance().reorderScenes(newOrder);
    }
}
void SceneViewController::filterCategory(QString const&)
{
    emit listNeedsRefresh();
}

void SceneViewController::addResource(QWidget* parent, SourceType type)
{
    // [Fix 1] 预先获取 shared_ptr 锁定生命周期，防止 dlg.exec 期间 m_currentScene 发生变化
    auto lockedScene = m_currentScene;

    if (!lockedScene)
    {
        UniversalDialog::showError(parent, "提示", "请先选择或创建一个场景");
        return;
    }
    if (!PermissionGuard::canAddMedia(parent))
        return;
    if (!PermissionGuard::canEditScene(parent))
        return;

    UniversalDialog dlg(parent);
    QWidget* widget = nullptr;
    if (type == SourceType::Camera || type == SourceType::Monitor ||
        type == SourceType::ScreenCapture_WGC)
    {
        widget = new DeviceSelectionWidget(type);
    }
    else if (type == SourceType::Image)
    {
        widget = new AssetSelectionWidget(2, "");
    }
    else if (type == SourceType::Text)
    {
        widget = new TextSelectionWidget();
    }
    if (!widget)
        return;

    dlg.setContentWidget(widget);
    dlg.addButton("取消");

    // [Fix 2] Lambda 按值捕获 type，并使用 lockedScene 替代 m_currentScene
    dlg.addButton("添加", "primary", [=, &dlg]() -> bool {
        // [Safety Check] 再次检查场景有效性
        if (!lockedScene)
            return true;

        AssetModel asset;
        if (auto w = qobject_cast<DeviceSelectionWidget*>(widget))
            asset = w->getSelectedAsset();
        else if (auto w = qobject_cast<AssetSelectionWidget*>(widget))
            asset = w->getSelectedAsset();
        else if (auto w = qobject_cast<TextSelectionWidget*>(widget))
            asset = w->getSelectedAsset();

        if (type == SourceType::ScreenCapture_WGC)
        {
            // 1. 获取原始 ID 字符串
            QString rawId = asset.pathOrId;

            qDebug() << "[AddResource] WGC Check - Raw ID:" << rawId;

            if (rawId.isEmpty() || rawId == "0")
            {
                UniversalDialog::showError(parent, "错误", "无效的窗口句柄");
                return false;
            }

            // 2. [关键修改] 去除 "win:" 或 "mon:" 前缀
            // 日志显示 ID 格式为 "win:12345"，直接转数字会失败，必须去掉前缀
            if (rawId.startsWith("win:"))
            {
                rawId = rawId.mid(4);  // 去掉前4个字符
            }
            else if (rawId.startsWith("mon:"))
            {
                rawId = rawId.mid(4);
            }

            // 3. 转换字符串为句柄 (自动兼容 10进制 和 0x十六进制)
            bool conversionOk = false;
            HWND hwnd = (HWND)rawId.toULongLong(&conversionOk, 0);

            // 4. 验证句柄有效性
            if (!conversionOk || !IsWindow(hwnd))
            {
                qWarning() << "[AddResource] Invalid HWND:" << hwnd << " Parsed from:" << rawId;
                UniversalDialog::showError(parent, "错误", "目标窗口句柄无效或窗口已关闭");
                return false;
            }
        }

        if (asset.pathOrId.isEmpty())
        {
            if (type == SourceType::Text)
                asset.pathOrId = asset.name.isEmpty() ? "Text Source" : asset.name;
            else if (type != SourceType::ScreenCapture_WGC && type != SourceType::Monitor)
                asset.pathOrId = "gen:" + QUuid::createUuid().toString();
        }
        if (asset.uuid.isEmpty())
            asset.uuid = asset.pathOrId;
        asset.type = type;

        QString baseName = asset.name;
        if (baseName.isEmpty())
        {
            switch (type)
            {
                case SourceType::Camera:
                    baseName = "摄像头";
                    break;
                case SourceType::ScreenCapture_WGC:
                    baseName = "窗口";
                    break;
                case SourceType::Image:
                    baseName = "图片";
                    break;
                case SourceType::Monitor:
                    baseName = "显示器";
                    break;
                case SourceType::Text:
                    baseName = "文本";
                    break;
                default:
                    baseName = "未知来源";
                    break;
            }
        }

        // [CRITICAL FIX] 预先提取所有占用名称，避免在 while 循环中反复访问 SceneItem 指针
        // 这彻底解决了 "item->d_ptr.d is nullptr" 的隐患，因为不再在循环中解引用 item
        QSet<QString> usedNames;
        auto existingItems = lockedScene->getItems();
        for (auto const& item : existingItems)
        {
            // 三重安全检查
            if (item && item->getSource())
            {
                QString n = QString::fromStdString(item->getSource()->getName());
                if (!n.isEmpty())
                {
                    usedNames.insert(n);
                }
            }
        }

        QString finalName = baseName;
        int counter = 1;

        // 纯内存操作，绝对安全
        while (usedNames.contains(finalName))
        {
            finalName = QString("%1 %2").arg(baseName).arg(++counter);
        }
        asset.name = finalName;

        SceneManager::instance().addAssetToScene(lockedScene, asset);

        // [Fix 4] 重新获取 items 确认添加成功
        auto currentItems = lockedScene->getItems();
        if (!currentItems.empty())
        {
            auto newItem = currentItems.back();
            if (newItem)
            {
                QRectF canvasRect = lockedScene->getCanvasRect();

                float centerX = canvasRect.width() / 2.0f;
                float centerY = canvasRect.height() / 2.0f;

                float initW = canvasRect.width() * 0.5f;
                float initH = initW * (9.0f / 16.0f);

                if (initH > canvasRect.height() * 0.8f)
                {
                    initH = canvasRect.height() * 0.8f;
                    initW = initH * (16.0f / 9.0f);
                }

                qDebug() << QString(
                                "[SceneViewController] Adding Resource to Canvas (%1x%2). "
                                "Calculated InitSize: %3x%4")
                                .arg(canvasRect.width())
                                .arg(canvasRect.height())
                                .arg(initW)
                                .arg(initH);

                newItem->setSize(initW, initH);
                newItem->setPosition(centerX - initW / 2.0f, centerY - initH / 2.0f);
            }
        }
        return true;
    });
    dlg.exec();
}
