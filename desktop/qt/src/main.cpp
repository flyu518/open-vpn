#include "app_controller.h"
#include "tray_controller.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QIcon>
#include <QEvent>
#include <QLockFile>
#include <QDir>
#include <QMessageBox>
#include <QProcess>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QDebug>

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

#ifdef Q_OS_WIN
void ensureWindowsDaemonReady() {
    const QString pipeName = QStringLiteral(R"(\\.\pipe\openvpn-ui)");
    QLocalSocket probe;
    probe.connectToServer(pipeName);
    if (probe.waitForConnected(200)) {
        probe.disconnectFromServer();
        return;
    }
    // An access-denied pipe belongs to another security principal. Starting a
    // second daemon cannot repair that existing pipe and would only obscure
    // the real error; leave it for EngineClient to report in the UI.
    if (probe.error() == QLocalSocket::SocketAccessError) return;
    probe.abort();

    const QString appDirectory = QCoreApplication::applicationDirPath();
    const QString launcher = QDir(appDirectory).filePath(
        QStringLiteral("openvpn_windows_daemon_launcher.exe"));
    if (!QFile::exists(launcher)) {
        qWarning().noquote() << "OpenVPN Windows daemon launcher is missing:" << launcher;
        return;
    }
    if (!QProcess::startDetached(launcher, {}, appDirectory)) {
        qWarning().noquote() << "Unable to start OpenVPN Windows daemon:" << launcher;
        return;
    }

    probe.connectToServer(pipeName);
    if (!probe.waitForConnected(3000)) {
        qWarning().noquote() << "OpenVPN Windows daemon did not open its pipe:"
                             << probe.errorString();
        return;
    }
    probe.disconnectFromServer();
}
#endif

// The packaged Windows executable is also used for visual smoke tests.  This
// hook is inert unless the test-only environment variable is explicitly set;
// normal launches never take this path.  Keeping the scene selection in the
// executable lets us inspect the exact QML resources shipped in the package,
// instead of testing a separately built QML test fixture.
void openUiSmokeScene(const QString& scene,
                      QObject* root,
                      AppController& controller) {
    if (!root) return;

    const auto dialog = [root](const char* name) {
        return root->findChild<QObject*>(QString::fromLatin1(name));
    };
    const auto session = controller.sessionModel()->sessions().value(0);
    const QString sessionId = session.value(QStringLiteral("sessionId")).toString();

    if (scene == QStringLiteral("details") ||
        scene == QStringLiteral("route") ||
        scene == QStringLiteral("rename") ||
        scene == QStringLiteral("delete") ||
        scene == QStringLiteral("forget")) {
        QMetaObject::invokeMethod(root, "openDetails",
                                  Q_ARG(QVariant, sessionId),
                                  Q_ARG(QVariant, QString()));
    }
    if (scene == QStringLiteral("settings")) {
        if (auto* value = dialog("settingsDialog")) value->setProperty("visible", true);
    } else if (scene == QStringLiteral("about")) {
        if (auto* value = dialog("aboutDialog")) value->setProperty("visible", true);
    } else if (scene == QStringLiteral("details")) {
        if (auto* value = dialog("detailsDialog")) value->setProperty("visible", true);
    } else if (scene == QStringLiteral("route")) {
        if (auto* value = dialog("routePolicyDialog")) value->setProperty("visible", true);
    } else if (scene == QStringLiteral("rename")) {
        if (auto* value = dialog("renameDialog")) value->setProperty("visible", true);
    } else if (scene == QStringLiteral("delete")) {
        if (auto* value = dialog("deleteProfileDialog")) value->setProperty("visible", true);
    } else if (scene == QStringLiteral("forget")) {
        if (auto* value = dialog("forgetDialog")) value->setProperty("visible", true);
    } else if (scene == QStringLiteral("credentials")) {
        root->setProperty("credentialSessionId", sessionId);
        root->setProperty("credentialReason", QStringLiteral("请输入用户名和密码后连接。"));
        root->setProperty("credentialPending", false);
        if (auto* value = dialog("credentialsDialog")) value->setProperty("visible", true);
    } else if (scene == QStringLiteral("quit")) {
        root->setProperty("activeConnectionsAtQuit", 1);
        if (auto* value = dialog("quitDialog")) value->setProperty("visible", true);
    } else if (scene == QStringLiteral("update")) {
        if (auto* value = dialog("updateDialog")) value->setProperty("visible", true);
    } else if (scene == QStringLiteral("notice")) {
        root->setProperty("noticeTitle", QStringLiteral("测试提示"));
        root->setProperty("noticeMessage", QStringLiteral("这是最终打包版的操作反馈区域。"));
    }
}

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
    if (!qEnvironmentVariableIsEmpty("OPENVPN_QT_SCREENSHOT_PATH")) {
        QStandardPaths::setTestModeEnabled(true);
    }

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

#ifdef Q_OS_WIN
    ensureWindowsDaemonReady();
#endif

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
        const QString smokeScene = qEnvironmentVariable("OPENVPN_QT_UI_SMOKE_SCENE");
        QTimer::singleShot(900, &application,
                          [window, screenshotPath, smokeScene, &controller, &application] {
            if (!smokeScene.isEmpty()) {
                openUiSmokeScene(smokeScene, window, controller);
            }
            QTimer::singleShot(180, &application, [window, screenshotPath, &application] {
            window->grabWindow().save(screenshotPath);
            application.quit();
            });
        });
    }
    return application.exec();
}
