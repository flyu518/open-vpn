#include "engine_client.h"

#include <QJsonDocument>
#include <QSignalBlocker>

#include <utility>

EngineClient::EngineClient(QString socketName,
                           QObject* parent,
                           int responseTimeoutMs)
    : QObject(parent),
      socketName_(std::move(socketName)),
      responseTimeoutMs_(qMax(1, responseTimeoutMs)) {
    socket_.setReadBufferSize(1024 * 1024 + 1);
    responseTimer_.setSingleShot(true);
    connect(&responseTimer_, &QTimer::timeout, this,
            &EngineClient::onResponseTimeout);
    connect(&socket_, &QLocalSocket::connected, this, &EngineClient::onConnected);
    connect(&socket_, &QLocalSocket::readyRead, this, &EngineClient::onReadyRead);
    connect(&socket_, &QLocalSocket::disconnected, this,
            &EngineClient::onDisconnected);
    connect(&socket_, &QLocalSocket::errorOccurred, this,
            &EngineClient::onSocketError);
}

EngineClient::~EngineClient() {
    // Socket destruction can emit disconnected after the callback queues have
    // already been destroyed. No callbacks may run during owner teardown.
    responseTimer_.stop();
    socket_.disconnect(this);
    socket_.abort();
}

bool EngineClient::available() const {
    return available_ && socket_.state() == QLocalSocket::ConnectedState;
}

QString EngineClient::socketName() const {
    return socketName_;
}

void EngineClient::request(const QJsonObject& request,
                           ResponseCallback callback) {
    outgoing_.append(PendingRequest{request, std::move(callback), QDeadlineTimer(responseTimeoutMs_)});
    if (resetting_) return;
    ensureConnected();
    flushRequests();
    armTimer();
}

void EngineClient::onConnected() {
    setAvailable(true);
    flushRequests();
}

void EngineClient::onReadyRead() {
    const auto generation = generation_;
    input_.append(socket_.readAll());
    for (;;) {
        const qsizetype newline = input_.indexOf('\n');
        if (newline > 1024 * 1024 || (newline < 0 && input_.size() > 1024 * 1024)) {
            failRequests(QStringLiteral("OpenVPN 服务数据超过大小限制"));
            return;
        }
        if (newline < 0) return;

        const QByteArray line = input_.left(newline).trimmed();
        input_.remove(0, newline + 1);
        if (line.isEmpty()) continue;

        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            failRequests(QStringLiteral("OpenVPN 服务返回了无效数据"));
            return;
        }
        const QJsonObject value = document.object();
        if (value.value(QStringLiteral("type")).toString() ==
            QStringLiteral("sessionEvent")) {
            const QString id = value.value("sessionId").toString();
            const auto session = value.value("session").toObject();
            if (id.isEmpty() || (value.contains("session") && !value.value("session").isObject()) ||
                (session.contains("sessionId") && session.value("sessionId").toString() != id)) {
                failRequests(QStringLiteral("OpenVPN 服务返回了无效会话事件"));
                return;
            }
            emit eventReceived(value);
            if (generation != generation_) return;
            continue;
        }
        if (value.contains("type") || !value.value("ok").isBool() || pendingResponses_.isEmpty()) {
            failRequests(QStringLiteral("OpenVPN 服务返回了无效或未匹配的响应"));
            return;
        }
        ResponseCallback callback = std::move(pendingResponses_.front().callback);
        pendingResponses_.removeFirst();
        armTimer();
        if (callback) callback(value);
        if (generation != generation_) return;
    }
}

void EngineClient::onDisconnected() {
    failRequests(QStringLiteral("OpenVPN 服务连接已关闭"));
}

void EngineClient::onResponseTimeout() {
    failRequests(QStringLiteral("OpenVPN 服务响应超时"));
}

void EngineClient::onSocketError(QLocalSocket::LocalSocketError error) {
    Q_UNUSED(error);
    const QString message = socket_.errorString().isEmpty()
                                ? QStringLiteral("无法连接 OpenVPN 服务")
                                : QStringLiteral("无法连接 OpenVPN 服务：") +
                                      socket_.errorString();
    failRequests(message);
}

void EngineClient::ensureConnected() {
    if (socket_.state() == QLocalSocket::ConnectedState ||
        socket_.state() == QLocalSocket::ConnectingState) {
        return;
    }
    socket_.abort();
    socket_.connectToServer(socketName_);
}

void EngineClient::flushRequests() {
    if (!available()) return;
    while (!outgoing_.isEmpty()) {
        PendingRequest pending = std::move(outgoing_.front());
        outgoing_.removeFirst();
        const QByteArray payload =
            QJsonDocument(pending.request).toJson(QJsonDocument::Compact) + '\n';
        pending.request = {};
        pendingResponses_.append(std::move(pending));
        if (socket_.write(payload) != payload.size()) {
            failRequests(QStringLiteral("无法发送 OpenVPN 服务请求"));
            return;
        }
    }
    socket_.flush();
    armTimer();
}

void EngineClient::failRequests(const QString& message) {
    if (resetting_) return;
    resetting_ = true;
    ++generation_;
    responseTimer_.stop();
    const auto pending = std::exchange(pendingResponses_, {});
    const auto outgoing = std::exchange(outgoing_, {});
    {
        const QSignalBlocker blocker(&socket_);
        socket_.abort();
    }
    input_.clear();
    setAvailable(false);
    emit serviceError(message);
    const QJsonObject failure{
        {QStringLiteral("ok"), false},
        {QStringLiteral("message"), message},
    };
    for (const auto& request : pending) {
        if (request.callback) request.callback(failure);
    }
    for (const PendingRequest& request : outgoing) {
        if (request.callback) request.callback(failure);
    }
    resetting_ = false;
    if (!outgoing_.isEmpty()) {
        // Retries from callbacks use a fresh transport. Old replies cannot
        // cross this boundary because the daemon protocol has no request IDs.
        QTimer::singleShot(0, this, [this] { ensureConnected(); flushRequests(); armTimer(); });
    }
}

void EngineClient::armTimer() {
    if (resetting_) return;
    if (!pendingResponses_.isEmpty()) {
        responseTimer_.start(qMax<qint64>(1, pendingResponses_.first().deadline.remainingTime()));
    } else if (!outgoing_.isEmpty()) {
        responseTimer_.start(qMax<qint64>(1, outgoing_.first().deadline.remainingTime()));
    } else {
        responseTimer_.stop();
    }
}

void EngineClient::setAvailable(bool available) {
    if (available_ == available) return;
    available_ = available;
    emit availabilityChanged(available_);
}
