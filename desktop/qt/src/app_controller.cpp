#include "app_controller.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QHostAddress>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QSignalBlocker>
#include <cmath>

#include <algorithm>
#include <utility>
#include <memory>

namespace {

QString stateForEvent(const QString& event) {
    if (event == QStringLiteral("CONNECTED")) return QStringLiteral("connected");
    if (event == QStringLiteral("DISCONNECTED")) {
        return QStringLiteral("disconnected");
    }
    if (event == QStringLiteral("RECONNECTING") ||
        event == QStringLiteral("RESOLVE")) {
        return QStringLiteral("connecting");
    }
    if (event == QStringLiteral("FATAL_ERROR") || event == QStringLiteral("AUTH_FAILED")) return QStringLiteral("failed");
    return {};
}

bool storedProfile(const QJsonObject& profile) {
    return !profile.value("profileId").toString().isEmpty() &&
           !profile.value("profilePath").toString().isEmpty();
}

bool activeProfile(const QJsonObject& profile) {
    const auto state = profile.value("state").toString();
    return state == "connected" || state == "connecting" || state == "disconnecting";
}

QString normalizeDomainPattern(const QString& type,
                               const QString& pattern,
                               bool* valid) {
    QString value = pattern.trimmed().toLower();
    if (type == QStringLiteral("domainSuffix") && value.startsWith(QStringLiteral("*."))) {
        value.remove(0, 2);
    } else if (value.contains(QLatin1Char('*'))) {
        if (valid) *valid = false;
        return {};
    }
    while (value.endsWith(QLatin1Char('.'))) value.chop(1);
    if (value.isEmpty() || value.size() > 253) {
        if (valid) *valid = false;
        return {};
    }
    const QRegularExpression labelPattern(QStringLiteral("^[a-z0-9_](?:[a-z0-9_-]{0,61}[a-z0-9_])?$"));
    const QStringList labels = value.split(QLatin1Char('.'));
    for (const QString& label : labels) {
        if (label.isEmpty() || label.size() > 63 || !labelPattern.match(label).hasMatch()) {
            if (valid) *valid = false;
            return {};
        }
    }
    if (valid) *valid = true;
    return value;
}

bool releaseUrl(const QUrl& url, bool asset = false) {
    const auto path = url.path(QUrl::FullyDecoded);
    return url.isValid() && url.scheme() == "https" && url.host() == "github.com" &&
           url.userInfo().isEmpty() && (url.port() == -1 || url.port() == 443) &&
           !path.split('/').contains("..") &&
           path.startsWith(asset ? "/flyu518/open-vpn/releases/download/" : "/flyu518/open-vpn/releases");
}

bool downloadUrl(const QUrl& url) {
    return url.scheme() == "https" && url.userInfo().isEmpty() &&
           (url.port() == -1 || url.port() == 443) &&
           (releaseUrl(url, true) || url.host() == "release-assets.githubusercontent.com" ||
            url.host() == "objects.githubusercontent.com");
}

QJsonObject defaultRuntimeState(QJsonObject value) {
    if (!value.contains(QStringLiteral("state"))) {
        value.insert(QStringLiteral("state"), QStringLiteral("disconnected"));
    }
    if (!value.contains(QStringLiteral("downloadBytes"))) {
        value.insert(QStringLiteral("downloadBytes"), 0);
    }
    if (!value.contains(QStringLiteral("uploadBytes"))) {
        value.insert(QStringLiteral("uploadBytes"), 0);
    }
    if (!value.contains(QStringLiteral("durationSeconds"))) {
        value.insert(QStringLiteral("durationSeconds"), 0);
    }
    return value;
}

QString defaultEngineSocketName() {
#ifdef Q_OS_WIN
    return QStringLiteral(R"(\\.\pipe\openvpn-ui)");
#else
    return QStringLiteral("/tmp/openvpn-ui.sock");
#endif
}

}  // namespace

AppController::AppController(bool trayAvailable,
                             QString profileStorePath,
                             QString engineSocketName,
                             QObject* parent)
    : QObject(parent),
      profileStore_(std::move(profileStorePath)),
      trafficHistory_(QFileInfo(profileStore_.location()).dir().filePath("traffic")),
      trayAvailable_(trayAvailable),
      engine_(engineSocketName.isEmpty() ? defaultEngineSocketName()
                                         : std::move(engineSocketName),
              this),
      snapshotTimer_(this) {
    updateNetwork_.setParent(this);
    connect(&engine_, &EngineClient::eventReceived, this,
            &AppController::onEngineEvent);
    connect(&engine_, &EngineClient::availabilityChanged, this,
            &AppController::onEngineAvailabilityChanged);
    connect(&engine_, &EngineClient::serviceError, this,
            &AppController::onEngineError);
    connect(&snapshotTimer_, &QTimer::timeout, this,
            &AppController::pollSnapshot);
}

AppController::~AppController() {
    snapshotTimer_.stop();
    engine_.disconnect(this);
    if (updateReply_) {
        updateReply_->disconnect(this);
        updateReply_->abort();
    }
}

SessionListModel* AppController::sessionModel() {
    return &sessionModel_;
}

QString AppController::serviceError() const {
    return serviceError_;
}

QString AppController::storageError() const {
    return storageError_;
}

QString AppController::storageLocation() const {
    return profileStore_.location();
}

bool AppController::serviceAvailable() const {
    return serviceAvailable_;
}

bool AppController::quitting() const {
    return quitting_;
}

bool AppController::quitConfirmationPending() const {
    return quitConfirmationPending_;
}

bool AppController::trayAvailable() const {
    return trayAvailable_;
}

void AppController::setTrayAvailable(bool available) {
    if (trayAvailable_ == available) return;
    trayAvailable_ = available;
    emit trayAvailableChanged();
}

void AppController::start() {
    if (started_) return;
    started_ = true;
    loadProfiles();
    loadStartupState();
    snapshotTimer_.start(2500);
    refreshService();
}

void AppController::refreshService() {
    engine_.request(
        QJsonObject{{QStringLiteral("command"), QStringLiteral("ping")}},
        [this](const QJsonObject& value) {
            if (!value.value(QStringLiteral("ok")).toBool()) {
                setServiceError(value.value(QStringLiteral("message")).toString(
                    QStringLiteral("OpenVPN 服务不可用")));
                return;
            }
            setServiceError({});
            requestSnapshot();
        });
}

void AppController::importProfile() {
    const QString path = QFileDialog::getOpenFileName(
        nullptr, QStringLiteral("导入 OpenVPN 配置"), {},
        QStringLiteral("OpenVPN 配置 (*.ovpn);;所有文件 (*)"));
    if (path.isEmpty()) return;

    importProfilePath(path);
}

bool AppController::importProfilePath(const QString& path) {
    if (path.trimmed().isEmpty()) return false;
    if (!profilesLoaded_ && sessionModel_.rowCount() == 0) {
        loadProfiles();
        if (!storageError_.isEmpty() && sessionModel_.rowCount() == 0) return false;
    }

    QJsonObject profile;
    QString error;
    if (!profileStore_.importFile(path, &profile, &error)) {
        emit notice(QStringLiteral("导入失败"), error);
        return false;
    }

    const QString importedContents =
        profile.value(QStringLiteral("profile")).toString().trimmed();
    QList<QJsonObject> profiles = sessionModel_.sessions();
    int replacement = -1;
    int row = 0;
    for (const QJsonObject& existing : sessionModel_.sessions()) {
        const QString existingContents =
            existing.value(QStringLiteral("profile")).toString().trimmed();
        const bool sameContent = !importedContents.isEmpty() &&
                                 importedContents == existingContents;
        if (sameContent) {
            const QString reason = QStringLiteral("配置内容完全相同");
            emit notice(QStringLiteral("配置已存在"),
                        QStringLiteral("检测到重复配置（") + reason +
                            QStringLiteral("）：") +
                            existing.value(QStringLiteral("name"))
                                .toString(QStringLiteral("未命名配置")));
            return false;
        }
        if (storedProfile(existing) && QFileInfo(existing.value("profilePath").toString()).absoluteFilePath() ==
                                           QFileInfo(path).absoluteFilePath()) replacement = row;
        ++row;
    }
    if (replacement >= 0) {
        const auto existing = profiles.at(replacement);
        if (activeProfile(existing)) {
            emit notice(QStringLiteral("无法更新配置"), QStringLiteral("请先断开此连接，再更新配置"));
            return false;
        }
        // Identity is also the Keychain lookup key. A content update must not
        // replace it or overwrite the user's name, policy or history.
        auto updated = existing;
        for (const auto& key : {"profile", "username", "remoteHost"}) updated.insert(key, profile.value(key));
        profiles[replacement] = updated;
        profile = updated;
    } else {
        QString id = profile.value("profileId").toString();
        if (!sessionModel_.session(id).isEmpty()) {
            id = "profile-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
            for (const auto& key : {"id", "profileId", "sessionId"}) profile.insert(key, id);
        }
        profiles.append(defaultRuntimeState(profile));
    }
    if (!commitProfiles(profiles)) return false;
    emit notice(QStringLiteral("导入成功"),
                QStringLiteral("已添加配置：") +
                    profile.value(QStringLiteral("name")).toString());
    return true;
}

