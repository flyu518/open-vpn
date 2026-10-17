#pragma once

#include "engine_client.h"
#include "profile_store.h"
#include "session_list_model.h"
#include "traffic_history.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
#include <QVariantList>

class AppController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(SessionListModel* sessionModel READ sessionModel CONSTANT)
    Q_PROPERTY(QString serviceError READ serviceError NOTIFY serviceErrorChanged)
    Q_PROPERTY(QString storageError READ storageError NOTIFY storageErrorChanged)
    Q_PROPERTY(QString storageLocation READ storageLocation CONSTANT)
    Q_PROPERTY(bool serviceAvailable READ serviceAvailable NOTIFY serviceAvailableChanged)
    Q_PROPERTY(bool quitting READ quitting NOTIFY quittingChanged)
    Q_PROPERTY(bool quitConfirmationPending READ quitConfirmationPending NOTIFY quitConfirmationPendingChanged)
    Q_PROPERTY(bool trayAvailable READ trayAvailable NOTIFY trayAvailableChanged)
    Q_PROPERTY(bool startupEnabled READ startupEnabled NOTIFY startupEnabledChanged)
    Q_PROPERTY(bool startupBusy READ startupBusy NOTIFY startupBusyChanged)
    Q_PROPERTY(QString startupError READ startupError NOTIFY startupErrorChanged)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(QString updateState READ updateState NOTIFY updateStateChanged)
    Q_PROPERTY(QString updateMessage READ updateMessage NOTIFY updateMessageChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateStateChanged)
    Q_PROPERTY(bool updateInstallerAvailable READ updateInstallerAvailable NOTIFY updateStateChanged)
    Q_PROPERTY(QString updateInstallerName READ updateInstallerName NOTIFY updateStateChanged)

public:
    explicit AppController(bool trayAvailable = false,
                           QString profileStorePath = {},
                           QString engineSocketName = {},
                           QObject* parent = nullptr);
    ~AppController() override;

    SessionListModel* sessionModel();
    QString serviceError() const;
    QString storageError() const;
    QString storageLocation() const;
    bool serviceAvailable() const;
    bool quitting() const;
    bool quitConfirmationPending() const;
    bool trayAvailable() const;
    void setTrayAvailable(bool available);
    bool startupEnabled() const;
    bool startupBusy() const;
    QString startupError() const;
    QString appVersion() const;
    QString updateState() const;
    QString updateMessage() const;
    bool updateAvailable() const;
    bool updateInstallerAvailable() const;
    QString updateInstallerName() const;

    Q_INVOKABLE void start();
    Q_INVOKABLE void refreshService();
    Q_INVOKABLE void importProfile();
    Q_INVOKABLE bool importProfilePath(const QString& path);
    Q_INVOKABLE void exportProfile(const QString& sessionId);
    Q_INVOKABLE bool renameSession(const QString& sessionId,
                                   const QString& name);
    Q_INVOKABLE void toggleConnection(const QString& sessionId);
    Q_INVOKABLE void submitCredentials(const QString& sessionId,
                                       const QString& username,
                                       const QString& password,
                                       bool remember);
    Q_INVOKABLE void cancelCredentials(const QString& sessionId);
    Q_INVOKABLE void forgetCredentials(const QString& sessionId);
    Q_INVOKABLE QVariantList routeRules(const QString& sessionId) const;
    Q_INVOKABLE bool addRouteRule(const QString& sessionId,
                                  const QString& type,
                                  const QString& pattern,
                                  const QString& action = QStringLiteral("prefer"));
    Q_INVOKABLE bool removeRouteRule(const QString& sessionId, int index);
    Q_INVOKABLE bool removeProfile(const QString& sessionId);
    Q_INVOKABLE bool setConflictMode(const QString& sessionId,
                                     const QString& mode);
    Q_INVOKABLE bool moveSession(const QString& sourceId,
                                 const QString& targetId);
    Q_INVOKABLE QVariantMap detailsForSession(const QString& sessionId) const;
    Q_INVOKABLE QVariantMap trafficForSession(const QString& sessionId, int hours);
    Q_INVOKABLE QVariantMap routeConflictInfo(const QString& sessionId) const;
    Q_INVOKABLE void loadStartupState();
    Q_INVOKABLE void setStartupEnabled(bool enabled);
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void downloadUpdate();
    Q_INVOKABLE void openUpdatePage();
    Q_INVOKABLE void showSettings();
    Q_INVOKABLE void showAbout();
    Q_INVOKABLE void quitApplication(bool showConfirmation = true);
    Q_INVOKABLE void cancelQuitApplication();

