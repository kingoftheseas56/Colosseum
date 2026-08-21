// tests/biblio_catalog_service_harness.cpp
//
// Fake-transport oracle for BiblioCatalog (Discover/Explore Task 4, plan
// 2026-08-03): the daily keyless refresh coordinator. Proves request
// coalescing, one refresh per local day, forced refresh, bounded (<=4)
// concurrency, cached first paint, partial-provider failure, total failure
// preserving the prior snapshot, and first-run failure leaving ready==false.
//
// Seeding-flip cases (plan 2026-08-15 Slice 3): Open Library trending/classics/
// subject jobs seed catalog breadth alongside the Apple feeds; most-read/
// classics ranking rows follow payload order (never a re-rank of the merged
// pool); the degradation matrix (OL dead / Apple dead / both dead) proves
// neither provider is the sole critical path; enrichment fan-out order puts
// preview-window (chart-pool) candidates before deep-grid (OL-seeded) ones;
// the subject-seed rotation yields exactly 16 deduped subject jobs per
// generation.
//
// Lazy grid enrichment cases (plan 2026-08-15 Slice 4): requestEnrichment
// bursts park raw apple-search bodies in biblio_pending_enrichment (never the
// published snapshot — revision stays stable), fold them into the NEXT daily
// publish and clear them after; empty/4xx answers park 'unavailable' gated to
// ≤1 re-attempt per local day (tested through the store map + a hand-set
// last_attempt, since the harness cannot fake the clock); a 40-work
// artwork-less grid gets exactly kLazyEnrichmentBudget(12) requests with no
// markers for the capped-out works; the burst is idempotent per session; and
// the daily fan-out consults pending state so an 'enriched' work is never
// re-searched.
//
// FakeBiblioTransport records every get() call (url + kind, inferred from the
// url) and hands back a FakeReply the test completes on its own schedule via
// complete(status, body, error) — nothing auto-responds, so every request's
// timing is explicit and every assertion about "how many are in flight right
// now" is exact. Completion is a direct (same-thread) signal emission, so
// BiblioCatalog's onReplyFinished runs synchronously inside complete(); only
// the bounded-retry backoff needs the event loop pumped (pump()).
//
// Prints BIBLIO_CATALOG_SERVICE_OK and returns 0 only when every require()
// passed.
#include "engine/BiblioCatalog.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDate>
#include <QEventLoop>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVariantMap>

#include <algorithm>
#include <iostream>

namespace {

int g_failures = 0;

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// --- fixture builders -------------------------------------------------------

QByteArray appleRssBody(const QStringList &titles)
{
    QString entries;
    for (int i = 0; i < titles.size(); ++i) {
        if (i) entries += QStringLiteral(",");
        entries += QStringLiteral(R"({
            "im:name":{"label":"%1"},
            "im:artist":{"label":"Author %1"},
            "id":{"attributes":{"im:id":"%2"}},
            "im:image":[{"label":"https://example.test/art.jpg"}],
            "im:releaseDate":{"label":"2021-05-01T00:00:00-07:00"}
        })").arg(titles.at(i), QString::number(1000 + i));
    }
    return QStringLiteral(R"({"feed":{"entry":[%1]}})").arg(entries).toUtf8();
}

QByteArray emptyAppleRssBody()
{
    return QByteArrayLiteral(R"({"feed":{"entry":[]}})");
}

QByteArray appleSearchBody()
{
    return QByteArrayLiteral(R"({"results":[]})");
}

QByteArray openLibraryBody()
{
    return QByteArrayLiteral(R"({"docs":[]})");
}

// One search.json doc object (the shape classics/subject/plain-OL responses
// share). `extraFields` is spliced in verbatim for cases that need more
// (e.g. a huge "readinglog_count" for the contamination negative control).
QString olDocJson(const QString &key, const QString &title, int year,
                  const QString &extraFields = QString())
{
    QString extra;
    if (!extraFields.isEmpty())
        extra = QLatin1Char(',') + extraFields;
    return QStringLiteral(R"({"key":"%1","title":"%2","author_name":["Author %2"],)"
                          R"("author_key":["OL_A_%1"],"first_publish_year":%3,)"
                          R"("language":["en"]%4})")
        .arg(key, title)
        .arg(year)
        .arg(extra);
}

QByteArray openLibraryDocsBody(const QStringList &docs)
{
    return QStringLiteral(R"({"docs":[%1]})").arg(docs.join(QLatin1Char(','))).toUtf8();
}

// A trending/daily.json envelope — {query, works:[...]}. Payload order is the
// ranking; years ride per-entry so a work can be shared with the classics
// fake (Pride-and-Prejudice-style: trending AND classic).
QByteArray openLibraryTrendingBody(const QStringList &keys, const QStringList &titles,
                                   const QStringList &years)
{
    QString works;
    for (int i = 0; i < keys.size(); ++i) {
        if (i) works += QLatin1Char(',');
        works += QStringLiteral(R"({
            "key":"%1",
            "title":"%2",
            "author_name":["Author %2"],
            "author_key":["OL_A_%1"],
            "first_publish_year":%3,
            "language":["en"]
        })").arg(keys.at(i), titles.at(i), years.at(i));
    }
    return QStringLiteral(R"({"query":"test","works":[%1]})").arg(works).toUtf8();
}

// --- fake transport ----------------------------------------------------------

class FakeReply : public BiblioTransportReply {
public:
    FakeReply() : BiblioTransportReply(nullptr) {}

    void cancel() override
    {
        if (m_finished) return;
        m_cancelled = true;
    }

    void complete(int status, const QByteArray &body, const QString &error)
    {
        if (m_finished) return;
        m_finished = true;
        emit finished(status, body, error);
    }

    bool isFinished() const { return m_finished; }
    bool isCancelled() const { return m_cancelled; }

private:
    bool m_finished = false;
    bool m_cancelled = false;
};

QString kindForUrl(const QUrl &url)
{
    const QString s = url.toString();
    if (s.contains(QStringLiteral("topebooks")))
        return QStringLiteral("apple-rss");
    if (s.contains(QStringLiteral("itunes.apple.com/search")))
        return QStringLiteral("apple-search");
    if (s.contains(QStringLiteral("openlibrary.org"))) {
        // NOTE: the subject URL's `fields` list also contains
        // "first_publish_year", so the subject check MUST come before the
        // classics check (the classics solr clause rides inside q=).
        if (s.contains(QStringLiteral("trending/daily.json")))
            return QStringLiteral("openlibrary-trending");
        if (s.contains(QStringLiteral("subject=")))
            return QStringLiteral("openlibrary-subject");
        if (s.contains(QStringLiteral("first_publish_year")))
            return QStringLiteral("openlibrary-classics");
        return QStringLiteral("openlibrary"); // title/author enrichment search
    }
    return QStringLiteral("unknown");
}

// Decoded query-item value (apple-search terms, OL title/subject params).
QString queryItem(const QUrl &url, const QString &key)
{
    return QUrlQuery(url).queryItemValue(key);
}

// IMPORTANT lifetime note: BiblioCatalog owns every reply get() hands back and
// deleteLater()s it once its outcome has been handled (production contract,
// see BiblioCatalog.h). Once the test's event loop is pumped, an
// already-completed FakeReply may actually be destroyed. So `calls` tracks
// completion in its OWN `completed` flag (never by re-querying the reply
// object), and every completion path reads `reply` into a local pointer
// BEFORE marking `completed`, then never touches the container again for
// that entry — safe even though completing one reply can reentrantly append
// new Call entries onto this same QList (RSS success fanning out into
// enrichment requests), which may reallocate the list's backing storage.
class FakeBiblioTransport : public IBiblioTransport {
public:
    struct Call {
        QUrl url;
        QString kind;
        FakeReply *reply = nullptr;
        bool completed = false;
    };
    QList<Call> calls;

    BiblioTransportReply *get(const QUrl &url, const QVariantMap & /*headers*/) override
    {
        auto *reply = new FakeReply();
        calls.append({url, kindForUrl(url), reply, false});
        return reply;
    }

    int pendingCount() const
    {
        int n = 0;
        for (const Call &c : calls)
            if (!c.completed) ++n;
        return n;
    }

    int nextPendingIndex() const
    {
        for (int i = 0; i < calls.size(); ++i)
            if (!calls.at(i).completed) return i;
        return -1;
    }

    void completeAt(int idx, int status, const QByteArray &body, const QString &error)
    {
        if (calls.at(idx).completed) return;
        FakeReply *reply = calls.at(idx).reply; // copy the pointer BEFORE mutating/reentering
        calls[idx].completed = true;             // mark done before any reentrant append can occur
        reply->complete(status, body, error);    // may reentrantly append new Call entries
    }

