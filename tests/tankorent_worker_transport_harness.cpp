#include "torrent/TankorentSearchService.h"
#include "torrent/TankorentWorkerIndexer.h"

#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <chrono>

namespace {
struct Probe {
    int aborts = 0;
    int lateFinishes = 0;
};

struct Response {
    int status = 200;
    QByteArray body;
    bool neverFinish = false;
    bool readyBeforeFinish = false;
    int lateFinishDelayMs = 0;
    qint64 contentLength = -1;
    std::shared_ptr<Probe> probe = std::make_shared<Probe>();
};

struct RequestRecord {
    QNetworkRequest request;
    QNetworkAccessManager::Operation operation = QNetworkAccessManager::UnknownOperation;
    QByteArray body;
    std::shared_ptr<Probe> probe;
};

class FakeReply final : public QNetworkReply {
public:
    FakeReply(const QNetworkRequest &request, QNetworkAccessManager::Operation operation,
              Response response, QObject *parent)
        : QNetworkReply(parent), m_response(std::move(response))
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(operation);
        setOpenMode(QIODevice::ReadOnly);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, m_response.status);
        if (m_response.contentLength >= 0)
            setHeader(QNetworkRequest::ContentLengthHeader, m_response.contentLength);
        if (!m_response.neverFinish) {
            QTimer::singleShot(0, this, [this] {
                if (isFinished()) return;
                if (m_response.readyBeforeFinish) {
                    emit readyRead();
                    if (isFinished()) return;
                }
                setFinished(true);
                emit readyRead();
                emit finished();
            });
        }
        if (m_response.lateFinishDelayMs > 0) {
            QTimer::singleShot(m_response.lateFinishDelayMs, this, [this] {
                ++m_response.probe->lateFinishes;
                emit readyRead();
                emit finished();
                m_allowDelete = true;
                deleteLater();
            });
        }
    }

    void abort() override
    {
        if (isFinished()) return;
        ++m_response.probe->aborts;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("aborted"));
        setFinished(true);
        emit finished();
    }

    qint64 bytesAvailable() const override
    {
        return qint64(m_response.body.size()) - m_offset + QNetworkReply::bytesAvailable();
    }

protected:
    bool event(QEvent *event) override
    {
        // Keep an aborted reply alive so the deadline test can deliver a late
        // fake completion and verify the client has disconnected it already.
        if (event->type() == QEvent::DeferredDelete
            && m_response.lateFinishDelayMs > 0 && !m_allowDelete)
            return true;
        return QNetworkReply::event(event);
    }

    qint64 readData(char *destination, qint64 maximum) override
    {
        const qint64 available = qint64(m_response.body.size()) - m_offset;
        const qint64 count = qMin(maximum, available);
        if (count <= 0) return -1;
        std::memcpy(destination, m_response.body.constData() + m_offset, size_t(count));
        m_offset += count;
        return count;
    }

private:
    Response m_response;
    qint64 m_offset = 0;
    bool m_allowDelete = false;
};

class FakeNam final : public QNetworkAccessManager {
public:
    QList<RequestRecord> requests;
    std::function<Response(const RequestRecord &)> respond;

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData = nullptr) override
    {
        RequestRecord record;
        record.request = request;
        record.operation = operation;
        if (outgoingData) record.body = outgoingData->readAll();
        Response response = respond ? respond(record) : Response{};
        record.probe = response.probe;
        requests.append(record);
        return new FakeReply(request, operation, std::move(response), this);
    }
};

bool check(bool condition, const char *label, int &failures)
{
    std::cerr << (condition ? "ok " : "FAIL ") << label << '\n';
    if (!condition) ++failures;
    return condition;
}

QString sourceName(const QString &sourceId)
{
    if (sourceId == QLatin1String("piratebay")) return QStringLiteral("PirateBay");
    if (sourceId == QLatin1String("exttorrents")) return QStringLiteral("ExtraTorrents");
    if (sourceId == QLatin1String("torrentscsv")) return QStringLiteral("Torrents-CSV");
    return QStringLiteral("Knaben");
}

