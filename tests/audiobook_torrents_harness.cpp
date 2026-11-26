#include "torrent/AudiobookTorrents.h"
#include "torrent/TankorentSearchService.h"
#include "torrent/TorrentIndexer.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <cstdlib>
#include <functional>
#include <iostream>

namespace {
void require(bool condition, const char* message)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

QString hash(QChar letter)
{
    return QString(40, letter);
}

TorrentResult row(const QString& title, QChar hashLetter, int seeders = 5,
                  const QString& category = QStringLiteral("102"))
{
    TorrentResult result;
    result.title = title;
    result.infoHash = hash(hashLetter);
    result.seeders = seeders;
    result.sizeBytes = 400LL * 1024 * 1024;
    result.sourceKey = QStringLiteral("piratebay");
    result.categoryId = category;
    return result;
}

QVariantMap recording(const QString& id, const QString& narrator)
{
    return {
        {"id", id}, {"workId", "the-time-machine"},
        {"narrators", QVariantList{QVariantMap{{"name", narrator}}}}
    };
}

// Projected from AudioSilo's /api/v1/works/the-time-machine, which returns an
// explicitly empty series array, H. G. Wells, and both of these recordings.
QVariantMap standalone()
{
    return {
        {"id", "the-time-machine"},
        {"title", "The Time Machine"},
        {"authors", QVariantList{QVariantMap{{"id", "h-g-wells"}, {"name", "H. G. Wells"}}}},
        {"series", QVariantList{}},
        {"recordings", QVariantList{
            recording("phil-chenevert", "Phil Chenevert"), recording("cliff-stone", "Cliff Stone")}}
    };
}

struct Response {
    QString token;
    QVariantList rows;
    QString error;
};

void observe(AudiobookTorrents& adapter, QList<Response>& responses)
{
    QObject::connect(&adapter, &AudiobookTorrents::searchFinished, &adapter,
        [&responses](const QString& token, const QVariantList& rows, const QString& error) {
            responses.append({token, rows, error});
        });
}

struct IndexerFixture {
    QList<TorrentResult> rows;
    bool fail = false;
    bool partial = false;
    QString mediaType;
    QString query;
    int calls = 0;
};

class MockIndexer : public TorrentIndexer {
public:
    explicit MockIndexer(IndexerFixture& fixture, QObject* parent, bool forceFailure = false)
        : TorrentIndexer(parent), m_fixture(fixture), m_forceFailure(forceFailure) {}

    QString id() const override { return QStringLiteral("piratebay"); }
    QString displayName() const override { return id(); }
    QString categoryFor(const QString& mediaType) const override
    {
        m_fixture.mediaType = mediaType;
        return QStringLiteral("102");
    }
    void search(const QString& query, int, const QString&) override
    {
        m_fixture.query = query;
        ++m_fixture.calls;
        const QList<TorrentResult> results = m_fixture.rows;
        const bool fail = m_fixture.fail || m_forceFailure;
        QTimer::singleShot(5, this, [this, results, fail] {
            if (fail)
                emit searchError(QStringLiteral("Network failure"));
            else
                emit searchFinished(results);
        });
    }
    IndexerHealth health() const override { return IndexerHealth::Ok; }
    QDateTime lastSuccess() const override { return {}; }
    QString lastError() const override { return {}; }
    qint64 lastResponseMs() const override { return 0; }

private:
    IndexerFixture& m_fixture;
    bool m_forceFailure;
};

class TestableSearchService : public TankorentSearchService {
public:
    explicit TestableSearchService(IndexerFixture& fixture)
        : TankorentSearchService(nullptr), m_fixture(fixture) {}

protected:
    QList<TorrentIndexer*> buildIndexersFor(const QString&, const QString&) override
    {
        QList<TorrentIndexer*> indexers{new MockIndexer(m_fixture, this)};
        if (m_fixture.partial)
            indexers.append(new MockIndexer(m_fixture, this, true));
        return indexers;
    }

private:
    IndexerFixture& m_fixture;
};

void pump(const std::function<bool()>& done, int timeoutMs = 5000)
{
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < timeoutMs) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    require(done(), "search completion timed out");
}

