// Restart-safe volume torrent transport contract.
//
// Drives MangaVolumeTorrentDownloader through a FAKE IMangaTorrentEngine (no
// libtorrent) that records every engine call and re-emits engine signals on
// demand. The behaviours proven here are REAL, not hard-coded:
//
//   * a candidate is added PAUSED and only started after its metadata is
//     inspected and the exact file's priorities are set;
//   * two volumes sharing one infoHash GROW the same priority union
//     ({0,7,0} -> {0,7,7}) instead of re-adding the magnet;
//   * each requested volume finishes / fails / cancels independently;
//   * a pick failure fails only that volume and never starts its payload;
//   * intents are journaled to a MangaVolumeRequestLedger, so a second ledger
//     instance (a restart) reloads exactly the in-flight rows from disk, and a
//     fresh downloader over that ledger replays them PAUSED.
#include "torrent/MangaVolumeTorrentDownloader.h"
#include "torrent/MangaVolumeRequestLedger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

#include <cstdlib>
#include <iostream>

using namespace MangaTankoban;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

// ── Fake engine seam ─────────────────────────────────────────────────────────
// Records calls (with a call log so ordering is checkable) and lets the test
// pump metadataReady / torrentProgress / torrentFinished / torrentError.
class FakeEngine : public IMangaTorrentEngine {
    Q_OBJECT
public:
    using IMangaTorrentEngine::IMangaTorrentEngine;

    QString addMagnet(const QString& magnetUri, const QString& savePath, bool paused) override
    {
        lastPaused = paused;
        lastMagnet = magnetUri;
        lastSavePath = savePath;
        ++addMagnetCount;
        callLog << QStringLiteral("addMagnet");
        return QString(); // engine handle key unused by the downloader (v1 BTIH magnets)
    }
    void setFilePriorities(const QString& infoHash, const QVector<int>& p) override
    {
        Q_UNUSED(infoHash);
        priorities = p;
        callLog << QStringLiteral("setFilePriorities");
    }
    void startTorrent(const QString& infoHash, const QString& savePath) override
    {
        Q_UNUSED(savePath);
        startedHashes << infoHash.toLower();
        callLog << QStringLiteral("startTorrent");
    }
    void removeTorrent(const QString& infoHash, bool deleteFiles) override
    {
        removed.append(qMakePair(infoHash.toLower(), deleteFiles));
        callLog << QStringLiteral("removeTorrent");
    }
    QJsonArray torrentFiles(const QString& infoHash) const override
    {
        return known.value(infoHash.toLower());
    }

    void emitMetadata(const QString& hash, const QJsonArray& files)
    {
        known.insert(hash.toLower(), files);
        emit metadataReady(hash, QStringLiteral("torrent"), 0, files);
    }
    void emitProgress(const QString& hash, float fraction) { emit torrentProgress(hash, fraction, 0, 0, 0, 0); }
    void emitFinished(const QString& hash) { emit torrentFinished(hash); }
    void emitError(const QString& hash, const QString& msg) { emit torrentError(hash, msg); }

    bool removedHash(const QString& hash, bool deleteFiles) const
    {
        for (const auto& r : removed)
            if (r.first == hash.toLower() && r.second == deleteFiles)
                return true;
        return false;
    }
    bool anyRemoved(const QString& hash) const
    {
        for (const auto& r : removed)
            if (r.first == hash.toLower())
                return true;
        return false;
    }

    bool lastPaused = false;
    QString lastMagnet;
    QString lastSavePath;
    int addMagnetCount = 0;
    QVector<int> priorities;
    QStringList startedHashes;
    QStringList callLog;
    QList<QPair<QString, bool>> removed;
    QHash<QString, QJsonArray> known;
};

// A 3-file pack: volume "2" -> index 1, volume "3" -> index 2.
QJsonArray packFiles()
{
    const char* names[] = {"Series v01.cbz", "Series v02.cbz", "Series v03.cbz"};
    QJsonArray arr;
    for (int i = 0; i < 3; ++i) {
        QJsonObject o;
        o[QStringLiteral("index")] = i;
        o[QStringLiteral("name")]  = QString::fromLatin1(names[i]);
        o[QStringLiteral("size")]  = static_cast<qint64>(48 * 1024 * 1024);
        arr.append(o);
    }
    return arr;
}

