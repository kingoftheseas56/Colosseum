#include "runtime/ColosseumServerRuntime.h"

#include <QCoreApplication>
#include <QSslConfiguration>
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
    return 0;
}
