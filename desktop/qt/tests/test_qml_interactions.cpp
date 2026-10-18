#include "app_controller.h"

#include <QApplication>
#include <QDir>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLocale>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTemporaryDir>
#include <QTranslator>
#include <QLibraryInfo>
#include <QWheelEvent>
#include <QtTest>

#include <algorithm>
#include <memory>

namespace {
QList<QQuickItem*> visualChildren(QQuickItem* root) {
    QList<QQuickItem*> result;
    if (root) for (auto* item : root->childItems()) {
        result.append(item);
        result.append(visualChildren(item));
    }
    return result;
}
QQuickItem* named(QQuickItem* root, const QString& name) {
    for (auto* item : visualChildren(root))
        if (item->objectName() == name) return item;
    return nullptr;
}
QQuickItem* buttonText(QQuickItem* root, const QString& text) {
    for (auto* item : visualChildren(root))
        if (item->isVisible() && item->property("pressed").isValid() &&
            item->property("text").toString() == text) return item;
    return nullptr;
}
QRectF rect(QQuickItem* item, QQuickItem* root) {
    return item->mapRectToItem(root, {0, 0, item->width(), item->height()});
}
QJsonObject profile(const QString& id, const QString& state = "disconnected") {
    return {{"sessionId", id}, {"profileId", id}, {"profilePath", "/tmp/ui-test-" + id + ".ovpn"},
            {"profile", "client\nremote vpn.example.test 1194 udp\n"},
            {"name", "配置 " + id}, {"remoteHost", id + ".example.test"}, {"state", state},
            {"routeStrategy", QJsonObject{{"conflictMode", "compatible"}, {"priority", 1},
                                          {"rules", QJsonArray{}}}}};
}
struct Fixture {
    QTemporaryDir directory{QDir::tempPath() + "/ovq-XXXXXX"};
    QLocalServer server;
    QList<QJsonObject> requests;
    bool requireCredentials = false;
    bool rejectPassword = false;
    bool failForget = false;
    std::unique_ptr<AppController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;
    Fixture() {
        server.listen(directory.filePath("e"));
        QObject::connect(&server, &QLocalServer::newConnection, &server, [this] {
            while (auto* socket = server.nextPendingConnection()) {
                QObject::connect(socket, &QLocalSocket::readyRead, &server, [this, socket] {
                    while (socket->canReadLine()) {
                        const auto request = QJsonDocument::fromJson(socket->readLine()).object();
                        requests.append(request);
                        QJsonObject response{{"ok", true}};
                        if (request.value("command") == "snapshot") response.insert("sessions", QJsonArray{});
                        if (request.value("command") == "connect") {
                            if (rejectPassword || (requireCredentials && request.value("credentials").toObject()
                                    .value("password").toString().isEmpty()))
                                response = {{"ok", false}, {"message", "AUTH_FAILED: username/password required"}};
                            else response.insert("session", QJsonObject{
                                {"sessionId", request.value("sessionId")}, {"state", "connecting"}});
                        }
                        if (request.value("command") == "forgetCredentials" && failForget)
                            response = {{"ok", false}, {"message", "测试服务暂时无法删除凭据，请重试"}};
                        QTimer::singleShot(30, socket, [socket, response] {
                            socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
                            socket->flush();
                        });
                    }
                });
            }
        });
        controller = std::make_unique<AppController>(false, directory.filePath("profiles.json"),
                                                     server.fullServerName());
    }
    bool load(const QList<QJsonObject>& profiles, QSize size = {960, 640}) {
        if (!directory.isValid() || !server.isListening() ||
            server.fullServerName().toLocal8Bit().size() >= 104) return false;
        controller->sessionModel()->setSessions(profiles);
        engine = std::make_unique<QQmlApplicationEngine>();
        engine->rootContext()->setContextProperty("controller", controller.get());
        engine->load(QUrl::fromLocalFile(QStringLiteral(OPENVPN_QT_TEST_SOURCE_DIR "/qml/Main.qml")));
        if (engine->rootObjects().isEmpty()) return false;
        window = qobject_cast<QQuickWindow*>(engine->rootObjects().first());
        if (!window) return false;
        window->resize(size);
        window->show();
        window->requestActivate();
        if (!QTest::qWaitForWindowExposed(window)) return false;
        QTest::qWait(80);
        return true;
    }
    ~Fixture() {
        if (window) window->hide();
        engine.reset();
        controller.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    QQuickItem* root() const { return window->contentItem(); }
    QQuickItem* item(const QString& name) const { return named(root(), name); }
    QQuickItem* list() const { return item("sessionList"); }
    QQuickItem* card(const QString& id) const {
        for (auto* item : visualChildren(list()))
            if (item->objectName() == "sessionDelegate" && item->property("sessionKey") == id) return item;
        return nullptr;
    }
    QQuickItem* action(const QString& id = "a") const { return named(card(id), "sessionActionButton"); }
    QObject* dialog(const QString& key) const {
        for (auto* object : window->findChildren<QObject*>())
            if (object->property("modal").isValid() &&
                (object->objectName() == key || object->property("title") == key)) return object;
        return nullptr;
    }
    QPoint center(QQuickItem* item) const { return rect(item, root()).center().toPoint(); }
    bool click(QQuickItem* item) {
        if (!item || !item->isVisible() || !item->isEnabled() || item->width() < 1 || item->height() < 1) return false;
        const auto point = center(item);
        if (!QRect(0, 0, window->width(), window->height()).contains(point)) return false;
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
        QCoreApplication::processEvents();
        return true;
    }
    bool clickText(const QString& text) { return click(buttonText(root(), text)); }
    bool details() {
        auto* target = card("a");
        if (!target) return false;
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                         target->mapToItem(root(), {target->width() / 2, 25}).toPoint());
        QCoreApplication::processEvents();
        return true;
    }
    QStringList order() const {
        QStringList result;
        for (const auto& value : controller->sessionModel()->sessions()) result.append(value.value("sessionId").toString());
        return result;
    }
    int commands(const QString& command) const {
        return std::count_if(requests.cbegin(), requests.cend(), [&](const auto& value) { return value.value("command") == command; });
    }
    bool geometry() const {
        QList<QQuickItem*> cards;
        for (auto* item : visualChildren(list())) if (item->objectName() == "sessionDelegate") cards.append(item);
        if (cards.isEmpty()) return false;
        const auto ids = order();
        const qreal gap = list()->property("spacing").toReal();
        for (auto* first : cards) {
            const auto a = rect(first, list());
            if (qAbs(a.left()) > 1 || qAbs(a.width() - list()->width()) > 1) return false;
            for (auto* second : cards) {
                if (first == second) continue;
                const auto b = rect(second, list());
                if (a.intersects(b)) return false;
                const int delta = ids.indexOf(second->property("sessionKey").toString()) -
                                  ids.indexOf(first->property("sessionKey").toString());
                if (qAbs(b.top() - a.top() - delta * (a.height() + gap)) > 1) return false;
            }
        }
        return true;
    }
    bool move(const QPoint& from, const QPoint& to) {
        const int steps = std::max(2, qAbs(to.y() - from.y()) / 12);
        for (int step = 1; step <= steps; ++step) {
            QTest::mouseMove(window, from + (to - from) * step / steps, 8);
            QTest::qWait(8);
            if (!geometry()) return false;
        }
        return true;
    }
    bool drag(const QString& source, const QString& target) {
        auto* a = named(card(source), "dragButton");
        auto* b = named(card(target), "dragButton");
        if (!a || !b) return false;
        const QPoint start = center(a), end = center(b);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
        const bool valid = move(start, end);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
        QTest::qWait(260);
        return valid && geometry();
    }
    bool screenshot(const QString& name) {
        const auto dir = qEnvironmentVariable("OPENVPN_QT_DIALOG_SCREENSHOT_DIR");
        const bool saved = dir.isEmpty() || window->grabWindow().save(dir + '/' + name + ".png");
        if (qEnvironmentVariable("OPENVPN_QT_SCREENSHOT_HOLD_NAME") == name) {
            const int hold = qEnvironmentVariableIntValue("OPENVPN_QT_SCREENSHOT_HOLD_MS");
            if (hold > 0) QTest::qWait(std::min(hold, 20000));
        }
        return saved;
    }
    QQuickItem* field(const QString& placeholder) const {
        for (auto* item : visualChildren(root()))
            if (item->isVisible() && item->property("placeholderText") == placeholder) return item;
        return nullptr;
    }
    void type(const QString& text) {
        for (const auto c : text) QTest::keyClick(window, c.toLatin1());
    }
    void event(const QString& state) {
        for (auto* socket : server.findChildren<QLocalSocket*>()) {
            socket->write(QJsonDocument(QJsonObject{{"type", "sessionEvent"}, {"sessionId", "a"},
                {"session", QJsonObject{{"sessionId", "a"}, {"state", state}}}}).toJson(QJsonDocument::Compact) + '\n');
            socket->flush();
        }
    }
};
QQuickItem* footer(QObject* popup) { return popup->property("footer").value<QQuickItem*>(); }
}

