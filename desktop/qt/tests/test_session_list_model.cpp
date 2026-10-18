#include "session_list_model.h"
#include "profile_store.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDir>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#ifdef Q_OS_WIN
#include <windows.h>
#include <aclapi.h>
#pragma comment(lib, "Advapi32.lib")
#endif

namespace {

QString legacyCompatibleId(const QString& path) {
    quint32 hash = 0x811c9dc5u;
    const QString normalized = path;
    for (const QChar character : normalized) {
        hash ^= character.unicode();
        hash *= 0x01000193u;
    }
    return QStringLiteral("profile-%1").arg(hash, 8, 16, QLatin1Char('0'));
}

#ifdef Q_OS_WIN
bool hasProtectedDacl(const QString& path) {
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const std::wstring nativePath = path.toStdWString();
    const DWORD result = GetNamedSecurityInfoW(
        const_cast<LPWSTR>(nativePath.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr,
        &descriptor);
    if (result != ERROR_SUCCESS || descriptor == nullptr || dacl == nullptr) {
        if (descriptor != nullptr) LocalFree(descriptor);
        return false;
    }
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    const bool protectedDacl = GetSecurityDescriptorControl(
        descriptor, &control, &revision) != FALSE &&
        (control & SE_DACL_PROTECTED) != 0;
    LocalFree(descriptor);
    return protectedDacl;
}
#endif

}  // namespace

class SessionListModelTest final : public QObject {
    Q_OBJECT

private slots:
    void exposesDaemonFields();
    void replacesAndMovesSessions();
    void importsAndPersistsProfileMetadata();
    void usesExistingOpenVpnProfileIndexLocation();
    void keepsLegacyProfileIdForUnicodePath();
    void persistsPrivateContentsAfterSourceRemoval();
    void protectsPrivateFilesOnWindows();
    void rejectsCorruptIndexWithoutOverwriting_data();
    void rejectsCorruptIndexWithoutOverwriting();
    void loadsLegacyIdAndPriorityOrder();
    void snapshotsExternalCredentials();
    void rejectsInvalidModelUpdates();
};

void SessionListModelTest::exposesDaemonFields() {
    SessionListModel model;
    model.setSessions({
        QJsonObject{
            {"sessionId", "profile-a"},
            {"name", "公司 A"},
            {"remoteHost", "vpn-a.example.com"},
            {"state", "connected"},
            {"downloadBytes", 1024},
            {"uploadBytes", 2048},
            {"durationSeconds", 12},
        },
    });

    QCOMPARE(model.rowCount(), 1);
    const QModelIndex index = model.index(0, 0);
    QCOMPARE(model.data(index, SessionListModel::IdRole).toString(),
             QStringLiteral("profile-a"));
    QCOMPARE(model.data(index, SessionListModel::NameRole).toString(),
             QStringLiteral("公司 A"));
    QCOMPARE(model.data(index, SessionListModel::ConnectedRole).toBool(), true);
    QCOMPARE(model.data(index, SessionListModel::StateTextRole).toString(),
             QStringLiteral("已连接"));
    QCOMPARE(model.data(index, SessionListModel::DownloadBytesRole).toLongLong(),
             1024);
    QCOMPARE(model.data(index, SessionListModel::DurationSecondsRole).toInt(),
             12);
}

void SessionListModelTest::replacesAndMovesSessions() {
    SessionListModel model;
    QSignalSpy changed(&model, &SessionListModel::orderChanged);
    model.setSessions({
        QJsonObject{{"sessionId", "a"}, {"name", "A"}},
        QJsonObject{{"sessionId", "b"}, {"name", "B"}},
        QJsonObject{{"sessionId", "c"}, {"name", "C"}},
    });

    QVERIFY(model.moveSession(QStringLiteral("c"), QStringLiteral("a")));
    QCOMPARE(model.data(model.index(0, 0), SessionListModel::IdRole).toString(),
             QStringLiteral("c"));
    QCOMPARE(model.data(model.index(1, 0), SessionListModel::IdRole).toString(),
             QStringLiteral("a"));
    QCOMPARE(changed.count(), 1);

    QVERIFY(model.moveSession(QStringLiteral("c"), QStringLiteral("b")));
    QCOMPARE(model.data(model.index(0, 0), SessionListModel::IdRole).toString(),
             QStringLiteral("a"));
    QCOMPARE(model.data(model.index(2, 0), SessionListModel::IdRole).toString(),
             QStringLiteral("c"));
    QCOMPARE(changed.count(), 2);

    QVERIFY(!model.moveSession(QStringLiteral("missing"), QStringLiteral("a")));
}

void SessionListModelTest::importsAndPersistsProfileMetadata() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString profilePath = directory.filePath(QStringLiteral("company.ovpn"));
    QFile profileFile(profilePath);
    QVERIFY(profileFile.open(QIODevice::WriteOnly));
    const QByteArray contents =
        "# OVPN_ACCESS_SERVER_PROFILE=Company\n"
        "# OVPN_ACCESS_SERVER_USERNAME=alice\n"
        "client\nremote vpn.company.example 1194\n";
    QCOMPARE(profileFile.write(contents), contents.size());
    profileFile.close();