void AppController::exportProfile(const QString& sessionId) {
    const QJsonObject profile = sessionModel_.session(sessionId);
    if (profile.isEmpty()) return;
    const QString suggested = profile.value(QStringLiteral("name")).toString() +
                              QStringLiteral(".ovpn");
    const QString path = QFileDialog::getSaveFileName(
        nullptr, QStringLiteral("导出 OpenVPN 配置"), suggested,
        QStringLiteral("OpenVPN 配置 (*.ovpn);;所有文件 (*)"));
    if (path.isEmpty()) return;

    QString error;
    if (!profileStore_.exportFile(profile, path, &error)) {
        emit notice(QStringLiteral("导出失败"), error);
        return;
    }
    emit notice(QStringLiteral("导出成功"), QStringLiteral("配置已导出到：") + path);
}

bool AppController::renameSession(const QString& sessionId, const QString& name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        emit notice(QStringLiteral("名称无效"), QStringLiteral("连接名称不能为空"));
        return false;
    }
    return updateProfile(sessionId, QJsonObject{{"name", trimmed}, {"nameIsCustom", true}});
}

void AppController::toggleConnection(const QString& sessionId) {
    const QJsonObject profile = sessionModel_.session(sessionId);
    if (profile.isEmpty()) return;
    const QString state = profile.value(QStringLiteral("state")).toString();
    if (disconnecting_.contains(sessionId)) return;
    if (state == QStringLiteral("connected") ||
        state == QStringLiteral("connecting") ||
        state == QStringLiteral("disconnecting")) {
        const auto operation = ++operations_[sessionId];
        ++revisions_[sessionId];
        connecting_.remove(sessionId);
        disconnecting_.insert(sessionId);
        cancelledConnections_.insert(sessionId);
        credentialPrompted_.remove(sessionId);
        credentialCancelled_.insert(sessionId);
        sessionModel_.updateSession(QJsonObject{{"sessionId", sessionId}, {"state", "disconnecting"}});
        engine_.request(
            QJsonObject{{QStringLiteral("command"), QStringLiteral("disconnect")},
                        {QStringLiteral("sessionId"), sessionId}},
            [this, sessionId, operation, state](const QJsonObject& value) {
                if (operations_.value(sessionId) != operation) return;
                disconnecting_.remove(sessionId);
                if (!value.value(QStringLiteral("ok")).toBool()) {
                    cancelledConnections_.remove(sessionId);
                    sessionModel_.updateSession(QJsonObject{{"sessionId", sessionId}, {"state", state}});
                    emit notice(QStringLiteral("断开失败"),
                                value.value(QStringLiteral("message")).toString());
                    return;
                }
                needsCleanup_.remove(sessionId);
                ++revisions_[sessionId];
                applyRuntimeSession(
                    QJsonObject{{QStringLiteral("sessionId"), sessionId},
                                {QStringLiteral("state"),
                                 QStringLiteral("disconnected")},
                                {QStringLiteral("error"), QString{}}});
                requestSnapshot();
            });
        return;
    }
    credentialCancelled_.remove(sessionId);
    if (authenticationRejected_.contains(sessionId)) {
        requestCredentials(profile, QStringLiteral("认证失败，请重新输入 VPN 凭据"));
        return;
    }
    connectSession(profile);
}

void AppController::submitCredentials(const QString& sessionId,
                                      const QString& username,
                                      const QString& password,
                                      bool remember) {
    const QString user = username.trimmed();
    if (user.isEmpty() || password.isEmpty()) {
        emit notice(QStringLiteral("凭据不完整"),
                    QStringLiteral("请输入用户名和密码后再连接"));
        return;
    }
    const QJsonObject profile = sessionModel_.session(sessionId);
    if (profile.isEmpty() || activeProfile(profile)) return;
    credentialPrompted_.remove(sessionId);
    credentialCancelled_.remove(sessionId);
    connectSession(profile, user, password, remember);
}

void AppController::cancelCredentials(const QString& sessionId) {
    credentialPrompted_.remove(sessionId);
    credentialCancelled_.insert(sessionId);
    if (sessionModel_.session(sessionId).value("state") == "connecting") toggleConnection(sessionId);
}

void AppController::forgetCredentials(const QString& sessionId) {
    const QJsonObject profile = sessionModel_.session(sessionId);
    if (!storedProfile(profile)) return;
    const QString profileId = profile.value(QStringLiteral("profileId")).toString(
        sessionId);
    if (profileId.isEmpty()) return;
    engine_.request(
        QJsonObject{{QStringLiteral("command"), QStringLiteral("forgetCredentials")},
                    {QStringLiteral("profileId"), profileId}},
        [this](const QJsonObject& value) {
            if (value.value(QStringLiteral("ok")).toBool()) {
                emit notice(QStringLiteral("已删除"), QStringLiteral("已删除此配置的已保存凭据"));
            } else {
                emit notice(QStringLiteral("删除失败"),
                            value.value(QStringLiteral("message")).toString());
            }
        });
}

QVariantList AppController::routeRules(const QString& sessionId) const {
    QVariantList result;
    const QJsonObject profile = sessionModel_.session(sessionId);
    if (profile.isEmpty()) return result;
    const QJsonArray rules = routeStrategyOrDefault(profile)
                                 .value(QStringLiteral("rules"))
                                 .toArray();
    for (const QJsonValue& value : rules) {
        const QJsonObject rule = value.toObject();
        result.append(QVariantMap{
            {QStringLiteral("type"), rule.value(QStringLiteral("type")).toString()},
            {QStringLiteral("pattern"),
             rule.value(QStringLiteral("pattern")).toString()},
            {QStringLiteral("action"),
             rule.value(QStringLiteral("action")).toString(QStringLiteral("prefer"))},
            {QStringLiteral("enabled"),
             rule.value(QStringLiteral("enabled")).toBool(true)},
        });
    }
    return result;
}

