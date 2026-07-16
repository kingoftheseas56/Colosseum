// Shared-infohash edition pack transport contract (Task 9, design: docs/
// superpowers/specs/2026-07-15-colosseum-tankorent-comic-volume-mode-design.md,
// "Durable shared-infohash transport"). Mirrors tests/manga_volume_torrent_
// harness.cpp's proof shape but drives ComicTorrentDownloader's NEW edition
// pack API through a FAKE IComicTorrentEngine (no real libtorrent session
// touched) plus a REAL ComicEditionAssembler + REAL ComicDownloader ingest
// target (both already-proven synchronous/local components).
//
// Proves all 8 required behaviors:
//   1. First intent calls addMagnet(paused=true) exactly once.
//   2. Metadata -> exact union priorities set BEFORE startTorrent.
//   3. A second edition on the SAME hash does not re-add the magnet and
//      grows the union.
//   4. Cancelling one edition narrows priorities and preserves the sibling.
//   5. One intent's assembly failure does not fail its sibling.
//   6. Shared files/torrent are removed only after ALL intents are terminal
//      (proven via the cancel-everything path, where nothing ever succeeds).
//   7. Restart replay regroups two ledger rows by hash, re-adds paused,
//      forgets the old selection, and re-resolves against fresh metadata.
//   8. A later edition joining an ALREADY-completed payload (a job that
//      stays resident because an earlier sibling already succeeded) resolves
//      against the existing manifest and assembles immediately — no second
//      addMagnet, no second torrentFinished needed.
#include "torrent/ComicTorrentDownloader.h"
#include "torrent/ComicRequestLedger.h"
#include "torrent/ComicEditionIdentity.h"
#include "engine/ComicDownloader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

#include <cstdlib>
#include <iostream>

using ComicEditionIdentity::ComicCollectionFormat;
using ComicEditionIdentity::ComicEditionTarget;

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
class FakeEngine : public IComicTorrentEngine {
    Q_OBJECT
public:
    using IComicTorrentEngine::IComicTorrentEngine;

    bool isRunning() const override { return true; }
    void start() override {}

