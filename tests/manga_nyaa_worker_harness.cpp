// Focused fake-network coverage for native Nyaa RSS discovery and metainfo
// retrieval. Query planning, trust/ranking and torrent ownership stay native.
#include "torrent/MangaNyaaSource.h"
#include "engine/MangaTankobanService.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <QUrlQuery>
#include <QXmlStreamWriter>

#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>

using namespace MangaTankoban;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct ReplyPlan {
    int status = 200;
    QByteArray body;
    int delayMs = 0;
    bool neverFinish = false;
    bool duplicateFinished = false;
    bool destroyWithoutFinish = false;
    bool advertiseLength = true;
};

class FakeReply final : public QNetworkReply
{
public:
    FakeReply(const QNetworkRequest& request, QNetworkAccessManager::Operation operation,
              ReplyPlan plan, QObject* parent)
        : QNetworkReply(parent), m_plan(std::move(plan))
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(operation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, m_plan.status);
        if (m_plan.advertiseLength)
            setHeader(QNetworkRequest::ContentLengthHeader, m_plan.body.size());
        open(QIODevice::ReadOnly);
        if (m_plan.destroyWithoutFinish) {
            QTimer::singleShot(m_plan.delayMs, this, [this] { delete this; });
        } else if (!m_plan.neverFinish) {
            QTimer::singleShot(m_plan.delayMs, this, [this] { finish(); });
        }
    }

    void abort() override
    {
        ++abortCount;
        if (isFinished())
            return;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("fake request aborted"));
        setFinished(true);
        emit finished();
    }

    qint64 bytesAvailable() const override
    {
        return qint64(m_plan.body.size()) - m_offset + QNetworkReply::bytesAvailable();
    }

    void finish()
    {
        if (isFinished())
            return;
        setFinished(true);
        emit readyRead();
        emit finished();
        if (m_plan.duplicateFinished)
            emit finished();
    }

    void emitLateFinished() { emit finished(); }

    int abortCount = 0;

protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        if (m_offset >= m_plan.body.size())
            return -1;
        const qint64 count = qMin<qint64>(maxSize, m_plan.body.size() - m_offset);
        std::memcpy(data, m_plan.body.constData() + m_offset, size_t(count));
        m_offset += count;
        return count;
    }

private:
    ReplyPlan m_plan;
    qint64 m_offset = 0;
};

class FakeNetworkAccessManager final : public QNetworkAccessManager
{
public:
    using Planner = std::function<ReplyPlan(const QNetworkRequest&, const QByteArray&, int)>;

    explicit FakeNetworkAccessManager(Planner planner, QObject* parent = nullptr)
        : QNetworkAccessManager(parent), m_planner(std::move(planner))
    {
    }

    struct Request {
        QNetworkAccessManager::Operation operation;
        QNetworkRequest request;
        QByteArray body;
        QPointer<FakeReply> reply;
    };

    QList<Request> requests;

protected:
    QNetworkReply* createRequest(QNetworkAccessManager::Operation operation,
                                 const QNetworkRequest& request,
                                 QIODevice* outgoingData = nullptr) override
    {
        const QByteArray body = outgoingData ? outgoingData->readAll() : QByteArray{};
        const int index = requests.size();
        ReplyPlan plan = m_planner(request, body, index);
        auto* reply = new FakeReply(request, operation, std::move(plan), this);
        requests.append({operation, request, body, reply});
        return reply;
    }

private:
    Planner m_planner;
};

QJsonObject validRow(const QString& title = QStringLiteral("Example Manga Vol 02 Digital"),
                     const QString& hash = QStringLiteral("0123456789abcdef0123456789abcdef01234567"),
                     const QString& torrentUrl = QStringLiteral("https://nyaa.si/download/123456.torrent"))
{
    QJsonObject row;
    row.insert(QStringLiteral("title"), title);
    row.insert(QStringLiteral("infoHash"), hash);
    row.insert(QStringLiteral("uploader"), QStringLiteral("TrustedUploader"));
    row.insert(QStringLiteral("seeders"), 17);
    row.insert(QStringLiteral("leechers"), 3);
    row.insert(QStringLiteral("sizeBytes"), 1234567);
    row.insert(QStringLiteral("torrentUrl"), torrentUrl);
    return row;
}

