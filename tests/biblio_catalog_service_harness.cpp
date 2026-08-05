// tests/biblio_catalog_service_harness.cpp
//
// Fake-transport oracle for BiblioCatalog (Discover/Explore Task 4, plan
// 2026-08-03): the daily keyless refresh coordinator. Proves request
// coalescing, one refresh per local day, forced refresh, bounded (<=4)
// concurrency, cached first paint, partial-provider failure, total failure
// preserving the prior snapshot, and first-run failure leaving ready==false.
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
#include <QEventLoop>
#include <QList>
#include <QSet>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
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
    if (s.contains(QStringLiteral("openlibrary.org")))
        return QStringLiteral("openlibrary");
    return QStringLiteral("unknown");
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
                completeAt(idx, 200, QByteArray("{}"), QString());
        }
    }

    // Fails every currently-pending call whose kind == `failingKind` with a
    // transport-level error; succeeds everything else via drainAll's normal
    // per-kind fixtures. Used for the partial-provider-failure case.
    void drainWithKindFailing(const QString &failingKind)
    {
        for (int guard = 0; guard < 500; ++guard) {
            const int idx = nextPendingIndex();
            if (idx < 0) return;
            const QString kind = calls.at(idx).kind;
            if (kind == failingKind) {
                completeAt(idx, 0, QByteArray(), QStringLiteral("simulated provider outage"));
                continue;
            }
            if (kind == QStringLiteral("apple-rss"))
                completeAt(idx, 200, appleRssBody({QStringLiteral("Book A"), QStringLiteral("Book B")}), QString());
            else if (kind == QStringLiteral("apple-search"))
                completeAt(idx, 200, appleSearchBody(), QString());
            else
                completeAt(idx, 200, QByteArray("{}"), QString());
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

void runRefreshToCompletionKindFailing(BiblioCatalog &catalog, FakeBiblioTransport &transport,
                                       const QString &failingKind)
{
    for (int round = 0; round < 50 && catalog.refreshing(); ++round) {
        transport.drainWithKindFailing(failingKind);
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
    runRefreshToCompletionKindFailing(catalog, transport, QStringLiteral("openlibrary"));

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

    catalog.refreshIfDue();
    runRefreshToCompletion(catalog, transport, true);
    require(catalog.ready(), "initial refresh publishes a good snapshot");
    const QVariantMap before = catalog.discoverPage(QStringLiteral("popular"), QString(), QString(), true, 0, 50);
    const int beforeCount = before.value(QStringLiteral("items")).toList().size();
    require(beforeCount > 0, "initial snapshot has items to compare against");
    const QDateTime successBefore = catalog.lastSuccessfulRefresh();

    bool finished = false, success = true;
    QObject::connect(&catalog, &BiblioCatalog::refreshFinished, [&](bool ok) {
        finished = true; success = ok;
    });

    // Forced refresh where EVERY request fails outright.
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
    require(rows.size() == 4, "exploreRows returns exactly the four house catalogues");
    QSet<QString> seenCatalogIds;
    for (const QVariant &row : rows)
        seenCatalogIds.insert(row.toMap().value(QStringLiteral("catalogId")).toString());
    require(seenCatalogIds.contains(QStringLiteral("popular")), "exploreRows includes popular");
    require(seenCatalogIds.contains(QStringLiteral("trending")), "exploreRows includes trending");

    // mosaic() with an unknown key returns empty rather than crashing.
    require(catalog.mosaic(QStringLiteral("not-a-real-facet-key"), 10, true).isEmpty(),
            "mosaic with an unknown facet key returns an empty list");
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
    testFilterGroupsAndExploreRowsAndMosaicProxy();

    if (g_failures == 0) {
        std::cout << "BIBLIO_CATALOG_SERVICE_OK\n";
        return 0;
    }
    std::cerr << g_failures << " service harness assertion(s) failed\n";
    return 1;
}