bool AppController::addRouteRule(const QString& sessionId,
                                 const QString& type,
                                 const QString& pattern,
                                 const QString& action) {
    const QString trimmedType = type.trimmed();
    QString trimmedPattern = pattern.trimmed();
    const QString trimmedAction = action.trimmed();
    if (trimmedAction != QStringLiteral("prefer") && trimmedAction != QStringLiteral("exclude")) {
        emit notice(QStringLiteral("规则无效"), QStringLiteral("路由规则动作无效"));
        return false;
    }
    if (trimmedType != QStringLiteral("ip") && trimmedType != QStringLiteral("cidr") &&
        trimmedType != QStringLiteral("domain") && trimmedType != QStringLiteral("domainSuffix")) {
        emit notice(QStringLiteral("暂不支持"), QStringLiteral("未知的路由规则类型"));
        return false;
    }
    if (trimmedType == QStringLiteral("domain") || trimmedType == QStringLiteral("domainSuffix")) {
        bool valid = false;
        trimmedPattern = normalizeDomainPattern(trimmedType, trimmedPattern, &valid);
        if (!valid) {
            emit notice(QStringLiteral("规则无效"),
                        QStringLiteral("请输入有效的域名或域名后缀，例如 *.corp.example.com"));
            return false;
        }
        const QJsonObject profile = sessionModel_.session(sessionId);
        if (profile.isEmpty()) return false;
        QJsonObject strategy = routeStrategyOrDefault(profile);
        QJsonArray rules = strategy.value(QStringLiteral("rules")).toArray();
        if (rules.size() >= 32) {
            emit notice(QStringLiteral("规则过多"), QStringLiteral("每个连接最多保存 32 条路由规则"));
            return false;
        }
        rules.append(QJsonObject{{QStringLiteral("type"), trimmedType},
                                 {QStringLiteral("pattern"), trimmedPattern},
                                 {QStringLiteral("action"), trimmedAction},
                                 {QStringLiteral("enabled"), true}});
        strategy.insert(QStringLiteral("rules"), rules);
        const bool saved = updateProfile(sessionId, QJsonObject{{"routeStrategy", strategy}});
        if (saved) emit notice(QStringLiteral("域名规则已保存"),
                               QStringLiteral("请断开本连接并重新连接；域名后缀需要当前 VPN 提供 DNS 服务器，实际 IP 路由仍由 VPN 配置提供。"));
        return saved;
    }
    QHostAddress address;
    if (trimmedType == QStringLiteral("ip")) {
        if (!address.setAddress(trimmedPattern)) {
            emit notice(QStringLiteral("规则无效"), QStringLiteral("请输入有效的 IPv4 或 IPv6 地址"));
            return false;
        }
        trimmedPattern = address.toString() +
            (address.protocol() == QAbstractSocket::IPv4Protocol ? QStringLiteral("/32") : QStringLiteral("/128"));
    } else {
        const int slash = trimmedPattern.lastIndexOf('/');
        bool ok = false;
        const int prefix = slash >= 0 ? trimmedPattern.mid(slash + 1).toInt(&ok) : -1;
        const QString addressText = slash >= 0 ? trimmedPattern.left(slash) : QString{};
        if (slash <= 0 || !ok || !address.setAddress(addressText) ||
            prefix < 0 || prefix > (address.protocol() == QAbstractSocket::IPv4Protocol ? 32 : 128)) {
            emit notice(QStringLiteral("规则无效"), QStringLiteral("请输入有效的 IP/CIDR，例如 10.20.0.0/16"));
            return false;
        }
        trimmedPattern = address.toString() + "/" + QString::number(prefix);
    }
    if (trimmedPattern.isEmpty()) {
        emit notice(QStringLiteral("规则无效"), QStringLiteral("请输入规则内容"));
        return false;
    }
    const QJsonObject profile = sessionModel_.session(sessionId);
    if (profile.isEmpty()) return false;
    QJsonObject strategy = routeStrategyOrDefault(profile);
    QJsonArray rules = strategy.value(QStringLiteral("rules")).toArray();
    if (rules.size() >= 32) {
        emit notice(QStringLiteral("规则过多"), QStringLiteral("每个连接最多保存 32 条路由规则"));
        return false;
    }
    rules.append(QJsonObject{
        {QStringLiteral("type"), trimmedType},
        {QStringLiteral("pattern"), trimmedPattern},
        {QStringLiteral("action"), trimmedAction},
        {QStringLiteral("enabled"), true},
    });
    strategy.insert(QStringLiteral("rules"), rules);
    const bool saved = updateProfile(sessionId, QJsonObject{{"routeStrategy", strategy}});
    if (saved) emit notice(QStringLiteral("路由规则已保存"),
                           QStringLiteral("请先断开本连接，再重新连接后生效。"));
    return saved;
}

bool AppController::removeRouteRule(const QString& sessionId, int index) {
    const QJsonObject profile = sessionModel_.session(sessionId);
    if (profile.isEmpty()) return false;
    QJsonObject strategy = routeStrategyOrDefault(profile);
    QJsonArray rules = strategy.value(QStringLiteral("rules")).toArray();
    if (index < 0 || index >= rules.size()) return false;
    rules.removeAt(index);
    strategy.insert(QStringLiteral("rules"), rules);
    const bool saved = updateProfile(sessionId, QJsonObject{{"routeStrategy", strategy}});
    if (saved) emit notice(QStringLiteral("路由规则已删除"),
                           QStringLiteral("请重新连接本 VPN 后使路由变化生效。"));
    return saved;
}

bool AppController::removeProfile(const QString& sessionId) {
    const QJsonObject profile = sessionModel_.session(sessionId);
    if (profile.isEmpty()) return false;
    const QString state = profile.value(QStringLiteral("state")).toString();
    if (state == QStringLiteral("connected") ||
        state == QStringLiteral("connecting") ||
        state == QStringLiteral("disconnecting")) {
        emit notice(QStringLiteral("无法删除配置"),
                    QStringLiteral("请先断开此连接，再删除配置"));
        return false;
    }
    auto profiles = sessionModel_.sessions();
    profiles.removeIf([&](const auto& value) { return value.value("sessionId").toString() == sessionId; });
    if (!commitProfiles(profiles)) return false;
    if (!trafficHistory_.remove(sessionId)) emit notice(QStringLiteral("历史流量清理失败"), trafficHistory_.error());
    credentialPrompted_.remove(sessionId);
    credentialCancelled_.remove(sessionId);
    authenticationRejected_.remove(sessionId);
    pendingConnectionHistory_.remove(sessionId);
    countedConnections_.remove(sessionId);
    ++operations_[sessionId];
    emit notice(QStringLiteral("已删除配置"),
                QStringLiteral("配置索引已删除，原 .ovpn 文件未被删除"));
    return true;
}

bool AppController::setConflictMode(const QString& sessionId,
                                    const QString& mode) {
    if (mode != QStringLiteral("compatible") &&
        mode != QStringLiteral("strict") && mode != QStringLiteral("ask")) {
        return false;
    }
    const QJsonObject profile = sessionModel_.session(sessionId);
    if (profile.isEmpty()) return false;
    QJsonObject strategy = routeStrategyOrDefault(profile);
    strategy.insert(QStringLiteral("conflictMode"), mode);
    const bool saved = updateProfile(sessionId, QJsonObject{{"routeStrategy", strategy}});
    if (saved) {
        const QString state = profile.value(QStringLiteral("state")).toString();
        if (state == QStringLiteral("connected") ||
            state == QStringLiteral("connecting") ||
            state == QStringLiteral("disconnecting")) {
            emit notice(QStringLiteral("路由策略已保存"),
                        QStringLiteral("当前连接仍使用旧策略，请先断开本连接，再重新连接后生效。"));
        } else {
            emit notice(QStringLiteral("路由策略已保存"),
                        QStringLiteral("下次连接时将使用新的路由策略。"));
        }
    }
    return saved;
}

bool AppController::moveSession(const QString& sourceId, const QString& targetId) {
    auto profiles = sessionModel_.sessions();
    int source = -1, target = -1;
    for (int i = 0; i < profiles.size(); ++i) {
        const auto id = profiles.at(i).value("sessionId").toString();
        if (id == sourceId) source = i;
        if (id == targetId) target = i;
    }
    if (source < 0 || target < 0 || source == target) return false;
    profiles.move(source, target);
    return commitProfiles(profiles);
}

QVariantMap AppController::trafficForSession(const QString& sessionId, int hours) {
    const auto now = QDateTime::currentMSecsSinceEpoch();
    const auto points = trafficHistory_.points(sessionId, hours, now);
    return {{"points", points}, {"now", now}, {"error", trafficHistory_.error()}};
}

