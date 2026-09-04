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

const QByteArray kTlsHandshakeCertificate = QByteArrayLiteral(R"(-----BEGIN CERTIFICATE-----
MIIDCTCCAfGgAwIBAgIUCdhc3pvvtkYfoJtgg/+cLNiVqK0wDQYJKoZIhvcNAQEL
BQAwFDESMBAGA1UEAwwJMTI3LjAuMC4xMB4XDTI2MDkwNDA5NDE1NVoXDTM2MDkw
MTA5NDE1NVowFDESMBAGA1UEAwwJMTI3LjAuMC4xMIIBIjANBgkqhkiG9w0BAQEF
AAOCAQ8AMIIBCgKCAQEAwh9gzP2b9BniEc1j5hfOBQwpxs+Qtsu1SdVe1xzk3auy
zKzVDcU47/W3VmfJIV9tXDd7LhLo7LQtgTZRT8JIxhHweHTgazuuHpx3XnIhwA0N
2XcGh58Wcao+4VmKXktNevrHu4Nr5BafMc5LgY3Kz68rhgZtLdNoL9rYsl9WvMSZ
9+piU/8cjT1Gxb10FGOlV31c7kFXyj7T2eH1neV6WLvUDLQU1BOigYP0VEXw5IpO
GPuAXJcCkdbMLcurL9/rkKkF9B/skftffoP+SbqNbPZNHBDrleo8sjCtor5ZvptT
Cl4RvZ/BpCFf4bb9FFuRZmdrH1Rb63J6ETsMgNsbRQIDAQABo1MwUTAdBgNVHQ4E
FgQUt2pmjQkDxmXugUU7kArHkuy14zMwHwYDVR0jBBgwFoAUt2pmjQkDxmXugUU7
kArHkuy14zMwDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEAUOij
bXRie1367tNrAlH1d3BFg66U+XZRRdhgRxhLkPYHhTn8tQaZhm090EDCGBYGVWEA
1BCf93G4HHdsSGG/lrYVlpj2aDwZEyiFjmuCyQLBuYl3/ZpkDnXRe6Zsgs7ruWhI
ah9lcf0zvkkgclF7e9LJZUqt8xtVOvw9eSL3NNyFOnEI+QHplkv3QXjOuD52Tna4
tgwKf7LA/oBaCi7bmBXu+pyrTscDb2+GIFlRQlxDPQWwVCuzvTRzigpBIl9/OqVz
RIvq1Abaj+WvjD/2/XRNP6Xh1IcbLFS3Ccf5DS36xSwiE/wS6gXBFAHX4zr039dC
J1Rm4IBmqlNtnvOAWg==
-----END CERTIFICATE-----
)");

const QByteArray kTlsHandshakePrivateKey = QByteArrayLiteral(R"(-----BEGIN PRIVATE KEY-----
MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQDCH2DM/Zv0GeIR
zWPmF84FDCnGz5C2y7VJ1V7XHOTdq7LMrNUNxTjv9bdWZ8khX21cN3suEujstC2B
NlFPwkjGEfB4dOBrO64enHdeciHADQ3ZdwaHnxZxqj7hWYpeS016+se7g2vkFp8x
zkuBjcrPryuGBm0t02gv2tiyX1a8xJn36mJT/xyNPUbFvXQUY6VXfVzuQVfKPtPZ
4fWd5XpYu9QMtBTUE6KBg/RURfDkik4Y+4BclwKR1swty6sv3+uQqQX0H+yR+19+
g/5Juo1s9k0cEOuV6jyyMK2ivlm+m1MKXhG9n8GkIV/htv0UW5FmZ2sfVFvrcnoR
OwyA2xtFAgMBAAECggEAP95oEHn+pj0f2uCbVjiN0a5TTnS7ddEnOAAqYJdCm8Zi
oSHaRVoW5iiPpi+mhsLpbdZZQmr5VOvhLuqqrRXMsNJ1LoSx08ZCfxLW2W2uugvx
exPEw7ltfn9XifhWZLjc6LH7JjBSvqxMu2vW/uyihltpdALISDQOyvPzqRhiJt/r
Cl1lCFMshIlrlaCMQIThPVeDFMq3DyLubn9VGByhw3/OBtJnFOhSpGR2cQc5ZTyf
lG+y6mkhZtffdsGthO1GPMxH1i8Yn49n1Xv8SuGcS2mEJp3IJUZL7y+Kgy941F6D
5D38TcpNwVve69Au8mRZN0AlLl+kC+pKzgBt88Fy7wKBgQD4F2URbYRX9P/mYSYz
prj9iAjje19yFVcB2+/s0I5Gwd1hQNbNDWh8MzLBA5+a1hQK62ODuNJH0r6siAoj
6GL3BicP/0LbVctGSvsj7a6AXWFySIJTdUqzJ1JYdQPLDDJClyw5/3bsQppV2ODY
kUxi6P1rHEnpNrndgc3EHcvSxwKBgQDIT48KcMkOjTwWQTifppNh5ZCAEeqekAwv
UPpzbGY3OggJkDG37njc/CcOH+lMOf+BHC9zmz/a50Fpk2ZC87TibD0UwQwGc/tw
e6fwDt2G1Jz5jP5q+c0JOBmS774nkxe6ldQAkysEe6hEU31DJFrnJCKBP9X8Dsyn
1eq3RINVkwKBgQCgDyfrqIab4LtAvSjSZhwvphZl/XDEv7PUxTrzxFImoGjdl+F9
hcsFZlq2YEoWsUtZCCi6EQHJyNOvqE0ygXln+hY1ofBWZfGxtip1MaFFu/lkrBc5
FRFOqG3eGBCMbZ/3imTEPmdRYl8ER9o4nvVzUvI8qpGc3uvnVxmUD80yfQKBgQCC
Q1K3LG9jmi84HcPv9sijgkF9N6mG4hA6eQPWKekzAvcVGQNsJJXOx9+yDMiPvKvO
z4CAQra86WSdfrCi24+HK3JxW7UxQR2Dobato00mkH9gvfL5qGdRFn1zE5tqavqk
aSkMEqiH6s6bWFv+XNcMt3AE83l5yDoI71ELS2/JJwKBgALcIcCsuhKJXedLBQPO
X+EM98iCbqfPbO5nTyAn2nY8LFkH42duA0vnqDJI23OIghfBmVzuOnAR1aCKSloc
CQ5ywXxIovWiovP306ilJLg4PWhiO4Ad2AgR/3uMiF3jyVf21SXPnXrmSgyHq9Ce
pb/GZzVyGapHLmnpduvFi+dl
-----END PRIVATE KEY-----
)");

