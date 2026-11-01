// [file: LayerManagerWidget.cpp]
#include "LayerManagerWidget.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QDebug>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <algorithm>
#include <QGraphicsDropShadowEffect>
#include "SceneItem.h"

// ============================================================================
// Internal Class: LayerStackView (Compact Version)
// ============================================================================
class LayerStackView : public QWidget
{
public:
    explicit LayerStackView(QWidget* parent = nullptr) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setSceneItems(std::vector<std::shared_ptr<SceneItem>> const& items)
    {
        m_items = items;
        // 保持 Z 轴排序，确保渲染顺序正确
        std::sort(m_items.begin(),
                  m_items.end(),
                  [](std::shared_ptr<SceneItem> const& a, std::shared_ptr<SceneItem> const& b) {
                      return a->getTransform().zOrder < b->getTransform().zOrder;
                  });
        update();
    }

    // [New] 设置选中项
    void setSelectedItem(std::shared_ptr<SceneItem> item)
    {
        m_selectedItem = item;
        update();
    }

protected:
    QColor getTypeColor(SourceType type)
    {
        switch (type)
        {
            case SourceType::Camera:
                return QColor(64, 158, 255);  // Blue
            case SourceType::Image:
                return QColor(103, 194, 58);  // Green
            case SourceType::ScreenCapture_WGC:
                return QColor(230, 162, 60);  // Orange
            case SourceType::Monitor:
                return QColor(230, 162, 60);  // Orange
            case SourceType::Text:
                return QColor(245, 108, 108);  // Red
            default:
                return QColor(144, 147, 153);  // Gray
        }
    }

    void paintEvent(QPaintEvent* event) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // 如果没有图层，绘制浅色提示文字
        if (m_items.empty())
        {
            p.setPen(QColor(150, 150, 150));  // 浅灰色，适应深色背景
            p.setFont(QFont("Microsoft YaHei UI", 8));
            p.drawText(rect(), Qt::AlignCenter, "暂无图层");
            return;
        }

        int count = m_items.size();
        int cardW = 100;
        int cardH = 28;

        int centerX = rect().width() / 2;
        int bottomY = rect().bottom() - 8;

        float stepY = -15.0f;

        // 动态计算堆叠高度
        if (count > 1)
        {
            float availableH = rect().height() - cardH - 10;
            float neededH = abs(stepY * (count - 1));
            if (neededH > availableH)
            {
                stepY = -(availableH / (count - 1));
            }
        }

        float startX = centerX - (cardW / 2.0f);
        float startY = bottomY - cardH;

        for (int i = 0; i < count; ++i)
        {
            auto item = m_items[i];
            float x = startX;
            float y = startY + (i * stepY);

            // [逻辑] 检查是否是当前选中项
            bool isSelected = false;
            auto selectedPtr = m_selectedItem.lock();
            if (selectedPtr && item == selectedPtr)
            {
                isSelected = true;
                // [特效] 如果选中，向左偏移 10像素，突出显示
                x -= 10;
            }

            drawCard(p, QRectF(x, y, cardW, cardH), item.get(), i, isSelected);
        }
    }