    // Completes every currently-pending call with a generic success/failure
    // response, looping until nothing new shows up (RSS success fans out
    // into enrichment requests, which this also drains). A hard iteration
    // cap defends against a genuine coordinator bug spinning forever. Does
    // NOT pump the event loop — retried requests (scheduled behind a
    // QTimer backoff) only appear after the caller pumps separately.
    void drainAll(bool succeed = true)
    {
        for (int guard = 0; guard < 500; ++guard) {
            const int idx = nextPendingIndex();
            if (idx < 0) return;
            if (!succeed) {
                completeAt(idx, 0, QByteArray(), QStringLiteral("simulated network failure"));
                continue;
            }
            const QString kind = calls.at(idx).kind;
            if (kind == QStringLiteral("apple-rss"))
                completeAt(idx, 200, appleRssBody({QStringLiteral("Book A"), QStringLiteral("Book B")}), QString());
            else if (kind == QStringLiteral("apple-search"))
                completeAt(idx, 200, appleSearchBody(), QString());
            else if (kind == QStringLiteral("openlibrary"))
                completeAt(idx, 200, openLibraryBody(), QString());
            else
                // The OL seed kinds all parse to zero records from "{}" —
                // the generic drain intentionally produces an Apple-only
                // snapshot (empty most-read/classics rows are valid).
                completeAt(idx, 200, QByteArray("{}"), QString());
        }
    }

    // Fails every currently-pending call whose kind is in `failingKinds` with
    // a transport-level error; succeeds everything else via drainAll's normal
    // per-kind fixtures. Used for the degradation-matrix cases (one provider
    // family dead while the other lives).
    void drainWithKindsFailing(const QSet<QString> &failingKinds)
    {
        for (int guard = 0; guard < 500; ++guard) {
            const int idx = nextPendingIndex();
            if (idx < 0) return;
            const QString kind = calls.at(idx).kind;
            if (failingKinds.contains(kind)) {
                completeAt(idx, 0, QByteArray(), QStringLiteral("simulated provider outage"));
                continue;
            }
            if (kind == QStringLiteral("apple-rss"))
                completeAt(idx, 200, appleRssBody({QStringLiteral("Book A"), QStringLiteral("Book B")}), QString());
            else if (kind == QStringLiteral("apple-search"))
                completeAt(idx, 200, appleSearchBody(), QString());
            else if (kind == QStringLiteral("openlibrary"))
                completeAt(idx, 200, openLibraryBody(), QString());
            else
                completeAt(idx, 200, QByteArray("{}"), QString());
        }
    }

    // Same drain loop, but every succeeding kind answers with the test's own
    // scripted body (kindBody.value(kind), falling back to "{}"); kinds in
    // failingKinds still transport-fail. This is how the seeding-flip cases
    // feed specific trending/classics/subject payloads.
    void drainWithKindBodies(const QHash<QString, QByteArray> &kindBody,
                             const QSet<QString> &failingKinds = QSet<QString>())
    {
        for (int guard = 0; guard < 500; ++guard) {
            const int idx = nextPendingIndex();
            if (idx < 0) return;
            const QString kind = calls.at(idx).kind;
            if (failingKinds.contains(kind)) {
                completeAt(idx, 0, QByteArray(), QStringLiteral("simulated provider outage"));
                continue;
            }
            completeAt(idx, 200, kindBody.value(kind, QByteArrayLiteral("{}")), QString());
        }
    }
};

// Drains whatever is pending, then pumps briefly so any scheduled retry
// timers fire and re-issue (which drainAll alone cannot see), repeating
// until BiblioCatalog reports it is no longer refreshing or the round cap
// is hit (defends against a genuine hang rather than masking one).
void runRefreshToCompletion(BiblioCatalog &catalog, FakeBiblioTransport &transport, bool succeed)
{
    for (int round = 0; round < 50 && catalog.refreshing(); ++round) {
        transport.drainAll(succeed);
        pump(30);
    }
}

void runRefreshToCompletionKindsFailing(BiblioCatalog &catalog, FakeBiblioTransport &transport,
                                        const QSet<QString> &failingKinds)
{
    for (int round = 0; round < 50 && catalog.refreshing(); ++round) {
        transport.drainWithKindsFailing(failingKinds);
        pump(30);
    }
}

// Seeding-flip runner: every kind answers with the scripted body (or
// transport-fails when listed). Same drain+pump round shape as the runners
// above so retry backoffs still resolve.
void runRefreshWithKindBodies(BiblioCatalog &catalog, FakeBiblioTransport &transport,
                              const QHash<QString, QByteArray> &kindBody,
                              const QSet<QString> &failingKinds = QSet<QString>())
{
    for (int round = 0; round < 50 && catalog.refreshing(); ++round) {
        transport.drainWithKindBodies(kindBody, failingKinds);
        pump(30);
    }
}

// --- per-test cases -----------------------------------------------------------

void testCachedFirstPaint()
{
    QTemporaryDir dir;
    require(dir.isValid(), "temp dir created");
    const QString dbPath = dir.path() + QStringLiteral("/pre-seeded.sqlite");

    // Pre-seed a valid snapshot directly through the store, simulating a prior
    // run's successful sync, BEFORE any BiblioCatalog is constructed.
    {
        BiblioCatalogStore seed;
        require(seed.open(dbPath), "seed store opens");
        BiblioCatalogSnapshot snap;
        snap.capturedAt = QDateTime::currentDateTimeUtc();
        BiblioWork w;
        w.canonicalId = QStringLiteral("seed-1");
        w.title = QStringLiteral("Seeded Title");
        w.author = QStringLiteral("Seeded Author");
        snap.works.append(w);
        snap.rankings.append(BiblioCatalogRanking{QStringLiteral("popular"), QStringLiteral("seed-1"), 1.0, 1});
        require(seed.publish(snap), "seed snapshot publishes");
    }

    FakeBiblioTransport transport;
    BiblioCatalog catalog(dbPath, &transport);

    // Cached first paint: ready + browsable BEFORE any refresh/network call.
    require(catalog.ready(), "ready==true immediately from a pre-seeded db");
    require(transport.calls.isEmpty(), "constructing BiblioCatalog issues no network calls");
    const QVariantMap page = catalog.discoverPage(QStringLiteral("popular"), QString(), QString(), true, 0, 10);
    const QVariantList items = page.value(QStringLiteral("items")).toList();
    require(items.size() == 1, "cached data is immediately readable via discoverPage");
    require(items.first().toMap().value(QStringLiteral("canonicalId")).toString() == QStringLiteral("seed-1"),
            "cached data matches the pre-seeded snapshot");
}

void testFirstRunFailureLeavesNotReady()
{
    QTemporaryDir dir;
    const QString dbPath = dir.path() + QStringLiteral("/fresh.sqlite");
    FakeBiblioTransport transport;
    BiblioCatalog catalog(dbPath, &transport);

    require(!catalog.ready(), "brand-new db starts not ready");

    int finishedSignals = 0;
    bool lastSuccess = true;
    QObject::connect(&catalog, &BiblioCatalog::refreshFinished, [&](bool ok) {
        ++finishedSignals; lastSuccess = ok;
    });

    catalog.refreshIfDue();
    require(!transport.calls.isEmpty(), "refreshIfDue on a fresh catalog issues requests");
    runRefreshToCompletion(catalog, transport, /*succeed=*/false);

    require(finishedSignals == 1, "refreshFinished emitted exactly once");
    require(!lastSuccess, "first-run total failure reports refreshFinished(false)");
    require(!catalog.ready(), "first-run failure leaves ready==false");
    require(!catalog.lastError().isEmpty(), "lastError is set after a failed refresh");
    require(catalog.offline(), "offline==true when every request failed");
    require(!catalog.refreshing(), "refreshing drops back to false once finalized");
}

void testRequestCoalescing()
{
    QTemporaryDir dir;
    const QString dbPath = dir.path() + QStringLiteral("/coalesce.sqlite");
    FakeBiblioTransport transport;
    BiblioCatalog catalog(dbPath, &transport);

    catalog.refreshIfDue();
    const int callsAfterFirst = transport.calls.size();
    require(callsAfterFirst > 0, "first refreshIfDue issues requests");

    // A second non-forced call while the first is still in flight must not
    // issue any additional requests (coalesced).
    catalog.refreshIfDue();
    catalog.refreshIfDue();
    require(transport.calls.size() == callsAfterFirst,
            "coalesced refreshIfDue calls issue no extra requests");

    runRefreshToCompletion(catalog, transport, true);
    require(!catalog.refreshing(), "refresh completes after draining");
}

void testOneRefreshPerLocalDayThenForced()
{
    QTemporaryDir dir;
    const QString dbPath = dir.path() + QStringLiteral("/dayGate.sqlite");
    FakeBiblioTransport transport;
    BiblioCatalog catalog(dbPath, &transport);

    catalog.refreshIfDue();
    runRefreshToCompletion(catalog, transport, true);
    require(catalog.ready(), "first refresh today publishes a snapshot");
    const int callsAfterFirstRefresh = transport.calls.size();

    // Same local day, not forced: no new requests.
    catalog.refreshIfDue();
    require(transport.calls.size() == callsAfterFirstRefresh,
            "a non-forced refreshIfDue on the same local day issues no new requests");

    // Forced: bypasses the day-gate, issues new requests.
    catalog.refreshIfDue(/*force=*/true);
    require(transport.calls.size() > callsAfterFirstRefresh,
            "a forced refreshIfDue issues new requests even on the same local day");
    runRefreshToCompletion(catalog, transport, true);
    require(!catalog.refreshing(), "forced refresh also completes");
}

