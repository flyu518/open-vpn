#include "app_controller.h"
#include "tray_controller.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QIcon>
#include <QEvent>
#include <QLockFile>
#include <QDir>
#include <QMessageBox>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTimer>

class QuitConfirmationFilter final : public QObject {
public:
    explicit QuitConfirmationFilter(AppController& controller) : controller_(controller) {}
protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() != QEvent::Quit || controller_.quitting()) return false;
        controller_.quitApplication();
        return true;
    }
private:
    AppController& controller_;
};

int main(int argc, char* argv[]) {
    // 使用 Qt 的非原生控件样式，保证自定义按钮、卡片和悬停反馈在
    // macOS、Windows 上都保持一致，不被系统原生样式覆盖。
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("OpenVPN"));
    application.setOrganizationName(QStringLiteral("OpenVPN"));
    application.setApplicationVersion(QStringLiteral(OPENVPN_APP_VERSION));
    application.setWindowIcon(QIcon(QStringLiteral(":/openvpn/app-icon.png")));
    QApplication::setQuitOnLastWindowClosed(false);

    const QString appData = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);
    QString lockPath = qEnvironmentVariable("OPENVPN_QT_LOCK_PATH");
    if (lockPath.isEmpty()) {
        lockPath = QDir(appData).filePath(QStringLiteral("openvpn-ui.lock"));
    }
#ifdef Q_OS_WIN
    QString activationName = QStringLiteral("openvpn-ui-activate");
#else
    QString activationName = QStringLiteral("/tmp/openvpn-ui-activate.sock");
#endif
    if (!qEnvironmentVariableIsEmpty("OPENVPN_QT_LOCK_PATH")) {
        const QString suffix = QString::fromLatin1(QCryptographicHash::hash(
            lockPath.toUtf8(), QCryptographicHash::Sha256).toHex().left(12));
        activationName += QLatin1Char('-') + suffix;
    }
    QLockFile instanceLock(lockPath);
    instanceLock.setStaleLockTime(30000);
    if (!instanceLock.tryLock(0)) {
        QLocalSocket activationClient;
        activationClient.connectToServer(activationName);
        if (activationClient.waitForConnected(300)) {
            activationClient.write("activate\n");
            activationClient.flush();
            activationClient.waitForBytesWritten(300);
            return 0;
        }
        QMessageBox::warning(nullptr, QStringLiteral("OpenVPN 已在运行"),
                             QStringLiteral("已有 OpenVPN 实例正在运行，请先关闭已有窗口后再启动。"));
        return 0;
    }

    AppController controller(QSystemTrayIcon::isSystemTrayAvailable());
    QuitConfirmationFilter quitFilter(controller);
    if (qEnvironmentVariableIsEmpty("OPENVPN_QT_SCREENSHOT_PATH")) {
        application.installEventFilter(&quitFilter);
    }
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("controller"),
                                             &controller);
    engine.loadFromModule(QStringLiteral("OpenVPN"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) return 1;

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    TrayController tray(&controller, window, &application);
    QObject::connect(&application, &QGuiApplication::applicationStateChanged,
                     &tray, [&tray](Qt::ApplicationState state) {
                         if (state == Qt::ApplicationActive ||
                             state == Qt::ApplicationInactive) {
                             tray.scheduleTrayRecovery();
                         }
                     });
    QLocalServer activationServer;
    QLocalServer::removeServer(activationName);
    if (activationServer.listen(activationName)) {
        QObject::connect(&activationServer, &QLocalServer::newConnection,
                         &application, [&activationServer, &tray, window] {
                             while (activationServer.hasPendingConnections()) {
                                 QLocalSocket* socket =
                                     activationServer.nextPendingConnection();
                                 socket->readAll();
                                 socket->deleteLater();
                                 tray.scheduleTrayRecovery();
                                 if (window) {
                                     window->show();
                                     window->raise();
                                     window->requestActivate();
                                 }
                             }
                         });
    }
    QObject::connect(&controller, &AppController::quitRequested, &tray,
                     [&tray] { tray.hide(); });
    controller.start();
    const QString screenshotPath = qEnvironmentVariable("OPENVPN_QT_SCREENSHOT_PATH");
    if (!screenshotPath.isEmpty() && window) {
        QTimer::singleShot(800, &application, [window, screenshotPath, &application] {
            window->grabWindow().save(screenshotPath);
            application.quit();
        });
    }
    return application.exec();
}
