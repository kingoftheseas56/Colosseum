#include "../NetworkAppServices.h"
#include "TestSupport.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace colosseum::server::app;

class FakeCertificateTransport final : public CertificateTransport {
public:
    QJsonObject next;
    QJsonObject lastPayload;
    QJsonObject request(const QUrl &, const QJsonObject &payload, QString *error) override
    {
        lastPayload = payload;
        if (next.isEmpty()) {
            if (error)
                *error = "fixture transport failure";
            return {};
        }
        return next;
    }
};

class FakeInterfaces final : public NetworkInterfaceProvider {
public:
    QStringList ipv4Interfaces(QString *error) override
    {
        Q_UNUSED(error);
        return {"192.0.2.5", "198.51.100.9"};
    }
};

class FakeProfiler final : public HardwareAccelerationProfiler {
public:
    QJsonValue next = QJsonArray{QString("d3d11va")};
    int lastPort = 0;
    QJsonValue profile(int port) override { lastPort = port; return next; }
};
static QJsonObject certificateReply(const QString &notBefore, const QString &notAfter)
{
    const QJsonObject contents{
        {"PrivateKey", QString::fromLatin1(QByteArray("KEY").toBase64())},
        {"Certificate", QString::fromLatin1(QByteArray("CERT").toBase64())},
        {"NotBefore", notBefore},
        {"NotAfter", notAfter},
    };
    const QJsonObject certificate{
        {"commonName", "*.strem.io"},
        {"contents", contents},
    };
    return {{"result", QJsonObject{{"certificate", QString::fromUtf8(QJsonDocument(certificate).toJson(QJsonDocument::Compact))}}}};
}

static void testCertificateLifecycle(TestState &t)
{
    FakeCertificateTransport transport;
    transport.next = certificateReply("2020-01-01T00:00:00Z", "2035-01-01T00:00:00Z");
    QTemporaryDir temp;
    HttpsCertificateService service(transport, temp.path(), QUrl("https://api.test/certificateGet"));
    QString error;
    const auto cert = service.requestNewCertificate("192.0.2.9", "auth-token", &error);
    t.require(cert.has_value(), "valid certificate response is accepted");
    t.equal(cert->domain, QString("192-0-2-9.strem.io"), "certificate domain follows module 804 rule");
    t.equal(cert->key, QByteArray("KEY"), "private key is base64 decoded");
    t.equal(transport.lastPayload.value("authKey").toString(), QString("auth-token"), "auth key reaches certificate API");

    FakeCertificateTransport offline;
    HttpsCertificateService reloaded(offline, temp.path(), QUrl("https://api.test/certificateGet"));
    const auto cached = reloaded.cachedCertificate(&error);
    t.require(cached.has_value(), "certificate cache reloads from httpsCert.json");
    t.equal(cached->cert, QByteArray("CERT"), "cached certificate bytes are preserved");
}
static void testSmallRoutes(TestState &t)
{
    FakeCertificateTransport certTransport;
    certTransport.next = certificateReply("2020-01-01T00:00:00Z", "2035-01-01T00:00:00Z");
    QTemporaryDir temp;
    HttpsCertificateService certificates(certTransport, temp.path(), QUrl("https://api.test/certificateGet"));
    FakeInterfaces interfaces;
    FakeProfiler profiler;
    NetworkRouteService routes(certificates, interfaces, profiler, 11470, 12470,
                               QUrl("https://app.strem.io/shell-v4.4/"));

    AppRequest heartbeatReq;
    heartbeatReq.path = "/heartbeat";
    const AppResponse heartbeat = routes.handle(heartbeatReq);
    t.equal(heartbeat.status, 200, "heartbeat status");
    t.equal(heartbeat.body, QByteArray("{\"success\":true}"), "heartbeat body is exact");

    AppRequest rootReq;
    rootReq.path = "/";
    rootReq.headers = {{"Host", "127.0.0.1:11470"}};
    const AppResponse root = routes.handle(rootReq);
    t.equal(root.status, 307, "root uses temporary redirect");
    t.equal(headerValue(root.headers, "location"),
            QByteArray("https://app.strem.io/shell-v4.4/?streamingServer=http%3A%2F%2F127.0.0.1%3A11470"),
            "root redirect encodes streaming server URL");

    AppRequest networkReq;
    networkReq.path = "/network-info";
    const QJsonObject network = QJsonDocument::fromJson(routes.handle(networkReq).body).object();
    t.equal(network.value("availableInterfaces").toArray().size(), qsizetype(2), "network-info exposes IPv4 interfaces");

    AppRequest deviceReq;
    deviceReq.path = "/device-info";
    const QJsonObject device = QJsonDocument::fromJson(routes.handle(deviceReq).body).object();
    t.equal(device.value("availableHardwareAccelerations").toArray().first().toString(), QString("d3d11va"),
            "device-info wraps profiler output");
    t.equal(profiler.lastPort, 11470, "device-info profiles the HTTP server port");

    AppRequest profilerReq;
    profilerReq.path = "/hwaccel-profiler";
    const AppResponse profile = routes.handle(profilerReq);
    t.equal(profile.status, 200, "hwaccel-profiler success status");
    t.require(!hasHeader(profile.headers, "content-type"), "hwaccel-profiler preserves missing content-type quirk");

    profiler.next = QJsonValue();
    const AppResponse noProfile = routes.handle(profilerReq);
    t.equal(noProfile.status, 500, "empty profiler result is 500");
    t.equal(noProfile.body, QByteArray("No viable hardware acceleration profiles detected"),
            "empty profiler message matches Stremio");

    AppRequest httpsReq;
    httpsReq.path = "/get-https";
    httpsReq.query = QUrlQuery("ipAddress=192.0.2.9&authKey=auth-token");
    const QJsonObject https = QJsonDocument::fromJson(routes.handle(httpsReq).body).object();
    t.equal(https.value("domain").toString(), QString("192-0-2-9.strem.io"), "get-https returns derived domain");
    t.equal(https.value("port").toInt(), 12470, "get-https reports TLS listener port");
}

static void testExpiredCertificate(TestState &t)
{
    FakeCertificateTransport transport;
    transport.next = certificateReply("2010-01-01T00:00:00Z", "2011-01-01T00:00:00Z");
    QTemporaryDir temp;
    HttpsCertificateService service(transport, temp.path(), QUrl("https://api.test/certificateGet"));
    QString error;
    const auto cert = service.requestNewCertificate("192.0.2.9", "auth-token", &error);
    t.require(!cert.has_value(), "expired certificate is rejected");
    t.equal(error, QString("Could not get a valid HTTPS certificate"), "expired certificate error text is exact");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    TestState t;
    testCertificateLifecycle(t);
    testSmallRoutes(t);
    testExpiredCertificate(t);
    return finishTests(t, "network");
}
