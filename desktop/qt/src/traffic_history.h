#pragma once
#include <QString>
#include <QVariantList>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <cmath>

// 流量历史的时间戳由采样者提供，便于与实际快照和断线边界一致。
class TrafficHistory {
public:
    explicit TrafficHistory(QString directory) : directory_(std::move(directory)) {}
    ~TrafficHistory() { flush(); }

    void record(const QString& id, qint64 time, qint64 down, qint64 up, qint64 duration) {
        if (id.isEmpty() || time < 0 || down < 0 || up < 0 || duration < 0) return;
        auto& data = load(id);
        // 系统时钟回拨时暂停采样，不能往已保存的时间线倒插数据。
        if (!data.samples.isEmpty() && time <= data.samples.last().to) {
            baselines_.remove(id);
            return;
        }
        const auto previous = baselines_.constFind(id);
        if (previous != baselines_.cend()) {
            const auto base = previous.value();
            const qint64 elapsed = time - base.time;
            if (elapsed > 0 && elapsed < 1000 && down >= base.down && up >= base.up && duration >= base.duration)
                return;
            if (elapsed >= 1000 && elapsed <= maxGapMs && down >= base.down && up >= base.up && duration >= base.duration) {
                // 每分钟合并连续采样，断线/重启/计数归零/长间隔不跨段连线。
                if (!data.samples.isEmpty() && data.samples.last().to == base.time &&
                    (data.samples.last().to - 1) / bucketMs == (time - 1) / bucketMs) {
                    auto& last = data.samples.last();
                    last.to = time; last.down += down - base.down; last.up += up - base.up;
                } else {
                    data.samples.append({base.time, time, double(down - base.down), double(up - base.up)});
                }
                data.dirty = true;
            }
        }
        baselines_.insert(id, {time, down, up, duration});
        prune(data, time);
    }
    void stop(const QString& id) { baselines_.remove(id); }
    QVariantList points(const QString& id, int hours, qint64 now) {
        if (id.isEmpty()) return {};
        auto& data = load(id);
        prune(data, now);
        QVariantList result;
        const qint64 cutoff = now - (hours == 24 ? 86400000LL : 3600000LL);
        for (const auto& p : data.samples) {
            if (p.to <= cutoff || p.from > now) continue;
            const double seconds = (p.to - p.from) / 1000.0;
            result.append(QVariantMap{{"from", p.from}, {"to", p.to},
                                     {"download", p.down / seconds}, {"upload", p.up / seconds}});
        }
        return result;
    }
    bool flush() {
        error_.clear();
        for (auto it = data_.begin(); it != data_.end(); ++it) {
            auto& data = it.value();
            if (data.invalid) { error_ = QStringLiteral("历史流量文件损坏，未覆盖原文件。"); continue; }
            if (!data.dirty) continue;
            if (!QDir().mkpath(directory_)) { error_ = QStringLiteral("历史流量目录不可写，当前数据仅保留在内存中。"); continue; }
            QFile::setPermissions(directory_, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
            QJsonArray array;
            for (const auto& p : data.samples) array.append(QJsonArray{p.from, p.to, p.down, p.up});
            const auto bytes = QJsonDocument(array).toJson(QJsonDocument::Compact);
            QSaveFile file(path(it.key()));
            if (!file.open(QIODevice::WriteOnly)) { error_ = QStringLiteral("历史流量保存失败：") + file.errorString(); continue; }
            file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
            if (file.write(bytes) != bytes.size() || !file.commit()) {
                error_ = QStringLiteral("历史流量保存失败：") + file.errorString(); continue;
            }
            data.dirty = false;
        }
        return error_.isEmpty();
    }
    bool remove(const QString& id) {
        stop(id); data_.remove(id);
        if (!QFile::exists(path(id)) || QFile::remove(path(id))) return true;
        error_ = QStringLiteral("配置已移除，但历史流量文件未能删除。"); return false;
    }
    QString error() const { return error_; }

private:
    static constexpr qint64 bucketMs = 60000;
    static constexpr qint64 maxGapMs = 15000;
    struct Sample { qint64 from, to; double down, up; };
    struct Data { QList<Sample> samples; bool dirty = false; bool invalid = false; };
    struct Baseline { qint64 time, down, up, duration; };
    QString directory_, error_;
    QHash<QString, Data> data_;
    QHash<QString, Baseline> baselines_;
    QString path(const QString& id) const {
        return QDir(directory_).filePath(QString::fromLatin1(
            QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".json");
    }
    void prune(Data& data, qint64 now) {
        const auto removed = data.samples.removeIf([now](const Sample& p) { return p.to <= now - 86400000LL; });
        if (removed) data.dirty = true;
        while (data.samples.size() > 6000) { data.samples.removeFirst(); data.dirty = true; }
    }
    Data& load(const QString& id) {
        auto it = data_.find(id);
        if (it != data_.end()) return it.value();
        Data data;
        QFile file(path(id));
        if (file.exists()) {
            if (!file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024) data.invalid = true;
            else {
                QJsonParseError error;
                const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
                data.invalid = error.error != QJsonParseError::NoError || !doc.isArray() || doc.array().size() > 6000;
                qint64 last = -1;
                if (!data.invalid) for (const auto& value : doc.array()) {
                    const auto a = value.toArray();
                    bool valid = a.size() == 4;
                    for (const auto& number : a) valid = valid && number.isDouble() && std::isfinite(number.toDouble()) && number.toDouble() >= 0 && number.toDouble() < 9e15;
                    const qint64 from = a.size() == 4 ? a[0].toInteger(-1) : -1;
                    const qint64 to = a.size() == 4 ? a[1].toInteger(-1) : -1;
                    // 分钟桶的首个采样可起于上一分钟，合法跨度上限为桶宽加采样间隔。
                    if (!valid || from < last || to <= from || to - from > bucketMs + maxGapMs) { data.invalid = true; break; }
                    data.samples.append({from, to, a[2].toDouble(), a[3].toDouble()}); last = to;
                }
            }
            if (data.invalid) { data.samples.clear(); error_ = QStringLiteral("历史流量文件无法读取，未覆盖原文件。"); }
        }
        return data_.insert(id, data).value();
    }
};