class QmlInteractionsTest final : public QObject {
    Q_OBJECT
private slots:
    void namedRouteConflicts();
    void routePolicyEditor();
    void dragHasVisualMotion();
    void historyChartRangeAndEmptyState();
    void historyChartWithSavedData();
    void actionCentered_data();
    void actionCentered();
    void actionMouse_data();
    void actionMouse();
    void dragRepeated_data();
    void dragRepeated();
    void dragReverseSameGesture();
    void dragScrolled();
    void dragAutoScroll();
    void dialogs_data();
    void dialogs();
    void emptyState();
    void updateConfirmationDialog();
    void renameSave();
    void credentialsSubmit();
    void credentialsRetryKeepsInput();
    void credentialsFailureShowsActualReason();
    void credentialsPendingCanBeCancelled();
    void deleteConfirmation();
    void nestedEscapeReturnsToDetails();
    void forgetWaitsForSuccess();
    void activeProfileCannotBeDeleted();
    void failedSaveKeepsDialogOpen();
    void noticeDoesNotCoverActions();
    void domainSuffixRuleDetails();
    void errorsAreReadableAndDiagnosticsRemainAvailable_data();
    void errorsAreReadableAndDiagnosticsRemainAvailable();
    void detailsScrollKeepsFooterReachable();
    void compactConnectedScreenshot();
    void connectedWarning_data();
    void connectedWarning();
    void quitConfirmationKeepsConnections();
};

void QmlInteractionsTest::actionCentered_data() {
    QTest::addColumn<QString>("state");
    for (const auto state : {"disconnected", "connected", "connecting", "disconnecting", "failed"})
        QTest::newRow(state) << QString(state);
}
void QmlInteractionsTest::actionCentered() {
    QFETCH(QString, state);
    Fixture f;
    QVERIFY(f.load({profile("a", state)}, {820, 560}));
    QVERIFY(f.screenshot("action-" + state));
    QVERIFY2(!buttonText(f.root(), "关于"), "主页面不应再显示关于按钮，关于入口位于状态栏/托盘菜单");
    for (auto* button : {f.action(), buttonText(f.root(), "导入配置")}) {
        QVERIFY(button);
        QRectF text;
        // Measure the visible glyph group, not the stretched contentItem rectangle.
        for (auto* item : visualChildren(button))
            if (item->isVisible() && ((!item->property("text").toString().isEmpty() &&
                item->property("contentWidth").isValid()) || item->objectName() == "buttonIcon"))
                text |= rect(item, button);
        QVERIFY(!text.isEmpty());
        const auto delta = text.center() - QPointF(button->width() / 2, button->height() / 2);
        QVERIFY2(qAbs(delta.x()) <= 1.5 && qAbs(delta.y()) <= 1.5,
                 qPrintable(QString("图文偏离按钮中心 (%1,%2)").arg(delta.x()).arg(delta.y())));
        QVERIFY(QRectF(0, 0, button->width(), button->height()).contains(text));
    }
}
void QmlInteractionsTest::actionMouse_data() {
    QTest::addColumn<QString>("state"); QTest::addColumn<QString>("text"); QTest::addColumn<QString>("command");
    QTest::newRow("connect") << QString("disconnected") << QString("连接") << QString("connect");
    QTest::newRow("disconnect") << QString("connected") << QString("断开") << QString("disconnect");
    QTest::newRow("cancel") << QString("connecting") << QString("取消连接") << QString("disconnect");
    QTest::newRow("retry") << QString("failed") << QString("重试") << QString("connect");
    QTest::newRow("disconnecting") << QString("disconnecting") << QString("正在断开") << QString();
}
void QmlInteractionsTest::actionMouse() {
    QFETCH(QString, state); QFETCH(QString, text); QFETCH(QString, command);
    Fixture f; QVERIFY(f.load({profile("a", state)}));
    auto* button = f.action(); QVERIFY(button);
    QCOMPARE(button->property("text").toString(), text);
    QCOMPARE(button->isEnabled(), !command.isEmpty());
    int count = 0;
    for (auto* item : visualChildren(f.card("a"))) if (item->objectName() == "sessionActionButton") ++count;
    QCOMPARE(count, 1);
    if (command.isEmpty()) return;
    QVERIFY(f.click(button));
    QTRY_COMPARE(f.commands(command), 1);
    QTRY_COMPARE(f.controller->sessionModel()->session("a").value("state").toString(),
                 command == "connect" ? QString("connecting") : QString("disconnected"));
    QVERIFY(!f.dialog("detailsDialog")->property("visible").toBool());
}
void QmlInteractionsTest::namedRouteConflicts() {
    Fixture f;
    auto a = profile("a", "connected"), b = profile("b", "connected"), c = profile("c", "connected");
    b.insert("name", "公司内网"); c.insert("name", "研发环境");
    a.insert("routeConflicts", QJsonArray{
        QJsonObject{{"peerSessionId", "b"}, {"peerName", "旧名称"}, {"candidateRoute", "10.20.0.0/16"}, {"activeRoute", "10.0.0.0/8"}, {"action", "skipped"}},
        QJsonObject{{"peerSessionId", "c"}, {"peerName", "研发环境"}, {"candidateRoute", "192.168.10.0/24"}, {"activeRoute", "192.168.0.0/16"}, {"action", "skipped"}}});
    a.insert("warnings", QJsonArray{"兼容模式：重叠路由已跳过"});
    QVERIFY(f.load({a, b, c}, {820, 560}));
    auto* hint = named(f.card("a"), "routeConflictHint");
    QVERIFY2(hint, "卡片应标明冲突连接，而不是只提示重叠");
    QVERIFY(hint->property("text").toString().contains("公司内网"));
    QVERIFY(hint->property("text").toString().contains("等 2 个连接"));
    QCOMPARE(f.card("a")->height(), 92.0);
    QVERIFY(f.controller->renameSession("b", "办公网络"));
    QTRY_VERIFY(hint->property("text").toString().contains("办公网络"));
    QVERIFY(f.click(hint));
    QTRY_VERIFY(f.dialog("detailsDialog")->property("visible").toBool());
    auto* section = f.item("routeConflictDetails"); QVERIFY(section);
    QString text;
    for (auto* item : visualChildren(section)) text += item->property("text").toString();
    QVERIFY(text.contains("办公网络") && text.contains("研发环境"));
    QVERIFY(text.contains("10.20.0.0/16") && text.contains("10.0.0.0/8"));
    QVERIFY(!text.contains("旧名称"));
    QVERIFY(f.screenshot("named-route-conflicts"));
    auto* scroll = f.dialog("detailsDialog")->property("contentItem").value<QQuickItem*>(); QVERIFY(scroll);
    auto* flick = scroll->property("contentItem").value<QQuickItem*>(); QVERIFY(flick);
    flick->setProperty("contentY", 100);
    QVERIFY(f.clickText("关闭"));
    QVERIFY(f.controller->sessionModel()->updateSession({{"sessionId", "b"}, {"state", "disconnected"}}));
    QVERIFY(f.details());
    QTRY_COMPARE(flick->property("contentY").toReal(), 0.0);
    text.clear();
    for (auto* item : visualChildren(f.item("routeConflictDetails"))) text += item->property("text").toString();
    QVERIFY(text.contains("当前未连接"));
}

