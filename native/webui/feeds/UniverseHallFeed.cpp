#include "FeedRegistry.h"

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace {
bool valid(const QVariantMap &params)
{
    return params.isEmpty();
}

QVariantList universeItems(const QVariantList &extensions)
{
    QVariantList items;
    for (const QVariant &value : extensions) {
        const QVariantMap entry = value.toMap();
        if (!entry.value(QStringLiteral("enabled")).toBool()) continue;
        const QVariantMap manifest = entry.value(QStringLiteral("manifest")).toMap();
        bool universe = false;
        // HomeFeed.cpp:11-45 + UniverseHallPage.qml:13-24,140-238:
        // the Hall is the enabled installed universe roster in store order.
        for (const QVariant &resource : manifest.value(QStringLiteral("resources")).toList()) {
            if (resource.toString() == QLatin1String("universe")
                || resource.toMap().value(QStringLiteral("name")).toString() == QLatin1String("universe")) {
                universe = true;
                break;
            }
        }
        if (!universe) continue;
        const QString id = entry.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) continue;
        const QString name = manifest.value(QStringLiteral("name"), id).toString();
        QVariantMap item{{QStringLiteral("key"), QStringLiteral("Colosseum:universe:") + id},
                         {QStringLiteral("world"), QStringLiteral("Colosseum")},
                         {QStringLiteral("kind"), QStringLiteral("universe")},
                         {QStringLiteral("title"), name},
                         {QStringLiteral("ref"), QVariantMap{{QStringLiteral("extensionId"), id},
                                                               {QStringLiteral("name"), name}}}};

        const QString logo = manifest.value(QStringLiteral("logo")).toString();
        const QString banner = manifest.value(QStringLiteral("background")).toString();
        if (!logo.isEmpty()) item.insert(QStringLiteral("cover"), logo);
        if (!banner.isEmpty()) item.insert(QStringLiteral("backdrop"), banner);
        items.append(item);
    }
    return items;
}

QVariantMap hallSection(int index, const QString &state, const QVariantList &items = {})
{
    QVariantMap data{{QStringLiteral("schema"), QStringLiteral("universes.hall")},
                     {QStringLiteral("count"), items.size()}};
    return {{QStringLiteral("id"), QStringLiteral("universeHall.worlds")},
            {QStringLiteral("index"), index},
            {QStringLiteral("title"), QString()},
            {QStringLiteral("layout"), QStringLiteral("custom")},
            {QStringLiteral("state"), state},
            {QStringLiteral("items"), items},
            {QStringLiteral("data"), data}};
}

QVariantList initial(const QVariantMap &)
{
    return {hallSection(0, QStringLiteral("loading"))};
}

QVariantList build(const FeedContext &context)
{
    const QVariantList items = universeItems(context.extensions);
    return {hallSection(0, items.isEmpty() ? QStringLiteral("empty")
                                         : QStringLiteral("ready"), items)};
}

const bool registered = [] {
    FeedRegistry::Entry entry;
    entry.name = QStringLiteral("page.universeHall");
    entry.valid = valid;
    entry.initial = initial;
    entry.build = build;
    entry.needsExtensions = true;
    return FeedRegistry::add(std::move(entry));
}();
} // namespace

