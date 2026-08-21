#include "BiblioCatalog.h"
#include "BiblioRanking.h"
#include "BiblioTaxonomy.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTimer>
#include <QVector>

namespace {

// Bounded concurrency (spec/Task 4: "cap concurrent enrichment requests at
// four" — applied here to every request this coordinator issues, RSS seed
// feeds included, which is a superset of the requirement and keeps the whole
// pipeline's network footprint predictable).
constexpr int kMaxConcurrent = 4;
// Bounded retry/backoff for 429/transient failures. Linear backoff
// (attempt * kRetryBaseDelayMs) keeps worst-case retry latency small and
// deterministic for tests.
constexpr int kMaxRetries = 3;
constexpr int kRetryBaseDelayMs = 20;
// How many DISTINCT title/author candidates surfaced by the RSS pass receive
// bounded Apple Search + Open Library enrichment (one candidate => up to two
// extra requests). A named constant — mirrors BiblioTaxonomy's
// kPublisherCoverageFloor — so raising it later is a deliberate choice, not
// a magic number.
constexpr int kEnrichmentBudget = 24;
// Lazy grid enrichment burst cap (plan 2026-08-15 Slice 4): the maximum
// number of apple-search requests one requestEnrichment(catalogId) burst may
// issue. Candidates beyond the cap this session stay un-fetched — NO pending
// row is written for them (a 'pending' marker with no in-flight request
// would block the next day for no reason); the next session or the daily
// pass covers them.
constexpr int kLazyEnrichmentBudget = 12;

struct GenreFeedSeed {
    int appleGenreId;
    const char *axis;
    const char *rawLabel;
};

// A small, curated subset of Apple's ebook chart genre ids (mirrors
// qml/BiblioGenreApi.js GENRE_IDS) whose label folds cleanly through
// BiblioTaxonomy::normalize. This is the only per-item "subject" evidence
// available from Apple's RSS chart today — BiblioSourceRecord (Task 2) carries
// no raw subject/category list — so each feed's results are tagged with its
// facet as a hint applied when the snapshot is assembled.
const QVector<GenreFeedSeed> &genreFeeds()
{
    static const QVector<GenreFeedSeed> feeds = {
        {9020, "genre", "Sci-Fi"},
        {9032, "genre", "Mystery"},
        {9003, "genre", "Romance"},
        {9008, "genre", "Biography"},
        {11165, "audience", "Young Adult"},
    };
    return feeds;
}

// ── Facet subject seeding (spec 2026-08-15 seeding flip) ─────────────────────
//
// One taxonomy facet key used directly as an Open Library subject. The pair is
// stamped onto every record the subject job returns through the SAME
// m_facetHints path the genre-tagged RSS feeds use — one mechanism, not two —
// so each (axis,key) below MUST be a real controlled BiblioTaxonomy pair
// (publish() rejects unknown facet pairs) AND a real Open Library subject slug
// (the key rides the URL verbatim: openLibrarySubjectUrl(key)).
struct FacetSubjectSeed {
    const char *axis;
    const char *key;
};

// Rotation version of the seed list. Bumping this integer is the sanctioned
// way to change which subjects seed a refresh (it exists so a future rotation
// is a deliberate, reviewable edit — never a silent reorder). v1: first-guess
// budget per plan 2026-08-15 Slice 3 — the four genre keys the Apple RSS
// genre feeds already evidence (Sci-Fi/Mystery/Romance/Biography) plus
// Young Adult, the remaining curated genre keys, then the first setting keys.
inline constexpr int kFacetSubjectSeedVersion = 1;

// Hard cap on subject jobs issued per refresh (bounded daily budget). The list
// below is exactly this long; if the list ever grows past the cap, only the
// first kMaxSubjectSeedJobs entries seed (startRefresh slices with qMin).
inline constexpr int kMaxSubjectSeedJobs = 16;

const QVector<FacetSubjectSeed> &facetSubjectSeeds()
{
    static const QVector<FacetSubjectSeed> seeds = {
        {"genre", "science-fiction"},
        {"genre", "mystery"},
        {"genre", "romance"},
        {"genre", "biography"},
        {"audience", "young-adult"},
        {"genre", "fantasy"},
        {"genre", "thriller"},
        {"genre", "horror"},
        {"genre", "historical-fiction"},
        {"genre", "literary-fiction"},
        {"genre", "nonfiction"},
        {"setting", "space"},
        {"setting", "dystopia"},
        {"setting", "post-apocalyptic"},
        {"setting", "urban"},
        {"setting", "small-town"},
    };
    return seeds;
}

const QStringList &houseCatalogIds()
{
    // The six house catalogues (spec 2026-08-15 adds the two Open Library
    // payload-ordered shelves after "trending"). Feeds BOTH exploreRows() and
    // buildSnapshot()'s ranking pass; "most-read"/"classics" rows are built
    // there from the tracked payload order, not BiblioRanking over the pool.
    static const QStringList ids = {
        QStringLiteral("popular"), QStringLiteral("top-rated"),
        QStringLiteral("new-releases"), QStringLiteral("trending"),
        QStringLiteral("most-read"), QStringLiteral("classics")};
    return ids;
}

} // namespace

// ---------------------------------------------------------------------------
// BiblioNetworkTransport — production QNetworkAccessManager-backed transport.
// ---------------------------------------------------------------------------