void QmlInteractionsTest::routePolicyEditor() {
    Fixture f;
    QVERIFY(f.load({profile("a", "connected")}, {820, 560}));
    QVERIFY(f.details());
    QSignalSpy notice(f.controller.get(), &AppController::notice);
    QVERIFY(f.clickText("编辑路由策略"));
    auto* dialog = f.dialog("routePolicyDialog"); QVERIFY(dialog);
    QVERIFY(f.clickText("严格模式"));
    QVERIFY(f.clickText("保存"));
    QCOMPARE(f.controller->sessionModel()->session("a").value("routeStrategy").toObject()
                 .value("conflictMode").toString(), QString("strict"));
    QVERIFY(notice.count() >= 1);
    QCOMPARE(notice.last().at(0).toString(), QString("路由策略已保存"));
    QVERIFY(notice.last().at(1).toString().contains("断开") &&
            notice.last().at(1).toString().contains("重新连接"));

    QVERIFY(f.clickText("编辑路由策略"));
    auto* ruleField = f.field("IP 或 CIDR，例如 10.20.0.0/16");
    QVERIFY(ruleField);
    ruleField->setProperty("text", QStringLiteral("10.20.10.4"));
    QVERIFY(f.clickText("优先当前连接"));
    auto rules = f.controller->routeRules(QStringLiteral("a"));
    QCOMPARE(rules.size(), 1);
    QCOMPARE(rules.first().toMap().value(QStringLiteral("pattern")).toString(),
             QStringLiteral("10.20.10.4/32"));
    QCOMPARE(rules.first().toMap().value(QStringLiteral("action")).toString(),
             QStringLiteral("prefer"));

    ruleField = f.field("IP 或 CIDR，例如 10.20.0.0/16");
    QVERIFY(ruleField);
    ruleField->setProperty("text", QStringLiteral("192.168.10.0/24"));
    QVERIFY(f.clickText("排除当前连接"));
    rules = f.controller->routeRules(QStringLiteral("a"));
    QCOMPARE(rules.size(), 2);
    QCOMPARE(rules.at(1).toMap().value(QStringLiteral("pattern")).toString(),
             QStringLiteral("192.168.10.0/24"));
    QCOMPARE(rules.at(1).toMap().value(QStringLiteral("action")).toString(),
             QStringLiteral("exclude"));

    QVERIFY(f.clickText("域名后缀"));
    ruleField = f.field("域名后缀，例如 *.corp.example.com");
    QVERIFY(ruleField);
    ruleField->setProperty("text", QStringLiteral("*.Corp.Example.Test"));
    QVERIFY(f.clickText("优先当前连接"));
    rules = f.controller->routeRules(QStringLiteral("a"));
    QCOMPARE(rules.size(), 3);
    QCOMPARE(rules.at(2).toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("domainSuffix"));
    QCOMPARE(rules.at(2).toMap().value(QStringLiteral("pattern")).toString(),
             QStringLiteral("corp.example.test"));

    QVERIFY(f.clickText("关闭"));
}

