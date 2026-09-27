#include "ActionRegistry.h"
#include "FeedHttp.h"
#include "FeedRegistry.h"
#include "FeedValue.h"

#include "../ColosseumWebBridge.h"
#include "../../MangaEngine.h"
#include "../../engine/ExtensionsStore.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <memory>

namespace {

QStringList resourceNames(const QVariantMap &manifest)
{
    QStringList names;
    for (const QVariant &value : manifest.value(QStringLiteral("resources")).toList()) {
        const QString name = value.metaType().id() == QMetaType::QString
            ? value.toString() : value.toMap().value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) names.append(name);
    }
    return names;
}

bool hasResource(const QVariantMap &manifest, const QString &name)
{
    return resourceNames(manifest).contains(name);
}

QStringList worldsFor(const QVariantMap &entry)
{
    const QVariantMap manifest = entry.value(QStringLiteral("manifest")).toMap();
    if (hasResource(manifest, QStringLiteral("universe")))
        return {QStringLiteral("universes")};

    const QStringList types = [&manifest] {
        QStringList out;
        for (const QVariant &value : manifest.value(QStringLiteral("types")).toList())
            out.append(value.toString());
        return out;
    }();

    QStringList out;
    const QList<QPair<QString, QStringList>> rules{
        {QStringLiteral("theatre"), {QStringLiteral("movie"), QStringLiteral("series"), QStringLiteral("anime")}},
        {QStringLiteral("tankoban"), {QStringLiteral("manga"), QStringLiteral("comic")}},
        {QStringLiteral("biblio"), {QStringLiteral("book"), QStringLiteral("audiobook")}}
    };
    for (const auto &rule : rules) {
        for (const QString &type : rule.second) {
            if (types.contains(type)) {
                out.append(rule.first);
                break;
            }
        }
    }
    if (out.isEmpty()
        && manifest.value(QStringLiteral("behaviorHints")).toMap()
               .value(QStringLiteral("configurationRequired")).toBool()) {
        out.append(QStringLiteral("theatre"));
    }
    return out;
}

bool isCatalogue(const QVariantMap &entry)
{
    return entry.value(QStringLiteral("core")).toBool()
        && hasResource(entry.value(QStringLiteral("manifest")).toMap(), QStringLiteral("catalog"));
}

bool isWell(const QVariantMap &entry)
{
    return !isCatalogue(entry)
        && hasResource(entry.value(QStringLiteral("manifest")).toMap(), QStringLiteral("stream"));
}

QString publicAsset(const QString &raw)
{
    const QUrl url(raw);
    if (!url.isValid() || (url.scheme() != QLatin1String("http")
        && url.scheme() != QLatin1String("https")) || !url.userInfo().isEmpty()
        || url.hasQuery() || url.hasFragment()) {
        return {};
    }
    return url.toString();
}

QString entryToken(const QVariantMap &entry)
{
    const QString id = entry.value(QStringLiteral("id")).toString();
    const QString transport = entry.value(QStringLiteral("transportUrl")).toString();
    if (transport.isEmpty() || transport.startsWith(QLatin1String("colosseum://")))
        return QStringLiteral("id:") + id;
    const QByteArray digest = QCryptographicHash::hash(
        transport.toUtf8(), QCryptographicHash::Sha256).toHex().left(24);
    return QStringLiteral("instance:") + QString::fromLatin1(digest);
}

QVariantMap findEntry(ExtensionsStore *store, const QString &token)
{
    if (!store || token.isEmpty()) return {};
    for (const QVariant &value : store->installed()) {
        const QVariantMap entry = value.toMap();
        if (entryToken(entry) == token) return entry;
    }
    return {};
}

QString nameOf(const QVariantMap &entry)
{
    const QVariantMap manifest = entry.value(QStringLiteral("manifest")).toMap();
    return manifest.value(QStringLiteral("name"),
                          entry.value(QStringLiteral("id"))).toString();
}

QString worldTitle(const QString &world)
{
    if (world == QLatin1String("theatre")) return QStringLiteral("Theatre");
    if (world == QLatin1String("tankoban")) return QStringLiteral("Tankoban");
    if (world == QLatin1String("biblio")) return QStringLiteral("Biblio");
    if (world == QLatin1String("universes")) return QStringLiteral("Universes");
    return world;
}

QString ordinal(int rank)
{
    if (rank == 1) return QStringLiteral("1st");
    if (rank == 2) return QStringLiteral("2nd");
    if (rank == 3) return QStringLiteral("3rd");
    return QString::number(rank) + QStringLiteral("th");
}

QVariantMap displayRow(const QVariantMap &entry)
{
    const QVariantMap manifest = entry.value(QStringLiteral("manifest")).toMap();
    const QStringList worlds = worldsFor(entry);
    QVariantMap row{
        {QStringLiteral("token"), entryToken(entry)},
        {QStringLiteral("id"), entry.value(QStringLiteral("id"))},
        {QStringLiteral("name"), nameOf(entry)},
        {QStringLiteral("enabled"), entry.value(QStringLiteral("enabled")).toBool()},
        {QStringLiteral("core"), entry.value(QStringLiteral("core")).toBool()},
        {QStringLiteral("catalogue"), isCatalogue(entry)},
        {QStringLiteral("source"), isWell(entry)},
        {QStringLiteral("house"), entry.value(QStringLiteral("transportUrl")).toString()
                                      .startsWith(QLatin1String("colosseum://"))},
        {QStringLiteral("worlds"), worlds},
        {QStringLiteral("configurable"),
            manifest.value(QStringLiteral("behaviorHints")).toMap()
                .value(QStringLiteral("configurable")).toBool()},
        {QStringLiteral("configurationRequired"),
            manifest.value(QStringLiteral("behaviorHints")).toMap()
                .value(QStringLiteral("configurationRequired")).toBool()}
    };
    const QString description = manifest.value(QStringLiteral("description")).toString();
    if (!description.isEmpty()) row.insert(QStringLiteral("description"), description);
    const QString logo = publicAsset(manifest.value(QStringLiteral("logo")).toString());
    if (!logo.isEmpty()) row.insert(QStringLiteral("logo"), logo);
    return row;
}

QList<QVariantMap> entriesForWorld(const QVariantList &installed, const QString &world)
{
    QList<QVariantMap> catalogues;
    QList<QVariantMap> wells;
    QList<QVariantMap> other;
    for (const QVariant &value : installed) {
        const QVariantMap entry = value.toMap();
        if (!worldsFor(entry).contains(world)) continue;
        if (isCatalogue(entry)) catalogues.append(entry);
        else if (isWell(entry)) wells.append(entry);
        else other.append(entry);
    }
    catalogues.append(wells);
    catalogues.append(other);
    return catalogues;
}

