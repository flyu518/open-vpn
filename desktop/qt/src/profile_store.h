#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

class ProfileStore final {
public:
    explicit ProfileStore(QString filePath = {});

    QString location() const;
    QList<QJsonObject> load(QString* error = nullptr) const;
    bool save(const QList<QJsonObject>& profiles, QString* error = nullptr) const;
    bool importFile(const QString& path,
                    QJsonObject* profile,
                    QString* error = nullptr) const;
    bool exportFile(const QJsonObject& profile,
                    const QString& path,
                    QString* error = nullptr) const;

    // 仅用于连接目标归组；不包含身份、证书和策略，不能用于导入判重。
    static QString connectionKey(const QString& contents);

private:
    static QString stableId(const QString& path);
    static QString profileName(const QString& path, const QString& contents);
    static QString username(const QString& contents);
    static QString remoteHost(const QString& contents);
    static bool generatedName(const QString& value);
    static QJsonObject withRuntimeFields(const QJsonObject& stored,
                                         const QString& path,
                                         const QString& contents);

    QString filePath_;
};
