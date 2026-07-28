#pragma once

// Durable, atomic on-disk index of downloaded Tankoban volumes.
//
// One volume = one canonical CBZ plus an atomic recovery sidecar and one row in
// a single JSON ledger written with QSaveFile, so a crash mid-write can never
// corrupt the ledger (the temp file is renamed into place or discarded whole).
// The ledger row carries the volume's canonical id, series metadata, source
// provenance (nyaa infohash / weebcentral chapter ids, uploader, release title),
// the naturally-ordered page filenames, per-page chapter-group ordinals, the
// payload byte count and the added-time.
//
// localPages() returns direct archive descriptors:
//   [{index, archive, entry, group}]
// The comic reader decodes these entries without extracting them.
//
// Layout (root is injected — the app passes AppDataLocation, tests a temp dir):
//   <root>/manga-volumes/volume-index.json          (the ledger)
//   <root>/manga-volumes/archives/<series>/vol-<n>-<hash>.cbz
//   <root>/manga-volumes/archives/<series>/vol-<n>-<hash>.cbz.json
//
// Self-heal doctrine (repair-then-prune): sidecars and legacy per-volume
// manifests repair the ledger first. Valid loose legacy payloads migrate to CBZ;
// only an unrecoverable lookup row is pruned, never unvalidated payload bytes.

#include "engine/MangaTankobanTypes.h"
#include "engine/DownloadFileOps.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace MangaTankoban {

// Source provenance for one volume. `id` is the canonical MangaTankoban::volumeId.
// `infoHash` is set for nyaa sources; `chapterIds` for weebcentral. The remaining
// fields are display / audit data lifted from the chosen source row.
struct VolumeProvenance {
    QString id;
    QString seriesId;
    QString seriesTitle;
    QString volumeNumber;
    QString sourceKind;      // "nyaa" | "weebcentral"
    QString releaseTitle;
    QString uploader;        // nyaa
    QString infoHash;        // nyaa
    QStringList chapterIds;  // weebcentral
};

class MangaVolumeIndex : public QObject
{
    Q_OBJECT
public:
    // rootDir is injected so the ledger never touches real AppData in tests.
    explicit MangaVolumeIndex(const QString& rootDir, QObject* parent = nullptr);
    MangaVolumeIndex(const QString& rootDir, DownloadFileOps::Remover treeRemover,
                     QObject* parent = nullptr);

    // <rootDir>/manga-volumes — the base for the ledger and all page dirs.
    QString baseDir() const { return m_baseDir; }

    // Deterministic pages directory for a record. The single source of the
    // on-disk layout truth, so the ingestor and the index agree on where a
    // volume's pages live.
    QString pagesDirFor(const VolumeProvenance& record) const;
    QString archivePathFor(const VolumeProvenance& record) const;

    // Atomically record a finalized volume. `finalDir` already holds the
    // page_NNN files; `orderedFiles` are those names in natural page order;
    // `groups` the per-page chapter-group ordinal (0 for a single source);
    // `bytes` the payload byte count. Overwrites any prior row for record.id.
    bool publish(const VolumeProvenance& record, const QString& finalDir,
                 const QStringList& orderedFiles, const QList<int>& groups, qint64 bytes);
    bool publishArchive(const VolumeProvenance& record, const QString& archivePath,
                        const QStringList& orderedFiles, const QList<int>& groups,
                        qint64 bytes);

    // Reader shape: one map per page {index, archive, entry, group}, index
    // ascending from 0. Returns [] unless the archive and ordered members verify.
    Q_INVOKABLE QVariantList localPages(const QString& volumeId) const;

    // {state: "none"|"ready", progress, + provenance/dir/pages/bytes/addedAt}.
    // File-aware: a row whose dir OR any recorded page file is missing reports
    // "none" (never a silent broken-ready), so it agrees with localPages without
    // any caller having to run heal() first.
    Q_INVOKABLE QVariantMap statusOf(const QString& volumeId) const;

    // Every published volume as a Downloads-page row: {id, seriesId, seriesTitle,
    // label "Vol. N", pages, bytes, addedAt, missing, art (file:// URL of the
    // first page — the volume's own honest local cover)}. Mirrors
    // MangaDownloader::downloadedChapters so LocalDownloads composes both lanes
    // with one shape.
    Q_INVOKABLE QVariantList downloadedVolumes() const;

    // Delete the CBZ, recovery sidecar, and ledger row. Legacy loose directories
    // remain supported only so pre-migration data can be retired safely.
    Q_INVOKABLE bool remove(const QString& volumeId);

    // Re-read the ledger from disk (proves atomic persistence across a restart).
    void reload();

    // Reconcile sidecars, migrate legacy loose pages, then prune only
    // unrecoverable ledger rows.
    void heal();

signals:
    void changed();

private:
    struct Entry {
        QString seriesId;
        QString seriesTitle;
        QString volumeNumber;
        QString dir;
        QString archive;
        QStringList files;
        QList<int> groups;
        qint64 bytes = 0;
        QString sourceKind;
        QString releaseTitle;
        QString uploader;
        QString infoHash;
        QStringList chapterIds;
        qint64 addedAt = 0;
    };

    QString indexPath() const;
    void load();
    bool save() const;
    bool entryIntact(const Entry& e) const;
    bool writeSidecar(const QString& id, const Entry& e) const;
    bool reconcileSidecar(const QString& id, Entry& e) const;
    bool migrateLegacy(const QString& id, Entry& e);

    QString m_baseDir;
    DownloadFileOps::Remover m_treeRemover;
    QHash<QString, Entry> m_index;
};

} // namespace MangaTankoban
