#pragma once

// Manga-specific Nyaa RSS volume discovery for Tankoban "volume mode".
//
// PORTED from Tankoban 2's proven core/manga/NyaaRuntimeSource: the query
// family, the namespace-aware RSS parse, the uploader-trust tiers, the
// volume-coverage matcher and the stable tier/seeder ranking are retained
// faithfully. It is EXTENDED with the derived fields the volume-mode QML needs
// (string coverage-range bounds, standalone/digital hints) and the rejection
// filters TB2 lacked (chapter-pack, wrong-target, raw/untranslated, weak
// series-title match, hash-less rows).
//
// This is a DELIBERATE fork, not a reuse of Colosseum's generic
// torrent/TankorentSearchService. That service explicitly DROPPED Nyaa and its
// generic result type carries no uploader metadata, so trust-ranked volume
// discovery cannot live there. The Nyaa behaviour is kept manga-side, here,
// leaving the generic torrent path untouched. Torrent payloads are later handed
// to Colosseum's one shared TorrentEngine — out of scope for this source.
//
// Concern split (intentional, so the pure logic is harness-testable):
//   * queryVariants / parseRss are pure and trust-agnostic. parseRss takes only
//     the RSS bytes (the pinned harness contract) and therefore does NO trust
//     tagging and NO target filtering — it just extracts fields and derives the
//     coverage/standalone/digital data straight from each title.
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

class QNetworkAccessManager;
class QNetworkReply;

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

    // Pure, trust-agnostic RSS parse. Returns candidates in document order with
    // the coverage/standalone/digital fields derived from each title; tier is
    // left at its default and no filtering is applied (see filterAndRank).
    static QList<MangaNyaaCandidate> parseRss(const QByteArray& payload);

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

    // Fire an RSS search for one volume across the query family. Results land on
    // searchSucceeded keyed by MangaTankoban::volumeId(series.seriesId,
    // targetVolume); a failure with no results lands on searchFailed.
    void search(const SeriesSnapshot& series, const QString& targetVolume);

    // Series-level search (catalogue-independence Slice 4): no volume target at
    // all, for a shelf-less series' "Search nyaa" entry. Reuses queryVariants/
    // parseRss unchanged (an empty volume number naturally falls to the bare-
    // title query family); filterAndRank runs in series mode. Results land on
    // searchSucceeded keyed by series.seriesId verbatim — the caller (the
    // façade) supplies whatever opaque key it wants results grouped under.
    void searchSeries(const SeriesSnapshot& series);

signals:
    void searchSucceeded(const QString& volumeId,
                         const QList<MangaTankoban::MangaNyaaCandidate>& rows);
    void searchFailed(const QString& volumeId, const QString& reason);

private slots:
    void onReplyFinished();

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

    void loadTrustResource();
    // Shared by search()/searchSeries(): fires the query family, tracks the
    // pending replies under `vid`, and (in finishReply) ranks with the right mode.
    void startSearch(const QString& vid, const SeriesSnapshot& series,
                     const QString& targetVolume, bool seriesMode);
    void finishReply(QNetworkReply* reply);

    QPointer<QNetworkAccessManager> m_nam;
    TrustTable m_trust;
    QHash<QString, PendingSearch> m_pending; // keyed by volumeId
};

} // namespace MangaTankoban

Q_DECLARE_METATYPE(MangaTankoban::MangaNyaaCandidate)
