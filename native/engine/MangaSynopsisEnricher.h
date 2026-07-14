#pragma once

// Lazy, source-honest per-volume synopsis enrichment for Tankoban "volume mode".
//
// Canonical volume rendering NEVER waits on this: every fetch is async/lazy and
// the enricher only augments a volume with a synopsis once it has genuine,
// target-volume evidence. Two providers, tried in order per uncached volume:
//   1. Open Library  — search by series+volume; a doc is accepted only on a
//      normalized series-title + explicit target-volume match. A matched edition
//      carrying a valid English-registration-group ISBN is stamped "exact-isbn";
//      a title+volume-only match is "exact-title-volume".
//   2. Apple Books   — iTunes Search (entity=ebook); a result is accepted only on
//      strong series agreement + an explicit target volume. Author agreement
//      breaks a near-tie; two equally-strong candidates with no distinguishing
//      author signal are LEFT EMPTY (never guessed).
//
// The enricher must never emit the SERIES synopsis (or another volume's text) as
// a volume synopsis — every accepted text is gated through acceptDistinctVolumeText.
//
// Concern split (so the matching is harness-testable with no I/O):
//   * matchOpenLibrary / matchApple / acceptDistinctVolumeText are pure static
//     functions over (series, volume, json) or two strings. They do all the
//     honesty work and are the pinned contract.
//   * enrichSeries owns the async cascade: cache lookup, Open Library first, a
//     throttled single-flight Apple fallback, cache writes and synopsisReady.
//
// Cache: JSON schema version 1, one SynopsisRecord per volumeId, persisted
// atomically with QSaveFile. A cache MISS (a valid response that yielded nothing
// acceptable) is retained 24h so we don't re-hammer; an ACCEPTED record 30 days.
// A network FAILURE records no permanent negative.

#include "engine/MangaTankobanTypes.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;

namespace MangaTankoban {

// One durable synopsis result for a single volume. `accepted == false` with an
// empty text/source is an honest MISS (kept so the cascade doesn't re-hammer).
struct SynopsisRecord {
    QString volumeId;
    QString text;
    QString source;      // "openlibrary" | "apple" | ""
    QString sourceUrl;
    QString confidence;  // "exact-isbn" | "exact-title-volume" | "none"
    QString fetchedAt;   // ISO-8601 UTC
    bool accepted = false;
};

class MangaSynopsisEnricher : public QObject
{
    Q_OBJECT
public:
    // `nam` may be null for cache-only use (the pure matchers and the cache API
    // never touch the network). `cachePath` is the JSON cache file; it is loaded
    // on construction and rewritten atomically on every cacheRecord().
    explicit MangaSynopsisEnricher(QNetworkAccessManager* nam,
                                   const QString& cachePath,
                                   QObject* parent = nullptr);
    ~MangaSynopsisEnricher() override;

    // ── Pure matching (no I/O; the pinned honesty contract) ────────────────
    static SynopsisRecord matchOpenLibrary(const SeriesSnapshot& series,
                                           const VolumeRecord& volume,
                                           const QByteArray& json);
    static SynopsisRecord matchApple(const SeriesSnapshot& series,
                                     const VolumeRecord& volume,
                                     const QByteArray& json);
    // False when `candidateText` is empty or normalizes (case/space/punct-
    // insensitive) equal to `compareText`. Used both against the series synopsis
    // and against already-accepted sibling-volume text.
    static bool acceptDistinctVolumeText(const QString& candidateText,
                                         const QString& compareText);

    // ── Cache (schema v1, one record per volumeId, atomic writes) ──────────
    SynopsisRecord cached(const QString& volumeId) const;
    void cacheRecord(const SynopsisRecord& record); // insert/replace + persist

    // ── Async, lazy cascade — NEVER blocks canonical volume rendering ──────
    // For each uncached (or stale) volume: Open Library first, then a throttled
    // Apple fallback. Emits synopsisReady incrementally as accepted results land.
    void enrichSeries(const SeriesSnapshot& series, const QString& seriesSynopsis = QString());

signals:
    void synopsisReady(const QString& volumeId, const MangaTankoban::SynopsisRecord& record);

private:
    // Per-in-flight-volume context carried across the async cascade.
    struct Job {
        QString volumeId;
        SeriesSnapshot series;
        VolumeRecord volume;
        QString seriesSynopsis;
    };

    void loadCache();
    void saveCache();
    bool isFresh(const SynopsisRecord& rec) const;
    // Accept text only if distinct from the series synopsis AND every sibling
    // volume's already-accepted text.
    bool textIsDistinct(const Job& job, const QString& text) const;
    void noteAccepted(const QString& seriesId, const QString& text);

    void startOpenLibrary(const Job& job);
    void enqueueApple(const Job& job);
    void scheduleApplePump();
    void startNextApple();

    QPointer<QNetworkAccessManager> m_nam;
    QString m_cachePath;
    QHash<QString, SynopsisRecord> m_cache;            // volumeId -> record
    QHash<QString, QStringList> m_acceptedTextsBySeries; // seriesId -> accepted texts

    QQueue<Job> m_appleQueue;
    bool m_appleInFlight = false;
    QDateTime m_lastAppleStart;
};

} // namespace MangaTankoban

Q_DECLARE_METATYPE(MangaTankoban::SynopsisRecord)