private:
    void drawCard(QPainter& p, QRectF rect, SceneItem* item, int index, bool isSelected)
    {
        if (!item || !item->getSource())
            return;

        SourceType type = item->getSource()->getType();
        QColor themeColor = getTypeColor(type);
        QString name = QString::fromStdString(item->getSource()->getName());

        p.save();

        // 1. 选中状态的高亮边框 (Golden/Bright)
        if (isSelected)
        {
            p.setPen(QPen(QColor(255, 255, 255, 200), 1.5));  // 亮白色边框
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(rect.adjusted(-1, -1, 1, 1), 4, 4);
        }

        // 2. 卡片阴影 (深色背景下需要更黑的阴影)
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 80));
        p.drawRoundedRect(rect.translated(0, 3), 3, 3);

        // 3. 卡片背景 (渐变)
        QLinearGradient grad(rect.topLeft(), rect.bottomLeft());
        if (isSelected)
        {
            // 选中：高亮色
            grad.setColorAt(0.0, themeColor.lighter(130));
            grad.setColorAt(1.0, themeColor);
        }
        else
        {
            // 未选中：稍微变暗，融入深色背景
            grad.setColorAt(0.0, themeColor.lighter(110));
            grad.setColorAt(1.0, themeColor.darker(110));
        }

        p.setBrush(grad);
        // 边框设为半透明，避免锯齿
        p.setPen(QPen(QColor(255, 255, 255, 30), 1));
        p.drawRoundedRect(rect, 3, 3);

        // 4. 顶部高光 (玻璃质感)
        p.setPen(QPen(QColor(255, 255, 255, isSelected ? 120 : 60), 1));
        p.drawLine(rect.left() + 2, rect.top() + 1, rect.right() - 2, rect.top() + 1);

        // 5. 文字
        p.setPen(Qt::white);
        p.setFont(QFont("Microsoft YaHei UI", 7, isSelected ? QFont::Bold : QFont::Normal));

        QRectF textRect = rect.adjusted(4, 0, -4, 0);
        p.drawText(textRect,
                   Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(name, Qt::ElideRight, textRect.width() - 12));

        // 6. 序号
        p.setFont(QFont("Arial", 6));
        p.setPen(QColor(255, 255, 255, 150));
        p.drawText(
            rect.adjusted(0, 0, -2, -1), Qt::AlignRight | Qt::AlignBottom, QString::number(index));

        p.restore();
    }

    std::vector<std::shared_ptr<SceneItem>> m_items;
    std::weak_ptr<SceneItem> m_selectedItem;
};

// ============================================================================
// LayerManagerWidget Implementation
// ============================================================================

LayerManagerWidget::LayerManagerWidget(QWidget* parent) : QWidget(parent)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(160, 120);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(0);

    // [UI优化] 标题颜色改为浅色，以适应深色玻璃背景
    QLabel* title = new QLabel("图层栈 (Layer Stack)", this);
    title->setStyleSheet(
        "color: #E0E0E0; font-size: 10px; font-weight: bold; background: transparent; font-family: "
        "'Segoe UI', 'Microsoft YaHei';");
    title->setFixedHeight(16);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    m_stackView = new LayerStackView(this);
    layout->addWidget(m_stackView);
}

void LayerManagerWidget::paintEvent(QPaintEvent* event)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRectF bgRect = rect().adjusted(1, 1, -1, -1);

    // [UI优化 - 核心] 半透明磨砂玻璃背景 (HUD 风格)
    // 颜色: 深蓝灰 (Dark Slate)
    // 透明度: 220/255 (约 86%)，保证能看清背后的视频轮廓，但不影响文字阅读
    QColor glassColor(30, 33, 40, 220);
    p.setBrush(glassColor);

    // [UI优化] 极细的微光边框
    // 让窗口边缘有一层淡淡的光晕，增加科技感
    p.setPen(QPen(QColor(255, 255, 255, 30), 1));

    p.drawRoundedRect(bgRect, 8, 8);  // 稍微增大圆角
}

void LayerManagerWidget::setScene(std::shared_ptr<Scene> scene)
{
    if (auto oldScene = m_currentScene.lock())
    {
        disconnect(oldScene.get(), nullptr, this, nullptr);
    }
    m_currentScene = scene;
    if (scene)
    {
        connect(scene.get(), &Scene::itemAdded, this, &LayerManagerWidget::onSceneContentChanged);
        connect(scene.get(), &Scene::itemRemoved, this, &LayerManagerWidget::onSceneContentChanged);
        connect(
            scene.get(), &Scene::contentChanged, this, &LayerManagerWidget::onSceneContentChanged);
        refreshList();
    }
    else
    {
        m_stackView->setSceneItems({});
    }
}

void LayerManagerWidget::setSelectedItem(std::shared_ptr<SceneItem> item)
{
    m_selectedItem = item;
    if (m_stackView)
    {
        m_stackView->setSelectedItem(item);
    }
}

void LayerManagerWidget::refreshList()
{
    auto scene = m_currentScene.lock();
    if (!scene)
    {
        m_stackView->setSceneItems({});
        return;
    }
    auto items = scene->getItems();
    std::vector<std::shared_ptr<SceneItem>> validItems;
    for (auto& item : items)
    {
        if (item->getSource())
        {
            validItems.push_back(item);
        }
    }
    m_stackView->setSceneItems(validItems);

    if (auto sel = m_selectedItem.lock())
    {
        m_stackView->setSelectedItem(sel);
    }
}

void LayerManagerWidget::onSceneContentChanged()
{
    refreshList();
}
