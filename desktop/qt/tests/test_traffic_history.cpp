#include "traffic_history.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TrafficHistoryTest : public QObject {
    Q_OBJECT
private slots:
    void minuteBoundaryJitterSurvivesReload() {
        QTemporaryDir dir;
        { TrafficHistory h(dir.path());
          h.record("a", 59000, 0, 0, 0);
          qint64 bytes = 0;
          for (const qint64 time : {61000LL, 73000LL, 85000LL, 97000LL, 109000LL, 119005LL}) {
              bytes += 1000;
              h.record("a", time, bytes, bytes / 2, (time - 59000) / 1000);
          }
          QVERIFY(h.flush()); }
        TrafficHistory reloaded(dir.path());
        const auto points = reloaded.points("a", 1, 120000);
        QCOMPARE(points.size(), 1);
        QCOMPARE(points.first().toMap().value("to").toLongLong(), 119005LL);
        QVERIFY(reloaded.error().isEmpty());
    }
    void excessiveIntervalStillRejected() {
        QTemporaryDir dir;
        { TrafficHistory h(dir.path()); h.record("a", 100000, 0, 0, 0); h.record("a", 102000, 100, 100, 2); QVERIFY(h.flush()); }
        const auto name = QDir(dir.path()).entryList({"*.json"}, QDir::Files).first();
        QFile file(dir.filePath(name)); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[[100000,180000,100,100]]"); file.close();
        TrafficHistory h(dir.path()); QVERIFY(h.points("a", 1, 181000).isEmpty());
        QVERIFY(!h.error().isEmpty());
    }
    void ratesAndProfiles() {
        QTemporaryDir dir; TrafficHistory history(dir.path());
        history.record("a", 100000, 1000, 2000, 10);
        history.record("a", 102000, 5000, 3000, 12);
        const auto points = history.points("a", 1, 103000);
        QCOMPARE(points.size(), 1);
        QCOMPARE(points.first().toMap().value("download").toDouble(), 2000.0);
        QCOMPARE(points.first().toMap().value("upload").toDouble(), 500.0);
        QVERIFY(history.points("b", 1, 103000).isEmpty());
    }
    void resetsAndGapsDoNotSpike() {
        QTemporaryDir dir; TrafficHistory h(dir.path());
        h.record("a", 100000, 1000, 1000, 10);
        h.record("a", 102000, 5000, 3000, 12);
        h.record("a", 104000, 0, 0, 0);
        h.record("a", 106000, 200, 100, 2);
        h.record("a", 200000, 99999999, 99999999, 96);
        h.record("a", 202000, 100000199, 100000099, 98);
        const auto p = h.points("a", 1, 203000);
        QCOMPARE(p.size(), 3);
        QCOMPARE(p[1].toMap().value("download").toDouble(), 100.0);
        QCOMPARE(p[2].toMap().value("download").toDouble(), 100.0);
        QVERIFY(p[1].toMap().value("from").toLongLong() > p[0].toMap().value("to").toLongLong());
    }
    void persistsWithoutCredentialsAndRestartGap() {
        QTemporaryDir dir;
        { TrafficHistory h(dir.path());
          h.record("../profile-a", 100000, 1000, 0, 10);
          h.record("../profile-a", 102000, 3000, 500, 12);
          QVERIFY(h.flush()); }
        TrafficHistory reloaded(dir.path());
        QCOMPARE(reloaded.points("../profile-a", 1, 103000).size(), 1);
        reloaded.record("../profile-a", 104000, 8000, 900, 14);
        QCOMPARE(reloaded.points("../profile-a", 1, 105000).size(), 1);
        const auto files = QDir(dir.path()).entryList({"*.json"}, QDir::Files);
        QCOMPARE(files.size(), 1);
        QFile file(dir.filePath(files.first())); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto data = file.readAll();
        QVERIFY(!data.contains("profile-a")); QVERIFY(!data.contains("password"));
#ifndef Q_OS_WIN
        QVERIFY(!(file.permissions() & (QFile::ReadGroup | QFile::ReadOther)));
#endif
    }
    void retentionAndAggregation() {
        QTemporaryDir dir; TrafficHistory h(dir.path());
        for (int i = 0; i <= 40000; ++i) h.record("a", 100000 + i * 2500LL, i * 2500LL, i * 500LL, i * 2);
        const auto p = h.points("a", 24, 100100000);
        QVERIFY(!p.isEmpty()); QVERIFY(p.size() <= 1442);
        QVERIFY(p.first().toMap().value("to").toLongLong() > 100100000 - 86400000);
        QVERIFY(h.points("a", 1, 100100000).size() <= 62);
    }
    void corruptOrUnwritableDoesNotPretendSaved() {
        QTemporaryDir dir; QFile blocker(dir.filePath("blocked"));
        QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.write("x"); blocker.close();
        TrafficHistory h(blocker.fileName());
        h.record("a", 100000, 0, 0, 10); h.record("a", 102000, 100, 50, 12);
        QVERIFY(!h.flush()); QVERIFY(!h.error().isEmpty());
        QCOMPARE(h.points("a", 1, 103000).size(), 1);
    }
    void corruptFileIsNotOverwritten() {
        QTemporaryDir dir;
        { TrafficHistory h(dir.path()); h.record("a", 100000, 0, 0, 10); h.record("a", 102000, 100, 50, 12); QVERIFY(h.flush()); }
        const auto name = QDir(dir.path()).entryList({"*.json"}, QDir::Files).first();
        QFile file(dir.filePath(name)); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("broken"); file.close();
        { TrafficHistory h(dir.path()); QVERIFY(h.points("a", 1, 103000).isEmpty()); QVERIFY(!h.error().isEmpty());
          h.record("a", 104000, 0, 0, 1); h.record("a", 106000, 200, 200, 3); QVERIFY(!h.flush()); }
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("broken"));
    }
    void deletionRemovesOnlySelectedHistory() {
        QTemporaryDir dir; TrafficHistory h(dir.path());
        for (const auto& id : {"a", "b"}) { h.record(id, 100000, 0, 0, 0); h.record(id, 102000, 100, 50, 2); }
        QVERIFY(h.flush()); QVERIFY(h.remove("a"));
        TrafficHistory reloaded(dir.path());
        QVERIFY(reloaded.points("a", 1, 103000).isEmpty());
        QCOMPARE(reloaded.points("b", 1, 103000).size(), 1);
    }
    void backwardClockDoesNotCorruptHistory() {
        QTemporaryDir dir;
        { TrafficHistory h(dir.path());
          h.record("a", 100000, 1000, 1000, 10); h.record("a", 102000, 3000, 2000, 12);
          h.record("a", 90000, 4000, 3000, 14); h.record("a", 92000, 5000, 4000, 16);
          QVERIFY(h.flush()); }
        TrafficHistory reloaded(dir.path());
        QCOMPARE(reloaded.points("a", 1, 103000).size(), 1);
        QVERIFY(reloaded.error().isEmpty());
    }
};
QTEST_GUILESS_MAIN(TrafficHistoryTest)
#include "test_traffic_history.moc"