namespace {

class NetworkBiblioTransportReply : public BiblioTransportReply {
public:
    explicit NetworkBiblioTransportReply(QNetworkReply *reply)
        : BiblioTransportReply(nullptr), m_reply(reply)
    {
        m_reply->setParent(this);
        connect(m_reply, &QNetworkReply::finished, this, [this]() {
            if (m_cancelled)
                return;
            const int status =
                m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            // A genuine transport-level failure never produced an HTTP status
            // at all; a 4xx/5xx response (which Qt ALSO flags via error())
            // still carries a valid status and is reported as that status,
            // not as a transport error.
            QString error;
            if (status == 0 && m_reply->error() != QNetworkReply::NoError)
                error = m_reply->errorString();
            const QByteArray body = m_reply->readAll();
            emit finished(status, body, error);
        });
    }

    void cancel() override
    {
        if (m_cancelled)
            return;
        m_cancelled = true;
        m_reply->abort();
    }

private:
    QNetworkReply *m_reply = nullptr;
    bool m_cancelled = false;
};

} // namespace

BiblioNetworkTransport::BiblioNetworkTransport(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this))
{
}

BiblioTransportReply *BiblioNetworkTransport::get(const QUrl &url, const QVariantMap &headers)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Colosseum/1.0 (+biblio-catalog; keyless)"));
    req.setTransferTimeout(6000);   // bound the wait so a slow/dead Apple or Open
                                    // Library can't wedge one of the 4 concurrency
                                    // slots forever (matches MangaSynopsisEnricher's norm).
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
        req.setRawHeader(it.key().toUtf8(), it.value().toByteArray());
    QNetworkReply *reply = m_nam->get(req);
    return new NetworkBiblioTransportReply(reply);
}

// ---------------------------------------------------------------------------
// BiblioCatalog
// ---------------------------------------------------------------------------

BiblioCatalog::BiblioCatalog(const QString &dbPath, IBiblioTransport *transport, QObject *parent)
    : QObject(parent)
{
    QDir().mkpath(QFileInfo(dbPath).absolutePath());
    m_store.open(dbPath);

    if (transport) {
        m_transport = transport; // NOT owned — caller's lifetime
    } else {
        m_ownedTransport = new BiblioNetworkTransport(this);
        m_transport = m_ownedTransport;
    }

    // Cached first paint: a previously published snapshot is browsable the
    // instant this object exists, before any network reply lands.
    m_ready = m_store.hasSnapshot();
    m_lastSuccessfulRefresh = m_store.lastSuccessUtc();
    recomputeStale();
}

BiblioCatalog::~BiblioCatalog()
{
    cancelGeneration(m_generation);
    // The lazy path's replies are not generation machinery — cancelGeneration
    // never reaches them — so clean them up the same way here: a destructing
    // catalog must not leave in-flight sockets (or their callbacks) behind.
    const QList<BiblioTransportReply *> lazyReplies = m_lazyInFlight.keys();
    for (BiblioTransportReply *reply : lazyReplies) {
        reply->disconnect(this);
        reply->cancel();
        m_lazyInFlight.remove(reply);
        reply->deleteLater();
    }
}

// --- property setters (each a no-op + no signal when the value is unchanged) -

void BiblioCatalog::setReady(bool v)
{
    if (m_ready == v) return;
    m_ready = v;
    emit readyChanged();
}

void BiblioCatalog::setRefreshing(bool v)
{
    if (m_refreshing == v) return;
    m_refreshing = v;
    emit refreshingChanged();
}

void BiblioCatalog::setStale(bool v)
{
    if (m_stale == v) return;
    m_stale = v;
    emit staleChanged();
}

void BiblioCatalog::setOffline(bool v)
{
    if (m_offline == v) return;
    m_offline = v;
    emit offlineChanged();
}

void BiblioCatalog::setLastError(const QString &v)
{
    if (m_lastError == v) return;
    m_lastError = v;
    emit lastErrorChanged();
}

void BiblioCatalog::recomputeStale()
{
    // "stale" only describes a READY cache that is not from today's local
    // date (never-synced or a refresh that failed today never bumps this).
    // A catalogue that has never had a successful publish is "not ready",
    // not "stale" — there is nothing browsable to call stale.
    const bool value = m_ready
        && (!m_lastSuccessfulRefresh.isValid()
            || m_lastSuccessfulRefresh.toLocalTime().date() != QDate::currentDate());
    setStale(value);
}

// --- read-only invokables: pure proxies onto the store --------------------

QVariantMap BiblioCatalog::discoverPage(const QString &catalogId, const QString &facetAxis,
                                        const QString &facetKey, bool includeExplicit,
                                        int offset, int limit) const
{
    return m_store.page(catalogId, facetAxis, facetKey, includeExplicit, offset, limit);
}

QVariantList BiblioCatalog::filterGroups(bool includeExplicit) const
{
    return m_store.filterGroups(includeExplicit);
}

QVariantList BiblioCatalog::exploreRows(int limitPerShelf, bool includeExplicit) const
{
    QVariantList out;
    for (const QString &catalogId : houseCatalogIds()) {
        const QVariantMap page =
            m_store.page(catalogId, QString(), QString(), includeExplicit, 0, limitPerShelf);
        out.append(QVariantMap{
            {QStringLiteral("catalogId"), catalogId},
            {QStringLiteral("items"), page.value(QStringLiteral("items"))}});
    }
    return out;
}