QSslConfiguration testTlsConfiguration()
{
    const QSslCertificate certificate(kTlsHandshakeCertificate, QSsl::Pem);
    const QSslKey key(kTlsHandshakePrivateKey, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey);
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
    socket.ignoreSslErrors();
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

    const QByteArray initialRootRequest = QByteArrayLiteral(
        "GET / HTTP/1.1\r\nHost: 127.0.0.1:")
        + QByteArray::number(runtime.httpUrl().port())
        + QByteArrayLiteral("\r\nConnection: close\r\n\r\n");
    const QByteArray initialRoot = exchange(runtime.httpUrl(), initialRootRequest);
    require(initialRoot.startsWith("HTTP/1.1 307 "),
            "native runtime root must preserve the server.js redirect status");
    require(initialRoot.contains(
                QByteArrayLiteral("streamingServer=http%3A%2F%2F127.0.0.1%3A")
                + QByteArray::number(runtime.httpUrl().port())),
            "native runtime root must use the current HTTP listener port");

    const QByteArray initialSettings = exchange(runtime.httpUrl(),
        "GET /settings HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    require(initialSettings.startsWith("HTTP/1.1 200 "),
            "native runtime settings must be served by the mounted generation");

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
    const QByteArray restartedRootRequest = QByteArrayLiteral(
        "GET / HTTP/1.1\r\nHost: 127.0.0.1:")
        + QByteArray::number(runtime.httpUrl().port())
        + QByteArrayLiteral("\r\nConnection: close\r\n\r\n");
    const QByteArray restartedRoot = exchange(runtime.httpUrl(), restartedRootRequest);
    require(restartedRoot.startsWith("HTTP/1.1 307 "),
            "restarted native runtime root must preserve the redirect route");
    require(restartedRoot.contains(
                QByteArrayLiteral("streamingServer=http%3A%2F%2F127.0.0.1%3A")
                + QByteArray::number(runtime.httpUrl().port())),
            "restarted native runtime root must use the current HTTP listener port");
    const QByteArray restartedNetworkInfo = exchange(runtime.httpUrl(),
        "GET /network-info HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    require(restartedNetworkInfo.startsWith("HTTP/1.1 200 "),
            "restarted native runtime must retain network service ownership");
    const QByteArray restartedSettings = exchange(runtime.httpUrl(),
        "GET /settings HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    require(restartedSettings.startsWith("HTTP/1.1 200 "),
            "restarted native runtime must retain settings service ownership");
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
    const QByteArray tlsRoot = tlsExchange(tlsRuntime.httpsUrl(),
        "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    require(tlsRoot.contains("streamingServer=https%3A%2F%2F127.0.0.1"),
            "native TLS root must advertise an HTTPS streaming server URL");
    tlsRuntime.stop();
    require(!tlsRuntime.isRunning(), "native TLS runtime must stop cleanly");
    return 0;
}
