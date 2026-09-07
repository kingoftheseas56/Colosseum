#include "server1/Runtime.h"

#include <QCoreApplication>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    server1::Runtime runtime;

    if (runtime.initialized()) return 10;
    if (runtime.streamingReady()) return 11;
    if (!runtime.capabilities().empty()) return 12;

    if (!runtime.initialize()) return 20;
    if (!runtime.initialized()) return 21;
    if (runtime.streamingReady()) return 22;
    if (!runtime.capabilities().empty()) return 23;

    runtime.shutdown();
    if (runtime.initialized()) return 30;
    if (runtime.streamingReady()) return 31;
    if (!runtime.capabilities().empty()) return 32;

    return 0;
}