QByteArray response(const QJsonArray& rows)
{
    static const QString nyaaNamespace = QStringLiteral("https://nyaa.si/xmlns/nyaa");
    QByteArray xml;
    QXmlStreamWriter writer(&xml);
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("rss"));
    writer.writeAttribute(QStringLiteral("version"), QStringLiteral("2.0"));
    writer.writeNamespace(nyaaNamespace, QStringLiteral("nyaa"));
    writer.writeStartElement(QStringLiteral("channel"));
    const auto nyaaElement = [&writer](const QString& name, const QString& value) {
        writer.writeStartElement(nyaaNamespace, name);
        writer.writeCharacters(value);
        writer.writeEndElement();
    };
    for (const QJsonValue& value : rows) {
        const QJsonObject row = value.toObject();
        writer.writeStartElement(QStringLiteral("item"));
        writer.writeTextElement(QStringLiteral("title"), row.value(QStringLiteral("title")).toString());
        writer.writeTextElement(QStringLiteral("link"), row.value(QStringLiteral("torrentUrl")).toString());
        nyaaElement(QStringLiteral("infoHash"), row.value(QStringLiteral("infoHash")).toString());
        nyaaElement(QStringLiteral("uploader"), row.value(QStringLiteral("uploader")).toString());
        nyaaElement(QStringLiteral("seeders"), row.value(QStringLiteral("seeders")).toVariant().toString());
        nyaaElement(QStringLiteral("leechers"), row.value(QStringLiteral("leechers")).toVariant().toString());
        nyaaElement(QStringLiteral("size"), row.value(QStringLiteral("sizeBytes")).toVariant().toString()
                                                + QStringLiteral(" B"));
        writer.writeEndElement();
    }
    writer.writeEndElement();
    writer.writeEndElement();
    writer.writeEndDocument();
    return xml;
}

QByteArray emptyResponse()
{
    return response(QJsonArray{});
}

SeriesSnapshot testSeries()
{
    SeriesSnapshot series;
    series.seriesId = QStringLiteral("native-rss-test-series");
    series.title = QStringLiteral("Example Manga");
    return series;
}

bool waitForOutcome(MangaNyaaSource& source, int timeoutMs = 2500)
{
    QEventLoop loop;
    const auto success = QObject::connect(&source, &MangaNyaaSource::searchSucceeded,
                                          &loop, &QEventLoop::quit);
    const auto failure = QObject::connect(&source, &MangaNyaaSource::searchFailed,
                                          &loop, &QEventLoop::quit);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    QObject::disconnect(success);
    QObject::disconnect(failure);
    return true;
}

void testStrictNyaaUrl()
{
    require(MangaNyaaSource::isValidTorrentUrl(QStringLiteral("https://nyaa.si/download/123.torrent")),
            "exact Nyaa download URL is accepted");
    const QStringList rejected = {
        QStringLiteral("http://nyaa.si/download/123.torrent"),
        QStringLiteral("https://nyaa.si:443/download/123.torrent"),
        QStringLiteral("https://user@nyaa.si/download/123.torrent"),
        QStringLiteral("https://nyaa.si/download/123.torrent?x=1"),
        QStringLiteral("https://nyaa.si/download/123.torrent#fragment"),
        QStringLiteral("https://nyaa.si/download/abc.torrent"),
        QStringLiteral("https://nyaa.si.evil.test/download/123.torrent"),
        QStringLiteral("https://nyaa.si/download/%31%32%33.torrent"),
    };
    for (const QString& url : rejected)
        require(!MangaNyaaSource::isValidTorrentUrl(url), "unsafe Nyaa URL form is rejected");
}

