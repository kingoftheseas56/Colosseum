#include "ActionRegistry.h"
#include "ContinueFeed.h"
#include "FeedHttp.h"
#include "FeedRegistry.h"
#include "FeedValue.h"
#include "../ColosseumWebBridge.h"

#include "../../ProgressStore.h"
#include "../../engine/BiblioCatalog.h"
#include "../../engine/BiblioCatalogStore.h"
#include "../../engine/ExtensionsStore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaType>
#include <QSettings>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <algorithm>
#include <functional>

namespace {

const QString kWorld = QStringLiteral("Biblio");
const QString kPrefsGroup = QStringLiteral("biblioExplore");
const QStringList kHouse{
    QStringLiteral("popular"),
    QStringLiteral("top-rated"),
    QStringLiteral("new-releases"),
    QStringLiteral("trending"),
    QStringLiteral("most-read"),
    QStringLiteral("classics")
};

struct Extra {
    QString name;
    QString label;
    QStringList options;
    bool required = false;
};

struct ExtCatalog {
    QString key;
    QString title;
    QString addonName;
    QString transportUrl;
    QString catalogId;
    QList<Extra> extras;
};

struct Prefs {
    QStringList order;
    QSet<QString> hidden;
};

QString houseTitle(const QString &key)
{
    if (key == QLatin1String("top-rated")) return QStringLiteral("Top Rated");
    if (key == QLatin1String("new-releases")) return QStringLiteral("New Releases");
    if (key == QLatin1String("trending")) return QStringLiteral("Trending");
    if (key == QLatin1String("most-read")) return QStringLiteral("Most Read");
    if (key == QLatin1String("classics")) return QStringLiteral("Classics");
    return QStringLiteral("Popular");
}

QString titleCase(QString value)
{
    if (!value.isEmpty()) value[0] = value.at(0).toUpper();
    return value;
}

QStringList asStrings(const QVariant &value)
{
    QStringList out;
    const QVariantList list = value.toList();
    if (!list.isEmpty()) {
        for (const QVariant &v : list) {
            const QString s = v.toString();
            if (!s.isEmpty()) out.append(s);
        }
        return out;
    }
    const QString scalar = value.toString();
    if (!scalar.isEmpty()) out.append(scalar);
    return out;
}

QList<Extra> extrasFor(const QVariantMap &catalog)
{
    QList<Extra> out;
    for (const QVariant &v : catalog.value(QStringLiteral("extra")).toList()) {
        const QVariantMap x = v.toMap();
        const QString name = x.value(QStringLiteral("name")).toString();
        if (name.isEmpty() || name == QLatin1String("skip") || name == QLatin1String("search"))
            continue;
        QStringList options = asStrings(x.value(QStringLiteral("options")));
        if (options.isEmpty() && name == QLatin1String("genre"))
            options = asStrings(catalog.value(QStringLiteral("genres")));
        if (!options.isEmpty())
            out.append({name, titleCase(name), options,
                        x.value(QStringLiteral("isRequired")).toBool()});
    }
    if (out.isEmpty()) {
        const QStringList genres = asStrings(catalog.value(QStringLiteral("genres")));
        if (!genres.isEmpty())
            out.append({QStringLiteral("genre"), QStringLiteral("Genre"), genres, false});
    }
    return out;
}

bool browsable(const QVariantMap &catalog)
{
    for (const QVariant &v : catalog.value(QStringLiteral("extra")).toList()) {
        const QVariantMap x = v.toMap();
        if (!x.value(QStringLiteral("isRequired")).toBool()) continue;
        const QString name = x.value(QStringLiteral("name")).toString();
        if (name == QLatin1String("skip")) continue;
        QStringList options = asStrings(x.value(QStringLiteral("options")));
        if (options.isEmpty() && name == QLatin1String("genre"))
            options = asStrings(catalog.value(QStringLiteral("genres")));
        if (name == QLatin1String("search") || options.isEmpty()) return false;
    }
    return true;
}

bool appleBooks(const QString &id, const QString &name)
{
    const QString s = (id + QLatin1Char(' ') + name).toLower();
    return s.contains(QStringLiteral("apple")) && s.contains(QStringLiteral("book"));
}

QList<ExtCatalog> extensions(const QVariantList &installed)
{
    QList<ExtCatalog> out;
    QSet<QString> seen;
    for (const QVariant &v : installed) {
        const QVariantMap ext = v.toMap();
        if (!ext.value(QStringLiteral("enabled")).toBool()
            || ext.value(QStringLiteral("core")).toBool())
            continue;
        const QVariantMap manifest = ext.value(QStringLiteral("manifest")).toMap();
        const QString extId = manifest.value(QStringLiteral("id"),
                                             ext.value(QStringLiteral("id"))).toString();
        const QString extName = manifest.value(QStringLiteral("name"), extId).toString();
        if (appleBooks(extId, extName)) continue;
        const QString transport = ext.value(QStringLiteral("transportUrl")).toString();
        if (transport.isEmpty()) continue;

        for (const QVariant &cv : manifest.value(QStringLiteral("catalogs")).toList()) {
            const QVariantMap c = cv.toMap();
            const QString id = c.value(QStringLiteral("id")).toString();
            if (c.value(QStringLiteral("type")).toString() != QLatin1String("book")
                || id.isEmpty())
                continue;
            if (c.contains(QStringLiteral("isDiscoverable"))
                && !c.value(QStringLiteral("isDiscoverable")).toBool())
                continue;
            if (!browsable(c)) continue;
            const QString key = transport + QStringLiteral("|book|") + id;
            if (seen.contains(key)) continue;
            seen.insert(key);
            out.append({key, c.value(QStringLiteral("name"), extName).toString(),
                        extName, transport, id, extrasFor(c)});
        }
    }
    return out;
}

const ExtCatalog *findExt(const QList<ExtCatalog> &list, const QString &key)
{
    for (const ExtCatalog &c : list) if (c.key == key) return &c;
    return nullptr;
}

QString extRowKey(const ExtCatalog &c)
{
    return QStringLiteral("ext:") + c.key;
}

QVariantMap nativeRoute(const QString &catalogue, const QString &axis,
                        const QString &key, bool explicitContent)
{
    QVariantMap facet{{QStringLiteral("medium"), QStringLiteral("book")},
                      {QStringLiteral("catalogue"), catalogue}};
    if (!axis.isEmpty()) facet.insert(QStringLiteral("axis"), axis);
    if (!key.isEmpty()) facet.insert(QStringLiteral("key"), key);
    return {{QStringLiteral("v"), 1},
            {QStringLiteral("world"), kWorld},
            {QStringLiteral("source"), QStringLiteral("catalogue")},
            {QStringLiteral("facet"), facet},
            {QStringLiteral("sort"), QStringLiteral("popular")},
            {QStringLiteral("explicit"), explicitContent},
            {QStringLiteral("pageSize"), 24}};
}

QVariantMap continueRoute(bool explicitContent)
{
    return {{QStringLiteral("v"), 1},
            {QStringLiteral("world"), kWorld},
            {QStringLiteral("source"), QStringLiteral("continue")},
            {QStringLiteral("explicit"), explicitContent},
            {QStringLiteral("pageSize"), 24}};
}

QVariantMap viewChoice(const QString &key, const QString &label,
                       const QVariantMap &patch, bool selected,
                       const QString &sub = {})
{
    QVariantMap out{{QStringLiteral("key"), key},
                    {QStringLiteral("label"), label},
                    {QStringLiteral("selected"), selected},
                    {QStringLiteral("target"), QVariantMap{
                         {QStringLiteral("view"), patch}}}};
    if (!sub.isEmpty()) out.insert(QStringLiteral("sublabel"), sub);
    return out;
}

QVariantMap choices(const QString &id, int index, const QString &title,
                    const QVariantList &list,
                    const QString &layout = QStringLiteral("chips"))
{
    QVariantMap section = WebFeedValue::section(
        id, index, title, layout, {},
        list.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"));
    section.insert(QStringLiteral("choices"), list);
    return section;
}

QVariantMap loading(const QString &id, int index, const QString &title,
                    const QString &layout)
{
    return WebFeedValue::section(id, index, title, layout, {}, QStringLiteral("loading"));
}

QVariantMap bookItem(const QVariantMap &row, const QString &source = {}, int rank = 0)
{
    QVariantMap copy = row;
    copy.insert(QStringLiteral("id"), row.value(QStringLiteral("canonicalId"),
                                                row.value(QStringLiteral("id"))));
    copy.insert(QStringLiteral("type"), QStringLiteral("book"));
    copy.insert(QStringLiteral("source"), source.isEmpty()
        ? row.value(QStringLiteral("source"), QStringLiteral("biblio"))
        : QVariant(source));
    copy.insert(QStringLiteral("subtitle"), row.value(QStringLiteral("author")));
    if (!copy.contains(QStringLiteral("cover")))
        copy.insert(QStringLiteral("cover"), row.value(QStringLiteral("coverUrl")));
    const QVariantMap rating = row.value(QStringLiteral("rating")).toMap();
    if (!rating.isEmpty())
        copy.insert(QStringLiteral("rating"), rating.value(QStringLiteral("average")));
    const QString published = row.value(QStringLiteral("canonicalFirstPublished")).toString();
    if (published.size() >= 4) copy.insert(QStringLiteral("year"), published.left(4).toInt());
    QVariantMap item = WebFeedValue::item(copy, kWorld, QStringLiteral("book"));
    if (!source.isEmpty()) item.insert(QStringLiteral("source"), source);
    if (rank > 0) item.insert(QStringLiteral("badge"), QString::number(rank));
    return item;
}

QVariantList bookItems(const QVariantList &rows, const QString &source = {}, bool ranked = false)
{
    QVariantList out;
    for (int i = 0; i < rows.size(); ++i) {
        const QVariantMap item = bookItem(rows.at(i).toMap(), source, ranked ? i + 1 : 0);
        if (!item.value(QStringLiteral("title")).toString().isEmpty()) out.append(item);
    }
    return out;
}

bool extensionVisible(const QVariantMap &meta, bool explicitContent)
{
    if (explicitContent) return true;
    if (meta.value(QStringLiteral("explicit")).toBool()) return false;
    if (meta.value(QStringLiteral("behaviorHints")).toMap()
            .value(QStringLiteral("adult")).toBool())
        return false;
    static const QSet<QString> tags{
        QStringLiteral("hentai"), QStringLiteral("erotica"),
        QStringLiteral("pornography"), QStringLiteral("sexually explicit"),
        QStringLiteral("adult film")};
    for (const QString &field : {QStringLiteral("genres"), QStringLiteral("subjects"),
                                 QStringLiteral("tags"), QStringLiteral("categories")}) {
        QVariantList values = meta.value(field).toList();
        if (values.isEmpty() && !meta.value(field).toString().isEmpty())
            values.append(meta.value(field));
        for (const QVariant &v : values)
            if (tags.contains(v.toString().toLower())) return false;
    }
    return true;
}

QVariantMap extensionItem(const QVariantMap &meta, const ExtCatalog &catalog)
{
    QVariantMap row{
        {QStringLiteral("id"), meta.value(QStringLiteral("id"))},
        {QStringLiteral("type"), QStringLiteral("book")},
        {QStringLiteral("title"), meta.value(QStringLiteral("title"),
            meta.value(QStringLiteral("name"), meta.value(QStringLiteral("caption"))))},
        {QStringLiteral("cover"), meta.value(QStringLiteral("cover"),
            meta.value(QStringLiteral("poster")))},
        {QStringLiteral("author"), meta.value(QStringLiteral("author"),
            meta.value(QStringLiteral("creator"), meta.value(QStringLiteral("writer"))))},
        {QStringLiteral("subtitle"), meta.value(QStringLiteral("author"),
            meta.value(QStringLiteral("creator"), meta.value(QStringLiteral("writer"))))},
        {QStringLiteral("source"), catalog.addonName}};
    const QVariant rating = meta.value(QStringLiteral("imdbRating"),
                                       meta.value(QStringLiteral("rating")));
    if (rating.isValid() && !rating.toString().isEmpty())
        row.insert(QStringLiteral("rating"), rating);
    const QVariant year = meta.value(QStringLiteral("releaseInfo"),
                                     meta.value(QStringLiteral("year")));
    if (year.isValid()) row.insert(QStringLiteral("year"), year);
    QVariantMap item = WebFeedValue::item(row, kWorld, QStringLiteral("book"));
    if (!catalog.addonName.isEmpty())
        item.insert(QStringLiteral("source"), catalog.addonName);
    return item;
}

QString enc(const QString &s)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(s));
}