QVariantMap AppController::routeConflictInfo(const QString& sessionId) const {
    const auto session = sessionModel_.session(sessionId);
    QVariantList groups;
    QStringList keys;
    bool blocked = false;
    for (const auto& value : session.value("routeConflicts").toArray()) {
        const auto conflict = value.toObject();
        const auto peerId = conflict.value("peerSessionId").toString();
        const bool external = conflict.value("external").toBool();
        const auto peer = external ? QJsonObject{} : sessionModel_.session(peerId);
        auto name = peer.value("name").toString(conflict.value("peerName").toString("其他 VPN 连接"));
        if (name.trimmed().isEmpty()) name = QStringLiteral("未命名配置");
        const auto key = external ? QStringLiteral("external:") + conflict.value("peerKey").toString(name) : peerId;
        int index = keys.indexOf(key);
        if (index < 0) {
            index = groups.size(); keys.append(key);
            const QString state = peer.isEmpty() ? QStringLiteral("当前状态未知") :
                peer.value("state") == "connected" ? QStringLiteral("当前已连接") :
                peer.value("state") == "connecting" ? QStringLiteral("当前正在连接") : QStringLiteral("当前未连接");
            groups.append(QVariantMap{{"name", name}, {"status", state}, {"routes", QVariantList{}}});
        }
        auto group = groups[index].toMap();
        auto routes = group.value("routes").toList();
        const bool rejected = conflict.value("action") == "blocked";
        blocked |= rejected;
        const QVariantMap route{{"candidateRoute", conflict.value("candidateRoute").toString()},
                                {"activeRoute", conflict.value("activeRoute").toString()},
                                {"result", rejected ? QStringLiteral("严格模式拒绝连接") : QStringLiteral("本连接已跳过")}};
        if (!routes.contains(route)) routes.append(route);
        group.insert("routes", routes); groups[index] = group;
    }
    QString summary;
    if (!groups.isEmpty()) {
        summary = QStringLiteral("与「%1」").arg(groups.first().toMap().value("name").toString());
        if (groups.size() > 1) summary += QStringLiteral("等 %1 个连接").arg(groups.size());
        summary += blocked ? QStringLiteral("路由重叠，连接被阻止") : QStringLiteral("重叠，部分路由已跳过");
    }
    return {{"groups", groups}, {"summary", summary},
            {"hasWarning", !groups.isEmpty() || !session.value("warnings").toArray().isEmpty() ||
                           session.value("error").toString().contains(QStringLiteral("重叠")) ||
                           session.value("error").toString().contains(QStringLiteral("冲突"))}};
}

QVariantMap AppController::detailsForSession(const QString& sessionId) const {
    const QJsonObject profile = sessionModel_.session(sessionId);
    QVariantMap details;
    if (profile.isEmpty()) return details;
    details.insert(QStringLiteral("name"), profile.value(QStringLiteral("name")).toString());
    details.insert(QStringLiteral("remoteHost"),
                   profile.value(QStringLiteral("remoteHost")).toString());
    details.insert(QStringLiteral("profilePath"),
                   profile.value(QStringLiteral("profilePath")).toString());
    details.insert(QStringLiteral("username"),
                   profile.value(QStringLiteral("username")).toString());
    details.insert(QStringLiteral("state"), profile.value(QStringLiteral("state")).toString());
    details.insert(QStringLiteral("profileId"),
                   profile.value(QStringLiteral("profileId")).toString(sessionId));
    details.insert(QStringLiteral("configLines"),
                   profile.value(QStringLiteral("profile")).toString().count('\n'));
    details.insert(QStringLiteral("connectionCount"),
                   profile.value(QStringLiteral("connectionCount")).toInt(0));
    details.insert(QStringLiteral("lastConnectedAt"),
                   profile.value(QStringLiteral("lastConnectedAt")).toString());
    details.insert(QStringLiteral("lastDisconnectedAt"),
                   profile.value(QStringLiteral("lastDisconnectedAt")).toString());
    details.insert(QStringLiteral("routeStrategy"),
                   profile.value(QStringLiteral("routeStrategy")).toObject().toVariantMap());
    details.insert(QStringLiteral("conflictMode"),
                   routeStrategyOrDefault(profile)
                       .value(QStringLiteral("conflictMode"))
                       .toString(QStringLiteral("compatible")));
    return details;
}

bool AppController::startupEnabled() const {
    return startupEnabled_;
}

bool AppController::startupBusy() const {
    return startupBusy_;
}

QString AppController::startupError() const {
    return startupError_;
}

QString AppController::appVersion() const {
    return QCoreApplication::applicationVersion();
}

QString AppController::updateState() const {
    return updateState_;
}

QString AppController::updateMessage() const {
    return updateMessage_;
}

bool AppController::updateAvailable() const {
    return updateState_ == QStringLiteral("available");
}

bool AppController::updateInstallerAvailable() const {
    return updateAvailable() && releaseUrl(updateInstallerUrl_, true) &&
           updateInstallerSha256_.size() == 32 && updateInstallerSize_ > 0;
}

QString AppController::updateInstallerName() const {
    return updateInstallerName_;
}

void AppController::loadStartupState() {
    QString error;
    const bool enabled = readStartupState(&error);
    if (!error.isEmpty()) {
        setStartupError(error);
        return;
    }
    setStartupError({});
    if (startupEnabled_ != enabled) {
        startupEnabled_ = enabled;
        emit startupEnabledChanged();
    }
}

void AppController::setStartupEnabled(bool enabled) {
    if (startupBusy_ || startupEnabled_ == enabled) return;
    const bool previous = startupEnabled_;
    startupBusy_ = true;
    setStartupError({});
    emit startupBusyChanged();
    QTimer::singleShot(0, this, [this, enabled, previous] {
        QString error;
        if (!writeStartupState(enabled, &error)) {
            setStartupError(error);
            if (startupEnabled_ != previous) {
                startupEnabled_ = previous;
                emit startupEnabledChanged();
            }
        } else if (startupEnabled_ != enabled) {
            startupEnabled_ = enabled;
            emit startupEnabledChanged();
        }
        startupBusy_ = false;
        emit startupBusyChanged();
    });
}

void AppController::checkForUpdates() {
    if (updateState_ == "downloading") return;
    if (updateReply_) {
        const auto old = updateReply_;
        updateReply_.clear();
        old->abort();
        old->deleteLater();
    }
    updateInstallerUrl_ = QUrl();
    updateReleaseUrl_ = QUrl();
    updateInstallerName_.clear();
    updateInstallerSha256_.clear();
    updateInstallerSize_ = 0;
    setUpdateState(QStringLiteral("checking"));
    setUpdateMessage(QStringLiteral("正在检查 GitHub Releases…"));

    QNetworkRequest request(
        QUrl(QStringLiteral("https://api.github.com/repos/flyu518/open-vpn/releases/latest")));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("OpenVPN/") +
                          QCoreApplication::applicationVersion());
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = updateNetwork_.get(request);
    reply->setReadBufferSize(1024 * 1024 + 1);
    updateReply_ = reply;
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 1024 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (updateReply_ != reply) {
            reply->deleteLater();
            return;
        }
        updateReply_.clear();
        const int statusCode = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        const QNetworkReply::NetworkError networkError = reply->error();
        reply->deleteLater();
        applyReleaseResponse(payload, statusCode, networkError);
    });
}

