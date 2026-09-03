#include "../NetworkAppServices.h"
#include "TestSupport.h"

#include <QCoreApplication>
#include <QJsonDocument>

using namespace colosseum::server::app;

class FakeYouTubeResolver final : public YouTubeResolver {
public:
    YouTubeResolution next;
    QString lastId;

    YouTubeResolution resolveAudioVideo(const QString &id) override
    {
        lastId = id;
        return next;
    }
};

static void testSuccess(TestState &t)
{
    FakeYouTubeResolver resolver;
    resolver.next.format = {
        {"itag", 22},
        {"url", "https://cdn.test/video"},
        {"qualityLabel", "720p"},
    };
    YouTubeService service(resolver);

    AppRequest redirectReq;
    redirectReq.path = "/yt/abc123";
    AppResponse redirect = service.handle(redirectReq);
    t.equal(redirect.status, 301, "YouTube media route redirects");
    t.equal(headerValue(redirect.headers, "location"), QByteArray("https://cdn.test/video"),
            "YouTube redirect uses chosen audio+video URL");
    t.require(redirect.body.isEmpty(), "YouTube redirect body is empty");
    t.equal(resolver.lastId, QString("abc123"), "YouTube id is forwarded unchanged");

    AppRequest jsonReq;
    jsonReq.path = "/yt/abc123.json";
    AppResponse json = service.handle(jsonReq);
    t.equal(json.status, 200, "YouTube JSON route status");
    t.equal(headerValue(json.headers, "content-type"), QByteArray("application/json"),
            "YouTube JSON content type");
    const QJsonObject body = QJsonDocument::fromJson(json.body).object();
    t.equal(body.value("itag").toInt(), 22, "YouTube JSON exposes chosen format");
    t.equal(body.value("url").toString(), QString("https://cdn.test/video"),
            "YouTube JSON includes direct URL");
}

static void testErrors(TestState &t)
{
    FakeYouTubeResolver resolver;
    resolver.next.error = "Video unavailable";
    YouTubeService service(resolver);

    AppRequest jsonReq;
    jsonReq.path = "/yt/dead.json";
    AppResponse json = service.handle(jsonReq);
    t.equal(json.status, 404, "module 564 quirk: JSON resolver error status is overwritten to 404");
    t.equal(QJsonDocument::fromJson(json.body).object().value("err").toString(),
            QString("Video unavailable"), "resolver error text is preserved");

    AppRequest redirectReq;
    redirectReq.path = "/yt/dead";
    AppResponse redirect = service.handle(redirectReq);
    t.equal(redirect.status, 403, "resolver error is 403 on redirect route");
    t.require(redirect.body.isEmpty(), "redirect error body stays empty");

    resolver.next = {};
    AppResponse missing = service.handle(jsonReq);
    t.equal(missing.status, 404, "format without URL is 404");
    t.equal(missing.body, QByteArray("{}"), "missing JSON format body is empty object");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    TestState t;
    testSuccess(t);
    testErrors(t);
    return finishTests(t, "youtube");
}
