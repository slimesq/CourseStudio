#ifndef USERPROFILEWIDGET_H
#define USERPROFILEWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>

class UserProfileWidget : public QWidget
{
    Q_OBJECT
public:
    explicit UserProfileWidget(QWidget* parent = nullptr);

    // 更新显示的信息
    void updateInfo();

signals:
    void logoutRequested();

protected:
    // 点击窗口外部关闭
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private slots:
    // 监听资源下载完成
    void onAssetReady(QString const& url, QString const& localPath, bool isTemp);

private:
    void initUI();

private:
    QLabel* m_avatar;
    QLabel* m_nameLabel;
    QLabel* m_descLabel;
    QLabel* m_statsLabel;
    QPushButton* m_logoutBtn;
};

#endif  // USERPROFILEWIDGET_H