void testValidAndEmptyResponses()
{
    const QUrl endpoint(QStringLiteral("https://nyaa.si/"));
    FakeNetworkAccessManager nam([](const QNetworkRequest&, const QByteArray&, int index) {
        ReplyPlan plan;
        plan.body = index == 0 ? response(QJsonArray{validRow()}) : emptyResponse();
        return plan;
    });
    MangaNyaaSource source(&nam, endpoint, 200, nullptr);
    QList<MangaNyaaCandidate> found;
    int successCount = 0;
    int failureCount = 0;
    QObject::connect(&source, &MangaNyaaSource::searchSucceeded,
                     [&found, &successCount](const QString&, const QList<MangaNyaaCandidate>& rows) {
        ++successCount;
        found = rows;
    });
    QObject::connect(&source, &MangaNyaaSource::searchFailed,
                     [&failureCount](const QString&, const QString&) { ++failureCount; });

    source.search(testSeries(), QStringLiteral("2"));
    waitForOutcome(source);
    require(successCount == 1 && failureCount == 0, "valid rows produce one successful search");
    require(found.size() == 1, "query family duplicate hashes merge once");
    const MangaNyaaCandidate& candidate = found.first();
    require(candidate.infoHash == QStringLiteral("0123456789abcdef0123456789abcdef01234567"),
            "infohash is validated and canonicalized");
    require(candidate.magnetUri.startsWith(QStringLiteral("magnet:?xt=urn:btih:")),
            "native tracker-bearing magnet is derived from the validated hash");
    require(candidate.coverageLo == QStringLiteral("2") && candidate.coverageHi == QStringLiteral("2")
                && candidate.standalone,
            "volume coverage and standalone flag are derived locally from title");
    require(candidate.digitalHint, "digital hint is derived locally from title");
    require(candidate.torrentUrl == QStringLiteral("https://nyaa.si/download/123456.torrent"),
            "strict Nyaa metainfo URL is retained");
    require(candidate.seeders == 17 && candidate.leechers == 3
                && candidate.sizeBytes == 1234567,
            "typed numeric counts and size are retained");
    require(candidate.discoveredAt > 0 && !candidate.query.isEmpty(),
            "query provenance and discovery timestamp are derived locally");

    require(!nam.requests.isEmpty(), "search emitted native RSS requests");
    require(nam.requests.size() <= 8, "query family remains capped at eight RSS requests");
    for (const auto& request : nam.requests) {
        require(request.operation == QNetworkAccessManager::GetOperation,
                "Nyaa search uses a native GET request");
        const QUrl requestedUrl = request.request.url();
        require(requestedUrl.scheme() == QStringLiteral("https")
                    && requestedUrl.host() == QStringLiteral("nyaa.si")
                    && requestedUrl.path() == QStringLiteral("/"),
                "Nyaa requests target the canonical HTTPS origin");
        require(request.request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt()
                    == QNetworkRequest::ManualRedirectPolicy,
                "native RSS redirects are disabled");
        require(request.request.rawHeader("Accept").contains("application/rss+xml"),
                "native request explicitly accepts RSS XML");
        require(request.body.isEmpty(), "native RSS request has no body");
        const QUrlQuery query(requestedUrl);
        require(query.queryItemValue(QStringLiteral("page")) == QStringLiteral("rss")
                    && query.queryItemValue(QStringLiteral("c")) == QStringLiteral("3_1")
                    && query.queryItemValue(QStringLiteral("s")) == QStringLiteral("seeders")
                    && query.queryItemValue(QStringLiteral("o")) == QStringLiteral("desc"),
                "native RSS request retains Nyaa category and seeder ordering");
        const QString searchQuery = query.queryItemValue(QStringLiteral("q"));
        require(!searchQuery.isEmpty() && searchQuery.size() <= 512,
                "each RSS query is bounded and preserved in URL provenance");
    }

    FakeNetworkAccessManager emptyNam([](const QNetworkRequest&, const QByteArray&, int) {
        ReplyPlan plan;
        plan.body = emptyResponse();
        return plan;
    });
    MangaNyaaSource emptySource(&emptyNam, endpoint, 200, nullptr);
    int emptySuccess = 0;
    int emptyFailure = 0;
    QObject::connect(&emptySource, &MangaNyaaSource::searchSucceeded,
                     [&emptySuccess](const QString&, const QList<MangaNyaaCandidate>& rows) {
        ++emptySuccess;
        require(rows.isEmpty(), "empty RSS result is preserved");
    });
    QObject::connect(&emptySource, &MangaNyaaSource::searchFailed,
                     [&emptyFailure](const QString&, const QString&) { ++emptyFailure; });
    emptySource.search(testSeries(), QStringLiteral("2"));
    waitForOutcome(emptySource);
    require(emptySuccess == 1 && emptyFailure == 0, "empty RSS feed is a successful search");
}

