// Async contract for the manual comic source search: ComicTorrents fans an
// edition out into three comics/all queries, merges + dedups canonical results,
// emits cumulative partial updates then exactly one final update, keeps mere
// browsing off the acquisition-failure path, and ignores the handles of a
// search that was replaced by a later manual query.
#include "torrent/ComicTorrents.h"
#include "torrent/ComicTorrentDownloader.h"
#include "torrent/TankorentSearchService.h"
#include "torrent/TorrentIndexer.h"
#include "torrent/TorrentResult.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QList>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <cstdlib>
#include <functional>
#include <iostream>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

TorrentResult row(const QString& title, int seeders, const QString& hash)
{
    TorrentResult r;
    r.title = title;
    r.seeders = seeders;
    r.infoHash = hash;
    r.magnetUri = buildMagnet(hash, title);
    return r;
}

// Per-query canned results for the single "good" mock indexer. The erroring
// mock ignores this and always emits searchError (partial indexer failure).
QHash<QString, QList<TorrentResult>> g_fixture;

// A TorrentIndexer that answers ASYNCHRONOUSLY (like a real network indexer),
// so ComicTorrents has registered the handle before any signal fires.
class MockTorrentIndexer : public TorrentIndexer {
public:
    MockTorrentIndexer(const QString& id, bool errorOut, QObject* parent = nullptr)
        : TorrentIndexer(parent), m_id(id), m_error(errorOut) {}

    QString id() const override { return m_id; }
    QString displayName() const override { return m_id; }
    void search(const QString& query, int, const QString&) override {
        const bool err = m_error;
        const QList<TorrentResult> results = err ? QList<TorrentResult>() : g_fixture.value(query);
        QTimer::singleShot(0, this, [this, err, results] {
            if (err) emit searchError(QStringLiteral("mock indexer offline"));
            else emit searchFinished(results);
        });
    }
    IndexerHealth health() const override { return IndexerHealth::Ok; }
    QDateTime lastSuccess() const override { return {}; }
    QString lastError() const override { return {}; }
    qint64 lastResponseMs() const override { return 0; }

private:
    QString m_id;
    bool m_error;
};

// Search service whose indexer set is two mocks: one answers from the fixture,
// one always errors — proving partial-failure survival without touching the net.
class TestableSearchService : public TankorentSearchService {
public:
    explicit TestableSearchService(QObject* parent = nullptr)
        : TankorentSearchService(nullptr, parent) {}
protected:
    QList<TorrentIndexer*> buildIndexersFor(const QString&, const QString&) override {
        return {
            new MockTorrentIndexer(QStringLiteral("piratebay"), false, this),
            new MockTorrentIndexer(QStringLiteral("torrentscsv"), true, this)
        };
    }
};

// Pump the event loop until `done` or a hard timeout (monotonic clock only).
void pump(const std::function<bool()>& done, int timeoutMs = 5000)
{
    QElapsedTimer clock;
    clock.start();
    while (!done()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        if (clock.elapsed() > timeoutMs) {
            std::cerr << "FAIL: search harness timed out waiting for completion\n";
            std::exit(1);
        }
    }
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const QString hashA(40, QChar('a'));
    const QString hashB(40, QChar('b'));
    const QString hashC(40, QChar('c'));
    const QString hashD(40, QChar('d'));

    // Fixture keyed by the EXACT queries ComicTorrentQueryPlanner produces for
    // "Saga: Book One" (canonical title, ISBN, then series #range).
    g_fixture[QStringLiteral("Saga: Book One")] = { row("Saga Book One 1-18 CBZ", 8, hashB) };
    g_fixture[QStringLiteral("9781632150783")]  = { row("Saga 9781632150783 Digital", 2, hashC) };
    g_fixture[QStringLiteral("Saga #1-18")]      = {
        row("Saga Book One 1-18 CBZ", 5, hashB),           // duplicate canonical hash, lower seed
        row("Annihilation Saga Issue 1 CBR", 900, hashA)   // unrelated high-seed pack
    };

    auto* search = new TestableSearchService();
    auto* downloader = new ComicTorrentDownloader(nullptr);   // search-only; engine never touched
    ComicTorrents torrents(search, downloader);               // DI seam takes ownership

    QVariantList lastRows;
    int finalCount = 0;
    int partialCount = 0;
    bool acquisitionFailed = false;
    bool sourceSearchFailed = false;

    QObject::connect(&torrents, &ComicTorrents::sourcesUpdated,
                     [&](const QString&, const QVariantList& rows, bool complete) {
        lastRows = rows;
        if (complete) ++finalCount; else ++partialCount;
    });
    QObject::connect(&torrents, &ComicTorrents::failed,
                     [&](const QString&, const QString&) { acquisitionFailed = true; });
    QObject::connect(&torrents, &ComicTorrents::sourceSearchFailed,
                     [&](const QString&, const QString&) { sourceSearchFailed = true; });

    // ── Scenario 1: automatic three-query merge ──
    torrents.searchSources(QStringLiteral("gc:saga:book-one"), QStringLiteral("Saga"),
                           QStringLiteral("Saga: Book One"), QStringLiteral("9781632150783"),
                           QStringLiteral("Saga #1-18"));
    pump([&] { return finalCount >= 1; });

    require(lastRows.size() == 3, "merged search exposes all canonical results");
    require(finalCount == 1, "all query handles settle into one final update");
    require(partialCount >= 1, "partial updates arrive before completion");
    require(!acquisitionFailed, "partial indexer failure is not an acquisition failure");
    require(!sourceSearchFailed, "results present means no source-search failure");
    require(lastRows.first().toMap().value("infoHash").toString() == hashC,
            "ISBN evidence ranks first in the merged picker");

    // ── Scenario 2: a replacement/manual search ignores the cancelled handles ──
    g_fixture[QStringLiteral("Saga Compendium")] = { row("Saga Compendium One CBZ", 10, hashD) };
    lastRows.clear();
    finalCount = 0;
    acquisitionFailed = false;

    torrents.searchSources(QStringLiteral("gc:saga:comp"), QStringLiteral("Saga"),
                           QStringLiteral("Saga: Book One"), QStringLiteral("9781632150783"),
                           QStringLiteral("Saga #1-18"));
    // Replace before the loop turns: the three automatic handles are now stale.
    torrents.searchSourcesQuery(QStringLiteral("gc:saga:comp"), QStringLiteral("Saga Compendium"));
    pump([&] { return finalCount >= 1; });

    require(finalCount == 1, "replacement search settles exactly once");
    require(lastRows.size() == 1
                && lastRows.first().toMap().value("infoHash").toString() == hashD,
            "manual query result replaces the cancelled automatic set");
    require(!acquisitionFailed, "replacing a search is not an acquisition failure");

    std::cout << "comic_torrents_search_harness PASS\n";
    return 0;
}
