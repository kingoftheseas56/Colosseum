#pragma once
// VaultEnricher — fills rows with honest facts, progressively, after the census
// (Slice 5). Deliberately THIN: it reuses Colosseum's existing archive + cover
// facilities instead of re-porting Tankoban 2's ArchiveReader —
//   comics: CbzArchive lists the pages; the cover ENTRY name it picks is served
//           on demand by the existing image://comiccover/ provider (no new
//           decoder, no thumbnail cache — Qt's image cache already memoises);
//   video:  ffprobe (kill-on-timeout), memoised in a triple-keyed duration cache;
//   books:  format from the extension, plus bounded EPUB OPF metadata/cover
//           extraction when the file is an EPUB.
// Non-EPUB books remain filename-honest; video thumbnails and page dimensions
// stay deferred. Enrichment is per-file and cancellable; the duration cache
// flushes every 20 files.

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "VaultIndex.h" // FileRow

namespace VaultKit { struct CancellationToken; }

class VaultEnricher : public QObject
{
    Q_OBJECT

public:
    explicit VaultEnricher(VaultIndex* index, QString cacheDir, QObject* parent = nullptr);

    // ── Comic facts (pure/static; reuse CbzArchive) ──
    // Pick the cover entry: a `cover.*` / `folder.*` basename wins, else the
    // first image in natural order.
    static QString pickCoverEntry(const QStringList& imageEntryNames);
    struct ComicFacts {
        bool ok = false;   // false for a corrupt/unreadable archive (honest error, never a wedge)
        int pages = 0;
        QString coverEntry;
        QString errorDetail;
    };
    static ComicFacts readComicFacts(const QString& cbzPath);

    // â”€â”€ EPUB book facts (pure/static; miniz + OPF XML) â”€â”€
    struct BookFacts {
        bool ok = false; // valid EPUB container/OPF, even when it has no cover
        QString title;
        QString author;
        QString synopsis;
        QString coverEntry;
        QString errorDetail;
    };
    static BookFacts readBookFacts(const QString& epubPath);

    // ── Local artwork adoption (browse-face execution plan, Slice 3) ──
    // Adopt by CONVENTION ONLY: a poster./folder./cover. basename (jpg/jpeg/png,
    // case-insensitive) sitting directly in a video group's own folder — never
    // "any image in the folder". A release-site junk image (e.g. the real
    // "www.YTS.MX.jpg" found in Hemanth's library) is refused by construction: it
    // simply never matches a conventional basename, no denylist needed. The
    // candidate is also verified as a genuine, decodable image (QImageReader)
    // before adoption, so a corrupt/truncated file is refused honestly instead of
    // handing QML a ref that only fails later (the no-art contract: never a
    // broken-image glyph). Returns a "file://" ref — namespaced, distinct from the
    // shipped comic/book "image://comiccover/" and "image://vaultbookcover/"
    // refs, which carry a bare in-archive entry name instead of a URL — or ""
    // when no valid conventional art exists. Pure/static; no DB, no network.
    static QString findLocalArtwork(const QString& folderPath);

    // ── Video duration cache ──
    double durationForVideo(const QString& path, qint64 size, qint64 mtimeMs); // cache-then-probe
    double cachedDuration(const QString& path, qint64 size, qint64 mtimeMs) const; // hit, or -1
    void putDuration(const QString& path, qint64 size, qint64 mtimeMs, double sec);
    void saveDurationCache();
    static double probeDurationSec(const QString& path); // ffprobe, kill-on-timeout
    static QString findFfprobe();

    // ── Orchestration ──
    void enrich(const QList<VaultIndex::FileRow>& rows,
                const VaultKit::CancellationToken* cancel = nullptr);

signals:
    void progress(int done, int total);
    void enrichmentFinished();

private:
    void loadDurationCache();
    // Commit the enriched batch on the VaultIndex OWNER thread (never a worker-thread QSqlDatabase
    // write), then emit enrichmentFinished(). Same-thread callers commit inline.
    void commitRowsOnIndexThread(QList<VaultIndex::FileRow> rows);

    VaultIndex* m_index;
    QString m_cacheDir;
    QHash<QString, double> m_durationCache;
};
