#include "app_controller.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QDir>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QCryptographicHash>
#include <QXmlStreamReader>
#include <QApplication>
#include <QStandardPaths>
#include <QTimer>
#include <QtTest>

namespace {

QJsonObject testProfile(const QString& id, const QString& state) {
    return QJsonObject{
        {QStringLiteral("sessionId"), id},
        {QStringLiteral("profileId"), id},
        {QStringLiteral("profilePath"), QStringLiteral("/tmp/") + id + ".ovpn"},
        {QStringLiteral("profile"), QStringLiteral("client\n")},
        {QStringLiteral("name"), id},
        {QStringLiteral("state"), state},
        {QStringLiteral("routeStrategy"),
         QJsonObject{
             {QStringLiteral("conflictMode"), QStringLiteral("compatible")},
             {QStringLiteral("priority"), 1},
             {QStringLiteral("rules"), QJsonArray{}},
         }},
    };
}

struct LocalDaemon {
    QLocalServer server;
    QPointer<QLocalSocket> peer;
    QList<QJsonObject> requests;
    std::function<QJsonObject(const QJsonObject&)> handler;

    explicit LocalDaemon(const QString& path) {
        server.listen(path);
        QObject::connect(&server, &QLocalServer::newConnection, &server, [this] {
            peer = server.nextPendingConnection();
            peer->setParent(&server);
            auto* socket = peer.data();
            QObject::connect(socket, &QLocalSocket::readyRead, socket, [this, socket] {
                while (socket->canReadLine()) {
                    const auto request = QJsonDocument::fromJson(socket->readLine()).object();
                    requests.append(request);
                    const auto reply = handler ? handler(request) : QJsonObject{{"ok", true}};
                    if (!reply.isEmpty()) send(reply);
                }
            });
        });
    }
    void send(const QJsonObject& object) {
        if (!peer) return;
        peer->write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
        peer->flush();
    }
    int count(const QString& command) const {
        return std::count_if(requests.cbegin(), requests.cend(), [&](const auto& request) {
            return request.value("command") == command;
        });
    }
};

void deliverEvent(AppController& controller, const QJsonObject& event) {
    QVERIFY(QMetaObject::invokeMethod(&controller, "onEngineEvent", Q_ARG(QJsonObject, event)));
}

}  // namespace

class AppControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void routeConflictSnapshotKeepsNamesAndValidatesDetails();
    void snapshotRecordsTrafficAndDeleteRemovesIt();
    void editsRouteStrategyRulesAndMode();
    void refusesToRemoveActiveProfile();
    void removesDisconnectedProfileFromIndex();
    void rejectsDuplicateProfileContents();
    void allowsChangedContentAtSamePath();
    void preservesDifferentIdentitiesAtSameEndpoint();
    void failedSaveDoesNotMutateProfiles();
    void refusesSamePathUpdateWhileActive();
    void savedCredentialsAndIndependentConnections();
    void authenticationCancelAndRetry();
    void normalErrorsDoNotPrompt_data();
    void normalErrorsDoNotPrompt();
    void credentialManagerPermissionPromptsForCredentials();
    void eventsPreserveMetadataAndRecordRealTransitions();
    void connectionTimeoutEventBecomesReadableFailure();
    void externalSessionsDisappearWithoutPersisting();
    void priorityOrderPersistsAndFailedSaveReturnsFalse();
    void update404IsNotSuccess();
    void rejectsUntrustedUpdateAssets();
    void verifiesInstallerBeforeWriting();
    void startupFileIsAtomicAndEscaped();
    void quitWithActiveConnectionsRequiresConfirmation();
    void malformedSnapshotDoesNotDisconnectOtherSessions();
    void lateConnectResponseDoesNotUndoCancellation();
    void failedRetryCleanupDoesNotConnect();
    void generatedLookingCustomNameSurvivesReload();
    void runtimeOnlyEditsDoNotOverwriteUnloadedIndex();
    void unverifiedDownloadDoesNotStartNetwork();
    void screenshotQuitDoesNotTouchDaemon();
    void cancellingSubmittedCredentialsCancelsOnlyThatSession();
    void managedCopyWriteFailurePreservesModelAndIndex();
    void selectsMatchingInstallerArchitecture();
    void snapshotsDoNotCreateConnectionHistory();
    void connectedEventAfterSnapshotCountsOnce();
    void localConnectionCompletedBySnapshotCountsOnce();
#ifdef Q_OS_WIN
    void importDialogStartsInDownloadsDirectory();
    void windowsStartupShortcutIsCreatedVerifiedAndRemoved();
    void windowsStartupShortcutDoesNotReplaceAnotherTarget();
#endif
};

void AppControllerTest::editsRouteStrategyRulesAndMode() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppController controller(false,
                             directory.filePath(QStringLiteral("profiles.json")));
    controller.sessionModel()->setSessions(
        {testProfile(QStringLiteral("company"), QStringLiteral("disconnected"))});

    controller.addRouteRule(QStringLiteral("company"),
                            QStringLiteral("domainSuffix"),
                            QStringLiteral("*.corp.example.com"));
    QCOMPARE(controller.routeRules(QStringLiteral("company")).size(), 1);
    QCOMPARE(controller.routeRules(QStringLiteral("company")).first()
                 .toMap()
                 .value(QStringLiteral("pattern"))
                 .toString(),
             QStringLiteral("corp.example.com"));

    controller.setConflictMode(QStringLiteral("company"), QStringLiteral("strict"));
    QCOMPARE(controller.detailsForSession(QStringLiteral("company"))
                 .value(QStringLiteral("conflictMode"))
                 .toString(),
             QStringLiteral("strict"));

    controller.removeRouteRule(QStringLiteral("company"), 0);
    QCOMPARE(controller.routeRules(QStringLiteral("company")).size(), 0);
}

void AppControllerTest::refusesToRemoveActiveProfile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppController controller(false,
                             directory.filePath(QStringLiteral("profiles.json")));
    controller.sessionModel()->setSessions(
        {testProfile(QStringLiteral("active"), QStringLiteral("connected"))});
    QSignalSpy notices(&controller, &AppController::notice);

    controller.removeProfile(QStringLiteral("active"));

    QCOMPARE(controller.sessionModel()->rowCount(), 1);
    QVERIFY(!notices.isEmpty());
    QCOMPARE(notices.last().at(0).toString(), QStringLiteral("无法删除配置"));
}

