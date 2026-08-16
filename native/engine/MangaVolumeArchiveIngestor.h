#pragma once

// Lossless archive → volume ingestion for Tankoban volume mode.
//
// A downloaded CBZ/ZIP is validated, copied atomically into canonical storage,
// and published without extraction. Other supported comic archives use the OS
// archiver only as a conversion step, then become a validated canonical CBZ.
// Images are never recompressed. A partial or failed conversion never publishes
// a ready volume.
//
// This is a focused fork of ComicDownloader's proven extraction lifecycle
// (bsdtar-first, 7-Zip fallback, recursive QCollator natural sort, page_%03d
// naming). ComicDownloader stays untouched; the manga path needs the volume
// provenance + the two-phase staging→final atomic rename, so the lifecycle is
// lifted here rather than shared through the western-comics object.
//
// publish() is the WeebCentral packer's entry: its temporary downloaded images
// are naturally ordered and packed into one canonical CBZ before publication.

#include "engine/MangaVolumeIndex.h"

#include <QObject>
#include <QQueue>
#include <QString>
#include <QVector>

class QProcess;

namespace MangaTankoban {

class MangaVolumeArchiveIngestor : public QObject
{
    Q_OBJECT
public:
    explicit MangaVolumeArchiveIngestor(MangaVolumeIndex* index, QObject* parent = nullptr);
    ~MangaVolumeArchiveIngestor() override;

    // Async. Preserve CBZ/ZIP inputs directly; convert other supported formats;
    // atomically publish, THEN delete the source archive.
    void ingestArchive(const VolumeProvenance& record, const QString& archivePath);

    // Sync. Pack an already-prepared loose-image directory into canonical CBZ.
    // Consumes (removes) preparedDir after successful publication.
    // Returns true on success; a failure also emits failed(volumeId, reason).
    // `groups` carries a per-page chapter-group ordinal in natural-sorted page
    // order (Task 7's WeebCentral packer hands over a multi-chapter volume). It is
    // honored only when its size equals the final page count; otherwise every page
    // falls in group 0 (the single-archive default).
    bool publish(const VolumeProvenance& record, const QString& preparedDir,
                 const QVector<int>& groups = {});

signals:
    void finished(const QString& volumeId);
    void failed(const QString& volumeId, const QString& reason);

private:
    struct Job {
        VolumeProvenance record;
        QString archivePath;
        QString extractTmp;   // scratch dir for raw extraction output
    };

    void startNext();
    // CBZ/ZIP fast path, retried on transient open failures of the source
    // archive (flush race — see the constants block in the .cpp).
    void validateAndAdoptCbz(int attempt);
    void runExtractor(int which);          // 0 = bsdtar, 1 = 7-Zip
    void onExtractDone(int exitCode, int which);
    void finishActiveSuccess();
    void failActive(const QString& reason);

    // Shared finalize: collect images recursively, validate, natural-sort,
    // write a canonical CBZ through a same-directory .part file, reopen it,
    // publish its sidecar + ledger row, and leave no loose page payload.
    // Returns an empty string on success, otherwise a failure reason. Never
    // deletes sourceDir (the caller owns that). `groups` (per-page, natural-sorted
    // order) is stored when its size matches the final page count; otherwise every
    // page defaults to group 0.
    QString finalizeInto(const VolumeProvenance& record, const QString& sourceDir,
                         const QVector<int>& groups = {});

    MangaVolumeIndex* m_index = nullptr;
    QProcess* m_proc = nullptr;
    Job* m_active = nullptr;
    QQueue<Job> m_queue;
};

} // namespace MangaTankoban
