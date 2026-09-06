#include <QCoreApplication>

#include <iostream>

#include "server1/Runtime.h"

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    server1::Runtime runtime;
    runtime.initialize();

    std::cout << "server1 skeleton initialized; streaming-ready=NO\n";
    runtime.shutdown();
    std::cout << "server1 skeleton shutdown; streaming-ready=NO\n";
    return runtime.initialized() ? 1 : 0;
}
