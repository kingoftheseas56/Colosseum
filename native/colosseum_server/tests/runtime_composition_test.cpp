#include "runtime/ColosseumServerRuntime.h"

#include <QCoreApplication>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QTcpSocket>

#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void fail(const char *message)
{
    std::fprintf(stderr, "FAIL:%s\n", message);
    std::abort();
}

void require(bool condition, const char *message)
{
    if (!condition)
        fail(message);
}

const QByteArray kTlsCertificate = QByteArrayLiteral(R"(-----BEGIN CERTIFICATE-----
MIIB0jCCATugAwIBAgIUYEA7eAzbhoTBV2S/oAiqZ/t8L7gwDQYJKoZIhvcNAQEL
BQAwFDESMBAGA1UEAwwJMTI3LjAuMC4xMB4XDTI2MDkwNDA2MzY1OFoXDTM2MDkw
MTA2MzY1OFowFDESMBAGA1UEAwwJMTI3LjAuMC4xMIGfMA0GCSqGSIb3DQEBAQUA
A4GNADCBiQKBgQCyWj7gCEHGQSQkt5Fgen1+jK9lwp5lkH4Kvm4LGvVdYHdNKTNd
y12GeGZr5be0yJSL6nxJuYsTD635j9/Vne+EMzZxzYHurBwG86n9XX0eDtLWIPqI
Z7cgQ1ZMKT+OCn4PMGu2GIW63Wg5urcR2Hh8pqE5QUY+c6iRpvhuYezzqQIDAQAB
oyEwHzAdBgNVHQ4EFgQUCUVRVVOBJ57MM6Wgr62onk7+01QwDQYJKoZIhvcNAQEL
BQADgYEASlyGOUvrjn8iRbUxh1NvoMVTlSVSmcWbSZ9cniMU+eCG2V15IOdlrI9Q
VJLYxwABYqHZctv0VwzIFTEZvd4+onT2x+5AXg+4JDXwwCqP3nxRonjfbZglvDH/
o8CENsod65cR6TUfIvIt4yASYTFvNWT1qkIyU/XAJGvGM5aqX80=
-----END CERTIFICATE-----
)");

const QByteArray kTlsPrivateKey = QByteArrayLiteral(R"(-----BEGIN RSA PRIVATE KEY-----
MIICXAIBAAKBgQCyWj7gCEHGQSQkt5Fgen1+jK9lwp5lkH4Kvm4LGvVdYHdNKTNd
y12GeGZr5be0yJSL6nxJuYsTD635j9/Vne+EMzZxzYHurBwG86n9XX0eDtLWIPqI
Z7cgQ1ZMKT+OCn4PMGu2GIW63Wg5urcR2Hh8pqE5QUY+c6iRpvhuYezzqQIDAQAB
AoGARSJuVPFebbc6h3EQzVEt7CwkoVF7jOshsJB4n51nlzaZiDN8UdNPAZ0SNqjp
OQ63ZjUS0JE3s7/UNHTs0yVRfktjXtvIPtNc/TT31DNrHYGzog6VHBEbSes2CB05
wbn6KlegOU5G9eZ7ZnghescT2Z92z8jyXXv4FvgX/+4W6bECQQDhvqFJE6w73R6W
AJWyfdkYWkigF0pjD9uXd8/t7FHfgpJLN2scvPxVAnpm83F+x9JVvBJhP4JfneCT
I+j5MyNvAkEAykGTqOtFQ5XtoGcBQFjyXLjtk64Mao9gBAZG1I9D3plAp/1sahss
cJot5gEbpha4OriT/jIfYAxGQf5xHOBuZwJAQB68gxxCZLKW+HZsDsnuOxuR218i
MucTMX/HoMXqL3lQAmtYUk5fwem1SL7HMwKg/NcxxUubxXr7ie++QeJDowJBAIoB
gO2ry1E4hjIC1tm/V3BpRsKT6ijzt8JHPiFfuCG1VGbMByPHcuVKbrMYYnNL4V1A
AMtcDAiPl4kEQs4/XVUCQB1u98SyZPWkYA07mfP282n9iNOAWwJCpvoBwne1T4t5
ufNYa+jdzvn8wJ6/v4yHRhq03qmAcHoX+FYVZZaysNc=
-----END RSA PRIVATE KEY-----
)");

QSslConfiguration testTlsConfiguration()
{
    const QSslCertificate certificate(kTlsCertificate, QSsl::Pem);
    const QSslKey key(kTlsPrivateKey, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey);
    require(!certificate.isNull(), "runtime TLS test certificate must parse");
    require(!key.isNull(), "runtime TLS test private key must parse");
    QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();
    configuration.setLocalCertificate(certificate);
    configuration.setPrivateKey(key);
    configuration.setPeerVerifyMode(QSslSocket::VerifyNone);
    return configuration;
}