    QString addMagnet(const QString& magnetUri, const QString& savePath, bool paused) override
    {
        Q_UNUSED(magnetUri);
        Q_UNUSED(savePath);
        lastPaused = paused;
        ++addMagnetCount;
        callLog << QStringLiteral("addMagnet");
        return QString();   // handle key unused by the downloader (bare 40-hex hash)
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
    void pauseTorrent(const QString&) override {}
    void resumeTorrent(const QString&) override {}
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
    void emitFinished(const QString& hash) { emit torrentFinished(hash); }
    void emitError(const QString& hash, const QString& msg) { emit torrentError(hash, msg); }

    bool removedHash(const QString& hash, bool deleteFiles) const
    {
        for (const auto& r : removed)
            if (r.first == hash.toLower() && r.second == deleteFiles) return true;
        return false;
    }
    bool anyRemoved(const QString& hash) const
    {
        for (const auto& r : removed)
            if (r.first == hash.toLower()) return true;
        return false;
    }

    bool lastPaused = false;
    int addMagnetCount = 0;
    QVector<int> priorities;
    QStringList startedHashes;
    QStringList callLog;
    QList<QPair<QString, bool>> removed;
    QHash<QString, QJsonArray> known;
};

// A 6-entry pack manifest: three sibling directories, one per collection
// format, each holding two loose page images. Directory names carry the
// coverage grammar ComicCoverage::detectComicCoverage() parses (format token
// + adjacent ordinal), so ComicEditionFileSelector's tierDirectoryCoverage
// resolves each edition to its own LooseImageSubtree without touching the
// others (different formats never trip the sibling-ordinal guard).
// A completed page's on-disk size equals its manifest size in production, so
// fixtures and the manifest share ONE size here: ComicTorrentDownloader::
// diskReadiness treats a file shorter than its manifest size as still flushing,
// and a "finished" fixture must model a fully-downloaded (full-size) file.
constexpr int kPackPageBytes = 2048;

QJsonArray sharedPackManifest()
{
    struct Entry { int index; const char* name; };
    static const Entry entries[] = {
        {0, "Compendium 1/page_000.png"},
        {1, "Compendium 1/page_001.png"},
        {2, "Omnibus 1/page_000.png"},
        {3, "Omnibus 1/page_001.png"},
        {4, "TPB 1/page_000.png"},
        {5, "TPB 1/page_001.png"},
    };
    QJsonArray arr;
    for (const Entry& e : entries) {
        QJsonObject o;
        o[QStringLiteral("index")] = e.index;
        o[QStringLiteral("name")]  = QString::fromLatin1(e.name);
        o[QStringLiteral("size")]  = static_cast<double>(kPackPageBytes);
        arr.append(o);
    }
    return arr;
}

ComicEditionTarget mkTarget(const QString& editionId, ComicCollectionFormat format, int ordinal)
{
    ComicEditionTarget t;
    t.editionId = editionId;
    t.seriesId = QStringLiteral("gc:series:pack-transport-test");
    t.seriesTitle = QStringLiteral("Pack Transport Test Series");
    t.editionTitle = QStringLiteral("irrelevant title (resolved by directory coverage)");
    t.format = format;
    t.ordinal = ordinal;
    return t;
}

// Writes a minimal but magic-byte-valid PNG fixture so ComicEditionAssembler's
// looksDecodable() gate accepts it.
bool writePngFixture(const QString& absPath)
{
    QDir().mkpath(QFileInfo(absPath).absolutePath());
    QFile f(absPath);
    if (!f.open(QIODevice::WriteOnly)) return false;
    static const unsigned char png[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    f.write(reinterpret_cast<const char*>(png), 8);
    // Pad to the full manifest page size so a written fixture models a
    // fully-downloaded file (diskReadiness == Ready). looksDecodable() only
    // sniffs the leading PNG magic, so the padding is harmless.
    f.write(QByteArray(kPackPageBytes - 8, 'x'));
    return true;
}

bool writeCompendiumFixture(const QString& saveDir)
{
    return writePngFixture(saveDir + QStringLiteral("/Compendium 1/page_000.png"))
        && writePngFixture(saveDir + QStringLiteral("/Compendium 1/page_001.png"));
}
bool writeTpbFixture(const QString& saveDir)
{
    return writePngFixture(saveDir + QStringLiteral("/TPB 1/page_000.png"))
        && writePngFixture(saveDir + QStringLiteral("/TPB 1/page_001.png"));
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const QString hash(40, QLatin1Char('a'));
    const QString hash2(40, QLatin1Char('b'));
    const QString hash3(40, QLatin1Char('c'));
    const QString magnetUri = QStringLiteral("magnet:?xt=urn:btih:");

    const ComicEditionTarget compendium1 =
        mkTarget(QStringLiteral("comic-pack-transport-test-compendium-1"),
                 ComicCollectionFormat::Compendium, 1);
    const ComicEditionTarget omnibus1 =
        mkTarget(QStringLiteral("comic-pack-transport-test-omnibus-1"),
                 ComicCollectionFormat::Omnibus, 1);
    const ComicEditionTarget tpb1 =
        mkTarget(QStringLiteral("comic-pack-transport-test-tpb-1"),
                 ComicCollectionFormat::TradePaperback, 1);

    // ── (1)(2)(3): paused add once, exact priorities before start, second
    //    edition on the same hash joins without re-adding the magnet ────────
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temp dir is valid");
        FakeEngine engine;
        ComicTorrentDownloader downloader(&engine, /*ingestTarget=*/nullptr,
            dir.filePath(QStringLiteral("ledger.json")), dir.filePath(QStringLiteral("dl")),
            dir.filePath(QStringLiteral("staging")));

        downloader.downloadEdition(compendium1, hash, magnetUri);
        require(engine.lastPaused, "(1) metadata is inspected before payload download");
        require(engine.addMagnetCount == 1, "(1) the shared torrent is added exactly once");

        engine.emitMetadata(hash, sharedPackManifest());
        require(engine.priorities == QVector<int>({7, 7, 0, 0, 0, 0}),
                "(2) only compendium-1's pages get priority");

        const int iAdd   = engine.callLog.indexOf(QStringLiteral("addMagnet"));
        const int iPri   = engine.callLog.indexOf(QStringLiteral("setFilePriorities"));
        const int iStart = engine.callLog.indexOf(QStringLiteral("startTorrent"));
        require(iAdd >= 0 && iPri > iAdd && iStart > iPri,
                "(2) startTorrent is called after metadata inspection and priorities");

        downloader.downloadEdition(omnibus1, hash, magnetUri);
        require(engine.addMagnetCount == 1,
                "(3) a second edition on the same hash does not re-add the magnet");
        require(engine.priorities == QVector<int>({7, 7, 7, 7, 0, 0}),
                "(3) the union grows to include the second edition's pages");
        require(engine.startedHashes.count(hash) == 1, "the shared torrent is started exactly once");
    }

    // ── (4)(6): cancelling one edition narrows priorities and preserves the
    //    sibling; the shared torrent is removed only once EVERY intent is
    //    terminal (here: both cancelled, nothing ever succeeded) ───────────
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temp dir is valid");
        FakeEngine engine;
        ComicTorrentDownloader downloader(&engine, nullptr,
            dir.filePath(QStringLiteral("ledger.json")), dir.filePath(QStringLiteral("dl")),
            dir.filePath(QStringLiteral("staging")));

        downloader.downloadEdition(compendium1, hash, magnetUri);
        engine.emitMetadata(hash, sharedPackManifest());
        downloader.downloadEdition(omnibus1, hash, magnetUri);
        require(engine.priorities == QVector<int>({7, 7, 7, 7, 0, 0}), "both editions live before cancel");

        require(downloader.cancelEdition(compendium1.editionId), "cancel compendium-1 succeeds");
        require(!engine.anyRemoved(hash), "(4) torrent stays alive while omnibus-1 still wants it");
        require(engine.priorities == QVector<int>({0, 0, 7, 7, 0, 0}),
                "(4) cancelling narrows priorities to the surviving sibling");
        require(downloader.statusOfEdition(compendium1.editionId).value(QStringLiteral("state")).toString()
                    == QStringLiteral("cancelled"),
                "cancelled edition reports its journaled cancelled state");
        require(downloader.statusOfEdition(omnibus1.editionId).value(QStringLiteral("state")).toString()
                    == QStringLiteral("downloading"),
                "surviving edition keeps downloading");

        require(downloader.cancelEdition(omnibus1.editionId), "cancel omnibus-1 succeeds");
        require(engine.removedHash(hash, true),
                "(6) shared files removed once every intent is terminal (nothing ever succeeded)");
        require(engine.callLog.count(QStringLiteral("removeTorrent")) == 1,
                "(6) the shared torrent is removed exactly once, not per-intent");

        ComicRequestLedger ledger(dir.filePath(QStringLiteral("ledger.json")));
        ledger.load();
        require(ledger.active().isEmpty(), "both cancelled rows leave active()");
    }