QList<QVariantMap> wellsForWorld(const QVariantList &installed, const QString &world)
{
    QList<QVariantMap> out;
    for (const QVariant &value : installed) {
        const QVariantMap entry = value.toMap();
        if (worldsFor(entry).contains(world) && isWell(entry)) out.append(entry);
    }
    return out;
}

// Moved from ExtensionsCatalog.js:317-401 and ExtensionsPage.qml:123-143.
// The on-screen order is world-relative but ExtensionsStore persists one global array.
// Cost both physical swaps and move the row that disturbs the fewest other worlds.
struct MoveTarget {
    QString token;
    int globalIndex = -1;
};

bool sharesWorld(const QVariantMap &a, const QVariantMap &b)
{
    const QStringList wa = worldsFor(a);
    const QStringList wb = worldsFor(b);
    for (const QString &world : wa)
        if (wb.contains(world)) return true;
    return false;
}

int crossingCost(const QList<QVariantMap> &all, int lo, int hi, const QVariantMap &mover)
{
    int cost = 0;
    for (int i = lo + 1; i < hi; ++i)
        if (sharesWorld(all.at(i), mover)) ++cost;
    return cost;
}

MoveTarget moveDestination(const QVariantList &installed, const QString &world,
                           const QString &token, int delta)
{
    QList<QVariantMap> all;
    for (const QVariant &value : installed) all.append(value.toMap());
    const QList<QVariantMap> wells = wellsForWorld(installed, world);
    int from = -1;
    for (int i = 0; i < wells.size(); ++i)
        if (entryToken(wells.at(i)) == token) { from = i; break; }
    const int to = from + delta;
    if (from < 0 || to < 0 || to >= wells.size()) return {};

    int ia = -1;
    int ib = -1;
    const QString neighbor = entryToken(wells.at(to));
    for (int i = 0; i < all.size(); ++i) {
        const QString candidate = entryToken(all.at(i));
        if (candidate == token) ia = i;
        if (candidate == neighbor) ib = i;
    }
    if (ia < 0 || ib < 0) return {};

    const int lo = std::min(ia, ib);
    const int hi = std::max(ia, ib);
    const int clickedCost = crossingCost(all, lo, hi, all.at(ia));
    const int otherCost = crossingCost(all, lo, hi, all.at(ib));
    return otherCost < clickedCost
        ? MoveTarget{entryToken(all.at(ib)), ia}
        : MoveTarget{entryToken(all.at(ia)), ib};
}

QVariantMap customSection(const QString &id, int index, const QString &title,
                          const QString &schema, const QVariantMap &data,
                          const QString &state = QStringLiteral("ready"),
                          const QVariantList &items = {}, const QString &error = {})
{
    QVariantMap section = WebFeedValue::section(
        id, index, title, QStringLiteral("custom"), items, state);
    QVariantMap payload = data;
    payload.insert(QStringLiteral("schema"), schema);
    section.insert(QStringLiteral("data"), payload);
    if (!error.isEmpty()) section.insert(QStringLiteral("error"), error);
    return section;
}

QVariantMap summaryData(const QVariantList &installed)
{
    int enabled = 0;
    for (const QVariant &value : installed)
        if (value.toMap().value(QStringLiteral("enabled")).toBool()) ++enabled;
    return {
        {QStringLiteral("installedCount"), installed.size()},
        {QStringLiteral("enabledCount"), enabled}
    };
}

