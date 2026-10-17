#include "profile_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QLockFile>
#include <QSet>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>
#include <utility>
#include <limits>

namespace {

QString managedDirectory(const QString& indexPath) {
    return QDir(QFileInfo(indexPath).absolutePath()).filePath("profiles");
}

bool readIndex(const QString& path, QJsonArray* profiles, QString* error) {
    QFile file(path);
    if (!file.exists() && !QFileInfo(path).isSymLink()) return true;
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("无法读取配置索引：") + file.errorString();
        return false;
    }
    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const auto root = document.object();
    bool valid = parseError.error == QJsonParseError::NoError && document.isObject() &&
                 root.value("profiles").isArray() &&
                 (!root.contains("version") || root.value("version") == 1);
    QSet<QString> ids;
    for (const auto& value : root.value("profiles").toArray()) {
        const auto profile = value.toObject();
        const auto id = profile.value("id").toString();
        if (!value.isObject() || id.isEmpty() || ids.contains(id) ||
            profile.value("path").toString().isEmpty() ||
            (profile.contains("profile") && !profile.value("profile").isString()) ||
            (profile.contains("managedFile") &&
             !QRegularExpression("^[0-9a-f]{64}\\.ovpn$").match(profile.value("managedFile").toString()).hasMatch()) ||
            (profile.contains("contentHash") && profile.value("contentHash").toString() + ".ovpn" != profile.value("managedFile").toString()) ||
            (profile.contains("routeStrategy") && !profile.value("routeStrategy").isObject())) {
            valid = false;
            break;
        }
        ids.insert(id);
    }
    if (!valid) {
        if (error) *error = QStringLiteral("配置索引损坏或版本不支持，已保留原文件并禁止覆盖");
        return false;
    }
    *profiles = root.value("profiles").toArray();
    return true;
}

// Keep imported key material independent of the source directory. Never include
// source lines (which may be passwords or private keys) in diagnostics.
bool readProfile(const QString& path, QString* contents, QString* error) {
    QFile file(path);
    constexpr qint64 maxSize = 4 * 1024 * 1024;
    if (!file.open(QIODevice::ReadOnly) || file.size() > maxSize) {
        if (error) *error = QStringLiteral("无法读取配置文件或配置文件过大");
        return false;
    }
    const QString source = QString::fromUtf8(file.readAll());
    if (source.trimmed().isEmpty() || source.contains(QChar::Null)) {
        if (error) *error = QStringLiteral("配置文件为空或包含无效字符");
        return false;
    }
    const QSet<QString> fileOptions{"ca", "cert", "key", "extra-certs", "dh",
                                   "tls-auth", "tls-crypt", "tls-crypt-v2",
                                   "auth-user-pass", "secret", "pkcs12"};
    const QRegularExpression directive(R"(^\s*(\S+)(?:\s+(.*))?$)");
    const QRegularExpression argument(R"rx(^\s*(?:"([^"]+)"|'([^']+)'|([^\s#;]+))(?:\s+([01]))?\s*(?:[#;].*)?$)rx");
    QStringList lines = source.split('\n');
    QString inlineTag;
    for (QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (!inlineTag.isEmpty()) {
            if (trimmed.compare("</" + inlineTag + ">", Qt::CaseInsensitive) == 0) inlineTag.clear();
            continue;
        }
        if (trimmed.startsWith('<') && !trimmed.startsWith("</") &&
            trimmed.compare("<connection>", Qt::CaseInsensitive) != 0) {
            const auto end = trimmed.indexOf('>');
            if (end > 1 && !trimmed.contains("</")) inlineTag = trimmed.mid(1, end - 1).toLower();
            continue;
        }
        const auto match = directive.match(line);
        const auto option = match.captured(1).toLower();
        if (!fileOptions.contains(option) || match.captured(2).trimmed().isEmpty()) continue;
        const auto value = argument.match(match.captured(2));
        if (!value.hasMatch()) {
            if (error) *error = QStringLiteral("无法解析外部证书或凭据文件引用");
            return false;
        }
        QString name = value.captured(1);
        if (name.isEmpty()) name = value.captured(2);
        if (name.isEmpty()) name = value.captured(3);
        if (name == "[inline]" || (option == "dh" && name == "none")) continue;
        QFile referenced(QDir(QFileInfo(path).absolutePath()).filePath(name));
        if (!referenced.open(QIODevice::ReadOnly) || referenced.size() > maxSize) {
            if (error) *error = QStringLiteral("无法读取配置引用的证书或凭据文件");
            return false;
        }
        const QByteArray bytes = referenced.readAll();
        QString data = option == "pkcs12" ? QString::fromLatin1(bytes.toBase64()) : QString::fromUtf8(bytes);
        if (data.isEmpty() || data.contains(QChar::Null) || data.contains("</" + option + ">", Qt::CaseInsensitive)) {
            if (error) *error = QStringLiteral("配置引用的证书或凭据文件格式无效");
            return false;
        }
        if (!data.endsWith('\n')) data += '\n';
        line = "<" + option + ">\n" + data + "</" + option + ">";
        if (!value.captured(4).isEmpty()) line += "\nkey-direction " + value.captured(4);
    }
    *contents = lines.join('\n');
    return true;
}

bool writePrivateFile(const QString& path, const QByteArray& data, QString* error) {
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
        file.write(data) != data.size() || !file.commit()) {
        if (error) *error = QStringLiteral("配置保存失败：") + file.errorString();
        return false;
    }
    return true;
}

} // namespace

