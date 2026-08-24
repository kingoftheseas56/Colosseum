// tests/background_work_composition_harness.cpp
#include "work/BackgroundActivityRegistry.h"
#include "work/BackgroundWorkCoordinator.h"

#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantMap>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

namespace {
void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool rowPaused(const work::BackgroundActivityRegistry &registry,
               const QString &id)
{
    for (const QVariant &value : registry.activities()) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("id")).toString() == id)
            return row.value(QStringLiteral("paused")).toBool();
    }
    return false;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    work::BackgroundWorkCoordinator coordinator(1);
    work::BackgroundActivityRegistry registry;
    registry.setCoordinator(&coordinator);

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("BackgroundActivity"), &registry);
    const auto runQmlCommand = [&](const QString &method) {
        QQmlComponent component(&engine);
        const QString source = QStringLiteral(
            "import QtQml\nQtObject { Component.onCompleted: BackgroundActivity.%1(\"target\") }")
                                   .arg(method);
        component.setData(source.toUtf8(), QUrl());
        std::unique_ptr<QObject> object(component.create());
        require(object != nullptr, "QML command object created");
    };

    std::atomic_bool releaseBlocker{false};
    std::atomic_bool blockerStarted{false};
    coordinator.submit({QStringLiteral("blocker"), 100},
                       [&](work::WorkContext &context) {
        blockerStarted.store(true);
        while (!releaseBlocker.load()) {
            if (!context.checkpoint())
                return work::WorkResult::Cancelled;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return work::WorkResult::Completed;
    });

    for (int i = 0; i < 500 && !blockerStarted.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    require(blockerStarted.load(), "blocker started");

    coordinator.submit({QStringLiteral("target"), 50},
                       [](work::WorkContext &) {
        return work::WorkResult::Completed;
    });
    registry.publish(QStringLiteral("target"),
                     QVariantMap{{QStringLiteral("title"), QStringLiteral("Target")},
                                 {QStringLiteral("stage"), QStringLiteral("Queued")},
                                 {QStringLiteral("progress"), 0.0},
                                 {QStringLiteral("paused"), false},
                                 {QStringLiteral("canPause"), true}});

    runQmlCommand(QStringLiteral("requestPause"));
    require(coordinator.status(QStringLiteral("target")) == work::Status::Paused,
            "registry pause reaches exact coordinator job");
    require(coordinator.isPaused(QStringLiteral("target")),
            "coordinator exposes authoritative pause token");
    require(rowPaused(registry, QStringLiteral("target")),
            "registry row reflects scheduler pause state");
    require(!coordinator.isPaused(QStringLiteral("blocker")),
            "pause is exact-id scoped");

    QVariantMap stale{{QStringLiteral("title"), QStringLiteral("Target")},
                      {QStringLiteral("stage"), QStringLiteral("Queued")},
                      {QStringLiteral("progress"), 0.2},
                      {QStringLiteral("paused"), false},
                      {QStringLiteral("canPause"), true}};
    registry.publish(QStringLiteral("target"), stale);
    require(rowPaused(registry, QStringLiteral("target")),
            "producer cannot overwrite authoritative paused state");

    runQmlCommand(QStringLiteral("requestResume"));
    require(coordinator.status(QStringLiteral("target")) == work::Status::Queued,
            "registry resume requeues exact coordinator job");
    require(!coordinator.isPaused(QStringLiteral("target")),
            "resume clears authoritative pause token");
    require(!rowPaused(registry, QStringLiteral("target")),
            "registry row reflects scheduler resume state");

    releaseBlocker.store(true);
    for (int i = 0; i < 500
         && coordinator.status(QStringLiteral("target")) != work::Status::Completed; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(coordinator.status(QStringLiteral("target")) == work::Status::Completed,
            "resumed target completes through shared scheduler");

    std::cout << "BACKGROUND_WORK_COMPOSITION_OK\n";
    return 0;
}
