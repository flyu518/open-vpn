#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QList>
#include <QString>

class SessionListModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        RemoteHostRole,
        StateRole,
        StateTextRole,
        ConnectedRole,
        ConnectingRole,
        FailedRole,
        ErrorRole,
        WarningsRole,
        RouteConflictsRole,
        DownloadBytesRole,
        UploadBytesRole,
        DurationSecondsRole,
        ProfileIdRole,
        ProfilePathRole,
        UsernameRole,
    };
    Q_ENUM(Role)

    explicit SessionListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setSessions(const QList<QJsonObject>& sessions);
    bool updateSession(const QJsonObject& session);
    bool removeSession(const QString& sessionId);
    bool moveSession(const QString& sourceId, const QString& targetId);
    QList<QJsonObject> sessions() const;
    QJsonObject session(const QString& sessionId) const;
    static bool validSession(const QJsonObject& session);

signals:
    void countChanged();
    void orderChanged();

private:
    static QString stateText(const QString& state);
    static bool isConnected(const QString& state);
    static bool isConnecting(const QString& state);
    static bool isFailed(const QString& state);
    static QStringList warnings(const QJsonValue& value);

    QList<QJsonObject> sessions_;
};
