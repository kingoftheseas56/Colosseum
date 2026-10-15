// native/torrent/MangaTorrentIndexer.cpp — see header. [Agent 0 (GLM), Arc 18 M4]
#include "torrent/MangaTorrentIndexer.h"

#include "engine/MangaTankobanLogic.h" // volumeId()
#include "torrent/MangaVolumeIdentity.h" // the ONE shared volume-identity grammar

#include <QFileInfo>
#include <QSet>

namespace MangaTankoban {
namespace {

// Same accepted set as MangaVolumeFilePicker's gate (both are policy over the
// shared grammar; neither owns the other).
bool isComicArchive(const QString& name)
{
    static const QSet<QString> exts{
        QStringLiteral("cbz"), QStringLiteral("cbr"),
        QStringLiteral("cb7"), QStringLiteral("cbt")
    };
    return exts.contains(QFileInfo(name).suffix().toLower());
}

QString coverageKindText(MangaVolumeIdentity::CoverageKind kind)
{
    using K = MangaVolumeIdentity::CoverageKind;
    switch (kind) {
    case K::Single: return QStringLiteral("single");
    case K::Range: return QStringLiteral("range");
    case K::None: return QStringLiteral("none");
    }
    return QStringLiteral("none");
}

QString coverageSourceText(MangaVolumeIdentity::EvidenceSource source)
{
    using S = MangaVolumeIdentity::EvidenceSource;
    switch (source) {
    case S::Filename: return QStringLiteral("filename");
    case S::Directory: return QStringLiteral("directory");
    case S::ReleaseTitle: return QStringLiteral("release_title");
    case S::None: return QStringLiteral("none");
    }
    return QStringLiteral("none");
}

} // namespace

MangaTorrentIndexer::MangaTorrentIndexer(IMangaTorrentMetainfoResolver* resolver,
                                         MangaTorrentIndex* index)
    : m_resolver(resolver), m_index(index)
{
}

bool MangaTorrentIndexer::indexCandidate(const SeriesSnapshot& series,
                                         const MangaNyaaCandidate& candidate,
                                         const QByteArray& torrentBytes,
                                         qint64 nowMs,
                                         IndexerOutcome* outcome)
{
    IndexerOutcome local;
    IndexerOutcome& o = outcome ? *outcome : local;

    if (!m_resolver || !m_index || !m_index->isOpen()) {
        o.diagnostics << QStringLiteral("indexer not wired to a resolver + open store");
        return false;
    }

    // ── Metainfo decode + the candidate bridge ───────────────────────────────
    TorrentMetainfo meta;
    if (!m_resolver->resolve(torrentBytes, meta)) {
        o.diagnostics << QStringLiteral("metainfo undecodable: %1").arg(candidate.title);
        return false;
    }
    // verifyInfoHash is the ONLY bridge from a Nyaa row to stored identity. A
    // mismatched .torrent response must poison nothing (M2 adoption rule).
    if (!verifyInfoHash(candidate.infoHash, meta)) {
        o.diagnostics << QStringLiteral("infoHash mismatch for %1 — refusing to index")
                          .arg(candidate.title);
        return false;
    }

    // ── Torrent row: availability snapshot, identity stays keyed by hash ─────
    IndexedTorrent row;
    row.infoHash = meta.infoHash;
    row.provider = QStringLiteral("nyaa");
    row.providerId.clear();
    row.releaseTitle = candidate.title;
    row.uploader = candidate.uploader;
    row.totalSize = meta.totalSize;
    row.seeders = candidate.seeders;
    row.leechers = candidate.leechers;
    row.discoveredAt = candidate.discoveredAt;
    row.lastSeenAt = nowMs;
    if (!m_index->upsertTorrent(row)) {
        o.diagnostics << QStringLiteral("torrent row persist failed for %1").arg(meta.infoHash);
        return false;
    }

    // ── Raw file list (every entry, engine order — no filtering/re-numbering) ─
    QList<IndexedFile> files;
    files.reserve(meta.files.size());
    for (const TorrentMetainfoFile& mf : meta.files) {
        IndexedFile f;
        f.fileIndex = mf.index;
        f.path = mf.path;
        f.size = mf.size;
        f.archiveType = isComicArchive(mf.path)
                            ? QFileInfo(mf.path).suffix().toLower()
                            : QString();
        const MangaVolumeIdentity::VolumeCoverage cover =
            MangaVolumeIdentity::coverageForPath(mf.path);
        f.coverageKind = coverageKindText(cover.kind);
        f.coverageLo = cover.lo.canonical;
        f.coverageHi = cover.hi.canonical;
        f.coverageSource = coverageSourceText(cover.source);
        files.append(f);
    }
    if (!m_index->replaceFiles(meta.infoHash, files)) {
        o.diagnostics << QStringLiteral("file list persist failed for %1").arg(meta.infoHash);
        return false;
    }

    // ── Named-volume fail-closed gate (contract §7) ───────────────────────────
    // A named/special token maps only when the canonical series volume list
    // carries that exact folded token. The map also carries the RECORD's own
    // number, because the product key is volumeId(seriesId, recordNumber) —
    // the folded form is only for matching, never for the key itself.
    QHash<QString, QString> namedVolumeRecords; // folded token -> record number
    for (const VolumeRecord& v : series.volumes) {
        const QString folded = MangaVolumeIdentity::foldNamed(v.number);
        if (!folded.isEmpty() && !MangaVolumeIdentity::isNumericToken(v.number))
            namedVolumeRecords.insert(folded, v.number);
    }

    // ── Per-archive volume solving (Torrentio direction: file -> volume) ─────
    // First pass: collect every single-coverage archive per canonical volume.
    struct Hit {
        int fileIndex = -1;
        QString path;
        QString volumeKey; // number for volumeId(): canonical (numeric) or the record number (named)
        MangaVolumeIdentity::EvidenceSource source;
    };
    QHash<QString, QList<Hit>> perVolume; // canonical volume -> candidate hits
    for (const TorrentMetainfoFile& mf : meta.files) {
        if (!isComicArchive(mf.path))
            continue;
        const MangaVolumeIdentity::VolumeCoverage cover =
            MangaVolumeIdentity::coverageForPath(mf.path);
        if (cover.kind == MangaVolumeIdentity::CoverageKind::Range) {
            // Combined multi-volume archive: excellent discovery evidence,
            // never isolable file identity.
            o.rejectedCount++;
            o.diagnostics << QStringLiteral("combined archive refused: %1").arg(mf.path);
            continue;
        }
        if (cover.kind != MangaVolumeIdentity::CoverageKind::Single)
            continue; // no explicit volume evidence in this path — unmapped
        QString volumeKey = cover.lo.canonical;
        if (cover.lo.isNamed()) {
            const auto known = namedVolumeRecords.constFind(cover.lo.canonical);
            if (known == namedVolumeRecords.constEnd()) {
                o.rejectedCount++;
                o.diagnostics << QStringLiteral("named volume not in canonical list, "
                                                "fail closed: %1").arg(mf.path);
                continue;
            }
            volumeKey = known.value();
        }
        Hit hit;
        hit.fileIndex = mf.index;
        hit.path = mf.path;
        hit.volumeKey = volumeKey;
        hit.source = cover.source;
        perVolume[cover.lo.canonical].append(hit);
    }

    // Second pass: exactly one hit = a verified mapping; two or more = the
    // volume is ambiguous inside this torrent and NEITHER file maps.
    for (auto it = perVolume.constBegin(); it != perVolume.constEnd(); ++it) {
        const QString& canonical = it.key();
        const QList<Hit>& hits = it.value();
        if (hits.size() > 1) {
            o.rejectedCount++;
            QStringList paths;
            for (const Hit& h : hits)
                paths << h.path;
            o.diagnostics << QStringLiteral("ambiguous volume %1 (%2 equally exact)")
                              .arg(canonical, paths.join(QStringLiteral(", ")));
            continue;
        }
        const Hit& hit = hits.first();
        const MappingEvidence evidence =
            (hit.source == MangaVolumeIdentity::EvidenceSource::Directory)
                ? MappingEvidence::MetainfoExactDirectory
                : MappingEvidence::MetainfoExactFilename;
        VolumeMapping mapping;
        mapping.volumeId = volumeId(series.seriesId, hits.first().volumeKey);
        mapping.infoHash = meta.infoHash;
        mapping.fileIndex = hit.fileIndex;
        mapping.evidence = evidence;
        mapping.verifiedAt = nowMs;
        mapping.parserVersion = QString::fromLatin1(MangaTorrentIndex::kParserVersion);
        mapping.status = MappingStatus::Verified;
        if (!m_index->insertMapping(mapping)) {
            o.diagnostics << QStringLiteral("mapping persist failed for volume %1")
                              .arg(canonical);
            return false;
        }
        o.verifiedCount++;
        o.diagnostics << QStringLiteral("verified volume %1 -> file %2 (%3)")
                          .arg(canonical)
                          .arg(hit.path)
                          .arg(hit.source == MangaVolumeIdentity::EvidenceSource::Directory
                                   ? QStringLiteral("directory")
                                   : QStringLiteral("filename"));
    }

    return true;
}

} // namespace MangaTankoban