QString BiblioCatalog::axisForFacetKey(const QString &key) const
{
    if (key.isEmpty())
        return QString();
    for (const BiblioFilterGroup &g : BiblioTaxonomy::filterGroups()) {
        if (g.axis == QStringLiteral("publisher"))
            continue; // data-derived values, not a fixed mosaic target
        for (const BiblioFacet &f : g.facets)
            if (f.key == key)
                return g.axis;
    }
    return QString();
}

QVariantList BiblioCatalog::mosaic(const QString &facetKey, int limit, bool includeExplicit) const
{
    const QString axis = axisForFacetKey(facetKey);
    if (axis.isEmpty())
        return {};
    const QVariantMap page =
        m_store.page(QStringLiteral("popular"), axis, facetKey, includeExplicit, 0, limit);
    return page.value(QStringLiteral("items")).toList();
}

// --- refresh coordinator ----------------------------------------------------

void BiblioCatalog::refreshIfDue(bool force)
{
    if (m_refreshing) {
        if (!force)
            return; // coalesced: a refresh is already running
        cancelGeneration(m_generation);
    } else if (!force && m_lastAttemptDate.isValid()
               && m_lastAttemptDate == QDate::currentDate()) {
        return; // already attempted today; a forced call bypasses this gate
    }
    startRefresh();
}

void BiblioCatalog::startRefresh()
{
    ++m_generation;
    const int gen = m_generation;

    setRefreshing(true);

    m_records.clear();
    m_requestedUrls.clear();
    m_enrichedCandidates.clear();
    m_facetHints.clear();
    m_mostReadOrder.clear();
    m_classicsOrder.clear();
    m_queue.clear();
    m_activeCount = 0;
    m_pendingRetries = 0;
    m_anySuccess = false;
    m_enrichmentRemaining = kEnrichmentBudget;
    // Slice 4: snapshot the parked lazy-enrichment rows for this generation's
    // fan-out consultation (enqueueEnrichment). Rows a lazy burst writes AFTER
    // this point are at worst re-searched once — the fan-out's own dedupe and
    // the identity merge make the duplication harmless.
    m_pendingEnrichmentAtStart = m_store.pendingEnrichmentMap();

    // Apple still seeds the four chart catalogues (spec 2026-08-15: Popular
    // stays the blended house ranking, Trending stays Apple chart velocity).
    enqueue(gen, BiblioProviders::appleTopEbooksRssUrl(), QStringLiteral("apple-rss"));
    for (const GenreFeedSeed &g : genreFeeds()) {
        const QString axis = QString::fromLatin1(g.axis);
        const QString key = BiblioTaxonomy::normalize(axis, QString::fromLatin1(g.rawLabel));
        enqueue(gen, BiblioProviders::appleTopEbooksRssUrl(QStringLiteral("us"), 100, g.appleGenreId),
                QStringLiteral("apple-rss"), axis, key);
    }

    // Open Library seeds catalog breadth (the seeding flip): the daily
    // most-read list, the readinglog-ordered classics list, and a bounded,
    // versioned rotation of taxonomy-facet subject slices. Payload order of
    // the first two IS their ranking — recorded in handleSuccess.
    enqueue(gen, BiblioProviders::openLibraryTrendingDailyUrl(100),
            QStringLiteral("openlibrary-trending"));
    enqueue(gen, BiblioProviders::openLibraryClassicsUrl(100),
            QStringLiteral("openlibrary-classics"));
    const QVector<FacetSubjectSeed> &seeds = facetSubjectSeeds();
    const int seedCount = qMin<int>(seeds.size(), kMaxSubjectSeedJobs);
    for (int i = 0; i < seedCount; ++i) {
        enqueue(gen, BiblioProviders::openLibrarySubjectUrl(
                           QString::fromLatin1(seeds.at(i).key), 50),
                QStringLiteral("openlibrary-subject"),
                QString::fromLatin1(seeds.at(i).axis), QString::fromLatin1(seeds.at(i).key));
    }

    pumpQueue(gen);
}

void BiblioCatalog::enqueue(int generation, const QUrl &url, const QString &kind,
                            const QString &facetAxis, const QString &facetKey)
{
    if (generation != m_generation)
        return; // defensive: caller belongs to an obsolete generation
    const QString urlKey = url.toString();
    if (m_requestedUrls.contains(urlKey))
        return; // dedupe within this generation
    m_requestedUrls.insert(urlKey);

    FetchJob job;
    job.url = url;
    job.kind = kind;
    job.facetAxis = facetAxis;
    job.facetKey = facetKey;
    m_queue.enqueue(job);
}

void BiblioCatalog::pumpQueue(int generation)
{
    if (generation != m_generation)
        return;
    while (m_activeCount < kMaxConcurrent && !m_queue.isEmpty()) {
        const FetchJob job = m_queue.dequeue();
        issueRequest(generation, job);
    }
    maybeFinalize(generation);
}

void BiblioCatalog::issueRequest(int generation, const FetchJob &job)
{
    BiblioTransportReply *reply = m_transport->get(job.url, QVariantMap());
    ++m_activeCount;
    m_inFlight.insert(reply, {job, generation});
    connect(reply, &BiblioTransportReply::finished, this,
            [this, reply](int status, const QByteArray &body, const QString &error) {
                onReplyFinished(reply, status, body, error);
            });
}

