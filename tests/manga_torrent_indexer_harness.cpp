// Arc 18 M4 — indexer harness (TEST-MATRIX M4). Proves OFFLINE, with REAL
// libtorrent-built .torrent bytes through the REAL resolver:
//   (1) one pack indexes several exact archive files in one pass — one mapping
//       row per (volumeId, infoHash, fileIndex), filename AND directory
//       evidence both landing as verified, fractional included;
//   (2) a combined "Volumes 1-12.cbz" archive is a rejected diagnostic, never
//       a verified mapping;
//   (3) two equally-exact archives under one hash are ambiguous — NEITHER
//       maps;
//   (4) a release-title claim alone never promotes: title says "v1-v5", files
//       only hold v1 → v5 gets NO row;
//   (5) a decoded infoHash that disagrees with the candidate fails the whole
//       candidate before anything persists;
//   (6) named/special volumes map only when the canonical series volume list
//       carries the exact folded token (fail closed otherwise).
#include "torrent/MangaTorrentIndexer.h"

#include "engine/MangaTankobanLogic.h"
#include "torrent/MangaTorrentMetainfoResolver.h"

#include <libtorrent/bencode.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/torrent_info.hpp>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace MangaTankoban;

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct BuiltTorrent {
    QByteArray bytes;
    QString infoHash; // 40-char lowercase hex from a direct libtorrent decode
};

void writeFile(const QString& dir, const QString& relPath, int size)
{
    const QString abs = dir + QLatin1Char('/') + relPath;
    QFileInfo fi(abs);
    QDir().mkpath(fi.absolutePath());
    QFile f(abs);
    require(f.open(QIODevice::WriteOnly), "fixture file can be written");
    f.write(QByteArray(size, '\x5a'));
    f.close();
}

// Build a real multi-file .torrent from `entries` (path/size pairs) rooted at
// `rootName`, mirroring the M2 resolver harness's offline technique.
BuiltTorrent buildTorrent(const QString& workDir, const QString& rootName,
                          const QList<QPair<QString, int>>& entries)
{
    BuiltTorrent out;
    lt::file_storage storage;
    for (const auto& e : entries) {
        writeFile(workDir, rootName + QLatin1Char('/') + e.first, e.second);
        storage.add_file((rootName.toStdString() + "/" + e.first.toStdString()),
                         e.second);
    }
    lt::create_torrent creator(storage, 16 * 1024);
    lt::set_piece_hashes(creator, workDir.toStdString());
    const lt::entry generated = creator.generate();
    std::vector<char> encoded;
    lt::bencode(std::back_inserter(encoded), generated);
    out.bytes = QByteArray(encoded.data(), static_cast<int>(encoded.size()));

    lt::error_code ec;
    const lt::torrent_info ref(encoded.data(), static_cast<int>(encoded.size()), ec);
    require(!ec, "fixture torrent_info decodes");
    const std::string raw = ref.info_hashes().v1.to_string();
    static const char kDigits[] = "0123456789abcdef";
    for (const unsigned char c : raw) {
        out.infoHash += QLatin1Char(kDigits[c >> 4]);
        out.infoHash += QLatin1Char(kDigits[c & 0x0f]);
    }
    return out;
}

SeriesSnapshot snapshotWithVolumes(const QString& seriesId, bool withNamedSpecial)
{
    SeriesSnapshot s;
    s.seriesId = seriesId;
    s.title = QStringLiteral("Series");
    const auto rec = [&seriesId](const char* num) {
        VolumeRecord v;
        v.seriesId = seriesId;
        v.number = QLatin1String(num);
        return v;
    };
    s.volumes << rec("1") << rec("2") << rec("10.5");
    if (withNamedSpecial)
        s.volumes << rec("Special");
    return s;
}