signals:
    void serviceErrorChanged();
    void storageErrorChanged();
    void trafficHistoryChanged();
    void serviceAvailableChanged();
    void trayAvailableChanged();
    void quittingChanged();
    void quitConfirmationPendingChanged();
    void quitConfirmationRequired(int activeConnections);
    void startupEnabledChanged();
    void startupBusyChanged();
    void startupErrorChanged();
    void updateStateChanged();
    void updateMessageChanged();
    void credentialsRequired(const QString& sessionId,
                             const QString& username,
                             const QString& reason);
    void notice(const QString& title, const QString& message);
    void settingsRequested();
    void aboutRequested();
    void quitRequested();

private slots:
    void onEngineEvent(const QJsonObject& event);
    void onEngineAvailabilityChanged(bool available);
    void onEngineError(const QString& message);
    void pollSnapshot();

private:
    friend class AppControllerTest;
    void loadProfiles();
    void saveProfiles();
    bool commitProfiles(QList<QJsonObject> profiles);
    bool updateProfile(const QString& sessionId, const QJsonObject& fields);
    void requestSnapshot();
    void applySnapshot(const QJsonArray& sessions, const QHash<QString, quint64>& revisions = {});
    void applyRuntimeSession(const QJsonObject& session, bool connectedEvent = false);
    void connectSession(const QJsonObject& profile,
                        const QString& username = {},
                        const QString& password = {},
                        bool remember = false);
    void requestCredentials(const QJsonObject& profile, const QString& reason);
    void setStartupError(const QString& value);
    bool readStartupState(QString* error) const;
    bool writeStartupState(bool enabled, QString* error) const;
    void setUpdateState(const QString& value);
    void applyReleaseResponse(const QByteArray& payload, int statusCode,
                              QNetworkReply::NetworkError networkError);
    bool saveUpdatePayload(const QByteArray& payload, const QString& path, QString* error) const;
    static QByteArray startupPlist(const QString& executable);
    static bool writeStartupFile(const QString& path, const QString& executable,
                                 bool enabled, QString* error);
    static bool startupFileEnabled(const QString& path, const QString& executable, QString* error);
    void setUpdateMessage(const QString& value);
    void setServiceError(const QString& value);
    void setStorageError(const QString& value);
    static bool isCredentialFailure(const QString& message);
    static QJsonObject routeStrategyOrDefault(const QJsonObject& profile);

    SessionListModel sessionModel_;
    ProfileStore profileStore_;
    TrafficHistory trafficHistory_;
    qint64 trafficFlushAt_ = 0;
    EngineClient engine_;
    QTimer snapshotTimer_;
    QString serviceError_;
    QString storageError_;
    bool serviceAvailable_ = false;
    bool quitting_ = false;
    bool quitConfirmationPending_ = false;
    bool trayAvailable_ = false;
    bool startupEnabled_ = false;
    bool startupBusy_ = false;
    QString startupError_;
    QString updateState_ = QStringLiteral("idle");
    QString updateMessage_;
    QNetworkAccessManager updateNetwork_;
    QPointer<QNetworkReply> updateReply_;
    QUrl updateReleaseUrl_;
    QUrl updateInstallerUrl_;
    QString updateInstallerName_;
    QByteArray updateInstallerSha256_;
    qint64 updateInstallerSize_ = 0;
    QSet<QString> credentialPrompted_;
    QSet<QString> credentialCancelled_;
    QSet<QString> authenticationRejected_;
    QSet<QString> needsCleanup_;
    QSet<QString> connecting_;
    QSet<QString> pendingConnectionHistory_;
    QSet<QString> countedConnections_;
    QSet<QString> disconnecting_;
    QSet<QString> cancelledConnections_;
    QHash<QString, quint64> operations_;
    QHash<QString, quint64> revisions_;
    bool profilesLoaded_ = false;
    bool snapshotPending_ = false;
    bool started_ = false;
};
