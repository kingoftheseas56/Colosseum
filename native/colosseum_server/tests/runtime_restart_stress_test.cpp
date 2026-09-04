#include "runtime/ColosseumServerRuntime.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QTemporaryDir>
#include <QTcpSocket>

#include <cstdio>
#include <cstdlib>

namespace {

[[noreturn]] void fail(const char *message)
{
    std::fprintf(stderr, "FAIL:%s\n", message);
    std::fflush(stderr);
    std::exit(1);
}

void require(bool condition, const char *message)
{
    if (!condition)
        fail(message);
}

void heartbeat(const QUrl &url)
{
    QTcpSocket socket;
    socket.connectToHost(url.host(), static_cast<quint16>(url.port()));
    require(socket.waitForConnected(3000), "restart stress heartbeat connection failed");
    const QByteArray request = QByteArrayLiteral(
        "GET /heartbeat HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    require(socket.write(request) == request.size(), "restart stress heartbeat write was partial");
    require(socket.waitForBytesWritten(3000), "restart stress heartbeat write failed");
    QByteArray response;
    while (socket.state() != QAbstractSocket::UnconnectedState) {
        if (socket.waitForReadyRead(100))
            response += socket.readAll();
        QCoreApplication::processEvents();
    }
    response += socket.readAll();
    require(response.startsWith("HTTP/1.1 200 "),
            "every restarted runtime generation must answer heartbeat");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir appDirectory;
    require(appDirectory.isValid(), "restart stress temporary directory must be valid");

    colosseum::server::runtime::ColosseumServerRuntimeOptions options;
    options.appPath = appDirectory.path();
    options.settingsDirectory = appDirectory.path();
    options.httpPort = 0;

    colosseum::server::runtime::ColosseumServerRuntime runtime(options);
    for (int generation = 0; generation < 50; ++generation) {
        require(runtime.start(), "runtime restart stress generation must start");
        require(runtime.isRunning(), "runtime restart stress generation must be running");
        const QUrl url = runtime.httpUrl();
        require(url.port() > 0, "runtime restart stress must publish a port");
        heartbeat(url);
        runtime.stop();
        require(!runtime.isRunning(), "runtime restart stress generation must stop");

        QTcpSocket probe;
        probe.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(url.port()));
        require(!probe.waitForConnected(100),
                "stopped runtime must release its loopback listener before the next generation");
    }
    return 0;
}