void BiblioCatalog::onReplyFinished(BiblioTransportReply *reply, int status,
                                    const QByteArray &body, const QString &error)
{
    auto it = m_inFlight.find(reply);
    if (it == m_inFlight.end()) {
        // Stray callback (already cancelled/removed elsewhere) — clean up
        // and stop; the generation this belonged to has already moved on.
        reply->deleteLater();
        return;
    }
    const FetchJob job = it.value().first;
    const int generation = it.value().second;
    m_inFlight.erase(it);
    reply->deleteLater();

    if (generation != m_generation)
        return; // obsolete generation; its bookkeeping was already reset

    --m_activeCount;

    const bool transportError = !error.isEmpty();
    const bool transientHttp = status == 429 || (status >= 500 && status < 600);
    const bool ok = !transportError && status >= 200 && status < 300;

    if (ok) {
        m_anySuccess = true;
        setOffline(false);
        handleSuccess(generation, job, body);
    } else if ((transportError || transientHttp) && job.attempt < kMaxRetries) {
        scheduleRetry(generation, job);
    }
    // Else: a permanent (non-retryable) failure for this one job. It
    // contributes nothing; overall success/failure is judged once, at
    // finalizeGeneration, by whether ANY usable record was collected.

    pumpQueue(generation);
}

void BiblioCatalog::scheduleRetry(int generation, FetchJob job)
{
    ++job.attempt;
    ++m_pendingRetries;
    const int delayMs = kRetryBaseDelayMs * job.attempt;
    QTimer::singleShot(delayMs, this, [this, generation, job]() {
        // Generation check FIRST: a stale-generation timer firing must be a
        // total no-op, touching NOTHING shared (not even the counter) —
        // otherwise it can decrement the CURRENT generation's
        // m_pendingRetries (reset to 0 by startRefresh after this timer was
        // scheduled), driving it negative and permanently wedging
        // maybeFinalize's `== 0` check, i.e. refreshing never returns to
        // false. See the regression test in the service harness.
        if (generation != m_generation)
            return;
        --m_pendingRetries;
        // Re-queue directly: enqueue()'s URL dedupe must not block a RETRY of
        // the very url it already recorded.
        m_queue.enqueue(job);
        pumpQueue(generation);
    });
}

void BiblioCatalog::maybeFinalize(int generation)
{
    if (generation != m_generation)
        return;
    if (m_queue.isEmpty() && m_activeCount == 0 && m_pendingRetries == 0)
        finalizeGeneration(generation);
}

void BiblioCatalog::cancelGeneration(int generation)
{
    QList<BiblioTransportReply *> stale;
    for (auto it = m_inFlight.constBegin(); it != m_inFlight.constEnd(); ++it)
        if (it.value().second == generation)
            stale.append(it.key());
    for (BiblioTransportReply *reply : stale) {
        reply->disconnect(this);
        reply->cancel();
        m_inFlight.remove(reply);
        reply->deleteLater();
    }
}

// ── Lazy grid enrichment (plan 2026-08-15 Slice 4) ───────────────────────────

void BiblioCatalog::requestEnrichment(const QString &catalogId)
{
    // Session idempotence (locked design): one burst per (session, catalogue).
    // Mark FIRST so even a burst that finds no candidates is never re-run.
    if (m_lazyEnrichedCatalogs.contains(catalogId))
        return;
    m_lazyEnrichedCatalogs.insert(catalogId);

    // Candidates page off the CURRENT published ranking (m_store.page) — never
    // the refresh generation's in-flight records. The gate state is read once
    // up front; the burst is short and single-threaded on the GUI thread, so
    // nothing can change underneath it except our own writes below.
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    const QHash<QString, BiblioPendingEnrichmentRow> pending = m_store.pendingEnrichmentMap();

    int issued = 0;
    int offset = 0;
    while (issued < kLazyEnrichmentBudget) {
        // 100 is the store's page limit clamp ceiling (kPageLimitMax); the
        // widest page means the fewest round trips over a small local db.
        // includeExplicit=true: enrichment is neutral to the explicit gate
        // (the fold-in merges by identity; gating is a read-time concern).
        const QVariantMap page =
            m_store.page(catalogId, QString(), QString(), /*includeExplicit=*/true, offset, 100);
        const QVariantList items = page.value(QStringLiteral("items")).toList();
        if (items.isEmpty())
            break;
        for (const QVariant &v : items) {
            if (issued >= kLazyEnrichmentBudget)
                break; // burst cap: the rest stay un-fetched, with no marker
            const QVariantMap item = v.toMap();
            const QString title = item.value(QStringLiteral("title")).toString();
            if (title.isEmpty())
                continue; // no usable identity to search on
            // Candidate = the PUBLISHED row still lacks artwork or rating
            // evidence (the two things apple-search enrichment carries).
            const QVariantMap rating = item.value(QStringLiteral("rating")).toMap();
            const bool artworkPresent =
                !item.value(QStringLiteral("coverUrl")).toString().isEmpty();
            const bool ratingPresent = rating.value(QStringLiteral("count")).toInt() > 0;
            if (artworkPresent && ratingPresent)
                continue;
            // The same candidate key the daily enrichment path dedupes on.
            const QString workKey =
                BiblioProviders::foldTitleAuthor(title) + QChar('|')
                + BiblioProviders::foldTitleAuthor(
                      item.value(QStringLiteral("author")).toString());
            const auto it = pending.constFind(workKey);
            if (it != pending.constEnd()) {
                const QString state = it.value().state;
                if (state == QStringLiteral("enriched"))
                    continue; // artwork already parked for the next publish
                // 'pending' blocks only same-day: the marker may belong to a
                // request issued earlier today (a cross-catalogue burst in
                // this session, or a live in-flight reply). A 'pending' row
                // older than today is crash residue — its reply died with the
                // session that issued it — so fall through and re-issue
                // instead of blocking this work's enrichment forever.
                if (state == QStringLiteral("pending") && it.value().lastAttempt == today)
                    continue; // arriving
                if (state == QStringLiteral("unavailable")
                    && it.value().lastAttempt == today)
                    continue; // ≤1 re-attempt per local day
                // 'unavailable' with last_attempt < today (or empty) falls
                // through: the daily gate says it is fetchable again.
            }
            issueLazyEnrichment(workKey, title);
            ++issued;
        }
        if (page.value(QStringLiteral("exhausted")).toBool())
            break;
        offset = page.value(QStringLiteral("nextOffset")).toInt();
    }
}