QVariantMap selections(const ExtCatalog &catalog, const QVariantMap &view)
{
    QVariantMap out;
    for (const Extra &x : catalog.extras)
        if (x.required && !x.options.isEmpty()) out.insert(x.name, x.options.first());
    const QString name = view.value(QStringLiteral("extraName")).toString();
    const QString value = view.value(QStringLiteral("extraValue")).toString();
    for (const Extra &x : catalog.extras)
        if (x.name == name && x.options.contains(value)) out.insert(name, value);
    return out;
}

QUrl extensionUrl(const ExtCatalog &catalog, const QVariantMap &view)
{
    QString base = catalog.transportUrl;
    if (base.endsWith(QStringLiteral("/manifest.json"), Qt::CaseInsensitive))
        base.chop(QStringLiteral("/manifest.json").size());
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    const QVariantMap selected = selections(catalog, view);
    QStringList parts;
    for (const Extra &x : catalog.extras) {
        const QString value = selected.value(x.name).toString();
        if (!value.isEmpty()) parts.append(x.name + QLatin1Char('=') + enc(value));
    }
    QString url = base + QStringLiteral("/catalog/book/") + enc(catalog.catalogId);
    if (!parts.isEmpty()) url += QLatin1Char('/') + parts.join(QLatin1Char('&'));
    return QUrl(url + QStringLiteral(".json"));
}