// Moved from ExtensionsSources.qml:38-108 and 115-239.
// Catalogues are unnumbered before the divider; fetch sources are numbered after it.
QVariantList chainSections(const FeedContext &ctx, const QString &query)
{
    QVariantList worlds;
    const QString needle = query.trimmed().toLower();
    for (const QString &world : {QStringLiteral("theatre"), QStringLiteral("tankoban"),
                                 QStringLiteral("biblio")}) {
        const QList<QVariantMap> entries = entriesForWorld(ctx.extensions, world);
        const QList<QVariantMap> wells = wellsForWorld(ctx.extensions, world);
        QVariantList rows;
        int sourceRank = 0;
        for (const QVariantMap &entry : entries) {
            QVariantMap row = displayRow(entry);
            const QString name = row.value(QStringLiteral("name")).toString();
            if (!needle.isEmpty() && !name.toLower().contains(needle)) {
                if (isWell(entry)) ++sourceRank;
                continue;
            }
            if (isWell(entry)) {
                ++sourceRank;
                row.insert(QStringLiteral("rank"), sourceRank);
                QString tie;
                const QStringList memberships = worldsFor(entry);
                if (memberships.size() > 1) {
                    for (const QString &other : memberships) {
                        if (other == world) continue;
                        const QList<QVariantMap> otherWells = wellsForWorld(ctx.extensions, other);
                        for (int i = 0; i < otherWells.size(); ++i) {
                            if (entryToken(otherWells.at(i)) == entryToken(entry)) {
                                tie = QStringLiteral("Also in %1 · %2")
                                    .arg(worldTitle(other), ordinal(i + 1));
                                break;
                            }
                        }
                        if (!tie.isEmpty()) break;
                    }
                }
                if (!tie.isEmpty()) row.insert(QStringLiteral("tie"), tie);
            }
            rows.append(row);
        }
        worlds.append(QVariantMap{
            {QStringLiteral("key"), world},
            {QStringLiteral("title"), worldTitle(world)},
            {QStringLiteral("rows"), rows},
            {QStringLiteral("sourceCount"), wells.size()}
        });
    }

    QVariantMap data = summaryData(ctx.extensions);
    data.insert(QStringLiteral("worlds"), worlds);
    return {customSection(QStringLiteral("extensions.chain"), 0, QString(),
                          QStringLiteral("extensions.chain"), data,
                          worlds.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"))};
}

QString houseJob(const QString &id)
{
    static const QHash<QString, QString> jobs{
        {QStringLiteral("colosseum.well.nyaa"), QStringLiteral("Torrents")},
        {QStringLiteral("colosseum.well.tankoyomi"), QStringLiteral("Chapters")},
        {QStringLiteral("colosseum.well.getcomics.issues"), QStringLiteral("Downloads")},
        {QStringLiteral("colosseum.well.libgen"), QStringLiteral("Downloads")},
        {QStringLiteral("colosseum.well.indexers"), QStringLiteral("Torrents")},
        {QStringLiteral("colosseum.well.audiobookbay"), QStringLiteral("Torrents")}
    };
    return jobs.value(id, QStringLiteral("Source"));
}

QString houseDescription(const QString &id, const QString &fallback)
{
    static const QHash<QString, QString> descriptions{
        {QStringLiteral("colosseum.well.nyaa"), QStringLiteral("Finds whole manga volumes as torrents.")},
        {QStringLiteral("colosseum.well.tankoyomi"), QStringLiteral("Reads manga chapters from the configured language-aware source ladder.")},
        {QStringLiteral("colosseum.well.getcomics.issues"), QStringLiteral("Downloads comic issues as they are released.")},
        {QStringLiteral("colosseum.well.libgen"), QStringLiteral("Downloads book files from Library Genesis.")},
        {QStringLiteral("colosseum.well.indexers"), QStringLiteral("Searches torrent indexers for comics and books.")},
        {QStringLiteral("colosseum.well.audiobookbay"), QStringLiteral("Finds audiobooks as torrents.")}
    };
    return descriptions.value(id, fallback);
}

// Moved from ExtensionsPage.qml:158-167 refresh(), now built from the native store snapshot.
QVariantList houseSections(const FeedContext &ctx, const QString &query)
{
    QVariantList rows;
    const QString needle = query.trimmed().toLower();
    for (const QVariant &value : ctx.extensions) {
        const QVariantMap entry = value.toMap();
        const QString transport = entry.value(QStringLiteral("transportUrl")).toString();
        if (!transport.startsWith(QLatin1String("colosseum://well/"))) continue;
        QVariantMap row = displayRow(entry);
        if (!needle.isEmpty()
            && !row.value(QStringLiteral("name")).toString().toLower().contains(needle)) continue;
        const QString id = entry.value(QStringLiteral("id")).toString();
        row.insert(QStringLiteral("job"), houseJob(id));
        row.insert(QStringLiteral("description"),
                   houseDescription(id, row.value(QStringLiteral("description")).toString()));
        rows.append(row);
    }

    QVariantMap data = summaryData(ctx.extensions);
    data.insert(QStringLiteral("rows"), rows);
    return {customSection(QStringLiteral("extensions.house"), 0, QString(),
                          QStringLiteral("extensions.house"), data,
                          rows.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"))};
}

bool jsonAdult(const QJsonObject &entry)
{
    if (entry.value(QStringLiteral("nsfw")).toBool()
        || entry.value(QStringLiteral("adult")).toBool()
        || entry.value(QStringLiteral("isAdult")).toBool()) return true;
    const QJsonObject manifest = entry.value(QStringLiteral("manifest")).isObject()
        ? entry.value(QStringLiteral("manifest")).toObject() : entry;
    if (manifest.value(QStringLiteral("adult")).toBool()
        || manifest.value(QStringLiteral("behaviorHints")).toObject()
            .value(QStringLiteral("adult")).toBool()) return true;
    for (const QJsonValue &catalog : manifest.value(QStringLiteral("catalogs")).toArray()) {
        const QString id = catalog.toObject().value(QStringLiteral("id")).toString().toLower();
        if (id.contains(QStringLiteral("nsfw")) || id.contains(QStringLiteral("adult")))
            return true;
    }
    for (const QJsonValue &value : entry.value(QStringLiteral("categories")).toArray()) {
        const QString category = value.toString().toLower();
        if (category == QLatin1String("nsfw") || category == QLatin1String("adult")) return true;
    }
    return false;
}

QJsonArray documentArray(const QJsonDocument &doc)
{
    if (doc.isArray()) return doc.array();
    if (!doc.isObject()) return {};
    const QJsonObject root = doc.object();
    for (const QString &key : {QStringLiteral("rows"), QStringLiteral("addons"),
                               QStringLiteral("items"),
                               QStringLiteral("data"), QStringLiteral("result")}) {
        if (root.value(key).isArray()) return root.value(key).toArray();
    }
    return {};
}

QString communityKind(const QJsonObject &manifest, const QJsonArray &categories)
{
    QStringList resources;
    for (const QJsonValue &value : manifest.value(QStringLiteral("resources")).toArray())
        resources.append(value.isString() ? value.toString()
                                         : value.toObject().value(QStringLiteral("name")).toString());
    QStringList parts;
    if (resources.contains(QStringLiteral("stream"))) parts.append(QStringLiteral("Streams"));
    if (resources.contains(QStringLiteral("catalog"))) parts.append(QStringLiteral("Catalogs"));
    if (resources.contains(QStringLiteral("meta"))) parts.append(QStringLiteral("Details"));
    if (resources.contains(QStringLiteral("subtitles"))) parts.append(QStringLiteral("Subtitles"));
    if (parts.isEmpty() && !categories.isEmpty())
        parts.append(categories.first().toString());
    return parts.isEmpty() ? QStringLiteral("Extension") : parts.join(QStringLiteral(" · "));
}

QVariantMap communityRow(const QJsonObject &entry, bool showExplicit,
                         const QSet<QString> &installedIds)
{
    if (entry.isEmpty() || (!showExplicit && jsonAdult(entry))) return {};
    const QJsonObject manifest = entry.value(QStringLiteral("manifest")).isObject()
        ? entry.value(QStringLiteral("manifest")).toObject() : entry;
    const QString url = entry.value(QStringLiteral("transportUrl")).toString().isEmpty()
        ? (entry.value(QStringLiteral("manifestUrl")).toString().isEmpty()
            ? entry.value(QStringLiteral("url")).toString()
            : entry.value(QStringLiteral("manifestUrl")).toString())
        : entry.value(QStringLiteral("transportUrl")).toString();
    const QString name = manifest.value(QStringLiteral("name")).toString().isEmpty()
        ? entry.value(QStringLiteral("name")).toString()
        : manifest.value(QStringLiteral("name")).toString();
    if (name.isEmpty() || url.isEmpty()) return {};

    const QString id = manifest.value(QStringLiteral("id")).toString().isEmpty()
        ? (entry.value(QStringLiteral("id")).toString().isEmpty() ? url
                                                                  : entry.value(QStringLiteral("id")).toString())
        : manifest.value(QStringLiteral("id")).toString();
    QString description = manifest.value(QStringLiteral("description")).toString();
    if (description.isEmpty()) description = entry.value(QStringLiteral("description")).toString();
    description = description.split(QLatin1Char('\n')).value(0).left(140);
    QString logo = manifest.value(QStringLiteral("logo")).toString();
    if (logo.isEmpty()) logo = entry.value(QStringLiteral("logo")).toString();
    logo = publicAsset(logo);

    const QJsonObject hints = manifest.value(QStringLiteral("behaviorHints")).toObject();
    QVariantMap row{
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("description"), description},
        {QStringLiteral("kind"), communityKind(manifest, entry.value(QStringLiteral("categories")).toArray())},
        {QStringLiteral("installUrl"), url},
        {QStringLiteral("stars"), entry.value(QStringLiteral("stars")).toInt(
             entry.value(QStringLiteral("votes")).toInt())},
        {QStringLiteral("installed"), installedIds.contains(id)},
        {QStringLiteral("configurable"), hints.value(QStringLiteral("configurable")).toBool()},
        {QStringLiteral("configurationRequired"),
            hints.value(QStringLiteral("configurationRequired")).toBool()}
    };
    if (!logo.isEmpty()) row.insert(QStringLiteral("logo"), logo);
    return row;
}

struct CommunityResult {
    QVariantList rows;
    QString error;
};

CommunityResult community(const QString &sort, const QString &search, bool showExplicit,
                          const QSet<QString> &installedIds)
{
    QUrl url;
    if (sort == QLatin1String("rising") && search.trimmed().isEmpty()) {
        url = QUrl(QStringLiteral("https://stremio-addons.net/api/v0/rising"));
    } else {
        url = QUrl(QStringLiteral("https://stremio-addons.net/api/v0/addons"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("page"), QStringLiteral("1"));
        query.addQueryItem(QStringLiteral("limit"), QStringLiteral("60"));
        if (!showExplicit) query.addQueryItem(QStringLiteral("nsfw"), QStringLiteral("exclude"));
        query.addQueryItem(QStringLiteral("order"), QStringLiteral("desc"));
        query.addQueryItem(QStringLiteral("sort_by"),
                           sort == QLatin1String("new") ? QStringLiteral("createdAt")
                                                       : QStringLiteral("stars"));
        if (!search.trimmed().isEmpty())
            query.addQueryItem(QStringLiteral("search"), search.trimmed());
        url.setQuery(query);
    }

    // Moved from ExtensionsPage.qml:204-220 loadCommunity() and
    // ExtensionsCatalog.js:20-34, 445-488. Community discovery is native now.
    WebFeedHttp::Reply reply = WebFeedHttp::request(url, {}, 5000);
    QJsonArray raw = reply.ok ? documentArray(reply.json) : QJsonArray{};
    if (raw.isEmpty()) {
        const auto fallback = WebFeedHttp::request(
            QUrl(QStringLiteral("https://api.strem.io/addonsofficialcollection.json")), {}, 5000);
        if (fallback.ok) raw = documentArray(fallback.json);
        else if (!reply.ok) return {{}, QStringLiteral("The community catalogue is unavailable right now.")};
    }

    QVariantList rows;
    const QString needle = search.trimmed().toLower();
    for (const QJsonValue &value : raw) {
        QVariantMap row = communityRow(value.toObject(), showExplicit, installedIds);
        if (row.isEmpty()) continue;
        if (!needle.isEmpty()) {
            const QString haystack = (row.value(QStringLiteral("name")).toString()
                + QLatin1Char(' ') + row.value(QStringLiteral("description")).toString()).toLower();
            if (!haystack.contains(needle)) continue;
        }
        rows.append(row);
    }
    return {rows, {}};
}

QVariantList take(const QVariantList &rows, int count,
                  const std::function<bool(const QVariantMap &)> &filter = {})
{
    QVariantList out;
    for (const QVariant &value : rows) {
        const QVariantMap row = value.toMap();
        if (filter && !filter(row)) continue;
        out.append(row);
        if (out.size() >= count) break;
    }
    return out;
}

QVariantList universeItems(const QVariantList &installed)
{
    QVariantList items;
    for (const QVariant &value : installed) {
        const QVariantMap entry = value.toMap();
        if (!entry.value(QStringLiteral("enabled")).toBool()) continue;
        const QVariantMap manifest = entry.value(QStringLiteral("manifest")).toMap();
        if (!hasResource(manifest, QStringLiteral("universe"))) continue;
        const QString id = entry.value(QStringLiteral("id")).toString();
        const QString name = nameOf(entry);
        QVariantMap item{
            {QStringLiteral("key"), QStringLiteral("Colosseum:universe:") + id},
            {QStringLiteral("world"), QStringLiteral("Colosseum")},
            {QStringLiteral("kind"), QStringLiteral("universe")},
            {QStringLiteral("title"), name},
            {QStringLiteral("ref"), QVariantMap{
                {QStringLiteral("extensionId"), id},
                {QStringLiteral("name"), name}
            }}
        };
        const QString logo = publicAsset(manifest.value(QStringLiteral("logo")).toString());
        const QString background = publicAsset(manifest.value(QStringLiteral("background")).toString());
        if (!logo.isEmpty()) item.insert(QStringLiteral("cover"), logo);
        if (!background.isEmpty()) item.insert(QStringLiteral("backdrop"), background);
        items.append(item);
    }
    return items;
}

QVariantList jobs(const QVariantList &communityRows, const QVariantList &installed)
{
    struct Job { QString name; QStringList needles; };
    const QList<Job> definitions{
        {QStringLiteral("Streams"), {QStringLiteral("stream")}},
        {QStringLiteral("Torrents"), {QStringLiteral("torrent")}},
        {QStringLiteral("Catalogs"), {QStringLiteral("catalog")}},
        {QStringLiteral("Subtitles"), {QStringLiteral("subtitle")}},
        {QStringLiteral("Anime"), {QStringLiteral("anime")}},
        {QStringLiteral("Live TV"), {QStringLiteral("live")}},
        {QStringLiteral("Manga & comics"), {QStringLiteral("manga"), QStringLiteral("comic")}},
        {QStringLiteral("Books & audiobooks"), {QStringLiteral("book"), QStringLiteral("audiobook")}}
    };
    QVariantList out;
    for (const Job &job : definitions) {
        int count = 0;
        for (const QVariant &value : communityRows) {
            const QVariantMap row = value.toMap();
            const QString hay = (row.value(QStringLiteral("kind")).toString()
                + QLatin1Char(' ') + row.value(QStringLiteral("description")).toString()).toLower();
            for (const QString &needle : job.needles)
                if (hay.contains(needle)) { ++count; break; }
        }
        for (const QVariant &value : installed) {
            const QVariantMap entry = value.toMap();
            if (!entry.value(QStringLiteral("transportUrl")).toString()
                    .startsWith(QLatin1String("colosseum://well/"))) continue;
            const QString hay = (nameOf(entry) + QLatin1Char(' ')
                + worldsFor(entry).join(QLatin1Char(' '))).toLower();
            for (const QString &needle : job.needles)
                if (hay.contains(needle)) { ++count; break; }
        }
        out.append(QVariantMap{{QStringLiteral("name"), job.name},
                               {QStringLiteral("count"), count}});
    }
    return out;
}

QVariantList exploreSections(const FeedContext &ctx, const QString &query)
{
    QSet<QString> installedIds;
    for (const QVariant &value : ctx.extensions)
        installedIds.insert(value.toMap().value(QStringLiteral("id")).toString());

    const QVariantList universes = universeItems(ctx.extensions);
    QVariantMap universeData = summaryData(ctx.extensions);
    universeData.insert(QStringLiteral("label"), QStringLiteral("Universes"));

    const CommunityResult top = community(QStringLiteral("top"), query,
                                          ctx.showExplicit, installedIds);
    if (!query.trimmed().isEmpty()) {
        QVariantMap data = summaryData(ctx.extensions);
        data.insert(QStringLiteral("rows"), top.rows);
        return {
            customSection(QStringLiteral("extensions.explore.universes"), 0, QString(),
                          QStringLiteral("extensions.explore.universes"), universeData,
                          universes.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
                          universes),
            customSection(QStringLiteral("extensions.explore.search"), 1,
                          QStringLiteral("Search results"),
                          QStringLiteral("extensions.explore.cards"), data,
                          !top.error.isEmpty() ? QStringLiteral("error")
                          : top.rows.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
                          {}, top.error)
        };
    }

    const CommunityResult rising = community(QStringLiteral("rising"), {},
                                             ctx.showExplicit, installedIds);
    QVariantList starred = top.rows;
    std::sort(starred.begin(), starred.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("stars")).toInt()
             > b.toMap().value(QStringLiteral("stars")).toInt();
    });

    QVariantList out;
    out.append(customSection(QStringLiteral("extensions.explore.universes"), out.size(), QString(),
                             QStringLiteral("extensions.explore.universes"), universeData,
                             universes.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
                             universes));

    QVariantMap jobsData = summaryData(ctx.extensions);
    jobsData.insert(QStringLiteral("rows"), jobs(top.rows, ctx.extensions));
    out.append(customSection(QStringLiteral("extensions.explore.jobs"), out.size(),
                             QStringLiteral("Browse by job"),
                             QStringLiteral("extensions.explore.jobs"), jobsData,
                             top.error.isEmpty() ? QStringLiteral("ready") : QStringLiteral("error"),
                             {}, top.error));

    auto cards = [&](const QString &id, const QString &title, const QVariantList &rows,
                     const QString &error = {}) {
        QVariantMap data = summaryData(ctx.extensions);
        data.insert(QStringLiteral("rows"), rows);
        out.append(customSection(id, out.size(), title,
                                 QStringLiteral("extensions.explore.cards"), data,
                                 !error.isEmpty() ? QStringLiteral("error")
                                 : rows.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
                                 {}, error));
    };
    cards(QStringLiteral("extensions.explore.rising"), QStringLiteral("Rising"),
          take(rising.rows, 7), rising.error);
    cards(QStringLiteral("extensions.explore.starred"), QStringLiteral("Most starred"),
          take(starred, 8), top.error);
    cards(QStringLiteral("extensions.explore.subtitles"), QStringLiteral("Subtitles"),
          take(top.rows, 7, [](const QVariantMap &row) {
              return row.value(QStringLiteral("kind")).toString().contains(
                  QStringLiteral("subtitle"), Qt::CaseInsensitive);
          }), top.error);
    cards(QStringLiteral("extensions.explore.live"), QStringLiteral("Live TV"),
          take(top.rows, 7, [](const QVariantMap &row) {
              const QString hay = row.value(QStringLiteral("kind")).toString()
                  + QLatin1Char(' ') + row.value(QStringLiteral("description")).toString();
              return hay.contains(QStringLiteral("live"), Qt::CaseInsensitive);
          }), top.error);
    cards(QStringLiteral("extensions.explore.anime"), QStringLiteral("Anime"),
          take(top.rows, 7, [](const QVariantMap &row) {
              const QString hay = row.value(QStringLiteral("kind")).toString()
                  + QLatin1Char(' ') + row.value(QStringLiteral("description")).toString();
              return hay.contains(QStringLiteral("anime"), Qt::CaseInsensitive);
          }), top.error);
    return out;
}

bool validParams(const QVariantMap &params)
{
    static const QSet<QString> keys{QStringLiteral("view"), QStringLiteral("query")};
    for (auto it = params.cbegin(); it != params.cend(); ++it)
        if (!keys.contains(it.key())) return false;
    const QString view = params.value(QStringLiteral("view"), QStringLiteral("chain")).toString();
    if (!QStringList{QStringLiteral("chain"), QStringLiteral("house"),
                     QStringLiteral("explore")}.contains(view)) return false;
    const QVariant query = params.value(QStringLiteral("query"));
    return !query.isValid() || (query.metaType().id() == QMetaType::QString
                               && query.toString().size() <= 200);
}

QVariantList initial(const QVariantMap &params)
{
    const QString view = params.value(QStringLiteral("view"), QStringLiteral("chain")).toString();
    const QString schema = view == QLatin1String("chain") ? QStringLiteral("extensions.chain")
                         : view == QLatin1String("house") ? QStringLiteral("extensions.house")
                         : QStringLiteral("extensions.explore.cards");
    return {customSection(QStringLiteral("extensions.") + view, 0, QString(), schema, {},
                          QStringLiteral("loading"))};
}

QVariantList build(const FeedContext &ctx)
{
    const QString view = ctx.params.value(QStringLiteral("view"),
                                          QStringLiteral("chain")).toString();
    const QString query = ctx.params.value(QStringLiteral("query")).toString();
    if (view == QLatin1String("chain")) return chainSections(ctx, query);
    if (view == QLatin1String("house")) return houseSections(ctx, query);
    return exploreSections(ctx, query);
}

QVariantMap ok(const QVariant &result = {})
{
    QVariantMap answer{{QStringLiteral("ok"), true}};
    if (result.isValid()) answer.insert(QStringLiteral("result"), result);
    return answer;
}

QVariantMap fail(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}

ExtensionsStore *extensions(ColosseumWebBridge &bridge)
{
    return qobject_cast<ExtensionsStore *>(bridge.service(QStringLiteral("Extensions")));
}

MangaEngine *manga(ColosseumWebBridge &bridge)
{
    return qobject_cast<MangaEngine *>(bridge.service(QStringLiteral("Manga")));
}

bool validToken(const QVariantMap &payload)
{
    return payload.value(QStringLiteral("token")).metaType().id() == QMetaType::QString
        && !payload.value(QStringLiteral("token")).toString().isEmpty();
}

// Moved from ExtensionsPage.qml:99-103 setEntryEnabled().
void enableAction(ColosseumWebBridge &bridge, const QVariantMap &payload,
                  ActionRegistry::Completion completion)
{
    ExtensionsStore *store = extensions(bridge);
    const QVariantMap entry = findEntry(store, payload.value(QStringLiteral("token")).toString());
    if (!store || entry.isEmpty()) return completion(fail(QStringLiteral("That extension is no longer installed.")));
    if (entry.value(QStringLiteral("core")).toBool())
        return completion(fail(QStringLiteral("The house catalogue stays enabled.")));
    const bool on = payload.value(QStringLiteral("enabled")).toBool();
    if (entry.value(QStringLiteral("enabled")).toBool() == on) return completion(ok());
    const QString transport = entry.value(QStringLiteral("transportUrl")).toString();
    if (transport.startsWith(QLatin1String("http"), Qt::CaseInsensitive))
        store->setEnabledInstance(transport, on);
    else
        store->setEnabled(entry.value(QStringLiteral("id")).toString(), on);
    const QVariantMap after = findEntry(store, entryToken(entry));
    completion(!after.isEmpty() && after.value(QStringLiteral("enabled")).toBool() == on
        ? ok() : fail(QStringLiteral("The extension could not be changed.")));
}

bool validEnable(const QVariantMap &payload)
{
    return validToken(payload) && payload.contains(QStringLiteral("enabled"))
        && payload.value(QStringLiteral("enabled")).metaType().id() == QMetaType::Bool;
}

// Moved from ExtensionsPage.qml:105-122 askRemove().
void removeAction(ColosseumWebBridge &bridge, const QVariantMap &payload,
                  ActionRegistry::Completion completion)
{
    ExtensionsStore *store = extensions(bridge);
    const QString token = payload.value(QStringLiteral("token")).toString();
    const QVariantMap entry = findEntry(store, token);
    if (!store || entry.isEmpty()) return completion(fail(QStringLiteral("That extension is no longer installed.")));
    if (entry.value(QStringLiteral("core")).toBool())
        return completion(fail(QStringLiteral("The house catalogue cannot be removed.")));

    const QStringList worlds = worldsFor(entry);
    if (worlds.size() > 1 && !payload.value(QStringLiteral("confirm")).toBool()) {
        QStringList names;
        for (const QString &world : worlds) names.append(worldTitle(world));
        return completion(fail(QStringLiteral("%1 feeds %2. Removing it takes it out of all of them. Press Remove again to confirm.")
            .arg(nameOf(entry), names.join(QStringLiteral(" and ")))));
    }

    const QString transport = entry.value(QStringLiteral("transportUrl")).toString();
    if (transport.startsWith(QLatin1String("http"), Qt::CaseInsensitive))
        store->removeInstance(transport);
    else
        store->remove(entry.value(QStringLiteral("id")).toString());
    completion(findEntry(store, token).isEmpty()
        ? ok() : fail(QStringLiteral("The extension could not be removed.")));
}

bool validRemove(const QVariantMap &payload)
{
    return validToken(payload)
        && (!payload.contains(QStringLiteral("confirm"))
            || payload.value(QStringLiteral("confirm")).metaType().id() == QMetaType::Bool);
}

// Moved from ExtensionsPage.qml:123-143 moveWell() and ExtensionsCatalog.js:317-401.
void reorderAction(ColosseumWebBridge &bridge, const QVariantMap &payload,
                   ActionRegistry::Completion completion)
{
    ExtensionsStore *store = extensions(bridge);
    if (!store) return completion(fail(QStringLiteral("Extensions are unavailable.")));
    const QString world = payload.value(QStringLiteral("world")).toString().toLower();
    const QString token = payload.value(QStringLiteral("token")).toString();
    const int delta = payload.value(QStringLiteral("delta")).toInt();
    if (!QStringList{QStringLiteral("theatre"), QStringLiteral("tankoban"),
                     QStringLiteral("biblio")}.contains(world) || (delta != -1 && delta != 1))
        return completion(fail(QStringLiteral("That source move is invalid.")));

    const QVariantList before = store->installed();
    const QList<QVariantMap> beforeWells = wellsForWorld(before, world);
    int beforeRank = -1;
    for (int i = 0; i < beforeWells.size(); ++i)
        if (entryToken(beforeWells.at(i)) == token) { beforeRank = i; break; }
    const MoveTarget target = moveDestination(before, world, token, delta);
    if (beforeRank < 0 || target.globalIndex < 0)
        return completion(fail(QStringLiteral("That source cannot move farther in this world.")));
    const QVariantMap mover = findEntry(store, target.token);
    if (mover.isEmpty()) return completion(fail(QStringLiteral("That source is no longer installed.")));

    const QString transport = mover.value(QStringLiteral("transportUrl")).toString();
    if (transport.startsWith(QLatin1String("http"), Qt::CaseInsensitive))
        store->moveInstanceTo(transport, target.globalIndex);
    else
        store->moveTo(mover.value(QStringLiteral("id")).toString(), target.globalIndex);

    const QList<QVariantMap> afterWells = wellsForWorld(store->installed(), world);
    int afterRank = -1;
    for (int i = 0; i < afterWells.size(); ++i)
        if (entryToken(afterWells.at(i)) == token) { afterRank = i; break; }
    completion(afterRank == beforeRank + delta
        ? ok() : fail(QStringLiteral("The source order could not be changed.")));
}

bool validReorder(const QVariantMap &payload)
{
    return validToken(payload)
        && payload.value(QStringLiteral("world")).metaType().id() == QMetaType::QString
        && payload.value(QStringLiteral("delta")).canConvert<int>();
}

struct AsyncLinks {
    QMetaObject::Connection success;
    QMetaObject::Connection failure;
    bool settled = false;
};

template <typename Completion>
auto settleOnce(std::shared_ptr<AsyncLinks> links, Completion completion)
{
    return [links, completion](const QVariantMap &answer) mutable {
        if (links->settled) return;
        links->settled = true;
        QObject::disconnect(links->success);
        QObject::disconnect(links->failure);
        completion(answer);
    };
}

// Moved from ExtensionsPage.qml:200-203 installFromCard() and its install-result handlers.
void installAction(ColosseumWebBridge &bridge, const QVariantMap &payload,
                   ActionRegistry::Completion completion)
{
    ExtensionsStore *store = extensions(bridge);
    if (!store) return completion(fail(QStringLiteral("Extensions are unavailable.")));
    const QString raw = payload.value(QStringLiteral("url")).toString();
    const QString expectedId = payload.value(QStringLiteral("id")).toString();
    const QString normalized = store->normalizeUrl(raw);
    if (normalized.isEmpty()) return completion(fail(QStringLiteral("That extension address is not valid.")));

    auto links = std::make_shared<AsyncLinks>();
    auto settle = settleOnce(links, completion);
    links->success = QObject::connect(store, &ExtensionsStore::installFinished, &bridge,
        [expectedId, settle](const QString &id, const QString &name) mutable {
            if (!expectedId.isEmpty() && id != expectedId) return;
            settle(ok(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), name}}));
        });
    links->failure = QObject::connect(store, &ExtensionsStore::installFailed, &bridge,
        [normalized, settle](const QString &url, const QString &reason) mutable {
            if (url != normalized) return;
            settle(fail(reason.isEmpty() ? QStringLiteral("The extension could not be installed.") : reason));
        });
    store->install(raw);
}