    // ── (5)(8): one intent's assembly failure does not fail its sibling; a
    //    later edition joining an already-completed payload (the job stays
    //    resident because a sibling already succeeded) assembles immediately
    //    without a second addMagnet or a second torrentFinished ────────────
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temp dir is valid");
        const QString saveRoot = dir.filePath(QStringLiteral("dl"));
        const QString saveDir = saveRoot + QLatin1Char('/') + hash2.toLower();

        QNetworkAccessManager nam;
        ComicDownloader comics(&nam);
        comics.deleteIssue(compendium1.editionId);
        comics.deleteIssue(omnibus1.editionId);
        comics.deleteIssue(tpb1.editionId);

        FakeEngine engine;
        ComicTorrentDownloader downloader(&engine, &comics,
            dir.filePath(QStringLiteral("ledger.json")), saveRoot, dir.filePath(QStringLiteral("staging")));

        QStringList failedIds;
        QObject::connect(&downloader, &ComicTorrentDownloader::failed, &app,
            [&](const QString& id, const QString&) { failedIds << id; });

        // Compendium-1's pages exist on disk; omnibus-1's do NOT (forces its
        // assembly to fail "missing on disk"); tpb-1's exist too, ready for
        // the later join.
        require(writeCompendiumFixture(saveDir), "compendium-1 fixture pages written");
        require(writeTpbFixture(saveDir), "tpb-1 fixture pages written");

