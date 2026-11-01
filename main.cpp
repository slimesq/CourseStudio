#include <QApplication>
#include "LoginWidget.h"
#include "WorkSpaceWindow.h"
#include "CrashHandler.h"

int main(int argc, char* argv[])
{
    CrashHandler::init();

    // 启用高分屏支持
    // [V7 优化]
    // 1. --disable-in-process-stack-traces: 减少崩溃时的开销
    // 2. --disable-accelerated-video-decode: 禁用视频加速，避免 WebEngine 在无窗口模式下崩溃
    // 3. --disable-features=UseSkiaRenderer: 禁用 Skia 渲染，尝试解决 D3D 冲突
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
            "--ignore-certificate-errors "
            "--disable-gpu "
            "--disable-d3d11 "
            "--no-sandbox "
            "--disable-web-security "
            "--remote-debugging-port=9222 "
            "--disable-accelerated-video-decode "
            "--disable-accelerated-video-encode "
            "--disable-features=UseSkiaRenderer");

    QApplication::setFont(QFont("Microsoft YaHei UI", 10));

    // 1. 开启高分屏缩放支持
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    // 2. 开启高分屏图片支持 (这一步对 QPixmap 的清晰度至关重要)
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    // [可选] 针对非整数缩放策略 (Qt 5.14+)
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif

    QApplication a(argc, argv);

    // 关键：设置应用程序全局图标（资源路径对应 qrc 中的 prefix + 文件名/别名）
    a.setWindowIcon(QIcon(":/UI/QSS/icon/CS.png"));

    // 1. 创建登录窗口
    LoginWidget* login = new LoginWidget();

    // 2. 核心逻辑：连接登录完成信号
    // 注意：这里使用 [=] 捕获指针，以便在 Lambda 内部使用
    QObject::connect(login, &LoginWidget::loginCompleted, [=]() {
        // A. 创建并显示工作台主窗口
        WorkSpaceWindow* workspace = new WorkSpaceWindow();
        workspace->show();

        // B. 关闭并释放登录窗口
        // 使用 deleteLater 确保在当前事件循环结束后安全释放内存
        login->close();
        login->deleteLater();
    });

    // 3. 显示登录窗口
    login->show();

    return a.exec();
}