void testStrictRssValidation()
{
    const QUrl endpoint(QStringLiteral("https://nyaa.si/"));
    const auto runInvalid = [&endpoint](const QByteArray& body, const char* reason) {
        FakeNetworkAccessManager nam([body](const QNetworkRequest&, const QByteArray&, int) {
            ReplyPlan plan;
            plan.body = body;
            return plan;
        });
        MangaNyaaSource source(&nam, endpoint, 200, nullptr);
        int successes = 0;
        QString failure;
        QObject::connect(&source, &MangaNyaaSource::searchSucceeded,
                         [&successes](const QString&, const QList<MangaNyaaCandidate>&) { ++successes; });
        QObject::connect(&source, &MangaNyaaSource::searchFailed,
                         [&failure](const QString&, const QString& error) { failure = error; });
        source.search(testSeries(), QStringLiteral("2"));
        waitForOutcome(source);
        require(successes == 0 && !failure.isEmpty(), reason);
    };

    runInvalid(response(QJsonArray{validRow(
        QStringLiteral("Example Manga Vol 02"), QStringLiteral("not-a-hash"))}),
               "malformed RSS infohashes fail strict validation");

    runInvalid(response(QJsonArray{validRow(
        QStringLiteral("Example Manga Vol 02"),
        QStringLiteral("0123456789abcdef0123456789abcdef01234567"),
        QStringLiteral("https://nyaa.si:443/download/123456.torrent"))}),
               "unsafe metainfo URL is rejected before it reaches native fetching");

    runInvalid(QByteArray("<!DOCTYPE rss [<!ENTITY x 'expanded'>]><rss><channel></channel></rss>"),
               "RSS DTD declarations are rejected before entity expansion");
    QByteArray deeplyNested("<rss><channel>");
    for (int i = 0; i < 63; ++i)
        deeplyNested.append("<x>");
    runInvalid(deeplyNested,
               "RSS XML nesting beyond 64 elements is rejected");
    runInvalid(QByteArray("<rss xmlns:nyaa=\"https://nyaa.si/xmlns/nyaa\"><channel>"
                          "<item><title>Example Manga Vol 02</title>"
                          "<link>https://nyaa.si/download/123456.torrent</link>"
                          "<nyaa:seeders>17</nyaa:seeders><nyaa:leechers>3</nyaa:leechers>"
                          "<nyaa:infoHash>0123456789abcdef0123456789abcdef01234567</nyaa:infoHash>"
                          "<nyaa:uploader>TrustedUploader</nyaa:uploader></item>"
                          "<item><title>"),
               "malformed XML fails closed without returning earlier partial items");
    runInvalid(QByteArray{}, "empty non-XML response is rejected");

    QJsonArray tooMany;
    for (int i = 0; i < 101; ++i)
        tooMany.append(validRow());
    runInvalid(response(tooMany), "RSS feeds over 100 items are rejected");
}

void testSplitRssFields()
{
    const QByteArray xml(
        "<rss xmlns:nyaa=\"https://nyaa.si/xmlns/nyaa\"><channel><item>"
        "<title>Grand<![CDATA[ ]]>Blue Vol 02</title>"
        "<link>https://nyaa.si/download/<![CDATA[123456]]>.torrent</link>"
        "<nyaa:infoHash>0123456789abcdef0123<![CDATA[456789abcdef01234567]]></nyaa:infoHash>"
        "<nyaa:seeders>1<![CDATA[7]]></nyaa:seeders><nyaa:leechers>3</nyaa:leechers>"
        "<nyaa:size>1<![CDATA[.5]]> MiB</nyaa:size>"
        "<author>Fallback Author</author><nyaa:uploader>Preferred Uploader</nyaa:uploader>"
        "</item></channel></rss>");
    const QList<MangaNyaaCandidate> rows = MangaNyaaSource::parseRss(xml);
    require(rows.size() == 1, "RSS field text split across CDATA sections is assembled");
    const MangaNyaaCandidate& row = rows.front();
    require(row.title == QStringLiteral("Grand Blue Vol 02"),
            "whitespace-only title chunks are preserved between text segments");
    require(row.torrentUrl == QStringLiteral("https://nyaa.si/download/123456.torrent"),
            "split canonical link text is fully accumulated");
    require(row.infoHash == QStringLiteral("0123456789abcdef0123456789abcdef01234567"),
            "split infohash text is fully accumulated");
    require(row.seeders == 17 && row.leechers == 3 && row.sizeBytes == 1572864,
            "split numeric RSS fields retain their complete values");
    require(row.uploader == QStringLiteral("Preferred Uploader"),
            "Nyaa uploader takes precedence over RSS author fallback");
}