void AppControllerTest::removesDisconnectedProfileFromIndex() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString indexPath = directory.filePath(QStringLiteral("profiles.json"));
    AppController controller(false, indexPath);
    controller.sessionModel()->setSessions(
        {testProfile(QStringLiteral("idle"), QStringLiteral("disconnected"))});

    controller.removeProfile(QStringLiteral("idle"));

    QCOMPARE(controller.sessionModel()->rowCount(), 0);
    ProfileStore store(indexPath);
    QVERIFY(store.load().isEmpty());
}

void AppControllerTest::rejectsDuplicateProfileContents() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString firstPath = directory.filePath(QStringLiteral("first.ovpn"));
    const QString secondPath = directory.filePath(QStringLiteral("copy.ovpn"));
    for (const QString& path : {firstPath, secondPath}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("client\nremote vpn.example.test 1194 udp\n") > 0);
    }

    AppController controller(false,
                             directory.filePath(QStringLiteral("profiles.json")),
                             directory.filePath(QStringLiteral("missing.sock")));
    QSignalSpy notices(&controller, &AppController::notice);
    QVERIFY(controller.importProfilePath(firstPath));
    QVERIFY(!controller.importProfilePath(secondPath));
    QCOMPARE(controller.sessionModel()->rowCount(), 1);
    QVERIFY(!notices.isEmpty());
    QCOMPARE(notices.last().at(0).toString(), QStringLiteral("配置已存在"));
}

void AppControllerTest::allowsChangedContentAtSamePath() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString profilePath = directory.filePath(QStringLiteral("updated.ovpn"));
    QFile file(profilePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("client\nremote first.example.test 1194 udp\n") > 0);
    file.close();

    AppController controller(false,
                             directory.filePath(QStringLiteral("profiles.json")),
                             directory.filePath(QStringLiteral("missing.sock")));
    QVERIFY(controller.importProfilePath(profilePath));
    const QString id = controller.sessionModel()->sessions().first().value("sessionId").toString();
    controller.renameSession(id, "Custom name");
    controller.setConflictMode(id, "strict");
    controller.addRouteRule(id, "domain", "private.example");

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(file.write("client\nremote second.example.test 443 tcp\n") > 0);
    file.close();

    QVERIFY(controller.importProfilePath(profilePath));
    QCOMPARE(controller.sessionModel()->rowCount(), 1);
    QCOMPARE(controller.detailsForSession(id).value("name").toString(), QString("Custom name"));
    QCOMPARE(controller.detailsForSession(id).value("conflictMode").toString(), QString("strict"));
    QCOMPARE(controller.routeRules(id).size(), 1);
    QCOMPARE(controller.sessionModel()
                 ->data(controller.sessionModel()->index(0, 0),
                        SessionListModel::RemoteHostRole)
                 .toString(),
             QStringLiteral("second.example.test"));
}

void AppControllerTest::preservesDifferentIdentitiesAtSameEndpoint() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString firstPath = directory.filePath(QStringLiteral("first.ovpn"));
    const QString secondPath = directory.filePath(QStringLiteral("second.ovpn"));
    {
        QFile file(firstPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("client\nremote vpn.example.test 1194 udp\n# OVPN_ACCESS_SERVER_USERNAME=alice\n<cert>\nCERT-A\n</cert>\n") > 0);
    }
    {
        QFile file(secondPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("client\nremote vpn.example.test 1194 udp\n# OVPN_ACCESS_SERVER_USERNAME=bob\n<cert>\nCERT-B\n</cert>\n") > 0);
    }

    AppController controller(false,
                             directory.filePath(QStringLiteral("profiles.json")),
                             directory.filePath(QStringLiteral("missing.sock")));
    QVERIFY(controller.importProfilePath(firstPath));
    QVERIFY(controller.importProfilePath(secondPath));
    QCOMPARE(controller.sessionModel()->rowCount(), 2);
}

void AppControllerTest::failedSaveDoesNotMutateProfiles() {
    QTemporaryDir directory;
    const QString index = directory.filePath("profiles.json");
    QFile broken(index);
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("broken index");
    broken.close();
    AppController controller(false, index, directory.filePath("missing.sock"));
    const auto original = QList<QJsonObject>{testProfile("a", "disconnected"), testProfile("b", "disconnected")};
    controller.sessionModel()->setSessions(original);
    QSignalSpy notices(&controller, &AppController::notice);
    controller.renameSession("a", "Lost rename");
    QCOMPARE(controller.sessionModel()->sessions(), original);
    controller.removeProfile("a");
    QCOMPARE(controller.sessionModel()->sessions(), original);
    controller.setConflictMode("a", "strict");
    QCOMPARE(controller.sessionModel()->sessions(), original);
    controller.moveSession("a", "b");
    QCOMPARE(controller.sessionModel()->sessions(), original);
    QFile source(directory.filePath("new.ovpn"));
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("client\nremote new.example\n");
    source.close();
    QVERIFY(!controller.importProfilePath(source.fileName()));
    QCOMPARE(controller.sessionModel()->sessions(), original);
    QVERIFY(!controller.storageError().isEmpty());
    for (const auto& notice : notices) QVERIFY(notice.at(0).toString() != "导入成功");
    QVERIFY(broken.open(QIODevice::ReadOnly));
    QCOMPARE(broken.readAll(), QByteArray("broken index"));
}

void AppControllerTest::refusesSamePathUpdateWhileActive() {
    QTemporaryDir directory;
    QFile source(directory.filePath("source.ovpn"));
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("client\nremote first.example\n");
    source.close();
    AppController controller(false, directory.filePath("profiles.json"));
    QVERIFY(controller.importProfilePath(source.fileName()));
    const QString id = controller.sessionModel()->sessions().first().value("sessionId").toString();
    controller.sessionModel()->updateSession(QJsonObject{{"sessionId", id}, {"state", "connected"}});
    const auto original = controller.sessionModel()->sessions();
    QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
    source.write("client\nremote second.example\n");
    source.close();
    QVERIFY(!controller.importProfilePath(source.fileName()));
    QCOMPARE(controller.sessionModel()->sessions(), original);
}

