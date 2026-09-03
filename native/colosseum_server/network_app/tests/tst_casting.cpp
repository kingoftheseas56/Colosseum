#include "../NetworkAppServices.h"
#include "TestSupport.h"

#include <QCoreApplication>
#include <QJsonDocument>

using namespace colosseum::server::app;

class FakeCastSession final : public CastPlayerSession {
public:
    QString lastMethod;
    QJsonArray lastArgs;
    QJsonValue invoke(const QString &method, const QJsonArray &args,
                      QJsonObject &mediaStatus) override
    {
        lastMethod = method;
        lastArgs = args;
        mediaStatus["state"] = method == "pause" ? 4 : 3;
        return mediaStatus;
    }
};

class FakeCastFactory final : public CastSessionFactory {
public:
    FakeCastSession *last = nullptr;
    std::unique_ptr<CastPlayerSession> create(const CastDevice &) override
    {
        auto session = std::make_unique<FakeCastSession>();
        last = session.get();
        return session;
    }
};
class FakeTranscoder final : public CastTranscoder {
public:
    AppResponse transcode(const AppRequest &, bool fmp4) override
    {
        AppResponse response;
        response.status = 200;
        response.headers = {
            {"Content-Type", fmp4 ? "video/mp4" : "video/x-mkv"},
            {"Transfer-Encoding", "chunked"},
            {"Accept-Ranges", "none"},
            {"Connection", "Close"},
            {"transferMode.dlna.org", "Streaming"},
            {"contentFeatures.dlna.org", "DLNA.ORG_OP=01;DLNA.ORG_CI=1;DLNA.ORG_FLAGS=01300000000000000000000000000000"},
        };
        return response;
    }
};

static CastDevice fixtureDevice()
{
    CastDevice device;
    device.facility = "SSDP";
    device.id = "uuid-1";
    device.name = "Fixture TV";
    device.host = "192.0.2.10";
    device.location = "http://192.0.2.10/device.xml";
    device.type = "tv";
    device.icon = "tv";
    device.playerUIRoles = {"playpause", "seek", "dub", "subtitles", "volume"};
    device.usePlayerUI = true;
    device.onlyHtml5Formats = false;
    return device;
}
static void testDiscovery(TestState &t)
{
    CastDiscoveryRegistry registry;
    registry.collect(fixtureDevice());
    CastDevice duplicate = fixtureDevice();
    duplicate.name = "Should not replace";
    registry.collect(duplicate);
    t.equal(registry.devices().size(), qsizetype(1), "first discovery by id wins");
    t.equal(registry.devices().first().name, QString("Fixture TV"), "first device is preserved");

    const QByteArray xml = R"(<root><device><deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType><friendlyName>Living Room</friendlyName><UDN>uuid:abc</UDN></device></root>)";
    const auto ssdp = CastDiscoveryRegistry::fromSsdpDescription(
        QUrl("http://192.0.2.11/desc.xml"), xml);
    t.require(ssdp.has_value(), "SSDP MediaRenderer parses");
    t.equal(ssdp->id, QString("abc"), "SSDP strips uuid prefix");
    t.equal(ssdp->type, QString("tv"), "SSDP MediaRenderer maps to tv");

    QJsonObject mdns{{"PTR", "Kitchen._googlecast._tcp.local"},
                     {"A", "192.0.2.12"},
                     {"SRV", QJsonObject{{"target", "cast-123.local"}}},
                     {"TXT", QJsonObject{{"fn", "Kitchen Cast"}}}};
    const auto cast = CastDiscoveryRegistry::fromMdnsRecords(mdns);
    t.require(cast.has_value(), "mDNS Chromecast parses");
    t.equal(cast->id, QString("cast-123"), "mDNS id derives from SRV target");
    t.equal(cast->name, QString("Kitchen Cast"), "mDNS friendly name derives from TXT fn");
}

static void testRoutes(TestState &t)
{
    CastDiscoveryRegistry registry;
    registry.collect(fixtureDevice());
    FakeCastFactory factory;
    FakeTranscoder transcoder;
    CastingService service(registry, factory, transcoder);
    AppRequest listReq;
    listReq.path = "/casting/";
    const AppResponse list = service.handle(listReq);
    t.equal(list.status, 200, "casting list status");
    t.equal(QJsonDocument::fromJson(list.body).array().size(), qsizetype(1), "casting list exposes devices");

    AppRequest missingReq;
    missingReq.path = "/casting/missing";
    const AppResponse missing = service.handle(missingReq);
    t.equal(missing.status, 404, "unknown casting device is 404");
    t.equal(missing.body, QByteArray("Device not found"), "unknown device body matches Stremio");

    AppRequest playerReq;
    playerReq.path = "/casting/uuid-1/player";
    playerReq.query = QUrlQuery("source=https%3A%2F%2Fmedia.test%2Fmovie&time=3000&paused=false");
    const AppResponse player = service.handle(playerReq);
    t.equal(player.status, 200, "casting player request succeeds");
    t.equal(factory.last->lastMethod, QString("resume"), "paused=false wins final player method precedence");

    AppRequest convertReq;
    convertReq.method = "HEAD";
    convertReq.path = "/casting/convert.mp4";
    convertReq.query = QUrlQuery("video=https%3A%2F%2Fmedia.test%2Fmovie&fmp4=1");
    const AppResponse converted = service.handle(convertReq);
    t.equal(converted.status, 200, "convert alias delegates to transcoder");
    t.equal(headerValue(converted.headers, "content-type"), QByteArray("video/mp4"), "fmp4 conversion advertises video/mp4");
    t.equal(headerValue(converted.headers, "accept-ranges"), QByteArray("none"), "casting transcode disables ranges");

    AppRequest badConvert;
    badConvert.path = "/casting/transcode";
    const AppResponse bad = service.handle(badConvert);
    t.equal(bad.status, 400, "casting transcode requires video query");
    t.equal(bad.body, QByteArray("provide ?video"), "casting missing-video text matches oracle");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    TestState t;
    testDiscovery(t);
    testRoutes(t);
    return finishTests(t, "casting");
}