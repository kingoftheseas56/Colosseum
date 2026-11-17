// audiobook_torrents_harness.cpp -- AudiobookTorrents contract: audio-only rows,
// ranked by title+author then seeders, bundle/format flags, hash-less rows dropped,
// a superseded search never leaks results, and "every indexer off" is an immediate
// empty result + searchFinished. No network: the search service is injected with mocks.
#include "torrent/AudiobookTorrents.h"
#include "torrent/TankorentSearchService.h"
#include "torrent/TorrentIndexer.h"
#include "torrent/TorrentResult.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QList>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <cstdlib>
#include <functional>
#include <iostream>

namespace {
void require(bool c, const char* m){ if(!c){ std::cerr<<"FAIL: "<<m<<'\n'; std::exit(1);} }

TorrentResult row(const QString& title, int seeders, const QString& hash,
                  const QString& sourceKey = {}, const QString& catId = {})
{
    TorrentResult r; r.title = title; r.seeders = seeders; r.infoHash = hash;
    r.sizeBytes = 400LL*1024*1024; r.sourceKey = sourceKey; r.sourceName = sourceKey;
    r.categoryId = catId; return r;
}

QList<TorrentResult> g_fixture;      // what the mock indexer returns
QStringList g_seenCategories;        // category ids the service asked the indexer for
int g_buildCalls = 0;
bool g_allOff = false;

class MockIndexer : public TorrentIndexer {
public:
    MockIndexer(const QString& id, int delayMs, QObject* parent)
        : TorrentIndexer(parent), m_id(id), m_delay(delayMs) {}
    QString id() const override { return m_id; }
    QString displayName() const override { return m_id; }
    QString categoryFor(const QString& mediaType) const override {
        g_seenCategories << mediaType; return {};
    }
    void search(const QString&, int, const QString&) override {
        const QList<TorrentResult> results = g_fixture;
        QTimer::singleShot(m_delay, this, [this, results]{ emit searchFinished(results); });
    }
    IndexerHealth health() const override { return IndexerHealth::Ok; }
    QDateTime lastSuccess() const override { return {}; }
    QString lastError() const override { return {}; }
    qint64 lastResponseMs() const override { return 0; }
private:
    QString m_id; int m_delay;
};

class TestableSearchService : public TankorentSearchService {
public:
    explicit TestableSearchService(QObject* parent = nullptr)
        : TankorentSearchService(nullptr, parent) {}
    QString lastMediaType;
protected:
    QList<TorrentIndexer*> buildIndexersFor(const QString& mediaType, const QString&) override {
        ++g_buildCalls; lastMediaType = mediaType;
        if (g_allOff) return {};
        return { new MockIndexer(QStringLiteral("piratebay"), 5, this) };
    }
};

void pump(const std::function<bool()>& done, int timeoutMs = 5000)
{
    QElapsedTimer clock; clock.start();
    while (!done()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (clock.elapsed() > timeoutMs) { std::cerr<<"FAIL: timed out\n"; std::exit(1); }
    }
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // ---- static row building: filter, rank, flags ----
    {
        QList<TorrentResult> raw{
            row("Dune Frank Herbert Unabridged m4b", 5, "h-m4b"),
            row("Dune.epub", 900, "h-epub"),                                  // readable book -> dropped
            row("Dune Messiah Audiobook mp3", 800, "h-messiah"),
            row("Dune Audiobook mp3 NO HASH", 50, ""),                        // no hash -> dropped
            row("Dune Full Cast", 20, "h-cat", "piratebay", "102"),           // category 102 alone
            row("Dune Frank Herbert Audiobook", 3, "h-exact-author"),
            row("Dune Saga Books 1-4 Audiobook MP3", 40, "h-bundle"),
        };
        const QVariantList rows = AudiobookTorrents::buildRows(
            QStringLiteral("Dune"), QStringLiteral("Frank Herbert"), raw);

        QStringList hashes;
        for (const auto& v : rows) hashes << v.toMap().value("infoHash").toString();
        require(!hashes.contains("h-epub"), "epub row is not an audiobook");
        require(!hashes.contains(QString()), "hashless row dropped");
        require(hashes.contains("h-cat"), "category 102 row kept");
        require(!rows.isEmpty(), "rows not empty");

        // ranking: title+author agreement first; "Dune Messiah" (800 seeders) must not lead
        require(hashes.first() == "h-exact-author" || hashes.first() == "h-m4b",
                "title+author match ranks first");
        require(hashes.indexOf("h-messiah") > hashes.indexOf("h-m4b"),
                "high-seed sequel does not outrank exact title+author");
        require(hashes.indexOf("h-m4b") < hashes.indexOf("h-exact-author")
                || hashes.indexOf("h-m4b") > hashes.indexOf("h-exact-author"),
                "both author rows present");

        QVariantMap m4b;
        for (const auto& v : rows) if (v.toMap().value("infoHash") == "h-m4b") m4b = v.toMap();
        require(m4b.value("format").toString() == "m4b", "format m4b read from title");
        require(!m4b.value("bundle").toBool(), "single book not a bundle");
        require(m4b.contains("title") && m4b.contains("seeders") && m4b.contains("sizeBytes")
                && m4b.contains("size") && m4b.contains("source"), "row carries the contract fields");

        QVariantMap bundle;
        for (const auto& v : rows) if (v.toMap().value("infoHash") == "h-bundle") bundle = v.toMap();
        require(bundle.value("bundle").toBool(), "Books 1-4 flagged bundle");
        require(bundle.value("format").toString() == "mp3", "format mp3 read from title");

        QVariantMap cat;
        for (const auto& v : rows) if (v.toMap().value("infoHash") == "h-cat") cat = v.toMap();
        require(cat.value("format").toString().isEmpty(), "format empty when title does not say");
    }

    // ---- async: search asks for "audiobooks", filters, finishes ----
    {
        g_fixture = { row("Dune Unabridged m4b", 5, "a1"), row("Dune.epub", 99, "b1") };
        auto* svc = new TestableSearchService();
        AudiobookTorrents at(svc);
        QVariantList got; int finished = 0; int resultsSignals = 0;
        QObject::connect(&at, &AudiobookTorrents::resultsReady, [&](const QVariantList& r){ got = r; ++resultsSignals; });
        QObject::connect(&at, &AudiobookTorrents::searchFinished, [&]{ ++finished; });
        at.search("Dune", "Frank Herbert");
        pump([&]{ return finished == 1; });
        require(svc->lastMediaType == "audiobooks", "searches the audiobooks media type");
        require(resultsSignals == 1 && got.size() == 1, "one results emission with only the audiobook row");
        require(got.first().toMap().value("infoHash").toString() == "a1", "audiobook row returned");
    }

    // ---- stale handle: a second search supersedes the first, no leak ----
    {
        g_fixture = { row("First Book Audiobook mp3", 5, "first") };
        auto* svc = new TestableSearchService();
        AudiobookTorrents at(svc);
        QList<QVariantList> emissions; int finished = 0;
        QObject::connect(&at, &AudiobookTorrents::resultsReady, [&](const QVariantList& r){ emissions << r; });
        QObject::connect(&at, &AudiobookTorrents::searchFinished, [&]{ ++finished; });
        at.search("First Book", "");
        g_fixture = { row("Second Book Audiobook mp3", 7, "second") };
        at.search("Second Book", "");
        pump([&]{ return finished >= 1; });
        // let any stray first-search timers fire
        QElapsedTimer t; t.start(); while (t.elapsed() < 100) QCoreApplication::processEvents();
        require(finished == 1, "exactly one searchFinished (superseded search stays silent)");
        require(emissions.size() == 1, "exactly one resultsReady");
        require(emissions.first().size() == 1
                && emissions.first().first().toMap().value("infoHash").toString() == "second",
                "only the latest search's rows are emitted");
    }

    // ---- every indexer off: empty result + searchFinished immediately ----
    {
        g_allOff = true;
        auto* svc = new TestableSearchService();
        AudiobookTorrents at(svc);
        int finished = 0, results = 0; QVariantList got{ QVariant(1) };
        QObject::connect(&at, &AudiobookTorrents::resultsReady, [&](const QVariantList& r){ got = r; ++results; });
        QObject::connect(&at, &AudiobookTorrents::searchFinished, [&]{ ++finished; });
        at.search("Dune", "Frank Herbert");
        require(finished == 1 && results == 1 && got.isEmpty(),
                "all indexers off: empty results and searchFinished emitted synchronously");
        g_allOff = false;
    }

    std::cout << "audiobook_torrents_harness PASS\n";
    return 0;
}