void AppControllerTest::savedCredentialsAndIndependentConnections() {
    QTemporaryDir directory;
    LocalDaemon daemon(directory.filePath("engine.sock"));
    QVERIFY(daemon.server.isListening());
    QSet<QString> active;
    daemon.handler = [&active](const auto& request) {
        const auto id = request.value("sessionId").toString();
        if (request.value("command") == "connect") active.insert(id);
        if (request.value("command") == "disconnect") active.remove(id);
        if (request.value("command") == "connect") return QJsonObject{{"ok", true}, {"session", QJsonObject{{"sessionId", request.value("sessionId")}, {"state", "connecting"}}}};
        if (request.value("command") == "snapshot") {
            QJsonArray sessions;
            for (const auto& current : active) sessions.append(QJsonObject{{"sessionId", current}, {"state", "connected"}});
            return QJsonObject{{"ok", true}, {"sessions", sessions}};
        }
        return QJsonObject{{"ok", true}};
    };
    AppController controller(false, directory.filePath("profiles.json"), daemon.server.fullServerName());
    auto a = testProfile("legacy-a", "disconnected");
    a.insert("username", "alice");
    controller.sessionModel()->setSessions({a, testProfile("b", "disconnected")});
    controller.toggleConnection("legacy-a");
    controller.toggleConnection("b");
    QTRY_COMPARE(daemon.count("connect"), 2);
    QCOMPARE(daemon.requests.first().value("profileId").toString(), QString("legacy-a"));
    const auto credentials = daemon.requests.first().value("credentials").toObject();
    for (const auto& field : credentials) QVERIFY(field.toString().isEmpty());
    daemon.send(QJsonObject{{"type", "sessionEvent"}, {"sessionId", "b"}, {"event", "CONNECTED"}, {"session", QJsonObject{{"sessionId", "b"}, {"state", "connected"}, {"downloadBytes", 345}, {"uploadBytes", 67}, {"durationSeconds", 9}}}});
    QTRY_COMPARE(controller.sessionModel()->session("b").value("state").toString(), QString("connected"));
    controller.toggleConnection("legacy-a");
    QTRY_COMPARE(controller.sessionModel()->session("legacy-a").value("state").toString(), QString("disconnected"));
    QCOMPARE(controller.sessionModel()->session("b").value("state").toString(), QString("connected"));
    QCOMPARE(controller.sessionModel()->session("b").value("downloadBytes").toInt(), 345);
    QCOMPARE(controller.sessionModel()->session("b").value("durationSeconds").toInt(), 9);
    controller.toggleConnection("b");
    QTRY_COMPARE(controller.sessionModel()->session("b").value("state").toString(), QString("disconnected"));
}

void AppControllerTest::authenticationCancelAndRetry() {
    QTemporaryDir directory;
    LocalDaemon daemon(directory.filePath("engine.sock"));
    daemon.handler = [](const auto& request) {
        if (request.value("command") == "connect") {
            if (request.value("credentials").toObject().value("password") == "correct")
                return QJsonObject{{"ok", true}, {"session", QJsonObject{{"sessionId", "a"}, {"state", "connected"}}}};
            return QJsonObject{{"ok", false}, {"message", "AUTH_FAILED"}};
        }
        return QJsonObject{{"ok", true}};
    };
    AppController controller(false, directory.filePath("profiles.json"), daemon.server.fullServerName());
    controller.sessionModel()->setSessions({testProfile("a", "disconnected")});
    QSignalSpy prompts(&controller, &AppController::credentialsRequired);
    controller.toggleConnection("a");
    QTRY_COMPARE(prompts.size(), 1);
    QCOMPARE(controller.sessionModel()->session("a").value("state").toString(), QString("failed"));
    controller.cancelCredentials("a");
    deliverEvent(controller, QJsonObject{{"sessionId", "a"}, {"event", "AUTH_FAILED"}, {"message", "AUTH_FAILED"}, {"session", QJsonObject{{"sessionId", "a"}, {"state", "failed"}}}});
    QCOMPARE(prompts.size(), 1);
    controller.toggleConnection("a");
    QTRY_COMPARE(prompts.size(), 2);
    controller.submitCredentials("a", "alice", "correct", true);
    QTRY_COMPARE(controller.sessionModel()->session("a").value("state").toString(), QString("connected"));
    QCOMPARE(controller.detailsForSession("a").value("connectionCount").toInt(), 1);
    QCOMPARE(daemon.requests.last().value("rememberCredentials").toBool(), true);
    QVERIFY(!controller.sessionModel()->session("a").contains("password"));
}

void AppControllerTest::normalErrorsDoNotPrompt_data() {
    QTest::addColumn<QString>("message");
    QTest::newRow("network") << QString("TLS authentication timeout");
    QTest::newRow("keychain") << QString("Keychain permission denied");
    QTest::newRow("configuration") << QString("certificate authentication configuration invalid");
}

void AppControllerTest::normalErrorsDoNotPrompt() {
    QFETCH(QString, message);
    QTemporaryDir directory;
    LocalDaemon daemon(directory.filePath("engine.sock"));
    daemon.handler = [message](const auto&) { return QJsonObject{{"ok", false}, {"message", message}}; };
    AppController controller(false, directory.filePath("profiles.json"), daemon.server.fullServerName());
    controller.sessionModel()->setSessions({testProfile("a", "disconnected")});
    QSignalSpy prompts(&controller, &AppController::credentialsRequired);
    controller.toggleConnection("a");
    QTRY_COMPARE(controller.sessionModel()->session("a").value("state").toString(), QString("failed"));
    QCOMPARE(prompts.size(), 0);
    QCOMPARE(controller.sessionModel()->session("a").value("error").toString(), message);
}

void AppControllerTest::credentialManagerPermissionPromptsForCredentials() {
    QTemporaryDir directory;
    LocalDaemon daemon(directory.filePath("engine.sock"));
    daemon.handler = [](const auto&) {
        return QJsonObject{{"ok", false},
                           {"message", QStringLiteral("读取凭据：Credential Manager 权限不足")}};
    };
    AppController controller(false, directory.filePath("profiles.json"), daemon.server.fullServerName());
    controller.sessionModel()->setSessions({testProfile("a", "disconnected")});
    QSignalSpy prompts(&controller, &AppController::credentialsRequired);

    controller.toggleConnection("a");

    QTRY_COMPARE(prompts.size(), 1);
    QCOMPARE(controller.sessionModel()->session("a").value("state").toString(),
             QStringLiteral("failed"));
}