QByteArray validEnvelope(const QString &sourceId, const QString &sourceKey = {})
{
    const QString key = sourceKey.isEmpty() ? sourceId : sourceKey;
    QJsonObject row;
    row.insert(QStringLiteral("title"), QStringLiteral("Example torrent"));
    row.insert(QStringLiteral("magnetUri"),
               QStringLiteral("magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&dn=Example"));
    row.insert(QStringLiteral("sizeBytes"), 8192);
    row.insert(QStringLiteral("seeders"), 12);
    row.insert(QStringLiteral("leechers"), 3);
    row.insert(QStringLiteral("sourceName"), sourceName(sourceId));
    row.insert(QStringLiteral("sourceKey"), key);
    row.insert(QStringLiteral("categoryId"), QStringLiteral("601"));
    row.insert(QStringLiteral("category"), QStringLiteral("Books"));
    row.insert(QStringLiteral("infoHash"), QStringLiteral("0123456789abcdef0123456789abcdef01234567"));
    row.insert(QStringLiteral("publishDate"), QStringLiteral("2026-10-23T12:34:56Z"));
    row.insert(QStringLiteral("detailsUrl"), QStringLiteral("https://example.test/details/1"));

    QJsonObject envelope;
    envelope.insert(QStringLiteral("contractVersion"), 1);
    envelope.insert(QStringLiteral("sourceId"), sourceId);
    envelope.insert(QStringLiteral("result"), QJsonArray{row});
    return QJsonDocument(envelope).toJson(QJsonDocument::Compact);
}

QByteArray errorEnvelope(const QString &sourceId, const QString &health)
{
    QJsonObject envelope;
    envelope.insert(QStringLiteral("contractVersion"), 1);
    envelope.insert(QStringLiteral("sourceId"), sourceId);
    envelope.insert(QStringLiteral("error"), QStringLiteral("upstream rate limit"));
    envelope.insert(QStringLiteral("health"), health);
    return QJsonDocument(envelope).toJson(QJsonDocument::Compact);
}

bool awaitSearch(TankorentSearchService &service, const QString &handle,
                 int timeoutMs = 2000)
{
    QEventLoop loop;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    bool finished = false;
    const auto connection = QObject::connect(
        &service, &TankorentSearchService::searchFinished, &loop,
        [&](const QString &completedHandle) {
            if (completedHandle == handle) {
                finished = true;
                loop.quit();
            }
        });
    QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
    watchdog.start(timeoutMs);
    loop.exec();
    QObject::disconnect(connection);
    return finished;
}

