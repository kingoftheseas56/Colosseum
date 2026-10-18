#pragma once

#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QTimer>

namespace Colosseum::Bootstrap {
inline void requestApplicationShutdown(QCoreApplication &app, QQmlApplicationEngine &engine)
{
    // Keep the event loop alive for the shell's retained-browser drain. Queue
    // the request so the updater's state-change call stack can finish first.
    QTimer::singleShot(0, &engine, [&app, &engine] {
        const auto roots = engine.rootObjects();
        if (roots.isEmpty()) {
            app.quit();
            return;
        }
        if (!QMetaObject::invokeMethod(roots.constFirst(), "requestApplicationQuit", Qt::DirectConnection))
            qCritical("Shell quit gate unavailable; keeping application open");
    });
}
}