bool validInstall(const QVariantMap &payload)
{
    return payload.value(QStringLiteral("url")).metaType().id() == QMetaType::QString
        && !payload.value(QStringLiteral("url")).toString().trimmed().isEmpty()
        && payload.value(QStringLiteral("id")).metaType().id() == QMetaType::QString
        && !payload.value(QStringLiteral("id")).toString().trimmed().isEmpty();
}

// The install-from-link sheet used ExtensionsStore::preview() before committing.
// Moved from ExtensionsPage.qml:1570-1636.
void previewAction(ColosseumWebBridge &bridge, const QVariantMap &payload,
                   ActionRegistry::Completion completion)
{
    ExtensionsStore *store = extensions(bridge);
    if (!store) return completion(fail(QStringLiteral("Extensions are unavailable.")));
    const QString raw = payload.value(QStringLiteral("url")).toString();
    const QString normalized = store->normalizeUrl(raw);
    if (normalized.isEmpty()) return completion(fail(QStringLiteral("Paste an extension address first.")));

    auto links = std::make_shared<AsyncLinks>();
    auto settle = settleOnce(links, completion);
    links->success = QObject::connect(store, &ExtensionsStore::previewReady, &bridge,
        [normalized, settle](const QString &url, const QVariantMap &manifest) mutable {
            if (url != normalized) return;
            QVariantMap safe{
                {QStringLiteral("id"), manifest.value(QStringLiteral("id"))},
                {QStringLiteral("name"), manifest.value(QStringLiteral("name"))},
                {QStringLiteral("description"), manifest.value(QStringLiteral("description"))},
                {QStringLiteral("configurationRequired"),
                    manifest.value(QStringLiteral("behaviorHints")).toMap()
                        .value(QStringLiteral("configurationRequired")).toBool()},
                {QStringLiteral("configurable"),
                    manifest.value(QStringLiteral("behaviorHints")).toMap()
                        .value(QStringLiteral("configurable")).toBool()}
            };
            settle(ok(safe));
        });
    links->failure = QObject::connect(store, &ExtensionsStore::previewFailed, &bridge,
        [normalized, raw, settle](const QString &url, const QString &reason) mutable {
            if (url != normalized && url != raw) return;
            settle(fail(reason.isEmpty() ? QStringLiteral("That address did not return an extension manifest.") : reason));
        });
    store->preview(raw);
}