ProfileStore::ProfileStore(QString filePath) {
    if (!filePath.isEmpty()) {
        filePath_ = std::move(filePath);
        return;
    }
#ifdef Q_OS_MACOS
    const QString home = QStandardPaths::writableLocation(
        QStandardPaths::HomeLocation);
    filePath_ = QDir(home).filePath(
        QStringLiteral("Library/Application Support/OpenVPN/profiles.json"));
#elif defined(Q_OS_WIN)
    QString root = qEnvironmentVariable("APPDATA");
    if (root.isEmpty()) {
        root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    filePath_ = QDir(root).filePath(QStringLiteral("OpenVPN/profiles.json"));
#else
    QString root = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (root.isEmpty()) {
        root = QDir(QStandardPaths::writableLocation(
                        QStandardPaths::HomeLocation))
                   .filePath(QStringLiteral(".config"));
    }
    filePath_ = QDir(root).filePath(QStringLiteral("OpenVPN/profiles.json"));
#endif
}

QString ProfileStore::location() const {
    return filePath_;
}

QList<QJsonObject> ProfileStore::load(QString* error) const {
    if (error) error->clear();
    QJsonArray profiles;
    if (!readIndex(filePath_, &profiles, error)) return {};

    QList<QJsonObject> result;
    for (const QJsonValue& value : profiles) {
        const QJsonObject stored = value.toObject();
        const QString path = stored.value(QStringLiteral("path")).toString();
        const QString id = stored.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || path.isEmpty()) continue;

        QString contents;
        const auto managedFile = stored.value("managedFile").toString();
        if (!managedFile.isEmpty()) {
            QFile copy(QDir(managedDirectory(filePath_)).filePath(managedFile));
            QByteArray bytes;
            if (!QFileInfo(copy).isSymLink() && copy.open(QIODevice::ReadOnly) && copy.size() <= 16 * 1024 * 1024) bytes = copy.readAll();
            if (bytes.isEmpty() || QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) + ".ovpn" != managedFile) {
                if (error) *error = QStringLiteral("托管配置副本不可用或校验失败，原索引已保留；请重新导入对应配置");
            } else contents = QString::fromUtf8(bytes);
        } else if (stored.contains("profile")) {
            // Read compatibility only. The next successful save migrates this
            // payload out of the index into a private managed file.
            contents = stored.value("profile").toString();
        } else {
            readProfile(path, &contents, nullptr);
        }
        result.append(withRuntimeFields(stored, path, contents));
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        const auto priority = [](const auto& profile) {
            const int value = profile.value("routeStrategy").toObject().value("priority").toInt();
            return value > 0 ? value : std::numeric_limits<int>::max();
        };
        return priority(a) < priority(b);
    });
    return result;
}