void BiblioCatalog::issueLazyEnrichment(const QString &workKey, const QString &title)
{
    // Deliberately NOT the daily generation machinery: no queue slot, no
    // concurrency accounting, no retry. The burst is bounded by
    // kLazyEnrichmentBudget, and the production transport
    // (QNetworkAccessManager) serializes the actual sockets.
    BiblioTransportReply *reply = m_transport->get(
        BiblioProviders::appleSearchUrl(title, QStringLiteral("ebook")), QVariantMap());
    m_lazyInFlight.insert(reply, workKey);
    connect(reply, &BiblioTransportReply::finished, this,
            [this, reply](int status, const QByteArray &body, const QString &error) {
                onLazyReplyFinished(reply, status, body, error);
            });
    // Crash-safe marker: a crash between issue and reply leaves 'pending'
    // dated today — today's bursts and today's daily pass skip it (no
    // duplicate stacked onto a possibly-live request), and a still-unresolved
    // row is treated as crash residue from the next day on: re-issued, never
    // a permanent block.
    m_store.upsertPendingEnrichment(workKey, QByteArray(), QStringLiteral("pending"),
                                    QDate::currentDate().toString(Qt::ISODate));
}

void BiblioCatalog::onLazyReplyFinished(BiblioTransportReply *reply, int status,
                                        const QByteArray &body, const QString &error)
{
    auto it = m_lazyInFlight.find(reply);
    if (it == m_lazyInFlight.end()) {
        // Stray callback (already cancelled/removed) — clean up and stop.
        reply->deleteLater();
        return;
    }
    const QString workKey = it.value();
    m_lazyInFlight.erase(it);
    reply->deleteLater();

    const QString today = QDate::currentDate().toString(Qt::ISODate);
    const bool transportError = !error.isEmpty();
    const bool transientHttp = status == 429 || (status >= 500 && status < 600);

    if (transportError || transientHttp) {
        // Transient: drop the crash-safe marker entirely so BOTH paths may
        // retry (a 'pending' row with no in-flight request would otherwise
        // block the lazy gate; an 'unavailable' row would burn the day's
        // single attempt on a hiccup). The next session/burst tries again.
        m_store.clearFoldedPending({workKey});
        return;
    }

    if (status >= 200 && status < 300) {
        // Park the body RAW: fold-in is parseAppleSearch(body) at merge time,
        // and the parsed records merge onto the right canonical work by
        // identity — no new serializer, no canonicalId mapping.
        const QList<BiblioSourceRecord> records =
            BiblioProviders::parseAppleSearch(body, QDateTime::currentDateTimeUtc());
        if (records.isEmpty())
            m_store.upsertPendingEnrichment(workKey, QByteArray(),
                                            QStringLiteral("unavailable"), today);
        else
            m_store.upsertPendingEnrichment(workKey, body,
                                            QStringLiteral("enriched"), today);
        return;
    }

    // A permanent 4xx: the storefront had nothing (or rejected the query) —
    // 'unavailable', gated to ≤1 re-attempt per local day via last_attempt.
    m_store.upsertPendingEnrichment(workKey, QByteArray(),
                                    QStringLiteral("unavailable"), today);
}