bool validPreview(const QVariantMap &payload)
{
    return payload.value(QStringLiteral("url")).metaType().id() == QMetaType::QString
        && !payload.value(QStringLiteral("url")).toString().trimmed().isEmpty();
}

QString configureUrl(const QString &raw)
{
    QString text = raw.trimmed();
    if (text.isEmpty() || text.startsWith(QLatin1String("colosseum://"), Qt::CaseInsensitive))
        return {};
    int split = text.size();
    const int query = text.indexOf(QLatin1Char('?'));
    const int hash = text.indexOf(QLatin1Char('#'));
    if (query >= 0) split = std::min(split, query);
    if (hash >= 0) split = std::min(split, hash);
    QString path = text.left(split);
    const QString suffix = text.mid(split);
    while (path.endsWith(QLatin1Char('/'))) path.chop(1);
    if (path.endsWith(QStringLiteral("/manifest.json"), Qt::CaseInsensitive))
        path.chop(QStringLiteral("/manifest.json").size());
    path += QStringLiteral("/configure");
    const QUrl url(path + suffix);
    if (!url.isValid() || (url.scheme() != QLatin1String("http")
        && url.scheme() != QLatin1String("https")) || url.host().isEmpty()) return {};
    return url.toString();
}

QVariantMap tankoyomiState(MangaEngine *engine, ExtensionsStore *store, const QString &wanted)
{
    QVariantList languages;
    for (const QVariant &value : engine ? engine->chapterLanguages() : QVariantList{}) {
        const QVariantMap row = value.toMap();
        QVariantMap safe{
            {QStringLiteral("code"), row.value(QStringLiteral("code"))},
            {QStringLiteral("label"), row.value(QStringLiteral("label"))},
            {QStringLiteral("countryCode"), row.value(QStringLiteral("countryCode"),
                                                       row.value(QStringLiteral("flag")))}
        };
        languages.append(safe);
    }
    QString selected = wanted.trimmed().toLower();
    const QString defaultLanguage = engine ? engine->chapterDefaultLanguage() : QString();
    bool found = false;
    for (const QVariant &value : languages)
        if (value.toMap().value(QStringLiteral("code")).toString().toLower() == selected)
            found = true;
    if (!found) selected = defaultLanguage;
    if (selected.isEmpty() && !languages.isEmpty())
        selected = languages.first().toMap().value(QStringLiteral("code")).toString();

    QVariantList providers;
    if (engine && !selected.isEmpty()) {
        for (const QVariant &value : engine->chapterProviders(selected)) {
            const QVariantMap row = value.toMap();
            QVariantList hosts;
            for (const QVariant &host : row.value(QStringLiteral("allowedHosts")).toList())
                hosts.append(host.toString());
            providers.append(QVariantMap{
                {QStringLiteral("id"), row.value(QStringLiteral("id"))},
                {QStringLiteral("name"), row.value(QStringLiteral("name"))},
                {QStringLiteral("enabled"), row.value(QStringLiteral("enabled")).toBool()},
                {QStringLiteral("allowedHosts"), hosts}
            });
        }
    }
    bool master = false;
    if (store) {
        for (const QVariant &value : store->installed()) {
            const QVariantMap row = value.toMap();
            if (row.value(QStringLiteral("id")).toString()
                == QLatin1String("colosseum.well.tankoyomi")) {
                master = row.value(QStringLiteral("enabled")).toBool();
                break;
            }
        }
    }
    return {
        {QStringLiteral("mode"), QStringLiteral("tankoyomi")},
        {QStringLiteral("enabled"), master},
        {QStringLiteral("languages"), languages},
        {QStringLiteral("selectedLanguage"), selected},
        {QStringLiteral("defaultLanguage"), defaultLanguage},
        {QStringLiteral("providers"), providers}
    };
}

