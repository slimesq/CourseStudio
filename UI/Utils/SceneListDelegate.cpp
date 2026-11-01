#include "SceneListDelegate.h"
#include <QDebug>
#include <QMouseEvent>
#include <QVariantAnimation>
#include "ThemeManager.h"
#include "SceneManager.h"
#include <QPainterPath>
#include "DraggableListWidget.h"
#include "AssetCacheManager.h"  // [New]

SceneListDelegate::SceneListDelegate(QObject* parent) : QStyledItemDelegate(parent)
{
    // [New] 连接资源下载信号，确保封面下载后自动刷新列表
    connect(&AssetCacheManager::instance(),
            &AssetCacheManager::assetReady,
            this,
            &SceneListDelegate::onAssetReady);
}

SceneListDelegate::~SceneListDelegate()
{
    qDeleteAll(m_animations);
    m_animations.clear();
}

// [New] 资源就绪回调
void SceneListDelegate::onAssetReady(QString const& url, QString const& localPath)
{
    Q_UNUSED(url);
    Q_UNUSED(localPath);
    // 当任意资源下载完成时，强制刷新列表
    // 这里的 parent() 通常是 QListWidget (viewport 的 parent)
    // 我们需要找到 View 来 update
    if (QAbstractItemView* view = qobject_cast<QAbstractItemView*>(parent()))
    {
        view->viewport()->update();
    }
    else if (QWidget* w = qobject_cast<QWidget*>(parent()))
    {
        w->update();
    }
}

QSize SceneListDelegate::sizeHint(QStyleOptionViewItem const& option,
                                  QModelIndex const& index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);
    return QSize(-1, 130);
}

// 辅助：根据动画进度计算按钮区域
QRect SceneListDelegate::getMenuButtonRect(QRect const& thumbRect, float progress) const
{
    int w = 28;
    int startH = 24;
    // [UI优化] 将高度从 34 调整为 30
    int endH = 30;

    int curH = startH + (endH - startH) * progress;

    int margin = 6;
    // Align Top-Right
    int x = thumbRect.right() - w - margin;
    int y = thumbRect.top() + margin;

    return QRect(x, y, w, curH);
}