void BiblioCatalog::enqueueEnrichment(int generation, const BiblioSourceRecord &record,
                                      bool alsoOpenLibrary)
{
    // Bounded enrichment fan-out, deduped by folded title+author so a book
    // appearing on more than one feed/payload is only enriched once.
    const QString candidateKey = record.normalizedTitle + QChar('|') + record.normalizedAuthor;
    if (record.normalizedTitle.isEmpty())
        return; // no usable identity to search on
    if (m_enrichedCandidates.contains(candidateKey))
        return;
    m_enrichedCandidates.insert(candidateKey);

    // Slice 4 priority refinement: consult the lazy path's parked rows so the
    // daily pass stays preview-priority honest — a work whose artwork is
    // already parked ('enriched') or in flight ('pending') skips the
    // apple-search half (the fold-in supplies it at publish), and an
    // 'unavailable' work respects the same ≤1/day gate during the daily pass.
    // m_pendingEnrichmentAtStart was snapshotted at startRefresh; rows a lazy
    // burst wrote after that are at worst re-searched once (the fan-out's own
    // dedupe and the identity merge make the duplication harmless). Only the
    // apple-search half is skipped — the Open Library identity search still
    // runs for apple-rss records.
    bool skipAppleSearch = false;
    const auto pit = m_pendingEnrichmentAtStart.constFind(candidateKey);
    if (pit != m_pendingEnrichmentAtStart.constEnd()) {
        const QString state = pit.value().state;
        // 'enriched' always covers. 'pending' covers only same-day — the
        // mirror of the lazy gate's aging: an older 'pending' row is crash
        // residue whose reply never comes, so the daily pass fetches rather
        // than waiting on a ghost marker. 'unavailable' respects the same
        // ≤1/day gate during the daily pass.
        if (state == QStringLiteral("enriched"))
            skipAppleSearch = true;
        else if ((state == QStringLiteral("pending") || state == QStringLiteral("unavailable"))
                 && pit.value().lastAttempt == QDate::currentDate().toString(Qt::ISODate))
            skipAppleSearch = true;
    }

    if (!skipAppleSearch) {
        if (m_enrichmentRemaining <= 0)
            return;
        --m_enrichmentRemaining;
        enqueue(generation,
                BiblioProviders::appleSearchUrl(record.title, QStringLiteral("ebook")),
                QStringLiteral("apple-search"));
    }
    if (alsoOpenLibrary) {
        enqueue(generation,
                BiblioProviders::openLibrarySearchUrl(record.title, record.author),
                QStringLiteral("openlibrary"));
    }
}

void BiblioCatalog::handleSuccess(int generation, const FetchJob &job, const QByteArray &body)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();

    if (job.kind == QStringLiteral("apple-rss")) {
        const QList<BiblioSourceRecord> records = BiblioProviders::parseAppleRss(body, now);
        for (const BiblioSourceRecord &r : records) {
            if (!job.facetAxis.isEmpty() && !job.facetKey.isEmpty())
                m_facetHints[r.sourceId].append({job.facetAxis, job.facetKey});
            m_records.append(r);
            // Apple chart records are NOT Open Library data: enrich through
            // BOTH storefronts (artwork/rating/description + OL identity).
            enqueueEnrichment(generation, r, /*alsoOpenLibrary=*/true);
        }
    } else if (job.kind == QStringLiteral("apple-search")) {
        m_records += BiblioProviders::parseAppleSearch(body, now);
    } else if (job.kind == QStringLiteral("openlibrary")) {
        m_records += BiblioProviders::parseOpenLibrarySearch(body, now);
    } else if (job.kind == QStringLiteral("openlibrary-trending")) {
        // Payload order IS the most-read ranking: record each sourceId in the
        // order the provider sent them (buildSnapshot maps them to canonical
        // works — never re-ranks the merged pool).
        const QList<BiblioSourceRecord> records = BiblioProviders::parseOpenLibraryTrending(body, now);
        for (const BiblioSourceRecord &r : records) {
            m_mostReadOrder.append(r.sourceId);
            m_records.append(r);
            enqueueEnrichment(generation, r, /*alsoOpenLibrary=*/false);
        }
    } else if (job.kind == QStringLiteral("openlibrary-classics")) {
        // Same contract as trending: the payload's readinglog order is the
        // classics ranking; the raw (noisy) year never orders.
        const QList<BiblioSourceRecord> records = BiblioProviders::parseOpenLibraryClassics(body, now);
        for (const BiblioSourceRecord &r : records) {
            m_classicsOrder.append(r.sourceId);
            m_records.append(r);
            enqueueEnrichment(generation, r, /*alsoOpenLibrary=*/false);
        }
    } else if (job.kind == QStringLiteral("openlibrary-subject")) {
        // Breadth seeding: stamp the requested (axis,key) hint through the
        // same m_facetHints path the genre-tagged RSS feeds use.
        const QList<BiblioSourceRecord> records = BiblioProviders::parseOpenLibrarySearch(body, now);
        for (const BiblioSourceRecord &r : records) {
            if (!job.facetAxis.isEmpty() && !job.facetKey.isEmpty())
                m_facetHints[r.sourceId].append({job.facetAxis, job.facetKey});
            m_records.append(r);
            enqueueEnrichment(generation, r, /*alsoOpenLibrary=*/false);
        }
    }
}