void search(AudiobookTorrents& adapter, const QString& token,
            const QVariantMap& work = standalone(), const QString& recordingId = "phil-chenevert")
{
    adapter.search(token, work, recordingId, QStringLiteral("the time machine|h g wells"));
}

void testEligibility()
{
    IndexerFixture fixture;
    AudiobookTorrents adapter(new TestableSearchService(fixture));
    QList<Response> responses;
    observe(adapter, responses);
    require(adapter.isEligible(standalone()), "complete standalone work must be eligible");

    auto missingSeries = standalone();
    missingSeries.remove("series");
    auto malformedSeries = standalone();
    malformedSeries.insert("series", QVariantMap{});
    auto seriesWork = standalone();
    seriesWork.insert("series", QVariantList{QVariantMap{{"name", "Series"}}});
    auto franchiseWork = standalone();
    franchiseWork.insert("franchiseId", "franchise");
    auto incomplete = standalone();
    incomplete.remove("recordings");
    auto ambiguous = standalone();
    ambiguous.insert("recordings", QVariantList{
        recording("phil-chenevert", "Phil Chenevert"), recording("phil-chenevert", "Cliff Stone")});

    for (const auto& rejected : {missingSeries, malformedSeries, seriesWork, franchiseWork, incomplete}) {
        require(!adapter.isEligible(rejected), "unknown, series or franchise work must be unavailable");
        search(adapter, "ineligible", rejected);
    }
    search(adapter, "unknown-edition", standalone(), "unrelated-recording");
    search(adapter, "ambiguous-edition", ambiguous);
    require(fixture.calls == 0, "ineligible or ambiguous selection must not contact Tankorent");
    require(responses.size() == 7, "every ineligible selection must settle the picker");
    for (const auto& response : responses)
        require(response.rows.isEmpty() && !response.error.isEmpty(), "ineligible selection must explain no rows");
}

void testManualRows()
{
    IndexerFixture fixture;
    fixture.rows = {
        row("The Time Machine by H G Wells [Audiobook M4B]", 'a', 3), // edition unknown
        row("H. G. Wells - The Time Machine (Cliff Stone) MP3", 'b', 8), // alternate
        row("The Time Machine H.G. Wells", 'c', 2), // explicit audiobook category
        row("The Time Machine H G Wells read by Avery Reader M4B", 'd', 2),
        row("The Time Machine H G Wells [Audiobook M4B]", 'a', 6), // same hash
        row("The Time Machine 2 H G Wells mp3", 'e', 999),
        row("The Time Machine H G Wells Books 1-4 audiobook m4b", 'e', 999),
        row("The Time Machine H G Wells Foundation and Dune m4b", 'e', 999),
        row("The Time Machine H G Wells Part 1 of 2 audiobook m4b", 'e', 999),
        row("The Time Machine H G Wells BluRay x265", 'e', 999, "201"),
        row("The Time Machine H G Wells MP3", 'e', 999, "201"),
        row("The Time Machine H G Wells", 'e', 999, "101"),
        row("The Time Machine H G Wells.epub", 'e', 999, "601"),
        row("The Time Machine H G Wells.aax", 'e', 999),
        row("The Time Machine H G Wells sample mp3", 'e', 999),
        row("The Time Machine Unknown Author m4b", 'e', 999),
        row("The Time Machine audiobook mp3", 'e', 999),
        row("The Time Machine H G Wells [Audiobook M4B]", 'x', 999)
    };
    AudiobookTorrents adapter(new TestableSearchService(fixture));
    QList<Response> responses;
    observe(adapter, responses);
    search(adapter, "standalone");
    pump([&] { return !responses.isEmpty(); });
    require(fixture.mediaType == "audiobooks" && fixture.query == "The Time Machine H. G. Wells",
            "adapter must use Tankorent's existing audiobook search");
    const auto response = responses.first();
    require(response.token == "standalone" && response.error.isEmpty() && response.rows.size() == 4,
            "only four proved standalone audio listings should reach the picker");

    QStringList hashes;
    for (const auto& item : response.rows) {
        const auto offer = item.toMap();
        const auto request = offer.value("request").toMap();
        const auto infoHash = request.value("infoHash").toString();
        hashes.append(infoHash);
        require(offer.value("sourceLabel") == "Tankorent" && offer.value("unresolved").toBool()
                && !offer.value("recommended").toBool(),
                "Tankorent listing facts are for manual selection");
        require(request.value("sourceKind") == "tankorent" && request.value("workId") == "the-time-machine"
                && request.value("title") == "The Time Machine" && request.value("author") == "H. G. Wells",
                "picker request must preserve work identity and Tankorent provenance");
        require(request.value("recordingKey") == "the time machine|h g wells#src:" + infoHash
                && !request.contains("audiosiloRecordingId") && !request.value("bundle").toBool(),
                "unknown or alternate editions must use the selected listing's hash identity");
    }
    require(hashes.first() == hash('b') && hashes.at(1) == hash('a') && hashes.contains(hash('c'))
            && hashes.contains(hash('d')), "deduplicate proved hashes and sort by seeders");
    const auto alternate = response.rows.at(0).toMap();
    require(alternate.value("title") == "H. G. Wells - The Time Machine (Cliff Stone) MP3"
            && alternate.value("narrators").toStringList() == QStringList{"Cliff Stone"},
            "alternate narration must preserve the actual listing and its printed credit");
    const auto sparse = response.rows.at(1).toMap();
    require(sparse.value("narrators").toStringList().isEmpty() && sparse.value("format") == "M4B"
            && sparse.value("seeders").toInt() == 6,
            "sparse listing must use available facts without borrowing catalogue narration");
}

