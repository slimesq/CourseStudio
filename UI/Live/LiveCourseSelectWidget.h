#ifndef LIVECOURSESELECTWIDGET_H
#define LIVECOURSESELECTWIDGET_H

#include <QWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPixmap>
#include "LiveCourseModel.h"

// 前置声明子组件
class LiveInfoPanel;
class LiveCarouselView;

class LiveCourseSelectWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LiveCourseSelectWidget(QWidget* parent = nullptr);
    ~LiveCourseSelectWidget();

    void loadRooms(QJsonArray const& roomList);
    void stopCardLoading(long long roomId);
    void loadBackground(QString const& path);

    // [Critical] 强制重写最小尺寸建议
    QSize minimumSizeHint() const override;

    void resetLayoutConstraints();
    void suspendRendering();
    void resumeRendering();

signals:
    void requestRoomDetail(long long roomId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void initUI();
    void initSignals();

private slots:
    void onCarouselIndexChanged(int index);
    void onCardSelected(long long roomId);
    void onEnterClicked();
    void onAssetDownloaded(QString const& url, QString const& localPath, bool isTemp);

private:
    LiveInfoPanel* m_infoPanel = nullptr;
    LiveCarouselView* m_carousel = nullptr;

    QList<CourseEntity> m_courseList;
    long long m_currentRoomId = -1;
    QPixmap m_bgPixmap;

    // [New] 导航锁，防止重复请求导致场景重复加载
    bool m_isNavigating = false;

    bool m_isInitialized = false;
};

#endif  // LIVECOURSESELECTWIDGET_H
