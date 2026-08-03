#include "BiblioRanking.h"

#include <QHash>
#include <QLatin1String>

#include <algorithm>

namespace {

// Tuning surface for the shelves — kept as named constants (same discipline as
// BiblioTaxonomy::kPublisherCoverageFloor) so the knobs are visible, not buried
// in the arithmetic.
//
// Popular blend weights over the max-normalized signals. They MUST sum to 1.0 so
// every work's Popular score stays on a comparable 0..1 scale.
constexpr double kPopularAppleWeight   = 0.4; // Apple Books chart performance
constexpr double kPopularVolumeWeight  = 0.3; // rating volume
constexpr double kPopularOpenLibWeight = 0.3; // Open Library popularity
static_assert(kPopularAppleWeight + kPopularVolumeWeight + kPopularOpenLibWeight == 1.0,
              "Popular blend weights must sum to 1.0");

// Trending needs two snapshots at least this many days apart before it will
// trust the seven-day momentum reading.
constexpr int kTrendingMinSpanDays = 6;

struct Scored {
    BiblioWork work;
    double     score = 0.0;
};

// Best-first, with canonical-id ascending as the deterministic tie-break so the
// output is stable regardless of input order.
QVector<BiblioWork> orderByScore(QVector<Scored> scored)
{
    std::stable_sort(scored.begin(), scored.end(), [](const Scored &a, const Scored &b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.work.canonicalId < b.work.canonicalId;
    });
    QVector<BiblioWork> out;
    out.reserve(scored.size());
    for (const Scored &s : scored)
        out.push_back(s.work);
    return out;
}

// Popular: sustained demand blended from Apple chart performance, rating volume
// and Open Library popularity. Each signal is normalized against the population
// maximum, so a work that tops one signal cannot alone dictate the order — the
// result is deliberately NOT a copy of the Apple Top 10. A zero signal simply
// contributes nothing; the work is still ranked on whatever evidence it has.
QVector<BiblioWork> rankPopular(const QList<BiblioWork> &works)
{
    double appleMax = 0.0, volumeMax = 0.0, olMax = 0.0;
    for (const BiblioWork &w : works) {
        appleMax  = std::max(appleMax, w.appleChartScore);
        volumeMax = std::max(volumeMax, double(w.rating.count));
        olMax     = std::max(olMax, w.openLibraryPopularity);
    }

    QVector<Scored> scored;
    scored.reserve(works.size());
    for (const BiblioWork &w : works) {
        const double appleN  = appleMax  > 0.0 ? w.appleChartScore / appleMax        : 0.0;
        const double volumeN = volumeMax > 0.0 ? double(w.rating.count) / volumeMax  : 0.0;
        const double olN     = olMax     > 0.0 ? w.openLibraryPopularity / olMax     : 0.0;
        const double score = kPopularAppleWeight * appleN
                             + kPopularVolumeWeight * volumeN
                             + kPopularOpenLibWeight * olN;
        scored.push_back({w, score});
    }
    return orderByScore(scored);
}

// Top Rated: an IMDb-style Bayesian shrink toward a population prior. The prior
// mean is the vote-weighted average rating and the prior strength is the mean
// vote count of the rated population, so the confidence prior ADAPTS to the
// active catalogue: in a heavily-rated population a work needs more votes before
// its raw average is trusted, which is exactly why a handful of perfect ratings
// cannot dominate a broadly-loved 4.7. Works with no ratings carry no rating
// signal and are excluded from this shelf entirely (and from the prior).
QVector<BiblioWork> rankTopRated(const QList<BiblioWork> &works)
{
    QVector<BiblioWork> rated;
    for (const BiblioWork &w : works)
        if (w.rating.count > 0)
            rated.push_back(w);
    if (rated.isEmpty())
        return {};

    double sumVotes = 0.0, sumWeighted = 0.0;
    for (const BiblioWork &w : rated) {
        sumVotes    += double(w.rating.count);
        sumWeighted += w.rating.average * double(w.rating.count);
    }
    const double priorMean  = sumWeighted / sumVotes;             // m
    const double priorVotes = sumVotes / double(rated.size());    // C (adapts to population)

    QVector<Scored> scored;
    scored.reserve(rated.size());
    for (const BiblioWork &w : rated) {
        const double v = double(w.rating.count);
        const double score = (v * w.rating.average + priorVotes * priorMean) / (v + priorVotes);
        scored.push_back({w, score});
    }
    return orderByScore(scored);
}

// New Releases: canonical works FIRST published in the trailing 12 months
// relative to nowUtc. Eligibility rides on canonicalFirstPublished only — a
// reprint, new cover, ebook conversion or audiobook release (which live on the
// work's editions) does NOT reset it. Works with no reliable first-publication
// date are excluded. Newest first.
QVector<BiblioWork> rankNewReleases(const QList<BiblioWork> &works, const QDateTime &nowUtc)
{
    const QDate today = nowUtc.date();
    const QDate windowStart = today.addYears(-1);

    QVector<Scored> scored;
    for (const BiblioWork &w : works) {
        const QDate d = w.canonicalFirstPublished;
        if (!d.isValid())
            continue;
        if (d < windowStart || d > today)
            continue;
        // Julian day as the score => larger is newer => newest first.
        scored.push_back({w, double(d.toJulianDay())});
    }
    return orderByScore(scored);
}

// Trending: seven-day momentum from dated demand snapshots. A work needs two
// snapshots at least six days apart to yield honest momentum; without that the
// work is skipped, and if nothing qualifies the shelf is EMPTY rather than
// falling back to any other ordering — so Trending never aliases Popular. Only
// genuinely rising works (positive momentum) trend.
QVector<BiblioWork> rankTrending(const QList<BiblioWork> &works,
                                 const QList<BiblioRankSnapshot> &history)
{
    struct Span {
        QDateTime first;
        QDateTime last;
        double firstScore = 0.0;
        double lastScore  = 0.0;
        bool   seen = false;
    };

    QHash<QString, Span> spans;
    for (const BiblioRankSnapshot &s : history) {
        if (!s.capturedAt.isValid())
            continue;
        Span &sp = spans[s.canonicalId];
        if (!sp.seen) {
            sp.first = sp.last = s.capturedAt;
            sp.firstScore = sp.lastScore = s.demandScore;
            sp.seen = true;
            continue;
        }
        if (s.capturedAt < sp.first) {
            sp.first = s.capturedAt;
            sp.firstScore = s.demandScore;
        }
        if (s.capturedAt > sp.last) {
            sp.last = s.capturedAt;
            sp.lastScore = s.demandScore;
        }
    }

    QHash<QString, const BiblioWork *> byId;
    for (const BiblioWork &w : works)
        byId.insert(w.canonicalId, &w);

    const qint64 minSpanSecs = qint64(kTrendingMinSpanDays) * 24 * 3600;

    QVector<Scored> scored;
    for (auto it = spans.constBegin(); it != spans.constEnd(); ++it) {
        const Span &sp = it.value();
        if (!sp.seen)
            continue;
        if (sp.first.secsTo(sp.last) < minSpanSecs)
            continue; // window too short to measure honest momentum
        const double momentum = sp.lastScore - sp.firstScore;
        if (momentum <= 0.0)
            continue; // flat or falling => not trending
        const auto found = byId.constFind(it.key());
        if (found == byId.constEnd())
            continue; // snapshot for a work not in this population
        scored.push_back({*found.value(), momentum});
    }
    return orderByScore(scored);
}

} // namespace

QVector<BiblioWork> BiblioRanking::rank(const QString &catalogId,
                                        const QList<BiblioWork> &works,
                                        const QList<BiblioRankSnapshot> &history,
                                        const QDateTime &nowUtc)
{
    if (catalogId == QLatin1String("popular"))      return rankPopular(works);
    if (catalogId == QLatin1String("top-rated"))    return rankTopRated(works);
    if (catalogId == QLatin1String("new-releases")) return rankNewReleases(works, nowUtc);
    if (catalogId == QLatin1String("trending"))     return rankTrending(works, history);
    return {}; // unknown shelf id
}