MangaNyaaCandidate candidateFor(const BuiltTorrent& t, const QString& title)
{
    MangaNyaaCandidate c;
    c.infoHash = t.infoHash;
    c.title = title;
    c.uploader = QStringLiteral("SomeTrust");
    c.seeders = 9;
    c.discoveredAt = 111111;
    return c;
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv); // required for SQL driver plugin discovery
    QTemporaryDir tmp;
    require(tmp.isValid(), "temp dir for indexer fixtures");
    const QString work = tmp.path();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    MangaTorrentIndex store;
    require(store.open(work + QStringLiteral("/indexer.db")), "index store opens");
    MangaTorrentMetainfoResolver resolver;
    MangaTorrentIndexer indexer(&resolver, &store);

    const QString sid = QStringLiteral("mal:1");

    // ── (1) One pack, several exact archives, one pass ────────────────────────
    {
        const BuiltTorrent pack = buildTorrent(
            work, QStringLiteral("SeriesPack"),
            { { QStringLiteral("Series Vol 01/Series.cbz"), 48 * 1024 },   // directory evidence
              { QStringLiteral("Series v02.cbz"), 48 * 1024 },            // filename evidence
              { QStringLiteral("Series Vol 10.5.cbz"), 32 * 1024 },       // fractional
              { QStringLiteral("notes.txt"), 64 } });                     // non-archive

        IndexerOutcome outcome;
        require(indexer.indexCandidate(snapshotWithVolumes(sid, /*withNamedSpecial=*/false),
                                       candidateFor(pack, QStringLiteral("Series Vol 1-3")),
                                       pack.bytes, now, &outcome),
                "pack candidate indexes");
        require(outcome.verifiedCount == 3, "one pass verifies three volumes");
        require(outcome.rejectedCount == 0, "no refusals in the clean pack");

        const QList<VolumeMapping> rows = store.mappingsForTorrent(pack.infoHash);
        require(rows.size() == 3, "three mapping rows under the pack hash");
        const QString v1 = MangaTankoban::volumeId(sid, QStringLiteral("1"));
        const QString v2 = MangaTankoban::volumeId(sid, QStringLiteral("2"));
        const QString vfrac = MangaTankoban::volumeId(sid, QStringLiteral("10.5"));
        require(store.mappingsForVolume(v1).size() == 1, "v1 mapped");
        require(store.mappingsForVolume(v2).size() == 1, "v2 mapped");
        require(store.mappingsForVolume(vfrac).size() == 1, "v10.5 mapped");

        const VolumeMapping m1 = store.mappingsForVolume(v1).at(0);
        require(m1.evidence == MappingEvidence::MetainfoExactDirectory,
                "v1 evidence is directory-exact");
        require(m1.status == MappingStatus::Verified, "v1 verified");
        require(store.mappingsForVolume(v2).at(0).evidence
                    == MappingEvidence::MetainfoExactFilename,
                "v2 evidence is filename-exact");
        // Raw file list: the store must carry EXACTLY what the resolver
        // enumerated from the same bytes — libtorrent may append .pad filler,
        // so truth is the resolver's own count, never a hardcoded N.
        TorrentMetainfo truth;
        require(resolver.resolve(pack.bytes, truth), "truth re-decode for count");
        const QList<IndexedFile> storedFiles = store.filesForTorrent(pack.infoHash);
        require(storedFiles.size() == truth.files.size(),
                "stored file list equals resolver enumeration");
        bool sawNotes = false;
        for (const IndexedFile& f : storedFiles)
            sawNotes = sawNotes || f.path.endsWith(QStringLiteral("notes.txt"));
        require(sawNotes, "non-archive entry persisted in file list");
    }

    // ── (4) Title claim alone never promotes ─────────────────────────────────
    {
        const BuiltTorrent claim = buildTorrent(
            work, QStringLiteral("ClaimPack"),
            { { QStringLiteral("Series v01.cbz"), 32 * 1024 } });
        IndexerOutcome outcome;
        require(indexer.indexCandidate(snapshotWithVolumes(sid, false),
                                       candidateFor(claim, QStringLiteral("Series Vol 1-5")),
                                       claim.bytes, now, &outcome),
                "title-claim candidate indexes (honestly)");
        require(outcome.verifiedCount == 1, "only the real file maps");
        const QString v5 = MangaTankoban::volumeId(sid, QStringLiteral("5"));
        require(store.mappingsForVolume(v5).isEmpty(),
                "title-claimed v5 gets NO mapping row");
    }

    // ── (2) Combined archive → rejected diagnostic, never a mapping ──────────
    {
        const BuiltTorrent combined = buildTorrent(
            work, QStringLiteral("CombinedPack"),
            { { QStringLiteral("Series Volumes 1-12.cbz"), 200 * 1024 } });
        IndexerOutcome outcome;
        require(indexer.indexCandidate(snapshotWithVolumes(sid, false),
                                       candidateFor(combined, QStringLiteral("Series Vols 1-12")),
                                       combined.bytes, now, &outcome),
                "combined candidate indexes");
        require(outcome.verifiedCount == 0, "combined verifies nothing");
        require(outcome.rejectedCount == 1, "combined counted as rejected");
        require(store.mappingsForTorrent(combined.infoHash).isEmpty(),
                "no mapping rows under the combined hash");
        bool sawCombined = false;
        for (const QString& d : outcome.diagnostics)
            sawCombined = sawCombined || d.contains(QStringLiteral("combined archive refused"));
        require(sawCombined, "combined diagnostic recorded");
    }

    // ── (3) Two equally-exact archives → ambiguous, NEITHER maps ─────────────
    {
        const BuiltTorrent ambiguous = buildTorrent(
            work, QStringLiteral("AmbiguousPack"),
            { { QStringLiteral("Series v01.cbz"), 32 * 1024 },
              { QStringLiteral("Vol 1/Series.cbz"), 32 * 1024 } });
        IndexerOutcome outcome;
        require(indexer.indexCandidate(snapshotWithVolumes(sid, false),
                                       candidateFor(ambiguous, QStringLiteral("Series v01")),
                                       ambiguous.bytes, now, &outcome),
                "ambiguous candidate indexes");
        require(outcome.verifiedCount == 0, "ambiguity verifies nothing");
        bool sawAmbiguous = false;
        for (const QString& d : outcome.diagnostics)
            sawAmbiguous = sawAmbiguous || d.contains(QStringLiteral("ambiguous volume 1"));
        require(sawAmbiguous, "ambiguity diagnostic recorded");
        // The pack's v1 from block (1) stays mapped; the ambiguous torrent
        // contributes nothing.
        const QString v1 = MangaTankoban::volumeId(sid, QStringLiteral("1"));
        // v1 may legitimately carry mappings from OTHER torrents (SeriesPack in
        // block 1, ClaimPack in block 4) — Torrentio keeps many sources per
        // volume. The pin is that the AMBIGUOUS torrent itself contributes none.
        const QList<VolumeMapping> rows = store.mappingsForVolume(v1);
        for (const VolumeMapping& r : rows)
            require(r.infoHash != ambiguous.infoHash,
                    "ambiguous torrent contributes no v1 row");
    }

    // ── (5) infoHash mismatch → fail closed before persisting ────────────────
    {
        const BuiltTorrent pack = buildTorrent(
            work, QStringLiteral("OtherPack"),
            { { QStringLiteral("Series v02.cbz"), 32 * 1024 } });
        MangaNyaaCandidate lying = candidateFor(pack, QStringLiteral("Series v02"));
        lying.infoHash = QStringLiteral("deadbeefdeadbeefdeadbeefdeadbeefdeadbeef");
        IndexerOutcome outcome;
        require(!indexer.indexCandidate(snapshotWithVolumes(sid, false), lying,
                                        pack.bytes, now, &outcome),
                "infoHash mismatch refuses the whole candidate");
        require(store.mappingsForTorrent(lying.infoHash).isEmpty()
                    && store.filesForTorrent(lying.infoHash).isEmpty(),
                "mismatched candidate persisted nothing");
    }

    // ── (6) Named/special: fail closed unless in the canonical volume list ───
    {
        // The filename must carry an explicit marker + named token for the
        // named grammar to fire at all ("Vol Special", not a bare "Special").
        const BuiltTorrent named = buildTorrent(
            work, QStringLiteral("NamedPack"),
            { { QStringLiteral("Series Vol Special.cbz"), 32 * 1024 } });

        IndexerOutcome closed;
        require(indexer.indexCandidate(snapshotWithVolumes(sid, /*withNamedSpecial=*/false),
                                       candidateFor(named, QStringLiteral("Series Vol Special")),
                                       named.bytes, now, &closed),
                "unknown-named candidate indexes (honestly)");
        require(closed.verifiedCount == 0, "unknown named volume maps nothing");
        bool sawNamedRefusal = false;
        for (const QString& d : closed.diagnostics)
            sawNamedRefusal = sawNamedRefusal || d.contains(QStringLiteral("fail closed"));
        require(sawNamedRefusal, "named fail-closed diagnostic recorded");

        // A different series whose canonical list DOES carry "Special" maps it.
        const QString sid2 = QStringLiteral("mal:2");
        IndexerOutcome open;
        require(indexer.indexCandidate(snapshotWithVolumes(sid2, /*withNamedSpecial=*/true),
                                       candidateFor(named, QStringLiteral("Series Vol Special")),
                                       named.bytes, now, &open),
                "known-named candidate indexes");
        require(open.verifiedCount == 1, "known named volume maps");
        const QString vSpecial = MangaTankoban::volumeId(sid2, QStringLiteral("Special"));
        require(store.mappingsForVolume(vSpecial).size() == 1,
                "named mapping persisted for the series that knows the token");
        // And the fail-closed series still has no such row.
        require(store.mappingsForVolume(MangaTankoban::volumeId(sid, QStringLiteral("Special")))
                    .isEmpty(),
                "fail-closed series keeps no named row");
    }

    std::cout << "MANGA_TORRENT_INDEXER_OK\n";
    return 0;
}