    ProfileStore store(directory.filePath(QStringLiteral("profiles.json")));
    QJsonObject imported;
    QString error;
    QVERIFY2(store.importFile(profilePath, &imported, &error),
             qPrintable(error));
    QCOMPARE(imported.value(QStringLiteral("name")).toString(),
             QStringLiteral("Company"));
    QCOMPARE(imported.value(QStringLiteral("username")).toString(),
             QStringLiteral("alice"));
    QCOMPARE(imported.value(QStringLiteral("remoteHost")).toString(),
             QStringLiteral("vpn.company.example"));

    QVERIFY2(store.save({imported}, &error), qPrintable(error));
    const QList<QJsonObject> loaded = store.load(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().value(QStringLiteral("profile")).toString(),
             QString::fromUtf8(contents));

    const QString exportPath = directory.filePath(QStringLiteral("copy.ovpn"));
    QVERIFY2(store.exportFile(loaded.first(), exportPath, &error),
             qPrintable(error));
    QFile exported(exportPath);
    QVERIFY(exported.open(QIODevice::ReadOnly));
    QCOMPARE(exported.readAll(), contents);
}

void SessionListModelTest::usesExistingOpenVpnProfileIndexLocation() {
    ProfileStore store;
#ifdef Q_OS_MACOS
    const QString expected = QDir(QStandardPaths::writableLocation(
        QStandardPaths::HomeLocation))
                                 .filePath(QStringLiteral(
                                     "Library/Application Support/OpenVPN/profiles.json"));
    QCOMPARE(store.location(), expected);
#elif defined(Q_OS_WIN)
    QString root = qEnvironmentVariable("APPDATA");
    if (root.isEmpty()) {
        root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    const QString expected = QDir(root).filePath(
        QStringLiteral("OpenVPN/profiles.json"));
    QCOMPARE(store.location(), expected);
#else
    QVERIFY(store.location().endsWith(QStringLiteral("OpenVPN/profiles.json")));
#endif
}

void SessionListModelTest::keepsLegacyProfileIdForUnicodePath() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString profilePath = QDir(directory.filePath(QStringLiteral("公司网络")))
                                    .filePath(QStringLiteral("办公.ovpn"));
    QDir().mkpath(QFileInfo(profilePath).absolutePath());
    QFile file(profilePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("client\n") > 0);
    file.close();

    ProfileStore store(directory.filePath(QStringLiteral("profiles.json")));
    QJsonObject imported;
    QString error;
    QVERIFY2(store.importFile(profilePath, &imported, &error),
             qPrintable(error));
    QCOMPARE(imported.value(QStringLiteral("profileId")).toString(),
             legacyCompatibleId(profilePath));
}

