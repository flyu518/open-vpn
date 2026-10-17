#include "engine_client.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QPointer>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QtTest>

class EngineClientTest final : public QObject {
    Q_OBJECT

private slots:
    void handlesResponsesAndEventsOutOfBand();
    void failsWhenDaemonStopsResponding();
    void timeoutReconnectsBeforeReentrantRequest();
    void malformedDataFailsAllRequests_data();
    void malformedDataFailsAllRequests();
    void fragmentedEventsDoNotConsumeResponses();
};

void EngineClientTest::handlesResponsesAndEventsOutOfBand() {
    const QString serverName =
        QStringLiteral("openvpn-qt-test-%1").arg(QCoreApplication::applicationPid());
    QLocalServer::removeServer(serverName);
    QLocalServer server;
    QVERIFY(server.listen(serverName));

    EngineClient client(serverName);
    QPointer<QLocalSocket> serverSocket;
    QSignalSpy events(&client, &EngineClient::eventReceived);
    QJsonObject response;
    bool callbackCalled = false;

    QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
        QLocalSocket* socket = server.nextPendingConnection();
        QVERIFY(socket != nullptr);
        socket->setParent(&server);
        serverSocket = socket;
        QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket] {
            const QByteArray request = socket->readAll();
            QVERIFY(request.contains("ping"));
            socket->write("{\"type\":\"sessionEvent\",\"sessionId\":\"a\","
                          "\"event\":\"CONNECTED\"}\n");
            socket->write("{\"ok\":true,\"message\":\"pong\"}\n");
            socket->flush();
        });
    });

    client.request(QJsonObject{{QStringLiteral("command"), QStringLiteral("ping")}},
                   [&](const QJsonObject& value) {
                       response = value;
                       callbackCalled = true;
                   });

    QTRY_VERIFY_WITH_TIMEOUT(callbackCalled, 3000);
    QCOMPARE(response.value(QStringLiteral("message")).toString(),
             QStringLiteral("pong"));
    QTRY_COMPARE_WITH_TIMEOUT(events.count(), 1, 3000);
    QCOMPARE(events.first().at(0).toJsonObject().value(QStringLiteral("event"))
                 .toString(),
             QStringLiteral("CONNECTED"));
    QVERIFY(client.available());

    server.close();
    if (serverSocket) {
        serverSocket->disconnectFromServer();
        serverSocket->deleteLater();
    }
    QCoreApplication::processEvents();
}

void EngineClientTest::failsWhenDaemonStopsResponding() {
    const QString serverName =
        QStringLiteral("openvpn-qt-timeout-test-%1")
            .arg(QCoreApplication::applicationPid());
    QLocalServer::removeServer(serverName);
    QLocalServer server;
    QVERIFY(server.listen(serverName));
    QPointer<QLocalSocket> serverSocket;
    QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
        serverSocket = server.nextPendingConnection();
        if (serverSocket) serverSocket->setParent(&server);
    });

    EngineClient client(serverName, nullptr, 50);
    QJsonObject response;
    bool callbackCalled = false;
    client.request(QJsonObject{{QStringLiteral("command"), QStringLiteral("ping")}},
                   [&](const QJsonObject& value) {
                       response = value;
                       callbackCalled = true;
                   });

    QTRY_VERIFY_WITH_TIMEOUT(callbackCalled, 2000);
    QCOMPARE(response.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(response.value(QStringLiteral("message")).toString(),
             QStringLiteral("OpenVPN 服务响应超时"));
    server.close();
    if (serverSocket) {
        serverSocket->disconnectFromServer();
        serverSocket->deleteLater();
    }
    QCoreApplication::processEvents();
}

