#pragma once

// Manga-specific Nyaa volume discovery for Tankoban "volume mode".
//
// PORTED from Tankoban 2's proven core/manga/NyaaRuntimeSource: the query
// family, the namespace-aware RSS parse, the uploader-trust tiers, the
// volume-coverage matcher and the stable tier/seeder ranking are retained
// faithfully. It is EXTENDED with the derived fields the volume-mode QML needs
// (string coverage-range bounds, standalone/digital hints) and the rejection
// filters TB2 lacked (chapter-pack, wrong-target, raw/untranslated, weak
// series-title match, hash-less rows).
//
// This remains a manga-specific source rather than a generic
// torrent/TankorentSearchService indexer: the Worker returns Nyaa metadata, but
// uploader trust and volume filtering are local manga decisions. Torrent bytes
// are still fetched and consumed by Colosseum's native torrent path.
//
// Concern split (intentional, so the pure logic is harness-testable):
//   * queryVariants is pure and trust-agnostic. The optional parseRss helper is
//     compiled only for legacy logic fixtures; production discovery receives
//     normalized JSON from the Nyaa Worker.
//   * filterAndRank is where every trust-dependent and volume-target decision
//     happens: uploader inference, tier tagging, blocked drop, coverage/target
//     match, chapter-pack / raw / weak-match / hash-less rejection, dedup by
//     infohash, and the advisory ordering. TB2 did target filtering inside the
//     batched-reply merge; we lift it into one pure function instead.

#include "engine/MangaTankobanTypes.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace MangaTankoban {

// One Nyaa search-result candidate. The Sources panel renders one row per
// candidate. The fields beyond TB2's original set — coverage bounds, standalone
// and digital hints — are derived by parseRss; `tier` is assigned by
// filterAndRank once the trust table is known.
struct MangaNyaaCandidate {
    QString title;        // full Nyaa title string
    QString uploader;
    QString magnetUri;
    QString infoHash;     // 40-char lowercase hex
    QString coverageLo;   // volume-range lower bound as STRING (e.g. "2", "1")
    QString coverageHi;   // volume-range upper bound as STRING (e.g. "2", "12")
    qint64  sizeBytes   = 0;
    int     seeders     = 0;
    int     leechers    = 0;
    int     tier        = 99;    // 1 / 2 / 99 (untrusted); set in filterAndRank
    bool    standalone  = false; // coverageLo == coverageHi (a single volume)
    bool    digitalHint = false; // title advertises a digital/official edition
    // ── Arc 18 M2 discovery provenance (identity/indexing inputs) ──
    QString torrentUrl;   // validated Nyaa .torrent URL used by native metainfo fetch
    QString query;        // which query variant found this candidate
    qint64  discoveredAt = 0;    // epoch ms when discovery merged this row
};

// Lower-cased uploader trust sets. The harness builds one inline; the instance
// path loads it from the embedded manga_uploader_trust.json.
struct TrustTable {
    QSet<QString> tier1;
    QSet<QString> tier2;
    QSet<QString> blocked;
};

// Tankoban 2 query family — `"<series> <vol>"`, 2- and 3-zero-padded forms,
// `"<series> Vol <vol>"`, and the bare series title — de-duped and simplified.
// A free, stateless function so both the volume-mode UI and the harness can call
// it without constructing a MangaNyaaSource.
QStringList queryVariants(const QString& seriesTitle, const QString& volumeNumber);

class MangaNyaaSource : public QObject
{
    Q_OBJECT
public:
    explicit MangaNyaaSource(QNetworkAccessManager* nam, QObject* parent = nullptr);
    ~MangaNyaaSource() override;

#ifdef COLOSSEUM_MANGA_NYAA_TESTING
    // Test-only injection seam. Production builds expose only the fixed Worker
    // endpoint and deadline through the constructor above.
    MangaNyaaSource(QNetworkAccessManager* nam, const QUrl& workerEndpoint,
                    int deadlineMs, QObject* parent = nullptr);
#endif

    // Worker results and the native metainfo fetcher both enforce this exact
    // Nyaa download URL shape before allowing native torrent retrieval.
    static bool isValidTorrentUrl(const QString& raw);

