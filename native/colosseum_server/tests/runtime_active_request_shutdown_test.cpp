#include "runtime/ColosseumServerRuntime.h"

#include <QCoreApplication>
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

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir appDirectory;
    require(appDirectory.isValid(), "active request temporary directory must be valid");

    colosseum::server::runtime::ColosseumServerRuntimeOptions options;
    options.appPath = appDirectory.path();
    options.settingsDirectory = appDirectory.path();
    options.httpPort = 0;

    {
        colosseum::server::runtime::ColosseumServerRuntime runtime(options);
        require(runtime.start(), "active request runtime must start");

        QTcpSocket active;
        active.connectToHost(runtime.httpUrl().host(),
                             static_cast<quint16>(runtime.httpUrl().port()));
        require(active.waitForConnected(3000), "active request client failed to connect");
        const QByteArray incompleteRequest = QByteArrayLiteral(
            "GET /heartbeat HTTP/1.1\r\nHost: 127.0.0.1\r\n");
        require(active.write(incompleteRequest) == incompleteRequest.size(),
                "active request write was partial");
        require(active.waitForBytesWritten(3000), "active request write failed");

        runtime.stop();
        require(!runtime.isRunning(), "runtime must stop with an active HTTP client");
        require(active.waitForDisconnected(3000),
                "active request client must observe runtime shutdown");
    }
    return 0;
}