void testBoundedConcurrency()
{
    QTemporaryDir dir;
    const QString dbPath = dir.path() + QStringLiteral("/concurrency.sqlite");
    FakeBiblioTransport transport;
    BiblioCatalog catalog(dbPath, &transport);

    catalog.refreshIfDue();
    // The RSS seed alone (1 global + 5 genre/audience feeds == 6 candidate
    // URLs) exceeds the 4-request concurrency cap, so exactly 4 must be
    // in flight immediately and the rest queued.
    require(transport.pendingCount() == 4, "at most 4 requests are in flight at once");
    require(transport.calls.size() == 4, "only 4 requests issued before any completes");

    // Completing in-flight requests must never push the in-flight count over 4,
    // even as fresh RSS/enrichment jobs get queued behind them.
    int maxObservedPending = 0;
    for (int guard = 0; guard < 500; ++guard) {
        const int idx = transport.nextPendingIndex();
        if (idx < 0) break;
        const QString kind = transport.calls.at(idx).kind;
        if (kind == QStringLiteral("apple-rss"))
            transport.completeAt(idx, 200, appleRssBody({QStringLiteral("Book A")}), QString());
        else
            transport.completeAt(idx, 200, QByteArray("{}"), QString());
        maxObservedPending = std::max(maxObservedPending, transport.pendingCount());
    }
    require(maxObservedPending <= 4, "in-flight request count never exceeds the concurrency cap");
    runRefreshToCompletion(catalog, transport, true);
    require(!catalog.refreshing(), "refresh completes once every job (incl. retries) drains");
}

void testPartialProviderFailureStillPublishes()
{
    QTemporaryDir dir;
    const QString dbPath = dir.path() + QStringLiteral("/partial.sqlite");
    FakeBiblioTransport transport;
    BiblioCatalog catalog(dbPath, &transport);

    bool finished = false, success = false;
    QObject::connect(&catalog, &BiblioCatalog::refreshFinished, [&](bool ok) {
        finished = true; success = ok;
    });

    catalog.refreshIfDue();
    // Open Library totally fails; Apple RSS/Search still succeed.
    runRefreshToCompletionKindsFailing(catalog, transport, {QStringLiteral("openlibrary")});

    require(finished, "refreshFinished fires after a partial-provider failure");
    require(success, "partial-provider failure (Apple ok, Open Library down) still publishes");
    require(catalog.ready(), "ready==true after a partial-provider success");
    const QVariantMap page = catalog.discoverPage(QStringLiteral("popular"), QString(), QString(), true, 0, 50);
    require(!page.value(QStringLiteral("items")).toList().isEmpty(),
            "the published snapshot carries the Apple-sourced works");
}

void testTotalFailurePreservesOldSnapshot()
{
    QTemporaryDir dir;
    const QString dbPath = dir.path() + QStringLiteral("/preserve.sqlite");
    FakeBiblioTransport transport;
    BiblioCatalog catalog(dbPath, &transport);

    // First refresh succeeds with BOTH provider families feeding it, so the
    // snapshot that must survive carries rows in the new catalogues too.
    const QHash<QString, QByteArray> goodBodies = {
        {QStringLiteral("apple-rss"), appleRssBody({QStringLiteral("Book A")})},
        {QStringLiteral("apple-search"), appleSearchBody()},
        {QStringLiteral("openlibrary"), openLibraryBody()},
        {QStringLiteral("openlibrary-trending"),
         openLibraryTrendingBody({QStringLiteral("/works/OLT1")},
                                 {QStringLiteral("Trend One")}, {QStringLiteral("2015")})},
        {QStringLiteral("openlibrary-classics"),
         openLibraryDocsBody({olDocJson(QStringLiteral("/works/OLC1"),
                                        QStringLiteral("Classic One"), 1861)})},
        {QStringLiteral("openlibrary-subject"), openLibraryDocsBody({})},
    };
    catalog.refreshIfDue();
    runRefreshWithKindBodies(catalog, transport, goodBodies);
    require(catalog.ready(), "initial refresh publishes a good snapshot");
    const QVariantMap before = catalog.discoverPage(QStringLiteral("popular"), QString(), QString(), true, 0, 50);
    const int beforeCount = before.value(QStringLiteral("items")).toList().size();
    require(beforeCount > 0, "initial snapshot has items to compare against");
    const QVariantList beforeMostRead = catalog.discoverPage(
        QStringLiteral("most-read"), QString(), QString(), true, 0, 50)
                                            .value(QStringLiteral("items")).toList();
    require(beforeMostRead.size() == 1, "initial snapshot carries a most-read row");
    const QDateTime successBefore = catalog.lastSuccessfulRefresh();

    bool finished = false, success = true;
    QObject::connect(&catalog, &BiblioCatalog::refreshFinished, [&](bool ok) {
        finished = true; success = ok;
    });

    // Forced refresh where EVERY request fails outright — Apple AND Open
    // Library seed jobs included.
    catalog.refreshIfDue(/*force=*/true);
    runRefreshToCompletion(catalog, transport, false);

    require(finished, "refreshFinished fires after a total failure");
    require(!success, "a total failure reports refreshFinished(false)");
    require(catalog.ready(), "ready stays true: the prior snapshot is untouched");
    require(!catalog.lastError().isEmpty(), "lastError is set after a total failure");
    require(catalog.lastSuccessfulRefresh() == successBefore,
            "lastSuccessfulRefresh does not advance on a failed refresh");

    const QVariantMap after = catalog.discoverPage(QStringLiteral("popular"), QString(), QString(), true, 0, 50);
    require(after.value(QStringLiteral("items")).toList().size() == beforeCount,
            "the OLD snapshot's items are exactly what discoverPage still returns");
    const QVariantList afterMostRead = catalog.discoverPage(
        QStringLiteral("most-read"), QString(), QString(), true, 0, 50)
                                           .value(QStringLiteral("items")).toList();
    require(afterMostRead.size() == beforeMostRead.size(),
            "the OLD snapshot's most-read rows also survive a total failure");
}

// Regression for the retry-generation race: generation 1 parks a retry in a
// QTimer::singleShot backoff (m_pendingRetries -> 1) via a transient (429)
// failure. Before that timer fires, a FORCED refresh starts generation 2,
// which resets m_pendingRetries to 0. Generation 2 then ALSO parks its own
// retry (m_pendingRetries -> 1). Generation 1's now-stale timer fires first
// (scheduled first, identical base delay) -- with the bug, its unconditional
// `--m_pendingRetries` (BEFORE the generation check) steals generation 2's
// count down to 0; when generation 2's own retry timer then fires, its
// decrement drives the counter to -1, which maybeFinalize's `== 0` check can
// never see again, so `refreshing` never returns to false. The fix moves the
// generation check before any shared-state mutation, so a stale-generation
// timer firing is a total no-op.
void testForcedRefreshDuringPendingRetryDoesNotWedgeRefreshing()
{
    QTemporaryDir dir;
    const QString dbPath = dir.path() + QStringLiteral("/retry-race.sqlite");
    FakeBiblioTransport transport;
    BiblioCatalog catalog(dbPath, &transport);

    int finishedSignals = 0;
    bool lastSuccess = false;
    QObject::connect(&catalog, &BiblioCatalog::refreshFinished, [&](bool ok) {
        ++finishedSignals; lastSuccess = ok;
    });

    catalog.refreshIfDue();
    require(transport.calls.size() == 4, "generation 1 issues its initial concurrency-capped batch");

    // Fail ONE generation-1 request with a transient 429: BiblioCatalog
    // schedules a retry (m_pendingRetries -> 1) and parks it behind a
    // QTimer::singleShot backoff -- the event loop has NOT been pumped, so
    // that timer has not fired yet.
    transport.completeAt(0, 429, QByteArray(), QString());
    require(catalog.refreshing(), "generation 1 is still mid-flight after one 429");

    // Capture where generation 2's calls will start BEFORE forcing the
    // refresh, so we can address a generation-2 request specifically
    // (transport.calls also still carries generation 1's now-cancelled, but
    // not-yet-completed-in-the-fake, entries -- nextPendingIndex() alone
    // cannot distinguish them from live generation-2 calls).
    const int gen2Start = transport.calls.size();

    // While generation 1's retry is still parked, force a refresh. This
    // cancels generation 1's remaining in-flight requests (cancelGeneration
    // only reaches m_inFlight, never the parked retry timer) and starts
    // generation 2, which resets m_pendingRetries to 0.
    catalog.refreshIfDue(/*force=*/true);
    require(catalog.refreshing(), "generation 2 is now the active refresh");
    require(transport.calls.size() > gen2Start, "generation 2 issued its own requests");

    // Generation 2 must ALSO park a retry (its own m_pendingRetries -> 1) --
    // this reproduces the exact race described above.
    transport.completeAt(gen2Start, 429, QByteArray(), QString());

    // Let every timer fire (generation 1's stale one first -- scheduled
    // first, identical base delay -- then generation 2's own), draining
    // everything else to success so the refresh can actually finish.
    for (int round = 0; round < 50 && catalog.refreshing(); ++round) {
        transport.drainAll(true);
        pump(30);
    }

    require(!catalog.refreshing(),
            "refreshing returns to false: a stale-generation retry timer must never "
            "corrupt the CURRENT generation's pendingRetries counter");
    require(finishedSignals == 1,
            "refreshFinished fires exactly once (for generation 2 only -- generation 1 "
            "was abandoned by the forced refresh and must never finalize)");
    require(lastSuccess, "generation 2 still publishes successfully once its own retry resolves");
}