QByteArray exchange(const QUrl &url, const QByteArray &request)
{
    QTcpSocket socket;
    socket.connectToHost(url.host(), static_cast<quint16>(url.port()));
    require(socket.waitForConnected(3000), "runtime test client failed to connect");
    require(socket.write(request) == request.size(), "runtime test request write was partial");
    require(socket.waitForBytesWritten(3000), "runtime test request was not written");

    QByteArray wire;
    for (int i = 0; i < 100 && socket.state() != QAbstractSocket::UnconnectedState; ++i) {
        if (socket.waitForReadyRead(100))
            wire += socket.readAll();
        QCoreApplication::processEvents();
    }
    wire += socket.readAll();
    return wire;
}

QByteArray responseBody(const QByteArray &wire)
{
    const qsizetype separator = wire.indexOf("\r\n\r\n");
    require(separator >= 0, "runtime response must contain headers");
    return wire.mid(separator + 4);
}

QByteArray tlsExchange(const QUrl &url, const QByteArray &request)
{
    QSslSocket socket;
    socket.setPeerVerifyMode(QSslSocket::VerifyNone);
    socket.connectToHostEncrypted(url.host(), static_cast<quint16>(url.port()));
    require(socket.waitForEncrypted(3000), "runtime TLS client failed to handshake");
    require(socket.write(request) == request.size(), "runtime TLS request write was partial");
    require(socket.waitForBytesWritten(3000), "runtime TLS request was not written");

    QByteArray wire;
    for (int i = 0; i < 100 && socket.state() != QAbstractSocket::UnconnectedState; ++i) {
        if (socket.waitForReadyRead(100))
            wire += socket.readAll();
        QCoreApplication::processEvents();
    }
    wire += socket.readAll();
    return wire;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir appDirectory;
    require(appDirectory.isValid(), "runtime test temporary directory must be valid");

    colosseum::server::runtime::ColosseumServerRuntimeOptions options;
    options.appPath = appDirectory.path();
    options.settingsDirectory = appDirectory.path();
    options.httpPort = 0;
    options.enableTls = false;

    colosseum::server::runtime::ColosseumServerRuntime runtime(options);
    require(runtime.start(), "native runtime must start its engine and HTTP listener");
    require(runtime.isRunning(), "native runtime must report running after start");
    require(runtime.httpUrl().scheme() == QStringLiteral("http"),
            "native runtime must publish an HTTP URL");
    require(runtime.httpUrl().port() > 0, "native runtime must publish its bound port");

    const QByteArray heartbeat = exchange(runtime.httpUrl(),
        "GET /heartbeat HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    require(heartbeat.startsWith("HTTP/1.1 200 "),
            "native runtime must expose the heartbeat route");
    require(responseBody(heartbeat) == "{\"success\":true}",
            "native runtime heartbeat must preserve server.js response bytes");

    const QByteArray stats = exchange(runtime.httpUrl(),
        "GET /stats.json?sys=1 HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    require(stats.startsWith("HTTP/1.1 200 "),
            "native runtime must expose the torrent stats route");
    require(responseBody(stats).contains("\"sys\":"),
            "native runtime stats must include system stats when requested");

    runtime.stop();
    require(!runtime.isRunning(), "native runtime must stop both listeners and the engine");

    require(runtime.start(), "native runtime must restart after a clean stop");
    const QByteArray restartedHeartbeat = exchange(runtime.httpUrl(),
        "GET /heartbeat HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    require(restartedHeartbeat.startsWith("HTTP/1.1 200 "),
            "restarted native runtime must retain its route graph");
    runtime.stop();
    require(!runtime.isRunning(), "restarted native runtime must stop cleanly");

    options.enableTls = true;
    options.tlsConfiguration = QSslConfiguration{};
    colosseum::server::runtime::ColosseumServerRuntime invalidTlsRuntime(options);
    require(!invalidTlsRuntime.start(), "native runtime must reject TLS without a certificate");
    require(!invalidTlsRuntime.isRunning(), "invalid TLS runtime must remain stopped");
    require(invalidTlsRuntime.lastError().contains(QStringLiteral("certificate"), Qt::CaseInsensitive),
            "invalid TLS runtime must explain the missing certificate");

    options.httpPort = 0;
    options.httpsPort = 0;
    options.tlsConfiguration = testTlsConfiguration();
    colosseum::server::runtime::ColosseumServerRuntime tlsRuntime(options);
    require(tlsRuntime.start(), "native runtime must start a valid TLS listener");
    require(tlsRuntime.httpsUrl().scheme() == QStringLiteral("https"),
            "native runtime must publish an HTTPS URL");
    require(tlsRuntime.httpsUrl().port() > 0,
            "native runtime must publish its HTTPS port");
    const QByteArray tlsHeartbeat = tlsExchange(tlsRuntime.httpsUrl(),
        "GET /heartbeat HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    require(tlsHeartbeat.startsWith("HTTP/1.1 200 "),
            "native TLS listener must serve the shared heartbeat route");
    require(responseBody(tlsHeartbeat) == "{\"success\":true}",
            "native TLS heartbeat must preserve route body bytes");
    tlsRuntime.stop();
    require(!tlsRuntime.isRunning(), "native TLS runtime must stop cleanly");
    return 0;
}