void AppControllerTest::eventsPreserveMetadataAndRecordRealTransitions() {
    QTemporaryDir directory;
    AppController controller(false, directory.filePath("profiles.json"));
    controller.sessionModel()->setSessions({testProfile("a", "connecting")});
    deliverEvent(controller, QJsonObject{{"sessionId", "a"}, {"event", "CONNECTED"}});
    QCOMPARE(controller.sessionModel()->session("a").value("state").toString(), QString("connected"));
    QCOMPARE(controller.detailsForSession("a").value("connectionCount").toInt(), 1);
    deliverEvent(controller, QJsonObject{{"sessionId", "a"}, {"event", "LOG"}, {"session", QJsonObject{{"sessionId", "a"}, {"name", "stale daemon name"}, {"profile", "hostile"}, {"state", "connected"}, {"durationSeconds", 13}, {"downloadBytes", 2048}, {"uploadBytes", 512}}}});
    QCOMPARE(controller.detailsForSession("a").value("name").toString(), QString("a"));
    QCOMPARE(controller.sessionModel()->session("a").value("profile").toString(), QString("client\n"));
    QCOMPARE(controller.sessionModel()->session("a").value("durationSeconds").toInt(), 13);
    QCOMPARE(controller.detailsForSession("a").value("connectionCount").toInt(), 1);
    deliverEvent(controller, QJsonObject{{"sessionId", "a"}, {"event", "DISCONNECTED"}});
    QCOMPARE(controller.sessionModel()->session("a").value("state").toString(), QString("disconnected"));
    QVERIFY(!controller.detailsForSession("a").value("lastDisconnectedAt").toString().isEmpty());
}

void AppControllerTest::connectionTimeoutEventBecomesReadableFailure() {
    QTemporaryDir directory;
    AppController controller(false, directory.filePath("profiles.json"));
    controller.sessionModel()->setSessions({testProfile("a", "connecting")});

    deliverEvent(controller, QJsonObject{
        {"sessionId", "a"},
        {"event", "CONNECTION_TIMEOUT"},
        {"session", QJsonObject{{"sessionId", "a"}, {"state", "failed"}}},
    });

    QCOMPARE(controller.sessionModel()->session("a").value("state").toString(),
             QStringLiteral("failed"));
    QCOMPARE(controller.sessionModel()->session("a").value("error").toString(),
             QStringLiteral("连接超时，请检查网络、服务器和 OpenVPN 服务。"));
}

void AppControllerTest::externalSessionsDisappearWithoutPersisting() {
    QTemporaryDir directory;
    const QString path = directory.filePath("profiles.json");
    AppController controller(false, path);
    controller.sessionModel()->setSessions({testProfile("legacy", "disconnected")});
    QVERIFY(controller.renameSession("legacy", "Custom"));
    controller.applySnapshot(QJsonArray{QJsonObject{{"sessionId", "acceptance-a"}, {"name", "External"}, {"state", "connected"}, {"downloadBytes", 123}, {"profileId", "hostile"}, {"profilePath", "/hostile"}}});
    QCOMPARE(controller.sessionModel()->rowCount(), 2);
    QVERIFY(controller.sessionModel()->session("acceptance-a").value("profilePath").toString().isEmpty());
    QVERIFY(controller.renameSession("legacy", "Still custom"));
    ProfileStore store(path);
    QCOMPARE(store.load().size(), 1);
    QCOMPARE(store.load().first().value("profileId").toString(), QString("legacy"));
    controller.applySnapshot({});
    QCOMPARE(controller.sessionModel()->rowCount(), 1);
    QCOMPARE(controller.sessionModel()->session("legacy").value("name").toString(), QString("Still custom"));
    QVERIFY(controller.sessionModel()->session("acceptance-a").isEmpty());
}

void AppControllerTest::priorityOrderPersistsAndFailedSaveReturnsFalse() {
    QTemporaryDir directory;
    const QString path = directory.filePath("profiles.json");
    AppController controller(false, path);
    controller.sessionModel()->setSessions({testProfile("a", "disconnected"), testProfile("b", "disconnected")});
    QVERIFY(controller.moveSession("b", "a"));
    ProfileStore store(path);
    const auto loaded = store.load();
    QCOMPARE(loaded.first().value("profileId").toString(), QString("b"));
    QCOMPARE(loaded.first().value("routeStrategy").toObject().value("priority").toInt(), 1);
    QCOMPARE(loaded.last().value("routeStrategy").toObject().value("priority").toInt(), 2);
    QFile file(path);
    QVERIFY(file.remove());
    QVERIFY(QDir().mkdir(path));
    QVERIFY(!controller.renameSession("a", "Unsaved"));
    QVERIFY(!controller.removeProfile("a"));
    QCOMPARE(controller.sessionModel()->session("a").value("name").toString(), QString("a"));
}

void AppControllerTest::update404IsNotSuccess() {
    QTemporaryDir directory;
    AppController controller(false, directory.filePath("profiles.json"));
    controller.applyReleaseResponse("{}", 404, QNetworkReply::ContentNotFoundError);
    QCOMPARE(controller.updateState(), QString("none"));
    QVERIFY(controller.updateMessage().contains("可访问"));
    QVERIFY(!controller.updateAvailable());
    QVERIFY(!controller.updateInstallerAvailable());
}

void AppControllerTest::rejectsUntrustedUpdateAssets() {
    QTemporaryDir directory;
    QCoreApplication::setApplicationVersion("0.2.2");
    AppController controller(false, directory.filePath("profiles.json"));
    const auto payload = QJsonDocument(QJsonObject{{"tag_name", "v0.3.0"},
        {"html_url", "file:///tmp/evil"}, {"assets", QJsonArray{QJsonObject{
            {"name", "../../evil.pkg"}, {"browser_download_url", "file:///tmp/evil.pkg"},
            {"size", 10}, {"digest", "sha256:" + QString(64, 'a')}}}}}).toJson();
    controller.applyReleaseResponse(payload, 200, QNetworkReply::NoError);
    QVERIFY(!controller.updateInstallerAvailable());
    QVERIFY(controller.updateReleaseUrl_.scheme() != "file");
    controller.applyReleaseResponse("{\"tag_name\":\"v999999999999999999999.0.0\"}", 200, QNetworkReply::NoError);
    QCOMPARE(controller.updateState(), QString("error"));
    controller.applyReleaseResponse("{\"tag_name\":\"v0.3.0\",\"prerelease\":true}", 200, QNetworkReply::NoError);
    QVERIFY(!controller.updateAvailable());
}

