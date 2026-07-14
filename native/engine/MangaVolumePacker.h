#pragma once

// WeebCentral volume fallback packer for Tankoban volume mode.
//
// When a tankobon has no single-archive (nyaa) source, this packer synthesizes
// the volume from its constituent WeebCentral chapters. For each chapterId in the
// VolumeRecord (in order) it asks a MangaScraper for that chapter's page image
// URLs, downloads every image through a QNetworkAccessManager, validates each as a
// real image (magic-byte sniff), and lays them out in a staging directory named
//
//     c<chapterNumber:03d>_<pageInChapter:03d>.<ext>
//
// (chapter order, then natural page order). Each page is tagged with its chapter's
// 0-based ordinal within the volume (its "group"). Once EVERY chapter and page is
// present and valid, the prepared directory is handed to the shared
// MangaVolumeArchiveIngestor::publish() path together with the per-page group
// vector, so a WeebCentral volume lands in MangaVolumeIndex with the SAME
// canonical "ready" shape a nyaa volume does. A missing/failed chapter or page, or
// a cancel(), tears down the staging directory and NEVER publishes a ready volume
// (complete() stays false): a partial fallback is never presented as complete.
//
// Chapter number: WeebCentral chapter ids are opaque ULIDs. The number is parsed
// from the id only when it cleanly carries a trailing numeric run at a
// non-alphanumeric (or start) boundary ("wc-chapter-10" -> 10); an opaque id falls
// back to the chapter's 1-based volume ordinal, so pages stay ordered and uniquely
// named regardless. Reading order and chapter-group boundaries come from the
// chapter order + the group vector, never from the label — so a ULID-only volume
// still reads correctly.

#include "engine/MangaResult.h"        // PageInfo
#include "engine/MangaTankobanTypes.h" // VolumeRecord

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>

class MangaScraper;
class QNetworkAccessManager;
class QNetworkReply;

namespace MangaTankoban {

class MangaVolumeIndex;
class MangaVolumeArchiveIngestor;

class MangaVolumePacker : public QObject
{
    Q_OBJECT
public:
    // rootStaging is injected so the packer never touches real AppData in tests.
    MangaVolumePacker(MangaScraper* scraper, QNetworkAccessManager* nam,
                      MangaVolumeIndex* index, const QString& stagingRoot,
                      QObject* parent = nullptr);
    ~MangaVolumePacker() override;

    // Async. Synthesize `volume` from its chapter page fetches + image downloads,
    // then publish it. `seriesTitle` is the SERIES snapshot title (not the volume
    // title) and is recorded as the published volume's provenance seriesTitle.
    // Progress/finished/failed report the run. Only ONE pack runs at a time: a
    // second call while a pack is active is queued and started when the active job
    // reaches a terminal state, so concurrent volumes never share the scraper's
    // uncorrelated pagesReady emit.
    void pack(const VolumeRecord& volume, const QString& seriesTitle);

    // Abort the in-flight pack for this volumeId: cancels every pending reply and
    // removes the staging dir. No ready volume is published.
    void cancel(const QString& volumeId);

    // Deterministic staging directory for a record (under stagingRoot). Exposed so
    // a caller/test can observe staging creation and teardown.
    QString stagingDirFor(const VolumeRecord& volume) const;

    // True iff the shared index currently reports a "ready" volume for this record.
    bool complete(const VolumeRecord& volume) const;

    // Packer-observable results of the most recent successful pack: the staging
    // page names in save order (chapter, then page) and the parallel per-page
    // chapter-group ordinals.
    QStringList lastSavedNames() const { return m_lastSavedNames; }
    QList<int>  lastGroups() const { return m_lastGroups; }

signals:
    void progress(const QString& volumeId, int done, int total);
    void finished(const QString& volumeId, const QString& preparedDirectory);
    void failed(const QString& volumeId, const QString& reason);

private:
    struct Job {
        VolumeRecord volume;
        QString      seriesTitle;      // series snapshot title → provenance seriesTitle
        QString      volumeId;
        QString      stagingDir;
        int          chapterIdx = 0;  // chapter currently being fetched/downloaded
        int          done = 0;        // pages validated so far (progress numerator)
        int          knownTotal = 0;  // pages of chapters fetched so far (denominator)
        QStringList  savedNames;      // staging names in save order (chapter, page)
        QList<int>   groups;          // parallel per-page chapter-group ordinal
        QSet<QNetworkReply*> replies; // in-flight downloads (for cancel/teardown)
        bool         cancelled = false;
        QMetaObject::Connection pagesConn;
    };

    void startChapter(const std::shared_ptr<Job>& job);
    void onPages(const std::shared_ptr<Job>& job, const QList<PageInfo>& pages);
    void finalize(const std::shared_ptr<Job>& job);
    void failJob(const std::shared_ptr<Job>& job, const QString& reason);
    void teardown(const std::shared_ptr<Job>& job);
    // Pop the next queued job into m_job and start it, or reset m_job when the
    // queue is empty. Called on every terminal transition of the active job.
    void advanceQueue();

    static QString chapterLabel(const QString& chapterId, int ordinal1Based);
    static QString extFor(const QString& imageUrl, const QByteArray& bytes);
    static bool    looksLikeImage(const QByteArray& bytes);

    MangaScraper*               m_scraper = nullptr;
    QNetworkAccessManager*      m_nam = nullptr;
    MangaVolumeIndex*           m_index = nullptr;
    MangaVolumeArchiveIngestor* m_ingestor = nullptr;
    QString                     m_stagingRoot;
    std::shared_ptr<Job>        m_job;   // single active pack; only one runs at a time
    QList<std::shared_ptr<Job>> m_queue; // packs waiting for the active one to finish
    QStringList                 m_lastSavedNames;
    QList<int>                  m_lastGroups;
};

} // namespace MangaTankoban
