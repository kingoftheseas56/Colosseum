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

const QStringList &houseCatalogIds()
{
    static const QStringList ids = {
        QStringLiteral("popular"), QStringLiteral("top-rated"),
        QStringLiteral("new-releases"), QStringLiteral("trending")};
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
    m_queue.clear();
    m_activeCount = 0;
    m_pendingRetries = 0;
    m_anySuccess = false;
    m_enrichmentRemaining = kEnrichmentBudget;

    enqueue(gen, BiblioProviders::appleTopEbooksRssUrl(), QStringLiteral("apple-rss"));
    for (const GenreFeedSeed &g : genreFeeds()) {
        const QString axis = QString::fromLatin1(g.axis);
        const QString key = BiblioTaxonomy::normalize(axis, QString::fromLatin1(g.rawLabel));
        enqueue(gen, BiblioProviders::appleTopEbooksRssUrl(QStringLiteral("us"), 100, g.appleGenreId),
                QStringLiteral("apple-rss"), axis, key);
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

void BiblioCatalog::handleSuccess(int generation, const FetchJob &job, const QByteArray &body)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();

    if (job.kind == QStringLiteral("apple-rss")) {
        const QList<BiblioSourceRecord> records = BiblioProviders::parseAppleRss(body, now);
        for (const BiblioSourceRecord &r : records) {
            if (!job.facetAxis.isEmpty() && !job.facetKey.isEmpty())
                m_facetHints[r.sourceId].append({job.facetAxis, job.facetKey});
            m_records.append(r);

            // Bounded enrichment fan-out, deduped by folded title+author so a
            // book appearing on more than one feed is only enriched once.
            const QString candidateKey = r.normalizedTitle + QChar('|') + r.normalizedAuthor;
            if (r.normalizedTitle.isEmpty())
                continue; // no usable identity to search on
            if (m_enrichedCandidates.contains(candidateKey))
                continue;
            if (m_enrichmentRemaining <= 0)
                continue;
            m_enrichedCandidates.insert(candidateKey);
            --m_enrichmentRemaining;

            enqueue(generation,
                    BiblioProviders::appleSearchUrl(r.title, QStringLiteral("ebook")),
                    QStringLiteral("apple-search"));
            enqueue(generation,
                    BiblioProviders::openLibrarySearchUrl(r.title, r.author),
                    QStringLiteral("openlibrary"));
        }
    } else if (job.kind == QStringLiteral("apple-search")) {
        m_records += BiblioProviders::parseAppleSearch(body, now);
    } else if (job.kind == QStringLiteral("openlibrary")) {
        m_records += BiblioProviders::parseOpenLibrarySearch(body, now);
    }
}

BiblioCatalogSnapshot BiblioCatalog::buildSnapshot(const QList<BiblioCanonicalWork> &canonical) const
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

    for (const QString &catalogId : houseCatalogIds()) {
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
        const QList<BiblioCanonicalWork> canonical = BiblioCanonicalizer::merge(m_records);
        if (canonical.isEmpty()) {
            failureReason = QStringLiteral("refresh failed: no canonical works resolved");
        } else {
            const BiblioCatalogSnapshot snapshot = buildSnapshot(canonical);
            if (m_store.publish(snapshot)) {
                success = true;
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

    emit refreshFinished(success);
}