void QmlInteractionsTest::dragHasVisualMotion() {
    Fixture f; QVERIFY(f.load({profile("a"), profile("b"), profile("c")}));
    auto* surface = named(f.card("a"), "cardSurface");
    QVERIFY2(surface, "卡片应有独立动画层，不能直接跳换位置");
    const auto start = f.center(named(f.card("a"), "dragButton"));
    const auto original = rect(surface, f.list()).top();
    QTest::mousePress(f.window, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(f.window, start + QPoint(0, 30), 30);
    QTest::qWait(30);
    QVERIFY2(qAbs(rect(surface, f.list()).top() - original - 30) < 2,
        qPrintable(QString("位移=%1 visualY=%2 lifted=%3 pointerY=%4 offset=%5")
        .arg(rect(surface, f.list()).top()-original).arg(f.card("a")->property("visualY").toDouble())
        .arg(f.card("a")->property("lifted").toBool()).arg(f.list()->property("pointerY").toDouble())
        .arg(f.list()->property("grabOffset").toDouble())));
    auto* card = f.card("a");
    QSignalSpy settling(card, card->metaObject()->property(card->metaObject()->indexOfProperty("visualY")).notifySignal());
    QTest::mouseRelease(f.window, Qt::LeftButton, Qt::NoModifier, start + QPoint(0, 30));
    // 观察实际动画帧，而不是假设繁忙机器会恰好在第 30ms 返回。
    QTRY_VERIFY(settling.count() > 1);
    QTRY_VERIFY(qAbs(rect(surface, f.list()).top() - original) < 1);
    QVERIFY(f.geometry());
    auto* neighbor = named(f.card("b"), "cardSurface"); QVERIFY(neighbor);
    auto* neighborCard = f.card("b");
    QSignalSpy displacement(neighborCard, neighborCard->metaObject()->property(neighborCard->metaObject()->indexOfProperty("visualY")).notifySignal());
    QTest::mousePress(f.window, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(f.window, start + QPoint(0, 120), 30);
    QCOMPARE(f.order(), (QStringList{"b", "a", "c"}));
    QTRY_VERIFY(displacement.count() > 1);
    QTest::mouseRelease(f.window, Qt::LeftButton, Qt::NoModifier, start + QPoint(0, 120));
    QTest::qWait(260);
    QVERIFY(qAbs(rect(neighbor, f.list()).top()) < 1);
    QVERIFY(qAbs(rect(surface, f.list()).top() - 100) < 1);
    QVERIFY(f.geometry());
}

void QmlInteractionsTest::historyChartRangeAndEmptyState() {
    Fixture f; QVERIFY(f.load({profile("a")}, {820, 560}));
    QVERIFY(f.details());
    auto* chart = f.item("trafficChart");
    QVERIFY2(chart, "连接详情应展示历史流量图");
    QCOMPARE(chart->property("hours").toInt(), 1);
    QVERIFY(f.clickText("24 小时"));
    QCOMPARE(chart->property("hours").toInt(), 24);
    QVERIFY(chart->property("samples").value<QVariantList>().isEmpty());
    QVERIFY(f.clickText("1 小时"));
    QCOMPARE(chart->property("hours").toInt(), 1);
    QVERIFY(f.clickText("关闭"));
    QVERIFY(f.geometry());
}

void QmlInteractionsTest::historyChartWithSavedData() {
    Fixture f;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    TrafficHistory history(f.directory.filePath("traffic"));
    qint64 down = 0, up = 0;
    for (int i = 0; i < 720; ++i) {
        down += (i % 19 + 1) * 120000; up += (i % 13 + 1) * 30000;
        history.record("a", now - 7200000 + i * 10000LL, down, up, i * 10);
    }
    QVERIFY(history.flush());
    QVERIFY(f.load({profile("a")}, {820, 560})); QVERIFY(f.details());
    auto* chart = f.item("trafficChart"); QVERIFY(chart);
    const auto recent = chart->property("samples").value<QVariantList>().size();
    QVERIFY(recent > 50 && recent < 63);
    QVERIFY(f.screenshot("traffic-history-1h"));
    QVERIFY(f.clickText("24 小时"));
    QVERIFY(chart->property("samples").value<QVariantList>().size() > recent);
    QVERIFY(f.screenshot("traffic-history-24h"));
    QVERIFY(f.clickText("关闭"));
}

void QmlInteractionsTest::dragRepeated_data() {
    QTest::addColumn<int>("count");
    QTest::newRow("two-cards") << 2; QTest::newRow("three-cards") << 3;
}
void QmlInteractionsTest::dragRepeated() {
    QFETCH(int, count);
    Fixture f;
    QList<QJsonObject> profiles{profile("a"), profile("b")};
    if (count == 3) profiles.append(profile("c"));
    QVERIFY(f.load(profiles));
    const auto initial = f.order();
    const auto original = rect(f.card("a"), f.list());
    for (int round = 0; round < 3; ++round) {
        QVERIFY(f.drag("a", initial.last()));
        QCOMPARE(f.order(), count == 2 ? (QStringList{"b", "a"}) : (QStringList{"b", "c", "a"}));
        QVERIFY(f.drag("a", "b"));
        QCOMPARE(f.order(), initial);
        QCOMPARE(rect(f.card("a"), f.list()), original);
    }
    QVERIFY(!f.dialog("detailsDialog")->property("visible").toBool());
    ProfileStore saved(f.directory.filePath("profiles.json"));
    QCOMPARE(saved.load().first().value("sessionId").toString(), QString("a"));
    QVERIFY(f.screenshot("drag-restored-" + QString::number(count)));
}
void QmlInteractionsTest::dragReverseSameGesture() {
    Fixture f; QVERIFY(f.load({profile("a"), profile("b"), profile("c")}));
    const auto a = f.center(named(f.card("a"), "dragButton"));
    const auto c = f.center(named(f.card("c"), "dragButton"));
    QTest::mousePress(f.window, Qt::LeftButton, Qt::NoModifier, a);
    const bool downValid = f.move(a, c);
    const auto downOrder = f.order();
    const bool upValid = f.move(c, a);
    QTest::mouseRelease(f.window, Qt::LeftButton, Qt::NoModifier, a);
    QVERIFY(downValid && upValid);
    QCOMPARE(downOrder, (QStringList{"b", "c", "a"}));
    QCOMPARE(f.order(), (QStringList{"a", "b", "c"}));
}
void QmlInteractionsTest::dragScrolled() {
    Fixture f;
    QVERIFY(f.load({profile("a"), profile("b"), profile("c"), profile("d"),
                    profile("e"), profile("f"), profile("g"), profile("h")}, {820, 560}));
    const auto pos = f.center(f.list());
    QWheelEvent wheel(pos, f.window->mapToGlobal(pos), {}, {0, -2400}, Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(f.window, &wheel);
    QTRY_VERIFY(f.list()->property("contentY").toReal() > 100);
    QTest::qWait(250);
    QVERIFY(f.drag("h", "g"));
    QCOMPARE(f.order(), (QStringList{"a", "b", "c", "d", "e", "f", "h", "g"}));
    QVERIFY(f.drag("h", "g"));
    QCOMPARE(f.order(), (QStringList{"a", "b", "c", "d", "e", "f", "g", "h"}));
    QVERIFY(f.screenshot("drag-scrolled"));
}
void QmlInteractionsTest::dragAutoScroll() {
    Fixture f;
    QVERIFY(f.load({profile("a"), profile("b"), profile("c"), profile("d"),
                    profile("e"), profile("f"), profile("g"), profile("h")}, {820, 560}));
    const auto start = f.center(named(f.card("a"), "dragButton"));
    const auto viewport = rect(f.list(), f.root());
    const QPoint bottom(start.x(), qRound(viewport.bottom()) - 4);
    const QPoint top(start.x(), qRound(viewport.top()) + 4);
    QTest::mousePress(f.window, Qt::LeftButton, Qt::NoModifier, start);
    QVERIFY(f.move(start, bottom));
    QTRY_COMPARE_WITH_TIMEOUT(f.order().last(), QString("a"), 5000);
    QVERIFY(f.geometry());
    QVERIFY(f.move(bottom, top));
    QTRY_COMPARE_WITH_TIMEOUT(f.order().first(), QString("a"), 5000);
    QTest::mouseRelease(f.window, Qt::LeftButton, Qt::NoModifier, top);
    QVERIFY(f.geometry());
    QCOMPARE(f.order(), (QStringList{"a", "b", "c", "d", "e", "f", "g", "h"}));
}
void QmlInteractionsTest::dialogs_data() {
    QTest::addColumn<QString>("kind"); QTest::addColumn<QSize>("size");
    for (const auto kind : {"settings", "details", "credentials", "rename", "delete", "forget", "about", "quit"}) {
        QTest::newRow(qPrintable(QString(kind) + "-minimum")) << QString(kind) << QSize(820, 560);
        QTest::newRow(qPrintable(QString(kind) + "-normal")) << QString(kind) << QSize(960, 640);
    }
}
void QmlInteractionsTest::dialogs() {
    QFETCH(QString, kind); QFETCH(QSize, size);
    Fixture f; QVERIFY(f.load({profile("a")}, size));
    QObject* popup = nullptr;
    QString close = "关闭";
    if (kind == "settings") {
        QVERIFY(f.clickText("设置")); popup = f.dialog("settingsDialog");
    } else if (kind == "quit") {
        QVERIFY(f.clickText("设置")); QVERIFY(f.clickText("退出界面"));
        popup = f.dialog("退出 OpenVPN"); close = "取消";
    } else if (kind == "about") {
        f.controller->showAbout();
        QTRY_VERIFY(f.dialog("关于 OpenVPN") != nullptr);
        popup = f.dialog("关于 OpenVPN");
    } else if (kind == "credentials") {
        f.requireCredentials = true;
        QVERIFY(f.click(f.action())); popup = f.dialog("输入 VPN 凭据"); close = "取消";
    } else {
        QVERIFY(f.details());
        popup = f.dialog("detailsDialog");
        QTRY_VERIFY(popup->property("visible").toBool());
        if (kind == "rename") {
            QVERIFY(f.clickText("编辑名称")); popup = f.dialog("编辑连接名称"); close = "取消";
        } else if (kind == "delete") {
            QVERIFY(f.clickText("删除配置")); popup = f.dialog("删除配置"); close = "取消";
        } else if (kind == "forget") {
            QVERIFY(f.clickText("删除凭据")); popup = f.dialog("删除已保存凭据"); close = "取消";
        }
    }
    QVERIFY(popup);
    QTRY_VERIFY(popup->property("visible").toBool());
    QTest::qWait(80);
    QVERIFY(f.screenshot(kind + '-' + QString::number(size.width())));
    auto* background = popup->property("background").value<QQuickItem*>();
    QVERIFY(background);
    const auto bounds = rect(background, f.root());
    const QRectF viewport(0, 0, size.width(), size.height());
    QVERIFY2(viewport.adjusted(8, 8, -8, -8).contains(bounds), "弹框必须完整位于窗口边界内");
    const auto delta = bounds.center() - viewport.center();
    QVERIFY2(qAbs(delta.x()) <= 1 && qAbs(delta.y()) <= 1,
             qPrintable(QString("弹框偏离中心 (%1,%2)").arg(delta.x()).arg(delta.y())));
    auto* closeButton = buttonText(footer(popup), close);
    QVERIFY2(closeButton, "中文关闭/取消按钮不能受系统日文 locale 影响");
    QVERIFY(bounds.contains(rect(closeButton, f.root())));
    QList<QRectF> actionRects;
    for (auto* item : visualChildren(footer(popup))) {
        if (!item->isVisible() || !item->property("pressed").isValid()) continue;
        const auto action = rect(item, f.root());
        QVERIFY2(bounds.contains(action), "每一个弹框按钮都必须完整位于弹框内");
        for (const auto& previous : actionRects) QVERIFY2(!action.intersects(previous), "弹框按钮不能重叠");
        actionRects.append(action);
    }
    QVERIFY(f.click(closeButton));
    QTRY_VERIFY(!popup->property("visible").toBool());
}
void QmlInteractionsTest::emptyState() {
    Fixture f; QVERIFY(f.load({}, {820, 560}));
    QVERIFY(f.list());
    QVERIFY(!f.list()->isVisible());
    auto* empty = f.item("emptyStateLabel");
    QVERIFY(empty);
    QVERIFY(empty->isVisible());
    QVERIFY(empty->property("text").toString().contains("尚未导入配置"));
    QCOMPARE(empty->property("horizontalAlignment").toInt(), int(Qt::AlignHCenter));
    QCOMPARE(empty->property("verticalAlignment").toInt(), int(Qt::AlignVCenter));
    const auto bounds = rect(empty, f.root());
    QVERIFY(bounds.width() > 0);
    QVERIFY(bounds.height() > 0);
    QVERIFY(QRectF(0, 0, f.window->width(), f.window->height()).contains(bounds));
}
void QmlInteractionsTest::updateConfirmationDialog() {
    Fixture f; QVERIFY(f.load({profile("a")}, {820, 560}));
    QJsonArray assets;
    for (const auto& name : {QStringLiteral("OpenVPN-windows-x64.exe"),
                            QStringLiteral("OpenVPN-macos-arm64.pkg"),
                            QStringLiteral("OpenVPN-macos-x86_64.pkg")}) {
        assets.append(QJsonObject{
            {"name", name},
            {"browser_download_url", "https://github.com/flyu518/open-vpn/releases/download/v0.3.0/" + name},
            {"size", 32},
            {"digest", "sha256:" + QString(64, 'a')}});
    }
    const QJsonObject release{
        {"tag_name", "v0.3.0"},
        {"html_url", "https://github.com/flyu518/open-vpn/releases/tag/v0.3.0"},
        {"assets", assets}};
    f.controller->applyReleaseResponseForTest(QJsonDocument(release).toJson());
    QVERIFY(f.controller->updateAvailable());
    QVERIFY(f.controller->updateInstallerAvailable());
    f.controller->showAbout();
    auto* about = f.dialog("aboutDialog");
    QTRY_VERIFY(about && about->property("visible").toBool());
    QVERIFY(f.clickText("下载更新"));
    auto* update = f.dialog("updateDialog");
    QTRY_VERIFY(update && update->property("visible").toBool());
    QVERIFY(f.screenshot("update-confirmation-820"));
    QVERIFY(f.click(buttonText(footer(update), "取消")));
    QTRY_VERIFY(!update->property("visible").toBool());
    QVERIFY(f.click(buttonText(footer(about), "关闭")));
    QTRY_VERIFY(!about->property("visible").toBool());
}
void QmlInteractionsTest::renameSave() {
    Fixture f; QVERIFY(f.load({profile("a")}, {820, 560}));
    QVERIFY(f.details()); QVERIFY(f.clickText("编辑名称"));
    auto* popup = f.dialog("编辑连接名称"); QVERIFY(popup);
    QVERIFY(f.click(f.field("连接名称")));
    QTest::keyClick(f.window, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(f.window, Qt::Key_Backspace);
    auto* save = buttonText(footer(popup), "保存"); QVERIFY(save);
    QVERIFY(!save->isEnabled());
    QVERIFY(f.screenshot("rename-empty-820"));
    f.type("Renamed VPN"); QVERIFY(f.click(save));
    QTRY_COMPARE(f.controller->sessionModel()->session("a").value("name").toString(), QString("Renamed VPN"));
    QTRY_VERIFY(!popup->property("visible").toBool());
    bool shown = false;
    for (auto* item : visualChildren(f.dialog("detailsDialog")->property("contentItem").value<QQuickItem*>()))
        shown |= item->isVisible() && item->property("text") == "Renamed VPN";
    QVERIFY2(shown, "保存名称后详情应立即刷新");
    ProfileStore saved(f.directory.filePath("profiles.json"));
    QCOMPARE(saved.load().first().value("name").toString(), QString("Renamed VPN"));
}
void QmlInteractionsTest::credentialsSubmit() {
    Fixture f; f.requireCredentials = true;
    QVERIFY(f.load({profile("a")}, {820, 560})); QVERIFY(f.click(f.action()));
    auto* popup = f.dialog("输入 VPN 凭据"); QVERIFY(popup);
    QTRY_VERIFY(popup->property("visible").toBool());
    auto* submit = buttonText(footer(popup), "连接"); QVERIFY(submit);
    QVERIFY(!submit->isEnabled());
    QVERIFY(f.click(f.field("用户名"))); f.type("test-user");
    QVERIFY(f.click(f.field("密码"))); f.type("test-password");
    QVERIFY(f.click(submit));
    QTRY_COMPARE(f.commands("connect"), 2);
    QTRY_COMPARE(f.controller->sessionModel()->session("a").value("state").toString(), QString("connecting"));
    QVERIFY2(popup->property("visible").toBool(), "凭据提交后必须等待连接成功，不能立即关闭");
    const auto credentials = f.requests.last().value("credentials").toObject();
    QCOMPARE(credentials.value("username").toString(), QString("test-user"));
    QCOMPARE(credentials.value("password").toString(), QString("test-password"));
    f.event("connected");
    QTRY_VERIFY(!popup->property("visible").toBool());
}
void QmlInteractionsTest::credentialsRetryKeepsInput() {
    Fixture f; f.requireCredentials = true; f.rejectPassword = true;
    QVERIFY(f.load({profile("a")}, {820, 560})); QVERIFY(f.click(f.action()));
    auto* popup = f.dialog("输入 VPN 凭据"); QVERIFY(popup);
    QTRY_VERIFY(popup->property("visible").toBool());
    QVERIFY(f.click(f.field("用户名"))); f.type("test-user");
    QVERIFY(!buttonText(footer(popup), "连接")->isEnabled());
    QVERIFY(f.screenshot("credentials-incomplete-820"));
    QTest::keyClick(f.window, Qt::Key_Return);
    QVERIFY(popup->property("visible").toBool());
    QVERIFY(f.click(f.field("密码"))); f.type("wrong-password");
    QVERIFY(f.click(buttonText(footer(popup), "连接")));
    QTRY_COMPARE(f.commands("connect"), 2);
    QTRY_VERIFY(!f.window->property("credentialPending").toBool());
    QVERIFY(popup->property("visible").toBool());
    QCOMPARE(f.field("用户名")->property("text").toString(), QString("test-user"));
    QCOMPARE(f.field("密码")->property("text").toString(), QString());
    QVERIFY(f.screenshot("credentials-retry-820"));
    f.rejectPassword = false;
    QVERIFY(f.click(f.field("密码"))); f.type("correct-password");
    QVERIFY(f.click(buttonText(footer(popup), "连接")));
    QTRY_COMPARE(f.commands("connect"), 3);
    f.event("connected");
    QTRY_VERIFY(!popup->property("visible").toBool());
}
void QmlInteractionsTest::credentialsFailureShowsActualReason() {
    Fixture f; f.requireCredentials = true;
    QVERIFY(f.load({profile("a")}, {820, 560})); QVERIFY(f.click(f.action()));
    auto* popup = f.dialog("输入 VPN 凭据"); QVERIFY(popup);
    QTRY_VERIFY(popup->property("visible").toBool());
    QVERIFY(f.click(f.field("用户名"))); f.type("test-user");
    QVERIFY(f.click(f.field("密码"))); f.type("test-password");
    QVERIFY(f.click(buttonText(footer(popup), "连接")));
    QTRY_VERIFY(f.window->property("credentialPending").toBool());

    QVERIFY(f.controller->sessionModel()->updateSession(
        QJsonObject{{"sessionId", "a"}, {"state", "failed"},
                    {"error", "连接超时，请检查网络、服务器和 OpenVPN 服务。"}}));
    QTRY_VERIFY(!f.window->property("credentialPending").toBool());

    auto* reason = qobject_cast<QQuickItem*>(popup->property("contentItem").value<QQuickItem*>());
    QVERIFY(reason);
    bool found = false;
    for (auto* item : visualChildren(reason)) {
        if (item->property("text").toString().contains("连接超时")) {
            found = true;
            break;
        }
    }
    QVERIFY2(found, "凭据框应展示实际连接失败原因，而不是固定的凭据错误提示");
}
void QmlInteractionsTest::credentialsPendingCanBeCancelled() {
    Fixture f; f.requireCredentials = true;
    QVERIFY(f.load({profile("a")}, {820, 560})); QVERIFY(f.click(f.action()));
    auto* popup = f.dialog("输入 VPN 凭据"); QVERIFY(popup);
    QTRY_VERIFY(popup->property("visible").toBool());
    QVERIFY(f.click(f.field("用户名"))); f.type("test-user");
    QVERIFY(f.click(f.field("密码"))); f.type("password");
    QVERIFY(f.click(buttonText(footer(popup), "连接")));
    QTRY_COMPARE(f.commands("connect"), 2);
    QTRY_COMPARE(f.controller->sessionModel()->session("a").value("state").toString(), QString("connecting"));
    QVERIFY(f.screenshot("credentials-pending-820"));
    QVERIFY(f.click(buttonText(footer(popup), "取消")));
    QTRY_VERIFY(!popup->property("visible").toBool());
    QTRY_COMPARE(f.controller->sessionModel()->session("a").value("state").toString(), QString("disconnected"));
    QVERIFY(f.commands("disconnect") >= 1);
}
void QmlInteractionsTest::deleteConfirmation() {
    Fixture f; QVERIFY(f.load({profile("a")}, {820, 560}));
    QVERIFY(f.details()); QVERIFY(f.clickText("删除配置"));
    auto* popup = f.dialog("删除配置"); QVERIFY(popup);
    QVERIFY(f.click(buttonText(footer(popup), "取消")));
    QCOMPARE(f.controller->sessionModel()->rowCount(), 1);
    QVERIFY(f.clickText("删除配置"));
    QVERIFY(f.click(buttonText(footer(popup), "删除")));
    QTRY_COMPARE(f.controller->sessionModel()->rowCount(), 0);
    QTRY_VERIFY(!f.dialog("detailsDialog")->property("visible").toBool());
    ProfileStore saved(f.directory.filePath("profiles.json"));
    QVERIFY(saved.load().isEmpty());
}
void QmlInteractionsTest::nestedEscapeReturnsToDetails() {
    Fixture f; QVERIFY(f.load({profile("a")}, {820, 560})); QVERIFY(f.details());
    for (const auto& entry : {qMakePair(QString("编辑名称"), QString("编辑连接名称")),
                              qMakePair(QString("删除配置"), QString("删除配置")),
                              qMakePair(QString("删除凭据"), QString("删除已保存凭据"))}) {
        QVERIFY(f.clickText(entry.first));
        auto* popup = f.dialog(entry.second); QVERIFY(popup);
        QTRY_VERIFY(popup->property("visible").toBool());
        QTest::keyClick(f.window, Qt::Key_Escape);
        QTRY_VERIFY(!popup->property("visible").toBool());
        QVERIFY(f.dialog("detailsDialog")->property("visible").toBool());
    }
    QVERIFY(f.clickText("关闭"));
    QVERIFY(!f.dialog("detailsDialog")->property("visible").toBool());
}
void QmlInteractionsTest::forgetWaitsForSuccess() {
    Fixture f; f.failForget = true;
    QVERIFY(f.load({profile("a")}, {820, 560})); QVERIFY(f.details());
    QVERIFY(f.clickText("删除凭据"));
    auto* popup = f.dialog("删除已保存凭据"); QVERIFY(popup);
    QVERIFY(f.click(buttonText(footer(popup), "删除")));
    QVERIFY(popup->property("visible").toBool());
    QTRY_COMPARE(f.commands("forgetCredentials"), 1);
    QTRY_VERIFY(!f.window->property("forgetPending").toBool());
    QVERIFY(popup->property("visible").toBool());
    QVERIFY(!f.window->property("forgetError").toString().isEmpty());
    QVERIFY(f.screenshot("forget-error-820"));
    f.failForget = false;
    QVERIFY(f.click(buttonText(footer(popup), "删除")));
    QTRY_COMPARE(f.commands("forgetCredentials"), 2);
    QTRY_VERIFY(!popup->property("visible").toBool());
    QVERIFY(f.dialog("detailsDialog")->property("visible").toBool());
    QCOMPARE(f.controller->sessionModel()->rowCount(), 1);
}
void QmlInteractionsTest::activeProfileCannotBeDeleted() {
    Fixture f; QVERIFY(f.load({profile("a")}, {820, 560})); QVERIFY(f.details());
    QVERIFY(f.clickText("删除配置"));
    auto* popup = f.dialog("删除配置"); QVERIFY(popup);
    f.controller->sessionModel()->updateSession({{"sessionId", "a"}, {"state", "connected"}});
    auto* confirm = buttonText(footer(popup), "删除"); QVERIFY(confirm);
    QVERIFY(!confirm->isEnabled());
    QTest::mouseClick(f.window, Qt::LeftButton, Qt::NoModifier, f.center(confirm));
    QCOMPARE(f.controller->sessionModel()->rowCount(), 1);
    QVERIFY(popup->property("visible").toBool());
    QVERIFY(f.click(buttonText(footer(popup), "取消")));
    QVERIFY(!buttonText(f.root(), "删除配置")->isEnabled());
}
void QmlInteractionsTest::failedSaveKeepsDialogOpen() {
    Fixture f; QVERIFY(f.load({profile("a")}, {820, 560}));
    // A directory at the index filename guarantees a safe, deterministic write failure.
    QVERIFY(QDir().mkdir(f.directory.filePath("profiles.json")));
    QVERIFY(f.details()); QVERIFY(f.clickText("编辑名称"));
    QVERIFY(f.click(f.field("连接名称")));
    QTest::keyClick(f.window, Qt::Key_A, Qt::ControlModifier); f.type("Cannot save");
    auto* rename = f.dialog("编辑连接名称"); QVERIFY(rename);
    QVERIFY(f.click(buttonText(footer(rename), "保存")));
    QVERIFY(rename->property("visible").toBool());
    QCOMPARE(f.controller->sessionModel()->session("a").value("name").toString(), QString("配置 a"));
    QVERIFY(f.screenshot("rename-save-error-820"));
    QVERIFY(f.click(buttonText(footer(rename), "取消")));
    QVERIFY(f.clickText("删除配置"));
    auto* remove = f.dialog("删除配置"); QVERIFY(remove);
    QVERIFY(f.click(buttonText(footer(remove), "删除")));
    QVERIFY(remove->property("visible").toBool());
    QCOMPARE(f.controller->sessionModel()->rowCount(), 1);
}
void QmlInteractionsTest::noticeDoesNotCoverActions() {
    Fixture f; QVERIFY(f.load({profile("a"), profile("b"), profile("c"), profile("d")}, {820, 560}));
    f.controller->notice("操作提示", QString("服务暂不可用，请检查连接。").repeated(12));
    QTest::qWait(50);
    auto* banner = f.item("noticeBar"); QVERIFY(banner);
    QVERIFY(!rect(banner, f.root()).intersects(rect(f.list(), f.root())));
    QVERIFY(f.click(f.action()));
    QTRY_COMPARE(f.commands("connect"), 1);
    QVERIFY(f.screenshot("notice-820"));
    QVERIFY(f.clickText("关闭提示"));
    QTRY_VERIFY(!banner->isVisible());
}
void QmlInteractionsTest::domainSuffixRuleDetails() {
    Fixture f; QVERIFY(f.load({profile("a")}));
    f.controller->addRouteRule("a", "domainSuffix", "*.corp.example.test");
    QVERIFY(f.details());
    bool stored = false, routeLimit = false;
    auto* content = f.dialog("detailsDialog")->property("contentItem").value<QQuickItem*>();
    for (auto* item : visualChildren(content)) {
        if (!item->isVisible()) continue;
        const auto text = item->property("text").toString();
        routeLimit |= text.contains("先连接成功") && text.contains("重新连接");
        QVERIFY2(!text.contains("macOS 分流 DNS"), "共享界面不能把 Windows DNS 功能描述成 macOS 专用");
        stored |= text == "corp.example.test";
        QVERIFY(item->objectName() != "routeRuleRemoveButton");
        QVERIFY(!item->property("placeholderText").toString().contains("10.20.0.0"));
    }
    QVERIFY(stored); QVERIFY(routeLimit);
}
void QmlInteractionsTest::errorsAreReadableAndDiagnosticsRemainAvailable_data() {
    QTest::addColumn<QString>("error");
    QTest::newRow("authentication") << QString("AUTH_FAILED: username/password required");
    QTest::newRow("setup-json") << QString("SetupClient: {\"error\":\"route add failed\",\"routes\":[{\"subnet\":\"10.0.0.0/8\"}]}");
}
void QmlInteractionsTest::errorsAreReadableAndDiagnosticsRemainAvailable() {
    QFETCH(QString, error);
    Fixture f;
    auto failed = profile("a", "failed"); failed.insert("error", error);
    QVERIFY(f.load({failed}, {820, 560}));
    bool friendly = false;
    for (auto* item : visualChildren(f.card("a"))) {
        if (!item->isVisible()) continue;
        const auto text = item->property("text").toString();
        QVERIFY(!text.contains("AUTH_FAILED"));
        QVERIFY(!text.contains("SetupClient"));
        friendly |= text.contains("用户名或密码不正确") || text.contains("诊断信息");
    }
    QVERIFY(friendly);
    QVERIFY(f.details());
    auto* content = f.dialog("detailsDialog")->property("contentItem").value<QQuickItem*>();
    const auto position = f.center(content);
    auto* diagnosticButton = buttonText(f.root(), "查看诊断信息");
    QVERIFY(diagnosticButton);
    for (int attempt = 0; attempt < 10 && !rect(content, f.root()).contains(rect(diagnosticButton, f.root())); ++attempt) {
        QWheelEvent wheel(position, f.window->mapToGlobal(position), {}, {0, -240}, Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        wheel.setTimestamp(QDateTime::currentMSecsSinceEpoch());
        QTest::mouseMove(f.window, position);
        QCoreApplication::sendEvent(f.window, &wheel);
        QTest::qWait(100);
    }
    auto* diagnosticFlick = content->property("contentItem").value<QQuickItem*>();
    QVERIFY(diagnosticFlick);
    QTRY_VERIFY(!diagnosticFlick->property("moving").toBool());
    QVERIFY2(rect(content, f.root()).contains(rect(diagnosticButton, f.root())),
             qPrintable(QString("视口 %1,%2,%3,%4；按钮 %5,%6,%7,%8")
             .arg(rect(content,f.root()).x()).arg(rect(content,f.root()).y()).arg(content->width()).arg(content->height())
             .arg(rect(diagnosticButton,f.root()).x()).arg(rect(diagnosticButton,f.root()).y()).arg(diagnosticButton->width()).arg(diagnosticButton->height())));
    QVERIFY(f.clickText("查看诊断信息"));
    QTRY_VERIFY(f.window->property("diagnosticExpanded").toBool());
    bool original = false;
    for (auto* item : visualChildren(content)) original |= item->isVisible() && item->property("text") == error;
    QVERIFY2(original, "原始诊断必须在详情中可查，不能丢弃");
    auto* diagnostic = named(content, "diagnosticText"); QVERIFY(diagnostic);
    QTRY_VERIFY2(rect(content, f.root()).contains(rect(diagnostic, f.root())),
                 "展开后诊断原文应滚入可读区域，不能藏在 footer 后面");
    QVERIFY(f.screenshot("diagnostics-" + QString::fromLatin1(QTest::currentDataTag())));
}
void QmlInteractionsTest::detailsScrollKeepsFooterReachable() {
    Fixture f;
    auto longProfile = profile("a");
    longProfile.insert("name", QString("很长的连接名称").repeated(15));
    QJsonArray rules;
    for (int i = 0; i < 25; ++i)
        rules.append(QJsonObject{{"type", "domainSuffix"}, {"pattern", QString("*.department-%1.example.test").arg(i)}});
    longProfile.insert("routeStrategy", QJsonObject{{"conflictMode", "compatible"}, {"priority", 1}, {"rules", rules}});
    QVERIFY(f.load({longProfile}, {820, 560})); QVERIFY(f.details());
    auto* popup = f.dialog("detailsDialog"); QVERIFY(popup);
    auto* scroll = popup->property("contentItem").value<QQuickItem*>(); QVERIFY(scroll);
    auto* flick = scroll->property("contentItem").value<QQuickItem*>(); QVERIFY(flick);
    QTRY_VERIFY(flick->property("contentHeight").toReal() > scroll->height());
    const auto pos = f.center(scroll);
    for (int step = 0; step < 10 && flick->property("contentY").toReal() <= 100; ++step) {
        QTest::mouseMove(f.window, pos);
        QWheelEvent wheel(pos, f.window->mapToGlobal(pos), {}, {0, -240}, Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        wheel.setTimestamp(QDateTime::currentMSecsSinceEpoch());
        QCoreApplication::sendEvent(f.window, &wheel);
        QTest::qWait(100);
    }
    QTRY_VERIFY(flick->property("contentY").toReal() > 100);
    QVERIFY(f.screenshot("details-scrolled-820"));
    QVERIFY(f.click(buttonText(footer(popup), "关闭")));
    QTRY_VERIFY(!popup->property("visible").toBool());
}
void QmlInteractionsTest::compactConnectedScreenshot() {
    Fixture f;
    auto a = profile("a", "connected"), b = profile("b", "connected");
    a.insert("name", "Office / Production"); a.insert("remoteHost", "vpn.office.example.com");
    b.insert("name", "研发网络"); b.insert("remoteHost", "vpn.dev.example.com");
    for (auto* value : {&a, &b}) {
        value->insert("durationSeconds", 187); value->insert("downloadBytes", 62438); value->insert("uploadBytes", 54192);
    }
    QVERIFY(f.load({a, b}));
    QVERIFY2(f.card("a")->height() <= 100, "两条连接不应使用高大空卡片");
    QVERIFY(f.screenshot("connected-overview"));
}
void QmlInteractionsTest::connectedWarning_data() {
    QTest::addColumn<QSize>("size");
    QTest::newRow("minimum") << QSize(820, 560);
    QTest::newRow("normal") << QSize(960, 640);
}
void QmlInteractionsTest::connectedWarning() {
    QFETCH(QSize, size);
    const QString warning = "兼容模式跳过路由：10.20.0.0/16 与已有连接重叠";
    const QString technical = "SetupClient: {\"route\":\"10.20.0.0/16\",\"action\":\"skipped\"}";
    auto connected = profile("a", "connected");
    connected.insert("durationSeconds", 61);
    connected.insert("downloadBytes", 1024);
    connected.insert("uploadBytes", 2048);
    connected.insert("warnings", QJsonArray{warning, technical});
    Fixture f; QVERIFY(f.load({connected}, size));
    QQuickItem* stats = nullptr;
    for (auto* item : visualChildren(f.card("a"))) {
        if (!item->isVisible()) continue;
        const auto text = item->property("text").toString();
        if (text.startsWith("运行 ")) stats = item;
        QVERIFY(!text.contains("SetupClient"));
        QVERIFY(!text.contains("10.20.0.0"));
    }
    QVERIFY(stats);
    const auto summary = stats->property("text").toString();
    QVERIFY2(summary.contains(" · 部分路由重叠"), "已连接时统计行也必须显示路由警告");
    QVERIFY(summary.contains("运行 01:01") && summary.contains("1.0 KB") && summary.contains("2.0 KB"));
    QCOMPARE(f.card("a")->height(), 92.0);
    QCOMPARE(stats->property("lineCount").toInt(), 1);
    QVERIFY(stats->property("truncated").isValid());
    QVERIFY(!stats->property("truncated").toBool());
    QVERIFY(rect(f.card("a"), f.root()).contains(rect(stats, f.root())));
    QVERIFY(!rect(stats, f.root()).intersects(rect(f.action(), f.root())));
    const QColor warningColor = stats->property("color").value<QColor>();
    QVERIFY(warningColor.hsvHue() >= 25 && warningColor.hsvHue() <= 50);
    QVERIFY(f.screenshot("connected-warning-" + QString::number(size.width())));

    // Snapshot changes must remove and restore the hint without changing state or card height.
    QVERIFY(f.controller->sessionModel()->updateSession({{"sessionId", "a"}, {"warnings", QJsonArray{}}}));
    QTRY_VERIFY(!stats->property("text").toString().contains("部分路由重叠"));
    QVERIFY(stats->property("color").value<QColor>() != warningColor);
    QCOMPARE(f.card("a")->height(), 92.0);
    QVERIFY(f.controller->sessionModel()->updateSession({{"sessionId", "a"}, {"warnings", QJsonArray{warning, technical}}}));
    QTRY_VERIFY(stats->property("text").toString().contains("部分路由重叠"));

    QVERIFY(f.details());
    auto* popup = f.dialog("detailsDialog"); QVERIFY(popup);
    auto* content = popup->property("contentItem").value<QQuickItem*>(); QVERIFY(content);
    auto* show = buttonText(content, "查看诊断信息"); QVERIFY(show);
    if (!rect(content, f.root()).contains(rect(show, f.root()))) {
        const auto pos = f.center(content);
        QWheelEvent wheel(pos, f.window->mapToGlobal(pos), {}, {0, -2400}, Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(f.window, &wheel);
        QTRY_VERIFY(rect(content, f.root()).contains(rect(show, f.root())));
        auto* flick = content->property("contentItem").value<QQuickItem*>(); QVERIFY(flick);
        QTRY_VERIFY(!flick->property("moving").toBool());
    }
    QVERIFY(f.click(show));
    auto* diagnostic = named(content, "diagnosticText"); QVERIFY(diagnostic);
    QTRY_VERIFY(diagnostic->isVisible());
    QCOMPARE(diagnostic->property("text").toString(), warning + '\n' + technical);
    QTRY_VERIFY2(rect(content, f.root()).contains(rect(diagnostic, f.root())),
        qPrintable(QString("viewport=%1,%2 %3x%4 diagnostic=%5,%6 %7x%8")
            .arg(rect(content, f.root()).x()).arg(rect(content, f.root()).y())
            .arg(rect(content, f.root()).width()).arg(rect(content, f.root()).height())
            .arg(rect(diagnostic, f.root()).x()).arg(rect(diagnostic, f.root()).y())
            .arg(rect(diagnostic, f.root()).width()).arg(rect(diagnostic, f.root()).height())));
    QVERIFY(f.screenshot("connected-warning-details-" + QString::number(size.width())));
    QVERIFY(f.click(buttonText(footer(popup), "关闭")));
    QTRY_VERIFY(!popup->property("visible").toBool());
    QCOMPARE(f.controller->sessionModel()->session("a").value("state").toString(), QString("connected"));
    QVERIFY(f.requests.isEmpty());
}
void QmlInteractionsTest::quitConfirmationKeepsConnections() {
    Fixture f; QVERIFY(f.load({profile("a", "connected"), profile("b", "connected")}, {820, 560}));
    QSignalSpy quit(f.controller.get(), &AppController::quitRequested);
    QVERIFY(f.clickText("设置")); QVERIFY(f.clickText("退出界面"));
    auto* popup = f.dialog("退出 OpenVPN"); QVERIFY(popup);
    QTRY_VERIFY(popup->property("visible").toBool());
    QVERIFY(f.controller->quitConfirmationPending());
    QTest::keyClick(f.window, Qt::Key_Escape);
    QTRY_VERIFY(!popup->property("visible").toBool());
    QVERIFY(!f.controller->quitConfirmationPending());
    QVERIFY(f.dialog("settingsDialog")->property("visible").toBool());
    QCOMPARE(quit.count(), 0);
    QVERIFY(f.clickText("退出界面"));
    QVERIFY(f.screenshot("quit-active-820"));
    QVERIFY(f.click(buttonText(footer(popup), "退出界面")));
    QCOMPARE(quit.count(), 1);
    QCOMPARE(f.requests.size(), 0);
    QCOMPARE(f.controller->sessionModel()->session("a").value("state").toString(), QString("connected"));
    QCOMPARE(f.controller->sessionModel()->session("b").value("state").toString(), QString("connected"));
}
int main(int argc, char** argv) {
    QQuickStyle::setStyle("Basic");
    QApplication application(argc, argv);
    application.setApplicationVersion("0.2.3");
    application.setQuitOnLastWindowClosed(false);
    QLocale::setDefault(QLocale(QLocale::Japanese, QLocale::Japan));
    QTranslator japanese;
    if (japanese.load("qtbase_ja", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        application.installTranslator(&japanese);
    QmlInteractionsTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_qml_interactions.moc"
