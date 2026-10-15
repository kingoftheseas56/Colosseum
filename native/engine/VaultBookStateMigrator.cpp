#include "VaultBookStateMigrator.h"

#include "reader/BookStores.h"

#include <QJsonObject>

bool VaultBookStateMigrator::migrate(const QString& oldPath, const QString& newPath)
{
    const QString oldKey = BookStores::keyFor(oldPath);
    const QString newKey = BookStores::keyFor(newPath);
    if (oldKey.isEmpty() || newKey.isEmpty() || oldKey == newKey)
        return false;

    bool changed = false;
    for (const QString& fileName : {QStringLiteral("progress.json"),
                                    QStringLiteral("bookmarks.json"),
                                    QStringLiteral("annotations.json")}) {
        QJsonObject all = BookStores::readStore(fileName);
        if (!all.contains(oldKey) || all.contains(newKey))
            continue;
        all.insert(newKey, all.value(oldKey));
        BookStores::writeStore(fileName, all);
        changed = true;
    }
    return changed;
}