VolumeRecord vol(const QString& seriesId, const QString& number)
{
    VolumeRecord v;
    v.seriesId = seriesId;
    v.number   = number;
    v.id       = QStringLiteral("tankoban:") + seriesId + QStringLiteral(":volume:") + number;
    v.title    = QStringLiteral("Series v") + number;
    return v;
}

MangaNyaaCandidate cand(const QString& hash)
{
    MangaNyaaCandidate c;
    c.infoHash  = hash;
    c.magnetUri = QStringLiteral("magnet:?xt=urn:btih:") + hash;
    c.title     = QStringLiteral("Series (Digital) (v01-03)");
    return c;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const QString hash(40, QLatin1Char('a'));       // valid 40-hex infoHash
    const VolumeRecord v2 = vol(QStringLiteral("series:demo"), QStringLiteral("2"));
    const VolumeRecord v3 = vol(QStringLiteral("series:demo"), QStringLiteral("3"));
    const MangaNyaaCandidate candidate = cand(hash);
    const QJsonArray files = packFiles();

    // ── Pinned contract ──────────────────────────────────────────────────────
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temp dir is valid");
        const QString ledgerPath = dir.filePath(QStringLiteral("ledger.json"));
        FakeEngine engine;
        MangaVolumeTorrentDownloader downloader(&engine, ledgerPath, dir.filePath(QStringLiteral("dl")));
        MangaVolumeRequestLedger ledger(ledgerPath);

        downloader.download(v2, candidate);
        require(engine.lastPaused, "metadata is inspected before payload download");
        engine.emitMetadata(hash, files);
        require(engine.priorities == QVector<int>({0, 7, 0}), "only v2 starts");
        downloader.download(v3, candidate);
        require(engine.priorities == QVector<int>({0, 7, 7}), "same hash unions requested files");
        ledger.reload();
        require(ledger.active().size() == 2, "both intents survive restart");

        // Payload only begins AFTER metadata + priorities — never before.
        const int iAdd   = engine.callLog.indexOf(QStringLiteral("addMagnet"));
        const int iPri   = engine.callLog.indexOf(QStringLiteral("setFilePriorities"));
        const int iStart = engine.callLog.indexOf(QStringLiteral("startTorrent"));
        require(iAdd >= 0 && iPri > iAdd && iStart > iPri,
                "startTorrent is called after metadata inspection and priorities");
        require(engine.addMagnetCount == 1, "the shared torrent is added exactly once");
        require(engine.startedHashes.count(hash) == 1, "the shared torrent is started exactly once");
    }

    // ── torrentFinished emits finished per requested volume, independently ────
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temp dir is valid");
        const QString ledgerPath = dir.filePath(QStringLiteral("ledger.json"));
        const QString saveRoot = dir.filePath(QStringLiteral("dl"));
        FakeEngine engine;
        MangaVolumeTorrentDownloader downloader(&engine, ledgerPath, saveRoot);

        QHash<QString, QString> finishedPaths;
        int finishedCount = 0;
        QObject::connect(&downloader, &MangaVolumeTorrentDownloader::finished, &app,
            [&](const QString& id, const QString& path) { finishedPaths.insert(id, path); ++finishedCount; });
        QHash<QString, double> progressReceived;
        QObject::connect(&downloader, &MangaVolumeTorrentDownloader::progress, &app,
            [&](const QString& id, double received, double) { progressReceived.insert(id, received); });

        downloader.download(v2, candidate);
        engine.emitMetadata(hash, files);
        downloader.download(v3, candidate);

        // Progress is per-volume, scaled by that volume's own file size.
        engine.emitProgress(hash, 0.5f);
        const double expected = 0.5 * 48.0 * 1024.0 * 1024.0;
        require(qAbs(progressReceived.value(v2.id) - expected) < 1.0,
                "v2 progress scaled by its file size");
        require(qAbs(progressReceived.value(v3.id) - expected) < 1.0,
                "v3 progress scaled by its file size");

        // Materialize the resolved archives so the finish-verify succeeds.
        const QString saveDir = saveRoot + QLatin1Char('/') + hash;
        require(QDir().mkpath(saveDir), "save dir created");
        for (const QString& name : {QStringLiteral("Series v02.cbz"), QStringLiteral("Series v03.cbz")}) {
            QFile f(saveDir + QLatin1Char('/') + name);
            require(f.open(QIODevice::WriteOnly), "archive fixture opened");
            f.write("cbz");
        }

        engine.emitFinished(hash);
        require(finishedCount == 2, "each requested volume finishes independently");
        require(finishedPaths.value(v2.id).endsWith(QStringLiteral("Series v02.cbz")), "v2 gets its own archive path");
        require(finishedPaths.value(v3.id).endsWith(QStringLiteral("Series v03.cbz")), "v3 gets its own archive path");
        require(QFileInfo::exists(finishedPaths.value(v2.id)), "the finished archive exists on disk");
        require(engine.removedHash(hash, false), "torrent removed but files kept after all volumes finish");

        // Terminal rows leave active(); completed states persist.
        MangaVolumeRequestLedger ledger(ledgerPath);
        require(ledger.active().isEmpty(), "no active rows after all volumes complete");
        require(ledger.all().size() == 2, "completed rows remain journaled");
        int completed = 0;
        for (const VolumeRequestRow& r : ledger.all())
            if (r.state == QStringLiteral("completed")) ++completed;
        require(completed == 2, "both rows reach completed");
    }

    // ── cancel: mid-volume drop keeps the job; last drop removes the torrent ──
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temp dir is valid");
        const QString ledgerPath = dir.filePath(QStringLiteral("ledger.json"));
        FakeEngine engine;
        MangaVolumeTorrentDownloader downloader(&engine, ledgerPath, dir.filePath(QStringLiteral("dl")));

        downloader.download(v2, candidate);
        engine.emitMetadata(hash, files);
        downloader.download(v3, candidate);

        require(downloader.cancel(v2.id), "cancel v2 succeeds");
        require(!engine.anyRemoved(hash), "torrent stays alive while v3 still wants it");
        require(downloader.statusOf(v2.id).value(QStringLiteral("state")).toString() == QStringLiteral("cancelled"),
                "cancelled volume reports its journaled cancelled state (I-2), not none");
        require(downloader.statusOf(v3.id).value(QStringLiteral("state")).toString() == QStringLiteral("downloading"),
                "surviving volume keeps downloading");

        require(downloader.cancel(v3.id), "cancel v3 succeeds");
        require(engine.removedHash(hash, true), "torrent removed once the last volume is cancelled");

        MangaVolumeRequestLedger ledger(ledgerPath);
        require(ledger.active().isEmpty(), "both cancelled rows leave active()");
    }

    // ── a pick failure fails only that volume and never starts its payload ────
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temp dir is valid");
        const QString ledgerPath = dir.filePath(QStringLiteral("ledger.json"));
        FakeEngine engine;
        MangaVolumeTorrentDownloader downloader(&engine, ledgerPath, dir.filePath(QStringLiteral("dl")));

        int failCount = 0;
        QString failReason;
        QString failedId;
        QObject::connect(&downloader, &MangaVolumeTorrentDownloader::failed, &app,
            [&](const QString& id, const QString& reason) { failedId = id; failReason = reason; ++failCount; });

        // Only an inseparable combined archive covers volume 2 — an ambiguous /
        // combined pick that cannot be honestly isolated.
        QJsonArray combined;
        {
            QJsonObject o;
            o[QStringLiteral("index")] = 0;
            o[QStringLiteral("name")]  = QStringLiteral("Series Volumes 1-3.cbz");
            o[QStringLiteral("size")]  = static_cast<qint64>(120 * 1024 * 1024);
            combined.append(o);
        }

        downloader.download(v2, candidate);
        engine.emitMetadata(hash, combined);
        require(failCount == 1, "the unsatisfiable volume fails");
        require(failedId == v2.id, "the failure is keyed to that volume");
        require(!failReason.isEmpty(), "failure carries a reason");
        require(engine.startedHashes.isEmpty(), "no payload starts for a failed pick");
        require(engine.removedHash(hash, true), "the unsatisfiable torrent is discarded");

        MangaVolumeRequestLedger ledger(ledgerPath);
        require(ledger.active().isEmpty(), "the failed row leaves active()");
        require(ledger.all().size() == 1 && ledger.all().first().state == QStringLiteral("failed"),
                "the failed intent is journaled as failed");
    }

    // ── restart: a fresh downloader over an existing ledger replays PAUSED ─────
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temp dir is valid");
        const QString ledgerPath = dir.filePath(QStringLiteral("ledger.json"));
        const QString saveRoot = dir.filePath(QStringLiteral("dl"));

        FakeEngine engine1;
        MangaVolumeTorrentDownloader d1(&engine1, ledgerPath, saveRoot);
        d1.download(v2, candidate);
        engine1.emitMetadata(hash, files);   // v2 downloading, persisted to disk

        MangaVolumeRequestLedger before(ledgerPath);
        require(before.active().size() == 1, "one active intent persisted before restart");

        // Simulate a process restart: brand-new engine + downloader, same ledger.
        FakeEngine engine2;
        MangaVolumeTorrentDownloader d2(&engine2, ledgerPath, saveRoot);
        require(engine2.addMagnetCount == 1, "replay re-adds the persisted torrent");
        require(engine2.lastPaused, "replay re-adds PAUSED so metadata is re-inspected first");
        require(engine2.startedHashes.isEmpty(), "replay does not start the payload before metadata");
        require(d2.statusOf(v2.id).value(QStringLiteral("state")).toString() != QStringLiteral("none"),
                "the replayed intent is tracked by the fresh downloader");
    }

    // ── M-3(a) torrentError fails every requested volume + discards torrent ───
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temp dir is valid");
        const QString ledgerPath = dir.filePath(QStringLiteral("ledger.json"));
        FakeEngine engine;
        MangaVolumeTorrentDownloader downloader(&engine, ledgerPath, dir.filePath(QStringLiteral("dl")));

        QStringList failedIds;
        QObject::connect(&downloader, &MangaVolumeTorrentDownloader::failed, &app,
            [&](const QString& id, const QString&) { failedIds << id; });

        downloader.download(v2, candidate);
        engine.emitMetadata(hash, files);
        downloader.download(v3, candidate);

        engine.emitError(hash, QStringLiteral("swarm died"));
        require(failedIds.contains(v2.id) && failedIds.contains(v3.id), "both volumes fail on engine error");
        require(engine.removedHash(hash, true), "the errored torrent is discarded (files deleted)");

        MangaVolumeRequestLedger ledger(ledgerPath);
        require(ledger.active().isEmpty(), "no active rows after an engine error");
        int failed = 0;
        for (const VolumeRequestRow& r : ledger.all())
            if (r.state == QStringLiteral("failed")) ++failed;
        require(failed == 2, "both rows journaled as failed");
    }

    // ── M-3(b) torrentFinished: missing file fails, present file finishes ─────
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temp dir is valid");
        const QString ledgerPath = dir.filePath(QStringLiteral("ledger.json"));
        const QString saveRoot = dir.filePath(QStringLiteral("dl"));
        FakeEngine engine;
        MangaVolumeTorrentDownloader downloader(&engine, ledgerPath, saveRoot);

        QHash<QString, QString> finishedPaths;
        QStringList failedIds;
        QObject::connect(&downloader, &MangaVolumeTorrentDownloader::finished, &app,
            [&](const QString& id, const QString& path) { finishedPaths.insert(id, path); });
        QObject::connect(&downloader, &MangaVolumeTorrentDownloader::failed, &app,
            [&](const QString& id, const QString&) { failedIds << id; });

        downloader.download(v2, candidate);
        engine.emitMetadata(hash, files);
        downloader.download(v3, candidate);

        // Only v2's archive lands on disk; v3's is missing at finish time.
        const QString saveDir = saveRoot + QLatin1Char('/') + hash;
        require(QDir().mkpath(saveDir), "save dir created");
        QFile present(saveDir + QLatin1Char('/') + QStringLiteral("Series v02.cbz"));
        require(present.open(QIODevice::WriteOnly), "present archive opened");
        present.write("cbz");
        present.close();

        engine.emitFinished(hash);
        require(finishedPaths.contains(v2.id) && finishedPaths.value(v2.id).endsWith(QStringLiteral("Series v02.cbz")),
                "the present volume finishes");
        require(failedIds.contains(v3.id), "the missing volume fails");
        require(!finishedPaths.contains(v3.id), "the missing volume never claims finished");
        require(engine.removedHash(hash, false), "files kept because one volume landed (M-4)");

        MangaVolumeRequestLedger ledger(ledgerPath);
        require(ledger.row(v2.id).state == QStringLiteral("completed"), "v2 journaled completed");
        require(ledger.row(v3.id).state == QStringLiteral("failed"), "v3 journaled failed");
    }

    // ── M-3(c) mixed pick: survivor starts, unsatisfiable volume fails alone ──
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temp dir is valid");
        const QString ledgerPath = dir.filePath(QStringLiteral("ledger.json"));
        FakeEngine engine;
        MangaVolumeTorrentDownloader downloader(&engine, ledgerPath, dir.filePath(QStringLiteral("dl")));

        QStringList failedIds;
        QObject::connect(&downloader, &MangaVolumeTorrentDownloader::failed, &app,
            [&](const QString& id, const QString&) { failedIds << id; });

        // v2 isolates cleanly to index 1; v3 is only covered by an inseparable
        // combined range archive at index 2.
        const char* names[] = {"Series v01.cbz", "Series v02.cbz", "Series Volumes 3-5.cbz"};
        QJsonArray mixed;
        for (int i = 0; i < 3; ++i) {
            QJsonObject o;
            o[QStringLiteral("index")] = i;
            o[QStringLiteral("name")]  = QString::fromLatin1(names[i]);
            o[QStringLiteral("size")]  = static_cast<qint64>(48 * 1024 * 1024);
            mixed.append(o);
        }

        downloader.download(v2, candidate);
        engine.emitMetadata(hash, mixed);   // v2 resolves + starts alone
        downloader.download(v3, candidate);  // v3's pick is combined -> fails

        require(failedIds == QStringList{v3.id}, "only v3 fails");
        require(engine.priorities == QVector<int>({0, 7, 0}), "survivor v2's priorities are unaffected");
        require(engine.startedHashes.count(hash) == 1, "the payload started once, for the survivor");
        require(downloader.statusOf(v2.id).value(QStringLiteral("state")).toString() == QStringLiteral("downloading"),
                "v2 keeps downloading");
        require(downloader.statusOf(v3.id).value(QStringLiteral("state")).toString() == QStringLiteral("failed"),
                "v3 is journaled failed, never started");
    }

    // ── M-3(d) revive after cancel: no duplicate intent, ledger row revived ───
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temp dir is valid");
        const QString ledgerPath = dir.filePath(QStringLiteral("ledger.json"));
        FakeEngine engine;
        MangaVolumeTorrentDownloader downloader(&engine, ledgerPath, dir.filePath(QStringLiteral("dl")));

        int v2progress = 0;
        QObject::connect(&downloader, &MangaVolumeTorrentDownloader::progress, &app,
            [&](const QString& id, double, double) { if (id == v2.id) ++v2progress; });

        downloader.download(v2, candidate);
        engine.emitMetadata(hash, files);
        downloader.download(v3, candidate);
        require(downloader.cancel(v2.id), "cancel v2 while v3 still wants the torrent");

        // Re-request v2 on the SAME still-alive job — must revive, not duplicate.
        downloader.download(v2, candidate);
        require(downloader.statusOf(v2.id).value(QStringLiteral("state")).toString() == QStringLiteral("downloading"),
                "revived v2 downloads again (I-1)");
        require(engine.priorities == QVector<int>({0, 7, 7}), "revived v2 rejoins the union");

        engine.emitProgress(hash, 0.25f);
        require(v2progress == 1, "revive did not create a duplicate intent (single progress emit)");

        MangaVolumeRequestLedger ledger(ledgerPath);
        require(ledger.all().size() == 2, "no duplicate ledger rows after revive");
        require(ledger.row(v2.id).state == QStringLiteral("downloading"), "v2 ledger row revived, not left cancelled");
        require(ledger.active().size() == 2, "both volumes active after revive");
    }

    // ── Arc 18 M6: indexed-identity expectation revalidation ─────────────────
    // A request carrying a persisted (fileIndex, path) must re-confirm EXACTLY
    // that file against live metadata — match proceeds, mismatch fails closed
    // with expectationViolated, and the expectation survives a restart replay.
    {
        // (a) live metadata matches → priorities + start exactly as today.
        {
            QTemporaryDir dir;
            const QString ledgerPath = dir.filePath(QStringLiteral("ledger.json"));
            FakeEngine engine;
            MangaVolumeTorrentDownloader downloader(&engine, ledgerPath,
                                                    dir.filePath(QStringLiteral("dl")));
            int violated = 0;
            QObject::connect(&downloader,
                             &MangaVolumeTorrentDownloader::expectationViolated, &app,
                             [&](const QString&, const QString&, int) { ++violated; });

            MangaVolumeExpectation expect;
            expect.fileIndex = 1;
            expect.filePath  = QStringLiteral("Series v02.cbz");
            downloader.download(v2, candidate, expect);
            engine.emitMetadata(hash, files);
            require(violated == 0, "a matching expectation raises no violation");
            require(engine.priorities == QVector<int>({0, 7, 0}),
                    "a matched expectation sets the exact file's priorities");
            require(engine.startedHashes.count(hash) == 1,
                    "a matched expectation starts the payload as today");
        }

        // (b) live metadata differs (index now holds a different file) → fail
        //     closed, violation raised, no payload, no fallback pick.
        {
            QTemporaryDir dir;
            const QString ledgerPath = dir.filePath(QStringLiteral("ledger.json"));
            FakeEngine engine;
            MangaVolumeTorrentDownloader downloader(&engine, ledgerPath,
                                                    dir.filePath(QStringLiteral("dl")));
            int violated = 0;
            QString violatedId, violatedHash;
            int violatedIndex = -2;
            QStringList failedIds;
            QObject::connect(&downloader,
                             &MangaVolumeTorrentDownloader::expectationViolated, &app,
                             [&](const QString& id, const QString& h, int idx) {
                                 ++violated; violatedId = id; violatedHash = h;
                                 violatedIndex = idx;
                             });
            QObject::connect(&downloader, &MangaVolumeTorrentDownloader::failed, &app,
                             [&](const QString& id, const QString&) { failedIds << id; });

            // The pack's index 1 is v02 in the INDEX but v05 live — the identity
            // row no longer describes this torrent. (v02 still exists elsewhere
            // in the torrent: index 2 — the fallback the transport must refuse.)
            QJsonArray swapped;
            const char* names[] = {"Series v01.cbz", "Series v05.cbz", "Series v02.cbz"};
            for (int i = 0; i < 3; ++i) {
                QJsonObject o;
                o[QStringLiteral("index")] = i;
                o[QStringLiteral("name")]  = QString::fromLatin1(names[i]);
                o[QStringLiteral("size")]  = static_cast<qint64>(48 * 1024 * 1024);
                swapped.append(o);
            }

            MangaVolumeExpectation expect;
            expect.fileIndex = 1;
            expect.filePath  = QStringLiteral("Series v02.cbz");
            downloader.download(v2, candidate, expect);
            engine.emitMetadata(hash, swapped);
            require(violated == 1, "a mismatched expectation raises one violation");
            require(violatedId == v2.id && violatedHash == hash && violatedIndex == 1,
                    "the violation names the volume, hash, and expected index");
            require(failedIds == QStringList{v2.id}, "the mismatched intent fails closed");
            require(engine.startedHashes.isEmpty(),
                    "no payload starts for a contradicted identity");
            require(engine.priorities.isEmpty(),
                    "the picker never falls back to the same torrent's other file");
            require(engine.removedHash(hash, true),
                    "the contradicted torrent is discarded");

            MangaVolumeRequestLedger ledger(ledgerPath);
            require(ledger.row(v2.id).state == QStringLiteral("failed"),
                    "the violated intent is journaled failed");
        }

        // (c) restart replay: the expectation persisted in the ledger is
        //     re-checked after resume — match proceeds, mismatch violates again.
        {
            QTemporaryDir dir;
            const QString ledgerPath = dir.filePath(QStringLiteral("ledger.json"));
            const QString saveRoot = dir.filePath(QStringLiteral("dl"));

            MangaVolumeExpectation expect;
            expect.fileIndex = 1;
            expect.filePath  = QStringLiteral("Series v02.cbz");

            FakeEngine engine1;
            MangaVolumeTorrentDownloader d1(&engine1, ledgerPath, saveRoot);
            d1.download(v2, candidate, expect);   // awaiting_metadata, journaled
            MangaVolumeRequestLedger before(ledgerPath);
            require(before.row(v2.id).expectedFileIndex == 1
                        && before.row(v2.id).expectedFilePath
                               == QStringLiteral("Series v02.cbz"),
                    "the expectation persists in the ledger for the restart");

            // Restart: fresh engine + downloader replay the row PAUSED with the
            // persisted expectation; matching metadata proceeds.
            FakeEngine engine2;
            MangaVolumeTorrentDownloader d2(&engine2, ledgerPath, saveRoot);
            require(engine2.addMagnetCount == 1 && engine2.lastPaused,
                    "replay re-adds the persisted torrent paused");
            engine2.emitMetadata(hash, files);
            require(engine2.priorities == QVector<int>({0, 7, 0}),
                    "a resumed expectation that matches proceeds to priorities + start");
            require(d2.statusOf(v2.id).value(QStringLiteral("state")).toString()
                        == QStringLiteral("downloading"),
                    "the resumed intent downloads after re-confirmation");
        }
        {
            // (d) restart into CONTRADICTED metadata: the replay re-checks and
            //     violates again — a stale expectation never rides through.
            QTemporaryDir dir;
            const QString ledgerPath = dir.filePath(QStringLiteral("ledger.json"));
            const QString saveRoot = dir.filePath(QStringLiteral("dl"));

            MangaVolumeExpectation expect;
            expect.fileIndex = 1;
            expect.filePath  = QStringLiteral("Series v02.cbz");

            FakeEngine engine1;
            MangaVolumeTorrentDownloader d1(&engine1, ledgerPath, saveRoot);
            d1.download(v2, candidate, expect);

            QJsonArray combined;
            {
                QJsonObject o;
                o[QStringLiteral("index")] = 0;
                o[QStringLiteral("name")]  = QStringLiteral("Series Volumes 1-3.cbz");
                o[QStringLiteral("size")]  = static_cast<qint64>(120 * 1024 * 1024);
                combined.append(o);
            }

            FakeEngine engine2;
            MangaVolumeTorrentDownloader d2(&engine2, ledgerPath, saveRoot);
            int violated = 0;
            QObject::connect(&d2, &MangaVolumeTorrentDownloader::expectationViolated, &app,
                             [&](const QString&, const QString&, int) { ++violated; });
            engine2.emitMetadata(hash, combined);
            require(violated == 1,
                    "a resumed expectation against contradicting metadata violates again");
            require(engine2.startedHashes.isEmpty(),
                    "the contradicted resume never starts its payload");
        }

        // (e) one infoHash, many volume intents — an expectation on one intent
        //     leaves its sibling's ordinary pick untouched.
        {
            QTemporaryDir dir;
            const QString ledgerPath = dir.filePath(QStringLiteral("ledger.json"));
            FakeEngine engine;
            MangaVolumeTorrentDownloader downloader(&engine, ledgerPath,
                                                    dir.filePath(QStringLiteral("dl")));
            QStringList failedIds;
            QObject::connect(&downloader, &MangaVolumeTorrentDownloader::failed, &app,
                             [&](const QString& id, const QString&) { failedIds << id; });

            MangaVolumeExpectation expect;
            expect.fileIndex = 2;
            expect.filePath  = QStringLiteral("Series v03.cbz");
            downloader.download(v2, candidate);           // ordinary pick → index 1
            downloader.download(v3, candidate, expect);   // expectation → index 2
            engine.emitMetadata(hash, files);
            require(failedIds.isEmpty(), "both intents resolve cleanly");
            require(engine.priorities == QVector<int>({0, 7, 7}),
                    "the union is unchanged by an expectation riding along");
            require(engine.addMagnetCount == 1,
                    "one job per infoHash is unchanged by expectations");
        }
    }

    std::cout << "MANGA_VOLUME_TORRENT_OK\n";
    return 0;
}

#include "manga_volume_torrent_harness.moc"
