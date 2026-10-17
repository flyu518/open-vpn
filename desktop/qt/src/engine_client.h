#pragma once

#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QDeadlineTimer>

#include <functional>

class EngineClient final : public QObject {
    Q_OBJECT

public:
    using ResponseCallback = std::function<void(const QJsonObject&)>;

    explicit EngineClient(QString socketName,
                          QObject* parent = nullptr,
                          int responseTimeoutMs = 20000);
    ~EngineClient() override;

    bool available() const;
    QString socketName() const;
    void request(const QJsonObject& request, ResponseCallback callback);

signals:
    void eventReceived(const QJsonObject& event);
    void availabilityChanged(bool available);
    void serviceError(const QString& message);

private slots:
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void onSocketError(QLocalSocket::LocalSocketError error);
    void onResponseTimeout();

private:
    struct PendingRequest {
        QJsonObject request;
        ResponseCallback callback;
        QDeadlineTimer deadline;
    };

    void ensureConnected();
    void flushRequests();
    void failRequests(const QString& message);
    void setAvailable(bool available);
    void armTimer();

    QString socketName_;
    QLocalSocket socket_;
    QByteArray input_;
    QList<PendingRequest> outgoing_;
    QList<PendingRequest> pendingResponses_;
    bool available_ = false;
    int responseTimeoutMs_ = 20000;
    QTimer responseTimer_;
    quint64 generation_ = 0;
    bool resetting_ = false;
};