void AppControllerTest::verifiesInstallerBeforeWriting() {
    QTemporaryDir directory;
    QCoreApplication::setApplicationVersion("0.2.2");
    AppController controller(false, directory.filePath("profiles.json"));
    const QByteArray data("installer-fixture");
    const QString digest = "sha256:" + QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
#if defined(Q_OS_WIN)
    const QString installerName = QStringLiteral("OpenVPN-windows-x64.exe");
#else
    const QString installerName = QStringLiteral("OpenVPN-macos-universal.pkg");
#endif
    const auto release = QJsonObject{{"tag_name", "v0.3.0"}, {"html_url", "https://github.com/flyu518/open-vpn/releases/tag/v0.3.0"},
        {"assets", QJsonArray{QJsonObject{{"name", installerName}, {"browser_download_url", "https://github.com/flyu518/open-vpn/releases/download/v0.3.0/" + installerName},
                                        {"size", data.size()}, {"digest", digest}}}}};
    controller.applyReleaseResponse(QJsonDocument(release).toJson(), 200, QNetworkReply::NoError);
    QVERIFY(controller.updateInstallerAvailable());
    const QString path = directory.filePath("installer.pkg");
    QString error;
    QVERIFY(!controller.saveUpdatePayload("tampered-fixture!", path, &error));
    QVERIFY(!QFileInfo::exists(path));
    QVERIFY(!controller.saveUpdatePayload({}, path, &error));
    QVERIFY(controller.saveUpdatePayload(data, path, &error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), data);
    file.close();
    QVERIFY(!controller.saveUpdatePayload("bad", path, &error));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), data);
    QVERIFY(!controller.saveUpdatePayload(data, directory.path(), &error));
}

void AppControllerTest::startupFileIsAtomicAndEscaped() {
    QTemporaryDir directory;
    const QString path = directory.filePath("LaunchAgents/com.openvpn.desktop.plist");
    const QString executable = "/Applications/O&V<PN>\"'/OpenVPN";
    QString error;
    QVERIFY(AppController::writeStartupFile(path, executable, true, &error));
    QVERIFY(AppController::startupFileEnabled(path, executable, &error));
    QVERIFY(!AppController::startupFileEnabled(path, "/other-app", &error));
    QVERIFY(!error.isEmpty());
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QXmlStreamReader xml(file.readAll());
    bool foundExecutable = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QStringLiteral("string") && xml.readElementText() == executable) foundExecutable = true;
    }
    QVERIFY(!xml.hasError());
    QVERIFY(foundExecutable);
    file.close();
    QVERIFY(AppController::writeStartupFile(path, executable, false, &error));
    QVERIFY(!QFileInfo::exists(path));
    QVERIFY(!AppController::startupFileEnabled(path, executable, &error));
    QVERIFY(AppController::writeStartupFile(path, executable, false, &error));
    QVERIFY(!AppController::writeStartupFile(directory.path(), executable, true, &error));
}

#ifdef Q_OS_WIN
void AppControllerTest::windowsStartupShortcutIsCreatedVerifiedAndRemoved() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString executable = directory.filePath(QStringLiteral("OpenVPN.exe"));
    QFile executableFile(executable);
    QVERIFY(executableFile.open(QIODevice::WriteOnly));
    QCOMPARE(executableFile.write("test"), qint64(4));
    executableFile.close();

    const QString shortcut = directory.filePath(QStringLiteral("Startup/OpenVPN.lnk"));
    QString error;
    QVERIFY2(AppController::writeStartupShortcut(shortcut, executable, true, &error),
             qPrintable(error));
    QCOMPARE(QDir::cleanPath(QFile::symLinkTarget(shortcut)),
             QDir::cleanPath(QFileInfo(executable).absoluteFilePath()));
    QVERIFY(AppController::startupShortcutEnabled(shortcut, executable, &error));
    QVERIFY(!AppController::startupShortcutEnabled(
        shortcut, directory.filePath(QStringLiteral("other.exe")), &error));
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY2(AppController::writeStartupShortcut(shortcut, executable, false, &error),
             qPrintable(error));
    QVERIFY(!QFileInfo::exists(shortcut));
}