QVariantList fetchExtension(const ExtCatalog &catalog, const QVariantMap &view,
                            bool explicitContent)
{
    const WebFeedHttp::Reply reply = WebFeedHttp::request(extensionUrl(catalog, view));
    if (!reply.ok || !reply.json.isObject()) return {};
    QVariantList out;
    for (const QJsonValue &value : reply.json.object().value(QStringLiteral("metas")).toArray()) {
        const QVariantMap meta = value.toObject().toVariantMap();
        if (!extensionVisible(meta, explicitContent)) continue;
        const QVariantMap item = extensionItem(meta, catalog);
        if (!item.value(QStringLiteral("title")).toString().isEmpty()) out.append(item);
        if (out.size() >= 100) break;
    }
    return out;
}

QStringList decodeList(const QString &raw)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &error);
    QStringList out;
    if (error.error != QJsonParseError::NoError || !doc.isArray()) return out;
    for (const QJsonValue &v : doc.array())
        if (v.isString() && !v.toString().isEmpty()) out.append(v.toString());
    return out;
}

QString encodeList(const QStringList &list)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(list))
                                 .toJson(QJsonDocument::Compact));
}

Prefs loadPrefs()
{
    QSettings s;
    s.beginGroup(kPrefsGroup);
    Prefs p;
    p.order = decodeList(s.value(QStringLiteral("orderJson"),
                                 QStringLiteral("[]")).toString());
    for (const QString &key : decodeList(s.value(QStringLiteral("hiddenJson"),
                                                  QStringLiteral("[]")).toString()))
        p.hidden.insert(key);
    s.endGroup();
    return p;
}

void savePrefs(const Prefs &p)
{
    QSettings s;
    s.beginGroup(kPrefsGroup);
    QStringList hidden = p.hidden.values();
    std::sort(hidden.begin(), hidden.end());
    s.setValue(QStringLiteral("orderJson"), encodeList(p.order));
    s.setValue(QStringLiteral("hiddenJson"), encodeList(hidden));
    s.endGroup();
    s.sync();
}

QStringList defaultRows(const QList<ExtCatalog> &exts)
{
    QStringList rows{QStringLiteral("top-10")};
    for (const ExtCatalog &c : exts) rows.append(extRowKey(c));
    rows.append(kHouse);
    return rows;
}

QStringList effectiveOrder(const QStringList &available, const QStringList &saved)
{
    QStringList out;
    QSet<QString> seen;
    for (const QString &key : saved) {
        if (available.contains(key) && !seen.contains(key)) {
            seen.insert(key);
            out.append(key);
        }
    }
    for (const QString &key : available) {
        if (!seen.contains(key)) {
            seen.insert(key);
            out.append(key);
        }
    }
    return out;
}

void addPref(QVariantMap &section, bool hidden)
{
    section.insert(QStringLiteral("pref"), QVariantMap{
        {QStringLiteral("movable"), true},
        {QStringLiteral("hidden"), hidden},
        {QStringLiteral("label"), section.value(QStringLiteral("title")).toString()},
        {QStringLiteral("renamable"), false}});
}

bool validView(const QString &tab, const QVariantMap &view)
{
    auto bounded = [&view](const QString &key, int max) {
        return !view.contains(key) || view.value(key).toString().size() <= max;
    };
    if (tab == QLatin1String("discover")) {
        static const QSet<QString> allowed{
            QStringLiteral("catalogue"), QStringLiteral("facetAxis"),
            QStringLiteral("facetKey"), QStringLiteral("extraName"),
            QStringLiteral("extraValue")};
        for (auto it = view.cbegin(); it != view.cend(); ++it)
            if (!allowed.contains(it.key())) return false;
        return bounded(QStringLiteral("catalogue"), 2048)
            && bounded(QStringLiteral("facetAxis"), 128)
            && bounded(QStringLiteral("facetKey"), 256)
            && bounded(QStringLiteral("extraName"), 128)
            && bounded(QStringLiteral("extraValue"), 256);
    }
    if (tab == QLatin1String("explore")) {
        for (auto it = view.cbegin(); it != view.cend(); ++it)
            if (it.key() != QLatin1String("customize")) return false;
        return !view.contains(QStringLiteral("customize"))
            || view.value(QStringLiteral("customize")).metaType().id() == QMetaType::Bool;
    }
    if (tab == QLatin1String("library")) {
        static const QSet<QString> allowed{
            QStringLiteral("query"), QStringLiteral("filter"), QStringLiteral("sort")};
        for (auto it = view.cbegin(); it != view.cend(); ++it)
            if (!allowed.contains(it.key())) return false;
        const QString filter = view.value(QStringLiteral("filter")).toString();
        const QString sort = view.value(QStringLiteral("sort")).toString();
        return bounded(QStringLiteral("query"), 256)
            && (filter.isEmpty() || filter == QLatin1String("inProgress"))
            && (sort.isEmpty() || sort == QLatin1String("added")
                || sort == QLatin1String("lastRead") || sort == QLatin1String("az"));
    }
    return false;
}

