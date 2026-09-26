#include "FeedRegistry.h"
#include "FeedChoice.h"
#include "FeedHttp.h"
#include "FeedValue.h"
#include "ContinueFeed.h"

#include <QJsonArray>
#include <QJsonObject>

namespace {
QVariantList universeItems(const QVariantList &extensions)
{
    QVariantList items;
    for (const QVariant &value : extensions) {
        const QVariantMap entry = value.toMap();
        if (!entry.value(QStringLiteral("enabled")).toBool()) continue;
        const QVariantMap manifest = entry.value(QStringLiteral("manifest")).toMap();
        bool universe = false;
        // ExtensionsCatalog.js:241-249 and Main.qml:1783-1800: resource role,
        // enabled roster, manifest display name/logo/banner, installed order.
        for (const QVariant &resource : manifest.value(QStringLiteral("resources")).toList()) {
            if (resource.toString() == QLatin1String("universe") ||
                resource.toMap().value(QStringLiteral("name")).toString() == QLatin1String("universe")) {
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

QVariantMap intro(const QString &id, int index, const QString &title,
                  const QVariantList &items, const QString &world, const QString &medium,
                  bool showExplicit)
{
    QVariantMap section = WebFeedValue::section(id, index, title, QStringLiteral("rail"), items,
        items.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"));
    section.insert(QStringLiteral("seeAll"), QVariantMap{{QStringLiteral("route"),
        WebFeedChoice::route(world, QStringLiteral("catalogue"),
            {{QStringLiteral("medium"), medium}, {QStringLiteral("catalogue"), QStringLiteral("popular")}},
            QStringLiteral("popular"), showExplicit, 24)}});
    return section;
}

QVariantList appleTop()
{
    // ReadingDesk.qml:7,31 and BiblioApi.js:121-129 use the US top ebooks chart.
    const auto reply = WebFeedHttp::request(QUrl(QStringLiteral("https://itunes.apple.com/us/rss/topebooks/limit=12/json")));
    QVariantList items;
    for (const QJsonValue &value : reply.json.object().value(QStringLiteral("feed")).toObject()
                                      .value(QStringLiteral("entry")).toArray()) {
        const QJsonObject entry = value.toObject();
        const QString id = entry.value(QStringLiteral("id")).toObject().value(QStringLiteral("attributes"))
            .toObject().value(QStringLiteral("im:id")).toString();
        const QString title = entry.value(QStringLiteral("im:name")).toObject().value(QStringLiteral("label")).toString();
        const QJsonArray art = entry.value(QStringLiteral("im:image")).toArray();
        const QVariantMap row{{QStringLiteral("id"), id}, {QStringLiteral("source"), QStringLiteral("apple")},
            {QStringLiteral("title"), title},
            {QStringLiteral("subtitle"), entry.value(QStringLiteral("im:artist")).toObject().value(QStringLiteral("label")).toString()},
            {QStringLiteral("cover"), art.isEmpty() ? QString() : art.last().toObject().value(QStringLiteral("label")).toString()}};
        if (!id.isEmpty() && !title.isEmpty()) items.append(WebFeedValue::item(row, QStringLiteral("Biblio"), QStringLiteral("book")));
        if (items.size() >= 10) break;
    }
    return items;
}

QVariantList build(const FeedContext &ctx)
{
    QVariantList sections;
    QVariantMap hero = WebFeedValue::section(QStringLiteral("home.universes"), 0,
        QStringLiteral("Universes"), QStringLiteral("hero"), universeItems(ctx.extensions));
    hero.insert(QStringLiteral("headerAction"), QVariantMap{{QStringLiteral("action"), QStringLiteral("open.universeHall")},
        {QStringLiteral("label"), QStringLiteral("Hall of Worlds")}});
    sections.append(hero);

    // Main.qml:3347-3366: one mixed Continue row, absent when there is no progress.
    QVariantList continuing = ContinueFeed::build(ctx.recent, QStringLiteral("all"), ctx.paths.imdb, 24);
    if (!continuing.isEmpty()) {
        QVariantMap section = continuing.first().toMap();
        if (!section.value(QStringLiteral("items")).toList().isEmpty()) {
            section.insert(QStringLiteral("id"), QStringLiteral("home.continue"));
            section.insert(QStringLiteral("index"), sections.size());
            section.insert(QStringLiteral("hasMore"), false);
            section.insert(QStringLiteral("seeAll"), QVariantMap{{QStringLiteral("route"),
                WebFeedChoice::route(QStringLiteral("all"), QStringLiteral("continue"), {}, {}, ctx.showExplicit, 24)}});
            sections.append(section);
        }
    }

    // Main.qml:3368-3432: Bookshelf, TheatreStrip, ReadingDesk, Vault order.
    // WorldFeed already owns the worker-local catalogue projections for these worlds.
    const QVariantList tankoban = WorldFeed::build(QStringLiteral("Tankoban"), QStringLiteral("discover"),
        ctx.paths, {}, ctx.showExplicit);
    QVariantList tankobanItems;
    for (const QVariant &value : tankoban) {
        const QVariantMap section = value.toMap();
        if (section.value(QStringLiteral("id")).toString().endsWith(QLatin1String("mangaPopular")) ||
            section.value(QStringLiteral("id")).toString().endsWith(QLatin1String("comicsPopular"))) {
            int fromLane = 0;
            for (const QVariant &item : section.value(QStringLiteral("items")).toList()) {
                tankobanItems.append(item);
                if (++fromLane >= 6) break;
            }
        }
    }
    sections.append(intro(QStringLiteral("home.tankoban"), sections.size(), QStringLiteral("Tankoban"),
        tankobanItems, QStringLiteral("Tankoban"), QStringLiteral("manga"), ctx.showExplicit));

    const QVariantList theatre = WorldFeed::build(QStringLiteral("Theatre"), QStringLiteral("discover"),
        ctx.paths, {}, ctx.showExplicit);
    QVariantList theatreItems;
    // TheatreStrip.qml:35-57 interleaves movies, shows and anime, three from each.
    for (int rank = 0; rank < 3; ++rank)
        for (const QVariant &value : theatre) {
            const QVariantMap section = value.toMap();
            const QVariantList items = section.value(QStringLiteral("items")).toList();
            if (QStringList{QStringLiteral("Top Movies"), QStringLiteral("Top Shows"),
                            QStringLiteral("Top Anime")}.contains(section.value(QStringLiteral("title")).toString())
                && rank < items.size()) theatreItems.append(items.at(rank));
        }
    sections.append(intro(QStringLiteral("home.theatre"), sections.size(), QStringLiteral("Theatre"),
        theatreItems, QStringLiteral("Theatre"), QStringLiteral("movie"), ctx.showExplicit));

    QVariantMap biblio = intro(QStringLiteral("home.biblio"), sections.size(), QStringLiteral("Biblio"),
        {}, QStringLiteral("Biblio"), QStringLiteral("book"), ctx.showExplicit);
    biblio.insert(QStringLiteral("state"), QStringLiteral("loading"));
    sections.append(biblio);
    // Main.qml:3434-3450 / VaultHomeWidget.qml: the Home teaser is a Vault door.
    sections.append(WebFeedChoice::section(QStringLiteral("home.vault"), sections.size(),
        QStringLiteral("Vault"), QStringLiteral("tiles"),
        {WebFeedChoice::choice(QStringLiteral("door:vault"), QStringLiteral("Open Vault"),
            {{QStringLiteral("act"), QStringLiteral("open.vault")}})}));
    return sections;
}

QVariantList enrich(const FeedContext &ctx)
{
    QVariantList sections = ctx.baseSections;
    for (int i = 0; i < sections.size(); ++i) {
        QVariantMap section = sections.at(i).toMap();
        if (section.value(QStringLiteral("id")).toString() != QLatin1String("home.biblio")) continue;
        const QVariantList items = appleTop();
        section.insert(QStringLiteral("items"), items);
        section.insert(QStringLiteral("state"), items.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"));
        sections[i] = section;
        break;
    }
    return sections;
}

const bool registered = FeedRegistry::add({QStringLiteral("home"), {},
    [](const QVariantMap &params) { return params.isEmpty(); },
    [](const QVariantMap &) -> QVariantList {
        return {WebFeedValue::section(QStringLiteral("home.universes"), 0,
            QStringLiteral("Universes"), QStringLiteral("hero"), {}, QStringLiteral("loading"))};
    }, build, true, false, true, false, enrich});
} // namespace