void testFilterGroupsAndExploreRowsAndMosaicProxy()
{
    QTemporaryDir dir;
    const QString dbPath = dir.path() + QStringLiteral("/proxy.sqlite");
    FakeBiblioTransport transport;
    BiblioCatalog catalog(dbPath, &transport);

    catalog.refreshIfDue();
    runRefreshToCompletion(catalog, transport, true);
    require(catalog.ready(), "refresh publishes before exercising the read proxies");

    const QVariantList groups = catalog.filterGroups(true);
    require(!groups.empty(), "filterGroups proxies the store's controlled axes");

    const QVariantList rows = catalog.exploreRows(10, true);
    require(rows.size() == 6, "exploreRows returns exactly the six house catalogues");
    QSet<QString> seenCatalogIds;
    for (const QVariant &row : rows)
        seenCatalogIds.insert(row.toMap().value(QStringLiteral("catalogId")).toString());
    require(seenCatalogIds.contains(QStringLiteral("popular")), "exploreRows includes popular");
    require(seenCatalogIds.contains(QStringLiteral("trending")), "exploreRows includes trending");
    require(seenCatalogIds.contains(QStringLiteral("most-read")), "exploreRows includes most-read");
    require(seenCatalogIds.contains(QStringLiteral("classics")), "exploreRows includes classics");
    require(rows.constFirst().toMap().value(QStringLiteral("catalogId")).toString()
                == QStringLiteral("popular"),
            "exploreRows keeps popular first in the fixed six-entry order");
    require(rows.constLast().toMap().value(QStringLiteral("catalogId")).toString()
                == QStringLiteral("classics"),
            "exploreRows keeps classics last in the fixed six-entry order");

    // mosaic() with an unknown key returns empty rather than crashing.
    require(catalog.mosaic(QStringLiteral("not-a-real-facet-key"), 10, true).isEmpty(),
            "mosaic with an unknown facet key returns an empty list");
}

// --- seeding-flip helpers ------------------------------------------------------

QVariantList pageItems(BiblioCatalog &catalog, const QString &catalogId,
                       const QString &axis = QString(), const QString &key = QString())
{
    return catalog.discoverPage(catalogId, axis, key, true, 0, 50)
        .value(QStringLiteral("items")).toList();
}

QString itemTitle(const QVariant &v)
{
    return v.toMap().value(QStringLiteral("title")).toString();
}

int countItemsTitled(const QVariantList &items, const QString &title)
{
    int n = 0;
    for (const QVariant &v : items)
        if (itemTitle(v) == title)
            ++n;
    return n;
}

int firstAppleSearchTermIndex(const FakeBiblioTransport &transport, const QString &term)
{
    for (int i = 0; i < transport.calls.size(); ++i) {
        const auto &c = transport.calls.at(i);
        if (c.kind == QStringLiteral("apple-search")
            && queryItem(c.url, QStringLiteral("term")) == term)
            return i;
    }
    return -1;
}

int countCalls(const FakeBiblioTransport &transport, const QString &kind,
               const QString &queryKeyName, const QString &queryValue)
{
    int n = 0;
    for (const auto &c : transport.calls) {
        if (c.kind == kind && queryItem(c.url, queryKeyName) == queryValue)
            ++n;
    }
    return n;
}

int countKind(const FakeBiblioTransport &transport, const QString &kind)
{
    int n = 0;
    for (const auto &c : transport.calls)
        if (c.kind == kind) ++n;
    return n;
}

// ── lazy grid enrichment helpers (plan 2026-08-15 Slice 4) ────────────────

// An apple-search body whose single result carries real artwork + rating
// evidence for (title, author) — the "usable record" the lazy path parks.
QByteArray appleSearchHitBody(const QString &title, const QString &author, qint64 trackId)
{
    return QStringLiteral(
               "{\"results\":[{"
               "\"wrapperType\":\"track\","
               "\"trackName\":\"%1\","
               "\"artistName\":\"%2\","
               "\"trackId\":%3,"
               "\"artworkUrl100\":\"https://example.test/lazy-%3.jpg\","
               "\"averageUserRating\":4.75,"
               "\"userRatingCount\":321"
               "}]}")
        .arg(title, author)
        .arg(trackId)
        .toUtf8();
}

// The enrichment candidate identity the engine dedupes on — computed through
// the SAME foldTitleAuthor the parsers use, so harness-computed keys always
// match engine-computed ones.
QString workKeyFor(const QString &title, const QString &author)
{
    return BiblioProviders::foldTitleAuthor(title) + QChar('|')
         + BiblioProviders::foldTitleAuthor(author);
}

// Completes every currently-pending fake call with `body` (status 200). After
// a refresh has fully drained, the only pending calls are the lazy burst's.
int completeAllPending(FakeBiblioTransport &transport, const QByteArray &body)
{
    int n = 0;
    for (int guard = 0; guard < 200; ++guard) {
        const int idx = transport.nextPendingIndex();
        if (idx < 0) break;
        transport.completeAt(idx, 200, body, QString());
        ++n;
    }
    return n;
}

// --- seeding-flip cases (plan 2026-08-15 Slice 3) ------------------------------

// Happy path: Apple chart feeds + OL trending/classics/subject all answer.
// Six shelves publish; a work present ONLY in the trending fake appears in the
// snapshot AND sits in most-read at its payload position; a classics-only work
// likewise; a work in BOTH payloads (Pride-and-Prejudice-style) is ONE
// canonical work appearing once in each catalogue's ranking; the subject-seed
// hint rides the same facet path as the RSS genre tags.
void testSeedingFlipHappyPath()
{
    QTemporaryDir dir;
    const QString dbPath = dir.path() + QStringLiteral("/seedflip.sqlite");
    FakeBiblioTransport transport;
    BiblioCatalog catalog(dbPath, &transport);

    bool finished = false, success = false;
    QObject::connect(&catalog, &BiblioCatalog::refreshFinished, [&](bool ok) {
        finished = true; success = ok;
    });

    const QHash<QString, QByteArray> bodies = {
        {QStringLiteral("apple-rss"), appleRssBody({QStringLiteral("Chart Book")})},
        {QStringLiteral("apple-search"), appleSearchBody()},
        {QStringLiteral("openlibrary"), openLibraryDocsBody({})},
        {QStringLiteral("openlibrary-trending"),
         openLibraryTrendingBody({QStringLiteral("/works/OLTP"), QStringLiteral("/works/OLT2")},
                                 {QStringLiteral("Pride Test"), QStringLiteral("Trend Two")},
                                 {QStringLiteral("1813"), QStringLiteral("2019")})},
        {QStringLiteral("openlibrary-classics"),
         openLibraryDocsBody({olDocJson(QStringLiteral("/works/OLTP"),
                                        QStringLiteral("Pride Test"), 1813),
                              olDocJson(QStringLiteral("/works/OLC2"),
                                        QStringLiteral("Classic Two"), 1871)})},
        {QStringLiteral("openlibrary-subject"),
         openLibraryDocsBody({olDocJson(QStringLiteral("/works/OLSJ"),
                                        QStringLiteral("Subject Seedling"), 1998)})},
    };

    catalog.refreshIfDue();
    runRefreshWithKindBodies(catalog, transport, bodies);

    require(finished && success, "seeding-flip happy path publishes a snapshot");
    require(catalog.ready(), "ready==true after the seeded refresh");
    require(catalog.exploreRows(10, true).size() == 6, "exploreRows exposes six shelves");

    // most-read: payload order of the trending fake — the shared classic
    // first (it led the payload), the trending-only work second at ITS
    // payload position.
    const QVariantList mostRead = pageItems(catalog, QStringLiteral("most-read"));
    require(mostRead.size() == 2, "most-read carries exactly the two trending-payload works");
    require(itemTitle(mostRead.at(0)) == QStringLiteral("Pride Test"),
            "most-read rank 1 is the trending payload's first work");
    require(mostRead.at(0).toMap().value(QStringLiteral("rank")).toInt() == 1,
            "most-read rank 1 stamps rank==1");
    require(itemTitle(mostRead.at(1)) == QStringLiteral("Trend Two"),
            "most-read rank 2 is the trending-only work at its payload position");

    // classics: its own payload order; the classics-only work present.
    const QVariantList classics = pageItems(catalog, QStringLiteral("classics"));
    require(classics.size() == 2, "classics carries exactly the two classics-payload works");
    require(itemTitle(classics.at(0)) == QStringLiteral("Pride Test"),
            "classics rank 1 is the classics payload's first work (same canonical work as most-read's)");
    require(itemTitle(classics.at(1)) == QStringLiteral("Classic Two"),
            "classics rank 2 is the classics-only work");

    // Breadth: the trending-only work exists in the snapshot at all (it is in
    // NO Apple fake), and the shared work merged to ONE canonical work.
    const QVariantList popular = pageItems(catalog, QStringLiteral("popular"));
    require(countItemsTitled(popular, QStringLiteral("Trend Two")) == 1,
            "a work present only in the trending fake is seeded into the snapshot");
    require(countItemsTitled(popular, QStringLiteral("Subject Seedling")) == 1,
            "a subject-seeded work is seeded into the snapshot");
    require(countItemsTitled(popular, QStringLiteral("Pride Test")) == 1,
            "the trending+classics work is ONE canonical work despite two payloads");

    // The subject hint rides the same facet path the RSS genre tags use: the
    // first seed entry is (genre, science-fiction), so the seedling is
    // filterable under it.
    const QVariantList scifi = pageItems(catalog, QStringLiteral("popular"),
                                         QStringLiteral("genre"), QStringLiteral("science-fiction"));
    require(countItemsTitled(scifi, QStringLiteral("Subject Seedling")) == 1,
            "subject-seeded records carry the seed's facet hint (genre/science-fiction)");

    // Enrichment dedupe across payloads: the shared work and the seedling
    // (returned by all 16 subject jobs) each fan out exactly once.
    require(countCalls(transport, QStringLiteral("apple-search"),
                       QStringLiteral("term"), QStringLiteral("Pride Test")) == 1,
            "enrichment fans out once for a work seen in two OL payloads");
    require(countCalls(transport, QStringLiteral("apple-search"),
                       QStringLiteral("term"), QStringLiteral("Subject Seedling")) == 1,
            "enrichment fans out once for a work seen in 16 subject payloads");
}

