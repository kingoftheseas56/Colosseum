#include "runtime/ColosseumServerRuntime.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QTemporaryDir>
#include <QTcpServer>

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

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir appDirectory;
    require(appDirectory.isValid(), "lifecycle test temporary directory must be valid");

    QTcpServer occupant;
    require(occupant.listen(QHostAddress::LocalHost, 0),
            "lifecycle test must reserve a loopback port");
    const quint16 preferredPort = occupant.serverPort();

    colosseum::server::runtime::ColosseumServerRuntimeOptions options;
    options.appPath = appDirectory.path();
    options.settingsDirectory = appDirectory.path();
    options.httpPort = preferredPort;

    colosseum::server::runtime::ColosseumServerRuntime runtime(options);
    require(runtime.start(),
            qPrintable(QStringLiteral("runtime must advance: %1").arg(runtime.lastError())));
    require(runtime.httpUrl().port() > preferredPort,
            "runtime must publish a port after the occupied preferred port");
    require(runtime.httpUrl().port() != preferredPort,
            "runtime must not steal the occupied preferred port");
    runtime.stop();
    return 0;
}