void testHttpErrorAndStreamingLimit()
{
    const QUrl endpoint(QStringLiteral("https://nyaa.si/"));
    FakeNetworkAccessManager httpNam([](const QNetworkRequest&, const QByteArray&, int index) {
        ReplyPlan plan;
        plan.status = index == 0 ? 302 : 503;
        plan.body = emptyResponse();
        return plan;
    });
    MangaNyaaSource httpSource(&httpNam, endpoint, 200, nullptr);
    QString httpFailure;
    QObject::connect(&httpSource, &MangaNyaaSource::searchFailed,
                     [&httpFailure](const QString&, const QString& error) { httpFailure = error; });
    httpSource.search(testSeries(), QStringLiteral("2"));
    waitForOutcome(httpSource);
    require(httpFailure.contains(QStringLiteral("redirects are not followed"))
                && httpFailure.contains(QStringLiteral("HTTP 503")),
            "redirects and non-2xx Nyaa responses fail with bounded status errors");

    const QByteArray oversized(1024 * 1024 + 1, 'x');
    FakeNetworkAccessManager largeNam([oversized](const QNetworkRequest&, const QByteArray&, int index) {
        ReplyPlan plan;
        plan.body = index == 0 ? oversized : emptyResponse();
        plan.advertiseLength = false;
        return plan;
    });
    MangaNyaaSource largeSource(&largeNam, endpoint, 200, nullptr);
    QString largeFailure;
    QObject::connect(&largeSource, &MangaNyaaSource::searchFailed,
                     [&largeFailure](const QString&, const QString& error) { largeFailure = error; });
    largeSource.search(testSeries(), QStringLiteral("2"));
    waitForOutcome(largeSource);
    require(largeFailure.contains(QStringLiteral("1 MiB")),
            "streaming response limit aborts an oversized body without Content-Length");
}

void testRepeatedProviderErrorsAndPartialResults()
{
    const QUrl endpoint(QStringLiteral("https://nyaa.si/"));
    FakeNetworkAccessManager failedNam([](const QNetworkRequest&, const QByteArray&, int) {
        ReplyPlan plan;
        plan.status = 503;
        return plan;
    });
    MangaNyaaSource failedSource(&failedNam, endpoint, 200, nullptr);
    SeriesSnapshot markupTitle = testSeries();
    markupTitle.title = QStringLiteral("<b>Private Series</b>");
    QString failure;
    int successSignals = 0;
    QObject::connect(&failedSource, &MangaNyaaSource::searchSucceeded,
                     [&successSignals](const QString&, const QList<MangaNyaaCandidate>&) {
        ++successSignals;
    });
    QObject::connect(&failedSource, &MangaNyaaSource::searchFailed,
                     [&failure](const QString&, const QString& reason) { failure = reason; });
    failedSource.search(markupTitle, QStringLiteral("2"));
    waitForOutcome(failedSource);
    require(failedNam.requests.size() > 1,
            "the repeated-error case spans multiple query-family requests");
    require(successSignals == 0
                && failure == QStringLiteral("Nyaa search failed (HTTP 503)"),
            "identical provider errors are collapsed to one concise reason");
    require(!failure.contains(QStringLiteral("<b>Private Series</b>"))
                && !failure.contains(QStringLiteral("Private Series")),
            "provider errors do not echo query or catalogue title text");

    FakeNetworkAccessManager partialNam([](const QNetworkRequest&, const QByteArray&, int index) {
        ReplyPlan plan;
        if (index < 2) {
            plan.status = 503;
        } else {
            plan.body = index == 2 ? response(QJsonArray{validRow()}) : emptyResponse();
        }
        return plan;
    });
    MangaNyaaSource partialSource(&partialNam, endpoint, 200, nullptr);
    QList<MangaNyaaCandidate> partialRows;
    int partialSuccesses = 0;
    int partialFailures = 0;
    QObject::connect(&partialSource, &MangaNyaaSource::searchSucceeded,
                     [&partialRows, &partialSuccesses](const QString&,
                                                        const QList<MangaNyaaCandidate>& rows) {
        ++partialSuccesses;
        partialRows = rows;
    });
    QObject::connect(&partialSource, &MangaNyaaSource::searchFailed,
                     [&partialFailures](const QString&, const QString&) { ++partialFailures; });
    partialSource.search(testSeries(), QStringLiteral("2"));
    waitForOutcome(partialSource);
    require(partialNam.requests.size() > 2
                && partialSuccesses == 1 && partialFailures == 0
                && partialRows.size() == 1,
            "repeated failed queries do not discard a successful sibling result");
}