// Contamination negative control: an Apple-chart work whose Open Library
// enrichment returns a HUGE readinglog count must NOT appear in most-read —
// it never rode the trending payload. Proves the ordering source is payload
// order, not openLibraryPopularity over the merged pool (the two scales are
// incommensurable: trending payload index vs all-time readinglog counts).
void testMostReadUncontaminatedByChartPool()
{
    QTemporaryDir dir;
    const QString dbPath = dir.path() + QStringLiteral("/contamination.sqlite");
    FakeBiblioTransport transport;
    BiblioCatalog catalog(dbPath, &transport);

    bool finished = false, success = false;
    QObject::connect(&catalog, &BiblioCatalog::refreshFinished, [&](bool ok) {
        finished = true; success = ok;
    });

    const QHash<QString, QByteArray> bodies = {
        {QStringLiteral("apple-rss"), appleRssBody({QStringLiteral("Chart Book")})},
        {QStringLiteral("apple-search"), appleSearchBody()},
        // The chart book's OL title/author enrichment: same title+author
        // identity, astronomical readinglog count.
        {QStringLiteral("openlibrary"),
         openLibraryDocsBody({olDocJson(QStringLiteral("/works/OLCH"),
                                        QStringLiteral("Chart Book"), 2001,
                                        QStringLiteral("\"readinglog_count\":999999"))})},
        {QStringLiteral("openlibrary-trending"),
         openLibraryTrendingBody({QStringLiteral("/works/OLTM1"), QStringLiteral("/works/OLTM2")},
                                 {QStringLiteral("Trend One"), QStringLiteral("Trend Two")},
                                 {QStringLiteral("2015"), QStringLiteral("2016")})},
        {QStringLiteral("openlibrary-classics"), openLibraryDocsBody({})},
        {QStringLiteral("openlibrary-subject"), openLibraryDocsBody({})},
    };

    catalog.refreshIfDue();
    runRefreshWithKindBodies(catalog, transport, bodies);

    require(finished && success, "contamination-control refresh publishes");
    require(catalog.ready(), "ready==true");

    // Non-vacuous negative: the chart book IS in the snapshot (its OL record
    // merged; its popularity signal dwarfs the trending proxy values 2 and 1).
    const QVariantList popular = pageItems(catalog, QStringLiteral("popular"));
    require(countItemsTitled(popular, QStringLiteral("Chart Book")) == 1,
            "the chart-pool work with the huge readinglog IS in the snapshot");

    // The actual control: most-read is exactly the trending payload, in
    // payload order, with no chart-pool contamination.
    const QVariantList mostRead = pageItems(catalog, QStringLiteral("most-read"));
    require(mostRead.size() == 2, "most-read carries only the two trending-payload works");
    require(itemTitle(mostRead.at(0)) == QStringLiteral("Trend One"),
            "most-read rank 1 follows the trending payload order, not popularity");
    require(countItemsTitled(mostRead, QStringLiteral("Chart Book")) == 0,
            "a chart-pool work with a huge readinglog never contaminates most-read");
}

// Negative control A (OL dead): trending/classics/subject jobs all
// transport-error. Apple still publishes the four chart catalogues; the two
// payload-ordered catalogues carry ZERO ranking rows — a valid publish (the
// store imposes no per-catalogue minimum).
void testOpenLibraryDeadStillPublishes()
{
    QTemporaryDir dir;
    const QString dbPath = dir.path() + QStringLiteral("/oldead.sqlite");
    FakeBiblioTransport transport;
    BiblioCatalog catalog(dbPath, &transport);

    bool finished = false, success = false;
    QObject::connect(&catalog, &BiblioCatalog::refreshFinished, [&](bool ok) {
        finished = true; success = ok;
    });

    const QHash<QString, QByteArray> bodies = {
        {QStringLiteral("apple-rss"), appleRssBody({QStringLiteral("Chart Book")})},
        {QStringLiteral("apple-search"), appleSearchBody()},
        {QStringLiteral("openlibrary"), openLibraryDocsBody({})},
    };
    const QSet<QString> failing = {
        QStringLiteral("openlibrary-trending"),
        QStringLiteral("openlibrary-classics"),
        QStringLiteral("openlibrary-subject"),
    };

    catalog.refreshIfDue();
    runRefreshWithKindBodies(catalog, transport, bodies, failing);

    require(finished && success, "OL-dead refresh still publishes (OL is not the sole critical path)");
    require(catalog.ready(), "ready==true after an OL-dead publish");
    require(!pageItems(catalog, QStringLiteral("popular")).isEmpty(),
            "the four chart catalogues are populated from Apple alone");
    require(pageItems(catalog, QStringLiteral("most-read")).isEmpty(),
            "most-read ranking rows are empty when the trending payload never arrived");
    require(pageItems(catalog, QStringLiteral("classics")).isEmpty(),
            "classics ranking rows are empty when the classics payload never arrived");
}

// Negative control B (Apple dead): rss + apple-search all fail. The OL-only
// snapshot still publishes, six shelves still expose.
void testAppleDeadPublishesOpenLibraryOnly()
{
    QTemporaryDir dir;
    const QString dbPath = dir.path() + QStringLiteral("/appledead.sqlite");
    FakeBiblioTransport transport;
    BiblioCatalog catalog(dbPath, &transport);

    bool finished = false, success = false;
    QObject::connect(&catalog, &BiblioCatalog::refreshFinished, [&](bool ok) {
        finished = true; success = ok;
    });

    const QHash<QString, QByteArray> bodies = {
        {QStringLiteral("openlibrary"), openLibraryDocsBody({})},
        {QStringLiteral("openlibrary-trending"),
         openLibraryTrendingBody({QStringLiteral("/works/OLT1"), QStringLiteral("/works/OLT2")},
                                 {QStringLiteral("Trend One"), QStringLiteral("Trend Two")},
                                 {QStringLiteral("2015"), QStringLiteral("2016")})},
        {QStringLiteral("openlibrary-classics"),
         openLibraryDocsBody({olDocJson(QStringLiteral("/works/OLC1"),
                                        QStringLiteral("Classic One"), 1861)})},
        {QStringLiteral("openlibrary-subject"), openLibraryDocsBody({})},
    };
    const QSet<QString> failing = {
        QStringLiteral("apple-rss"),
        QStringLiteral("apple-search"),
    };

    catalog.refreshIfDue();
    runRefreshWithKindBodies(catalog, transport, bodies, failing);

    require(finished && success, "Apple-dead refresh still publishes (Apple is not the sole critical path)");
    require(catalog.ready(), "ready==true after an Apple-dead publish");
    require(catalog.exploreRows(10, true).size() == 6,
            "exploreRows still exposes six shelves on an OL-only snapshot");
    const QVariantList mostRead = pageItems(catalog, QStringLiteral("most-read"));
    require(mostRead.size() == 2, "most-read rows come from the OL trending payload alone");
    require(pageItems(catalog, QStringLiteral("classics")).size() == 1,
            "classics rows come from the OL classics payload alone");
}