bool ProfileStore::save(const QList<QJsonObject>& profiles, QString* error) const {
    if (error) error->clear();
    if (!QDir().mkpath(QFileInfo(filePath_).absolutePath())) {
        if (error) *error = QStringLiteral("无法创建配置存储目录");
        return false;
    }
    QLockFile lock(filePath_ + ".lock");
    if (!lock.tryLock(0)) {
        if (error) *error = QStringLiteral("配置索引正被其他进程使用或无法锁定");
        return false;
    }
    QJsonArray existing;
    if (!readIndex(filePath_, &existing, error)) return false;
    const QString directory = managedDirectory(filePath_);
    if (QFileInfo(directory).isSymLink() || !QDir().mkpath(directory) ||
        !QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) {
        if (error) *error = QStringLiteral("无法创建受限配置副本目录");
        return false;
    }
    QJsonArray values;
    QSet<QString> ids;
    int order = 1;
    for (const QJsonObject& profile : profiles) {
        const QString id = profile.value(QStringLiteral("profileId"))
                               .toString(profile.value(QStringLiteral("id"))
                                             .toString());
        const QString path = profile.value(QStringLiteral("profilePath"))
                                 .toString(profile.value(QStringLiteral("path"))
                                               .toString());
        if (id.isEmpty() || path.isEmpty() || ids.contains(id)) {
            if (error) *error = QStringLiteral("配置缺少标识或存在重复标识，已取消保存");
            return false;
        }
        ids.insert(id);

        QJsonObject strategy = profile.value(QStringLiteral("routeStrategy"))
                                   .toObject();
        strategy.insert(QStringLiteral("priority"), order++);
        QJsonObject stored{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"),
             profile.value(QStringLiteral("name")).toString()},
            {QStringLiteral("path"), path},
            {QStringLiteral("nameIsCustom"), profile.value("nameIsCustom").toBool()},
            {QStringLiteral("routeStrategy"), strategy},
            {QStringLiteral("connectionCount"),
             profile.value(QStringLiteral("connectionCount")).toInt(0)},
        };
        const QByteArray contents = profile.value("profile").toString().toUtf8();
        if (!contents.isEmpty()) {
            const QString hash = QString::fromLatin1(QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex());
            const QString name = hash + ".ovpn";
            const QString copyPath = QDir(directory).filePath(name);
            if (QFileInfo(copyPath).isSymLink()) {
                if (error) *error = QStringLiteral("配置副本路径不安全，已取消保存");
                return false;
            }
            // Content-addressed immutable copies are committed first. A failed
            // index commit leaves the previous index and every old copy intact.
            QFile copy(copyPath);
            const bool matches = copy.open(QIODevice::ReadOnly) && copy.readAll() == contents;
            copy.close();
            if (matches) {
                if (!QFile::setPermissions(copyPath, QFile::ReadOwner | QFile::WriteOwner)) {
                    if (error) *error = QStringLiteral("无法设置配置副本权限");
                    return false;
                }
            } else if (!writePrivateFile(copyPath, contents, error)) return false;
            stored.insert("managedFile", name);
            stored.insert("contentHash", hash);
        } else {
            // A temporarily missing copy must not be discarded by a rename or
            // a history update. Keep its identity and reference recoverable.
            for (const auto& previousValue : existing) {
                const auto previous = previousValue.toObject();
                if (previous.value("id").toString() != id) continue;
                for (const auto& key : {"managedFile", "contentHash"}) {
                    if (previous.contains(key)) stored.insert(key, previous.value(key));
                }
                break;
            }
        }
        for (const QString& key : {QStringLiteral("lastConnectedAt"),
                                   QStringLiteral("lastDisconnectedAt")}) {
            if (profile.contains(key)) stored.insert(key, profile.value(key));
        }
        values.append(stored);
    }

    QJsonObject root{
        {QStringLiteral("version"), 1},
        {QStringLiteral("profiles"), values},
    };
    const QByteArray payload =
        QJsonDocument(root).toJson(QJsonDocument::Indented) + '\n';
    return writePrivateFile(filePath_, payload, error);
}

bool ProfileStore::importFile(const QString& path,
                              QJsonObject* profile,
                              QString* error) const {
    if (error) error->clear();
    QString contents;
    if (!readProfile(path, &contents, error)) return false;
    if (profile) {
        *profile = withRuntimeFields(
            QJsonObject{
                {QStringLiteral("id"), stableId(path)},
                {QStringLiteral("name"), profileName(path, contents)},
                {QStringLiteral("path"), path},
                {QStringLiteral("routeStrategy"),
                 QJsonObject{
                     {QStringLiteral("conflictMode"),
                      QStringLiteral("compatible")},
                     {QStringLiteral("priority"), 0},
                     {QStringLiteral("rules"), QJsonArray{}},
                 }},
                {QStringLiteral("connectionCount"), 0},
            },
            path, contents);
    }
    return true;
}

bool ProfileStore::exportFile(const QJsonObject& profile,
                              const QString& path,
                              QString* error) const {
    if (error) error->clear();
    const QString contents = profile.value(QStringLiteral("profile")).toString();
    if (contents.isEmpty()) {
        if (error) *error = QStringLiteral("原配置文件不可用，无法导出");
        return false;
    }
    const QByteArray data = contents.toUtf8();
    return writePrivateFile(path, data, error);
}

QString ProfileStore::stableId(const QString& path) {
    quint32 hash = 0x811c9dc5u;
    // 兼容历史配置索引：先只规范化 Windows 分隔符，再按
    // UTF-16 code unit 计算 FNV-1a；不能改成 UTF-8 字节哈希。
    QString normalized = path;
    normalized.replace(QChar('\\'), QChar('/'));
    for (const QChar character : normalized) {
        hash ^= character.unicode();
        hash *= 0x01000193u;
    }
    return QStringLiteral("profile-%1").arg(hash, 8, 16, QLatin1Char('0'));
}