void SessionListModelTest::persistsPrivateContentsAfterSourceRemoval() {
    QTemporaryDir directory;
    const QString path = directory.filePath("source.ovpn");
    QFile source(path);
    QVERIFY(source.open(QIODevice::WriteOnly));
    const QByteArray contents = "client\nremote private.example 443 tcp\n<key>\n-----BEGIN PRIVATE KEY-----\nPRIVATE FIXTURE\n-----END PRIVATE KEY-----\n</key>\n";
    QCOMPARE(source.write(contents), contents.size());
    source.close();
    ProfileStore store(directory.filePath("profiles.json"));
    QJsonObject profile;
    QString error;
    QVERIFY(store.importFile(path, &profile, &error));
    QVERIFY(store.save({profile}, &error));
    QFile indexFile(store.location());
    QVERIFY(indexFile.open(QIODevice::ReadOnly));
    const auto indexBytes = indexFile.readAll();
    QVERIFY(!indexBytes.contains("PRIVATE FIXTURE"));
    QVERIFY(!indexBytes.contains("PRIVATE KEY"));
    const auto record = QJsonDocument::fromJson(indexBytes).object().value("profiles").toArray().first().toObject();
    QVERIFY(!record.contains("profile"));
    QVERIFY(!record.value("managedFile").toString().isEmpty());
    const auto managedPath = directory.filePath("profiles/" + record.value("managedFile").toString());
#if !defined(Q_OS_WIN)
    QVERIFY(!(QFile::permissions(directory.filePath("profiles")) & (QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup | QFile::WriteOther | QFile::ExeGroup | QFile::ExeOther)));
#endif
    QVERIFY(QFileInfo::exists(managedPath));
#if !defined(Q_OS_WIN)
    QVERIFY(!(QFile::permissions(managedPath) & (QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup | QFile::WriteOther)));
#endif
    QVERIFY(source.remove());
    const auto loaded = store.load(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().value("profile").toString(), QString::fromUtf8(contents));
#if !defined(Q_OS_WIN)
    const auto permissions = QFile::permissions(store.location());
    QVERIFY(!(permissions & (QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup |
                              QFile::ReadOther | QFile::WriteOther | QFile::ExeOther)));
#endif
    const QString exported = directory.filePath("export.ovpn");
    QVERIFY(store.exportFile(loaded.first(), exported, &error));
    QFile exportedFile(exported);
    QVERIFY(exportedFile.open(QIODevice::ReadOnly));
    QCOMPARE(exportedFile.readAll(), contents);
#if !defined(Q_OS_WIN)
    QVERIFY(!(QFile::permissions(exported) & (QFile::ReadGroup | QFile::ReadOther)));
#endif
}

void SessionListModelTest::protectsPrivateFilesOnWindows() {
#ifndef Q_OS_WIN
    QSKIP("Windows ACL verification only applies on Windows");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourcePath = directory.filePath(QStringLiteral("source.ovpn"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QVERIFY(source.write("client\nremote acl.example\n") > 0);
    source.close();

    ProfileStore store(directory.filePath(QStringLiteral("profiles.json")));
    QJsonObject profile;
    QString error;
    QVERIFY2(store.importFile(sourcePath, &profile, &error), qPrintable(error));
    QVERIFY2(store.save({profile}, &error), qPrintable(error));

    QFile index(store.location());
    QVERIFY(index.open(QIODevice::ReadOnly));
    const auto record = QJsonDocument::fromJson(index.readAll())
                            .object().value("profiles").toArray().first().toObject();
    const QString managedPath = directory.filePath(
        QStringLiteral("profiles/") + record.value("managedFile").toString());
    QVERIFY(hasProtectedDacl(directory.filePath(QStringLiteral("profiles"))));
    QVERIFY(hasProtectedDacl(store.location()));
    QVERIFY(hasProtectedDacl(managedPath));

    const QString exportPath = directory.filePath(QStringLiteral("export.ovpn"));
    QVERIFY2(store.exportFile(profile, exportPath, &error), qPrintable(error));
    QVERIFY(hasProtectedDacl(exportPath));
#endif
}

void SessionListModelTest::rejectsCorruptIndexWithoutOverwriting_data() {
    QTest::addColumn<QByteArray>("payload");
    QTest::newRow("json") << QByteArray("broken");
    QTest::newRow("missing-array") << QByteArray("{\"version\":1}");
    QTest::newRow("invalid-entry") << QByteArray("{\"version\":1,\"profiles\":[{}]}");
    QTest::newRow("future-version") << QByteArray("{\"version\":42,\"profiles\":[]}");
    QTest::newRow("duplicate-id") << QByteArray("{\"version\":1,\"profiles\":[{\"id\":\"a\",\"path\":\"a\"},{\"id\":\"a\",\"path\":\"b\"}]}");
}

void SessionListModelTest::rejectsCorruptIndexWithoutOverwriting() {
    QFETCH(QByteArray, payload);
    QTemporaryDir directory;
    const QString path = directory.filePath("profiles.json");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(payload), payload.size());
    file.close();
    ProfileStore store(path);
    QString error;
    store.load(&error);
    QVERIFY(!error.isEmpty());
    QVERIFY(!store.save({}, &error));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), payload);
}