// Enrichment pacing, observable in the fake's recorded request order: the
// chart-pool (preview-window) candidates' apple-search jobs are issued BEFORE
// the OL-seeded (deep-grid) candidates' — the RSS jobs ride the head of the
// FIFO queue, so their fan-out enqueues first. Also proves the flip's fan-out
// shape: apple-rss keeps BOTH apple-search + openlibrary enrichment; the OL
// branches enqueue apple-search ONLY (the work already IS Open Library data).
void testEnrichmentOrderPreviewsBeforeDeepGrid()
{
    QTemporaryDir dir;
    const QString dbPath = dir.path() + QStringLiteral("/enrichorder.sqlite");
    FakeBiblioTransport transport;
    BiblioCatalog catalog(dbPath, &transport);

    const QHash<QString, QByteArray> bodies = {
        {QStringLiteral("apple-rss"),
         appleRssBody({QStringLiteral("Chart Alpha"), QStringLiteral("Chart Beta")})},
        {QStringLiteral("apple-search"), appleSearchBody()},
        {QStringLiteral("openlibrary"), openLibraryDocsBody({})},
        {QStringLiteral("openlibrary-trending"),
         openLibraryTrendingBody({QStringLiteral("/works/OLT1")},
                                 {QStringLiteral("Trend One")}, {QStringLiteral("2015")})},
        {QStringLiteral("openlibrary-classics"), openLibraryDocsBody({})},
        {QStringLiteral("openlibrary-subject"), openLibraryDocsBody({})},
    };

    catalog.refreshIfDue();
    runRefreshWithKindBodies(catalog, transport, bodies);
    require(!catalog.refreshing(), "enrichment-order refresh completes");

    const int chartAlpha = firstAppleSearchTermIndex(transport, QStringLiteral("Chart Alpha"));
    const int chartBeta = firstAppleSearchTermIndex(transport, QStringLiteral("Chart Beta"));
    const int trendOne = firstAppleSearchTermIndex(transport, QStringLiteral("Trend One"));
    require(chartAlpha >= 0, "chart-pool candidate Chart Alpha received an apple-search job");
    require(chartBeta >= 0, "chart-pool candidate Chart Beta received an apple-search job");
    require(trendOne >= 0, "OL-seeded candidate Trend One received an apple-search job");
    require(chartAlpha < trendOne,
            "preview-window (chart-pool) candidates enrich before deep-grid (OL-seeded) ones");
    require(chartBeta < trendOne,
            "every chart-pool candidate enriches before the OL-seeded candidates");

    // Fan-out shape: dual for apple-rss, apple-search-only for the OL branches.
    require(countCalls(transport, QStringLiteral("openlibrary"),
                       QStringLiteral("title"), QStringLiteral("Chart Alpha")) == 1,
            "apple-rss enrichment still searches Open Library too (dual fan-out kept)");
    require(countCalls(transport, QStringLiteral("openlibrary"),
                       QStringLiteral("title"), QStringLiteral("Trend One")) == 0,
            "OL-seeded works are never re-searched on Open Library");
}

// Subject-seed budget: the checked-in rotation yields exactly 16 subject jobs
// per refresh, all distinct URLs (deduped per generation; a forced second
// refresh issues 16 fresh ones, not zero and not 32 in one generation).
void testSubjectSeedBudget()
{
    QTemporaryDir dir;
    const QString dbPath = dir.path() + QStringLiteral("/subjectbudget.sqlite");
    FakeBiblioTransport transport;
    BiblioCatalog catalog(dbPath, &transport);

    const QHash<QString, QByteArray> bodies = {
        {QStringLiteral("apple-rss"), appleRssBody({QStringLiteral("Budget Book")})},
        {QStringLiteral("apple-search"), appleSearchBody()},
        {QStringLiteral("openlibrary"), openLibraryDocsBody({})},
        {QStringLiteral("openlibrary-trending"), openLibraryDocsBody({})},
        {QStringLiteral("openlibrary-classics"), openLibraryDocsBody({})},
        {QStringLiteral("openlibrary-subject"), openLibraryDocsBody({})},
    };

    auto subjectCallCount = [&transport](int *distinctUrls) {
        int n = 0;
        QSet<QString> urls;
        for (const auto &c : transport.calls) {
            if (c.kind == QStringLiteral("openlibrary-subject")) {
                ++n;
                urls.insert(c.url.toString());
            }
        }
        *distinctUrls = urls.size();
        return n;
    };

    catalog.refreshIfDue();
    runRefreshWithKindBodies(catalog, transport, bodies);
    require(catalog.ready(), "budget refresh publishes");

    int distinct = 0;
    const int firstRun = subjectCallCount(&distinct);
    require(firstRun == 16, "exactly 16 subject jobs are issued per refresh");
    require(distinct == 16, "the 16 subject URLs are distinct (deduped, one per seed key)");

    // Forced second refresh: the per-generation dedupe resets, so 16 more
    // issue — never 32 in one generation, never zero after the first.
    catalog.refreshIfDue(/*force=*/true);
    runRefreshWithKindBodies(catalog, transport, bodies);
    require(!catalog.refreshing(), "forced budget refresh completes");
    int distinct2 = 0;
    const int secondRunTotal = subjectCallCount(&distinct2);
    require(secondRunTotal == 32, "a forced refresh issues 16 NEW subject jobs (per-generation dedupe)");
    require(distinct2 == 16, "across both refreshes only the same 16 distinct subject URLs exist");
}

// --- lazy grid enrichment cases (plan 2026-08-15 Slice 4) --------------------

// Fold-in happy path: day-1 publish leaves a trending work artwork-less;
// requestEnrichment("most-read") fetches it, parks the raw body as 'enriched',
// and the PUBLISHED snapshot is untouched (revision stable, artwork still
// empty). The next (forced) daily refresh folds the parked body into the
// publish — artwork + rating appear, rank is unchanged, and the row is
// cleared. The day-2 pass also proves the pending consultation: the covered
// work is NOT re-searched.
void testLazyEnrichmentFoldIn()
{
    QTemporaryDir dir;
    const QString dbPath = dir.path() + QStringLiteral("/lazy-foldin.sqlite");
    FakeBiblioTransport transport;
    BiblioCatalog catalog(dbPath, &transport);

    bool finished = false, success = false;
    QObject::connect(&catalog, &BiblioCatalog::refreshFinished, [&](bool ok) {
        finished = true; success = ok;
    });

    const QString workTitle = QStringLiteral("Lazy Work");
    const QString workAuthor = QStringLiteral("Author Lazy Work");
    const QString workKey = workKeyFor(workTitle, workAuthor);

    const QHash<QString, QByteArray> bodies = {
        {QStringLiteral("apple-rss"), appleRssBody({QStringLiteral("Chart Book")})},
        {QStringLiteral("apple-search"), appleSearchBody()}, // daily fan-out finds nothing usable
        {QStringLiteral("openlibrary"), openLibraryDocsBody({})},
        {QStringLiteral("openlibrary-trending"),
         openLibraryTrendingBody({QStringLiteral("/works/OLLZ")}, {workTitle},
                                 {QStringLiteral("2011")})},
        {QStringLiteral("openlibrary-classics"), openLibraryDocsBody({})},
        {QStringLiteral("openlibrary-subject"), openLibraryDocsBody({})},
    };

    catalog.refreshIfDue();
    runRefreshWithKindBodies(catalog, transport, bodies);
    require(finished && success, "day-1 refresh publishes (chart + trending bodies)");
    require(catalog.ready(), "ready after day-1 publish");
    const int revisionDay1 = catalog.revision();

    const QVariantList mostRead1 = pageItems(catalog, QStringLiteral("most-read"));
    require(mostRead1.size() == 1, "day-1 most-read carries the single trending work");
    const QVariantMap item1 = mostRead1.first().toMap();
    require(item1.value(QStringLiteral("coverUrl")).toString().isEmpty(),
            "day-1 published artwork is empty (the trending fake carries no cover)");
    require(item1.value(QStringLiteral("rating")).toMap()
                .value(QStringLiteral("count")).toInt() == 0,
            "day-1 published rating evidence is empty");
    require(item1.value(QStringLiteral("rank")).toInt() == 1, "day-1 rank is 1");

    // ── Lazy burst ──
    const int searchedDay1 = countCalls(transport, QStringLiteral("apple-search"),
                                        QStringLiteral("term"), workTitle);
    require(searchedDay1 == 1,
            "the day-1 daily fan-out already searched the work once (empty result)");
    catalog.requestEnrichment(QStringLiteral("most-read"));
    const int searchedAfterLazy = countCalls(transport, QStringLiteral("apple-search"),
                                             QStringLiteral("term"), workTitle);
    require(searchedAfterLazy == searchedDay1 + 1,
            "requestEnrichment issues exactly one new apple-search for the sole artwork-less work");

    const QByteArray hit = appleSearchHitBody(workTitle, workAuthor, 990042);
    require(completeAllPending(transport, hit) == 1,
            "exactly one lazy reply was in flight and completed");

    {
        BiblioCatalogStore peek;
        require(peek.open(dbPath), "peek store opens the same db file");
        const BiblioPendingEnrichmentRow row = peek.pendingEnrichmentMap().value(workKey);
        require(row.state == QStringLiteral("enriched"), "parked row state is 'enriched'");
        require(row.body == hit, "parked row stores the raw apple-search body byte-exact");
        require(row.lastAttempt == QDate::currentDate().toString(Qt::ISODate),
                "parked row stamps today's attempt date");
    }

    // The PUBLISHED snapshot is untouched by the lazy path.
    require(catalog.revision() == revisionDay1,
            "revision is stable across the lazy burst (pending never touches the snapshot)");
    require(catalog.ready(), "ready untouched by the lazy burst");
    const QVariantMap itemStill =
        pageItems(catalog, QStringLiteral("most-read")).first().toMap();
    require(itemStill.value(QStringLiteral("coverUrl")).toString().isEmpty(),
            "published artwork is STILL empty until the next daily publish folds it in");
    require(itemStill.value(QStringLiteral("rank")).toInt() == 1,
            "published rank untouched by lazy parking");

    // ── Day 2: forced refresh folds the parked row into the publish ──
    finished = false;
    catalog.refreshIfDue(/*force=*/true);
    runRefreshWithKindBodies(catalog, transport, bodies); // apple-search still answers empty
    require(finished && success, "day-2 forced refresh publishes");

    require(countCalls(transport, QStringLiteral("apple-search"),
                       QStringLiteral("term"), workTitle) == searchedAfterLazy,
            "the day-2 daily fan-out did NOT re-search the 'enriched' work (pending consultation)");

    require(catalog.revision() == revisionDay1 + 1, "day-2 publish bumps the revision once");
    const QVariantList mostRead2 = pageItems(catalog, QStringLiteral("most-read"));
    require(mostRead2.size() == 1, "most-read still carries exactly the one work");
    const QVariantMap item2 = mostRead2.first().toMap();
    require(item2.value(QStringLiteral("title")).toString() == workTitle,
            "the folded work keeps its identity");
    require(item2.value(QStringLiteral("coverUrl")).toString()
                == QStringLiteral("https://example.test/lazy-990042.jpg"),
            "the folded apple-search artwork is now published");
    const QVariantMap rating2 = item2.value(QStringLiteral("rating")).toMap();
    require(rating2.value(QStringLiteral("count")).toInt() == 321
                && rating2.value(QStringLiteral("average")).toDouble() == 4.75,
            "the folded apple-search rating is now published");
    require(item2.value(QStringLiteral("rank")).toInt() == 1,
            "fold-in never changes the work's most-read rank");

    {
        BiblioCatalogStore peek;
        require(peek.open(dbPath), "peek store reopens");
        require(!peek.pendingEnrichmentMap().contains(workKey),
                "the folded row is cleared after the successful publish");
        require(peek.pendingEnrichmentMap().isEmpty(),
                "no pending rows remain at all after the fold-in");
    }
}