void AppController::applyReleaseResponse(const QByteArray& payload, int statusCode,
                                         QNetworkReply::NetworkError networkError) {
        updateReleaseUrl_ = QUrl();
        updateInstallerUrl_ = QUrl();
        updateInstallerName_.clear();
        updateInstallerSha256_.clear();
        updateInstallerSize_ = 0;
        if (networkError != QNetworkReply::NoError && statusCode != 404) {
            setUpdateState(QStringLiteral("error"));
            setUpdateMessage(QStringLiteral("检查更新失败：") +
                             (networkError == QNetworkReply::ContentNotFoundError
                                  ? QStringLiteral("暂无发布版本")
                                  : QStringLiteral("网络请求失败")));
            return;
        }
        if (statusCode == 404) {
            setUpdateState(QStringLiteral("none"));
            setUpdateMessage(QStringLiteral("暂无可访问的发行版（可能尚未发布或无访问权限）"));
            return;
        }
        if (statusCode < 200 || statusCode >= 300) {
            setUpdateState(QStringLiteral("error"));
            setUpdateMessage(QStringLiteral("检查更新失败（HTTP %1）").arg(statusCode));
            return;
        }

        QJsonParseError parseError{};
        const QJsonDocument document =
            QJsonDocument::fromJson(payload, &parseError);
        const QJsonObject release = document.object();
        const QString tag = release.value(QStringLiteral("tag_name")).toString();
        const QRegularExpression pattern(QStringLiteral(R"(^v?(\d+)\.(\d+)\.(\d+)$)"));
        const auto remoteMatch = pattern.match(tag.trimmed());
        const auto currentMatch = pattern.match(appVersion());
        if (payload.size() > 1024 * 1024 || parseError.error != QJsonParseError::NoError || !document.isObject() ||
            !remoteMatch.hasMatch() || !currentMatch.hasMatch()) {
            setUpdateState(QStringLiteral("error"));
            setUpdateMessage(QStringLiteral("发布版本号格式无法识别"));
            return;
        }
        if (release.value("draft").toBool() || release.value("prerelease").toBool()) {
            setUpdateState(QStringLiteral("none"));
            setUpdateMessage(QStringLiteral("暂无可访问的正式发行版"));
            return;
        }
        for (int i = 1; i <= 3; ++i) {
            bool remoteOk = false, currentOk = false;
            remoteMatch.captured(i).toInt(&remoteOk);
            currentMatch.captured(i).toInt(&currentOk);
            if (!remoteOk || !currentOk) {
                setUpdateState(QStringLiteral("error"));
                setUpdateMessage(QStringLiteral("发布版本号超出支持范围"));
                return;
            }
        }
        const auto versionNumber = [](const QRegularExpressionMatch& match, int offset) {
            return match.captured(offset).toInt();
        };
        const QList<int> remoteVersion{
            versionNumber(remoteMatch, 1), versionNumber(remoteMatch, 2),
            versionNumber(remoteMatch, 3)};
        const QList<int> currentVersion{
            versionNumber(currentMatch, 1), versionNumber(currentMatch, 2),
            versionNumber(currentMatch, 3)};
        updateReleaseUrl_ = QUrl(release.value(QStringLiteral("html_url"))
                                     .toString(QStringLiteral("https://github.com/flyu518/open-vpn/releases")));
        if (!releaseUrl(updateReleaseUrl_)) updateReleaseUrl_ = QUrl("https://github.com/flyu518/open-vpn/releases");
        int bestScore = -1;
        const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
        for (const QJsonValue& value : assets) {
            const QJsonObject asset = value.toObject();
            const QString name = asset.value(QStringLiteral("name")).toString();
            const QString downloadUrl =
                asset.value(QStringLiteral("browser_download_url")).toString();
            const QString lowerName = name.toLower();
            bool platformMatch = false;
            int score = 0;
#ifdef Q_OS_MACOS
            platformMatch = lowerName.endsWith(QStringLiteral(".pkg"));
            if (lowerName.contains(QStringLiteral("mac")) ||
                lowerName.contains(QStringLiteral("darwin")) ||
                lowerName.contains(QStringLiteral("osx"))) {
                score += 2;
            }
#elif defined(Q_OS_WIN)
            platformMatch = lowerName.endsWith(QStringLiteral(".exe")) ||
                            lowerName.endsWith(QStringLiteral(".msi"));
            if (lowerName.contains(QStringLiteral("win")) ||
                lowerName.contains(QStringLiteral("windows"))) {
                score += 2;
            }
#endif
            const auto size = asset.value("size").toDouble();
            const auto digest = QRegularExpression("^sha256:([0-9a-fA-F]{64})$").match(asset.value("digest").toString());
            if (!platformMatch || !releaseUrl(QUrl(downloadUrl), true) ||
                !QRegularExpression("^[A-Za-z0-9][A-Za-z0-9._-]{0,180}$").match(name).hasMatch() ||
                !digest.hasMatch() || !std::isfinite(size) || size <= 0 || size > 512 * 1024 * 1024 || std::floor(size) != size) continue;
#if defined(Q_PROCESSOR_ARM_64)
            if (lowerName.contains("x86_64") || lowerName.contains("amd64") || lowerName.contains("x64")) continue;
            if (lowerName.contains("arm64") || lowerName.contains("aarch64")) score += 4;
#elif defined(Q_PROCESSOR_X86_64)
            if (lowerName.contains("arm64") || lowerName.contains("aarch64")) continue;
            if (lowerName.contains("x86_64") || lowerName.contains("amd64") || lowerName.contains("x64")) score += 4;
#endif
            if (lowerName.contains("universal")) score += 3;
            if (score > bestScore) {
                bestScore = score;
                updateInstallerUrl_ = QUrl(downloadUrl);
                updateInstallerName_ = name;
                updateInstallerSha256_ = QByteArray::fromHex(digest.captured(1).toLatin1());
                updateInstallerSize_ = qint64(size);
            }
        }
        if (std::lexicographical_compare(currentVersion.cbegin(), currentVersion.cend(),
                                         remoteVersion.cbegin(), remoteVersion.cend())) {
            setUpdateState(QStringLiteral("available"));
            const QString suffix = updateInstallerAvailable()
                                       ? QStringLiteral("，可下载：") + updateInstallerName_
                                       : QStringLiteral("，暂无适配且带完整性校验的安装包，请查看发布页");
            setUpdateMessage(QStringLiteral("发现新版本：") + tag + suffix);
        } else {
            setUpdateState(QStringLiteral("upToDate"));
            setUpdateMessage(QStringLiteral("当前已是最新版本（") + appVersion() +
                             QStringLiteral("）"));
        }
}

void AppController::downloadUpdate() {
    if (updateReply_) return;
    if (!updateInstallerAvailable()) {
        emit notice(QStringLiteral("无法自动下载"), QStringLiteral("请先检查更新并选择有完整性校验的安装包"));
        return;
    }
    // The QML updateDialog is the single confirmation boundary. Validate the
    // selected asset here without presenting a second native dialog.

    setUpdateState(QStringLiteral("downloading"));
    setUpdateMessage(QStringLiteral("正在下载安装包…"));
    QNetworkRequest request(updateInstallerUrl_);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("OpenVPN/") +
                          QCoreApplication::applicationVersion());
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = updateNetwork_.get(request);
    reply->setReadBufferSize(64 * 1024);
    updateReply_ = reply;
    auto payload = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, payload] {
        payload->append(reply->readAll());
        if (payload->size() > updateInstallerSize_) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, payload] {
        if (updateReply_ != reply) {
            reply->deleteLater();
            return;
        }
        updateReply_.clear();
        const int statusCode = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        payload->append(reply->readAll());
        const QNetworkReply::NetworkError networkError = reply->error();
        const bool trustedUrl = downloadUrl(reply->url());
        reply->deleteLater();
        if (networkError != QNetworkReply::NoError || statusCode < 200 ||
            statusCode >= 300 || !trustedUrl) {
            setUpdateState(QStringLiteral("error"));
            setUpdateMessage(QStringLiteral("下载安装包失败"));
            return;
        }

        QTemporaryDir directory(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("OpenVPN-update-XXXXXX"));
        if (!directory.isValid()) {
            setUpdateState(QStringLiteral("error"));
            setUpdateMessage(QStringLiteral("无法创建私密下载目录"));
            return;
        }
        const QString filePath = directory.filePath(updateInstallerName_);
        QString error;
        if (!saveUpdatePayload(*payload, filePath, &error)) {
            setUpdateState(QStringLiteral("error"));
            setUpdateMessage(error);
            return;
        }
        directory.setAutoRemove(false);
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(filePath))) {
            setUpdateState(QStringLiteral("error"));
            setUpdateMessage(QStringLiteral("无法打开安装包，请手动打开：") + filePath);
            return;
        }
        setUpdateState(QStringLiteral("available"));
        setUpdateMessage(QStringLiteral("安装包已打开，请按系统提示完成更新"));
    });
}