QString ProfileStore::profileName(const QString& path, const QString& contents) {
    const QRegularExpression accessServer(
        QStringLiteral(R"(^\s*#\s*OVPN_ACCESS_SERVER_PROFILE\s*=\s*([^\r\n#]+?)\s*$)"),
        QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    const auto accessMatch = accessServer.match(contents);
    if (accessMatch.hasMatch() && !accessMatch.captured(1).trimmed().isEmpty()) {
        return accessMatch.captured(1).trimmed();
    }
    QString name = QFileInfo(path).completeBaseName();
    return name.isEmpty() ? QStringLiteral("未命名配置") : name;
}

QString ProfileStore::username(const QString& contents) {
    const QRegularExpression comment(
        QStringLiteral(R"(^\s*#\s*OVPN_ACCESS_SERVER_USERNAME\s*=\s*([^\r\n#]+?)\s*$)"),
        QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    const auto commentMatch = comment.match(contents);
    if (commentMatch.hasMatch()) return commentMatch.captured(1).trimmed();

    const QRegularExpression block(
        QStringLiteral(R"(<auth-user-pass\s*>([\s\S]*?)</auth-user-pass\s*>)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto blockMatch = block.match(contents);
    if (!blockMatch.hasMatch()) return {};
    const QStringList lines = blockMatch.captured(1).split(
        QRegularExpression(QStringLiteral(R"(\r?\n)")),
        Qt::SkipEmptyParts);
    return lines.isEmpty() ? QString{} : lines.first().trimmed();
}

QString ProfileStore::remoteHost(const QString& contents) {
    const QRegularExpression remote(
        QStringLiteral(R"(^\s*remote\s+([^\s#]+))"),
        QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    const auto match = remote.match(contents);
    return match.hasMatch() ? match.captured(1).trimmed() : QString{};
}

QString ProfileStore::connectionKey(const QString& contents) {
    QString defaultProtocol = QStringLiteral("udp");
    const QRegularExpression protoDirective(
        QStringLiteral(R"(^\s*proto\s+([^\s#]+))"),
        QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    const auto protoMatch = protoDirective.match(contents);
    if (protoMatch.hasMatch()) {
        defaultProtocol = protoMatch.captured(1).trimmed().toLower();
    }

    const QRegularExpression remoteDirective(
        QStringLiteral(R"(^\s*remote\s+([^\s#]+)(?:\s+(\d+))?(?:\s+([^\s#]+))?)"),
        QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    QStringList endpoints;
    auto iterator = remoteDirective.globalMatch(contents);
    while (iterator.hasNext()) {
        const auto match = iterator.next();
        QString host = match.captured(1).trimmed().toLower();
        if (host.startsWith(QLatin1Char('[')) && host.endsWith(QLatin1Char(']'))) {
            host = host.mid(1, host.size() - 2);
        }
        if (host.isEmpty()) continue;
        const QString port = match.captured(2).isEmpty()
                                 ? QStringLiteral("1194")
                                 : match.captured(2);
        const QString protocol = match.captured(3).isEmpty()
                                     ? defaultProtocol
                                     : match.captured(3).trimmed().toLower();
        endpoints.append(host + QLatin1Char(':') + port + QLatin1Char('/') + protocol);
    }
    endpoints.removeDuplicates();
    std::sort(endpoints.begin(), endpoints.end());
    return endpoints.join(QLatin1Char('|'));
}

bool ProfileStore::generatedName(const QString& value) {
    static const QRegularExpression pattern(
        QStringLiteral(R"(^profile-[0-9a-f]{8,}(?:\(\d+\))?$)"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern.match(value).hasMatch();
}

QJsonObject ProfileStore::withRuntimeFields(const QJsonObject& stored,
                                            const QString& path,
                                            const QString& contents) {
    QJsonObject result = stored;
    const QString id = stored.value(QStringLiteral("id")).toString(
        stored.value(QStringLiteral("profileId")).toString(stableId(path)));
    const QString storedName = stored.value(QStringLiteral("name")).toString();
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("profileId"), id);
    result.insert(QStringLiteral("sessionId"), id);
    result.insert(QStringLiteral("path"), path);
    result.insert(QStringLiteral("profilePath"), path);
    result.insert(QStringLiteral("profile"), contents);
    result.insert(QStringLiteral("name"),
                 (generatedName(storedName) && !stored.value("nameIsCustom").toBool()) || storedName.isEmpty()
                     ? profileName(path, contents)
                     : storedName);
    result.insert(QStringLiteral("username"), username(contents));
    result.insert(QStringLiteral("remoteHost"), remoteHost(contents));
    if (!result.contains(QStringLiteral("routeStrategy"))) {
        result.insert(QStringLiteral("routeStrategy"), QJsonObject{
                                                              {QStringLiteral("conflictMode"),
                                                               QStringLiteral("compatible")},
                                                              {QStringLiteral("priority"), 0},
                                                              {QStringLiteral("rules"),
                                                               QJsonArray{}},
                                                          });
    }
    return result;
}