QVariantList initial(const QVariantMap &params)
{
    const QString tab = params.value(QStringLiteral("tab")).toString();
    QVariantList out{
        loading(QStringLiteral("biblio.chrome.featured"), 0,
                QStringLiteral("Featured in Biblio"), QStringLiteral("hero")),
        loading(QStringLiteral("biblio.chrome.continue"), 1,
                QStringLiteral("Continue Reading"), QStringLiteral("continue"))};
    if (tab == QLatin1String("discover")) {
        out.append(loading(QStringLiteral("biblio.discover.catalogues"), out.size(),
                           QStringLiteral("Catalogues"), QStringLiteral("chips")));
        out.append(loading(QStringLiteral("biblio.discover.results"), out.size(),
                           QStringLiteral("Popular"), QStringLiteral("grid")));
    } else if (tab == QLatin1String("explore")) {
        out.append(loading(QStringLiteral("biblio.explore.controls"), out.size(),
                           QString(), QStringLiteral("chips")));
        out.append(loading(QStringLiteral("biblio.explore.top-10"), out.size(),
                           QStringLiteral("Top 10"), QStringLiteral("rail")));
        out.append(loading(QStringLiteral("biblio.explore.mosaics"), out.size(),
                           QString(), QStringLiteral("tiles")));
    } else {
        out.append(loading(QStringLiteral("biblio.library.filters"), out.size(),
                           QStringLiteral("Filter"), QStringLiteral("chips")));
        out.append(loading(QStringLiteral("biblio.library.sort"), out.size(),
                           QStringLiteral("Sort"), QStringLiteral("chips")));
        out.append(loading(QStringLiteral("biblio.library.saved"), out.size(),
                           QStringLiteral("Library"), QStringLiteral("grid")));
    }
    return out;
}