void EngineClientTest::timeoutReconnectsBeforeReentrantRequest() {
    QTemporaryDir directory;
    QLocalServer server;
    QVERIFY(server.listen(directory.filePath("engine.sock")));
    int connections = 0;
    connect(&server, &QLocalServer::newConnection, &server, [&] {
        auto* socket = server.nextPendingConnection();
        socket->setParent(&server);
        const int generation = ++connections;
        connect(socket, &QLocalSocket::readyRead, socket, [socket, generation] {
            while (socket->canReadLine()) {
                const auto request = QJsonDocument::fromJson(socket->readLine()).object();
                if (request.value("command") == "second") {
                    // A response to the old request must never satisfy the new one.
                    if (generation == 1) socket->write("{\"ok\":true,\"message\":\"late-first\"}\n");
                    socket->write("{\"ok\":true,\"message\":\"second\"}\n");
                    socket->flush();
                }
            }
        });
    });
    EngineClient client(server.fullServerName(), nullptr, 60);
    int firstCalls = 0;
    int secondCalls = 0;
    QJsonObject second;
    client.request(QJsonObject{{"command", "first"}}, [&](const QJsonObject& value) {
        ++firstCalls;
        QVERIFY(!value.value("ok").toBool());
        QVERIFY(!client.available());
        client.request(QJsonObject{{"command", "second"}}, [&](const QJsonObject& value) {
            ++secondCalls;
            second = value;
        });
    });
    QTRY_COMPARE_WITH_TIMEOUT(secondCalls, 1, 2000);
    QCOMPARE(firstCalls, 1);
    QCOMPARE(connections, 2);
    QVERIFY(second.value("ok").toBool());
    QCOMPARE(second.value("message").toString(), QString("second"));
}

void EngineClientTest::malformedDataFailsAllRequests_data() {
    QTest::addColumn<QByteArray>("data");
    QTest::newRow("invalid-json") << QByteArray("not-json\n");
    QTest::newRow("array") << QByteArray("[]\n");
    QTest::newRow("missing-ok") << QByteArray("{\"message\":\"bad\"}\n");
    QTest::newRow("wrong-ok-type") << QByteArray("{\"ok\":\"true\"}\n");
    QTest::newRow("unknown-event") << QByteArray("{\"type\":\"unexpected\",\"ok\":true}\n");
    QTest::newRow("mismatched-session") << QByteArray("{\"type\":\"sessionEvent\",\"sessionId\":\"a\",\"session\":{\"sessionId\":\"b\"}}\n");
    QTest::newRow("oversize") << QByteArray(2 * 1024 * 1024, 'x');
}

void EngineClientTest::malformedDataFailsAllRequests() {
    QFETCH(QByteArray, data);
    QTemporaryDir directory;
    QLocalServer server;
    QVERIFY(server.listen(directory.filePath("engine.sock")));
    connect(&server, &QLocalServer::newConnection, &server, [&] {
        auto* socket = server.nextPendingConnection();
        socket->setParent(&server);
        connect(socket, &QLocalSocket::readyRead, socket, [socket, data] {
            socket->readAll();
            socket->write(data + "{\"ok\":true}\n");
            socket->flush();
        });
    });
    EngineClient client(server.fullServerName(), nullptr, 500);
    QList<QJsonObject> responses;
    client.request(QJsonObject{{"command", "one"}}, [&](const auto& value) { responses.append(value); });
    client.request(QJsonObject{{"command", "two"}}, [&](const auto& value) { responses.append(value); });
    QTRY_COMPARE_WITH_TIMEOUT(responses.size(), 2, 2000);
    for (const auto& response : responses) QVERIFY(!response.value("ok").toBool());
    QVERIFY(!client.available());
}

void EngineClientTest::fragmentedEventsDoNotConsumeResponses() {
    QTemporaryDir directory;
    QLocalServer server;
    QVERIFY(server.listen(directory.filePath("engine.sock")));
    connect(&server, &QLocalServer::newConnection, &server, [&] {
        auto* socket = server.nextPendingConnection();
        socket->setParent(&server);
        connect(socket, &QLocalSocket::readyRead, socket, [socket] {
            while (socket->canReadLine()) {
                const auto request = QJsonDocument::fromJson(socket->readLine()).object();
                const QByteArray payload = QJsonDocument(QJsonObject{{"ok", true}, {"message", request.value("command")}}).toJson(QJsonDocument::Compact) + '\n';
                socket->write("{\"type\":\"sessionEvent\",\"sessionId\":\"a\",\"event\":");
                socket->flush();
                QTimer::singleShot(5, socket, [socket, payload] {
                    socket->write("\"CONNECTED\"}\n" + payload);
                    socket->flush();
                });
            }
        });
    });
    EngineClient client(server.fullServerName());
    QSignalSpy events(&client, &EngineClient::eventReceived);
    QStringList messages;
    client.request(QJsonObject{{"command", "one"}}, [&](const auto& value) {
        messages.append(value.value("message").toString());
        client.request(QJsonObject{{"command", "two"}}, [&](const auto& value) { messages.append(value.value("message").toString()); });
    });
    QTRY_COMPARE(messages.size(), 2);
    QCOMPARE(messages, QStringList({"one", "two"}));
    QCOMPARE(events.size(), 2);
}

QTEST_MAIN(EngineClientTest)
#include "test_engine_client.moc"