BiblioCatalogSnapshot BiblioCatalog::buildSnapshot(const QList<BiblioCanonicalWork> &canonical,
                                                   const QStringList &mostReadOrder,
                                                   const QStringList &classicsOrder) const
{
    BiblioCatalogSnapshot snap;
    snap.capturedAt = QDateTime::currentDateTimeUtc();

    QList<BiblioWork> works;
    works.reserve(canonical.size());
    for (const BiblioCanonicalWork &cw : canonical)
        works.append(cw.work);
    snap.works = works;

    for (const BiblioCanonicalWork &cw : canonical) {
        const QString &id = cw.work.canonicalId;

        // NOTE: editions are NOT also appended to snap.editions here. Every
        // canonical work's editions already ride along nested on
        // cw.work.editions (and snap.works carries the same BiblioWork
        // values via `works` above), and BiblioCatalogStore::publish()
        // persists BOTH the flat snapshot.editions list AND each work's
        // nested editions inside the same transaction — populating both
        // would insert every edition twice and collide on the
        // (snapshot_id, edition_id) primary key.

        for (const BiblioFieldSource &fs : cw.fieldSources) {
            BiblioCatalogSource cs;
            cs.field = fs.field;
            cs.source = fs.source;
            cs.sourceId = fs.sourceId;
            cs.observedAt = fs.observedAt;
            snap.sources.append({id, cs});
        }

        // --- facets --------------------------------------------------------
        QSet<QString> seenPairs;
        auto addFacet = [&](const QString &axis, const QString &key) {
            if (axis.isEmpty() || key.isEmpty())
                return;
            const QString pairKey = axis + QChar('/') + key;
            if (seenPairs.contains(pairKey))
                return;
            seenPairs.insert(pairKey);
            snap.facets.append({id, BiblioCatalogFacet{axis, key}});
        };

        // length/era/language/publisher are computed directly from canonical
        // fields — real, derivable signals for every work regardless of
        // which provider(s) contributed it.
        int representativePages = 0;
        bool hasEnglishEdition = false;
        for (const BiblioEdition &e : cw.work.editions) {
            if (!e.englishReadable)
                continue;
            hasEnglishEdition = true;
            if (e.pageCount > 0 && (representativePages == 0 || e.pageCount < representativePages))
                representativePages = e.pageCount;
        }
        if (representativePages <= 0) {
            for (const BiblioEdition &e : cw.work.editions) {
                if (e.pageCount > 0) { representativePages = e.pageCount; break; }
            }
        }
        addFacet(QStringLiteral("length"), BiblioTaxonomy::lengthKey(representativePages));
        addFacet(QStringLiteral("era"), BiblioTaxonomy::eraKey(
            cw.work.canonicalFirstPublished.isValid() ? cw.work.canonicalFirstPublished.year() : 0));
        addFacet(QStringLiteral("language"),
                 BiblioTaxonomy::languageKey(cw.work.originalLanguage, hasEnglishEdition));
        addFacet(QStringLiteral("publisher"),
                 BiblioTaxonomy::normalize(QStringLiteral("publisher"), cw.work.publisher));

        // genre/audience hints gathered from whichever tagged RSS feed a
        // field-sourcing record arrived on (see genreFeeds() above — the only
        // raw-subject evidence this task's providers carry).
        for (const BiblioFieldSource &fs : cw.fieldSources) {
            const auto hintIt = m_facetHints.constFind(fs.sourceId);
            if (hintIt == m_facetHints.constEnd())
                continue;
            for (const auto &hint : hintIt.value())
                addFacet(hint.first, hint.second);
        }
    }

    // --- rankings + ranking-history -----------------------------------------
    const QDateTime now = snap.capturedAt;
    QList<BiblioRankSnapshot> history;
    for (const BiblioWork &w : works) {
        // Prior daily readings already on disk (Trending's 7-day delta source).
        const QVariantList rows = m_store.rankingHistoryFor(w.canonicalId);
        for (const QVariant &row : rows) {
            const QVariantMap m = row.toMap();
            BiblioRankSnapshot s;
            s.canonicalId = m.value(QStringLiteral("canonicalId")).toString();
            s.capturedAt = m.value(QStringLiteral("capturedAt")).toDateTime();
            s.demandScore = m.value(QStringLiteral("demandScore")).toDouble();
            history.append(s);
        }

        // Today's fresh demand reading: a zero-safe composite of the Apple
        // chart signal and the Open Library popularity signal. Persisted into
        // snap.history so tomorrow's refresh can compute an honest delta.
        BiblioCatalogHistory h;
        h.canonicalId = w.canonicalId;
        h.capturedAt = now;
        h.demandScore = w.appleChartScore + w.openLibraryPopularity;
        snap.history.append(h);

        BiblioRankSnapshot todayReading;
        todayReading.canonicalId = w.canonicalId;
        todayReading.capturedAt = now;
        todayReading.demandScore = h.demandScore;
        history.append(todayReading);
    }

    // The four chart catalogues stay BiblioRanking's job (pure formula over
    // the merged pool). most-read/classics are skipped here and appended below
    // from the tracked payload order — the two openLibraryPopularity scales are
    // incommensurable (trending payload index vs all-time readinglog counts),
    // so ranking the merged pool would let a chart-pool work with a huge
    // readinglog contaminate the daily list.
    for (const QString &catalogId : houseCatalogIds()) {
        if (catalogId == QStringLiteral("most-read") || catalogId == QStringLiteral("classics"))
            continue; // payload-ordered — see appendOrderRanking below
        const QVector<BiblioWork> ranked = BiblioRanking::rank(catalogId, works, history, now);
        const int total = ranked.size();
        for (int i = 0; i < total; ++i) {
            BiblioCatalogRanking r;
            r.catalogId = catalogId;
            r.canonicalId = ranked.at(i).canonicalId;
            r.score = double(total - i); // secondary key only; rank is authoritative
            r.rank = i + 1;
            snap.rankings.append(r);
        }
    }

    // ── Payload-ordered rankings: most-read / classics ─────────────────────
    // Map every sourceId the canonicalizer's provenance carried (fieldSources
    // is where record ids ride — BiblioEdition carries none) to the canonical
    // work it merged into, then walk the tracked order lists: rank/score
    // follow payload position, first occurrence wins over a duplicate
    // canonicalId (one work can legitimately sit in BOTH catalogues, e.g. a
    // classic that is also being read today), and unmapped ids are skipped
    // (a record that lost every field of its merge group left no provenance).
    QHash<QString, QString> sourceIdToCanonical;
    sourceIdToCanonical.reserve(canonical.size() * 4);
    for (const BiblioCanonicalWork &cw : canonical)
        for (const BiblioFieldSource &fs : cw.fieldSources)
            sourceIdToCanonical.insert(fs.sourceId, cw.work.canonicalId);

    auto appendOrderRanking = [&snap, &sourceIdToCanonical](const QString &catalogId,
                                                            const QStringList &order) {
        QSet<QString> seenCanonical;
        QVector<QString> canonicalIds;
        canonicalIds.reserve(order.size());
        for (const QString &sourceId : order) {
            const QString canonicalId = sourceIdToCanonical.value(sourceId);
            if (canonicalId.isEmpty())
                continue; // never mapped to a canonical work
            if (seenCanonical.contains(canonicalId))
                continue; // duplicate — first occurrence wins
            seenCanonical.insert(canonicalId);
            canonicalIds.append(canonicalId);
        }
        const int total = canonicalIds.size();
        for (int i = 0; i < total; ++i) {
            BiblioCatalogRanking r;
            r.catalogId = catalogId;
            r.canonicalId = canonicalIds.at(i);
            r.score = double(total - i); // N..1 down the payload order
            r.rank = i + 1;              // 1..N in payload order
            snap.rankings.append(r);
        }
    };
    appendOrderRanking(QStringLiteral("most-read"), mostReadOrder);
    appendOrderRanking(QStringLiteral("classics"), classicsOrder);

    // explicitWorkIds intentionally left empty: no source-confirmed
    // sexually-explicit signal exists on BiblioSourceRecord (Task 2's data
    // model). Populating it is Task 9's explicit-content hardening.
    return snap;
}