// Moved from ExtensionsPage.qml:171-185, 601-605, 1297-1324 and the embedded
// TankoyomiConfigurationPage.qml:75-175. Configuration remains a page action.
void configureAction(ColosseumWebBridge &bridge, const QVariantMap &payload,
                     ActionRegistry::Completion completion)
{
    ExtensionsStore *store = extensions(bridge);
    if (!store) return completion(fail(QStringLiteral("Extensions are unavailable.")));

    const QString token = payload.value(QStringLiteral("token")).toString();
    QVariantMap entry = token.isEmpty() ? QVariantMap{} : findEntry(store, token);
    const QString publicUrl = payload.value(QStringLiteral("url")).toString();
    const QString op = payload.value(QStringLiteral("op"), QStringLiteral("state")).toString();

    if (!entry.isEmpty()
        && entry.value(QStringLiteral("id")).toString() == QLatin1String("colosseum.well.tankoyomi")) {
        MangaEngine *engine = manga(bridge);
        if (!engine) return completion(fail(QStringLiteral("Tankoyomi settings are unavailable.")));
        const QString language = payload.value(QStringLiteral("language")).toString();
        bool changed = true;
        if (op == QLatin1String("master")) {
            const bool wanted = payload.value(QStringLiteral("enabled")).toBool();
            store->setEnabled(QStringLiteral("colosseum.well.tankoyomi"), wanted);
            const QVariantMap current = findEntry(store, QStringLiteral("id:colosseum.well.tankoyomi"));
            changed = !current.isEmpty()
                && current.value(QStringLiteral("enabled")).toBool() == wanted;
        } else if (op == QLatin1String("defaultLanguage")) {
            changed = engine->setChapterDefaultLanguage(language);
        } else if (op == QLatin1String("providerEnabled")) {
            changed = engine->setChapterProviderEnabled(
                language, payload.value(QStringLiteral("providerId")).toString(),
                payload.value(QStringLiteral("enabled")).toBool());
        } else if (op == QLatin1String("providerMove")) {
            const int direction = payload.value(QStringLiteral("direction")).toInt();
            changed = direction < 0
                ? engine->moveChapterProviderUp(language,
                    payload.value(QStringLiteral("providerId")).toString())
                : engine->moveChapterProviderDown(language,
                    payload.value(QStringLiteral("providerId")).toString());
        } else if (op == QLatin1String("resetOrder")) {
            changed = engine->resetChapterProviderOrder(language);
        } else if (op != QLatin1String("state")) {
            return completion(fail(QStringLiteral("That Tankoyomi setting is not supported.")));
        }
        if (!changed) return completion(fail(QStringLiteral("Tankoyomi could not save that setting.")));
        return completion(ok(tankoyomiState(engine, store, language)));
    }

    QString target;
    if (!entry.isEmpty()) {
        const QString transport = entry.value(QStringLiteral("transportUrl")).toString();
        if (transport.startsWith(QLatin1String("colosseum://"))) {
            return completion(ok(QVariantMap{
                {QStringLiteral("mode"), QStringLiteral("notice")},
                {QStringLiteral("message"), nameOf(entry)
                    + QStringLiteral(" settings arrive with its source sheet.")}
            }));
        }
        target = configureUrl(transport);
    } else if (!publicUrl.isEmpty()) {
        target = configureUrl(publicUrl);
    } else {
        return completion(fail(QStringLiteral("That extension is no longer installed.")));
    }

    if (target.isEmpty() || !QDesktopServices::openUrl(QUrl(target)))
        return completion(fail(QStringLiteral("Colosseum could not open that extension's configuration page.")));
    completion(ok(QVariantMap{{QStringLiteral("mode"), QStringLiteral("external")}}));
}

