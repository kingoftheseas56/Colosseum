#pragma once

// Lossless archive → volume ingestion for Tankoban volume mode.
//
// A downloaded cbz/cbr/cb7/cbt (the nyaa transport's payload) is extracted by
// the OS archiver, its pages are naturally ordered and renamed page_NNN, then
// the whole payload is ATOMICALLY moved into place and published into the shared
// MangaVolumeIndex. Images are never recompressed — only moved/renamed. A
// partial or failed extraction never publishes a ready volume.
//
// This is a focused fork of ComicDownloader's proven extraction lifecycle
// (bsdtar-first, 7-Zip fallback, recursive QCollator natural sort, page_%03d
// naming). ComicDownloader stays untouched; the manga path needs the volume
// provenance + the two-phase staging→final atomic rename, so the lifecycle is
// lifted here rather than shared through the western-comics object.
//
// publish() is the WeebCentral packer's entry (Task 7): an already-prepared
// loose-image directory is finalized the same way, minus the extraction step.

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

    // Async. Extract `archivePath`, natural-sort, atomically publish into the
    // index, THEN delete the source archive. Emits finished(volumeId) on success
    // or failed(volumeId, reason). Queues if a job is already running.
    void ingestArchive(const VolumeProvenance& record, const QString& archivePath);

    // Sync. Publish an already-prepared loose-image directory (no archive step,
    // same finalize + atomic-rename + publish). Consumes (removes) preparedDir.
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
    void runExtractor(int which);          // 0 = bsdtar, 1 = 7-Zip
    void onExtractDone(int exitCode, int which);
    void finishActiveSuccess();
    void failActive(const QString& reason);

    // Shared finalize: collect images recursively from sourceDir, validate at
    // least one decodable image, natural-sort (QCollator numeric, case-insensitive),
    // move them into a fresh staging dir as page_NNN, write the per-volume
    // index.json, atomically rename staging → final, and publish into the index.
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
