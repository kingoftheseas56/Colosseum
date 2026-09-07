#include "server1/Runtime.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {

void printState(QTextStream& out, char const* phase, server1::Runtime const& runtime)
{
    out << "{\"phase\":\"" << phase
        << "\",\"initialized\":" << (runtime.initialized() ? "true" : "false")
        << ",\"streamingReady\":" << (runtime.streamingReady() ? "true" : "false")
        << ",\"capabilities\":[]}" << Qt::endl;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    server1::Runtime runtime;
    if (!runtime.initialize())
        return 2;

    printState(out, "initialized", runtime);
    runtime.shutdown();
    printState(out, "shutdown", runtime);

    return runtime.initialized() || runtime.streamingReady() || !runtime.capabilities().empty()
        ? 3
        : 0;
}