void appendChrome(QVariantList &out, BiblioCatalogStore &store, bool ready,
                  const FeedContext &ctx)
{
    QVariantList featured;
    if (ready) {
        const QVariantMap page = store.page(QStringLiteral("popular"), {}, {},
                                            ctx.showExplicit, 0, 4);
        for (const QVariant &value : page.value(QStringLiteral("items")).toList()) {
            const QVariantMap row = value.toMap();
            QVariantMap item = bookItem(row);
            if (item.value(QStringLiteral("title")).toString().isEmpty()) continue;
            const QString author = row.value(QStringLiteral("author")).toString();
            item.insert(QStringLiteral("subtitle"), author.isEmpty()
                ? QStringLiteral("Popular on Biblio.")
                : QStringLiteral("By %1.").arg(author));
            featured.append(item);
        }
    }
    out.append(WebFeedValue::section(
        QStringLiteral("biblio.chrome.featured"), out.size(),
        QStringLiteral("Featured in Biblio"), QStringLiteral("hero"), featured,
        ready ? (featured.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"))
              : QStringLiteral("loading")));

    QVariantMap cont;
    const QVariantList built = ContinueFeed::build(ctx.recent, kWorld, ctx.paths.imdb, 12);
    if (!built.isEmpty()) cont = built.first().toMap();
    if (cont.isEmpty())
        cont = WebFeedValue::section(QStringLiteral("biblio.chrome.continue"), out.size(),
            QStringLiteral("Continue Reading"), QStringLiteral("continue"), {},
            QStringLiteral("empty"));
    cont.insert(QStringLiteral("id"), QStringLiteral("biblio.chrome.continue"));
    cont.insert(QStringLiteral("index"), out.size());
    cont.insert(QStringLiteral("title"), QStringLiteral("Continue Reading"));
    cont.insert(QStringLiteral("hasMore"), false);
    cont.insert(QStringLiteral("seeAll"), QVariantMap{
        {QStringLiteral("route"), continueRoute(ctx.showExplicit)}});
    out.append(cont);
}

void canonicalFacet(BiblioCatalogStore &store, bool explicitContent, QVariantMap &view)
{
    const QString axis = view.value(QStringLiteral("facetAxis")).toString();
    const QString key = view.value(QStringLiteral("facetKey")).toString();
    if (axis.isEmpty() || key.isEmpty()) {
        view.remove(QStringLiteral("facetAxis"));
        view.remove(QStringLiteral("facetKey"));
        return;
    }
    for (const QVariant &gv : store.filterGroups(explicitContent)) {
        const QVariantMap group = gv.toMap();
        if (group.value(QStringLiteral("axis")).toString() != axis) continue;
        for (const QVariant &fv : group.value(QStringLiteral("facets")).toList())
            if (fv.toMap().value(QStringLiteral("key")).toString() == key) return;
    }
    view.remove(QStringLiteral("facetAxis"));
    view.remove(QStringLiteral("facetKey"));
}

void discover(QVariantList &out, BiblioCatalogStore &store, bool ready,
              const QList<ExtCatalog> &exts, const FeedContext &ctx, QVariantMap view)
{
    const QString requestedCatalogue = view.value(QStringLiteral("catalogue")).toString();
    QString catalogue = requestedCatalogue.isEmpty()
        ? QStringLiteral("popular") : requestedCatalogue;
    const ExtCatalog *ext = findExt(exts, catalogue);
    const bool missingCatalogue = !requestedCatalogue.isEmpty()
        && !kHouse.contains(requestedCatalogue) && !ext;
    if (missingCatalogue) catalogue = QStringLiteral("popular");
    view.insert(QStringLiteral("catalogue"), catalogue);

    QVariantList catalogues;
    for (const QString &key : kHouse)
        catalogues.append(viewChoice(QStringLiteral("biblio:catalogue:") + key,
            houseTitle(key),
            {{QStringLiteral("catalogue"), key},
             {QStringLiteral("facetAxis"), QString()},
             {QStringLiteral("facetKey"), QString()},
             {QStringLiteral("extraName"), QString()},
             {QStringLiteral("extraValue"), QString()}},
            catalogue == key, QStringLiteral("Biblio built-in catalogue")));
    for (const ExtCatalog &c : exts)
        catalogues.append(viewChoice(QStringLiteral("biblio:catalogue:ext:") + c.key,
            c.title,
            {{QStringLiteral("catalogue"), c.key},
             {QStringLiteral("facetAxis"), QString()},
             {QStringLiteral("facetKey"), QString()},
             {QStringLiteral("extraName"), QString()},
             {QStringLiteral("extraValue"), QString()}},
            catalogue == c.key, c.addonName));
    out.append(choices(QStringLiteral("biblio.discover.catalogues"), out.size(),
                       QStringLiteral("Catalogues"), catalogues));

    if (ext) {
        const QString activeName = view.value(QStringLiteral("extraName")).toString();
        const QString activeValue = view.value(QStringLiteral("extraValue")).toString();
        for (const Extra &x : ext->extras) {
            QVariantList opts;
            if (!x.required)
                opts.append(viewChoice(QStringLiteral("biblio:extra:") + x.name + QStringLiteral(":all"),
                    QStringLiteral("All"),
                    {{QStringLiteral("extraName"), QString()},
                     {QStringLiteral("extraValue"), QString()}},
                    activeName.isEmpty()));
            for (const QString &value : x.options)
                opts.append(viewChoice(QStringLiteral("biblio:extra:") + x.name + QLatin1Char(':') + value,
                    value,
                    {{QStringLiteral("extraName"), x.name},
                     {QStringLiteral("extraValue"), value}},
                    activeName == x.name && activeValue == value));
            out.append(choices(QStringLiteral("biblio.discover.extra.") + x.name,
                               out.size(), x.label, opts));
        }
        out.append(loading(QStringLiteral("biblio.discover.results"), out.size(),
                           ext->title, QStringLiteral("grid")));
        return;
    }

    if (!ready) {
        out.append(loading(QStringLiteral("biblio.discover.results"), out.size(),
                           houseTitle(catalogue), QStringLiteral("grid")));
        return;
    }

    canonicalFacet(store, ctx.showExplicit, view);
    const QString activeAxis = view.value(QStringLiteral("facetAxis")).toString();
    const QString activeKey = view.value(QStringLiteral("facetKey")).toString();
    QVariantList filters;
    filters.append(viewChoice(QStringLiteral("biblio:facet:all"), QStringLiteral("All"),
        {{QStringLiteral("facetAxis"), QString()}, {QStringLiteral("facetKey"), QString()}},
        activeAxis.isEmpty()));
    for (const QVariant &gv : store.filterGroups(ctx.showExplicit)) {
        const QVariantMap group = gv.toMap();
        const QString axis = group.value(QStringLiteral("axis")).toString();
        const QString groupLabel = group.value(QStringLiteral("label"), axis).toString();
        for (const QVariant &fv : group.value(QStringLiteral("facets")).toList()) {
            const QVariantMap facet = fv.toMap();
            const QString key = facet.value(QStringLiteral("key")).toString();
            filters.append(viewChoice(QStringLiteral("biblio:facet:") + axis + QLatin1Char(':') + key,
                facet.value(QStringLiteral("label"), key).toString(),
                {{QStringLiteral("facetAxis"), axis}, {QStringLiteral("facetKey"), key}},
                activeAxis == axis && activeKey == key, groupLabel));
        }
    }
    out.append(choices(QStringLiteral("biblio.discover.filters"), out.size(),
                       QStringLiteral("Filter"), filters));

    const QVariantMap page = store.page(catalogue, activeAxis, activeKey,
                                        ctx.showExplicit, 0, 100);
    const QVariantList rows = page.value(QStringLiteral("items")).toList();
    QVariantMap section = WebFeedValue::section(
        QStringLiteral("biblio.discover.results"), out.size(), houseTitle(catalogue),
        QStringLiteral("grid"), bookItems(rows),
        rows.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"));
    if (rows.isEmpty()) {
        section.insert(QStringLiteral("emptyTitle"),
            activeKey.isEmpty() ? QStringLiteral("This catalogue answered with nothing.")
                                : QStringLiteral("No books match this filter."));
    }
    QString warning = page.value(QStringLiteral("warning")).toString();
    if (missingCatalogue)
        warning = QStringLiteral("That source is no longer available — showing the built-in catalogue instead.");
    if (!warning.isEmpty()) section.insert(QStringLiteral("error"), warning);
    out.append(section);
}

void explore(QVariantList &out, BiblioCatalogStore &store, bool ready,
             const QList<ExtCatalog> &exts, const FeedContext &ctx, const QVariantMap &view)
{
    const bool customize = view.value(QStringLiteral("customize")).toBool();
    out.append(choices(QStringLiteral("biblio.explore.controls"), out.size(), QString(),
        {viewChoice(QStringLiteral("biblio:explore:customize"),
                    customize ? QStringLiteral("Done") : QStringLiteral("Customize shelves"),
                    {{QStringLiteral("customize"), !customize}}, customize)}));

    if (!ready) {
        out.append(loading(QStringLiteral("biblio.explore.unavailable"), out.size(),
                           QStringLiteral("Explore"), QStringLiteral("list")));
        return;
    }

    const Prefs prefs = loadPrefs();
    for (const QString &rowKey : effectiveOrder(defaultRows(exts), prefs.order)) {
        const bool hidden = prefs.hidden.contains(rowKey);
        if (hidden && !customize) continue;

        if (rowKey == QLatin1String("top-10")) {
            const QVariantList rows = store.top10(10, ctx.showExplicit);
            QVariantMap section = WebFeedValue::section(
                QStringLiteral("biblio.explore.top-10"), out.size(), QStringLiteral("Top 10"),
                QStringLiteral("rail"),
                bookItems(rows, QStringLiteral("Apple Books · Open Library"), true),
                rows.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"));
            section.insert(QStringLiteral("seeAll"), QVariantMap{
                {QStringLiteral("route"), nativeRoute(QStringLiteral("popular"), {}, {},
                                                      ctx.showExplicit)}});
            addPref(section, hidden);
            out.append(section);
            continue;
        }

        if (rowKey.startsWith(QLatin1String("ext:"))) {
            const ExtCatalog *c = findExt(exts, rowKey.mid(4));
            if (!c) continue;
            QVariantMap section = loading(QStringLiteral("biblio.explore.extension.") + c->key,
                                          out.size(), c->title, QStringLiteral("rail"));
            addPref(section, hidden);
            out.append(section);
            continue;
        }

        if (kHouse.contains(rowKey)) {
            const QVariantMap page = store.page(rowKey, {}, {}, ctx.showExplicit, 0, 20);
            const QVariantList rows = page.value(QStringLiteral("items")).toList();
            const QString source = rowKey == QLatin1String("most-read")
                    || rowKey == QLatin1String("classics")
                ? QStringLiteral("Open Library")
                : QStringLiteral("Apple Books · Open Library");
            QVariantMap section = WebFeedValue::section(
                QStringLiteral("biblio.explore.") + rowKey, out.size(), houseTitle(rowKey),
                QStringLiteral("rail"), bookItems(rows, source),
                rows.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"));
            section.insert(QStringLiteral("seeAll"), QVariantMap{
                {QStringLiteral("route"), nativeRoute(rowKey, {}, {}, ctx.showExplicit)}});
            addPref(section, hidden);
            out.append(section);
        }
    }

    struct Mosaic { const char *key; const char *title; const char *axis; const char *facet; };
    const Mosaic specs[] = {
        {"mosaic-fiction", "Fiction", "genre", "literary-fiction"},
        {"mosaic-nonfiction", "Nonfiction", "genre", "nonfiction"},
        {"mosaic-audience", "Audience", "audience", "young-adult"}};
    QVariantList tiles;
    for (const Mosaic &m : specs) {
        const QString axis = QString::fromLatin1(m.axis);
        const QString facet = QString::fromLatin1(m.facet);
        const QVariantList rows = store.page(QStringLiteral("popular"), axis, facet,
                                             ctx.showExplicit, 0, 14)
                                      .value(QStringLiteral("items")).toList();
        if (rows.isEmpty()) continue;
        QVariantMap tile{{QStringLiteral("key"), QStringLiteral("biblio:mosaic:")
                                                + QString::fromLatin1(m.key)},
                         {QStringLiteral("label"), QString::fromLatin1(m.title)},
                         {QStringLiteral("target"), QVariantMap{
                              {QStringLiteral("route"), nativeRoute(QStringLiteral("popular"),
                                  axis, facet, ctx.showExplicit)}}}};
        const QString art = rows.first().toMap().value(QStringLiteral("coverUrl")).toString();
        if (!art.isEmpty()) tile.insert(QStringLiteral("art"), art);
        QVariantList artList;
        for (int i = 0; i < rows.size() && i < 7; ++i) {
            const QString cover = rows.at(i).toMap().value(QStringLiteral("coverUrl")).toString();
            if (!cover.isEmpty()) artList.append(cover);
        }
        if (!artList.isEmpty()) tile.insert(QStringLiteral("artList"), artList);
        tiles.append(tile);
    }
    out.append(choices(QStringLiteral("biblio.explore.mosaics"), out.size(),
                       QString(), tiles, QStringLiteral("tiles")));
}

QVariantMap progressFor(const QVariantMap &entry, const QVariantList &recent)
{
    const QString id = entry.value(QStringLiteral("id")).toString();
    for (const QVariant &v : recent) {
        const QVariantMap p = v.toMap();
        if (p.value(QStringLiteral("kind")).toString() != QLatin1String("book")) continue;
        if (p.value(QStringLiteral("id")).toString() == id
            || p.value(QStringLiteral("workId")).toString() == id
            || p.value(QStringLiteral("bookId")).toString() == id)
            return p;
    }
    return {};
}

QVariantMap libraryRow(const QVariantMap &entry, const QVariantList &recent)
{
    QVariantMap row = entry;
    const QVariantMap book = entry.value(QStringLiteral("payload")).toMap()
                                      .value(QStringLiteral("book")).toMap();
    if (row.value(QStringLiteral("title")).toString().isEmpty())
        row.insert(QStringLiteral("title"), book.value(QStringLiteral("title")));
    if (row.value(QStringLiteral("title")).toString().isEmpty())
        row.insert(QStringLiteral("title"), QStringLiteral("Untitled"));
    if (row.value(QStringLiteral("cover")).toString().isEmpty())
        row.insert(QStringLiteral("cover"), book.value(QStringLiteral("cover")));
    QString author = row.value(QStringLiteral("author")).toString();
    if (author.isEmpty()) author = book.value(QStringLiteral("author")).toString();
    row.insert(QStringLiteral("author"), author);
    row.insert(QStringLiteral("subtitle"), author);
    row.insert(QStringLiteral("source"), QStringLiteral("biblio"));
    row.insert(QStringLiteral("type"), QStringLiteral("book"));
    row.insert(QStringLiteral("_addedAt"), entry.value(QStringLiteral("addedAt")));
    const QVariantMap p = progressFor(entry, recent);
    row.insert(QStringLiteral("_lastReadAt"),
               p.value(QStringLiteral("updatedAt"), entry.value(QStringLiteral("addedAt"))));
    if (!p.isEmpty()) {
        row.insert(QStringLiteral("progress"), p.value(QStringLiteral("progress")));
        if (p.contains(QStringLiteral("resume")))
            row.insert(QStringLiteral("resume"), p.value(QStringLiteral("resume")));
    }
    return row;
}

QVariantList filterLibrary(const QVariantList &rows, const QVariantMap &view)
{
    const QString q = view.value(QStringLiteral("query")).toString().trimmed().toLower();
    const QString filter = view.value(QStringLiteral("filter")).toString();
    const QString sort = view.value(QStringLiteral("sort"),
                                    QStringLiteral("added")).toString();
    QVariantList out;
    for (const QVariant &v : rows) {
        const QVariantMap row = v.toMap();
        if (filter == QLatin1String("inProgress")
            && !(row.value(QStringLiteral("progress")).toDouble() > 0.0))
            continue;
        if (!q.isEmpty()
            && !row.value(QStringLiteral("title")).toString().toLower().contains(q)
            && !row.value(QStringLiteral("author")).toString().toLower().contains(q))
            continue;
        out.append(row);
    }
    std::stable_sort(out.begin(), out.end(), [sort](const QVariant &a, const QVariant &b) {
        const QVariantMap l = a.toMap(), r = b.toMap();
        if (sort == QLatin1String("az"))
            return l.value(QStringLiteral("title")).toString().toLower()
                < r.value(QStringLiteral("title")).toString().toLower();
        if (sort == QLatin1String("lastRead")) {
            const qlonglong lt = l.value(QStringLiteral("_lastReadAt")).toLongLong();
            const qlonglong rt = r.value(QStringLiteral("_lastReadAt")).toLongLong();
            if (lt != rt) return lt > rt;
        }
        return l.value(QStringLiteral("_addedAt")).toLongLong()
            > r.value(QStringLiteral("_addedAt")).toLongLong();
    });
    return out;
}

QVariantMap libraryItem(QVariantMap row)
{
    const QVariantMap resume = row.value(QStringLiteral("resume")).toMap();
    const bool canResume = !resume.value(QStringLiteral("path")).toString().isEmpty()
        || !resume.value(QStringLiteral("book")).toMap().isEmpty();
    row.remove(QStringLiteral("_addedAt"));
    row.remove(QStringLiteral("_lastReadAt"));
    QVariantMap item = WebFeedValue::item(row, kWorld, QStringLiteral("book"));
    item.insert(QStringLiteral("primary"),
                canResume ? QStringLiteral("resume") : QStringLiteral("details"));

    // BiblioLibraryPage.qml:169-186. The old card menu exposes Resume only
    // when a proven reopen payload exists, plus Details and Remove always.
    QVariantList menu;
    if (canResume)
        menu.append(QVariantMap{{QStringLiteral("key"), QStringLiteral("resume")},
            {QStringLiteral("label"), QStringLiteral("Resume")},
            {QStringLiteral("target"), QVariantMap{{QStringLiteral("intent"),
                QStringLiteral("resume")}}}});
    menu.append(QVariantMap{{QStringLiteral("key"), QStringLiteral("detail")},
        {QStringLiteral("label"), QStringLiteral("Details")},
        {QStringLiteral("target"), QVariantMap{{QStringLiteral("intent"),
            QStringLiteral("details")}}}});
    menu.append(QVariantMap{{QStringLiteral("key"), QStringLiteral("remove")},
        {QStringLiteral("label"), QStringLiteral("Remove from Library")},
        {QStringLiteral("warn"), true},
        {QStringLiteral("target"), QVariantMap{{QStringLiteral("act"),
            QStringLiteral("collection.remove")}}}});
    item.insert(QStringLiteral("menu"), menu);
    return item;
}

void library(QVariantList &out, const FeedContext &ctx, QVariantMap view)
{
    if (view.value(QStringLiteral("sort")).toString().isEmpty())
        view.insert(QStringLiteral("sort"), QStringLiteral("added"));
    QVariantList rows;
    int inProgress = 0;
    for (const QVariant &v : ctx.collection) {
        const QVariantMap entry = v.toMap();
        if (entry.value(QStringLiteral("id")).toString().isEmpty()) continue;
        const QVariantMap row = libraryRow(entry, ctx.recent);
        if (row.value(QStringLiteral("progress")).toDouble() > 0.0) ++inProgress;
        rows.append(row);
    }
    const QString filter = view.value(QStringLiteral("filter")).toString();
    const QString sort = view.value(QStringLiteral("sort")).toString();
    out.append(choices(QStringLiteral("biblio.library.filters"), out.size(),
        QStringLiteral("Filter"),
        {viewChoice(QStringLiteral("biblio:library:filter:all"), QStringLiteral("All"),
                    {{QStringLiteral("filter"), QString()}}, filter.isEmpty()),
         viewChoice(QStringLiteral("biblio:library:filter:progress"), QStringLiteral("In Progress"),
                    {{QStringLiteral("filter"), QStringLiteral("inProgress")}},
                    filter == QLatin1String("inProgress"))}));
    out.append(choices(QStringLiteral("biblio.library.sort"), out.size(),
        QStringLiteral("Sort"),
        {viewChoice(QStringLiteral("biblio:library:sort:added"), QStringLiteral("Recently added"),
                    {{QStringLiteral("sort"), QStringLiteral("added")}},
                    sort == QLatin1String("added")),
         viewChoice(QStringLiteral("biblio:library:sort:last"), QStringLiteral("Last read"),
                    {{QStringLiteral("sort"), QStringLiteral("lastRead")}},
                    sort == QLatin1String("lastRead")),
         viewChoice(QStringLiteral("biblio:library:sort:az"), QStringLiteral("A–Z"),
                    {{QStringLiteral("sort"), QStringLiteral("az")}},
                    sort == QLatin1String("az"))}));
    const QVariantList filtered = filterLibrary(rows, view);
    QVariantList items;
    for (const QVariant &v : filtered) items.append(libraryItem(v.toMap()));
    QVariantMap section = WebFeedValue::section(
        QStringLiteral("biblio.library.saved"), out.size(), QStringLiteral("Library"),
        QStringLiteral("grid"), items,
        items.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"));
    if (rows.isEmpty()) {
        section.insert(QStringLiteral("emptyTitle"), QStringLiteral("Your library is empty"));
        section.insert(QStringLiteral("emptyText"),
                       QStringLiteral("Save a book with + Library — it lands here."));
    } else if (items.isEmpty()) {
        section.insert(QStringLiteral("emptyTitle"), QStringLiteral("Nothing matches"));
    }
    out.append(section);
}

QVariantList build(const FeedContext &ctx)
{
    const QString tab = ctx.params.value(QStringLiteral("tab")).toString();
    const QVariantMap view = ctx.params.value(QStringLiteral("view")).toMap();
    const QList<ExtCatalog> exts = extensions(ctx.extensions);
    BiblioCatalogStore store;
    const bool ready = store.open(ctx.paths.biblio);
    QVariantList out;
    appendChrome(out, store, ready, ctx);
    if (tab == QLatin1String("discover")) discover(out, store, ready, exts, ctx, view);
    else if (tab == QLatin1String("explore")) explore(out, store, ready, exts, ctx, view);
    else library(out, ctx, view);
    return out;
}

QVariantList enrich(const FeedContext &ctx)
{
    const QString tab = ctx.params.value(QStringLiteral("tab")).toString();
    if (tab != QLatin1String("discover") && tab != QLatin1String("explore"))
        return ctx.baseSections;
    const QList<ExtCatalog> exts = extensions(ctx.extensions);
    QVariantList out = ctx.baseSections;
    const QVariantMap view = ctx.params.value(QStringLiteral("view")).toMap();

    if (tab == QLatin1String("discover")) {
        const ExtCatalog *ext = findExt(exts, view.value(QStringLiteral("catalogue")).toString());
        if (!ext) return out;
        const QVariantList items = fetchExtension(*ext, view, ctx.showExplicit);
        for (int i = 0; i < out.size(); ++i) {
            QVariantMap section = out.at(i).toMap();
            if (section.value(QStringLiteral("id")).toString()
                != QLatin1String("biblio.discover.results"))
                continue;
            section.insert(QStringLiteral("items"), items);
            section.insert(QStringLiteral("state"), items.isEmpty()
                ? QStringLiteral("empty") : QStringLiteral("ready"));
            if (items.isEmpty()) {
                const bool filtered = !view.value(QStringLiteral("extraName")).toString().isEmpty()
                    && !view.value(QStringLiteral("extraValue")).toString().isEmpty();
                section.insert(QStringLiteral("emptyTitle"),
                    filtered ? QStringLiteral("No books match this filter.")
                             : QStringLiteral("This catalogue answered with nothing."));
            }
            out[i] = section;
            break;
        }
        return out;
    }

    for (const ExtCatalog &ext : exts) {
        const QString id = QStringLiteral("biblio.explore.extension.") + ext.key;
        int index = -1;
        for (int i = 0; i < out.size(); ++i)
            if (out.at(i).toMap().value(QStringLiteral("id")).toString() == id) {
                index = i;
                break;
            }
        if (index < 0) continue;
        const QVariantList items = fetchExtension(ext, {}, ctx.showExplicit);
        if (items.isEmpty()) {
            out.removeAt(index);
            for (int i = index; i < out.size(); ++i) {
                QVariantMap shifted = out.at(i).toMap();
                shifted.insert(QStringLiteral("index"), i);
                out[i] = shifted;
            }
            continue;
        }
        QVariantMap section = out.at(index).toMap();
        section.insert(QStringLiteral("items"), items);
        section.insert(QStringLiteral("state"), QStringLiteral("ready"));
        out[index] = section;
    }
    return out;
}

QMetaObject::Connection bindProgress(QObject *owner, QObject *receiver,
                                     std::function<void()> refresh)
{
    auto *p = qobject_cast<ProgressStore *>(owner);
    return p ? QObject::connect(p, &ProgressStore::changed, receiver,
                                [refresh] { refresh(); })
             : QMetaObject::Connection{};
}

QMetaObject::Connection bindRevision(QObject *owner, QObject *receiver,
                                     std::function<void()> refresh)
{
    auto *c = qobject_cast<BiblioCatalog *>(owner);
    return c ? QObject::connect(c, &BiblioCatalog::revisionChanged, receiver,
                                [refresh] { refresh(); })
             : QMetaObject::Connection{};
}

QMetaObject::Connection bindReady(QObject *owner, QObject *receiver,
                                  std::function<void()> refresh)
{
    auto *c = qobject_cast<BiblioCatalog *>(owner);
    return c ? QObject::connect(c, &BiblioCatalog::readyChanged, receiver,
                                [refresh] { refresh(); })
             : QMetaObject::Connection{};
}

bool rowPayload(const QVariantMap &payload)
{
    const QVariantMap params = payload.value(QStringLiteral("params")).toMap();
    return payload.value(QStringLiteral("feed")).toString() == QLatin1String("world")
        && params.value(QStringLiteral("world")).toString() == kWorld
        && params.value(QStringLiteral("tab")).toString() == QLatin1String("explore")
        && payload.value(QStringLiteral("sectionId")).toString()
               .startsWith(QLatin1String("biblio.explore."));
}

QString prefKey(const QString &section)
{
    if (section == QLatin1String("biblio.explore.top-10")) return QStringLiteral("top-10");
    const QString extPrefix = QStringLiteral("biblio.explore.extension.");
    if (section.startsWith(extPrefix)) return QStringLiteral("ext:") + section.mid(extPrefix.size());
    const QString prefix = QStringLiteral("biblio.explore.");
    if (section.startsWith(prefix)) {
        const QString key = section.mid(prefix.size());
        if (kHouse.contains(key)) return key;
    }
    return {};
}

QStringList liveRows(ColosseumWebBridge &bridge)
{
    auto *store = qobject_cast<ExtensionsStore *>(bridge.service(QStringLiteral("Extensions")));
    return defaultRows(store ? extensions(store->installed()) : QList<ExtCatalog>{});
}

void moveRow(ColosseumWebBridge &bridge, const QVariantMap &payload,
             ActionRegistry::Completion done)
{
    const QString key = prefKey(payload.value(QStringLiteral("sectionId")).toString());
    const int dir = payload.value(QStringLiteral("dir")).toInt();
    if (key.isEmpty() || (dir != -1 && dir != 1)) {
        done({{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("Shelf cannot be moved.")}});
        return;
    }
    Prefs prefs = loadPrefs();
    QStringList order = effectiveOrder(liveRows(bridge), prefs.order);
    const int from = order.indexOf(key);
    const int last = qMax(0, static_cast<int>(order.size()) - 1);
    const int to = qBound(0, from + dir, last);
    if (from >= 0 && from != to) {
        order.move(from, to);
        prefs.order = order;
        savePrefs(prefs);
    }
    done({{QStringLiteral("ok"), true}});
}

void hideRow(ColosseumWebBridge &, const QVariantMap &payload,
             ActionRegistry::Completion done)
{
    const QString key = prefKey(payload.value(QStringLiteral("sectionId")).toString());
    if (key.isEmpty()) {
        done({{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("Shelf cannot be hidden.")}});
        return;
    }
    Prefs prefs = loadPrefs();
    if (payload.value(QStringLiteral("hidden")).toBool()) prefs.hidden.insert(key);
    else prefs.hidden.remove(key);
    savePrefs(prefs);
    done({{QStringLiteral("ok"), true}});
}

void resetRows(ColosseumWebBridge &, const QVariantMap &, ActionRegistry::Completion done)
{
    savePrefs({});
    done({{QStringLiteral("ok"), true}});
}

bool enrichmentPayload(const QVariantMap &payload)
{
    const QString catalogue = payload.value(QStringLiteral("catalogue")).toString();
    return catalogue == QLatin1String("most-read")
        || catalogue == QLatin1String("classics");
}

void requestEnrichment(ColosseumWebBridge &bridge, const QVariantMap &payload,
                       ActionRegistry::Completion done)
{
    auto *catalog = qobject_cast<BiblioCatalog *>(
        bridge.service(QStringLiteral("BiblioCatalog")));
    if (!catalog) {
        done({{QStringLiteral("ok"), false},
              {QStringLiteral("error"), QStringLiteral("Biblio catalogue is unavailable.")}});
        return;
    }
    catalog->requestEnrichment(payload.value(QStringLiteral("catalogue")).toString());
    done({{QStringLiteral("ok"), true}});
}

const bool feedRegistered = FeedRegistry::add({
    QStringLiteral("world"), kWorld,
    [](const QVariantMap &params) {
        const QString tab = params.value(QStringLiteral("tab")).toString();
        if (!QStringList{QStringLiteral("discover"), QStringLiteral("explore"),
                         QStringLiteral("library")}.contains(tab))
            return false;
        if (params.contains(QStringLiteral("view"))
            && params.value(QStringLiteral("view")).metaType().id() != QMetaType::QVariantMap)
            return false;
        return validView(tab, params.value(QStringLiteral("view")).toMap());
    },
    initial, build, true, true, true, false, enrich, nullptr, nullptr,
    {{QStringLiteral("Progress"), bindProgress},
     {QStringLiteral("BiblioCatalog"), bindRevision},
     {QStringLiteral("BiblioCatalog"), bindReady}}
});

const bool enrichmentRegistered = ActionRegistry::add({
    QStringLiteral("world.biblio.requestEnrichment"),
    enrichmentPayload,
    requestEnrichment
});

const bool moveRegistered = ActionRegistry::add({
    QStringLiteral("rows.move"),
    [](const QVariantMap &payload) {
        const int dir = payload.value(QStringLiteral("dir")).toInt();
        return rowPayload(payload) && (dir == -1 || dir == 1);
    },
    moveRow
});

const bool hideRegistered = ActionRegistry::add({
    QStringLiteral("rows.hide"),
    [](const QVariantMap &payload) {
        return rowPayload(payload)
            && payload.value(QStringLiteral("hidden")).metaType().id() == QMetaType::Bool;
    },
    hideRow
});

const bool resetRegistered = ActionRegistry::add({
    QStringLiteral("rows.reset"),
    [](const QVariantMap &payload) {
        const QVariantMap params = payload.value(QStringLiteral("params")).toMap();
        return payload.value(QStringLiteral("feed")).toString() == QLatin1String("world")
            && params.value(QStringLiteral("world")).toString() == kWorld
            && params.value(QStringLiteral("tab")).toString() == QLatin1String("explore");
    },
    resetRows
});

} // namespace
