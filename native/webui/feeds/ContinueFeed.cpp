#include "ContinueFeed.h"
#include "FeedRegistry.h"
#include "FeedValue.h"

#include "../../engine/ImdbCatalog.h"

#include <QHash>
#include <QStringList>
#include <QUuid>

namespace {
const bool registered = FeedRegistry::add({
    QStringLiteral("continue"), {},
    [](const QVariantMap &params) {
        return QStringList{QStringLiteral("all"), QStringLiteral("Tankoban"),
                           QStringLiteral("Biblio"), QStringLiteral("Theatre")}
            .contains(params.value(QStringLiteral("scope")).toString());
    },
    [](const QVariantMap &params) -> QVariantList {
        const QString scope = params.value(QStringLiteral("scope")).toString();
        return {WebFeedValue::section(QStringLiteral("continue.") + scope, 0,
            QStringLiteral("Loading"), QStringLiteral("list"), {},
            QStringLiteral("loading"))};
    },
    [](const FeedContext &ctx) {
        return ContinueFeed::build(ctx.recent,
            ctx.params.value(QStringLiteral("scope")).toString(),
            ctx.paths.imdb, ctx.visibleCount);
    }, true, false});
} // namespace

QVariantList ContinueFeed::build(const QVariantList &recent, const QString &scope,
                                 const QString &imdbPath, int visibleCount)
{
    // Moved from Main.qml:3080-3133 (Home Continue rail),
    // ContinueSeeAllPage.qml (scoped backlog), and
    // DeveloperWebUiBridge.cpp:141-210 (IMDb enrichment). The original Progress
    // row remains in Item.ref for Main.qml's resume/detail dispatcher.
    QStringList imdbIds;
    QVariantList selected;
    for (const QVariant &value : recent) {
        const QVariantMap row = value.toMap();
        const QString kind = row.value(QStringLiteral("kind")).toString();
        const bool inScope = scope == QLatin1String("all")
            || (scope == QLatin1String("Theatre") && kind == QLatin1String("video"))
            || (scope == QLatin1String("Biblio") && kind == QLatin1String("book"))
            || (scope == QLatin1String("Tankoban")
                && (kind == QLatin1String("manga") || kind == QLatin1String("tankoban")
                    || kind == QLatin1String("comic") || kind == QLatin1String("comics")));
        if (!inScope)
            continue;
        selected.append(row);
        if (kind == QLatin1String("video")) {
            QString root = row.value(QStringLiteral("libraryId")).toString();
            if (root.isEmpty())
                root = row.value(QStringLiteral("id")).toString().section(QLatin1Char(':'), 0, 0);
            if (root.startsWith(QLatin1String("tt")) && !imdbIds.contains(root))
                imdbIds.append(root);
        }
    }

    QVariantMap metadata;
    if (!imdbPath.isEmpty() && !imdbIds.isEmpty()) {
        // Own a distinct SQLite connection on this worker thread (§5.5).
        ImdbCatalog imdb(imdbPath, nullptr,
                         QStringLiteral("web_continue_imdb_")
                             + QUuid::createUuid().toString(QUuid::WithoutBraces));
        if (imdb.ready())
            metadata = imdb.rowsByIds(imdbIds);
    }

    QVariantList items;
    const int count = qMin(qMax(visibleCount, 1), selected.size());
    for (int i = 0; i < count; ++i) {
        QVariantMap row = selected.at(i).toMap();
        QString world;
        const QString kind = row.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("video")) {
            world = QStringLiteral("Theatre");
            QString root = row.value(QStringLiteral("libraryId")).toString();
            if (root.isEmpty())
                root = row.value(QStringLiteral("id")).toString().section(QLatin1Char(':'), 0, 0);
            const QVariantMap meta = metadata.value(root).toMap();
            for (const QString &key : {QStringLiteral("title"), QStringLiteral("year"),
                                       QStringLiteral("type"), QStringLiteral("rating")}) {
                if (!row.contains(key) || row.value(key).toString().isEmpty())
                    row.insert(key, meta.value(key));
            }
            if (root.startsWith(QLatin1String("tt")))
                row.insert(QStringLiteral("tt"), root);
        } else if (kind == QLatin1String("book")) {
            world = QStringLiteral("Biblio");
        } else {
            world = QStringLiteral("Tankoban");
        }
        items.append(WebFeedValue::item(row, world, QString(), true));
    }

    return {WebFeedValue::section(QStringLiteral("continue.%1.entries").arg(scope), 0,
                                  QStringLiteral("Continue"), QStringLiteral("continue"),
                                  items, items.isEmpty() ? QStringLiteral("empty")
                                                        : QStringLiteral("ready"),
                                  selected.size() > count)};
}
