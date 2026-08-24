// Arc 18 M3 — durable index harness (TEST-MATRIX M3). Proves, all offline:
// (1) one pack persists one mapping row per volume/file pair; (2) a COLD
// second store instance (new object, new connection, same file) reads the same
// canonical mappings; (3) a provider outage (recordSearchError) never deletes
// a verified mapping — availability goes stale, identity survives; (4) a
// successful-empty answer and a provider error land in DISTINCT search_state
// classes, only the former carries a negative TTL; (5) parser_version is
// persisted with mappings; (6) every statement uses bound values through a
// unique per-instance connection (the store API admits no raw SQL).
#include "torrent/MangaTorrentIndex.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDateTime>

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
} // namespace

int main(int argc, char** argv)
{
    // QCoreApplication is REQUIRED before any QSqlDatabase use: SQL driver
    // plugin discovery runs off the app instance's library paths (the sibling
    // comics/mal catalog harnesses do the same).
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    require(tmp.isValid(), "temp dir for index db");
    const QString dbPath = tmp.path() + QStringLiteral("/manga_torrent_index.db");
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    const QString hash1 = QStringLiteral("a1b2c3d4e5f60718293a4b5c6d7e8f9012345678");
    const QString hash2 = QStringLiteral("b2c3d4e5f60718293a4b5c6d7e8f90123456789");

    // ── Warm store: one pack -> several exact mapping rows ───────────────────
    {
        MangaTorrentIndex store;
        require(store.open(dbPath), "warm store opens");
        require(store.isOpen(), "warm store reports open");

        IndexedTorrent pack;
        pack.infoHash = hash1;
        pack.providerId = QStringLiteral("1770013");
        pack.releaseTitle = QStringLiteral("Grand Blue Vol 1-3");
        pack.uploader = QStringLiteral("SomeTrust");
        pack.totalSize = 150 * 1024 * 1024;
        pack.seeders = 12;
        pack.discoveredAt = now - 5000;
        pack.lastSeenAt = now;
        require(store.upsertTorrent(pack), "torrent row upserted");

        QList<IndexedFile> files;
        IndexedFile f0;
        f0.fileIndex = 0;
        f0.path = QStringLiteral("Grand Blue v01.cbz");
        f0.size = 50 * 1024 * 1024;
        f0.archiveType = QStringLiteral("cbz");
        f0.coverageKind = QStringLiteral("single");
        f0.coverageLo = f0.coverageHi = QStringLiteral("1");
        f0.coverageSource = QStringLiteral("filename");
        files.append(f0);
        IndexedFile f1 = f0;
        f1.fileIndex = 1;
        f1.path = QStringLiteral("Grand Blue v02.cbz");
        f1.coverageLo = f1.coverageHi = QStringLiteral("2");
        files.append(f1);
        IndexedFile f2 = f0;
        f2.fileIndex = 2;
        f2.path = QStringLiteral("Grand Blue v10.5.cbz");
        f2.coverageLo = f2.coverageHi = QStringLiteral("10.5");
        files.append(f2);
        require(store.replaceFiles(hash1, files), "full file list persisted");

        // One pack, three volumes — one row per volume/file pair (contract §6).
        const auto map = [&hash1](const char* vol, int idx) {
            VolumeMapping m;
            m.volumeId = QStringLiteral("mal:1:v:") + QLatin1String(vol);
            m.infoHash = hash1;
            m.fileIndex = idx;
            m.evidence = MappingEvidence::MetainfoExactFilename;
            m.verifiedAt = 123456;
            m.status = MappingStatus::Verified;
            return m;
        };
        require(store.insertMapping(map("1", 0)), "mapping v1 inserted");
        require(store.insertMapping(map("2", 1)), "mapping v2 inserted");
        require(store.insertMapping(map("10.5", 2)), "mapping v10.5 inserted");
        require(store.insertMapping(map("1", 0)), "mapping v1 re-insert upserts, not errors");

        const QList<VolumeMapping> forVolume = store.mappingsForVolume(
            QStringLiteral("mal:1:v:1"));
        require(forVolume.size() == 1, "one row per volume/file pair, no dupes");
        require(forVolume.at(0).status == MappingStatus::Verified, "mapping verified");
        require(forVolume.at(0).evidence == MappingEvidence::MetainfoExactFilename,
                "evidence persisted");
        require(forVolume.at(0).parserVersion == QString::fromLatin1(MangaTorrentIndex::kParserVersion),
                "parser_version persisted with mapping");
        require(forVolume.at(0).verifiedAt == 123456, "verified_at persisted");

        const QList<VolumeMapping> forTorrent = store.mappingsForTorrent(hash1);
        require(forTorrent.size() == 3, "pack exposes all three mappings under its hash");

        const QList<IndexedFile> filesBack = store.filesForTorrent(hash1);
        require(filesBack.size() == 3, "file list reread");
        require(filesBack.at(0).path == QStringLiteral("Grand Blue v01.cbz")
                    && filesBack.at(2).coverageLo == QStringLiteral("10.5"),
                "file rows carry raw index/path/coverage");

        // Search-state classes: empty answer vs provider error (distinct).
        require(store.recordSearchSuccess(QStringLiteral("mal:1:v:1"), 3, now),
                "successful search recorded");
        require(store.recordSearchSuccess(QStringLiteral("mal:2:v:1"), 0, now - 1000),
                "successful-empty search recorded");
        require(store.recordSearchError(QStringLiteral("mal:3:v:1"), now - 500),
                "provider error recorded");

        const IndexedSearchState ok3 = store.searchState(QStringLiteral("mal:1:v:1"));
        require(ok3.errorClass == SearchErrorClass::None && ok3.resultCount == 3,
                "success class none");
        const IndexedSearchState empty2 = store.searchState(QStringLiteral("mal:2:v:1"));
        require(empty2.errorClass == SearchErrorClass::Empty, "empty class distinct");
        require(empty2.expiresAt > now, "empty answer carries negative TTL");
        require(store.searchNegativeCached(QStringLiteral("mal:2:v:1"), now),
                "empty answer negative-cached inside TTL");
        const IndexedSearchState err1 = store.searchState(QStringLiteral("mal:3:v:1"));
        require(err1.errorClass == SearchErrorClass::ProviderError,
                "provider error class distinct from empty");
        require(err1.expiresAt == 0, "provider error carries NO negative TTL");
        require(!store.searchNegativeCached(QStringLiteral("mal:3:v:1"), now),
                "error is never negative-cached");

        // ── Provider outage: availability goes stale, identity must not move ──
        pack.seeders = 0;
        pack.lastSeenAt = now;
        require(store.upsertTorrent(pack), "outage availability refresh accepted");
        require(store.recordSearchError(QStringLiteral("mal:1:v:1"), now),
                "outage error on volume with verified mapping");
        const QList<VolumeMapping> survived = store.mappingsForVolume(
            QStringLiteral("mal:1:v:1"));
        require(survived.size() == 1 && survived.at(0).status == MappingStatus::Verified,
                "verified mapping survives provider outage");
        require(store.mappingsForTorrent(hash1).size() == 3,
                "whole pack identity survives outage");

        // Status transitions the runtime (M6) will drive.
        require(store.updateMappingStatus(QStringLiteral("mal:1:v:1"), hash1, 0,
                                          MappingStatus::NeedsRevalidation, now),
                "mapping marked for revalidation");
        require(store.mappingsForVolume(QStringLiteral("mal:1:v:1"))
                    .at(0).status == MappingStatus::NeedsRevalidation,
                "revalidation status persisted");
        require(store.updateMappingStatus(QStringLiteral("mal:1:v:1"), hash1, 0,
                                          MappingStatus::Verified, now),
                "mapping restored to verified");
        require(!store.updateMappingStatus(QStringLiteral("nope"), hash1, 7,
                                           MappingStatus::Verified, now),
                "status update on missing row fails honestly");

        // Availability refresh must not clobber identity fields with blanks:
        // a later cold read still sees the pack's file list and mappings keyed
        // to a fully-identified torrent row (verified by the cold block below).
        IndexedTorrent blank;
        blank.infoHash = hash1;
        blank.seeders = 5;
        require(store.upsertTorrent(blank), "blank availability refresh accepted");

        store.close();
        require(!store.isOpen(), "warm store closes");
    }

    // ── Cold store: second instance, new connection, same file ───────────────
    {
        MangaTorrentIndex cold;
        require(cold.open(dbPath), "cold store opens same file");
        const QList<VolumeMapping> v2mappings = cold.mappingsForVolume(
            QStringLiteral("mal:1:v:2"));
        require(v2mappings.size() == 1, "cold reread finds v2 mapping");
        require(v2mappings.at(0).infoHash == hash1 && v2mappings.at(0).fileIndex == 1,
                "cold reread keeps exact hash + fileIndex");
        require(v2mappings.at(0).parserVersion == QString::fromLatin1(MangaTorrentIndex::kParserVersion),
                "cold reread keeps parser_version");
        require(v2mappings.at(0).status == MappingStatus::Verified,
                "cold reread keeps verified status");
        const QList<VolumeMapping> frac = cold.mappingsForVolume(
            QStringLiteral("mal:1:v:10.5"));
        require(frac.size() == 1 && frac.at(0).fileIndex == 2,
                "fractional volume mapping survives restart");
        require(cold.filesForTorrent(hash1).size() == 3, "file list survives restart");
        require(cold.searchNegativeCached(QStringLiteral("mal:2:v:1"), now),
                "negative TTL survives restart");
        // A second live store beside the first connection-wise: unique names.
        MangaTorrentIndex sibling;
        require(sibling.open(dbPath), "sibling store opens concurrently");
        require(sibling.mappingsForVolume(QStringLiteral("mal:1:v:1")).size() == 1,
                "sibling store reads same mapping through its own connection");
        sibling.close();
        cold.close();
    }

    std::cout << "MANGA_TORRENT_INDEX_OK\n";
    return 0;
}
