#ifndef LIVECAROUSELVIEW_H
#define LIVECAROUSELVIEW_H

#include <QWidget>
#include <QList>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QTimer>
#include "MentorCard.h"
#include "CarouselLayoutEngine.h"

class LiveCarouselView : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal scrollValue READ scrollValue WRITE setScrollValue)

public:
    explicit LiveCarouselView(QWidget* parent = nullptr);
    ~LiveCarouselView();

    void setData(CourseList const& data);
    void updateCardImage(long long roomId, QString const& localPath);

    // [Updated] 支持无限循环的滚动接口
    void scrollTo(int index);
    int currentIndex() const;  // 返回归一化后的索引 (0 ~ N-1)

    qreal scrollValue() const
    {
        return m_scrollValue;
    }
    void setScrollValue(qreal v);

    QSize minimumSizeHint() const override;

signals:
    void cardClicked(long long roomId);
    void indexChanged(int index);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private slots:
    void onProcessScrollBuffer();
    void onUpClicked();
    void onDownClicked();
    void onCardClicked(long long roomId);
    void updateStyles();

private:
    void clearCards();
    void updateLayout();
    void layoutButtons();
    void updateButtonStyles();

    // [New] 归一化索引，处理负数和超过长度的情况
    int normalizeIndex(int index) const;

private:
    QList<MentorCard*> m_cards;
    CarouselLayoutEngine m_layoutEngine;

    QPushButton* m_btnUp = nullptr;
    QPushButton* m_btnDown = nullptr;

    CourseList m_dataList;

    // [Updated] m_targetIndex 不再限制范围，可以是负数或无限大，用于动画插值
    int m_targetIndex = 0;
    qreal m_scrollValue = 0.0;
    QPropertyAnimation* m_scrollAnim = nullptr;

    int m_bufferedScrollSteps = 0;
    int m_wheelAccumulator = 0;
    QTimer* m_scrollThrottleTimer = nullptr;
};

#endif  // LIVECAROUSELVIEW_H