bool validConfigure(const QVariantMap &payload)
{
    const QVariant token = payload.value(QStringLiteral("token"));
    const QVariant url = payload.value(QStringLiteral("url"));
    if ((!token.isValid() || token.toString().isEmpty())
        && (!url.isValid() || url.toString().isEmpty())) return false;
    static const QSet<QString> ops{
        QStringLiteral("state"), QStringLiteral("master"),
        QStringLiteral("defaultLanguage"), QStringLiteral("providerEnabled"),
        QStringLiteral("providerMove"), QStringLiteral("resetOrder")
    };
    return ops.contains(payload.value(QStringLiteral("op"), QStringLiteral("state")).toString());
}

QMetaObject::Connection watchExtensionsChanged(QObject *object, QObject *receiver,
                                               std::function<void()> changed)
{
    auto *store = qobject_cast<ExtensionsStore *>(object);
    if (!store) return {};
    return QObject::connect(store, &ExtensionsStore::changed,
                            receiver, [changed] { changed(); });
}

QMetaObject::Connection watchExtensionsPolicy(QObject *object, QObject *receiver,
                                              std::function<void()> changed)
{
    auto *store = qobject_cast<ExtensionsStore *>(object);
    if (!store) return {};
    return QObject::connect(store, &ExtensionsStore::showExplicitChanged,
                            receiver, [changed] { changed(); });
}