void BiblioCatalog::finalizeGeneration(int generation)
{
    if (generation != m_generation)
        return;

    setRefreshing(false);
    m_lastAttemptDate = QDate::currentDate();

    if (!m_anySuccess)
        setOffline(true);

    bool success = false;
    QString failureReason;

    if (m_records.isEmpty()) {
        failureReason = QStringLiteral(
            "refresh failed: no catalogue data retrieved from any provider");
    } else {
        // ── Slice 4 fold-in: parked lazy-enrichment rows fold into THIS
        // publish. Load every 'enriched' row, parse its stored raw
        // apple-search body, and append the records to the generation's pool
        // — they merge onto the right canonical works by identity, and the
        // canonicalizer's existing precedence already prefers Apple artwork
        // and Apple ratings over Open Library rows, so the overlay needs no
        // new precedence rule. A refresh that retrieved nothing never reaches
        // here (see above), and a publish that fails to validate leaves every
        // parked row intact for the next successful one — the lazy path can
        // never publish anything by itself.
        QStringList foldedPendingKeys;
        const QHash<QString, BiblioPendingEnrichmentRow> pending =
            m_store.pendingEnrichmentMap();
        for (auto it = pending.constBegin(); it != pending.constEnd(); ++it) {
            if (it.value().state != QStringLiteral("enriched"))
                continue; // 'pending' markers and 'unavailable' gate rows stay parked
            m_records += BiblioProviders::parseAppleSearch(
                it.value().body, QDateTime::currentDateTimeUtc());
            foldedPendingKeys.append(it.key());
        }

        const QList<BiblioCanonicalWork> canonical = BiblioCanonicalizer::merge(m_records);
        if (canonical.isEmpty()) {
            failureReason = QStringLiteral("refresh failed: no canonical works resolved");
        } else {
            // The generation's payload-order lists ride along as value params
            // (buildSnapshot stays const; see its comment for why most-read/
            // classics rows come from these, not BiblioRanking).
            const BiblioCatalogSnapshot snapshot =
                buildSnapshot(canonical, m_mostReadOrder, m_classicsOrder);
            if (m_store.publish(snapshot)) {
                success = true;
                // Clear exactly the rows this publish folded in. The
                // 'unavailable' rows stay (the ≤1/day gate reads them
                // tomorrow); any live 'pending' markers stay too.
                if (!foldedPendingKeys.isEmpty())
                    m_store.clearFoldedPending(foldedPendingKeys);
            } else {
                failureReason = m_store.lastWarning().isEmpty()
                    ? QStringLiteral("refresh failed: snapshot did not validate")
                    : m_store.lastWarning();
            }
        }
    }

    if (success) {
        setLastError(QString());
        m_lastSuccessfulRefresh = m_store.lastSuccessUtc();
        emit lastSuccessfulRefreshChanged();
        setReady(true);
        recomputeStale();
        ++m_revision;
        emit revisionChanged();
    } else {
        setLastError(failureReason);
        // A failed refresh NEVER revokes an existing good cache (spec §8):
        // ready reflects only whether a snapshot has EVER published.
        setReady(m_store.hasSnapshot());
        recomputeStale();
    }

    m_records.clear();
    m_facetHints.clear();
    m_enrichedCandidates.clear();
    m_mostReadOrder.clear();
    m_classicsOrder.clear();

    emit refreshFinished(success);
}