bool AppController::saveUpdatePayload(const QByteArray& payload, const QString& path, QString* error) const {
    if (error) error->clear();
    if (payload.isEmpty() || payload.size() != updateInstallerSize_ || updateInstallerSha256_.size() != 32 ||
        QCryptographicHash::hash(payload, QCryptographicHash::Sha256) != updateInstallerSha256_) {
        if (error) *error = QStringLiteral("安装包大小或 SHA-256 校验失败，未保存或打开安装包");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
        file.write(payload) != payload.size() || !file.commit()) {
        if (error) *error = QStringLiteral("保存安装包失败：") + file.errorString();
        return false;
    }
    return true;
}

void AppController::openUpdatePage() {
    const QUrl url = releaseUrl(updateReleaseUrl_)
                         ? updateReleaseUrl_
                         : QUrl(QStringLiteral("https://github.com/flyu518/open-vpn/releases"));
    if (!QDesktopServices::openUrl(url)) {
        emit notice(QStringLiteral("无法打开发布页面"), url.toString());
    }
}

void AppController::showSettings() {
    emit settingsRequested();
}

void AppController::showAbout() {
    emit aboutRequested();
}

void AppController::setStartupError(const QString& value) {
    if (startupError_ == value) return;
    startupError_ = value;
    emit startupErrorChanged();
}

bool AppController::readStartupState(QString* error) const {
    if (error) error->clear();
#ifdef Q_OS_WIN
    QSettings settings(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
        QSettings::NativeFormat);
    const auto command = settings.value(QStringLiteral("OpenVPN")).toString();
    if (settings.status() != QSettings::NoError) {
        if (error) *error = QStringLiteral("读取 Windows 开机自启失败");
        return false;
    }
    return command == QStringLiteral("\"") + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + QStringLiteral("\"");
#elif defined(Q_OS_MACOS)
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    const QString path = QDir(home).filePath(
        QStringLiteral("Library/LaunchAgents/com.openvpn.desktop.plist"));
    return startupFileEnabled(path, QCoreApplication::applicationFilePath(), error);
#else
    Q_UNUSED(error);
    return false;
#endif
}

bool AppController::writeStartupState(bool enabled, QString* error) const {
    if (error) error->clear();
#ifdef Q_OS_WIN
    QSettings settings(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
        QSettings::NativeFormat);
    if (enabled) {
        const QString executable = QDir::toNativeSeparators(
            QCoreApplication::applicationFilePath());
        settings.setValue(QStringLiteral("OpenVPN"),
                          QStringLiteral("\"") + executable + QStringLiteral("\""));
    } else {
        settings.remove(QStringLiteral("OpenVPN"));
    }
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (error) *error = QStringLiteral("写入 Windows 开机自启失败");
        return false;
    }
    return true;
#elif defined(Q_OS_MACOS)
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    const QString path = QDir(home).filePath(
        QStringLiteral("Library/LaunchAgents/com.openvpn.desktop.plist"));
    // LaunchAgents are loaded at login. Never bootout/bootstrap this label:
    // the current UI may itself have been started by that job.
    return writeStartupFile(path, QCoreApplication::applicationFilePath(), enabled, error);
#else
    if (error) *error = QStringLiteral("当前平台不支持开机自启");
    return false;
#endif
}

bool AppController::startupFileEnabled(const QString& path, const QString& executable, QString* error) {
    if (error) error->clear();
    QFile file(path);
    if (!file.exists()) return false;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024) {
        if (error) *error = QStringLiteral("无法读取 macOS 开机自启配置");
        return false;
    }
    QXmlStreamReader xml(file.readAll());
    QString label, program;
    bool runAtLoad = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QStringLiteral("key")) continue;
        const auto key = xml.readElementText();
        if (!xml.readNextStartElement()) break;
        if (key == "Label" && xml.name() == QStringLiteral("string")) label = xml.readElementText();
        else if (key == "ProgramArguments" && xml.name() == QStringLiteral("array")) {
            if (xml.readNextStartElement() && xml.name() == QStringLiteral("string")) program = xml.readElementText();
        } else if (key == "RunAtLoad") runAtLoad = xml.name() == QStringLiteral("true");
    }
    if (xml.hasError() || label != "com.openvpn.desktop" || program != executable) {
        if (error) *error = QStringLiteral("macOS 开机自启配置无效或指向其他应用路径");
        return false;
    }
    return runAtLoad;
}

QByteArray AppController::startupPlist(const QString& executable) {
    const auto escapeXml = [](QString value) {
        return value.replace('&', QStringLiteral("&amp;"))
            .replace('<', QStringLiteral("&lt;"))
            .replace('>', QStringLiteral("&gt;"))
            .replace('"', QStringLiteral("&quot;"))
            .replace('\'', QStringLiteral("&apos;"));
    };
    const QString plist = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>"
        "<key>Label</key><string>com.openvpn.desktop</string>"
        "<key>ProgramArguments</key><array><string>%1</string></array>"
        "<key>RunAtLoad</key><true/></dict></plist>\n")
                                .arg(escapeXml(executable));
    return plist.toUtf8();
}

bool AppController::writeStartupFile(const QString& path, const QString& executable,
                                      bool enabled, QString* error) {
    if (error) error->clear();
    if (!enabled) {
        if (!QFileInfo::exists(path)) return true;
        if (QFile::remove(path)) return true;
        if (error) *error = QStringLiteral("删除 macOS 开机自启文件失败");
        return false;
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) *error = QStringLiteral("创建 macOS 开机自启目录失败");
        return false;
    }
    QSaveFile file(path);
    const QByteArray data = startupPlist(executable);
    if (!file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
        file.write(data) != data.size() || !file.commit()) {
        if (error) *error = QStringLiteral("保存 macOS 开机自启文件失败：") +
                             file.errorString();
        return false;
    }
    return true;
}

void AppController::setUpdateState(const QString& value) {
    if (updateState_ == value) return;
    updateState_ = value;
    emit updateStateChanged();
}

void AppController::setUpdateMessage(const QString& value) {
    if (updateMessage_ == value) return;
    updateMessage_ = value;
    emit updateMessageChanged();
}

void AppController::quitApplication(bool showConfirmation) {
    if (quitting_) return;
    if (showConfirmation) {
        if (quitConfirmationPending_) return;
        quitConfirmationPending_ = true;
        emit quitConfirmationPendingChanged();
        const auto sessions = sessionModel_.sessions();
        emit quitConfirmationRequired(std::count_if(sessions.begin(), sessions.end(), activeProfile));
        return;
    }
    cancelQuitApplication();
    quitting_ = true;
    emit quittingChanged();
    emit quitRequested();
    QCoreApplication::quit();
}

void AppController::cancelQuitApplication() {
    if (!quitConfirmationPending_) return;
    quitConfirmationPending_ = false;
    emit quitConfirmationPendingChanged();
}

void AppController::onEngineEvent(const QJsonObject& event) {
    QJsonObject session = event.value(QStringLiteral("session")).toObject();
    const QString sessionId = event.value(QStringLiteral("sessionId")).toString(
        session.value(QStringLiteral("sessionId")).toString());
    if (sessionId.isEmpty() || (session.contains("sessionId") && session.value("sessionId").toString() != sessionId)) return;
    session.insert("sessionId", sessionId);
    if (!session.contains(QStringLiteral("state"))) {
        const QString state = stateForEvent(event.value(QStringLiteral("event")).toString());
        if (!state.isEmpty()) session.insert(QStringLiteral("state"), state);
    }
    if (!SessionListModel::validSession(session)) {
        setServiceError(QStringLiteral("OpenVPN 服务返回了无效会话事件"));
        return;
    }
    if ((disconnecting_.contains(sessionId) || cancelledConnections_.contains(sessionId)) && session.value("state") != "disconnected") return;
    ++revisions_[sessionId];
    const QString message = event.value("message").toString(session.value("error").toString());
    if (session.value("state") == "failed" && !message.isEmpty()) session.insert("error", message);
    applyRuntimeSession(session, event.value("event") == "CONNECTED");
    if (session.value("state") == "failed") {
        needsCleanup_.insert(sessionId);
        if (isCredentialFailure(event.value("event").toString()) || isCredentialFailure(message)) {
            authenticationRejected_.insert(sessionId);
            if (!credentialCancelled_.contains(sessionId)) requestCredentials(sessionModel_.session(sessionId), message);
        }
    }
}