void testCancellation()
{
    IndexerFixture fixture;
    fixture.rows = {row("The Time Machine H G Wells audiobook", 'a')};
    AudiobookTorrents adapter(new TestableSearchService(fixture));
    QList<Response> responses;
    observe(adapter, responses);
    search(adapter, "superseded");
    fixture.rows = {row("The Time Machine H G Wells Audiobook MP3", 'b')};
    search(adapter, "current");
    adapter.cancel("superseded");
    pump([&] { return !responses.isEmpty(); });
    require(responses.size() == 1 && responses.first().token == "current",
            "superseded token must not deliver or cancel the active search");

    search(adapter, "closed");
    adapter.cancel("closed");
    bool settled = false;
    QTimer::singleShot(25, &adapter, [&] { settled = true; });
    pump([&] { return settled; });
    require(responses.size() == 1, "cancelled picker must receive no rows");
}

void testIndexerStates()
{
    IndexerFixture fixture;
    AudiobookTorrents adapter(new TestableSearchService(fixture));
    QList<Response> responses;
    observe(adapter, responses);

    search(adapter, "empty");
    pump([&] { return responses.size() == 1; });
    require(responses.first().rows.isEmpty() && responses.first().error.isEmpty(),
            "successful empty response must remain a no-match");

    fixture.fail = true;
    search(adapter, "network");
    pump([&] { return responses.size() == 2; });
    require(responses.last().rows.isEmpty() && !responses.last().error.isEmpty(),
            "all indexers failing must surface an error");

    fixture.fail = false;
    fixture.partial = true;
    search(adapter, "partial");
    pump([&] { return responses.size() == 3; });
    require(responses.last().rows.isEmpty() && !responses.last().error.isEmpty(),
            "one failure alongside no matches must remain an incomplete search");

    QTemporaryDir config;
    require(config.isValid(), "create isolated indexer configuration");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
    QCoreApplication::setOrganizationName("ColosseumTankorentHarness");
    QCoreApplication::setApplicationName("DisabledIndexers");
    QSettings settings;
    for (const auto* id : {"piratebay", "exttorrents", "torrentscsv"})
        settings.setValue(QStringLiteral("tankorent/indexers/%1/enabled").arg(QLatin1String(id)), false);
    settings.sync();
    QNetworkAccessManager network;
    AudiobookTorrents disabled(&network);
    QList<Response> disabledResponses;
    observe(disabled, disabledResponses);
    search(disabled, "disabled");
    require(disabledResponses.size() == 1 && disabledResponses.first().rows.isEmpty()
            && !disabledResponses.first().error.isEmpty(),
            "real search service must settle immediately when its audio indexers are disabled");
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    testEligibility();
    testManualRows();
    testCancellation();
    testIndexerStates();
    std::cout << "audiobook_torrents_harness PASS\n";
}
