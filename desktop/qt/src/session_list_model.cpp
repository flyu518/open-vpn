#include "session_list_model.h"

#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <limits>

SessionListModel::SessionListModel(QObject* parent)
    : QAbstractListModel(parent) {}

int SessionListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : sessions_.size();
}

QVariant SessionListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= sessions_.size()) {
        return {};
    }

    const QJsonObject& session = sessions_.at(index.row());
    const QString state = session.value(QStringLiteral("state")).toString();
    switch (role) {
    case IdRole:
        return session.value(QStringLiteral("sessionId")).toString();
    case NameRole:
        return session.value(QStringLiteral("name")).toString();
    case RemoteHostRole:
        return session.value(QStringLiteral("remoteHost")).toString();
    case StateRole:
        return state;
    case StateTextRole:
        return stateText(state);
    case ConnectedRole:
        return isConnected(state);
    case ConnectingRole:
        return isConnecting(state);
    case FailedRole:
        return isFailed(state);
    case ErrorRole:
        return session.value(QStringLiteral("error")).toString();
    case WarningsRole:
        return warnings(session.value(QStringLiteral("warnings")));
    case RouteConflictsRole:
        return session.value("routeConflicts").toArray().toVariantList();
    case DownloadBytesRole:
        return session.value(QStringLiteral("downloadBytes")).toInteger();
    case UploadBytesRole:
        return session.value(QStringLiteral("uploadBytes")).toInteger();
    case DurationSecondsRole:
        return session.value(QStringLiteral("durationSeconds")).toInteger();
    case ProfileIdRole:
        return session.value(QStringLiteral("profileId")).toString();
    case ProfilePathRole:
        return session.value(QStringLiteral("profilePath")).toString();
    case UsernameRole:
        return session.value(QStringLiteral("username")).toString();
    case Qt::DisplayRole:
        return session.value(QStringLiteral("name")).toString();
    default:
        return {};
    }
}

QHash<int, QByteArray> SessionListModel::roleNames() const {
    return {
        {IdRole, "sessionId"},
        {NameRole, "name"},
        {RemoteHostRole, "remoteHost"},
        {StateRole, "state"},
        {StateTextRole, "stateText"},
        {ConnectedRole, "connected"},
        {ConnectingRole, "connecting"},
        {FailedRole, "failed"},
        {ErrorRole, "error"},
        {WarningsRole, "warnings"},
        {RouteConflictsRole, "routeConflicts"},
        {DownloadBytesRole, "downloadBytes"},
        {UploadBytesRole, "uploadBytes"},
        {DurationSecondsRole, "durationSeconds"},
        {ProfileIdRole, "profileId"},
        {ProfilePathRole, "profilePath"},
        {UsernameRole, "username"},
    };
}

void SessionListModel::setSessions(const QList<QJsonObject>& sessions) {
    beginResetModel();
    sessions_.clear();
    QSet<QString> ids;
    for (const auto& session : sessions) {
        const auto id = session.value("sessionId").toString();
        if (!validSession(session) || ids.contains(id)) continue;
        ids.insert(id);
        sessions_.append(session);
    }
    endResetModel();
    emit countChanged();
}

bool SessionListModel::validSession(const QJsonObject& session) {
    if (session.value("sessionId").toString().isEmpty()) return false;
    if (session.contains("state") &&
        !QSet<QString>{"connected", "connecting", "disconnected", "disconnecting", "failed"}
             .contains(session.value("state").toString())) return false;
    for (const auto& key : {"downloadBytes", "uploadBytes", "durationSeconds"}) {
        if (!session.contains(key)) continue;
        const auto value = session.value(key);
        const double number = value.toDouble(-1);
        if (!value.isDouble() || !std::isfinite(number) || number < 0 ||
            std::floor(number) != number || number >= double(std::numeric_limits<qint64>::max())) return false;
    }
    for (const auto& key : {"name", "remoteHost", "error"}) {
        if (session.contains(key) && !session.value(key).isString()) return false;
    }
    if (session.contains("warnings")) {
        if (!session.value("warnings").isArray()) return false;
        for (const auto& warning : session.value("warnings").toArray()) if (!warning.isString()) return false;
    }
    if (session.contains("routeConflicts")) {
        const auto value = session.value("routeConflicts");
        if (!value.isArray() || value.toArray().size() > 32) return false;
        for (const auto& item : value.toArray()) {
            if (!item.isObject()) return false;
            const auto conflict = item.toObject();
            for (const auto& key : {"candidateRoute", "activeRoute", "peerSessionId", "peerName", "action"})
                if (!conflict.value(key).isString() || conflict.value(key).toString().size() > 256) return false;
            if (conflict.value("candidateRoute").toString().isEmpty() || conflict.value("activeRoute").toString().isEmpty() ||
                (conflict.value("action") != "skipped" && conflict.value("action") != "blocked")) return false;
            if (conflict.contains("external") && !conflict.value("external").isBool()) return false;
            if (conflict.contains("peerKey") && (!conflict.value("peerKey").isString() || conflict.value("peerKey").toString().size() > 256)) return false;
        }
    }
    return true;
}