void testIndependentDeadlinesAndPartialSuccess()
{
    const QUrl endpoint(QStringLiteral("https://nyaa.si/"));
    FakeNetworkAccessManager nam([](const QNetworkRequest&, const QByteArray&, int index) {
        ReplyPlan plan;
        if (index == 0) {
            plan.neverFinish = true;
            return plan;
        }
        plan.body = index == 1 ? response(QJsonArray{validRow()}) : emptyResponse();
        return plan;
    });
    MangaNyaaSource source(&nam, endpoint, 35, nullptr);
    QList<MangaNyaaCandidate> rows;
    int successes = 0;
    int failures = 0;
    QObject::connect(&source, &MangaNyaaSource::searchSucceeded,
                     [&rows, &successes](const QString&, const QList<MangaNyaaCandidate>& found) {
        ++successes;
        rows = found;
    });
    QObject::connect(&source, &MangaNyaaSource::searchFailed,
                     [&failures](const QString&, const QString&) { ++failures; });
    source.search(testSeries(), QStringLiteral("2"));
    waitForOutcome(source);
    require(successes == 1 && failures == 0 && rows.size() == 1,
            "one timed-out query does not discard a successful sibling query");
    require(nam.requests.size() > 1 && nam.requests.size() <= 8,
            "each query receives an independent bounded request");
}

void testDuplicateCompletionAndCancellation()
{
    const QUrl endpoint(QStringLiteral("https://nyaa.si/"));
    FakeNetworkAccessManager duplicateNam([](const QNetworkRequest&, const QByteArray&, int) {
        ReplyPlan plan;
        plan.body = emptyResponse();
        plan.duplicateFinished = true;
        return plan;
    });
    MangaNyaaSource source(&duplicateNam, endpoint, 200, nullptr);
    int successes = 0;
    int failures = 0;
    QObject::connect(&source, &MangaNyaaSource::searchSucceeded,
                     [&successes](const QString&, const QList<MangaNyaaCandidate>&) { ++successes; });
    QObject::connect(&source, &MangaNyaaSource::searchFailed,
                     [&failures](const QString&, const QString&) { ++failures; });
    source.search(testSeries(), QStringLiteral("2"));
    waitForOutcome(source);
    QCoreApplication::processEvents();
    require(successes == 1 && failures == 0,
            "duplicate finished signals cannot complete one search twice");

    FakeNetworkAccessManager lateNam([](const QNetworkRequest&, const QByteArray&, int) {
        ReplyPlan plan;
        plan.neverFinish = true;
        return plan;
    });
    MangaNyaaSource cancellable(&lateNam, endpoint, 200, nullptr);
    int cancelledSignals = 0;
    QObject::connect(&cancellable, &MangaNyaaSource::searchSucceeded,
                     [&cancelledSignals](const QString&, const QList<MangaNyaaCandidate>&) { ++cancelledSignals; });
    QObject::connect(&cancellable, &MangaNyaaSource::searchFailed,
                     [&cancelledSignals](const QString&, const QString&) { ++cancelledSignals; });
    cancellable.search(testSeries(), QStringLiteral("2"));
    require(!lateNam.requests.isEmpty(), "cancellable search started requests");
    QPointer<FakeReply> lateReply = lateNam.requests.first().reply;
    cancellable.cancel();
    require(lateReply && lateReply->abortCount > 0, "cancel aborts in-flight replies");
    lateReply->emitLateFinished(); // hostile duplicate completion after cancellation
    QCoreApplication::processEvents();
    require(cancelledSignals == 0, "late completion after cancel is suppressed");

    SeriesSnapshot tooLong = testSeries();
    tooLong.title = QString(513, QLatin1Char('x'));
    FakeNetworkAccessManager invalidQueryNam([](const QNetworkRequest&, const QByteArray&, int) {
        return ReplyPlan{};
    });
    MangaNyaaSource invalidQuerySource(&invalidQueryNam, endpoint, 200, nullptr);
    int invalidQueryFailures = 0;
    QObject::connect(&invalidQuerySource, &MangaNyaaSource::searchFailed,
                     [&invalidQueryFailures](const QString&, const QString&) { ++invalidQueryFailures; });
    invalidQuerySource.search(tooLong, QStringLiteral("2"));
    require(invalidQueryFailures == 1 && invalidQueryNam.requests.isEmpty(),
            "queries above 512 characters fail without native RSS traffic");

    FakeNetworkAccessManager destructionNam([](const QNetworkRequest&, const QByteArray&, int) {
        ReplyPlan plan;
        plan.neverFinish = true;
        return plan;
    });
    auto* destroyedSource = new MangaNyaaSource(&destructionNam, endpoint, 200, nullptr);
    destroyedSource->search(testSeries(), QStringLiteral("2"));
    QPointer<FakeReply> destroyedReply = destructionNam.requests.first().reply;
    delete destroyedSource;
    require(destroyedReply && destroyedReply->abortCount > 0,
            "source destruction aborts its in-flight replies");
    destroyedReply->emitLateFinished();
    QCoreApplication::processEvents();
}

