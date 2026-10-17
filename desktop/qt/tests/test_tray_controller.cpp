#include "app_controller.h"
#include "test_tray_controller.h"
#include "tray_controller.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QPointer>
#include <QSignalSpy>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>

void TrayControllerTest::sessionActionTriggersControllerBeforeMenuRefresh() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppController controller(false,
                             directory.filePath(QStringLiteral("profiles.json")),
                             directory.filePath(QStringLiteral("missing.sock")));
    controller.sessionModel()->setSessions({QJsonObject{
        {QStringLiteral("sessionId"), QStringLiteral("profile-a")},
        {QStringLiteral("profileId"), QStringLiteral("profile-a")},
        {QStringLiteral("profilePath"), QStringLiteral("/tmp/profile-a.ovpn")},
        {QStringLiteral("profile"), QStringLiteral("client\n")},
        {QStringLiteral("name"), QStringLiteral("配置 A")},
        {QStringLiteral("state"), QStringLiteral("disconnected")},
        {QStringLiteral("routeStrategy"), QJsonObject{
            {QStringLiteral("conflictMode"), QStringLiteral("compatible")},
            {QStringLiteral("priority"), 1},
            {QStringLiteral("rules"), QJsonArray{}},
        }},
    }});

    TrayController tray(&controller, nullptr, &controller);
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        QTRY_VERIFY_WITH_TIMEOUT(controller.trayAvailable(), 1000);
    }
    QAction* action = nullptr;
    for (QAction* candidate : tray.menu()->actions()) {
        if (candidate->text() == QStringLiteral("连接：配置 A")) {
            action = candidate;
            break;
        }
    }
    QVERIFY(action != nullptr);
    QPointer<QAction> actionGuard(action);
    action->trigger();
    const QString state = controller.sessionModel()
                              ->data(controller.sessionModel()->index(0, 0),
                                     SessionListModel::StateRole)
                              .toString();
    QVERIFY(state == QStringLiteral("connecting") ||
            state == QStringLiteral("failed"));
    QVERIFY(actionGuard != nullptr);

    QCoreApplication::processEvents();
    QVERIFY(actionGuard != nullptr);
}

void TrayControllerTest::nativeMenuKeepsActionsAliveUntilItCloses() {
    QTemporaryDir directory;
    AppController controller(false, directory.filePath("profiles.json"), directory.filePath("missing.sock"));
    controller.sessionModel()->setSessions({QJsonObject{{"sessionId", "a"}, {"name", "A"}, {"state", "connected"}}});
    TrayController tray(&controller, nullptr);
    QPointer<QAction> action;
    for (auto* candidate : tray.menu()->actions()) if (candidate->data() == "a") action = candidate;
    QVERIFY(action);
    // Cocoa 原生菜单的生命周期不能用 QWidget::isVisible() 代替。
    QVERIFY(QMetaObject::invokeMethod(tray.menu(), "aboutToShow"));
    controller.sessionModel()->updateSession(QJsonObject{{"sessionId", "a"}, {"durationSeconds", 10}});
    QCoreApplication::processEvents();
    QVERIFY(action);
    QVERIFY(QMetaObject::invokeMethod(tray.menu(), "aboutToHide"));
    QCoreApplication::processEvents();
    QVERIFY2(action, "菜单关闭后，系统仍可能投递选中事件，不能因统计刷新删除动作");
    controller.sessionModel()->removeSession("a");
    QTRY_VERIFY(action.isNull());
}

void TrayControllerTest::secondSessionAndWindowActionsWork() {
    QTemporaryDir directory;
    QLocalServer server;
    QVERIFY(server.listen(directory.filePath("engine.sock")));
    QJsonObject received;
    connect(&server, &QLocalServer::newConnection, this, [&] {
        auto* socket = server.nextPendingConnection();
        connect(socket, &QLocalSocket::readyRead, socket, [&, socket] {
            while (socket->canReadLine()) {
                received = QJsonDocument::fromJson(socket->readLine()).object();
                socket->write("{\"ok\":true}\n");
                socket->flush();
            }
        });
    });
    AppController controller(false, directory.filePath("profiles.json"), server.fullServerName());
    controller.sessionModel()->setSessions({
        QJsonObject{{"sessionId", "a"}, {"profileId", "a"}, {"profile", "client\n"}, {"name", "A"}, {"state", "disconnected"}},
        QJsonObject{{"sessionId", "b"}, {"profileId", "b"}, {"profile", "client\n"}, {"name", "B"}, {"state", "disconnected"}}});
    QWindow window;
    TrayController tray(&controller, &window);
    QAction* second = nullptr;
    for (auto* action : tray.menu()->actions()) {
        if (action->data().toString() == "b") second = action;
    }
    QVERIFY(second);
    tray.menu()->popup(QPoint(50, 50));
    QTest::qWait(50);
    QTest::mouseClick(tray.menu(), Qt::LeftButton, Qt::NoModifier,
                      tray.menu()->actionGeometry(second).center());
    QTRY_COMPARE(received.value("sessionId").toString(), QStringLiteral("b"));
    QCOMPARE(received.value("command").toString(), QStringLiteral("connect"));
    QCOMPARE(controller.sessionModel()->session("a").value("state").toString(), QStringLiteral("disconnected"));
    QCoreApplication::processEvents();
    QSignalSpy settings(&controller, &AppController::settingsRequested);
    for (auto* action : tray.menu()->actions()) {
        if (action->text() == QStringLiteral("桌面设置")) action->trigger();
    }
    QCOMPARE(settings.count(), 1);
    QVERIFY(window.isVisible());
    window.hide();
    for (auto* action : tray.menu()->actions()) {
        if (action->text() == QStringLiteral("打开 OpenVPN")) action->trigger();
    }
    QVERIFY(window.isVisible());
    window.hide();
    QSignalSpy confirmQuit(&controller, &AppController::quitConfirmationRequired);
    for (auto* action : tray.menu()->actions()) {
        if (action->text() == QStringLiteral("退出")) action->trigger();
    }
    QCOMPARE(confirmQuit.count(), 1);
    QVERIFY(window.isVisible());
    QVERIFY(!controller.quitting());
    controller.cancelQuitApplication();
    window.hide();
}

void TrayControllerTest::recreatesTrayItemAfterMenuBarRefresh() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppController controller(false,
                             directory.filePath(QStringLiteral("profiles.json")),
                             directory.filePath(QStringLiteral("missing.sock")));
    TrayController tray(&controller, nullptr, &controller);
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        QSKIP("当前测试环境没有可用的系统状态栏");

    QVERIFY(tray.recoverTrayIcon());
    QVERIFY(controller.trayAvailable());
    QVERIFY(!tray.menu()->actions().isEmpty());
}

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    TrayControllerTest test;
    return QTest::qExec(&test, argc, argv);
}
