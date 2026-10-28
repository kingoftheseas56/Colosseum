#include "bootstrap/ApplicationShutdown.h"
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QTimer>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.loadData("import QtQml\nQtObject { property int requests: 0; function requestApplicationQuit() { requests++ } }");
    if (engine.rootObjects().isEmpty()) return 2;
    bool deadlineReached = false;
    QTimer::singleShot(30, &engine, [&] { deadlineReached = true; app.quit(); });
    Colosseum::Bootstrap::requestApplicationShutdown(app, engine);
    app.exec();
    if (!deadlineReached || engine.rootObjects().constFirst()->property("requests").toInt() != 1) {
        std::fputs("Shutdown bypassed the shell gate instead of waiting for it.\n", stderr);
        return 1;
    }

    QQmlApplicationEngine emptyEngine;
    deadlineReached = false;
    QTimer::singleShot(100, &emptyEngine, [&] { deadlineReached = true; app.quit(); });
    Colosseum::Bootstrap::requestApplicationShutdown(app, emptyEngine);
    app.exec();
    if (deadlineReached) return 3;

    QQmlApplicationEngine missingGate;
    missingGate.loadData("import QtQml\nQtObject {}");
    if (missingGate.rootObjects().isEmpty()) return 4;
    deadlineReached = false;
    QTimer::singleShot(30, &missingGate, [&] { deadlineReached = true; app.quit(); });
    Colosseum::Bootstrap::requestApplicationShutdown(app, missingGate);
    app.exec();
    if (!deadlineReached) return 5;
    std::puts("PASS: shell owns quit, empty startup can exit, missing gate stays open.");
    return 0;
}
