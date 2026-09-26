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
    if (entry.name.isEmpty() || !entry.valid || !entry.initial) return false;
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
    const QString selector = name == QLatin1String("world")
        ? params.value(QStringLiteral("world")).toString() : QString();
    const auto it = entries().constFind(key(name, selector));
    return it == entries().cend() ? nullptr : &it.value();
}