void SceneListDelegate::paint(QPainter* painter,
                              QStyleOptionViewItem const& option,
                              QModelIndex const& index) const
{
    // 1. 获取数据指针 & 基础状态
    quintptr rawPtr = index.data(Qt::UserRole).value<quintptr>();
    Scene* scene = reinterpret_cast<Scene*>(rawPtr);

    if (!scene)
    {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    // 既然移除了编辑按钮，这里主要关注删除权限
    bool canDelete = SceneManager::instance().checkSceneAuth(
        std::shared_ptr<Scene>(scene, [](Scene*) {}), SceneAuthType::Delete);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    bool isSelected = (opt.state & QStyle::State_Selected);
    bool isHover = (opt.state & QStyle::State_MouseOver);

    // 状态修正
    if (index != m_currentHoveredIndex)
    {
        isHover = false;
        opt.state &= ~QStyle::State_MouseOver;
    }
    if (!isHover && !m_animations.contains(rawPtr))
    {
        if (m_animProgress.contains(rawPtr) && m_animProgress[rawPtr] > 0.0f)
        {
            m_animProgress[rawPtr] = 0.0f;
        }
    }

    QRect rect = opt.rect;

    // [核心修改] 从 Parent Widget 获取样式颜色
    QColor cHover = QColor("#F5F7FA");
    QColor cSelected = QColor("#ECF5FF");
    QColor cBar = QColor("#006EFF");
    QColor cIcon = QColor("#606266");
    QColor cSep = QColor("#EBEEF5");

    if (auto const* view = qobject_cast<DraggableListWidget const*>(option.widget))
    {
        cHover = view->hoverColor();
        cSelected = view->selectedColor();
        cBar = view->barColor();
        cIcon = view->iconColor();
        cSep = view->separatorColor();
    }

    // --- 1. 背景绘制 ---

    int bgMargin = 8;
    QRect contentRect = rect.adjusted(bgMargin, 2, -bgMargin, -2);

    if (isSelected)
    {
        painter->setBrush(cSelected);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(contentRect, 6, 6);
    }
    else if (isHover)
    {
        painter->setBrush(cHover);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(contentRect, 6, 6);
    }

    // --- 2. 选中指示条 ---
    if (isSelected)
    {
        int barH = 16;
        int barW = 3;
        int barX = contentRect.left() + 4;
        int barY = rect.center().y() - barH / 2;

        QPainterPath barPath;
        barPath.addRoundedRect(barX, barY, barW, barH, 1.5, 1.5);
        painter->setBrush(cBar);
        painter->drawPath(barPath);
    }

    // --- 3. 缩略图绘制 ---
    int marginX = 18;
    int thumbW = rect.width() - marginX * 2;
    if (thumbW > 240)
        thumbW = 240;
    int thumbH = thumbW * 9 / 16;
    int thumbX = rect.center().x() - thumbW / 2;
    int thumbY = rect.top() + 12;
    QRect thumbRect(thumbX, thumbY, thumbW, thumbH);

    QPixmap pix = scene->generateThumbnail();

    // [Fix] 如果缓存为空，但有 Cover URL，尝试触发下载
    if (pix.isNull())
    {
        QString coverUrl = scene->getCoverUrl();
        if (!coverUrl.isEmpty() && coverUrl.startsWith("http"))
        {
            // 检查本地是否有文件但未加载进缓存
            QString localPath = AssetCacheManager::instance().queryLocalPath(coverUrl);
            if (localPath.isEmpty())
            {
                // 本地没有，触发下载
                // 注意：paint 会被频繁调用，cacheAsset 内部必须有去重机制 (m_pendingDownloads)
                AssetCacheManager::instance().cacheAsset(coverUrl);
            }
        }
    }

    if (pix.isNull())
    {
        pix = QPixmap(thumbW, thumbH);
        QLinearGradient bgGrad(0, 0, 0, thumbH);
        bgGrad.setColorAt(0.0, QColor(248, 250, 252));
        bgGrad.setColorAt(1.0, QColor(240, 242, 245));
        QPainter p(&pix);
        p.fillRect(pix.rect(), bgGrad);
        p.setPen(QColor(192, 196, 204));
        QFont f = p.font();
        f.setPixelSize(12);
        p.setFont(f);
        p.drawText(pix.rect(), Qt::AlignCenter, "加载中...");
    }
    else
    {
        pix = pix.scaled(thumbW, thumbH, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        if (pix.width() > thumbW || pix.height() > thumbH)
        {
            pix = pix.copy((pix.width() - thumbW) / 2, (pix.height() - thumbH) / 2, thumbW, thumbH);
        }
    }

    QPainterPath path;
    path.addRoundedRect(thumbRect, 6, 6);
    painter->setClipPath(path);
    painter->drawPixmap(thumbRect.topLeft(), pix);
    painter->setClipping(false);

    // 缩略图内描边
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(QColor(220, 223, 230), 1));
    painter->drawRoundedRect(thumbRect, 6, 6);

    // --- 4. 底部文字遮罩 ---
    int textHeight = 32;
    QRect textRect(thumbRect.x(), thumbRect.bottom() - textHeight, thumbRect.width(), textHeight);

    QLinearGradient gradient(textRect.topLeft(), textRect.bottomLeft());
    gradient.setColorAt(0.0, Qt::transparent);
    gradient.setColorAt(1.0, QColor(0, 0, 0, 140));

    QPainterPath textPath;
    textPath.addRoundedRect(thumbRect, 6, 6);

    painter->setClipPath(textPath);
    painter->setPen(Qt::NoPen);
    painter->setBrush(gradient);
    painter->drawRect(textRect);
    painter->setClipping(false);

    // --- 5. 文字绘制 ---
    painter->setPen(Qt::white);
    QFont fName = painter->font();
    fName.setPixelSize(12);
    fName.setBold(false);
    painter->setFont(fName);

    QRect actualTextRect = textRect.adjusted(6, 0, -6, -4);
    QString sceneName = QString::fromStdString(scene->getName());
    QString elidedText =
        painter->fontMetrics().elidedText(sceneName, Qt::ElideRight, actualTextRect.width());

    // 文字阴影
    painter->save();
    painter->setPen(QColor(0, 0, 0, 120));
    painter->translate(0, 1);
    painter->drawText(actualTextRect, Qt::AlignBottom | Qt::AlignHCenter, elidedText);
    painter->restore();

    painter->setPen(Qt::white);
    painter->drawText(actualTextRect, Qt::AlignBottom | Qt::AlignHCenter, elidedText);

    // --- 6. 悬浮菜单按钮 (Modified: Only Delete) ---
    if (m_isLiveMode)
    {
        return;
    }
    float progress = m_animProgress.value(rawPtr, 0.0f);

    if (m_animations.contains(rawPtr))
    {
        progress = m_animations[rawPtr]->currentValue().toFloat();
    }

    if (m_isLiveMode)
    {
        progress = 0.0f;
    }

    if (isHover || progress > 0.01f)
    {
        QRect menuRect = getMenuButtonRect(thumbRect, progress);

        painter->save();

        // 绘制阴影
        if (progress > 0.1f)
        {
            for (int i = 0; i < 3; ++i)
            {
                QRect shadowRect = menuRect.adjusted(-i, -i, i, i + 2);
                QColor shadowColor(0, 0, 0, 10 - i * 2);
                painter->setBrush(shadowColor);
                painter->setPen(Qt::NoPen);
                painter->drawRoundedRect(shadowRect, 8, 8);
            }
        }

        // 绘制主体 (White Pill/Box)
        painter->setBrush(Qt::white);
        painter->setPen(QPen(QColor(220, 223, 230), 1));
        painter->drawRoundedRect(menuRect, 8, 8);

        if (progress < 0.3)
        {
            // 三点图标 (More Options)
            painter->setBrush(cIcon);
            painter->setPen(Qt::NoPen);

            int dotSize = 3;
            int gap = 3;
            int startY = menuRect.center().y() - dotSize - gap;
            int x = menuRect.center().x() - dotSize / 2;

            double opacity = 1.0 - (progress * 3);
            if (opacity < 0)
                opacity = 0;
            painter->setOpacity(opacity);

            painter->drawEllipse(x, startY, dotSize, dotSize);
            painter->drawEllipse(x, startY + dotSize + gap, dotSize, dotSize);
            painter->drawEllipse(x, startY + (dotSize + gap) * 2, dotSize, dotSize);
        }
        else
        {
            // 展开后的按钮图标 (只显示删除)
            double opacity = (progress - 0.3) * 1.4;
            if (opacity > 1)
                opacity = 1;
            if (opacity < 0)
                opacity = 0;
            painter->setOpacity(opacity);

            // 整个区域都是删除按钮
            QRect btnRect = menuRect;
            QColor iconColor = cIcon;

            // 如果有权限则绘制
            if (canDelete)
            {
                drawTrashIcon(painter, btnRect, iconColor);
            }
        }
        painter->restore();
    }

    painter->restore();
}

bool SceneListDelegate::editorEvent(QEvent* event,
                                    QAbstractItemModel* model,
                                    QStyleOptionViewItem const& option,
                                    QModelIndex const& index)
{
    if (event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove)
    {
        m_currentHoveredIndex = index;
    }
    quintptr rawPtr = index.data(Qt::UserRole).value<quintptr>();

    // Standard rect calcs
    QRect rect = option.rect;
    int marginX = 14;
    int thumbW = rect.width() - marginX * 2;
    if (thumbW > 240)
        thumbW = 240;
    int thumbH = thumbW * 9 / 16;
    int thumbX = rect.center().x() - thumbW / 2;
    int thumbY = rect.center().y() - thumbH / 2;
    QRect thumbRect(thumbX, thumbY, thumbW, thumbH);

    // Get Fully Expanded Rect for hit testing
    QRect maxExpandRect = getMenuButtonRect(thumbRect, 1.0f);
    QRect hoverRect = maxExpandRect.adjusted(-5, -5, 5, 20);  // Add buffer

    if (event->type() == QEvent::MouseMove)
    {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        // [新增] 直播模式下，直接忽略悬停检测，不启动弹出动画
        if (m_isLiveMode)
        {
            // 如果之前有残留的动画（比如刚切换模式时），可以强制关闭
            if (m_animations.contains(rawPtr) && m_animations[rawPtr]->endValue().toFloat() > 0.0f)
            {
                startAnimation(rawPtr, false);
            }
            return QStyledItemDelegate::editorEvent(event, model, option, index);
        }
        bool isInside = hoverRect.contains(me->pos());
        float currentProgress = m_animProgress.value(rawPtr, 0.0f);
        if (isInside && currentProgress < 1.0f)
            startAnimation(rawPtr, true);
        else if (!isInside && currentProgress > 0.0f)
            startAnimation(rawPtr, false);
    }
    else if (event->type() == QEvent::MouseButtonRelease)
    {
        QMouseEvent* e = static_cast<QMouseEvent*>(event);

        // 获取当前动画进度，计算按钮位置
        float progress = 0.0f;
        if (m_animations.contains(rawPtr))
        {
            progress = m_animations[rawPtr]->currentValue().toFloat();
        }

        // [新增] 直播模式下，即使 Progress 异常不为0，也禁止响应删除点击
        if (m_isLiveMode)
        {
            return QStyledItemDelegate::editorEvent(event, model, option, index);
        }

        QRect btnRect = getMenuButtonRect(option.rect, progress);  // 假设你有这个辅助函数

        if (btnRect.contains(e->pos()) && progress > 0.8f)
        {  // 只有完全显示才允许点击
            // [原有逻辑] 发送删除信号
            // Scene* scene = ...;
            // emit deleteRequested(scene);
            return true;
        }
    }
    else if (event->type() == QEvent::MouseButtonPress)
    {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        float currentProgress = m_animProgress.value(rawPtr, 0.0f);

        // Only react if menu is expanded
        if (currentProgress > 0.8f && maxExpandRect.contains(me->pos()))
        {
            Scene* scene = reinterpret_cast<Scene*>(rawPtr);
            emit deleteRequested(scene);
            return true;
        }
    }
    // Double click handled by default (createEditor)
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

// --- 编辑器支持 ---

QWidget* SceneListDelegate::createEditor(QWidget* parent,
                                         QStyleOptionViewItem const& option,
                                         QModelIndex const& index) const
{
    QLineEdit* editor = new QLineEdit(parent);
    editor->setProperty("sceneEditor", true);
    editor->setAlignment(Qt::AlignCenter);
    return editor;
}

void SceneListDelegate::setEditorData(QWidget* editor, QModelIndex const& index) const
{
    QLineEdit* lineEdit = static_cast<QLineEdit*>(editor);
    quintptr rawPtr = index.data(Qt::UserRole).value<quintptr>();
    Scene* scene = reinterpret_cast<Scene*>(rawPtr);
    if (scene)
    {
        lineEdit->setText(QString::fromStdString(scene->getName()));
    }
}

void SceneListDelegate::setModelData(QWidget* editor,
                                     QAbstractItemModel* model,
                                     QModelIndex const& index) const
{
    QLineEdit* lineEdit = static_cast<QLineEdit*>(editor);
    QString newName = lineEdit->text().trimmed();

    if (newName.isEmpty())
        return;

    quintptr rawPtr = index.data(Qt::UserRole).value<quintptr>();
    Scene* scene = reinterpret_cast<Scene*>(rawPtr);

    if (scene)
    {
        // 1. 更新 Scene 对象名称
        scene->setName(newName.toStdString());

        // 2. 更新 Model 显示 (QListWidgetItem)
        model->setData(index, newName, Qt::EditRole);

        // 3. 触发持久化保存
        auto scenes = SceneManager::instance().getAllScenes();
        for (auto const& s : scenes)
        {
            if (s.get() == scene)
            {
                SceneManager::instance().saveScene(s);
                break;
            }
        }
    }
}

void SceneListDelegate::updateEditorGeometry(QWidget* editor,
                                             QStyleOptionViewItem const& option,
                                             QModelIndex const& index) const
{
    Q_UNUSED(index);
    QRect rect = option.rect;

    int marginX = 14;
    int thumbW = rect.width() - marginX * 2;
    if (thumbW > 240)
        thumbW = 240;
    int thumbH = thumbW * 9 / 16;
    int thumbX = rect.center().x() - thumbW / 2;
    int thumbY = rect.center().y() - thumbH / 2;
    QRect thumbRect(thumbX, thumbY, thumbW, thumbH);

    int textHeight = 26;
    QRect textRect(thumbRect.x(), thumbRect.bottom() - textHeight, thumbRect.width(), textHeight);

    editor->setGeometry(textRect);
}

void SceneListDelegate::clearHoverState()
{
    if (m_currentHoveredIndex.isValid())
    {
        m_currentHoveredIndex = QModelIndex();
    }

    QMapIterator<quintptr, QVariantAnimation*> i(m_animations);
    while (i.hasNext())
    {
        i.next();
        QVariantAnimation* anim = i.value();
        if (anim->direction() == QAbstractAnimation::Forward)
        {
            anim->setDirection(QAbstractAnimation::Backward);
            if (anim->state() == QAbstractAnimation::Stopped)
            {
                anim->start();
            }
        }
    }
}

void SceneListDelegate::startAnimation(quintptr itemPtr, bool open)
{
    if (m_animations.contains(itemPtr))
    {
        m_animations[itemPtr]->stop();
        m_animations[itemPtr]->deleteLater();
        m_animations.remove(itemPtr);
    }

    float startVal = m_animProgress.value(itemPtr, 0.0f);
    float endVal = open ? 1.0f : 0.0f;

    if (qAbs(startVal - endVal) < 0.01f)
        return;

    QVariantAnimation* anim = new QVariantAnimation(this);
    anim->setStartValue(startVal);
    anim->setEndValue(endVal);

    anim->setDuration(200);
    anim->setEasingCurve(QEasingCurve::OutQuad);

    connect(anim, &QVariantAnimation::valueChanged, this, [this, itemPtr](QVariant const& val) {
        m_animProgress[itemPtr] = val.toFloat();
        if (auto view = qobject_cast<QWidget*>(parent()))
        {
            view->update();
        }
    });

    connect(anim, &QVariantAnimation::finished, this, [this, itemPtr]() {
        if (m_animations.contains(itemPtr))
        {
            m_animations[itemPtr]->deleteLater();
            m_animations.remove(itemPtr);
        }
    });

    m_animations[itemPtr] = anim;
    anim->start();
}

void SceneListDelegate::drawTrashIcon(QPainter* painter,
                                      QRect const& rect,
                                      QColor const& color) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QPen pen(color);
    pen.setWidthF(1.5);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);

    int w = 12;
    int h = 13;
    QPoint center = rect.center();
    int x = center.x() - w / 2;
    int y = center.y() - h / 2;

    // [UI优化] 视觉重心修正
    y += 1;

    // 1. 桶身 (U型)
    QPainterPath bodyPath;
    bodyPath.moveTo(x, y + 2);
    bodyPath.lineTo(x + 1, y + h - 2);
    bodyPath.quadTo(x + w / 2, y + h, x + w - 1, y + h - 2);
    bodyPath.lineTo(x + w, y + 2);
    painter->drawPath(bodyPath);

    // 2. 桶盖
    painter->drawLine(x - 2, y + 2, x + w + 2, y + 2);

    // 3. 提手
    painter->drawArc(x + 4, y, 4, 3, 0, 180 * 16);

    // 4. 竖条纹
    painter->drawLine(x + 4, y + 5, x + 4, y + 9);
    painter->drawLine(x + 8, y + 5, x + 8, y + 9);

    painter->restore();
}

void SceneListDelegate::onAnimationValueChanged(QVariant const& value)
{
    Q_UNUSED(value);
}
