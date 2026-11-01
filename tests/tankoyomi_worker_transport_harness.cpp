#include "engine/TankoyomiScriptProvider.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <functional>
#include <cstring>
#include <iostream>
#include <memory>

namespace {
constexpr qint64 kResponseLimit = 2 * 1024 * 1024;

struct Probe {
    int aborts = 0;
};

struct Response {
    int status = 200;
    QByteArray body = R"JSON({"contractVersion":1,"result":[]})JSON";
    QUrl redirect;
    int delayMs = 0;
    bool neverFinish = false;
    std::shared_ptr<Probe> probe = std::make_shared<Probe>();
};

struct RequestRecord {
    QNetworkRequest request;
    QNetworkAccessManager::Operation operation = QNetworkAccessManager::UnknownOperation;
    QByteArray body;
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
        if (!m_response.redirect.isEmpty())
            setAttribute(QNetworkRequest::RedirectionTargetAttribute, m_response.redirect);
        if (!m_response.neverFinish) {
            QTimer::singleShot(m_response.delayMs, this, [this] {
                if (isFinished()) return;
                setFinished(true);
                emit readyRead();
                emit finished();
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
        requests.append(record);
        const Response response = respond ? respond(requests.constLast()) : Response{};
        return new FakeReply(request, operation, response, this);
    }
};

struct Outcome {
    int resolved = 0;
    int failed = 0;
    QVariant value;
    QString error;
    bool timedOut = false;
};

Outcome awaitOutcome(TankoyomiScriptProvider &provider, int timeoutMs = 2000)
{
    Outcome outcome;
    QEventLoop loop;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &loop, [&] {
        outcome.timedOut = true;
        loop.quit();
    });
    QObject::connect(&provider, &TankoyomiScriptProvider::resolved, &loop,
                     [&](const QString &, const QVariant &value) {
        ++outcome.resolved;
        outcome.value = value;
        loop.quit();
    });
    QObject::connect(&provider, &TankoyomiScriptProvider::failed, &loop,
                     [&](const QString &, const QString &message) {
        ++outcome.failed;
        outcome.error = message;
        loop.quit();
    });
    watchdog.start(timeoutMs);
    loop.exec();
    return outcome;
}

bool check(bool condition, const char *label, int &failures)
{
    std::cerr << (condition ? "ok " : "FAIL ") << label << '\n';
    if (!condition) ++failures;
    return condition;
}

QByteArray envelope(const QByteArray &result)
{
    return QByteArrayLiteral("{\"contractVersion\":1,\"result\":") + result + QByteArrayLiteral("}");
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    const bool includeDeadline = argc < 2 || QByteArray(argv[1]) != QByteArrayLiteral("--skip-deadline");

    {
        FakeNam nam;
        nam.respond = [](const RequestRecord &) {
            Response response;
            response.body = envelope(R"JSON([{"id":"series-1"}])JSON");
            return response;
        };
        TankoyomiScriptProvider provider("manga_source", "es", QUrl("https://worker.test/api/"), &nam);
        check(provider.isReady(), "valid HTTPS worker base is ready without loading scripts", failures);
        TankoyomiScriptProvider explicitDefaultPort("manga_source", "es",
                                                     QUrl("https://worker.test:443/api"), &nam);
        check(explicitDefaultPort.isReady(), "explicit standard HTTPS port is accepted", failures);

        provider.searchSeries("search", "Blue Lock");
        const Outcome search = awaitOutcome(provider);
        check(search.resolved == 1 && search.failed == 0 && !search.timedOut
                  && search.value.toList().value(0).toMap().value("id") == QLatin1String("series-1"),
              "search resolves the contract result array asynchronously", failures);
        check(nam.requests.size() == 1
                  && nam.requests.first().request.url()
                         == QUrl("https://worker.test/api/v1/es/manga_source/searchSeries")
                  && nam.requests.first().operation == QNetworkAccessManager::PostOperation,
              "search posts to the encoded provider endpoint", failures);
        const QJsonDocument searchBody = QJsonDocument::fromJson(nam.requests.first().body);
        check(searchBody.isObject()
                  && searchBody.object().value(QStringLiteral("query")).toString() == QLatin1String("Blue Lock"),
              "search request contains the query JSON field", failures);
        const QNetworkRequest searchRequest = nam.requests.first().request;
        check(searchRequest.rawHeader("Content-Type").startsWith("application/json")
                  && searchRequest.rawHeader("User-Agent") == QByteArrayLiteral("Colosseum-Tankoyomi/1")
                  && searchRequest.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt()
                         == QNetworkRequest::ManualRedirectPolicy,
              "worker requests use JSON headers and reject automatic redirects", failures);

        nam.respond = [](const RequestRecord &) { return Response{}; };
        provider.getChapters("chapters", {{"id", "series-1"}});
        const Outcome chapters = awaitOutcome(provider);
        provider.getPages("pages", {{"id", "chapter-1"}});
        const Outcome pages = awaitOutcome(provider);
        check(chapters.resolved == 1 && pages.resolved == 1 && nam.requests.size() == 3
                  && nam.requests.at(1).request.url().path().endsWith(QLatin1String("/getChapters"))
                  && nam.requests.at(2).request.url().path().endsWith(QLatin1String("/getPages")),
              "chapter and page methods use their corresponding worker paths", failures);
        const QJsonDocument chaptersBody = QJsonDocument::fromJson(nam.requests.at(1).body);
        const QJsonDocument pagesBody = QJsonDocument::fromJson(nam.requests.at(2).body);
        check(chaptersBody.object().value(QStringLiteral("series")).toObject()
                      .value(QStringLiteral("id")).toString() == QLatin1String("series-1")
                  && pagesBody.object().value(QStringLiteral("chapter")).toObject()
                         .value(QStringLiteral("id")).toString() == QLatin1String("chapter-1"),
              "chapter and page requests contain their named JSON inputs", failures);
    }

    {
        const QList<QUrl> invalidBases = {
            QUrl("https://worker.test/?token=secret"),
            QUrl("https://user:secret@worker.test"),
            QUrl("https://worker.test:8443"),
            QUrl("https://worker.test/#fragment"),
            QUrl("http://worker.test")};
        for (const QUrl &base : invalidBases) {
            FakeNam nam;
            TankoyomiScriptProvider provider("manga_source", "es", base, &nam);
            check(!provider.isReady(), "unsafe worker URL is rejected", failures);
            provider.searchSeries("invalid-base", "title");
            const Outcome invalid = awaitOutcome(provider);
            check(invalid.failed == 1 && !invalid.error.contains(QLatin1String("secret"))
                      && nam.requests.isEmpty(),
                  "invalid worker configuration fails asynchronously without disclosure or network", failures);
        }
    }

    {
        FakeNam nam;
        nam.respond = [](const RequestRecord &) {
            Response response;
            response.status = 503;
            response.body = "SECRET_RESPONSE_BODY";
            return response;
        };
        TankoyomiScriptProvider provider("manga_source", "es", QUrl("https://worker.test"), &nam);
        provider.searchSeries("http-error", "title");
        const Outcome httpError = awaitOutcome(provider);
        check(httpError.failed == 1 && httpError.error.contains(QLatin1String("503"))
                  && !httpError.error.contains(QLatin1String("SECRET_RESPONSE_BODY")),
              "non-2xx failures expose status without response bodies", failures);

        nam.respond = [](const RequestRecord &) {
            Response response;
            response.status = 302;
            response.redirect = QUrl("https://other.test/collect");
            return response;
        };
        provider.searchSeries("redirect", "title");
        const Outcome redirect = awaitOutcome(provider);
        check(redirect.failed == 1 && nam.requests.size() == 2,
              "all redirects are rejected without forwarding the request", failures);
    }

    {
        FakeNam nam;
        TankoyomiScriptProvider provider("manga_source", "es", QUrl("https://worker.test"), &nam);
        nam.respond = [](const RequestRecord &) {
            Response response;
            response.body = R"JSON({"contractVersion":2,"result":[]})JSON";
            return response;
        };
        provider.searchSeries("bad-version", "title");
        const Outcome version = awaitOutcome(provider);
        check(version.failed == 1 && version.resolved == 0,
              "unsupported contract versions fail closed", failures);

        nam.respond = [](const RequestRecord &) {
            Response response;
            response.body = R"JSON({"contractVersion":1,"result":{}})JSON";
            return response;
        };
        provider.searchSeries("bad-result", "title");
        const Outcome schema = awaitOutcome(provider);
        check(schema.failed == 1 && schema.resolved == 0,
              "non-array worker results fail schema validation", failures);

        nam.respond = [](const RequestRecord &) {
            Response response;
            response.body = QByteArrayLiteral("{\"contractVersion\":1,\"error\":\"secret-token\"}");
            return response;
        };
        provider.searchSeries("worker-error", "title");
        const Outcome workerError = awaitOutcome(provider);
        check(workerError.failed == 1 && !workerError.error.contains(QLatin1String("secret-token")),
              "worker error details are not echoed", failures);

        nam.respond = [](const RequestRecord &) {
            Response response;
            response.body = QByteArrayLiteral("{\"contractVersion\":1,\"result\":[")
                + char(0xff) + QByteArrayLiteral("]}");
            return response;
        };
        provider.searchSeries("bad-utf8", "title");
        const Outcome utf8 = awaitOutcome(provider);
        check(utf8.failed == 1 && utf8.error.contains(QLatin1String("invalid"), Qt::CaseInsensitive),
              "invalid UTF-8 cannot resolve as JSON", failures);
    }

    {
        FakeNam nam;
        nam.respond = [](const RequestRecord &) {
            Response response;
            response.body = QByteArray(kResponseLimit + 1, 'x');
            return response;
        };
        TankoyomiScriptProvider provider("manga_source", "es", QUrl("https://worker.test"), &nam);
        provider.searchSeries("large", "title");
        const Outcome large = awaitOutcome(provider);
        check(large.failed == 1 && large.resolved == 0
                  && large.error.contains(QLatin1String("2 MiB")),
              "responses larger than 2 MiB are rejected", failures);
    }

    {
        FakeNam nam;
        const auto probe = std::make_shared<Probe>();
        nam.respond = [probe](const RequestRecord &) {
            Response response;
            response.neverFinish = true;
            response.probe = probe;
            return response;
        };
        auto *provider = new TankoyomiScriptProvider("manga_source", "es",
                                                     QUrl("https://worker.test"), &nam);
        int emittedSignals = 0;
        QObject::connect(provider, &TankoyomiScriptProvider::failed, &app,
                         [&](const QString &, const QString &) { ++emittedSignals; });
        QObject::connect(provider, &TankoyomiScriptProvider::resolved, &app,
                         [&](const QString &, const QVariant &) { ++emittedSignals; });
        provider->searchSeries("destroy", "title");
        QCoreApplication::processEvents();
        delete provider;
        QCoreApplication::processEvents();
        check(probe->aborts == 1 && emittedSignals == 0,
              "destruction aborts and releases an outstanding worker reply without signals", failures);
    }

    if (includeDeadline) {
        FakeNam nam;
        nam.respond = [](const RequestRecord &) {
            Response response;
            response.neverFinish = true;
            return response;
        };
        TankoyomiScriptProvider provider("manga_source", "es", QUrl("https://worker.test"), &nam);
        provider.searchSeries("deadline", "title");
        const Outcome timeout = awaitOutcome(provider, 47000);
        check(timeout.failed == 1 && timeout.resolved == 0 && !timeout.timedOut
                  && timeout.error.contains(QLatin1String("timeout"), Qt::CaseInsensitive),
              "the whole worker request has a 45-second deadline", failures);
    }

    std::cerr << (failures ? "TANKOYOMI_WORKER_TRANSPORT_FAIL" : "TANKOYOMI_WORKER_TRANSPORT_OK") << '\n';
    return failures ? 1 : 0;
}