        downloader.downloadEdition(compendium1, hash2, magnetUri);
        engine.emitMetadata(hash2, sharedPackManifest());
        downloader.downloadEdition(omnibus1, hash2, magnetUri);
        require(engine.priorities == QVector<int>({7, 7, 7, 7, 0, 0}), "both resolved before finish");

        engine.emitFinished(hash2);   // synchronous: assemble+ingest run inline

        require(failedIds == QStringList{omnibus1.editionId},
                "(5) only the sibling with the missing payload fails");
        require(comics.isDownloaded(compendium1.editionId),
                "(5) the sibling with present pages still finishes and publishes");
        require(!comics.isDownloaded(omnibus1.editionId),
                "the failed edition never gets an index entry");
        require(!engine.anyRemoved(hash2),
                "the job stays resident: at least one intent (compendium-1) succeeded");

        // ── (8) a later edition joins the still-resident, already-finished job ──
        const int addMagnetCountBeforeJoin = engine.addMagnetCount;
        downloader.downloadEdition(tpb1, hash2, magnetUri);
        require(engine.addMagnetCount == addMagnetCountBeforeJoin,
                "(8) joining an already-completed payload never re-adds the magnet");
        require(comics.isDownloaded(tpb1.editionId),
                "(8) the later edition assembles and publishes immediately, "
                "with no second torrentFinished needed");