void testNativeMetainfoFetcher()
{
    int invalidRequestFailures = 0;
    FakeNetworkAccessManager unusedNam([](const QNetworkRequest&, const QByteArray&, int) {
        return ReplyPlan{};
    });
    MangaTorrentMetainfoFetcher invalidFetcher(&unusedNam);
    QObject::connect(&invalidFetcher, &IMangaTorrentMetainfoFetcher::fetchFailed,
                     [&invalidRequestFailures](const QString&, const QString&) {
        ++invalidRequestFailures;
    });
    invalidFetcher.fetch(QStringLiteral("https://nyaa.si/download/123.torrent?redirect=1"),
                        QStringLiteral("unsafe"));
    require(invalidRequestFailures == 1 && unusedNam.requests.isEmpty(),
            "native metainfo fetcher rejects an unsafe URL before making a request");

    const QByteArray metainfo("d4:infod4:name1:xee");
    FakeNetworkAccessManager validNam([metainfo](const QNetworkRequest&, const QByteArray&, int) {
        ReplyPlan plan;
        plan.body = metainfo;
        return plan;
    });
    MangaTorrentMetainfoFetcher validFetcher(&validNam);
    bool validDone = false;
    QByteArray fetchedBytes;
    QEventLoop validLoop;
    QObject::connect(&validFetcher, &IMangaTorrentMetainfoFetcher::fetched,
                     [&validDone, &fetchedBytes, &validLoop](const QString&, const QByteArray& bytes) {
        validDone = true;
        fetchedBytes = bytes;
        validLoop.quit();
    });
    QObject::connect(&validFetcher, &IMangaTorrentMetainfoFetcher::fetchFailed,
                     [&validDone, &validLoop](const QString&, const QString&) {
        validDone = true;
        validLoop.quit();
    });
    validFetcher.fetch(QStringLiteral("https://nyaa.si/download/123456.torrent"),
                       QStringLiteral("valid"));
    QTimer::singleShot(1000, &validLoop, &QEventLoop::quit);
    validLoop.exec();
    require(validDone && fetchedBytes == metainfo, "valid native metainfo bytes are delivered");
    require(validNam.requests.size() == 1
                && validNam.requests.first().request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt()
                    == QNetworkRequest::ManualRedirectPolicy,
            "native metainfo request explicitly rejects redirects");

    FakeNetworkAccessManager redirectNam([](const QNetworkRequest&, const QByteArray&, int) {
        ReplyPlan plan;
        plan.status = 302;
        plan.body = QByteArray("redirect");
        return plan;
    });
    MangaTorrentMetainfoFetcher redirectFetcher(&redirectNam);
    QString redirectError;
    QEventLoop redirectLoop;
    QObject::connect(&redirectFetcher, &IMangaTorrentMetainfoFetcher::fetchFailed,
                     [&redirectError, &redirectLoop](const QString&, const QString& error) {
        redirectError = error;
        redirectLoop.quit();
    });
    QObject::connect(&redirectFetcher, &IMangaTorrentMetainfoFetcher::fetched,
                     [&redirectLoop](const QString&, const QByteArray&) { redirectLoop.quit(); });
    redirectFetcher.fetch(QStringLiteral("https://nyaa.si/download/123456.torrent"),
                          QStringLiteral("redirect"));
    QTimer::singleShot(1000, &redirectLoop, &QEventLoop::quit);
    redirectLoop.exec();
    require(redirectError.contains(QStringLiteral("redirects are not followed")),
            "native metainfo fetcher fails a redirect response");

    const QByteArray oversized(4 * 1024 * 1024 + 1, 'x');
    FakeNetworkAccessManager oversizedNam([oversized](const QNetworkRequest&, const QByteArray&, int) {
        ReplyPlan plan;
        plan.body = oversized;
        plan.advertiseLength = false;
        return plan;
    });
    MangaTorrentMetainfoFetcher oversizedFetcher(&oversizedNam);
    QString oversizedError;
    QEventLoop oversizedLoop;
    QObject::connect(&oversizedFetcher, &IMangaTorrentMetainfoFetcher::fetchFailed,
                     [&oversizedError, &oversizedLoop](const QString&, const QString& error) {
        oversizedError = error;
        oversizedLoop.quit();
    });
    QObject::connect(&oversizedFetcher, &IMangaTorrentMetainfoFetcher::fetched,
                     [&oversizedLoop](const QString&, const QByteArray&) { oversizedLoop.quit(); });
    oversizedFetcher.fetch(QStringLiteral("https://nyaa.si/download/123456.torrent"),
                           QStringLiteral("oversized"));
    QTimer::singleShot(3000, &oversizedLoop, &QEventLoop::quit);
    oversizedLoop.exec();
    require(oversizedError.contains(QStringLiteral("4 MiB")),
            "native metainfo fetcher streams and rejects bodies over 4 MiB");

    FakeNetworkAccessManager destroyedNam([](const QNetworkRequest&, const QByteArray&, int) {
        ReplyPlan plan;
        plan.destroyWithoutFinish = true;
        return plan;
    });
    MangaTorrentMetainfoFetcher destroyedFetcher(&destroyedNam);
    int destroyedFailures = 0;
    QEventLoop destroyedLoop;
    QObject::connect(&destroyedFetcher, &IMangaTorrentMetainfoFetcher::fetchFailed,
                     [&destroyedFailures, &destroyedLoop](const QString&, const QString&) {
        ++destroyedFailures;
        destroyedLoop.quit();
    });
    QObject::connect(&destroyedFetcher, &IMangaTorrentMetainfoFetcher::fetched,
                     [&destroyedLoop](const QString&, const QByteArray&) { destroyedLoop.quit(); });
    destroyedFetcher.fetch(QStringLiteral("https://nyaa.si/download/123456.torrent"),
                           QStringLiteral("destroyed"));
    QTimer::singleShot(1000, &destroyedLoop, &QEventLoop::quit);
    destroyedLoop.exec();
    QCoreApplication::processEvents();
    require(destroyedFailures == 1,
            "native metainfo fetcher reports one terminal failure when reply is destroyed");

    FakeNetworkAccessManager stalledNam([](const QNetworkRequest&, const QByteArray&, int) {
        ReplyPlan plan;
        plan.neverFinish = true;
        return plan;
    });
    MangaTorrentMetainfoFetcher stalledFetcher(&stalledNam);
    int deadlineFailures = 0;
    QString deadlineError;
    QEventLoop stalledLoop;
    QObject::connect(&stalledFetcher, &IMangaTorrentMetainfoFetcher::fetchFailed,
                     [&deadlineFailures, &deadlineError, &stalledLoop](const QString&, const QString& error) {
        ++deadlineFailures;
        deadlineError = error;
        stalledLoop.quit();
    });
    QObject::connect(&stalledFetcher, &IMangaTorrentMetainfoFetcher::fetched,
                     [&stalledLoop](const QString&, const QByteArray&) { stalledLoop.quit(); });
    stalledFetcher.fetch(QStringLiteral("https://nyaa.si/download/123456.torrent"),
                         QStringLiteral("deadline"));
    QTimer::singleShot(1000, &stalledLoop, &QEventLoop::quit);
    stalledLoop.exec();
    QCoreApplication::processEvents();
    require(deadlineFailures == 1 && deadlineError.contains(QStringLiteral("test deadline")),
            "native metainfo fetcher enforces one absolute deadline and suppresses abort completion");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    testStrictNyaaUrl();
    testValidAndEmptyResponses();
    testStrictRssValidation();
    testSplitRssFields();
    testHttpErrorAndStreamingLimit();
    testRepeatedProviderErrorsAndPartialResults();
    testIndependentDeadlinesAndPartialSuccess();
    testDuplicateCompletionAndCancellation();
    testNativeMetainfoFetcher();
    std::cout << "manga_nyaa_worker_harness: native RSS ALL PASS\n";
    return 0;
}