const bool registeredFeed = [] {
    FeedRegistry::Entry entry;
    entry.name = QStringLiteral("page.extensions");
    entry.valid = validParams;
    entry.initial = initial;
    entry.build = build;
    entry.needsExtensions = true;
    entry.ownerSignals.append({QStringLiteral("Extensions"), watchExtensionsChanged});
    entry.ownerSignals.append({QStringLiteral("Extensions"), watchExtensionsPolicy});
    return FeedRegistry::add(entry);
}();

const bool registeredEnable = ActionRegistry::add({
    QStringLiteral("page.extensions.enable"), validEnable, enableAction
});
const bool registeredRemove = ActionRegistry::add({
    QStringLiteral("page.extensions.remove"), validRemove, removeAction
});
const bool registeredReorder = ActionRegistry::add({
    QStringLiteral("page.extensions.reorder"), validReorder, reorderAction
});
const bool registeredInstall = ActionRegistry::add({
    QStringLiteral("page.extensions.install"), validInstall, installAction
});
const bool registeredPreview = ActionRegistry::add({
    QStringLiteral("page.extensions.preview"), validPreview, previewAction
});
const bool registeredConfigure = ActionRegistry::add({
    QStringLiteral("page.extensions.configure"), validConfigure, configureAction
});

} // namespace