void AppControllerTest::windowsStartupShortcutDoesNotReplaceAnotherTarget() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString firstExecutable = directory.filePath(QStringLiteral("first.exe"));
    const QString secondExecutable = directory.filePath(QStringLiteral("second.exe"));
    for (const auto& path : {firstExecutable, secondExecutable}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("test"), qint64(4));
    }

    const QString shortcut = directory.filePath(QStringLiteral("Startup/OpenVPN.lnk"));
    QString error;
    QVERIFY(AppController::writeStartupShortcut(shortcut, firstExecutable, true, &error));
    QVERIFY(!AppController::writeStartupShortcut(shortcut, secondExecutable, true, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(QDir::cleanPath(QFile::symLinkTarget(shortcut)),
             QDir::cleanPath(QFileInfo(firstExecutable).absoluteFilePath()));
}
#endif

void AppControllerTest::quitWithActiveConnectionsRequiresConfirmation() {
    QTemporaryDir directory;
    LocalDaemon daemon(directory.filePath("engine.sock"));
    AppController controller(false, directory.filePath("profiles.json"), daemon.server.fullServerName());
    controller.sessionModel()->setSessions({testProfile("a", "connected")});
    QSignalSpy quit(&controller, &AppController::quitRequested);
    QSignalSpy prompts(&controller, &AppController::quitConfirmationRequired);
    controller.quitApplication();
    QCOMPARE(prompts.size(), 1);
    QCOMPARE(prompts.first().first().toInt(), 1);
    QVERIFY(controller.quitConfirmationPending());
    controller.quitApplication();
    QCOMPARE(prompts.size(), 1);
    controller.cancelQuitApplication();
    QVERIFY(!controller.quitConfirmationPending());
    QCOMPARE(quit.size(), 0);
    QVERIFY(!controller.quitting());
    QCOMPARE(controller.sessionModel()->session("a").value("state").toString(), QString("connected"));
    QCOMPARE(daemon.count("disconnect"), 0);
    controller.quitApplication();
    QCOMPARE(prompts.size(), 2);
    controller.quitApplication(false);
    QCOMPARE(quit.size(), 1);
    QVERIFY(controller.quitting());
    QCOMPARE(daemon.count("disconnect"), 0);
}

void AppControllerTest::screenshotQuitDoesNotTouchDaemon() {
    QTemporaryDir directory;
    LocalDaemon daemon(directory.filePath("engine.sock"));
    AppController controller(false, directory.filePath("profiles.json"), daemon.server.fullServerName());
    controller.sessionModel()->setSessions({testProfile("a", "connected"), testProfile("b", "connected")});
    QSignalSpy prompts(&controller, &AppController::quitConfirmationRequired);
    QSignalSpy quit(&controller, &AppController::quitRequested);
    controller.quitApplication(false);
    QCOMPARE(prompts.size(), 0);
    QCOMPARE(quit.size(), 1);
    QCOMPARE(daemon.requests.size(), 0);
    QVERIFY(!QFileInfo::exists(controller.storageLocation()));
    QCOMPARE(controller.sessionModel()->session("a").value("state").toString(), QString("connected"));
    QCOMPARE(controller.sessionModel()->session("b").value("state").toString(), QString("connected"));
}

void AppControllerTest::malformedSnapshotDoesNotDisconnectOtherSessions() {
    QTemporaryDir directory;
    AppController controller(false, directory.filePath("profiles.json"));
    controller.sessionModel()->setSessions({testProfile("a", "connected")});
    const auto original = controller.sessionModel()->sessions();
    controller.applySnapshot(QJsonArray{QJsonObject{{"sessionId", "unknown"}, {"state", "connected"}, {"downloadBytes", -1}}});
    QCOMPARE(controller.sessionModel()->sessions(), original);
    QVERIFY(!controller.serviceError().isEmpty());
}

void AppControllerTest::lateConnectResponseDoesNotUndoCancellation() {
    QTemporaryDir directory;
    LocalDaemon daemon(directory.filePath("engine.sock"));
    daemon.handler = [](const auto&) { return QJsonObject{}; };
    AppController controller(false, directory.filePath("profiles.json"), daemon.server.fullServerName());
    controller.sessionModel()->setSessions({testProfile("a", "disconnected"), testProfile("b", "connected")});
    controller.toggleConnection("a");
    QTRY_COMPARE(daemon.count("connect"), 1);
    controller.toggleConnection("a");
    QTRY_COMPARE(daemon.count("disconnect"), 1);
    daemon.send(QJsonObject{{"type", "sessionEvent"}, {"sessionId", "a"}, {"event", "CONNECTED"}});
    daemon.send(QJsonObject{{"ok", true}, {"session", QJsonObject{{"sessionId", "a"}, {"state", "connected"}}}});
    daemon.send(QJsonObject{{"ok", true}});
    QTRY_COMPARE(controller.sessionModel()->session("a").value("state").toString(), QString("disconnected"));
    QCOMPARE(controller.sessionModel()->session("b").value("state").toString(), QString("connected"));
    QCOMPARE(controller.detailsForSession("a").value("connectionCount").toInt(), 0);
    daemon.send(QJsonObject{{"type", "sessionEvent"}, {"sessionId", "a"}, {"event", "CONNECTED"}});
    QTest::qWait(20);
    QCOMPARE(controller.sessionModel()->session("a").value("state").toString(), QString("disconnected"));
}

void AppControllerTest::failedRetryCleanupDoesNotConnect() {
    QTemporaryDir directory;
    LocalDaemon daemon(directory.filePath("engine.sock"));
    daemon.handler = [](const auto& request) {
        return QJsonObject{{"ok", false}, {"message", request.value("command") == "disconnect" ? "cleanup failed" : "AUTH_FAILED"}};
    };
    AppController controller(false, directory.filePath("profiles.json"), daemon.server.fullServerName());
    controller.sessionModel()->setSessions({testProfile("a", "disconnected")});
    QSignalSpy prompts(&controller, &AppController::credentialsRequired);
    controller.toggleConnection("a");
    QTRY_COMPARE(prompts.size(), 1);
    controller.submitCredentials("a", "alice", "new-password", false);
    QTRY_COMPARE(controller.sessionModel()->session("a").value("state").toString(), QString("failed"));
    QCOMPARE(daemon.count("connect"), 1);
    QCOMPARE(daemon.count("disconnect"), 1);
    QCOMPARE(controller.sessionModel()->session("a").value("error").toString(), QString("cleanup failed"));
}

void AppControllerTest::generatedLookingCustomNameSurvivesReload() {
    QTemporaryDir directory;
    const QString path = directory.filePath("profiles.json");
    AppController controller(false, path);
    controller.sessionModel()->setSessions({testProfile("legacy", "disconnected")});
    QVERIFY(controller.renameSession("legacy", "profile-deadbeef"));
    ProfileStore store(path);
    QCOMPARE(store.load().first().value("name").toString(), QString("profile-deadbeef"));
}

void AppControllerTest::runtimeOnlyEditsDoNotOverwriteUnloadedIndex() {
    QTemporaryDir directory;
    const QString path = directory.filePath("profiles.json");
    ProfileStore store(path);
    QVERIFY(store.save({testProfile("legacy", "disconnected")}));
    AppController controller(false, path);
    controller.sessionModel()->setSessions({QJsonObject{{"sessionId", "external"}, {"state", "disconnected"}}});
    QVERIFY(controller.removeProfile("external"));
    QCOMPARE(controller.sessionModel()->rowCount(), 0);
    QCOMPARE(store.load().size(), 1);
    QCOMPARE(store.load().first().value("profileId").toString(), QString("legacy"));
}

void AppControllerTest::unverifiedDownloadDoesNotStartNetwork() {
    QTemporaryDir directory;
    AppController controller(false, directory.filePath("profiles.json"));
    controller.updateInstallerName_ = "OpenVPN.pkg";
    controller.updateInstallerUrl_ = QUrl("https://github.com/flyu518/open-vpn/releases/download/v1.0.0/OpenVPN.pkg");
    controller.updateInstallerSize_ = 123;
    controller.setUpdateState("available");
    QSignalSpy notices(&controller, &AppController::notice);
    controller.downloadUpdate();
    QCOMPARE(notices.size(), 1);
    QVERIFY(!controller.updateReply_);
    QCOMPARE(controller.updateState(), QString("available"));
    controller.updateInstallerSha256_ = QByteArray(32, 'a');
    controller.updateInstallerUrl_ = QUrl("file:///tmp/untrusted.pkg");
    controller.downloadUpdate();
    QCOMPARE(notices.size(), 2);
    QVERIFY(!controller.updateReply_);
}

void AppControllerTest::cancellingSubmittedCredentialsCancelsOnlyThatSession() {
    QTemporaryDir directory;
    LocalDaemon daemon(directory.filePath("engine.sock"));
    daemon.handler = [](const auto&) { return QJsonObject{}; };
    AppController controller(false, directory.filePath("profiles.json"), daemon.server.fullServerName());
    controller.sessionModel()->setSessions({testProfile("a", "failed"), testProfile("b", "connected")});
    controller.submitCredentials("a", "alice", "fixture-password", false);
    QTRY_COMPARE(daemon.count("connect"), 1);
    controller.cancelCredentials("a");
    QTRY_COMPARE(daemon.count("disconnect"), 1);
    QCOMPARE(daemon.requests.last().value("sessionId").toString(), QString("a"));
    daemon.send(QJsonObject{{"ok", true}, {"session", QJsonObject{{"sessionId", "a"}, {"state", "connecting"}}}});
    daemon.send(QJsonObject{{"ok", true}});
    QTRY_COMPARE(controller.sessionModel()->session("a").value("state").toString(), QString("disconnected"));
    QCOMPARE(controller.sessionModel()->session("b").value("state").toString(), QString("connected"));
}

void AppControllerTest::managedCopyWriteFailurePreservesModelAndIndex() {
    QTemporaryDir directory;
    QFile source(directory.filePath("source.ovpn"));
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("client\nremote original.example\n");
    source.close();
    const QString indexPath = directory.filePath("profiles.json");
    AppController controller(false, indexPath);
    QVERIFY(controller.importProfilePath(source.fileName()));
    const auto original = controller.sessionModel()->sessions();
    QFile index(indexPath);
    QVERIFY(index.open(QIODevice::ReadOnly));
    const auto originalIndex = index.readAll();
    index.close();
    const QByteArray changed = "client\nremote changed.example\n";
    const auto hash = QString::fromLatin1(QCryptographicHash::hash(changed, QCryptographicHash::Sha256).toHex());
    // A directory at the intended file path causes a real QSaveFile failure.
    QVERIFY(QDir().mkdir(directory.filePath("profiles/" + hash + ".ovpn")));
    QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
    source.write(changed);
    source.close();
    QVERIFY(!controller.importProfilePath(source.fileName()));
    QCOMPARE(controller.sessionModel()->sessions(), original);
    QVERIFY(index.open(QIODevice::ReadOnly));
    QCOMPARE(index.readAll(), originalIndex);
    ProfileStore store(indexPath);
    QCOMPARE(store.load().first().value("profile"), original.first().value("profile"));
}

void AppControllerTest::selectsMatchingInstallerArchitecture() {
    QTemporaryDir directory;
    QCoreApplication::setApplicationVersion("0.2.2");
    AppController controller(false, directory.filePath("profiles.json"));
    QJsonArray assets;
#if defined(Q_OS_WIN)
    const QStringList assetNames{QStringLiteral("OpenVPN-windows-arm64.exe"), QStringLiteral("OpenVPN-windows-x64.exe")};
#else
    const QStringList assetNames{QStringLiteral("OpenVPN-macos-arm64.pkg"), QStringLiteral("OpenVPN-macos-x86_64.pkg"), QStringLiteral("OpenVPN-macos-universal.pkg")};
#endif
    for (const QString& name : assetNames) {
        assets.append(QJsonObject{{"name", name}, {"browser_download_url", "https://github.com/flyu518/open-vpn/releases/download/v0.3.0/" + name},
                                  {"size", 10}, {"digest", "sha256:" + QString(64, 'a')}});
    }
    controller.applyReleaseResponse(QJsonDocument(QJsonObject{{"tag_name", "v0.3.0"}, {"assets", assets}}).toJson(), 200, QNetworkReply::NoError);
    QVERIFY(controller.updateInstallerAvailable());
#if defined(Q_OS_WIN)
#if defined(Q_PROCESSOR_ARM_64)
    QCOMPARE(controller.updateInstallerName(), QString("OpenVPN-windows-arm64.exe"));
#elif defined(Q_PROCESSOR_X86_64)
    QCOMPARE(controller.updateInstallerName(), QString("OpenVPN-windows-x64.exe"));
#endif
#else
#if defined(Q_PROCESSOR_ARM_64)
    QCOMPARE(controller.updateInstallerName(), QString("OpenVPN-macos-arm64.pkg"));
#elif defined(Q_PROCESSOR_X86_64)
    QCOMPARE(controller.updateInstallerName(), QString("OpenVPN-macos-x86_64.pkg"));
#endif
#endif
}

void AppControllerTest::snapshotsDoNotCreateConnectionHistory() {
    QTemporaryDir directory;
    const QString path = directory.filePath("profiles.json");
    const QString connectedAt = "2026-10-16T08:00:00Z";
    auto profile = testProfile("legacy", "disconnected");
    profile.insert("connectionCount", 7);
    profile.insert("lastConnectedAt", connectedAt);
    ProfileStore store(path);
    QVERIFY(store.save({profile}));
    QFile index(path);
    QVERIFY(index.open(QIODevice::ReadOnly));
    const auto originalIndex = index.readAll();
    index.close();
    const QJsonArray snapshot{
        QJsonObject{{"sessionId", "legacy"}, {"state", "connected"}, {"durationSeconds", 120}, {"downloadBytes", 4096}},
        QJsonObject{{"sessionId", "acceptance-external"}, {"state", "connected"}}};
    // Each new controller starts with disconnected runtime state from the same index.
    for (int opening = 0; opening < 2; ++opening) {
        AppController controller(false, path, directory.filePath("missing.sock"));
        controller.loadProfiles();
        controller.applySnapshot(snapshot);
        controller.applySnapshot(snapshot);
        const auto loaded = controller.sessionModel()->session("legacy");
        QCOMPARE(loaded.value("state").toString(), QString("connected"));
        QCOMPARE(loaded.value("durationSeconds").toInt(), 120);
        QCOMPARE(loaded.value("downloadBytes").toInt(), 4096);
        QCOMPARE(loaded.value("connectionCount").toInt(), 7);
        QCOMPARE(loaded.value("lastConnectedAt").toString(), connectedAt);
        const auto external = controller.sessionModel()->session("acceptance-external");
        QVERIFY(!external.contains("connectionCount"));
        QVERIFY(!external.contains("lastConnectedAt"));
        QVERIFY(index.open(QIODevice::ReadOnly));
        QCOMPARE(index.readAll(), originalIndex);
        index.close();
    }
}

void AppControllerTest::connectedEventAfterSnapshotCountsOnce() {
    QTemporaryDir directory;
    AppController controller(false, directory.filePath("profiles.json"));
    auto profile = testProfile("a", "disconnected");
    profile.insert("connectionCount", 7);
    profile.insert("lastConnectedAt", "2026-10-16T08:00:00Z");
    controller.sessionModel()->setSessions({profile});
    const QJsonArray snapshot{QJsonObject{{"sessionId", "a"}, {"state", "connected"}}};
    controller.applySnapshot(snapshot);
    deliverEvent(controller, QJsonObject{{"sessionId", "a"}, {"event", "LOG"}, {"session", snapshot.first()}});
    QCOMPARE(controller.detailsForSession("a").value("connectionCount").toInt(), 7);
    deliverEvent(controller, QJsonObject{{"sessionId", "a"}, {"event", "CONNECTED"}});
    QCOMPARE(controller.detailsForSession("a").value("connectionCount").toInt(), 8);
    const auto connectedAt = controller.detailsForSession("a").value("lastConnectedAt");
    QVERIFY(connectedAt.toString() != "2026-10-16T08:00:00Z");
    deliverEvent(controller, QJsonObject{{"sessionId", "a"}, {"event", "CONNECTED"}});
    controller.applySnapshot(snapshot, controller.revisions_);
    QCOMPARE(controller.detailsForSession("a").value("connectionCount").toInt(), 8);
    QCOMPARE(controller.detailsForSession("a").value("lastConnectedAt"), connectedAt);
    deliverEvent(controller, QJsonObject{{"sessionId", "a"}, {"event", "RECONNECTING"}});
    deliverEvent(controller, QJsonObject{{"sessionId", "a"}, {"event", "CONNECTED"}});
    QCOMPARE(controller.detailsForSession("a").value("connectionCount").toInt(), 9);
    ProfileStore store(controller.storageLocation());
    QCOMPARE(store.load().first().value("connectionCount").toInt(), 9);
}

void AppControllerTest::localConnectionCompletedBySnapshotCountsOnce() {
    QTemporaryDir directory;
    LocalDaemon daemon(directory.filePath("engine.sock"));
    QVERIFY(daemon.server.isListening());
    daemon.handler = [](const auto& request) {
        return QJsonObject{{"ok", true}, {"session", QJsonObject{{"sessionId", request.value("sessionId")}, {"state", "connecting"}}}};
    };
    AppController controller(false, directory.filePath("profiles.json"), daemon.server.fullServerName());
    auto profile = testProfile("a", "disconnected");
    profile.insert("connectionCount", 7);
    controller.sessionModel()->setSessions({profile});
    controller.toggleConnection("a");
    QTRY_COMPARE(daemon.count("connect"), 1);
    QTRY_VERIFY(!controller.connecting_.contains("a"));
    const QJsonArray snapshot{QJsonObject{{"sessionId", "a"}, {"state", "connected"}}};
    controller.applySnapshot(snapshot, controller.revisions_);
    QCOMPARE(controller.detailsForSession("a").value("connectionCount").toInt(), 8);
    const auto connectedAt = controller.detailsForSession("a").value("lastConnectedAt");
    QVERIFY(!connectedAt.toString().isEmpty());
    deliverEvent(controller, QJsonObject{{"sessionId", "a"}, {"event", "CONNECTED"}});
    controller.applySnapshot(snapshot, controller.revisions_);
    QCOMPARE(controller.detailsForSession("a").value("connectionCount").toInt(), 8);
    QCOMPARE(controller.detailsForSession("a").value("lastConnectedAt"), connectedAt);
}

void AppControllerTest::snapshotRecordsTrafficAndDeleteRemovesIt() {
    QTemporaryDir dir;
    LocalDaemon daemon(dir.filePath("engine"));
    const auto now = QDateTime::currentMSecsSinceEpoch();
    auto session = testProfile("a", "connected");
    session.insert("downloadBytes", 4000); session.insert("uploadBytes", 1000); session.insert("durationSeconds", 12);
    daemon.handler = [session](const QJsonObject&) { return QJsonObject{{"ok", true}, {"sessions", QJsonArray{session}}}; };
    AppController controller(false, dir.filePath("profiles.json"), daemon.server.fullServerName());
    controller.sessionModel()->setSessions({session});
    controller.trafficHistory_.record("a", now - 2500, 0, 0, 10);
    controller.requestSnapshot();
    QTRY_COMPARE(controller.trafficForSession("a", 1).value("points").toList().size(), 1);
    const auto point = controller.trafficForSession("a", 1).value("points").toList().first().toMap();
    QVERIFY(point.value("download").toDouble() > 1000);
    QVERIFY(controller.trafficHistory_.flush());
    controller.sessionModel()->updateSession({{"sessionId", "a"}, {"state", "disconnected"}});
    QVERIFY(controller.removeProfile("a"));
    QVERIFY(QDir(dir.filePath("traffic")).entryList({"*.json"}, QDir::Files).isEmpty());
}

void AppControllerTest::routeConflictSnapshotKeepsNamesAndValidatesDetails() {
    QTemporaryDir dir;
    AppController controller(false, dir.filePath("profiles.json"), dir.filePath("missing"));
    auto a = testProfile("a", "connected"), b = testProfile("b", "connected");
    b.insert("name", "已改名的连接");
    controller.sessionModel()->setSessions({a, b});
    QJsonArray conflicts{QJsonObject{{"peerSessionId", "b"}, {"peerName", "旧名称"},
        {"candidateRoute", "10.1.0.0/16"}, {"activeRoute", "10.0.0.0/8"}, {"action", "blocked"}}};
    controller.applySnapshot({QJsonObject{{"sessionId", "a"}, {"state", "failed"}, {"routeConflicts", conflicts}}, b});
    auto info = controller.routeConflictInfo("a");
    QVERIFY(info.value("summary").toString().contains("已改名的连接"));
    QVERIFY(info.value("summary").toString().contains("连接被阻止"));
    QVERIFY(!info.value("summary").toString().contains("跳过"));
    controller.applySnapshot({QJsonObject{{"sessionId", "a"}, {"state", "connected"}, {"routeConflicts", "not-an-array"}}, b});
    QCOMPARE(controller.sessionModel()->session("a").value("state").toString(), QString("failed"));
    QVERIFY(!controller.serviceError().isEmpty());
    controller.connectSession(controller.sessionModel()->session("a"));
    QVERIFY(controller.routeConflictInfo("a").value("groups").toList().isEmpty());
}

#ifdef Q_OS_WIN
void AppControllerTest::importDialogStartsInDownloadsDirectory() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppController controller(false, directory.filePath("profiles.json"),
                             directory.filePath("missing.sock"));

    const bool wasUsingNativeDialogs =
        QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QString openedDirectory;
    bool dialogFound = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if (!dialog) {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                dialog = qobject_cast<QFileDialog*>(widget);
                if (dialog) break;
            }
        }
        if (!dialog) return;
        openedDirectory = QDir::cleanPath(dialog->directory().absolutePath());
        dialogFound = true;
        dialog->reject();
    });

    controller.importProfile();
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs,
                                   wasUsingNativeDialogs);

    QVERIFY(dialogFound);
    QString expectedDirectory =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (!QDir(expectedDirectory).exists()) expectedDirectory = QDir::homePath();
    QCOMPARE(openedDirectory, QDir::cleanPath(expectedDirectory));
}
#endif

QTEST_MAIN(AppControllerTest)
#include "test_app_controller.moc"
