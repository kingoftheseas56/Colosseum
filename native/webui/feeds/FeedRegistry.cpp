#include "FeedRegistry.h"

#include <QHash>
#include <QDebug>
#include <utility>

namespace {
QHash<QString, FeedRegistry::Entry> &entries()
{
    static QHash<QString, FeedRegistry::Entry> registry;
    return registry;
}

QString key(const QString &name, const QString &selector)
{
    return name + QLatin1Char(':') + selector;
}
} // namespace

bool FeedRegistry::add(Entry entry)
{
    if (entry.name.isEmpty() || !entry.valid || !entry.initial || !entry.build) return false;
    const QString id = key(entry.name, entry.selector);
    if (entries().contains(id)) {
        qWarning() << "Duplicate web feed registration:" << id;
        return false;
    }
    entries().insert(id, std::move(entry));
    return true;
}

const FeedRegistry::Entry *FeedRegistry::find(const QString &name,
                                               const QVariantMap &params)
{
    QString selector;
    if (name == QLatin1String("world"))
        selector = params.value(QStringLiteral("world")).toString();
    else if (name == QLatin1String("seeAll")) {
        const QVariantMap route = params.value(QStringLiteral("route")).toMap();
        if (route.value(QStringLiteral("world")).toString() == QLatin1String("Theatre")
            && route.value(QStringLiteral("source")).toString() == QLatin1String("catalogue")
            && route.value(QStringLiteral("facet")).toMap()
                .value(QStringLiteral("kind")).toString() == QLatin1String("extension"))
            selector = QStringLiteral("TheatreExtension");
    }
    const auto it = entries().constFind(key(name, selector));
    return it == entries().cend() ? nullptr : &it.value();
}