// Unavailable negative control: an apple-search body with no usable record
// parks 'unavailable' — the work STILL pages with its fallback fields and its
// most-read rank is unchanged. The ≤1/day re-attempt gate is proven across
// three sessions: a fresh session on the SAME day issues nothing; after
// hand-setting last_attempt to yesterday (the harness cannot fake the clock —
// the store map + gate strings are the deterministic seam), a fresh session
// re-attempts exactly once.
void testLazyEnrichmentUnavailableGate()
{
    QTemporaryDir dir;
    const QString dbPath = dir.path() + QStringLiteral("/lazy-unavail.sqlite");
    FakeBiblioTransport transport;
    BiblioCatalog catalog(dbPath, &transport);

    const QString workTitle = QStringLiteral("Ghost Work");
    const QString workAuthor = QStringLiteral("Author Ghost Work");
    const QString workKey = workKeyFor(workTitle, workAuthor);
    const QString today = QDate::currentDate().toString(Qt::ISODate);

    const QHash<QString, QByteArray> bodies = {
        {QStringLiteral("apple-rss"), appleRssBody({QStringLiteral("Chart Book")})},
        {QStringLiteral("apple-search"), appleSearchBody()}, // empty results everywhere
        {QStringLiteral("openlibrary"), openLibraryDocsBody({})},
        {QStringLiteral("openlibrary-trending"),
         openLibraryTrendingBody({QStringLiteral("/works/OLGV")}, {workTitle},
                                 {QStringLiteral("1999")})},
        {QStringLiteral("openlibrary-classics"), openLibraryDocsBody({})},
        {QStringLiteral("openlibrary-subject"), openLibraryDocsBody({})},
    };

    catalog.refreshIfDue();
    runRefreshWithKindBodies(catalog, transport, bodies);
    require(catalog.ready(), "baseline refresh publishes");

    // The daily fan-out already searched the work once (empty result); the
    // lazy burst adds exactly one more.
    const int dailySearched = countCalls(transport, QStringLiteral("apple-search"),
                                         QStringLiteral("term"), workTitle);
    require(dailySearched == 1, "the daily fan-out searched the work exactly once");
    catalog.requestEnrichment(QStringLiteral("most-read"));
    const int issuedFirst = countCalls(transport, QStringLiteral("apple-search"),
                                       QStringLiteral("term"), workTitle);
    require(issuedFirst == dailySearched + 1,
            "the lazy burst issues exactly one new apple-search for the work");
    require(completeAllPending(transport, appleSearchBody()) == 1,
            "the lazy reply completes with an empty-results body");

    {
        BiblioCatalogStore peek;
        require(peek.open(dbPath), "peek store opens");
        const BiblioPendingEnrichmentRow row = peek.pendingEnrichmentMap().value(workKey);
        require(row.state == QStringLiteral("unavailable"),
                "an empty parse parks 'unavailable'");
        require(row.lastAttempt == today, "'unavailable' stamps today's attempt");
    }

    // Never-remove / never-demote: the work still pages with fallback fields.
    const QVariantList mostRead = pageItems(catalog, QStringLiteral("most-read"));
    require(mostRead.size() == 1, "the work still pages");
    const QVariantMap item = mostRead.first().toMap();
    require(item.value(QStringLiteral("title")).toString() == workTitle,
            "fallback identity unchanged");
    require(item.value(QStringLiteral("coverUrl")).toString().isEmpty(),
            "fallback artwork (empty) unchanged");
    require(item.value(QStringLiteral("rank")).toInt() == 1,
            "most-read rank unchanged by enrichment state");

    // Same session: the second burst is a session no-op.
    catalog.requestEnrichment(QStringLiteral("most-read"));
    require(countCalls(transport, QStringLiteral("apple-search"),
                       QStringLiteral("term"), workTitle) == issuedFirst,
            "a same-session second requestEnrichment issues no new apple-search");

    // Fresh session, SAME local day: the ≤1/day gate (not session
    // idempotence) is what blocks the re-attempt.
    {
        FakeBiblioTransport transport2;
        BiblioCatalog catalog2(dbPath, &transport2);
        catalog2.requestEnrichment(QStringLiteral("most-read"));
        require(transport2.calls.isEmpty(),
                "a fresh session on the same local day issues no re-attempt "
                "(gate: 'unavailable' with last_attempt == today)");
    }

    // Simulate the next local day: hand-set last_attempt to yesterday through
    // the store seam, then prove the gate opens for exactly one re-attempt.
    {
        BiblioCatalogStore peek;
        require(peek.open(dbPath), "peek store opens for the hand-set");
        require(peek.upsertPendingEnrichment(workKey, QByteArray(),
                                             QStringLiteral("unavailable"),
                                             QDate::currentDate().addDays(-1).toString(Qt::ISODate)),
                "hand-set last_attempt to yesterday (next-day simulation)");
    }
    {
        FakeBiblioTransport transport3;
        BiblioCatalog catalog3(dbPath, &transport3);
        catalog3.requestEnrichment(QStringLiteral("most-read"));
        require(countCalls(transport3, QStringLiteral("apple-search"),
                           QStringLiteral("term"), workTitle) == 1,
                "with last_attempt == yesterday the gate opens and exactly one re-attempt fires");
        // The re-attempt succeeds this time: the row flips to 'enriched'.
        const QByteArray hit = appleSearchHitBody(workTitle, workAuthor, 990123);
        require(completeAllPending(transport3, hit) == 1,
                "the single re-attempt reply completes");
        BiblioCatalogStore peek;
        require(peek.open(dbPath), "peek store reopens");
        const BiblioPendingEnrichmentRow row = peek.pendingEnrichmentMap().value(workKey);
        require(row.state == QStringLiteral("enriched"),
                "the successful re-attempt flips the row to 'enriched' for the next fold-in");
    }
}

// Crash residue (reviewer case 2026-08-15): a 'pending' marker whose issue date
// is NOT today models a session that died mid-burst — its reply will never
// arrive. The lazy gate must age it out and re-issue rather than block the
// work's enrichment forever; a same-day 'pending' marker (a possibly-live
// in-flight request) still blocks.
void testLazyEnrichmentPendingAging()
{
    QTemporaryDir dir;
    const QString dbPath = dir.path() + QStringLiteral("/lazy-pendage.sqlite");
    FakeBiblioTransport transport;
    BiblioCatalog catalog(dbPath, &transport);

    const QString workTitle = QStringLiteral("Ghost Pending");
    const QString workAuthor = QStringLiteral("Author Ghost Pending");
    const QString workKey = workKeyFor(workTitle, workAuthor);

    const QHash<QString, QByteArray> bodies = {
        {QStringLiteral("apple-rss"), appleRssBody({QStringLiteral("Chart Book")})},
        {QStringLiteral("apple-search"), appleSearchBody()},
        {QStringLiteral("openlibrary"), openLibraryDocsBody({})},
        {QStringLiteral("openlibrary-trending"),
         openLibraryTrendingBody({QStringLiteral("/works/OLPA")}, {workTitle},
                                 {QStringLiteral("1999")})},
        {QStringLiteral("openlibrary-classics"), openLibraryDocsBody({})},
        {QStringLiteral("openlibrary-subject"), openLibraryDocsBody({})},
    };
    catalog.refreshIfDue();
    runRefreshWithKindBodies(catalog, transport, bodies);
    require(catalog.ready(), "baseline refresh publishes");

    // Crash residue: hand-park 'pending' dated yesterday (the session that
    // issued the request died before any reply landed).
    {
        BiblioCatalogStore peek;
        require(peek.open(dbPath), "peek store opens for the residue hand-set");
        require(peek.upsertPendingEnrichment(workKey, QByteArray(),
                                             QStringLiteral("pending"),
                                             QDate::currentDate().addDays(-1).toString(Qt::ISODate)),
                "hand-park a yesterday-dated 'pending' marker");
    }

    // Fresh session: the aged marker opens the gate — exactly one re-issue,
    // and its successful reply overwrites the residue marker.
    {
        FakeBiblioTransport transport2;
        BiblioCatalog catalog2(dbPath, &transport2);
        catalog2.requestEnrichment(QStringLiteral("most-read"));
        require(countCalls(transport2, QStringLiteral("apple-search"),
                           QStringLiteral("term"), workTitle) == 1,
                "a yesterday-dated 'pending' marker is crash residue: re-issued exactly once");
        require(completeAllPending(transport2,
                                   appleSearchHitBody(workTitle, workAuthor, 990777)) == 1,
                "the re-issued reply completes");
    }
    {
        BiblioCatalogStore peek;
        require(peek.open(dbPath), "peek store reopens");
        const BiblioPendingEnrichmentRow row = peek.pendingEnrichmentMap().value(workKey);
        require(row.state == QStringLiteral("enriched"),
                "the re-issue's successful reply overwrites the residue marker");
    }

    // Same-day 'pending' still blocks: park one dated today, fresh session,
    // zero calls (the marker may belong to a live in-flight request).
    {
        BiblioCatalogStore peek;
        require(peek.open(dbPath), "peek store opens for the same-day hand-set");
        require(peek.upsertPendingEnrichment(workKey, QByteArray(),
                                             QStringLiteral("pending"),
                                             QDate::currentDate().toString(Qt::ISODate)),
                "hand-park a today-dated 'pending' marker");
    }
    {
        FakeBiblioTransport transport3;
        BiblioCatalog catalog3(dbPath, &transport3);
        catalog3.requestEnrichment(QStringLiteral("most-read"));
        require(transport3.calls.isEmpty(),
                "a today-dated 'pending' marker still blocks (live in-flight protection)");
    }
}

