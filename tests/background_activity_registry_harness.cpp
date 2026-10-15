// tests/background_activity_registry_harness.cpp
#include "work/BackgroundActivityRegistry.h"

#include <QCoreApplication>
#include <QVariantMap>

#include <iostream>

namespace {
void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    work::BackgroundActivityRegistry registry;

    int changes = 0;
    QObject::connect(&registry, &work::BackgroundActivityRegistry::activitiesChanged,
                     [&] { ++changes; });
    QString pausedId;
    QObject::connect(&registry, &work::BackgroundActivityRegistry::pauseRequested,
                     [&](const QString &id) { pausedId = id; });

    QVariantMap guided{{QStringLiteral("title"), QStringLiteral("Analyzing One Piece pages")},
                       {QStringLiteral("stage"), QStringLiteral("Detecting panels")},
                       {QStringLiteral("progress"), 0.4},
                       {QStringLiteral("paused"), false},
                       {QStringLiteral("canPause"), true}};
    registry.publish(QStringLiteral("guided:onepiece"), guided);
    require(registry.activities().size() == 1, "publish adds a row");
    require(changes == 1, "publish notifies");

    guided[QStringLiteral("progress")] = 0.6;
    registry.publish(QStringLiteral("guided:onepiece"), guided);
    require(registry.activities().size() == 1, "re-publish updates in place, no duplicate");
    require(registry.activities().first().toMap().value(QStringLiteral("progress")).toDouble() == 0.6,
            "re-publish carries new state");
    require(registry.activities().first().toMap().value(QStringLiteral("id")).toString()
                == QStringLiteral("guided:onepiece"),
            "id injected into the row");

    registry.publish(QStringLiteral("align:dune"),
                     QVariantMap{{QStringLiteral("title"), QStringLiteral("Syncing Dune")},
                                 {QStringLiteral("stage"), QStringLiteral("Aligning words")},
                                 {QStringLiteral("progress"), 0.1},
                                 {QStringLiteral("paused"), false},
                                 {QStringLiteral("canPause"), true}});
    require(registry.activities().size() == 2, "second domain coexists");
    require(registry.activities().at(0).toMap().value(QStringLiteral("id")).toString()
                == QStringLiteral("guided:onepiece"),
            "insertion order preserved");

    registry.requestPause(QStringLiteral("align:dune"));
    require(pausedId == QStringLiteral("align:dune"), "pause request reaches producer side");

    registry.remove(QStringLiteral("guided:onepiece"));
    require(registry.activities().size() == 1, "remove drops the row");
    registry.remove(QStringLiteral("ghost"));
    require(registry.activities().size() == 1, "removing unknown id is a no-op");

    std::cout << "BACKGROUND_ACTIVITY_REGISTRY_OK\n";
    return 0;
}
