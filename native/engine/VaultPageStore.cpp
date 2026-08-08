#include "VaultPageStore.h"

#include "CbzArchive.h"

#include <QCollator>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>

VaultPageStore::VaultPageStore(QObject* parent) : QObject(parent) {}

QVariantList VaultPageStore::localPages(const QString& archivePath) const
{
    QVariantList out;
    QString err;
    const auto entries = MangaTankoban::CbzArchive::imageEntries(archivePath, &err);
    if (entries.isEmpty())
        return out; // corrupt / unreadable — the reader shows nothing, never a wedge

    QStringList names;
    names.reserve(entries.size());
    for (const auto& e : entries)
        names.append(e.name);

    // Natural reading order (numeric-aware): "page 2" before "page 10".
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(names.begin(), names.end(),
              [&collator](const QString& a, const QString& b) { return collator.compare(a, b) < 0; });

    for (int i = 0; i < names.size(); ++i) {
        out.append(QVariantMap{
            {QStringLiteral("index"), i},
            {QStringLiteral("archive"), archivePath},
            {QStringLiteral("entry"), names.at(i)},
            {QStringLiteral("group"), 0},
        });
    }
    return out;
}
