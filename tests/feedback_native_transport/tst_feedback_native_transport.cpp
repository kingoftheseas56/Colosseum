#include "../../native/feedback/FeedbackRequestService.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <cstring>

using namespace Qt::StringLiterals;

class FeedbackNativeTransportTest : public QObject
{
    Q_OBJECT
private slots:
    void rateLimitAndRetry();
    void blocksChallengeAndUntrustedDestination();
    void preservesUniverseArticle();
};

namespace {
class StubReply final : public QNetworkReply
{
public:
    StubReply(const QNetworkRequest &request, int status, QByteArray body, QObject *parent)
        : QNetworkReply(parent), m_body(std::move(body))
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        open(ReadOnly | Unbuffered);
        QTimer::singleShot(0, this, [this] {
            setFinished(true);
            emit readyRead();
            emit finished();
        });
    }

    void abort() override {}

    qint64 bytesAvailable() const override
    {
        return m_body.size() - m_offset + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 size = qMin(maxSize, qint64(m_body.size() - m_offset));
        if (size <= 0)
            return -1;
        std::memcpy(data, m_body.constData() + m_offset, size_t(size));
        m_offset += size;
        return size;
    }

private:
    QByteArray m_body;
    qint64 m_offset = 0;
};

class StubNetwork final : public QNetworkAccessManager
{
public:
    struct Response { int status; QByteArray body; };
    struct Call { Operation operation; QNetworkRequest request; QByteArray body; };
    QList<Response> responses;
    QList<Call> calls;

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *data) override
    {
        calls.append({operation, request, data ? data->readAll() : QByteArray()});
        const auto response = responses.isEmpty() ? Response{500, {}} : responses.takeFirst();
        return new StubReply(request, response.status, response.body, this);
    }
};

const QByteArray formspark = R"({"provider":"formspark","formEndpoint":"https://submit-form.com/testForm"})";
const QVariantMap featureFields{{u"title"_s, u"Better catalog filters"_s},
                               {u"message"_s, u"Add a category filter to the catalogue."_s},
                               {u"benefit"_s, u"Find the right catalogue faster."_s},
                               {u"reference"_s, u"safe-retry-id"_s}};
}

void FeedbackNativeTransportTest::rateLimitAndRetry()
{
    StubNetwork network;
    network.responses = {{200, formspark}, {429, "{}"}, {200, formspark}, {200, "{}"}};
    FeedbackRequestService service(nullptr, &network);
    QSignalSpy finished(&service, &FeedbackRequestService::submissionFinished);

    service.submit(u"feature"_s, featureFields);
    service.submit(u"feature"_s, featureFields);
    QTRY_COMPARE(finished.size(), 1);
    QVERIFY(!finished[0][0].toBool());
    QVERIFY(finished[0][1].toString().contains(u"Too many attempts"));
    QCOMPARE(network.calls.size(), 2);

    service.submit(u"feature"_s, featureFields);
    QTRY_COMPARE(finished.size(), 2);
    QVERIFY(finished[1][0].toBool());
    QCOMPARE(network.calls.size(), 4);
    const auto &firstPost = network.calls[1];
    const auto &lastPost = network.calls[3];
    QCOMPARE(firstPost.body, lastPost.body);
    QCOMPARE(firstPost.operation, QNetworkAccessManager::PostOperation);
    QCOMPARE(firstPost.request.url(), QUrl(u"https://submit-form.com/testForm"_s));
    QCOMPARE(firstPost.request.rawHeader("Accept"), QByteArray("application/json"));
    QCOMPARE(firstPost.request.header(QNetworkRequest::ContentTypeHeader).toString(), u"application/json"_s);
    QCOMPARE(firstPost.request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
             int(QNetworkRequest::ManualRedirectPolicy));
    QCOMPARE(firstPost.request.attribute(QNetworkRequest::CookieLoadControlAttribute).toInt(),
             int(QNetworkRequest::Manual));
    const auto body = QJsonDocument::fromJson(firstPost.body).object();
    QCOMPARE(body.value(u"type"_s).toString(), u"Feature idea"_s);
    QCOMPARE(body.value(u"title"_s).toString(), featureFields.value(u"title"_s).toString());
    QCOMPARE(body.value(u"message"_s).toString(), featureFields.value(u"message"_s).toString());
    QCOMPARE(body.value(u"benefit"_s).toString(), featureFields.value(u"benefit"_s).toString());
    QCOMPARE(body.value(u"reference"_s).toString(), u"safe-retry-id"_s);
    QCOMPARE(body.value(u"_email"_s).toObject().value(u"subject"_s).toString(),
             u"[Colosseum] [Feature idea] [General] Better catalog filters"_s);
}

void FeedbackNativeTransportTest::blocksChallengeAndUntrustedDestination()
{
    StubNetwork network;
    network.responses = {
        {200, R"({"provider":"formspark","formEndpoint":"https://submit-form.com/testForm","turnstileSiteKey":"challenge"})"},
        {200, R"({"provider":"formspark","formEndpoint":"https://submit-form.com.example/testForm"})"}
    };
    FeedbackRequestService service(nullptr, &network);
    QSignalSpy finished(&service, &FeedbackRequestService::submissionFinished);
    service.submit(u"feature"_s, featureFields);
    QTRY_COMPARE(finished.size(), 1);
    QVERIFY(!finished[0][0].toBool());
    service.submit(u"feature"_s, featureFields);
    QTRY_COMPARE(finished.size(), 2);
    QVERIFY(!finished[1][0].toBool());
    QCOMPARE(network.calls.size(), 2);
    for (const auto &call : network.calls)
        QCOMPARE(call.operation, QNetworkAccessManager::GetOperation);
}

void FeedbackNativeTransportTest::preservesUniverseArticle()
{
    StubNetwork network;
    network.responses = {{200, formspark}, {200, "{}"}};
    FeedbackRequestService service(nullptr, &network);
    QSignalSpy finished(&service, &FeedbackRequestService::submissionFinished);
    service.submit(u"universe"_s, {{u"title"_s, u"Example Franchise"_s},
                                  {u"message"_s, u"example"_s},
                                  {u"wikipedia_page_id"_s, 42}});
    QTRY_COMPARE(finished.size(), 1);
    QVERIFY(finished[0][0].toBool());
    QCOMPARE(network.calls.size(), 2);
    const auto body = QJsonDocument::fromJson(network.calls[1].body).object();
    QCOMPARE(body.value(u"type"_s).toString(), u"Universe request"_s);
    QCOMPARE(body.value(u"area"_s).toString(), u"Universes"_s);
    QCOMPARE(body.value(u"wikipedia_page_id"_s).toInt(), 42);
    QCOMPARE(body.value(u"wikipedia_url"_s).toString(), u"https://en.wikipedia.org/wiki/Example_Franchise"_s);
}

QTEST_GUILESS_MAIN(FeedbackNativeTransportTest)
#include "tst_feedback_native_transport.moc"