void AppController::applyRuntimeSession(const QJsonObject& session, bool connectedEvent) {
    const QString id = session.value("sessionId").toString();
    if (id.isEmpty()) return;
    const auto previous = sessionModel_.session(id);
    auto current = previous;
    current.insert("sessionId", id);
    // A daemon snapshot is runtime data, never an authority for profile identity,
    // secrets, custom names, routing policy or persisted connection history.
    for (const auto& key : {"state", "remoteHost", "downloadBytes", "uploadBytes", "durationSeconds", "error", "warnings", "routeConflicts"}) {
        if (session.contains(key)) current.insert(key, session.value(key));
    }
    if (!storedProfile(previous) && session.value("name").isString()) current.insert("name", session.value("name"));
    const QString before = previous.value("state").toString();
    const QString after = current.value("state").toString();
    const bool connected = after == "connected" && before != "connected";
    const bool disconnected = after == "disconnected" && before != "disconnected";
    // Discovering an existing daemon session is not a new connection. Record
    // only a CONNECTED event or completion of an attempt started by this UI.
    // Keep this separate from runtime state: a snapshot may arrive before the
    // event, and the eventual event/response must not count it twice.
    const bool recordConnection = after == "connected" &&
        (before != "connected" || !countedConnections_.contains(id)) &&
        (connectedEvent || pendingConnectionHistory_.contains(id));
    if (recordConnection) {
        current.insert("connectionCount", previous.value("connectionCount").toInt() + 1);
        current.insert("lastConnectedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    } else if (disconnected) {
        current.insert("lastDisconnectedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    }
    if (connected) current.insert("error", "");
    if (!sessionModel_.updateSession(defaultRuntimeState(current))) return;
    if (before != "connected" || after != "connected") countedConnections_.remove(id);
    if (recordConnection) countedConnections_.insert(id);
    if (after == "connected" || after == "disconnected" || after == "failed") pendingConnectionHistory_.remove(id);
    if (after == "connected") {
        credentialPrompted_.remove(id);
        credentialCancelled_.remove(id);
        authenticationRejected_.remove(id);
    }
    if ((recordConnection || disconnected) && storedProfile(current)) saveProfiles();
}

void AppController::onEngineAvailabilityChanged(bool available) {
    if (serviceAvailable_ == available) return;
    serviceAvailable_ = available;
    emit serviceAvailableChanged();
}

void AppController::onEngineError(const QString& message) {
    setServiceError(message);
}

void AppController::pollSnapshot() {
    if (engine_.available()) {
        requestSnapshot();
    } else {
        refreshService();
    }
}

void AppController::loadProfiles() {
    if (profilesLoaded_) return;
    QString error;
    const QList<QJsonObject> profiles = profileStore_.load(&error);
    setStorageError(error);
    if (!error.isEmpty() && profiles.isEmpty()) return;
    profilesLoaded_ = true;
    QList<QJsonObject> runtime;
    for (QJsonObject profile : profiles) runtime.append(defaultRuntimeState(profile));
    sessionModel_.setSessions(runtime);
}

void AppController::saveProfiles() {
    QList<QJsonObject> profiles;
    for (const auto& profile : sessionModel_.sessions()) if (storedProfile(profile)) profiles.append(profile);
    // Runtime events must never create an empty index for external sessions.
    if (profiles.isEmpty()) return;
    QString error;
    if (!profileStore_.save(profiles, &error)) setStorageError(error);
    else setStorageError({});
}

bool AppController::commitProfiles(QList<QJsonObject> profiles) {
    QList<QJsonObject> stored;
    int priority = 1;
    for (auto& profile : profiles) {
        if (!storedProfile(profile)) continue;
        QJsonObject strategy = routeStrategyOrDefault(profile);
        strategy.insert("priority", priority++);
        profile.insert("routeStrategy", strategy);
        stored.append(profile);
    }
    QString error;
    const auto currentProfiles = sessionModel_.sessions();
    const bool hadStoredProfiles = std::any_of(currentProfiles.begin(), currentProfiles.end(), storedProfile);
    if ((!stored.isEmpty() || hadStoredProfiles) && !profileStore_.save(stored, &error)) {
        setStorageError(error);
        emit notice(QStringLiteral("保存失败"), error);
        return false;
    }
    setStorageError({});
    if (!stored.isEmpty() || hadStoredProfiles) profilesLoaded_ = true;
    // Preserve delegates while reordering; a model reset during a drag can
    // destroy the item delivering the pointer event.
    QSet<QString> ids;
    for (const auto& profile : profiles) ids.insert(profile.value("sessionId").toString());
    for (const auto& current : sessionModel_.sessions()) {
        const auto id = current.value("sessionId").toString();
        if (!ids.contains(id)) sessionModel_.removeSession(id);
    }
    for (const auto& profile : profiles) sessionModel_.updateSession(profile);
    for (int i = 0; i < profiles.size(); ++i) {
        const auto id = profiles.at(i).value("sessionId").toString();
        const auto atRow = sessionModel_.sessions().at(i).value("sessionId").toString();
        if (id != atRow) sessionModel_.moveSession(id, atRow);
    }
    return true;
}

bool AppController::updateProfile(const QString& sessionId, const QJsonObject& fields) {
    auto profiles = sessionModel_.sessions();
    for (auto& profile : profiles) {
        if (profile.value("sessionId").toString() != sessionId || !storedProfile(profile)) continue;
        for (auto it = fields.begin(); it != fields.end(); ++it) profile.insert(it.key(), it.value());
        return commitProfiles(profiles);
    }
    return false;
}

void AppController::requestSnapshot() {
    if (snapshotPending_) return;
    snapshotPending_ = true;
    const auto revisions = revisions_;
    engine_.request(
        QJsonObject{{QStringLiteral("command"), QStringLiteral("snapshot")}},
        [this, revisions](const QJsonObject& value) {
            snapshotPending_ = false;
            if (!value.value(QStringLiteral("ok")).toBool()) {
                setServiceError(value.value(QStringLiteral("message")).toString(
                    QStringLiteral("无法读取 OpenVPN 连接状态")));
                return;
            }
            if (!value.value("sessions").isArray()) {
                setServiceError(QStringLiteral("OpenVPN 服务返回了无效会话快照"));
                return;
            }
            setServiceError({});
            applySnapshot(value.value(QStringLiteral("sessions")).toArray(), revisions);
            const auto now = QDateTime::currentMSecsSinceEpoch();
            for (const auto& session : sessionModel_.sessions()) {
                if (!storedProfile(session)) continue;
                const auto id = session.value("sessionId").toString();
                if (session.value("state").toString() != "connected") { trafficHistory_.stop(id); continue; }
                trafficHistory_.record(id, now, session.value("downloadBytes").toInteger(),
                                       session.value("uploadBytes").toInteger(), session.value("durationSeconds").toInteger());
            }
            if (now - trafficFlushAt_ >= 30000) { trafficHistory_.flush(); trafficFlushAt_ = now; }
            emit trafficHistoryChanged();
        });
}

void AppController::applySnapshot(const QJsonArray& sessions, const QHash<QString, quint64>& revisions) {
    QHash<QString, QJsonObject> active;
    for (const QJsonValue& value : sessions) {
        const QJsonObject session = value.toObject();
        const QString id = session.value(QStringLiteral("sessionId")).toString();
        if (!SessionListModel::validSession(session) || active.contains(id) || !session.contains("state")) {
            setServiceError(QStringLiteral("OpenVPN 服务返回了无效会话快照"));
            return;
        }
        active.insert(id, session);
    }

    const QList<QJsonObject> current = sessionModel_.sessions();
    QSet<QString> seen;
    for (QJsonObject profile : current) {
        const QString id = profile.value(QStringLiteral("sessionId")).toString();
        if (id.isEmpty()) continue;
        seen.insert(id);
        if (revisions_.value(id) != revisions.value(id) || connecting_.contains(id) || disconnecting_.contains(id)) continue;
        const auto activeIt = active.constFind(id);
        if (activeIt != active.constEnd()) {
            cancelledConnections_.remove(id);
            applyRuntimeSession(*activeIt);
            continue;
        } else {
            if (!storedProfile(profile)) {
                sessionModel_.removeSession(id);
                countedConnections_.remove(id);
                pendingConnectionHistory_.remove(id);
                continue;
            }
            profile.insert(QStringLiteral("state"), QStringLiteral("disconnected"));
            profile.insert(QStringLiteral("error"), QString{});
            profile.insert(QStringLiteral("durationSeconds"), 0);
            profile.insert(QStringLiteral("downloadBytes"), 0);
            profile.insert(QStringLiteral("uploadBytes"), 0);
        }
        applyRuntimeSession(profile);
    }
    for (auto it = active.constBegin(); it != active.constEnd(); ++it) {
        if (seen.contains(it.key()) || revisions_.value(it.key()) != revisions.value(it.key())) continue;
        applyRuntimeSession(it.value());
    }
}

void AppController::connectSession(const QJsonObject& profile,
                                   const QString& username,
                                   const QString& password,
                                   bool remember) {
    const QString sessionId = profile.value(QStringLiteral("sessionId")).toString(
        profile.value(QStringLiteral("profileId")).toString());
    if (sessionId.isEmpty()) return;
    if (profile.value("profile").toString().trimmed().isEmpty()) {
        emit notice(QStringLiteral("连接失败"), QStringLiteral("配置内容不可用，请重新导入配置"));
        return;
    }
    const auto operation = ++operations_[sessionId];
    ++revisions_[sessionId];
    cancelledConnections_.remove(sessionId);
    connecting_.insert(sessionId);
    pendingConnectionHistory_.insert(sessionId);
    countedConnections_.remove(sessionId);
    credentialCancelled_.remove(sessionId);
    credentialPrompted_.remove(sessionId);
    sessionModel_.updateSession(
        QJsonObject{{QStringLiteral("sessionId"), sessionId},
                    {QStringLiteral("state"), QStringLiteral("connecting")},
                    {QStringLiteral("error"), QString{}},
                    {QStringLiteral("warnings"), QJsonArray{}},
                    {QStringLiteral("routeConflicts"), QJsonArray{}},
                    {QStringLiteral("downloadBytes"), 0},
                    {QStringLiteral("uploadBytes"), 0},
                    {QStringLiteral("durationSeconds"), 0}});

    const QJsonObject credentials{
        {QStringLiteral("username"), username},
        {QStringLiteral("password"), password},
        {QStringLiteral("response"), QString{}},
        {QStringLiteral("dynamicChallengeCookie"), QString{}},
    };
    const QJsonObject request{
        {QStringLiteral("command"), QStringLiteral("connect")},
        {QStringLiteral("sessionId"), sessionId},
        {QStringLiteral("profileId"),
         profile.value(QStringLiteral("profileId")).toString(sessionId)},
        {QStringLiteral("name"), profile.value(QStringLiteral("name")).toString()},
        {QStringLiteral("profile"), profile.value(QStringLiteral("profile")).toString()},
        {QStringLiteral("credentials"), credentials},
        {QStringLiteral("rememberCredentials"), remember},
        {QStringLiteral("routeStrategy"), routeStrategyOrDefault(profile)},
    };
    const auto send = [this, request, sessionId, operation] {
      if (operations_.value(sessionId) != operation) return;
      const auto revision = revisions_.value(sessionId);
      engine_.request(request, [this, sessionId, operation, revision](const QJsonObject& value) {
        if (operations_.value(sessionId) != operation) return;
        connecting_.remove(sessionId);
        needsCleanup_.insert(sessionId);
        // A sessionEvent can precede the response to connect. Its newer runtime
        // state must not be replaced by the response's earlier snapshot.
        if (revisions_.value(sessionId) != revision) return;
        ++revisions_[sessionId];
        if (!value.value(QStringLiteral("ok")).toBool()) {
            pendingConnectionHistory_.remove(sessionId);
            const QString message = value.value(QStringLiteral("message")).toString(
                QStringLiteral("连接请求失败"));
            sessionModel_.updateSession(
                QJsonObject{{QStringLiteral("sessionId"), sessionId},
                            {QStringLiteral("state"), QStringLiteral("failed")},
                            {QStringLiteral("error"), message}});
            if (isCredentialFailure(message)) {
                authenticationRejected_.insert(sessionId);
                requestCredentials(sessionModel_.session(sessionId), message);
            } else {
                emit notice(QStringLiteral("连接失败"), message);
            }
            return;
        }
        const QJsonObject session = value.value(QStringLiteral("session")).toObject();
        if (!session.isEmpty()) {
            if (session.value("sessionId").toString() != sessionId) {
                pendingConnectionHistory_.remove(sessionId);
                setServiceError(QStringLiteral("连接响应的会话标识不匹配"));
                return;
            }
            applyRuntimeSession(session);
        }
        setServiceError({});
      });
    };
    if (needsCleanup_.contains(sessionId)) {
        engine_.request(QJsonObject{{"command", "disconnect"}, {"sessionId", sessionId}},
                        [this, sessionId, operation, send](const auto& response) {
            if (operations_.value(sessionId) != operation) return;
            if (!response.value("ok").toBool()) {
                connecting_.remove(sessionId);
                pendingConnectionHistory_.remove(sessionId);
                sessionModel_.updateSession(QJsonObject{{"sessionId", sessionId}, {"state", "failed"}, {"error", response.value("message")}});
                emit notice(QStringLiteral("重试失败"), QStringLiteral("无法清理上一次连接，请稍后重试"));
                return;
            }
            needsCleanup_.remove(sessionId);
            send();
        });
    } else {
        send();
    }
}

void AppController::requestCredentials(const QJsonObject& profile,
                                       const QString& reason) {
    const QString id = profile.value(QStringLiteral("sessionId")).toString(
        profile.value(QStringLiteral("profileId")).toString());
    if (id.isEmpty() || credentialPrompted_.contains(id) || credentialCancelled_.contains(id)) return;
    credentialPrompted_.insert(id);
    emit credentialsRequired(id,
                             profile.value(QStringLiteral("username")).toString(),
                             reason.isEmpty() ? QStringLiteral("请输入此配置的 VPN 凭据")
                                              : reason);
}

void AppController::setServiceError(const QString& value) {
    if (serviceError_ == value) return;
    serviceError_ = value;
    emit serviceErrorChanged();
}

void AppController::setStorageError(const QString& value) {
    if (storageError_ == value) return;
    storageError_ = value;
    emit storageErrorChanged();
}

bool AppController::isCredentialFailure(const QString& message) {
    const QString lower = message.toLower();
    return lower.contains(QStringLiteral("auth_failed")) ||
           lower.contains(QStringLiteral("authentication failed")) ||
           lower.contains(QStringLiteral("invalid credentials")) ||
           message.contains(QStringLiteral("认证失败")) ||
           message.contains(QStringLiteral("用户名或密码错误")) ||
           message.contains(QStringLiteral("没有对应凭据"));
}

QJsonObject AppController::routeStrategyOrDefault(const QJsonObject& profile) {
    const QJsonObject value = profile.value(QStringLiteral("routeStrategy")).toObject();
    if (!value.isEmpty()) return value;
    return QJsonObject{
        {QStringLiteral("conflictMode"), QStringLiteral("compatible")},
        {QStringLiteral("priority"), 0},
        {QStringLiteral("rules"), QJsonArray{}},
    };
}