void SessionListModelTest::loadsLegacyIdAndPriorityOrder() {
    QTemporaryDir directory;
    const QString profilePath = directory.filePath("legacy.ovpn");
    QFile source(profilePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("client\nremote legacy.example\n");
    source.close();
    const QJsonArray entries{
        QJsonObject{{"id", "legacy-b"}, {"path", profilePath}, {"name", "B"},
                    {"routeStrategy", QJsonObject{{"priority", 2}}}},
        QJsonObject{{"id", "legacy-a"}, {"path", profilePath}, {"name", "A"},
                    {"routeStrategy", QJsonObject{{"priority", 1}}}}};
    const QString index = directory.filePath("profiles.json");
    QFile file(index);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject{{"version", 1}, {"profiles", entries}}).toJson());
    file.close();
    ProfileStore store(index);
    QString error;
    const auto profiles = store.load(&error);
    QVERIFY(error.isEmpty());
    QCOMPARE(profiles.size(), 2);
    QCOMPARE(profiles.first().value("profileId").toString(), QString("legacy-a"));
    QVERIFY(store.save(profiles, &error));
    QVERIFY(source.remove());
    QCOMPARE(store.load(&error).first().value("profileId").toString(), QString("legacy-a"));
    QVERIFY(!store.load(&error).first().value("profile").toString().isEmpty());
}

void SessionListModelTest::snapshotsExternalCredentials() {
    QTemporaryDir directory;
    QFile certificate(directory.filePath("client cert.pem"));
    QVERIFY(certificate.open(QIODevice::WriteOnly));
    certificate.write("CERTIFICATE FIXTURE\n");
    certificate.close();
    QFile ca(directory.filePath("ca.pem"));
    QVERIFY(ca.open(QIODevice::WriteOnly));
    ca.write("CA FIXTURE\n");
    ca.close();
    QFile key(directory.filePath("client.key"));
    QVERIFY(key.open(QIODevice::WriteOnly));
    key.write("PRIVATE KEY FIXTURE\n");
    key.close();
    QFile source(directory.filePath("client.ovpn"));
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("client\nremote external.example\ncert \"client cert.pem\"\nca ca.pem\nkey client.key\n");
    source.close();
    ProfileStore store(directory.filePath("profiles.json"));
    QJsonObject profile;
    QString error;
    QVERIFY2(store.importFile(source.fileName(), &profile, &error), qPrintable(error));
    QVERIFY(profile.value("profile").toString().contains("<cert>\nCERTIFICATE FIXTURE\n</cert>"));
    QVERIFY(profile.value("profile").toString().contains("<ca>\nCA FIXTURE\n</ca>"));
    QVERIFY(profile.value("profile").toString().contains("<key>\nPRIVATE KEY FIXTURE\n</key>"));
    QVERIFY(store.save({profile}, &error));
    QVERIFY(certificate.remove());
    QVERIFY(ca.remove());
    QVERIFY(key.remove());
    QVERIFY(source.remove());
    QCOMPARE(store.load().first().value("profile"), profile.value("profile"));
}

void SessionListModelTest::rejectsInvalidModelUpdates() {
    SessionListModel model;
    model.setSessions({QJsonObject{{"sessionId", "a"}, {"state", "connected"}, {"downloadBytes", 12}}});
    QVERIFY(!model.updateSession(QJsonObject{{"sessionId", "a"}, {"state", "nonsense"}}));
    QVERIFY(!model.updateSession(QJsonObject{{"sessionId", "a"}, {"downloadBytes", -1}}));
    QCOMPARE(model.session("a").value("state").toString(), QString("connected"));
    QCOMPARE(model.session("a").value("downloadBytes").toInt(), 12);
}

QTEST_MAIN(SessionListModelTest)
#include "test_session_list_model.moc"