    // Optional fixture-only parser kept for the existing RSS logic fixtures.
#ifdef COLOSSEUM_MANGA_NYAA_RSS_FIXTURES
    static QList<MangaNyaaCandidate> parseRss(const QByteArray& payload);
#endif

    // Pure filter + rank. Rejects chapter packs, wrong-target / no-coverage rows,
    // blocked uploaders, raw/untranslated releases, weak series-title matches and
    // hash-less rows; dedups by infohash; assigns tiers; orders advisory-only
    // (trusted tier, standalone-before-pack, digital hint, seeders, title).
    // `seriesMode` (catalogue-independence Slice 4, 2026-08-20, default false —
    // every existing volume-targeted call is byte-identical): when true, the
    // coverage/target match is SKIPPED entirely — a shelf-less series has no
    // volume to target, so the series-level "Search nyaa" picker wants every
    // release that matches the series, not one that covers a specific number.
    // Trust tiers and every rejection filter (chapter-pack, raw, weak-match,
    // hash-less, blocked uploader, dedup) still apply unchanged.
    static QList<MangaNyaaCandidate> filterAndRank(const SeriesSnapshot& series,
                                                   const QString& targetVolume,
                                                   const QList<MangaNyaaCandidate>& parsed,
                                                   const TrustTable& trust,
                                                   bool seriesMode = false);

    // Fire one bounded Worker request per query in the family. Results land on
    // searchSucceeded keyed by MangaTankoban::volumeId(series.seriesId,
    // targetVolume); a failure with no results lands on searchFailed.
    void search(const SeriesSnapshot& series, const QString& targetVolume);

    // Abort all in-flight query requests. Destruction performs the same cleanup.
    void cancel();

    // Series-level search (catalogue-independence Slice 4): no volume target at
    // all, for a shelf-less series' "Search nyaa" entry. Reuses queryVariants/
    // empty volume number naturally falls to the bare-title query family;
    // filterAndRank runs in series mode. Results land on
    // searchSucceeded keyed by series.seriesId verbatim — the caller (the
    // façade) supplies whatever opaque key it wants results grouped under.
    void searchSeries(const SeriesSnapshot& series);

signals:
    void searchSucceeded(const QString& volumeId,
                         const QList<MangaTankoban::MangaNyaaCandidate>& rows);
    void searchFailed(const QString& volumeId, const QString& reason);

private:
    struct PendingSearch {
        QString volumeId;
        SeriesSnapshot series;
        QString targetVolume;
        bool seriesMode = false;
        int pendingReplies = 0;
        QList<MangaNyaaCandidate> parsed;
        QStringList errors;
    };

    struct ReplyState {
        QString volumeId;
        QString query;
        QPointer<QNetworkReply> reply;
        QPointer<QTimer> deadline;
        QByteArray body;
        bool tooLarge = false;
    };

    void loadTrustResource();
    // Shared by search()/searchSeries(): fires the bounded Worker query family,
    // tracks each reply under `vid`, then ranks with the right mode.
    void startSearch(const QString& vid, const SeriesSnapshot& series,
                     const QString& targetVolume, bool seriesMode);
    void readAvailable(QNetworkReply* reply);
    void onReplyFinished(QNetworkReply* reply);
    void onReplyDestroyed(QNetworkReply* reply);
    void failReply(QNetworkReply* reply, const QString& reason);
    void releaseReply(QNetworkReply* reply, bool abort);
    void completeQuery(const QString& vid, const QList<MangaNyaaCandidate>& rows,
                       const QString& error = {});
    void completeSearch(const QString& vid);

    QPointer<QNetworkAccessManager> m_nam;
    QUrl m_workerEndpoint;
    int m_deadlineMs = 20000;
    TrustTable m_trust;
    QHash<QString, PendingSearch> m_pending; // keyed by volumeId
    QHash<QNetworkReply*, ReplyState> m_replies; // one bounded response per query
};

} // namespace MangaTankoban

Q_DECLARE_METATYPE(MangaTankoban::MangaNyaaCandidate)