bool SessionListModel::updateSession(const QJsonObject& session) {
    if (!validSession(session)) return false;
    const QString sessionId = session.value("sessionId").toString();

    for (int row = 0; row < sessions_.size(); ++row) {
        if (sessions_.at(row).value(QStringLiteral("sessionId")).toString() !=
            sessionId) {
            continue;
        }
        QJsonObject merged = sessions_.at(row);
        for (auto it = session.constBegin(); it != session.constEnd(); ++it) {
            merged.insert(it.key(), it.value());
        }
        sessions_[row] = merged;
        const QModelIndex modelIndex = index(row, 0);
        emit dataChanged(modelIndex, modelIndex, {});
        return true;
    }

    beginInsertRows({}, sessions_.size(), sessions_.size());
    sessions_.append(session);
    endInsertRows();
    emit countChanged();
    return true;
}

bool SessionListModel::removeSession(const QString& sessionId) {
    for (int row = 0; row < sessions_.size(); ++row) {
        if (sessions_.at(row).value(QStringLiteral("sessionId")).toString() !=
            sessionId) {
            continue;
        }
        beginRemoveRows({}, row, row);
        sessions_.removeAt(row);
        endRemoveRows();
        emit countChanged();
        emit orderChanged();
        return true;
    }
    return false;
}

bool SessionListModel::moveSession(const QString& sourceId,
                                   const QString& targetId) {
    int sourceRow = -1;
    int targetRow = -1;
    for (int row = 0; row < sessions_.size(); ++row) {
        const QString id =
            sessions_.at(row).value(QStringLiteral("sessionId")).toString();
        if (id == sourceId) sourceRow = row;
        if (id == targetId) targetRow = row;
    }
    if (sourceRow < 0 || targetRow < 0 || sourceRow == targetRow) return false;

    const int destinationRow = targetRow > sourceRow ? targetRow + 1 : targetRow;
    if (!beginMoveRows({}, sourceRow, sourceRow, {}, destinationRow)) {
        return false;
    }
    const QJsonObject moved = sessions_.takeAt(sourceRow);
    sessions_.insert(targetRow, moved);
    endMoveRows();
    emit orderChanged();
    return true;
}

QList<QJsonObject> SessionListModel::sessions() const {
    return sessions_;
}

QJsonObject SessionListModel::session(const QString& sessionId) const {
    for (const QJsonObject& value : sessions_) {
        if (value.value(QStringLiteral("sessionId")).toString() == sessionId) {
            return value;
        }
    }
    return {};
}

QString SessionListModel::stateText(const QString& state) {
    if (state == QStringLiteral("connected")) return QStringLiteral("已连接");
    if (state == QStringLiteral("connecting")) return QStringLiteral("正在连接");
    if (state == QStringLiteral("disconnecting")) return QStringLiteral("正在断开");
    if (state == QStringLiteral("failed")) return QStringLiteral("连接失败");
    return QStringLiteral("未连接");
}

bool SessionListModel::isConnected(const QString& state) {
    return state == QStringLiteral("connected");
}

bool SessionListModel::isConnecting(const QString& state) {
    return state == QStringLiteral("connecting") ||
           state == QStringLiteral("disconnecting");
}

bool SessionListModel::isFailed(const QString& state) {
    return state == QStringLiteral("failed");
}

QStringList SessionListModel::warnings(const QJsonValue& value) {
    QStringList result;
    const QJsonArray values = value.toArray();
    QSet<QString> seen;
    for (const QJsonValue& item : values) {
        const QString warning = item.toString().trimmed();
        if (warning.isEmpty() || seen.contains(warning)) continue;
        seen.insert(warning);
        result.append(warning.size() > 320
                          ? QStringLiteral("检测到 VPN 路由提示，请查看路由策略。")
                          : warning);
    }
    return result;
}