// Burst cap: a catalogue whose ranking carries 40 artwork-less works gets
// exactly 12 apple-search requests (kLazyEnrichmentBudget); the other 28 have
// NO pending rows written for them (a marker without an in-flight request
// would block the next day for no reason).
void testLazyEnrichmentBurstCap()
{
    QTemporaryDir dir;
    const QString dbPath = dir.path() + QStringLiteral("/lazy-cap.sqlite");
    FakeBiblioTransport transport;
    BiblioCatalog catalog(dbPath, &transport);

    QStringList keys, titles, years;
    for (int i = 0; i < 40; ++i) {
        keys << QStringLiteral("/works/OLCAP%1").arg(i, 2, 10, QLatin1Char('0'));
        titles << QStringLiteral("Cap Work %1").arg(i, 2, 10, QLatin1Char('0'));
        years << QStringLiteral("2001");
    }
    const QHash<QString, QByteArray> bodies = {
        {QStringLiteral("apple-rss"), appleRssBody({QStringLiteral("Chart Book")})},
        {QStringLiteral("apple-search"), appleSearchBody()},
        {QStringLiteral("openlibrary"), openLibraryDocsBody({})},
        {QStringLiteral("openlibrary-trending"), openLibraryTrendingBody(keys, titles, years)},
        {QStringLiteral("openlibrary-classics"), openLibraryDocsBody({})},
        {QStringLiteral("openlibrary-subject"), openLibraryDocsBody({})},
    };

    catalog.refreshIfDue();
    runRefreshWithKindBodies(catalog, transport, bodies);
    require(catalog.ready(), "baseline refresh publishes the 40-work ranking");
    require(pageItems(catalog, QStringLiteral("most-read")).size() == 40,
            "all 40 works are published rank-ordered");

    const int appleSearchBefore = countKind(transport, QStringLiteral("apple-search"));
    catalog.requestEnrichment(QStringLiteral("most-read"));
    const int issued = countKind(transport, QStringLiteral("apple-search")) - appleSearchBefore;
    require(issued == 12,
            "a 40-work artwork-less grid receives exactly 12 apple-search requests (burst cap)");

    {
        BiblioCatalogStore peek;
        require(peek.open(dbPath), "peek store opens");
        const QHash<QString, BiblioPendingEnrichmentRow> map = peek.pendingEnrichmentMap();
        require(map.size() == 12, "exactly 12 pending rows exist — the 28 capped-out works get none");
        int pendingStates = 0;
        for (auto it = map.constBegin(); it != map.constEnd(); ++it)
            if (it.value().state == QStringLiteral("pending")) ++pendingStates;
        require(pendingStates == 12, "every parked row is the crash-safe 'pending' marker");
        // The burst walks the ranking in rank order: works 0..11 are inside,
        // work 12+ is capped out with no marker.
        require(map.contains(workKeyFor(titles.at(0), QStringLiteral("Author ") + titles.at(0))),
                "the rank-1 work is inside the burst");
        require(map.contains(workKeyFor(titles.at(11), QStringLiteral("Author ") + titles.at(11))),
                "the rank-12 work is the last one inside the burst");
        require(!map.contains(workKeyFor(titles.at(12), QStringLiteral("Author ") + titles.at(12))),
                "the rank-13 work is capped out with no pending row");
        require(!map.contains(workKeyFor(titles.at(39), QStringLiteral("Author ") + titles.at(39))),
                "the rank-40 work is capped out with no pending row");
    }

    // Completing the burst resolves every marker; still exactly 12 rows.
    require(completeAllPending(transport, appleSearchBody()) == 12,
            "all 12 lazy replies complete");
    {
        BiblioCatalogStore peek;
        require(peek.open(dbPath), "peek store reopens");
        const QHash<QString, BiblioPendingEnrichmentRow> map = peek.pendingEnrichmentMap();
        require(map.size() == 12, "still exactly 12 rows after the burst resolves");
        int unavailable = 0;
        for (auto it = map.constBegin(); it != map.constEnd(); ++it)
            if (it.value().state == QStringLiteral("unavailable")) ++unavailable;
        require(unavailable == 12, "empty answers park 'unavailable' for exactly the 12 fetched");
    }
}

// Session idempotence: a second (and third) requestEnrichment for the same
// catalogue in the same session issues zero new requests of any kind.
void testLazyEnrichmentIdempotentPerSession()
{
    QTemporaryDir dir;
    const QString dbPath = dir.path() + QStringLiteral("/lazy-idem.sqlite");
    FakeBiblioTransport transport;
    BiblioCatalog catalog(dbPath, &transport);

    const QString workTitle = QStringLiteral("Once Work");
    const QHash<QString, QByteArray> bodies = {
        {QStringLiteral("apple-rss"), appleRssBody({QStringLiteral("Chart Book")})},
        {QStringLiteral("apple-search"), appleSearchBody()},
        {QStringLiteral("openlibrary"), openLibraryDocsBody({})},
        {QStringLiteral("openlibrary-trending"),
         openLibraryTrendingBody({QStringLiteral("/works/OLONCE")}, {workTitle},
                                 {QStringLiteral("2005")})},
        {QStringLiteral("openlibrary-classics"), openLibraryDocsBody({})},
        {QStringLiteral("openlibrary-subject"), openLibraryDocsBody({})},
    };

    catalog.refreshIfDue();
    runRefreshWithKindBodies(catalog, transport, bodies);
    require(catalog.ready(), "baseline refresh publishes");

    catalog.requestEnrichment(QStringLiteral("most-read"));
    const int callsAfterFirst = transport.calls.size();
    require(callsAfterFirst > 0, "the first burst issued its request");
    require(completeAllPending(transport, appleSearchBody()) == 1,
            "the lazy reply completes");

    catalog.requestEnrichment(QStringLiteral("most-read"));
    catalog.requestEnrichment(QStringLiteral("most-read"));
    require(transport.calls.size() == callsAfterFirst,
            "repeat requestEnrichment in the same session issues zero new requests");

    // A different catalogue in the same session is still its own burst: the
    // classics ranking here is empty, so it issues nothing.
    catalog.requestEnrichment(QStringLiteral("classics"));
    require(transport.calls.size() == callsAfterFirst,
            "an empty-ranking catalogue burst issues nothing");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testCachedFirstPaint();
    testFirstRunFailureLeavesNotReady();
    testRequestCoalescing();
    testOneRefreshPerLocalDayThenForced();
    testBoundedConcurrency();
    testPartialProviderFailureStillPublishes();
    testTotalFailurePreservesOldSnapshot();
    testForcedRefreshDuringPendingRetryDoesNotWedgeRefreshing();
    testFilterGroupsAndExploreRowsAndMosaicProxy();
    testSeedingFlipHappyPath();
    testMostReadUncontaminatedByChartPool();
    testOpenLibraryDeadStillPublishes();
    testAppleDeadPublishesOpenLibraryOnly();
    testEnrichmentOrderPreviewsBeforeDeepGrid();
    testSubjectSeedBudget();
    testLazyEnrichmentFoldIn();
    testLazyEnrichmentUnavailableGate();
    testLazyEnrichmentPendingAging();
    testLazyEnrichmentBurstCap();
    testLazyEnrichmentIdempotentPerSession();

    if (g_failures == 0) {
        std::cout << "BIBLIO_CATALOG_SERVICE_OK\n";
        return 0;
    }
    std::cerr << g_failures << " service harness assertion(s) failed\n";
    return 1;
}