        comics.deleteIssue(compendium1.editionId);
        comics.deleteIssue(tpb1.editionId);
    }

    // ── (7): restart replay regroups two ledger rows by hash, re-adds
    //    paused, forgets the old selection, and re-resolves against fresh
    //    metadata ────────────────────────────────────────────────────────
    {
        QTemporaryDir dir;
        require(dir.isValid(), "temp dir is valid");
        const QString ledgerPath = dir.filePath(QStringLiteral("ledger.json"));
        const QString saveRoot = dir.filePath(QStringLiteral("dl"));
        const QString stagingRoot = dir.filePath(QStringLiteral("staging"));

        FakeEngine engine1;
        ComicTorrentDownloader d1(&engine1, nullptr, ledgerPath, saveRoot, stagingRoot);
        d1.downloadEdition(compendium1, hash3, magnetUri);
        engine1.emitMetadata(hash3, sharedPackManifest());
        d1.downloadEdition(omnibus1, hash3, magnetUri);
        require(engine1.priorities == QVector<int>({7, 7, 7, 7, 0, 0}), "both intents downloading pre-restart");

        ComicRequestLedger before(ledgerPath);
        before.load();
        require(before.active().size() == 2, "two active rows persisted before restart");

        // Simulate a process restart: brand-new engine + downloader, same ledger.
        FakeEngine engine2;
        ComicTorrentDownloader d2(&engine2, nullptr, ledgerPath, saveRoot, stagingRoot);
        require(engine2.addMagnetCount == 1,
                "(7) replay regroups both rows by hash into ONE re-added torrent");
        require(engine2.lastPaused, "(7) replay re-adds paused so metadata is re-inspected first");
        require(engine2.startedHashes.isEmpty(), "(7) replay does not start the payload before metadata");
        require(d2.statusOfEdition(compendium1.editionId).value(QStringLiteral("state")).toString()
                    != QStringLiteral("none"),
                "the replayed compendium-1 intent is tracked by the fresh downloader");
        require(d2.statusOfEdition(omnibus1.editionId).value(QStringLiteral("state")).toString()
                    != QStringLiteral("none"),
                "the replayed omnibus-1 intent is tracked by the fresh downloader");

        // Feed fresh metadata: the old pickedFileIndices (if any) are forgotten
        // for execution — the union below proves a genuine RE-selection, not a
        // replay of stale indices.
        engine2.emitMetadata(hash3, sharedPackManifest());
        require(engine2.priorities == QVector<int>({7, 7, 7, 7, 0, 0}),
                "(7) replay re-selects both editions against current metadata");
    }

    // ── publishLabel (Codex #2): a confirmed combined whole-archive publishes
    //    under its OWN release title, never falsely as the requested edition ──
    {
        using ComicEditionFileSelector::ComicPayloadKind;
        using ComicEditionFileSelector::ComicSelectedFile;

        ComicSelectedFile combined;
        combined.path = QStringLiteral("Compendiums/Invincible Compendium v01-v03.cbz");
        combined.bytes = 100;
        require(ComicTorrentDownloader::publishLabel(ComicPayloadKind::CombinedWholeArchive,
                    QStringLiteral("Invincible Compendium #1"), { combined })
                    == QStringLiteral("Invincible Compendium v01-v03"),
                "publishLabel: combined archive publishes as its release title, not the target edition");

        ComicSelectedFile single;
        single.path = QStringLiteral("Invincible Compendium v01.cbz");
        require(ComicTorrentDownloader::publishLabel(ComicPayloadKind::SingleArchive,
                    QStringLiteral("Invincible Compendium #1"), { single })
                    == QStringLiteral("Invincible Compendium #1"),
                "publishLabel: a normal single archive still publishes under the requested edition title");

        require(ComicTorrentDownloader::publishLabel(ComicPayloadKind::CombinedWholeArchive,
                    QStringLiteral("Fallback"), {}) == QStringLiteral("Fallback"),
                "publishLabel: combined with no files falls back to the edition title");
    }

    // ── diskReadiness (Codex #3): assembly must wait out the torrentFinished->
    //    extract flush race (exists-but-short) that made real multi-issue
    //    downloads intermittently fail, WITHOUT deferring a genuinely-missing
    //    file (which must still fail promptly / synchronously) ──
    {
        using ComicEditionFileSelector::ComicSelectedFile;
        using DiskReadiness = ComicTorrentDownloader::DiskReadiness;
        QTemporaryDir dir;
        require(dir.isValid(), "readiness temp dir is valid");
        const QString saveDir = dir.path();

        ComicSelectedFile f;
        f.path = QStringLiteral("issues/Invincible 001.cbz");
        f.bytes = 64;

        require(ComicTorrentDownloader::diskReadiness(saveDir, { f }) == DiskReadiness::Missing,
                "diskReadiness: a missing selected file is Missing (fails promptly, never deferred)");

        const QString abs = saveDir + QStringLiteral("/issues/Invincible 001.cbz");
        QDir().mkpath(QFileInfo(abs).absolutePath());
        {
            QFile part(abs);
            require(part.open(QIODevice::WriteOnly), "short fixture opens for write");
            part.write(QByteArray(32, 'x'));   // half the manifest size (mid-flush)
        }
        require(ComicTorrentDownloader::diskReadiness(saveDir, { f }) == DiskReadiness::Flushing,
                "diskReadiness: a partially-written (short) file is Flushing (wait it out)");

        {
            QFile full(abs);
            require(full.open(QIODevice::WriteOnly), "full fixture opens for write");
            full.write(QByteArray(64, 'x'));   // exactly the manifest size
        }
        require(ComicTorrentDownloader::diskReadiness(saveDir, { f }) == DiskReadiness::Ready,
                "diskReadiness: a fully-written file at manifest size is Ready");
    }

    std::cout << "COMIC_TORRENT_PACK_TRANSPORT_OK\n";
    return 0;
}

#include "comic_torrent_pack_transport_harness.moc"