bool awaitIndexer(TankorentWorkerIndexer &indexer, bool &succeeded,
                  QString &error, int timeoutMs = 2000)
{
    QEventLoop loop;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    bool finished = false;
    QObject::connect(&indexer, &TorrentIndexer::searchFinished, &loop,
                     [&](const QList<TorrentResult> &) {
        succeeded = true;
        finished = true;
        loop.quit();
    });
    QObject::connect(&indexer, &TorrentIndexer::searchError, &loop,
                     [&](const QString &message) {
        error = message;
        finished = true;
        loop.quit();
    });
    QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
    watchdog.start(timeoutMs);
    loop.exec();
    return finished;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Colosseum"));
    QCoreApplication::setApplicationName(QStringLiteral("tankorent_worker_transport_harness"));

    QTemporaryDir settingsRoot;
    if (!settingsRoot.isValid()) {
        std::cerr << "FAIL unable to create isolated settings directory\n";
        return 1;
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsRoot.path());

    int failures = 0;
    {
        QSettings settings;
        settings.clear();
        settings.setValue(QStringLiteral("tankorent/indexers/exttorrents/enabled"), false);
        settings.sync();

        FakeNam nam;
        nam.respond = [](const RequestRecord &record) {
            const QString sourceId = record.request.url().path().section('/', 2, 2);
            Response response;
            response.body = validEnvelope(sourceId);
            return response;
        };
        TankorentSearchService service(&nam);
        QList<TorrentResult> results;
        int resultSignals = 0;
        int errorSignals = 0;
        bool finishedSignal = false;
        QObject::connect(&service, &TankorentSearchService::resultsReady, &service,
                         [&](const QString &, const QList<TorrentResult> &slice) {
            ++resultSignals;
            results.append(slice);
        });
        QObject::connect(&service, &TankorentSearchService::indexerError, &service,
                         [&](const QString &, const QString &, const QString &) { ++errorSignals; });
        QObject::connect(&service, &TankorentSearchService::searchFinished, &service,
                         [&](const QString &) { finishedSignal = true; });

        const QString handle = service.startSearch(QStringLiteral("books"), QStringLiteral("all"),
                                                   QStringLiteral("The Hobbit"), 15);
        check(!handle.isEmpty() && !finishedSignal,
              "search handle is returned before queued network completion", failures);
        check(awaitSearch(service, handle), "configured search settles asynchronously", failures);
        check(nam.requests.size() == 2 && resultSignals == 2 && errorSignals == 0
                  && results.size() == 2,
              "disabled source is omitted while remaining results are forwarded", failures);

        QStringList paths;
        bool requestShapesValid = true;
        for (const RequestRecord &request : nam.requests) {
            paths.append(request.request.url().path());
            const QJsonDocument body = QJsonDocument::fromJson(request.body);
            const QJsonObject object = body.object();
            const QString expectedCategory = request.request.url().path().endsWith(QLatin1String("/piratebay/search"))
                ? QStringLiteral("601") : QString();
            requestShapesValid = requestShapesValid
                && request.operation == QNetworkAccessManager::PostOperation
                && request.request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt()
                       == QNetworkRequest::ManualRedirectPolicy
                && request.request.transferTimeout() == 25000
                && request.request.rawHeader("Content-Type").startsWith("application/json")
                && object.value(QStringLiteral("query")).toString() == QLatin1String("The Hobbit")
                && object.value(QStringLiteral("limit")).toInt() == 15
                && object.value(QStringLiteral("categoryId")).toString() == expectedCategory;
        }
        check(paths.contains(QLatin1String("/v1/piratebay/search"))
                  && paths.contains(QLatin1String("/v1/torrentscsv/search"))
                  && requestShapesValid,
              "worker POST paths and query/limit/category JSON preserve source routing", failures);
        bool fieldsValid = results.size() == 2;
        for (const TorrentResult &result : results) {
            fieldsValid = fieldsValid
                && result.title == QLatin1String("Example torrent")
                && result.infoHash == QLatin1String("0123456789abcdef0123456789abcdef01234567")
                && result.sizeBytes == 8192 && result.seeders == 12 && result.leechers == 3
                && (result.sourceKey == QLatin1String("piratebay")
                    || result.sourceKey == QLatin1String("torrentscsv"));
            fieldsValid = fieldsValid
                && result.publishDate.isValid()
                && result.detailsUrl == QLatin1String("https://example.test/details/1");
        }
        check(fieldsValid, "typed torrent fields and provenance survive validated JSON parsing", failures);
    }

    {
        QSettings settings;
        settings.clear();
        settings.sync();
        FakeNam nam;
        nam.respond = [](const RequestRecord &record) {
            const QString sourceId = record.request.url().path().section('/', 2, 2);
            Response response;
            response.body = validEnvelope(sourceId);
            return response;
        };
        TankorentSearchService service(&nam);
        const QString knabenHandle = service.startSearch(QStringLiteral("books"), QStringLiteral("knaben"),
                                                         QStringLiteral("Some title"), 9);
        check(!knabenHandle.isEmpty() && awaitSearch(service, knabenHandle)
                  && nam.requests.size() == 1
                  && nam.requests.first().request.url().path() == QLatin1String("/v1/knaben/search"),
              "explicit Knaben selection bypasses the media allowlist", failures);

        const QString allHandle = service.startSearch({}, QStringLiteral("all"),
                                                      QStringLiteral("Some title"), 9,
                                                      QStringLiteral("42"));
        check(!allHandle.isEmpty() && awaitSearch(service, allHandle)
                  && nam.requests.size() == 5,
              "unfiltered search includes Knaben and every enabled source", failures);
        bool customCategoryForwarded = true;
        for (int i = 1; i < nam.requests.size(); ++i) {
            const QJsonObject body = QJsonDocument::fromJson(nam.requests.at(i).body).object();
            customCategoryForwarded = customCategoryForwarded
                && body.value(QStringLiteral("categoryId")).toString() == QLatin1String("42");
        }
        check(customCategoryForwarded, "explicit category id is passed to each Worker source", failures);
    }

    {
        FakeNam nam;
        Response response;
        response.status = 503;
        response.body = errorEnvelope(QStringLiteral("knaben"), QStringLiteral("rate_limited"));
        nam.respond = [response](const RequestRecord &) { return response; };
        TankorentWorkerIndexer indexer(&nam, QStringLiteral("knaben"));
        bool succeeded = false;
        QString error;
        indexer.search(QStringLiteral("title"));
        const bool completed = awaitIndexer(indexer, succeeded, error);
        check(completed && !succeeded && error.contains(QLatin1String("upstream rate limit"))
                  && indexer.health() == IndexerHealth::RateLimited,
              "non-2xx Worker error envelope preserves its bounded error and health", failures);
    }

    {
        struct InvalidCase { const char *label; std::function<void(QJsonObject &)> mutate; };
        const QList<InvalidCase> cases = {
            {"contract version", [](QJsonObject &root) { root[QStringLiteral("contractVersion")] = 2; }},
            {"source id", [](QJsonObject &root) { root[QStringLiteral("sourceId")] = QStringLiteral("piratebay"); }},
            {"result type", [](QJsonObject &root) { root[QStringLiteral("result")] = QStringLiteral("not-an-array"); }},
            {"source key", [](QJsonObject &root) {
                 QJsonArray rows = root.value(QStringLiteral("result")).toArray();
                 QJsonObject row = rows.first().toObject();
                 row[QStringLiteral("sourceKey")] = QStringLiteral("torrentscsv");
                 rows[0] = row;
                 root[QStringLiteral("result")] = rows;
             }},
            {"magnet hash", [](QJsonObject &root) {
                 QJsonArray rows = root.value(QStringLiteral("result")).toArray();
                 QJsonObject row = rows.first().toObject();
                 row[QStringLiteral("magnetUri")] = QStringLiteral("magnet:?xt=urn:btih:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
                 rows[0] = row;
                 root[QStringLiteral("result")] = rows;
             }},
            {"negative count", [](QJsonObject &root) {
                 QJsonArray rows = root.value(QStringLiteral("result")).toArray();
                 QJsonObject row = rows.first().toObject();
                 row[QStringLiteral("seeders")] = -1;
                 rows[0] = row;
                 root[QStringLiteral("result")] = rows;
             }},
            {"invalid date", [](QJsonObject &root) {
                 QJsonArray rows = root.value(QStringLiteral("result")).toArray();
                 QJsonObject row = rows.first().toObject();
                 row[QStringLiteral("publishDate")] = QStringLiteral("yesterday-ish");
                 rows[0] = row;
                 root[QStringLiteral("result")] = rows;
             }},
        };
        for (const InvalidCase &testCase : cases) {
            FakeNam nam;
            QByteArray body = validEnvelope(QStringLiteral("knaben"));
            QJsonObject root = QJsonDocument::fromJson(body).object();
            testCase.mutate(root);
            Response response;
            response.body = QJsonDocument(root).toJson(QJsonDocument::Compact);
            nam.respond = [response](const RequestRecord &) { return response; };
            TankorentWorkerIndexer indexer(&nam, QStringLiteral("knaben"));
            bool succeeded = false;
            QString error;
            indexer.search(QStringLiteral("title"));
            const bool completed = awaitIndexer(indexer, succeeded, error);
            const QString label = QStringLiteral("bad remote %1 is rejected").arg(QLatin1String(testCase.label));
            check(completed && !succeeded
                      && (error.contains(QLatin1String("validation"))
                          || error.contains(QLatin1String("contract"))
                          || error.contains(QLatin1String("result array"))),
                  label.toLocal8Bit().constData(), failures);
        }
    }

    {
        FakeNam nam;
        Response response;
        response.status = 302;
        response.body = validEnvelope(QStringLiteral("knaben"));
        nam.respond = [response](const RequestRecord &) { return response; };
        TankorentWorkerIndexer indexer(&nam, QStringLiteral("knaben"));
        bool succeeded = false;
        QString error;
        indexer.search(QStringLiteral("title"));
        const bool completed = awaitIndexer(indexer, succeeded, error);
        check(completed && !succeeded && error.contains(QLatin1String("redirects"))
                  && nam.requests.size() == 1,
              "HTTP redirects are rejected without following another URL", failures);
    }

    {
        FakeNam nam;
        Response response;
        response.body = QByteArray(1024 * 1024 + 1, 'x');
        response.readyBeforeFinish = true;
        nam.respond = [response](const RequestRecord &) { return response; };
        TankorentWorkerIndexer indexer(&nam, QStringLiteral("knaben"));
        bool succeeded = false;
        QString error;
        int errors = 0;
        QObject::connect(&indexer, &TorrentIndexer::searchError, &indexer,
                         [&](const QString &) { ++errors; });
        indexer.search(QStringLiteral("title"));
        const bool completed = awaitIndexer(indexer, succeeded, error);
        QCoreApplication::processEvents();
        check(completed && !succeeded && error.contains(QLatin1String("1 MiB")),
              "response body is capped at 1 MiB", failures);
        check(errors == 1, "oversize abort produces exactly one completion error", failures);
    }

    {
        FakeNam nam;
        Response response;
        response.neverFinish = true;
        nam.respond = [response](const RequestRecord &) { return response; };
        TankorentSearchService service(&nam);
        int resultSignals = 0;
        int finishedSignals = 0;
        QObject::connect(&service, &TankorentSearchService::resultsReady, &service,
                         [&](const QString &, const QList<TorrentResult> &) { ++resultSignals; });
        QObject::connect(&service, &TankorentSearchService::searchFinished, &service,
                         [&](const QString &) { ++finishedSignals; });
        const QString handle = service.startSearch(QStringLiteral("books"), QStringLiteral("piratebay"),
                                                   QStringLiteral("title"), 10);
        check(!handle.isEmpty() && nam.requests.size() == 1,
              "cancellable search creates its in-flight request", failures);
        service.cancelSearch(handle);
        QCoreApplication::processEvents();
        check(nam.requests.first().probe->aborts == 1
                  && resultSignals == 0 && finishedSignals == 0
                  && !service.isActive(handle),
              "cancel aborts the network reply and suppresses terminal signals", failures);
    }

    {
        FakeNam nam;
        Response response;
        response.neverFinish = true;
        response.lateFinishDelayMs = 26500;
        nam.respond = [response](const RequestRecord &) { return response; };
        TankorentWorkerIndexer indexer(&nam, QStringLiteral("knaben"));
        bool succeeded = false;
        QString error;
        int errors = 0;
        int results = 0;
        QObject::connect(&indexer, &TorrentIndexer::searchError, &indexer,
                         [&](const QString &) { ++errors; });
        QObject::connect(&indexer, &TorrentIndexer::searchFinished, &indexer,
                         [&](const QList<TorrentResult> &) { ++results; });
        const auto started = std::chrono::steady_clock::now();
        indexer.search(QStringLiteral("deadline probe"));
        const bool completed = awaitIndexer(indexer, succeeded, error, 28000);

        QEventLoop lateLoop;
        QTimer latePoll;
        latePoll.setInterval(20);
        QObject::connect(&latePoll, &QTimer::timeout, &lateLoop, [&] {
            if (!nam.requests.isEmpty() && nam.requests.first().probe->lateFinishes > 0)
                lateLoop.quit();
        });
        QTimer lateWatchdog;
        lateWatchdog.setSingleShot(true);
        QObject::connect(&lateWatchdog, &QTimer::timeout, &lateLoop, &QEventLoop::quit);
        latePoll.start();
        lateWatchdog.start(5000);
        if (nam.requests.isEmpty() || nam.requests.first().probe->lateFinishes == 0)
            lateLoop.exec();

        const qint64 elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        const bool lateObserved = !nam.requests.isEmpty()
            && nam.requests.first().probe->lateFinishes == 1;
        check(completed && !succeeded && error.contains(QLatin1String("25 second deadline"))
                  && !nam.requests.isEmpty() && nam.requests.first().probe->aborts == 1
                  && elapsedMs >= 26000,
              "hard deadline aborts a never-finishing reply after 25 seconds", failures);
        check(lateObserved && errors == 1 && results == 0,
              "late reply completion is suppressed after the single deadline error", failures);
        std::cerr << "deadline case elapsed: " << elapsedMs << " ms\n";
    }

    {
        FakeNam nam;
        nam.respond = [](const RequestRecord &) { return Response{}; };
        TankorentWorkerIndexer indexer(&nam, QStringLiteral("knaben"));
        bool succeeded = false;
        QString error;
        indexer.search(QString(513, QLatin1Char('x')));
        const bool completed = awaitIndexer(indexer, succeeded, error);
        const QByteArray capped = TankorentWorkerIndexer::buildRequestBody(
            QString(600, QLatin1Char('q')), 900, QString(200, QLatin1Char('c')));
        const QJsonObject request = QJsonDocument::fromJson(capped).object();
        check(completed && !succeeded && nam.requests.isEmpty()
                  && error.contains(QLatin1String("size limits"))
                  && request.value(QStringLiteral("query")).toString().size() == 512
                  && request.value(QStringLiteral("limit")).toInt() == 300
                  && request.value(QStringLiteral("categoryId")).toString().size() == 128,
              "oversized fields fail closed and request serialization applies hard caps", failures);
    }

    std::cerr << (failures == 0 ? "VERDICT: PASS\n" : "VERDICT: FAIL\n");
    return failures == 0 ? 0 : 1;
}
