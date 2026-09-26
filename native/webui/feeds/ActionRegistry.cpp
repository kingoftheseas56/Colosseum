#include "ActionRegistry.h"

#include <QDebug>
#include <QHash>
#include <utility>

namespace {
QHash<QString, ActionRegistry::Entry> &entries()
{
    static QHash<QString, ActionRegistry::Entry> registry;
    return registry;
}
}

bool ActionRegistry::add(Entry entry)
{
    if (entry.name.isEmpty() || !entry.valid || !entry.handle) return false;
    if (entries().contains(entry.name)) {
        qWarning() << "Duplicate web action registration:" << entry.name;
        return false;
    }
    entries().insert(entry.name, std::move(entry));
    return true;
}

const ActionRegistry::Entry *ActionRegistry::find(const QString &name)
{
    const auto it = entries().constFind(name);
    return it == entries().cend() ? nullptr : &it.value();
}
