#include "work/ForegroundPriorityGovernor.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void spin(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    work::ForegroundPriorityGovernor governor(80);

    require(governor.pressure() == 0, "starts Normal");
    governor.noteUserInteraction();
    require(governor.pressure() == 1, "input immediately enters LatencySensitive");
    spin(50);
    governor.noteUserInteraction();
    spin(50);
    require(governor.pressure() == 1, "second input extends the lease");
    governor.setImmersiveSurfaceOpen(true);
    require(governor.pressure() == 2, "immersive dominates interaction");
    spin(100);
    require(governor.pressure() == 2, "interaction expiry cannot release immersive pressure");

    governor.noteUserInteraction();
    governor.setImmersiveSurfaceOpen(false);
    require(governor.pressure() == 1, "releasing immersive falls back to active interaction");
    spin(250);
    require(governor.pressure() == 0, "pressure decays to Normal after final reason clears");

    std::cout << "FOREGROUND_PRIORITY_GOVERNOR_OK\n";
    return 0;
}
