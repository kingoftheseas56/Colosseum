#include "TankoyomiSeriesMatcher.h"

#include <QSet>

namespace TankoyomiSeriesMatcher {

QString foldTitle(const QString &title)
{
    // Preserve the established Manga/Nyaa case, separator and apostrophe fold,
    // without importing torrent matching or its permissive substring rule.
    QString folded;
    folded.reserve(title.size());
    for (const QChar character : title) {
        if (character == QChar(0x0027) || character == QChar(0x2019)) continue;
        folded.append(character.isLetterOrNumber() ? character.toLower() : QLatin1Char(' '));
    }
    return folded.simplified();
}

TankoyomiSeriesMatch match(const TankoyomiSeriesQuery &query,
                          const QVariantList &rows, const QStringList &titleDecorators)
{
    QSet<QString> identities;
    for (const QString &title : QStringList{query.discoveryTitle, query.title} + query.aliases) {
        const QString folded = foldTitle(title);
        if (!folded.isEmpty()) identities.insert(folded);
    }
    if (identities.isEmpty()) return {{}, QStringLiteral("empty-query"), false};

    QStringList markers;
    for (const QString &marker : query.requiredTitleMarkers) {
        const QString folded = foldTitle(marker);
        if (!folded.isEmpty()) markers.append(folded);
    }
    QSet<QString> decorators;
    for (const QString &decorator : titleDecorators) {
        const QString folded = foldTitle(decorator);
        if (!folded.isEmpty() && !folded.contains(QLatin1Char(' '))) decorators.insert(folded);
    }
    const auto findTier = [&](bool decorated) -> TankoyomiSeriesMatch {
        QVariantMap selected;
        QString selectedId;
        for (const QVariant &value : rows) {
            const QVariantMap candidate = value.toMap();
            const QString id = candidate.value(QStringLiteral("id")).toString().trimmed();
            const QString original = foldTitle(candidate.value(QStringLiteral("title")).toString());
            if (id.isEmpty() || original.isEmpty()) continue;
            bool markerPresent = query.requiredTitleMarkers.isEmpty();
            for (const QString &marker : markers) markerPresent = markerPresent || original.contains(marker);
            if (!markerPresent) continue;

            QString folded = original;
            if (decorated) {
                QStringList words = folded.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                while (!words.isEmpty() && decorators.contains(words.first())) words.removeFirst();
                while (!words.isEmpty() && decorators.contains(words.last())) words.removeLast();
                folded = words.join(QLatin1Char(' '));
                if (folded == original) continue;
            }
            if (!identities.contains(folded)) continue;
            if (!selectedId.isEmpty() && selectedId != id)
                return {{}, decorated ? QStringLiteral("ambiguous-decorated-exact")
                                      : QStringLiteral("ambiguous-exact"), false};
            if (selectedId.isEmpty()) {
                selected = candidate;
                selectedId = id;
            }
        }
        if (selectedId.isEmpty()) return {{}, QStringLiteral("no-match"), false};
        return {selected, decorated ? QStringLiteral("decorated-exact") : QStringLiteral("exact"), true};
    };
    const auto exact = findTier(false);
    if (exact.accepted || exact.reason.startsWith(QLatin1String("ambiguous"))) return exact;
    return findTier(true);
}

} // namespace TankoyomiSeriesMatcher
