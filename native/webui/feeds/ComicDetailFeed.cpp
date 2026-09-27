#include "ActionRegistry.h"
#include "FeedRegistry.h"
#include "FeedValue.h"
#include "../ColosseumWebBridge.h"

#include "../../CollectionStore.h"
#include "../../ProgressStore.h"
#include "../../engine/ComicDownloader.h"
#include "../../engine/ComicsCatalog.h"

#include <QCollator>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QSharedPointer>
#include <QSet>
#include <QUrl>
#include <algorithm>
#include <functional>
#include <utility>

namespace {
const QString kFeed = QStringLiteral("detail.comic");

struct RemoteState {
    bool started = false;
    bool loading = false;
    QString error;
    QVariantMap meta;
    QVariantList rows;
    QMap<int, QVariantList> pages;
};

struct WaitLinks {
    QMetaObject::Connection finished;
    QMetaObject::Connection failed;
    bool settled = false;
};

struct AltSourceState {
    bool open = false;
    bool loading = false;
    bool complete = false;
    bool confirmingWeak = false;
    bool acquiring = false;
    QString issueId;
    QString query;
    QString error;
    QString selectionState = QStringLiteral("results");
    QString pendingSourceId;
    QVariantMap context;
    QVariantMap pendingRow;
    QPointer<ColosseumWebBridge> boundBridge;
    QPointer<ComicDownloader> boundDownloads;
    QVariantList rows;
    QVariantList archiveFiles;
    QStringList missingIssues;
    QVariantList combinedFiles;
};

QHash<QString, RemoteState> remoteStates;
QHash<QString, QVariantMap> privateRows;
QHash<QString, QVariantList> readerChains;
QHash<QString, AltSourceState> sourceStates;
QHash<QString, QVariantMap> privateSourceRows;
QSet<QString> terminalIssueIds;
QSet<QString> pendingReads;

QVariantMap customSection(const QString &id, int index, const QString &state,
                          const QString &schema, const QVariantMap &data = {},
                          bool hasMore = false)
{
    QVariantMap section = WebFeedValue::section(id, index, QString(), QStringLiteral("custom"),
                                                {}, state, hasMore);
    if (!schema.isEmpty()) {
        QVariantMap body = data;
        body.insert(QStringLiteral("schema"), schema);
        section.insert(QStringLiteral("data"), body);
    }
    return section;
}

void unavailable(ActionRegistry::Completion done, const QString &message)
{
    done({{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}});
}

QString decodeEntities(QString text)
{
    static const QList<QPair<QString, QString>> named{
        {QStringLiteral("&#8211;"), QStringLiteral("–")},
        {QStringLiteral("&ndash;"), QStringLiteral("–")},
        {QStringLiteral("&#8212;"), QStringLiteral("—")},
        {QStringLiteral("&mdash;"), QStringLiteral("—")},
        {QStringLiteral("&#8216;"), QStringLiteral("‘")},
        {QStringLiteral("&#8217;"), QStringLiteral("’")},
        {QStringLiteral("&#8220;"), QStringLiteral("“")},
        {QStringLiteral("&#8221;"), QStringLiteral("”")},
        {QStringLiteral("&#038;"), QStringLiteral("&")},
        {QStringLiteral("&amp;"), QStringLiteral("&")},
        {QStringLiteral("&hellip;"), QStringLiteral("…")},
        {QStringLiteral("&lt;"), QStringLiteral("<")},
        {QStringLiteral("&gt;"), QStringLiteral(">")},
        {QStringLiteral("&quot;"), QStringLiteral("\"")}};
    for (const auto &pair : named) text.replace(pair.first, pair.second);
    const QRegularExpression numeric(QStringLiteral("&#(\\d+);"));
    auto match = numeric.match(text);
    while (match.hasMatch()) {
        text.replace(match.captured(0), QChar(match.captured(1).toInt()));
        match = numeric.match(text);
    }
    return text;
}

QString plainHtml(QString text)
{
    text.remove(QRegularExpression(QStringLiteral("<[^>]+>")));
    return decodeEntities(text).simplified();
}

QString rowKey(const QString &routeId, const QString &unitId)
{
    return routeId + QLatin1Char('|') + unitId;
}

QString sourceKey(const QString &routeId, const QString &issueId, const QString &sourceId)
{
    return routeId + QLatin1Char('|') + issueId + QLatin1Char('|') + sourceId;
}

void clearPendingIssue(const QString &issueId)
{
    const QString suffix = QLatin1Char('|') + issueId;
    for (auto it = pendingReads.begin(); it != pendingReads.end();) {
        if (it->endsWith(suffix))
            it = pendingReads.erase(it);
        else
            ++it;
    }
}

void clearRoutePrivateState(const QString &routeId)
{
    const QString prefix = routeId + QLatin1Char('|');
    for (auto it = privateRows.begin(); it != privateRows.end();) {
        if (it.key().startsWith(prefix))
            it = privateRows.erase(it);
        else
            ++it;
    }
    readerChains.remove(routeId);
}

void clearSourcePrivateRows(const QString &routeId, const QString &issueId)
{
    const QString prefix = sourceKey(routeId, issueId, QString());
    for (auto it = privateSourceRows.begin(); it != privateSourceRows.end();) {
        if (it.key().startsWith(prefix))
            it = privateSourceRows.erase(it);
        else
            ++it;
    }
}

QVariantMap publicTorrentSource(const QVariantMap &raw, const QString &id)
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("title"), raw.value(QStringLiteral("title"))},
            {QStringLiteral("sizeText"), raw.value(QStringLiteral("sizeText"))},
            {QStringLiteral("seeders"), raw.value(QStringLiteral("seeders"))},
            {QStringLiteral("leechers"), raw.value(QStringLiteral("leechers"))},
            {QStringLiteral("sourceName"), raw.value(QStringLiteral("sourceName"))},
            {QStringLiteral("confidence"), raw.value(QStringLiteral("confidence"))},
            {QStringLiteral("matchTier"), raw.value(QStringLiteral("matchTier"))},
            {QStringLiteral("evidence"), raw.value(QStringLiteral("evidence"))},
            {QStringLiteral("archiveHint"), raw.value(QStringLiteral("archiveHint"))},
            {QStringLiteral("coverage"), raw.value(QStringLiteral("coverage"))},
            {QStringLiteral("uploader"), raw.value(QStringLiteral("uploader"))},
            {QStringLiteral("trustTier"), raw.value(QStringLiteral("trustTier"))}};
}

QString humanBytes(qint64 bytes)
{
    if (bytes <= 0) return {};
    static const QStringList units{QStringLiteral("B"), QStringLiteral("KB"),
                                   QStringLiteral("MB"), QStringLiteral("GB"),
                                   QStringLiteral("TB")};
    double value = double(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < units.size() - 1) {
        value /= 1024.0;
        ++unit;
    }
    return unit == 0
        ? QStringLiteral("%1 %2").arg(qint64(value)).arg(units.at(unit))
        : QStringLiteral("%1 %2").arg(value, 0, 'f', 1).arg(units.at(unit));
}

QVariantList publicArchiveFiles(const QVariantList &files)
{
    QVariantList out;
    for (const QVariant &value : files) {
        const QVariantMap raw = value.toMap();
        const QString path = raw.value(QStringLiteral("path")).toString();
        QString name = raw.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) name = path.section(QLatin1Char('/'), -1);
        QString extension = raw.value(QStringLiteral("extension")).toString();
        if (extension.isEmpty()) extension = name.section(QLatin1Char('.'), -1).toUpper();
        QString sizeText = raw.value(QStringLiteral("sizeText")).toString();
        if (sizeText.isEmpty())
            sizeText = humanBytes(raw.value(QStringLiteral("bytes"),
                                           raw.value(QStringLiteral("sizeBytes"))).toLongLong());
        out.append(QVariantMap{{QStringLiteral("index"), raw.value(QStringLiteral("index"))},
                               {QStringLiteral("name"), name},
                               {QStringLiteral("extension"), extension},
                               {QStringLiteral("sizeText"), sizeText}});
    }
    return out;
}

bool naturalLess(const QVariant &left, const QVariant &right)
{
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    return collator.compare(left.toMap().value(QStringLiteral("title")).toString(),
                            right.toMap().value(QStringLiteral("title")).toString()) < 0;
}

// ComicSeries.qml:88-149 keeps filtering/sort view-only while the reader chain
// remains in source order. This helper preserves that exact separation.
QVariantList filteredRows(QVariantList rows, const QVariantMap &view)
{
    const QString query = view.value(QStringLiteral("query")).toString().trimmed();
    if (!query.isEmpty()) {
        QVariantList kept;
        for (const QVariant &value : std::as_const(rows))
            if (value.toMap().value(QStringLiteral("title")).toString()
                    .contains(query, Qt::CaseInsensitive))
                kept.append(value);
        rows = kept;
    }
    const QString sort = view.value(QStringLiteral("sort"), QStringLiteral("new")).toString();
    if (sort == QLatin1String("old")) {
        std::stable_sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
            const int ay = a.toMap().value(QStringLiteral("year"), 9999).toInt();
            const int by = b.toMap().value(QStringLiteral("year"), 9999).toInt();
            return ay == by ? naturalLess(a, b) : ay < by;
        });
    } else if (sort == QLatin1String("az")) {
        std::stable_sort(rows.begin(), rows.end(), naturalLess);
    }
    return rows;
}

using JsonDone = std::function<void(QJsonDocument, QVariantMap, QString)>;

void getJson(ColosseumWebBridge &bridge, const QUrl &url, JsonDone done)
{
    // Main.cpp:239-425 owns the CachingNam pin/UA/cache policy. A private NAM here
    // would bypass GetComics' IPv4 pin and revive the dead-AAAA failure. Codex C
    // exposes that owner as WebNetwork; until then the section fails honestly.
    auto *network = qobject_cast<QNetworkAccessManager *>(
        bridge.service(QStringLiteral("WebNetwork")));
    if (!network) {
        done({}, {}, QStringLiteral("Live GetComics browsing is waiting for the shared network service."));
        return;
    }
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = network->get(request);
    QObject::connect(reply, &QNetworkReply::finished, &bridge,
        [reply, done = std::move(done)]() mutable {
            QVariantMap headers;
            headers.insert(QStringLiteral("pages"),
                           QString::fromLatin1(reply->rawHeader("X-WP-TotalPages")).toInt());
            headers.insert(QStringLiteral("total"),
                           QString::fromLatin1(reply->rawHeader("X-WP-Total")).toInt());
            const bool ok = reply->error() == QNetworkReply::NoError;
            QJsonParseError parse;
            const QJsonDocument doc = ok ? QJsonDocument::fromJson(reply->readAll(), &parse)
                                         : QJsonDocument{};
            const QString error = !ok ? QStringLiteral("The comics source is unavailable right now.")
                                      : parse.error == QJsonParseError::NoError
                                      ? QString()
                                      : QStringLiteral("The comics source returned unreadable data.");
            reply->deleteLater();
            done(doc, headers, error);
        });
}

QVariantMap postRow(const QJsonObject &post)
{
    const QString title = decodeEntities(post.value(QStringLiteral("title")).toObject()
                                         .value(QStringLiteral("rendered")).toString());
    const QString excerpt = post.value(QStringLiteral("excerpt")).toObject()
                            .value(QStringLiteral("rendered")).toString();
    const auto yearMatch = QRegularExpression(
        QStringLiteral("Year\\s*:\\s*</strong>\\s*(\\d{4})"),
        QRegularExpression::CaseInsensitiveOption).match(excerpt);
    const auto sizeMatch = QRegularExpression(
        QStringLiteral("Size\\s*:\\s*</strong>\\s*([\\d.]+)\\s*(GB|MB)"),
        QRegularExpression::CaseInsensitiveOption).match(excerpt);
    double sizeMb = 0;
    if (sizeMatch.hasMatch()) {
        sizeMb = sizeMatch.captured(1).toDouble();
        if (sizeMatch.captured(2).compare(QStringLiteral("GB"), Qt::CaseInsensitive) == 0)
            sizeMb *= 1024.0;
    }
    QString cover;
    const QJsonArray art = post.value(QStringLiteral("yoast_head_json")).toObject()
                           .value(QStringLiteral("og_image")).toArray();
    if (!art.isEmpty()) cover = art.first().toObject().value(QStringLiteral("url")).toString();
    QString synopsis = excerpt;
    synopsis.remove(QRegularExpression(
        QStringLiteral("<p[^>]*>.*?Size\\s*:.*?</p>"),
        QRegularExpression::CaseInsensitiveOption
            | QRegularExpression::DotMatchesEverythingOption));
    const bool collection = QRegularExpression(
        QStringLiteral("\\(TPB\\)|omnibus|treasury|library edition|hardcover|complete collection"),
        QRegularExpression::CaseInsensitiveOption).match(title).hasMatch();
    return {{QStringLiteral("id"), QString::number(post.value(QStringLiteral("id")).toInt())},
            {QStringLiteral("title"), title},
            {QStringLiteral("postUrl"), post.value(QStringLiteral("link")).toString()},
            {QStringLiteral("cover"), cover},
            {QStringLiteral("year"), yearMatch.hasMatch() ? yearMatch.captured(1).toInt() : 0},
            {QStringLiteral("sizeMB"), qRound(sizeMb)},
            {QStringLiteral("synopsis"), plainHtml(synopsis)},
            {QStringLiteral("date"), post.value(QStringLiteral("date")).toString()},
            {QStringLiteral("collection"), collection}};
}

QVariantList posts(const QJsonDocument &document)
{
    QVariantList out;
    for (const QJsonValue &value : document.array())
        out.append(postRow(value.toObject()));
    return out;
}

void rebuildPages(RemoteState &state)
{
    state.rows.clear();
    for (auto it = state.pages.cbegin(); it != state.pages.cend(); ++it)
        state.rows.append(it.value());
}

void notify(QPointer<ColosseumWebBridge> bridge, const QString &id)
{
    if (bridge) bridge->updateDetail(kFeed, id, {});
}

void bindSourceSignals(const QString &routeId, ColosseumWebBridge &bridge,
                       ComicDownloader &downloads)
{
    auto &state = sourceStates[routeId];
    if (state.boundBridge == &bridge && state.boundDownloads == &downloads) return;
    state.boundBridge = &bridge;
    state.boundDownloads = &downloads;
    QPointer<ColosseumWebBridge> guarded(&bridge);

    QObject::connect(&downloads, &ComicDownloader::torrentSourcesUpdated, &bridge,
        [routeId, guarded](const QString &issueId, const QVariantList &rows, bool complete) {
            auto it = sourceStates.find(routeId);
            if (it == sourceStates.end() || !it->open || it->issueId != issueId) return;
            clearSourcePrivateRows(routeId, issueId);
            QVariantList publicRows;
            int fallback = 0;
            for (const QVariant &value : rows) {
                const QVariantMap raw = value.toMap();
                QString id = raw.value(QStringLiteral("sourceKey")).toString();
                if (id.isEmpty()) id = QStringLiteral("source-%1").arg(fallback);
                ++fallback;
                privateSourceRows.insert(sourceKey(routeId, issueId, id), raw);
                publicRows.append(publicTorrentSource(raw, id));
            }
            it->rows = publicRows;
            it->complete = complete;
            it->loading = !complete;
            it->error.clear();
            notify(guarded, routeId);
        });
    QObject::connect(&downloads, &ComicDownloader::torrentSourceSearchFailed, &bridge,
        [routeId, guarded](const QString &issueId, const QString &reason) {
            auto it = sourceStates.find(routeId);
            if (it == sourceStates.end() || !it->open || it->issueId != issueId) return;
            it->rows.clear();
            it->loading = false;
            it->complete = true;
            it->error = reason;
            notify(guarded, routeId);
        });
    QObject::connect(&downloads, &ComicDownloader::torrentArchiveSelectionRequired, &bridge,
        [routeId, guarded](const QString &issueId, const QVariantList &files) {
            auto it = sourceStates.find(routeId);
            if (it == sourceStates.end() || !it->open || it->issueId != issueId) return;
            it->archiveFiles = publicArchiveFiles(files);
            it->selectionState = QStringLiteral("ambiguous");
            notify(guarded, routeId);
        });
    QObject::connect(&downloads, &ComicDownloader::torrentCombinedArchiveConfirmationRequired, &bridge,
        [routeId, guarded](const QString &issueId, const QVariantList &files) {
            auto it = sourceStates.find(routeId);
            if (it == sourceStates.end() || !it->open || it->issueId != issueId) return;
            it->combinedFiles = publicArchiveFiles(files);
            it->selectionState = QStringLiteral("combined");
            notify(guarded, routeId);
        });
    QObject::connect(&downloads, &ComicDownloader::torrentIncompleteIssueSetDetected, &bridge,
        [routeId, guarded](const QString &issueId, const QStringList &missing) {
            auto it = sourceStates.find(routeId);
            if (it == sourceStates.end() || !it->open || it->issueId != issueId) return;
            it->missingIssues = missing;
            it->selectionState = QStringLiteral("incomplete");
            notify(guarded, routeId);
        });
    auto closeOnSafeProgress = [routeId, guarded](const QString &issueId) {
        auto it = sourceStates.find(routeId);
        if (it == sourceStates.end() || !it->open || it->issueId != issueId
            || it->selectionState != QLatin1String("inspecting")) return;
        it->open = false;
        it->acquiring = false;
        it->confirmingWeak = false;
        notify(guarded, routeId);
    };
    QObject::connect(&downloads, &ComicDownloader::progress, &bridge,
        [closeOnSafeProgress](const QString &issueId, double, double) { closeOnSafeProgress(issueId); });
    QObject::connect(&downloads, &ComicDownloader::finished, &bridge,
        [closeOnSafeProgress](const QString &issueId) { closeOnSafeProgress(issueId); });
    QObject::connect(&downloads, &ComicDownloader::failed, &bridge,
        [routeId, guarded](const QString &issueId, const QString &) {
            auto it = sourceStates.find(routeId);
            if (it == sourceStates.end() || !it->open || it->issueId != issueId
                || it->selectionState == QLatin1String("results")) return;
            it->acquiring = false;
            it->selectionState = QStringLiteral("results");
            it->confirmingWeak = false;
            notify(guarded, routeId);
        });
}

QString gcApi(const QString &tail)
{
    return QStringLiteral("https://getcomics.org/wp-json/wp/v2/") + tail;
}

// ComicSeries.qml:192-244 + ComicsApi.js:126-197.
// A GetComics tag is the western series; page 1 paints first and up to five WP
// pages are then folded into the same native feed state.
void ensureGcSeries(ColosseumWebBridge &bridge, const QString &id,
                    const QString &slug, const QString &titleHint)
{
    auto &state = remoteStates[id];
    if (state.started) return;
    state.started = true;
    state.loading = true;
    state.meta.insert(QStringLiteral("title"), titleHint);
    state.meta.insert(QStringLiteral("slug"), slug);
    QPointer<ColosseumWebBridge> guarded(&bridge);
    const QString tagUrl = gcApi(QStringLiteral("tags?slug=%1&_fields=id,name,slug,count")
                                 .arg(QString::fromLatin1(QUrl::toPercentEncoding(slug))));
    getJson(bridge, QUrl(tagUrl), [guarded, id](QJsonDocument doc, QVariantMap, QString error) {
        auto &slot = remoteStates[id];
        if (!error.isEmpty() || !doc.isArray() || doc.array().isEmpty()) {
            slot.loading = false;
            slot.error = error.isEmpty() ? QStringLiteral("This GetComics series could not be found.") : error;
            notify(guarded, id);
            return;
        }
        const QJsonObject tag = doc.array().first().toObject();
        slot.meta.insert(QStringLiteral("tagId"), tag.value(QStringLiteral("id")).toInt());
        slot.meta.insert(QStringLiteral("slug"), tag.value(QStringLiteral("slug")).toString());
        if (slot.meta.value(QStringLiteral("title")).toString().isEmpty())
            slot.meta.insert(QStringLiteral("title"),
                             decodeEntities(tag.value(QStringLiteral("name")).toString()));
        if (!guarded) return;
        const int tagId = slot.meta.value(QStringLiteral("tagId")).toInt();
        const QString base = gcApi(QStringLiteral(
            "posts?tags=%1&per_page=100&_fields=id,link,title,date,excerpt,yoast_head_json.og_image")
            .arg(tagId));
        getJson(*guarded, QUrl(base + QStringLiteral("&page=1")),
            [guarded, id, base](QJsonDocument first, QVariantMap headers, QString pageError) {
                auto &current = remoteStates[id];
                if (!pageError.isEmpty() || !first.isArray()) {
                    current.loading = false;
                    current.error = pageError.isEmpty()
                        ? QStringLiteral("No releases are available for this series.") : pageError;
                    notify(guarded, id);
                    return;
                }
                current.pages.insert(1, posts(first));
                current.meta.insert(QStringLiteral("total"), headers.value(QStringLiteral("total")));
                current.loading = false;
                current.error.clear();
                rebuildPages(current);
                notify(guarded, id);
                const int pageCount = qBound(1, headers.value(QStringLiteral("pages"), 1).toInt(), 5);
                if (!guarded || pageCount <= 1) return;
                for (int page = 2; page <= pageCount; ++page) {
                    getJson(*guarded, QUrl(base + QStringLiteral("&page=%1").arg(page)),
                        [guarded, id, page](QJsonDocument extra, QVariantMap, QString extraError) {
                            if (extraError.isEmpty() && extra.isArray()) {
                                auto &again = remoteStates[id];
                                again.pages.insert(page, posts(extra));
                                rebuildPages(again);
                                notify(guarded, id);
                            }
                        });
                }
            });
    });
}

bool archiveNoise(const QString &name)
{
    static const QRegularExpression noise(
        QStringLiteral("^(0-day|non 0-day|tpb|request|getcomics|.*\\bweek\\b.*|marvel now|"
                       "infinity comic|dc comics collection|the art of|epic collection|zip)$"),
        QRegularExpression::CaseInsensitiveOption);
    return noise.match(name).hasMatch();
}

bool publisherTag(const QString &name)
{
    static const QRegularExpression publishers(
        QStringLiteral("^(marvel comics|dc comics|image comics|idw|boom studios|dynamite entertainment|"
                       "archie|vertigo|zenescope|oni press|valiant|mad cave|aftershock comics|rebellion|"
                       "europe comics|dark horse|titan comics|avatar press|vault comics|black mask|"
                       "ahoy comics|action lab)$"),
        QRegularExpression::CaseInsensitiveOption);
    return publishers.match(name).hasMatch();
}

// ComicArchiveBoard.qml:24-32 + ComicsApi.js:223-258.
void ensureArchiveBoard(ColosseumWebBridge &bridge, const QString &id)
{
    auto &state = remoteStates[id];
    if (state.started) return;
    state.started = true;
    state.loading = true;
    state.meta.insert(QStringLiteral("title"), QStringLiteral("GetComics Archives"));
    QPointer<ColosseumWebBridge> guarded(&bridge);
    getJson(bridge, QUrl(gcApi(QStringLiteral(
        "tags?per_page=60&orderby=count&order=desc&_fields=id,name,slug,count"))),
        [guarded, id](QJsonDocument doc, QVariantMap, QString error) {
            auto &slot = remoteStates[id];
            slot.loading = false;
            if (!error.isEmpty() || !doc.isArray()) {
                slot.error = error.isEmpty() ? QStringLiteral("The archives could not be loaded.") : error;
                notify(guarded, id);
                return;
            }
            QVariantList pubs;
            QVariantList franchises;
            for (const QJsonValue &value : doc.array()) {
                const QJsonObject tag = value.toObject();
                const QString title = decodeEntities(tag.value(QStringLiteral("name")).toString());
                if (archiveNoise(title)) continue;
                const int tagId = tag.value(QStringLiteral("id")).toInt();
                QVariantMap row{{QStringLiteral("id"), QStringLiteral("gcbox:%1").arg(tagId)},
                                {QStringLiteral("title"), title},
                                {QStringLiteral("count"), tag.value(QStringLiteral("count")).toInt()},
                                {QStringLiteral("tag"), tag.value(QStringLiteral("slug")).toString()},
                                {QStringLiteral("tagId"), tagId},
                                {QStringLiteral("cover"), QString()}};
                (publisherTag(title) ? pubs : franchises).append(row);
            }
            slot.rows = pubs.mid(0, 8);
            slot.rows.append(franchises.mid(0, 12));
            slot.error.clear();
            notify(guarded, id);
        });
}

// ComicArchiveIndex.qml:33-52 + ComicsApi.js:271-318.
// The newest two raw-post pages reveal active co-tag series archives.
void ensureArchiveIndex(ColosseumWebBridge &bridge, const QVariantMap &params)
{
    const QString id = params.value(QStringLiteral("id")).toString();
    auto &state = remoteStates[id];
    state.meta.insert(QStringLiteral("title"), params.value(QStringLiteral("title")));
    state.meta.insert(QStringLiteral("count"), params.value(QStringLiteral("count")));
    state.meta.insert(QStringLiteral("tag"), params.value(QStringLiteral("tag")));
    if (state.started) return;
    state.started = true;
    state.loading = true;
    const int tagId = id.mid(QStringLiteral("gcbox:").size()).toInt();
    if (tagId <= 0) {
        state.loading = false;
        state.error = QStringLiteral("This archive is unavailable.");
        return;
    }
    QPointer<ColosseumWebBridge> guarded(&bridge);
    auto frequencies = QSharedPointer<QHash<int, int>>::create();
    auto pending = QSharedPointer<int>::create(2);
    auto collect = [guarded, id, tagId, frequencies, pending](QJsonDocument doc,
                                                              QVariantMap, QString) {
        if (doc.isArray()) {
            for (const QJsonValue &value : doc.array())
                for (const QJsonValue &tag : value.toObject().value(QStringLiteral("tags")).toArray())
                    (*frequencies)[tag.toInt()] += 1;
        }
        if (--(*pending) > 0 || !guarded) return;
        frequencies->remove(tagId);
        QList<int> ids = frequencies->keys();
        std::sort(ids.begin(), ids.end(), [frequencies](int a, int b) {
            return frequencies->value(a) > frequencies->value(b);
        });
        if (ids.size() > 80) ids = ids.mid(0, 80);
        if (ids.isEmpty()) {
            auto &slot = remoteStates[id];
            slot.loading = false;
            slot.rows.clear();
            notify(guarded, id);
            return;
        }
        QStringList parts;
        for (int value : std::as_const(ids)) parts.append(QString::number(value));
        const QString url = gcApi(QStringLiteral(
            "tags?include=%1&per_page=100&_fields=id,name,slug,count").arg(parts.join(QLatin1Char(','))));
        getJson(*guarded, QUrl(url), [guarded, id, frequencies](QJsonDocument tags,
                                                               QVariantMap, QString error) {
            auto &slot = remoteStates[id];
            slot.loading = false;
            if (!error.isEmpty() || !tags.isArray()) {
                slot.error = error.isEmpty() ? QStringLiteral("Series archives could not be loaded.") : error;
                notify(guarded, id);
                return;
            }
            QVariantList rows;
            for (const QJsonValue &value : tags.array()) {
                const QJsonObject tag = value.toObject();
                const QString title = decodeEntities(tag.value(QStringLiteral("name")).toString());
                if (archiveNoise(title) || publisherTag(title)) continue;
                const int tid = tag.value(QStringLiteral("id")).toInt();
                rows.append(QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("gc:") + tag.value(QStringLiteral("slug")).toString()},
                    {QStringLiteral("title"), title},
                    {QStringLiteral("count"), tag.value(QStringLiteral("count")).toInt()},
                    {QStringLiteral("frequency"), frequencies->value(tid)},
                    {QStringLiteral("cover"), QString()}});
            }
            std::stable_sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
                const QVariantMap am = a.toMap(), bm = b.toMap();
                const int af = am.value(QStringLiteral("frequency")).toInt();
                const int bf = bm.value(QStringLiteral("frequency")).toInt();
                return af == bf ? am.value(QStringLiteral("count")).toInt()
                                      > bm.value(QStringLiteral("count")).toInt()
                                : af > bf;
            });
            slot.rows = rows.mid(0, 24);
            slot.error.clear();
            notify(guarded, id);
        });
    };
    const QString base = gcApi(QStringLiteral("posts?tags=%1&per_page=100&_fields=tags").arg(tagId));
    getJson(bridge, QUrl(base + QStringLiteral("&page=1")), collect);
    getJson(bridge, QUrl(base + QStringLiteral("&page=2")), collect);
}

bool valid(const QVariantMap &params)
{
    const QString id = params.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || id.size() > 220) return false;
    const bool known = id.startsWith(QLatin1String("gc:"))
        || id.startsWith(QLatin1String("gcd:"))
        || id.startsWith(QLatin1String("locg:"))
        || id == QLatin1String("comic:archives")
        || id.startsWith(QLatin1String("gcbox:"))
        || id.startsWith(QLatin1String("publisher:"));
    if (!known) return false;
    const QVariantMap view = params.value(QStringLiteral("view")).toMap();
    const QString sort = view.value(QStringLiteral("sort"), QStringLiteral("new")).toString();
    return QStringList{QStringLiteral("new"), QStringLiteral("old"), QStringLiteral("az")}.contains(sort)
        && view.value(QStringLiteral("query")).toString().size() <= 160;
}

QVariantList initial(const QVariantMap &params)
{
    const QString id = params.value(QStringLiteral("id")).toString();
    if (id == QLatin1String("comic:archives") || id.startsWith(QLatin1String("gcbox:"))
        || id.startsWith(QLatin1String("publisher:"))) {
        return {customSection(QStringLiteral("hero"), 0, QStringLiteral("loading"), QString()),
                customSection(QStringLiteral("content"), 1, QStringLiteral("loading"), QString())};
    }
    return {customSection(QStringLiteral("hero"), 0, QStringLiteral("loading"), QString()),
            customSection(QStringLiteral("filter"), 1, QStringLiteral("loading"), QString()),
            WebFeedValue::section(QStringLiteral("sort"), 2, QString(), QStringLiteral("chips"),
                                  {}, QStringLiteral("loading")),
            customSection(QStringLiteral("releases"), 3, QStringLiteral("loading"), QString())};
}

QString firstVerifiedPost(const QVariantMap &edition)
{
    for (const QVariant &value : edition.value(QStringLiteral("sources")).toList()) {
        const QVariantMap source = value.toMap();
        const QString confidence = source.value(QStringLiteral("confidenceClass")).toString();
        if (!source.value(QStringLiteral("fanMade")).toBool()
            && source.value(QStringLiteral("available"), true).toBool()
            && (confidence == QLatin1String("exact") || confidence == QLatin1String("strong"))) {
            const QString post = source.value(QStringLiteral("postUrl")).toString();
            if (!post.isEmpty()) return post;
        }
    }
    return edition.value(QStringLiteral("getcomicsPost")).toString();
}

void capture(ColosseumWebBridge &bridge, FeedContext &ctx)
{
    const QString id = ctx.params.value(QStringLiteral("id")).toString();
    auto *catalog = qobject_cast<ComicsCatalog *>(bridge.service(QStringLiteral("ComicsCatalog")));
    auto *downloads = qobject_cast<ComicDownloader *>(bridge.service(QStringLiteral("Comics")));
    auto *collection = qobject_cast<CollectionStore *>(bridge.service(QStringLiteral("Collection")));
    auto *progress = qobject_cast<ProgressStore *>(bridge.service(QStringLiteral("Progress")));

    if (id == QLatin1String("comic:archives")) {
        ensureArchiveBoard(bridge, id);
        const RemoteState state = remoteStates.value(id);
        ctx.nativeSnapshot.insert(QStringLiteral("pageKind"), QStringLiteral("archiveBoard"));
        ctx.nativeSnapshot.insert(QStringLiteral("loading"), state.loading);
        ctx.nativeSnapshot.insert(QStringLiteral("error"), state.error);
        ctx.nativeSnapshot.insert(QStringLiteral("meta"), state.meta);
        ctx.nativeSnapshot.insert(QStringLiteral("rows"), state.rows);
        return;
    }
    if (id.startsWith(QLatin1String("gcbox:"))) {
        ensureArchiveIndex(bridge, ctx.params);
        const RemoteState state = remoteStates.value(id);
        ctx.nativeSnapshot.insert(QStringLiteral("pageKind"), QStringLiteral("archiveIndex"));
        ctx.nativeSnapshot.insert(QStringLiteral("loading"), state.loading);
        ctx.nativeSnapshot.insert(QStringLiteral("error"), state.error);
        ctx.nativeSnapshot.insert(QStringLiteral("meta"), state.meta);
        ctx.nativeSnapshot.insert(QStringLiteral("rows"), state.rows);
        return;
    }
    if (id.startsWith(QLatin1String("publisher:"))) {
        ctx.nativeSnapshot.insert(QStringLiteral("pageKind"), QStringLiteral("publisher"));
        const QString publisher = QUrl::fromPercentEncoding(id.mid(QStringLiteral("publisher:").size()).toUtf8());
        QVariantMap page;
        if (catalog && catalog->ready()) {
            const QString sort = ctx.params.value(QStringLiteral("view")).toMap()
                                     .value(QStringLiteral("sort"), QStringLiteral("new")).toString();
            page = catalog->discoverPage(sort == QLatin1String("az")
                                             ? QStringLiteral("all")
                                             : QStringLiteral("new-releases"),
                                         QStringLiteral("publisher"), publisher,
                                         ctx.showExplicit, qMax(0, ctx.visibleCount), 100);
        }
        QVariantList rows;
        for (const QVariant &value : page.value(QStringLiteral("items")).toList()) {
            const QVariantMap item = value.toMap();
            rows.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("locg:")
                                                         + item.value(QStringLiteral("locgId")).toString()},
                                    {QStringLiteral("title"), item.value(QStringLiteral("title"))},
                                    {QStringLiteral("cover"), item.value(QStringLiteral("cover"))},
                                    {QStringLiteral("year"), item.value(QStringLiteral("year"))},
                                    {QStringLiteral("publisher"), item.value(QStringLiteral("publisher"))}});
        }
        ctx.nativeSnapshot.insert(QStringLiteral("meta"),
                                  QVariantMap{{QStringLiteral("title"), publisher}});
        ctx.nativeSnapshot.insert(QStringLiteral("rows"), rows);
        ctx.nativeSnapshot.insert(QStringLiteral("hasMore"),
                                  !page.value(QStringLiteral("exhausted"), true).toBool());
        return;
    }

    QVariantMap meta;
    QVariantList rows;
    QString collectionId = id;
    QString readerSeriesId = id;
    QString sourceLabel;
    bool loading = false;
    QString error;

    if (id.startsWith(QLatin1String("gc:"))) {
        const QString slug = id.mid(3);
        ensureGcSeries(bridge, id, slug, ctx.params.value(QStringLiteral("title")).toString());
        const RemoteState state = remoteStates.value(id);
        meta = state.meta;
        rows = state.rows;
        loading = state.loading;
        error = state.error;
        sourceLabel = QStringLiteral("GetComics");
    } else if (id.startsWith(QLatin1String("gcd:"))) {
        const int gcdId = id.mid(4).toInt();
        const QVariantMap series = catalog && catalog->ready() ? catalog->series(gcdId) : QVariantMap{};
        if (series.isEmpty()) {
            error = QStringLiteral("This comic run is unavailable.");
        } else {
            meta = series;
            meta.insert(QStringLiteral("title"),
                        series.value(QStringLiteral("title")).toString()
                            + (series.value(QStringLiteral("year")).toInt() > 0
                               ? QStringLiteral(" (%1)").arg(series.value(QStringLiteral("year")).toInt())
                               : QString()));
            for (const QVariant &value : catalog->downloadsFor(gcdId)) {
                const QVariantMap raw = value.toMap();
                rows.append(QVariantMap{
                    {QStringLiteral("id"), raw.value(QStringLiteral("postId")).toString()},
                    {QStringLiteral("title"), raw.value(QStringLiteral("title"))},
                    {QStringLiteral("postUrl"), raw.value(QStringLiteral("link"))},
                    {QStringLiteral("cover"), series.value(QStringLiteral("cover"))},
                    {QStringLiteral("year"), raw.value(QStringLiteral("yearStart"))},
                    {QStringLiteral("sizeMB"), 0},
                    {QStringLiteral("date"), raw.value(QStringLiteral("date"))},
                    {QStringLiteral("collection"), raw.value(QStringLiteral("kind")).toString()
                                                       != QLatin1String("single")}});
            }
            sourceLabel = QStringLiteral("GCD · GetComics");
        }
    } else {
        const QString bare = id.mid(QStringLiteral("locg:").size());
        const QVariantMap series = catalog && catalog->ready()
            ? catalog->curatedSeries(bare) : QVariantMap{};
        if (series.isEmpty()) {
            error = QStringLiteral("This comic series is unavailable.");
        } else {
            meta = series;
            readerSeriesId = QStringLiteral("gc:") + bare;
            for (const QVariant &value : series.value(QStringLiteral("editions")).toList()) {
                const QVariantMap edition = value.toMap();
                rows.append(QVariantMap{
                    {QStringLiteral("id"), edition.value(QStringLiteral("chid")).toString()},
                    {QStringLiteral("title"), edition.value(QStringLiteral("displayTitle"),
                                                           edition.value(QStringLiteral("title")))},
                    {QStringLiteral("postUrl"), firstVerifiedPost(edition)},
                    {QStringLiteral("cover"), edition.value(QStringLiteral("cover"))},
                    {QStringLiteral("year"), edition.value(QStringLiteral("published")).toString().left(4).toInt()},
                    {QStringLiteral("sizeMB"), 0},
                    {QStringLiteral("format"), edition.value(QStringLiteral("format"))},
                    {QStringLiteral("pages"), edition.value(QStringLiteral("pages"))},
                    {QStringLiteral("isbn"), edition.value(QStringLiteral("isbn"))},
                    {QStringLiteral("collects"), edition.value(QStringLiteral("collects"))},
                    {QStringLiteral("creators"), edition.value(QStringLiteral("creators"))},
                    {QStringLiteral("description"), edition.value(QStringLiteral("description"))},
                    {QStringLiteral("collection"), true}});
            }
            sourceLabel = QStringLiteral("Comics Catalogue · GetComics");
        }
    }

    if (!meta.contains(QStringLiteral("title")) || meta.value(QStringLiteral("title")).toString().isEmpty())
        meta.insert(QStringLiteral("title"), ctx.params.value(QStringLiteral("title")));
    if (!meta.contains(QStringLiteral("cover")) || meta.value(QStringLiteral("cover")).toString().isEmpty())
        meta.insert(QStringLiteral("cover"), ctx.params.value(QStringLiteral("cover")));
    meta.insert(QStringLiteral("sourceLabel"), sourceLabel);
    meta.insert(QStringLiteral("collectionId"), collectionId);
    meta.insert(QStringLiteral("readerSeriesId"), readerSeriesId);

    clearRoutePrivateState(id);
    QVariantList publicRows;
    QVariantList chain;
    QVariantMap resume;
    if (progress) resume = progress->get(QStringLiteral("comic"), readerSeriesId);
    const QVariantMap resumeUnit = resume.value(QStringLiteral("resume")).toMap();
    const QString resumeId = resumeUnit.value(QStringLiteral("chapterId")).toString();
    for (const QVariant &value : std::as_const(rows)) {
        const QVariantMap raw = value.toMap();
        const QString unitId = raw.value(QStringLiteral("id")).toString();
        if (unitId.isEmpty()) continue;
        const QVariantMap status = downloads ? downloads->statusOf(unitId) : QVariantMap{};
        QString state = status.value(QStringLiteral("state"), QStringLiteral("none")).toString();
        if (state != QLatin1String("done") && terminalIssueIds.contains(unitId))
            state = QStringLiteral("dead");
        const bool resumeMatches = unitId == resumeId;
        QVariantMap publicRow{
            {QStringLiteral("id"), unitId},
            {QStringLiteral("title"), raw.value(QStringLiteral("title"))},
            {QStringLiteral("cover"), raw.value(QStringLiteral("cover"))},
            {QStringLiteral("year"), raw.value(QStringLiteral("year"))},
            {QStringLiteral("sizeMB"), raw.value(QStringLiteral("sizeMB"))},
            {QStringLiteral("date"), raw.value(QStringLiteral("date"))},
            {QStringLiteral("format"), raw.value(QStringLiteral("format"))},
            {QStringLiteral("pages"), raw.value(QStringLiteral("pages"))},
            {QStringLiteral("description"), raw.value(QStringLiteral("description"))},
            {QStringLiteral("group"), raw.value(QStringLiteral("format")).toString().isEmpty()
                ? (raw.value(QStringLiteral("collection")).toBool()
                   ? QStringLiteral("Collected editions") : QStringLiteral("Issues"))
                : raw.value(QStringLiteral("format")).toString()},
            {QStringLiteral("downloadState"), state},
            {QStringLiteral("downloadDone"), status.value(QStringLiteral("done")).toDouble()},
            {QStringLiteral("downloadTotal"), status.value(QStringLiteral("total")).toDouble()},
            {QStringLiteral("available"), state == QLatin1String("done")
                || (state != QLatin1String("dead")
                    && !raw.value(QStringLiteral("postUrl")).toString().isEmpty())},
            {QStringLiteral("alternateAvailable"), id.startsWith(QLatin1String("locg:"))
                && state != QLatin1String("done")
                && state != QLatin1String("resolving")
                && state != QLatin1String("queued")
                && state != QLatin1String("downloading")
                && state != QLatin1String("extracting")},
            {QStringLiteral("readPending"), pendingReads.contains(rowKey(id, unitId))},
            {QStringLiteral("hasReadingProgress"), resumeMatches && !resumeUnit.isEmpty()},
            {QStringLiteral("readingFinished"), resumeMatches && resumeUnit.value(QStringLiteral("finished")).toBool()},
            {QStringLiteral("readingProgress"), resumeMatches
                ? qBound(0.0, resume.value(QStringLiteral("progress")).toDouble(), 1.0) : 0.0}};
        publicRows.append(publicRow);
        const QString postUrl = raw.value(QStringLiteral("postUrl")).toString();
        privateRows.insert(rowKey(id, unitId),
            {{QStringLiteral("postUrl"), postUrl},
             {QStringLiteral("seriesId"), readerSeriesId},
             {QStringLiteral("seriesTitle"), meta.value(QStringLiteral("title"))},
             {QStringLiteral("label"), raw.value(QStringLiteral("title"))},
             {QStringLiteral("cover"), raw.value(QStringLiteral("cover"))},
             {QStringLiteral("sizeMB"), raw.value(QStringLiteral("sizeMB"))},
             {QStringLiteral("isbn"), raw.value(QStringLiteral("isbn"))},
             {QStringLiteral("collects"), raw.value(QStringLiteral("collects"))},
             {QStringLiteral("format"), raw.value(QStringLiteral("format"))},
             {QStringLiteral("creators"), raw.value(QStringLiteral("creators"))}});
        chain.append(QVariantMap{{QStringLiteral("id"), unitId},
                                 {QStringLiteral("name"), raw.value(QStringLiteral("title"))},
                                 {QStringLiteral("url"), postUrl},
                                 {QStringLiteral("cover"), raw.value(QStringLiteral("cover"))},
                                 {QStringLiteral("sizeMB"), raw.value(QStringLiteral("sizeMB"))}});
    }
    readerChains.insert(id, chain);
    ctx.nativeSnapshot.insert(QStringLiteral("pageKind"), QStringLiteral("series"));
    ctx.nativeSnapshot.insert(QStringLiteral("loading"), loading);
    ctx.nativeSnapshot.insert(QStringLiteral("error"), error);
    ctx.nativeSnapshot.insert(QStringLiteral("meta"), meta);
    ctx.nativeSnapshot.insert(QStringLiteral("rows"), publicRows);
    ctx.nativeSnapshot.insert(QStringLiteral("saved"),
                              collection && collection->has(QStringLiteral("tankoban"), collectionId));
    ctx.nativeSnapshot.insert(QStringLiteral("resume"), resume);
    const AltSourceState sources = sourceStates.value(id);
    if (sources.open) {
        QStringList identity;
        const QString editionTitle = sources.context.value(QStringLiteral("editionTitle")).toString();
        const QString isbn = sources.context.value(QStringLiteral("isbn")).toString();
        const QString collects = sources.context.value(QStringLiteral("collects")).toString();
        if (!editionTitle.isEmpty()) identity.append(editionTitle);
        if (!isbn.isEmpty()) identity.append(QStringLiteral("ISBN ") + isbn);
        if (!collects.isEmpty()) identity.append(collects);
        ctx.nativeSnapshot.insert(QStringLiteral("sources"), QVariantMap{
            {QStringLiteral("open"), true},
            {QStringLiteral("issueId"), sources.issueId},
            {QStringLiteral("editionTitle"), editionTitle},
            {QStringLiteral("cover"), sources.context.value(QStringLiteral("cover"))},
            {QStringLiteral("identityLine"), identity.join(QStringLiteral("      ·      "))},
            {QStringLiteral("query"), sources.query},
            {QStringLiteral("loading"), sources.loading},
            {QStringLiteral("complete"), sources.complete},
            {QStringLiteral("confirmingWeak"), sources.confirmingWeak},
            {QStringLiteral("selectionState"), sources.selectionState},
            {QStringLiteral("pendingTitle"), sources.pendingRow.value(QStringLiteral("title"))},
            {QStringLiteral("rows"), sources.rows},
            {QStringLiteral("archiveFiles"), sources.archiveFiles},
            {QStringLiteral("missingIssues"), sources.missingIssues},
            {QStringLiteral("combinedCount"), sources.combinedFiles.size()},
            {QStringLiteral("error"), sources.error}});
    }
}

QVariantMap readyOrError(const QString &id, int index, const QString &schema,
                         const QString &error, const QVariantMap &data, bool more = false)
{
    QVariantMap section = customSection(id, index,
        error.isEmpty() ? QStringLiteral("ready") : QStringLiteral("error"),
        schema, data, more);
    if (!error.isEmpty()) section.insert(QStringLiteral("error"), error);
    return section;
}

QVariantMap sortSection(const QVariantMap &view)
{
    const QString selected = view.value(QStringLiteral("sort"), QStringLiteral("new")).toString();
    QVariantMap section = WebFeedValue::section(QStringLiteral("sort"), 2, QString(),
                                                QStringLiteral("chips"), {},
                                                QStringLiteral("ready"));
    QVariantList choices;
    for (const auto &pair : {
             qMakePair(QStringLiteral("new"), QStringLiteral("Newest")),
             qMakePair(QStringLiteral("old"), QStringLiteral("Oldest")),
             qMakePair(QStringLiteral("az"), QStringLiteral("A–Z"))}) {
        choices.append(QVariantMap{
            {QStringLiteral("key"), QStringLiteral("comic.sort.") + pair.first},
            {QStringLiteral("label"), pair.second},
            {QStringLiteral("selected"), pair.first == selected},
            {QStringLiteral("target"), QVariantMap{
                {QStringLiteral("view"), QVariantMap{{QStringLiteral("sort"), pair.first}}}}}});
    }
    section.insert(QStringLiteral("choices"), choices);
    return section;
}

QVariantMap publisherSortSection(const QVariantMap &view)
{
    const QString selected = view.value(QStringLiteral("sort"), QStringLiteral("new")).toString();
    QVariantMap section = WebFeedValue::section(QStringLiteral("sort"), 1, QString(),
                                                QStringLiteral("chips"), {},
                                                QStringLiteral("ready"));
    QVariantList choices;
    for (const auto &pair : {
             qMakePair(QStringLiteral("new"), QStringLiteral("Newest")),
             qMakePair(QStringLiteral("az"), QStringLiteral("A–Z"))}) {
        choices.append(QVariantMap{
            {QStringLiteral("key"), QStringLiteral("comic.publisher.sort.") + pair.first},
            {QStringLiteral("label"), pair.second},
            {QStringLiteral("selected"), pair.first == selected},
            {QStringLiteral("target"), QVariantMap{
                {QStringLiteral("view"), QVariantMap{{QStringLiteral("sort"), pair.first}}}}}});
    }
    section.insert(QStringLiteral("choices"), choices);
    return section;
}

QVariantList build(const FeedContext &ctx)
{
    const QString id = ctx.params.value(QStringLiteral("id")).toString();
    const QString kind = ctx.nativeSnapshot.value(QStringLiteral("pageKind")).toString();
    const bool loading = ctx.nativeSnapshot.value(QStringLiteral("loading")).toBool();
    const QString error = ctx.nativeSnapshot.value(QStringLiteral("error")).toString();
    const QVariantMap meta = ctx.nativeSnapshot.value(QStringLiteral("meta")).toMap();

    if (kind == QLatin1String("archiveBoard")) {
        if (loading)
            return {customSection(QStringLiteral("hero"), 0, QStringLiteral("loading"), QString()),
                    customSection(QStringLiteral("content"), 1, QStringLiteral("loading"), QString())};
        const QVariantList rows = ctx.nativeSnapshot.value(QStringLiteral("rows")).toList();
        return {
            readyOrError(QStringLiteral("hero"), 0, QStringLiteral("comic.archiveHero"), error,
                {{QStringLiteral("title"), QStringLiteral("GetComics Archives")},
                 {QStringLiteral("subtitle"), QStringLiteral("Publishers & franchises · whole-archive releases")}}),
            readyOrError(QStringLiteral("content"), 1, QStringLiteral("comic.archiveBoxes"), error,
                {{QStringLiteral("rows"), rows}})};
    }

    if (kind == QLatin1String("archiveIndex")) {
        if (loading)
            return {customSection(QStringLiteral("hero"), 0, QStringLiteral("loading"), QString()),
                    customSection(QStringLiteral("content"), 1, QStringLiteral("loading"), QString())};
        const QVariantList rows = ctx.nativeSnapshot.value(QStringLiteral("rows")).toList();
        return {
            readyOrError(QStringLiteral("hero"), 0, QStringLiteral("comic.archiveIndexHero"), error,
                {{QStringLiteral("title"), meta.value(QStringLiteral("title"))},
                 {QStringLiteral("count"), meta.value(QStringLiteral("count"))},
                 {QStringLiteral("seriesCount"), rows.size()},
                 {QStringLiteral("allTarget"), meta.value(QStringLiteral("tag")).toString().isEmpty()
                    ? QString() : QStringLiteral("gc:") + meta.value(QStringLiteral("tag")).toString()}}),
            readyOrError(QStringLiteral("content"), 1, QStringLiteral("comic.archiveSeries"), error,
                {{QStringLiteral("rows"), rows}})};
    }

    if (kind == QLatin1String("publisher")) {
        const QVariantList rows = ctx.nativeSnapshot.value(QStringLiteral("rows")).toList();
        const QVariantMap view = ctx.params.value(QStringLiteral("view")).toMap();
        const bool hasMore = ctx.nativeSnapshot.value(QStringLiteral("hasMore")).toBool();
        return {
            customSection(QStringLiteral("hero"), 0, QStringLiteral("ready"),
                          QStringLiteral("comic.publisherHero"),
                          {{QStringLiteral("title"), meta.value(QStringLiteral("title"))},
                           {QStringLiteral("count"), qMax(0, ctx.visibleCount) + rows.size()},
                           {QStringLiteral("hasMore"), hasMore}}),
            publisherSortSection(view),
            customSection(QStringLiteral("content"), 2,
                          rows.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
                          QStringLiteral("comic.publisherSeries"),
                          {{QStringLiteral("rows"), rows},
                           {QStringLiteral("windowStart"), qMax(0, ctx.visibleCount)}},
                          hasMore)};
    }

    if (loading && ctx.nativeSnapshot.value(QStringLiteral("rows")).toList().isEmpty())
        return initial(ctx.params);

    const QVariantMap view = ctx.params.value(QStringLiteral("view")).toMap();
    const QVariantList rows = filteredRows(ctx.nativeSnapshot.value(QStringLiteral("rows")).toList(), view);
    const int start = qMax(0, ctx.visibleCount);
    const QVariantList window = rows.mid(start, 100);
    const QVariantMap resume = ctx.nativeSnapshot.value(QStringLiteral("resume")).toMap();
    const QVariantMap resumeUnit = resume.value(QStringLiteral("resume")).toMap();
    QString resumeLabel;
    for (const QVariant &value : rows)
        if (value.toMap().value(QStringLiteral("id")).toString()
            == resumeUnit.value(QStringLiteral("chapterId")).toString()) {
            resumeLabel = value.toMap().value(QStringLiteral("title")).toString();
            break;
        }

    QVariantMap hero = readyOrError(QStringLiteral("hero"), 0, QStringLiteral("comic.hero"),
                                    error,
        {{QStringLiteral("id"), id},
         {QStringLiteral("title"), meta.value(QStringLiteral("title"))},
         {QStringLiteral("cover"), meta.value(QStringLiteral("cover"))},
         {QStringLiteral("publisher"), meta.value(QStringLiteral("publisher"))},
         {QStringLiteral("year"), meta.value(QStringLiteral("year"))},
         {QStringLiteral("synopsis"), meta.value(QStringLiteral("synopsis"))},
         {QStringLiteral("sourceLabel"), meta.value(QStringLiteral("sourceLabel"))},
         {QStringLiteral("saved"), ctx.nativeSnapshot.value(QStringLiteral("saved")).toBool()},
         {QStringLiteral("releaseCount"), ctx.nativeSnapshot.value(QStringLiteral("rows")).toList().size()},
         {QStringLiteral("resumeUnitId"), resumeUnit.value(QStringLiteral("chapterId"))},
         {QStringLiteral("resumeUnitLabel"), resumeLabel}});

    QVariantMap filter = customSection(QStringLiteral("filter"), 1, QStringLiteral("ready"),
        QStringLiteral("comic.filter"),
        {{QStringLiteral("query"), view.value(QStringLiteral("query"))},
         {QStringLiteral("matchCount"), rows.size()}});

    QVariantMap releases = customSection(QStringLiteral("releases"), 3,
        !error.isEmpty() ? QStringLiteral("error")
                         : rows.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
        QStringLiteral("comic.releases"),
        {{QStringLiteral("scope"), id + QLatin1Char('|')
             + view.value(QStringLiteral("query")).toString() + QLatin1Char('|')
             + view.value(QStringLiteral("sort"), QStringLiteral("new")).toString()},
         {QStringLiteral("windowStart"), start},
         {QStringLiteral("rows"), window},
         {QStringLiteral("total"), rows.size()}},
        start + window.size() < rows.size());
    if (!error.isEmpty()) releases.insert(QStringLiteral("error"), error);

    QVariantList out{hero, filter, sortSection(view), releases};
    const QVariantMap sources = ctx.nativeSnapshot.value(QStringLiteral("sources")).toMap();
    if (sources.value(QStringLiteral("open")).toBool())
        out.append(customSection(QStringLiteral("sources"), 4, QStringLiteral("ready"),
                                 QStringLiteral("comic.sources"), sources));
    return out;
}

bool identity(const QVariantMap &payload)
{
    const QString id = payload.value(QStringLiteral("id")).toString();
    return !id.isEmpty() && id.size() <= 220;
}

bool active(ColosseumWebBridge &bridge, const QVariantMap &payload,
            const ActionRegistry::Completion &done)
{
    if (bridge.detailActive(kFeed, payload.value(QStringLiteral("id")).toString())) return true;
    unavailable(done, QStringLiteral("This comics page is no longer open."));
    return false;
}

void delegateReader(ColosseumWebBridge &bridge, const QString &routeId,
                    const QString &unitId, ActionRegistry::Completion done)
{
    const QVariantMap row = privateRows.value(rowKey(routeId, unitId));
    if (row.isEmpty()) return unavailable(done, QStringLiteral("This issue is no longer available."));
    bridge.delegateAction(QStringLiteral("detail.comic.openReader"),
        {{QStringLiteral("seriesId"), row.value(QStringLiteral("seriesId"))},
         {QStringLiteral("seriesTitle"), row.value(QStringLiteral("seriesTitle"))},
         {QStringLiteral("seriesCover"), row.value(QStringLiteral("cover"))},
         {QStringLiteral("unitId"), unitId},
         {QStringLiteral("unitLabel"), row.value(QStringLiteral("label"))},
         {QStringLiteral("chapters"), readerChains.value(routeId)}}, done);
}

void addCollection(ColosseumWebBridge &bridge, const QString &routeId)
{
    auto *collection = qobject_cast<CollectionStore *>(bridge.service(QStringLiteral("Collection")));
    if (!collection || !collection->healthy()) return;
    const QVariantMap params = bridge.detailParams(kFeed, routeId);
    const QString collectionId = routeId;
    collection->add(QStringLiteral("tankoban"),
        {{QStringLiteral("id"), collectionId},
         {QStringLiteral("type"), QStringLiteral("comic")},
         {QStringLiteral("title"), params.value(QStringLiteral("title"))},
         {QStringLiteral("cover"), params.value(QStringLiteral("cover"))},
         {QStringLiteral("payload"), QVariantMap{}}});
}

QSharedPointer<WaitLinks> waitForRead(ColosseumWebBridge &bridge, ComicDownloader &downloads,
                                      const QString &routeId, const QString &unitId,
                                      ActionRegistry::Completion done)
{
    auto links = QSharedPointer<WaitLinks>::create();
    QPointer<ColosseumWebBridge> guarded(&bridge);
    links->finished = QObject::connect(&downloads, &ComicDownloader::finished, &bridge,
        [guarded, routeId, unitId, links, done](const QString &finishedId) mutable {
            if (links->settled || finishedId != unitId) return;
            links->settled = true;
            pendingReads.remove(rowKey(routeId, unitId));
            terminalIssueIds.remove(unitId);
            QObject::disconnect(links->finished);
            QObject::disconnect(links->failed);
            if (!guarded) return;
            guarded->updateDetail(kFeed, routeId, {});
            delegateReader(*guarded, routeId, unitId, std::move(done));
        });
    links->failed = QObject::connect(&downloads, &ComicDownloader::failed, &bridge,
        [routeId, unitId, links, done](const QString &failedId, const QString &) mutable {
            if (links->settled || failedId != unitId) return;
            links->settled = true;
            pendingReads.remove(rowKey(routeId, unitId));
            QObject::disconnect(links->finished);
            QObject::disconnect(links->failed);
            unavailable(std::move(done), QStringLiteral("This issue could not be downloaded."));
        });
    return links;
}

bool acceptedDownloadState(const QString &state)
{
    return QStringList{QStringLiteral("resolving"), QStringLiteral("queued"),
                       QStringLiteral("downloading"), QStringLiteral("extracting"),
                       QStringLiteral("done")}.contains(state);
}

bool beginAlternateDownload(ColosseumWebBridge &bridge, const QString &routeId,
                            ActionRegistry::Completion &done)
{
    auto *downloads = qobject_cast<ComicDownloader *>(bridge.service(QStringLiteral("Comics")));
    auto it = sourceStates.find(routeId);
    if (!downloads || it == sourceStates.end() || !it->open || it->pendingSourceId.isEmpty()) {
        unavailable(done, QStringLiteral("That alternate source is no longer available."));
        return false;
    }
    const QVariantMap source = privateSourceRows.value(
        sourceKey(routeId, it->issueId, it->pendingSourceId));
    if (source.isEmpty()) {
        unavailable(done, QStringLiteral("That alternate source is no longer available."));
        return false;
    }
    const QVariantMap context = it->context;
    it->confirmingWeak = false;
    it->acquiring = true;
    it->selectionState = QStringLiteral("inspecting");
    downloads->cancelTorrentSourceSearch(it->issueId);
    downloads->downloadTorrentEdition(
        it->issueId,
        context.value(QStringLiteral("seriesId")).toString(),
        context.value(QStringLiteral("seriesTitle")).toString(),
        context.value(QStringLiteral("editionTitle")).toString(),
        context.value(QStringLiteral("isbn")).toString(),
        context.value(QStringLiteral("collects")).toString(),
        context.value(QStringLiteral("format")).toString(),
        source.value(QStringLiteral("infoHash")).toString(),
        source.value(QStringLiteral("magnetUri")).toString());
    bridge.updateDetail(kFeed, routeId, {});
    return true;
}

QMetaObject::Connection watchComicProgress(QObject *object, QObject *receiver,
                                            std::function<void()> changed)
{
    auto *downloads = qobject_cast<ComicDownloader *>(object);
    if (!downloads) return {};
    return QObject::connect(downloads, &ComicDownloader::progress, receiver,
        [changed = std::move(changed)](const QString &, double, double) { changed(); });
}
QMetaObject::Connection watchComicFinished(QObject *object, QObject *receiver,
                                            std::function<void()> changed)
{
    auto *downloads = qobject_cast<ComicDownloader *>(object);
    if (!downloads) return {};
    return QObject::connect(downloads, &ComicDownloader::finished, receiver,
        [changed = std::move(changed)](const QString &issueId) {
            terminalIssueIds.remove(issueId);
            clearPendingIssue(issueId);
            changed();
        });
}
QMetaObject::Connection watchComicFailed(QObject *object, QObject *receiver,
                                          std::function<void()> changed)
{
    auto *downloads = qobject_cast<ComicDownloader *>(object);
    if (!downloads) return {};
    return QObject::connect(downloads, &ComicDownloader::failed, receiver,
        [changed = std::move(changed)](const QString &issueId, const QString &reason) {
            if (reason.startsWith(QLatin1String("no-source")))
                terminalIssueIds.insert(issueId);
            else
                terminalIssueIds.remove(issueId);
            clearPendingIssue(issueId);
            changed();
        });
}
QMetaObject::Connection watchComicRemoved(QObject *object, QObject *receiver,
                                           std::function<void()> changed)
{
    auto *downloads = qobject_cast<ComicDownloader *>(object);
    if (!downloads) return {};
    return QObject::connect(downloads, &ComicDownloader::removed, receiver,
        [changed = std::move(changed)](const QString &issueId) {
            terminalIssueIds.remove(issueId);
            clearPendingIssue(issueId);
            changed();
        });
}


QMetaObject::Connection watchProgress(QObject *object, QObject *receiver,
                                      std::function<void()> changed)
{
    auto *progress = qobject_cast<ProgressStore *>(object);
    if (!progress) return {};
    return QObject::connect(progress, &ProgressStore::changed, receiver,
        [changed = std::move(changed)] { changed(); });
}
QMetaObject::Connection watchCollection(QObject *object, QObject *receiver,
                                        std::function<void()> changed)
{
    auto *collection = qobject_cast<CollectionStore *>(object);
    if (!collection) return {};
    return QObject::connect(collection, &CollectionStore::changed, receiver,
        [changed = std::move(changed)] { changed(); });
}

const bool feedRegistered = [] {
    FeedRegistry::Entry entry;
    entry.name = kFeed;
    entry.valid = valid;
    entry.initial = initial;
    entry.build = build;
    entry.capture = capture;
    entry.needsProgress = true;
    entry.needsCollection = true;
    entry.ownerSignals.append({QStringLiteral("Comics"), watchComicProgress});
    entry.ownerSignals.append({QStringLiteral("Comics"), watchComicFinished});
    entry.ownerSignals.append({QStringLiteral("Comics"), watchComicFailed});
    entry.ownerSignals.append({QStringLiteral("Comics"), watchComicRemoved});
    entry.ownerSignals.append({QStringLiteral("Progress"), watchProgress});
    entry.ownerSignals.append({QStringLiteral("Collection"), watchCollection});
    return FeedRegistry::add(std::move(entry));
}();

const bool navigateRegistered = ActionRegistry::add({QStringLiteral("detail.comic.navigate"),
    [](const QVariantMap &p) {
        return identity(p) && !p.value(QStringLiteral("targetId")).toString().isEmpty()
            && p.value(QStringLiteral("targetId")).toString().size() <= 220;
    },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        if (!active(bridge, p, done)) return;
        const QString routeId = p.value(QStringLiteral("id")).toString();
        const QString target = p.value(QStringLiteral("targetId")).toString();
        QVariantMap row;
        const QVariantMap current = bridge.detailParams(kFeed, routeId);
        const QString archiveTarget = routeId.startsWith(QLatin1String("gcbox:"))
            && !current.value(QStringLiteral("tag")).toString().isEmpty()
            ? QStringLiteral("gc:") + current.value(QStringLiteral("tag")).toString()
            : QString();
        const bool directArchiveTarget = !archiveTarget.isEmpty() && target == archiveTarget;
        if (target != QLatin1String("comic:archives") && !directArchiveTarget)
            row = bridge.detailRow(kFeed, routeId, QStringLiteral("content"), target);
        if (target != QLatin1String("comic:archives") && !directArchiveTarget && row.isEmpty())
            return unavailable(done, QStringLiteral("That comics destination is no longer available."));
        QVariantMap params{{QStringLiteral("id"), target}};
        if (directArchiveTarget) {
            params.insert(QStringLiteral("title"), current.value(QStringLiteral("title")));
        }
        for (const QString &field : {QStringLiteral("title"), QStringLiteral("cover"),
                                     QStringLiteral("count"), QStringLiteral("publisher"),
                                     QStringLiteral("tag"), QStringLiteral("tagId")})
            if (row.contains(field)) params.insert(field, row.value(field));
        done({{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
            {QStringLiteral("route"), QVariantMap{{QStringLiteral("name"), QStringLiteral("detail")},
                                                  {QStringLiteral("kind"), QStringLiteral("comic")},
                                                  {QStringLiteral("params"), params}}}}}});
    }});

const bool collectionRegistered = ActionRegistry::add({QStringLiteral("detail.comic.collection"),
    [](const QVariantMap &p) { return identity(p) && p.contains(QStringLiteral("saved")); },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        if (!active(bridge, p, done)) return;
        auto *collection = qobject_cast<CollectionStore *>(bridge.service(QStringLiteral("Collection")));
        if (!collection || !collection->healthy())
            return unavailable(done, QStringLiteral("Collection is unavailable."));
        const QString id = p.value(QStringLiteral("id")).toString();
        if (id == QLatin1String("comic:archives") || id.startsWith(QLatin1String("gcbox:"))
            || id.startsWith(QLatin1String("publisher:")))
            return unavailable(done, QStringLiteral("Only a comic series can be saved."));
        const QVariantMap params = bridge.detailParams(kFeed, id);
        const bool saved = p.value(QStringLiteral("saved")).toBool();
        const bool ok = saved
            ? collection->add(QStringLiteral("tankoban"),
                {{QStringLiteral("id"), id},
                 {QStringLiteral("type"), QStringLiteral("comic")},
                 {QStringLiteral("title"), params.value(QStringLiteral("title"))},
                 {QStringLiteral("cover"), params.value(QStringLiteral("cover"))},
                 {QStringLiteral("payload"), QVariantMap{}}})
            : collection->remove(QStringLiteral("tankoban"), id);
        if (!ok) return unavailable(done, QStringLiteral("Collection could not be updated."));
        bridge.updateDetail(kFeed, id, {});
        done({{QStringLiteral("ok"), true}});
    }});

const bool downloadRegistered = ActionRegistry::add({QStringLiteral("detail.comic.download"),
    [](const QVariantMap &p) {
        return identity(p) && !p.value(QStringLiteral("unitId")).toString().isEmpty();
    },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        if (!active(bridge, p, done)) return;
        const QString id = p.value(QStringLiteral("id")).toString();
        const QString unitId = p.value(QStringLiteral("unitId")).toString();
        if (!privateRows.contains(rowKey(id, unitId)))
            return unavailable(done, QStringLiteral("This issue is no longer available."));
        auto *downloads = qobject_cast<ComicDownloader *>(bridge.service(QStringLiteral("Comics")));
        if (!downloads) return unavailable(done, QStringLiteral("Comic downloads are unavailable."));
        const QVariantMap row = privateRows.value(rowKey(id, unitId));
        if (row.isEmpty()) return unavailable(done, QStringLiteral("This issue is no longer available."));
        QString state = downloads->statusOf(unitId).value(QStringLiteral("state"),
                                                          QStringLiteral("none")).toString();
        if (state != QLatin1String("done") && terminalIssueIds.contains(unitId))
            state = QStringLiteral("dead");
        // ComicSeries.qml:797-817, 921-947. The trailing acquire utility is a
        // stateful verb: downloaded -> remove, in-flight -> cancel, idle/error
        // -> download/retry. The web only sends the stable unit id.
        if (state == QLatin1String("done")) {
            const QVariantMap result = downloads->deleteIssue(unitId);
            if (!result.value(QStringLiteral("success"), true).toBool())
                return unavailable(done, result.value(QStringLiteral("message"),
                    QStringLiteral("The downloaded issue could not be removed.")).toString());
            state = QStringLiteral("none");
        } else if (acceptedDownloadState(state)) {
            downloads->cancelDownload(unitId);
            state = downloads->statusOf(unitId).value(QStringLiteral("state"),
                                                      QStringLiteral("none")).toString();
        } else {
            if (state == QLatin1String("dead"))
                return unavailable(done, QStringLiteral("This release has no usable download source."));
            if (row.value(QStringLiteral("postUrl")).toString().isEmpty())
                return unavailable(done, QStringLiteral("No downloadable source is available for this issue."));
            downloads->downloadIssue(unitId, row.value(QStringLiteral("postUrl")).toString(),
                                     row.value(QStringLiteral("seriesId")).toString(),
                                     row.value(QStringLiteral("seriesTitle")).toString(),
                                     row.value(QStringLiteral("label")).toString(),
                                     row.value(QStringLiteral("sizeMB")).toDouble() * 1024.0 * 1024.0);
            state = downloads->statusOf(unitId).value(QStringLiteral("state"),
                                                      QStringLiteral("none")).toString();
            if (!acceptedDownloadState(state))
                return unavailable(done, QStringLiteral("This issue could not be queued."));
            addCollection(bridge, id);
        }
        bridge.updateDetail(kFeed, id, {});
        done({{QStringLiteral("ok"), true}, {QStringLiteral("result"),
              QVariantMap{{QStringLiteral("state"), state}, {QStringLiteral("jobId"), unitId}}}});
    }});

const bool readRegistered = ActionRegistry::add({QStringLiteral("detail.comic.read"),
    [](const QVariantMap &p) {
        return identity(p) && !p.value(QStringLiteral("unitId")).toString().isEmpty();
    },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        if (!active(bridge, p, done)) return;
        const QString id = p.value(QStringLiteral("id")).toString();
        const QString unitId = p.value(QStringLiteral("unitId")).toString();
        if (!privateRows.contains(rowKey(id, unitId)))
            return unavailable(done, QStringLiteral("This issue is no longer available."));
        auto *downloads = qobject_cast<ComicDownloader *>(bridge.service(QStringLiteral("Comics")));
        if (!downloads) return unavailable(done, QStringLiteral("Comic downloads are unavailable."));
        const QVariantMap row = privateRows.value(rowKey(id, unitId));
        if (row.isEmpty()) return unavailable(done, QStringLiteral("This issue is no longer available."));
        QString state = downloads->statusOf(unitId).value(QStringLiteral("state"),
                                                          QStringLiteral("none")).toString();
        if (state != QLatin1String("done") && terminalIssueIds.contains(unitId))
            state = QStringLiteral("dead");
        if (state == QLatin1String("done"))
            return delegateReader(bridge, id, unitId, std::move(done));
        if (state == QLatin1String("dead"))
            return unavailable(done, QStringLiteral("This release has no usable download source."));
        if (row.value(QStringLiteral("postUrl")).toString().isEmpty())
            return unavailable(done, QStringLiteral("No downloadable source is available for this issue."));

        // ComicSeries.qml:294-359: Read is consumption intent. If the unit is
        // absent, acquisition starts once, then the same intent opens the reader
        // after ComicDownloader reports completion.
        QSharedPointer<WaitLinks> links;
        if (!acceptedDownloadState(state)) {
            downloads->downloadIssue(unitId, row.value(QStringLiteral("postUrl")).toString(),
                                     row.value(QStringLiteral("seriesId")).toString(),
                                     row.value(QStringLiteral("seriesTitle")).toString(),
                                     row.value(QStringLiteral("label")).toString(),
                                     row.value(QStringLiteral("sizeMB")).toDouble() * 1024.0 * 1024.0);
            state = downloads->statusOf(unitId).value(QStringLiteral("state"),
                                                      QStringLiteral("none")).toString();
        }
        if (state == QLatin1String("done"))
            return delegateReader(bridge, id, unitId, std::move(done));
        if (!acceptedDownloadState(state))
            return unavailable(std::move(done), QStringLiteral("This issue could not be queued."));
        pendingReads.insert(rowKey(id, unitId));
        links = waitForRead(bridge, *downloads, id, unitId, std::move(done));
        Q_UNUSED(links);
        addCollection(bridge, id);
        bridge.updateDetail(kFeed, id, {});
    }});

const bool sourceOpenRegistered = ActionRegistry::add({QStringLiteral("detail.comic.openSources"),
    [](const QVariantMap &p) {
        return identity(p) && !p.value(QStringLiteral("unitId")).toString().isEmpty();
    },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        if (!active(bridge, p, done)) return;
        const QString routeId = p.value(QStringLiteral("id")).toString();
        const QString issueId = p.value(QStringLiteral("unitId")).toString();
        const QVariantMap row = privateRows.value(rowKey(routeId, issueId));
        if (row.isEmpty() || !routeId.startsWith(QLatin1String("locg:")))
            return unavailable(done, QStringLiteral("Alternate sources are unavailable for this edition."));
        auto *downloads = qobject_cast<ComicDownloader *>(bridge.service(QStringLiteral("Comics")));
        if (!downloads) return unavailable(done, QStringLiteral("Comic sources are unavailable."));
        const QString stateNow = downloads->statusOf(issueId)
                                     .value(QStringLiteral("state"), QStringLiteral("none")).toString();
        if (stateNow == QLatin1String("done") || acceptedDownloadState(stateNow))
            return unavailable(done, QStringLiteral("Alternate sources are only available for an idle edition."));

        auto &state = sourceStates[routeId];
        const QPointer<ColosseumWebBridge> boundBridge = state.boundBridge;
        const QPointer<ComicDownloader> boundDownloads = state.boundDownloads;
        state = AltSourceState{};
        state.boundBridge = boundBridge;
        state.boundDownloads = boundDownloads;
        state.open = true;
        state.loading = true;
        state.issueId = issueId;
        state.context = {{QStringLiteral("seriesId"), row.value(QStringLiteral("seriesId"))},
                         {QStringLiteral("seriesTitle"), row.value(QStringLiteral("seriesTitle"))},
                         {QStringLiteral("editionTitle"), row.value(QStringLiteral("label"))},
                         {QStringLiteral("isbn"), row.value(QStringLiteral("isbn"))},
                         {QStringLiteral("collects"), row.value(QStringLiteral("collects"))},
                         {QStringLiteral("format"), row.value(QStringLiteral("format"))},
                         {QStringLiteral("cover"), row.value(QStringLiteral("cover"))}};
        bindSourceSignals(routeId, bridge, *downloads);
        clearSourcePrivateRows(routeId, issueId);
        downloads->searchTorrentSources(issueId,
            row.value(QStringLiteral("seriesTitle")).toString(),
            row.value(QStringLiteral("label")).toString(),
            row.value(QStringLiteral("isbn")).toString(),
            row.value(QStringLiteral("collects")).toString(),
            row.value(QStringLiteral("format")).toString());
        bridge.updateDetail(kFeed, routeId, {});
        done({{QStringLiteral("ok"), true}});
    }});

const bool sourceQueryRegistered = ActionRegistry::add({QStringLiteral("detail.comic.searchSources"),
    [](const QVariantMap &p) {
        return identity(p) && !p.value(QStringLiteral("query")).toString().trimmed().isEmpty()
            && p.value(QStringLiteral("query")).toString().size() <= 300;
    },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        if (!active(bridge, p, done)) return;
        const QString routeId = p.value(QStringLiteral("id")).toString();
        auto *downloads = qobject_cast<ComicDownloader *>(bridge.service(QStringLiteral("Comics")));
        auto it = sourceStates.find(routeId);
        if (!downloads || it == sourceStates.end() || !it->open)
            return unavailable(done, QStringLiteral("The alternate-source picker is no longer open."));
        const QString query = p.value(QStringLiteral("query")).toString().trimmed();
        downloads->cancelTorrentSourceSearch(it->issueId);
        clearSourcePrivateRows(routeId, it->issueId);
        it->query = query;
        it->rows.clear();
        it->error.clear();
        it->loading = true;
        it->complete = false;
        it->confirmingWeak = false;
        it->pendingSourceId.clear();
        it->pendingRow.clear();
        it->selectionState = QStringLiteral("results");
        downloads->searchTorrentSourcesQuery(it->issueId, query);
        bridge.updateDetail(kFeed, routeId, {});
        done({{QStringLiteral("ok"), true}});
    }});

const bool sourceSelectRegistered = ActionRegistry::add({QStringLiteral("detail.comic.selectSource"),
    [](const QVariantMap &p) {
        return identity(p) && !p.value(QStringLiteral("sourceId")).toString().isEmpty();
    },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        if (!active(bridge, p, done)) return;
        const QString routeId = p.value(QStringLiteral("id")).toString();
        auto it = sourceStates.find(routeId);
        if (it == sourceStates.end() || !it->open)
            return unavailable(done, QStringLiteral("The alternate-source picker is no longer open."));
        const QString sourceId = p.value(QStringLiteral("sourceId")).toString();
        const QVariantMap raw = privateSourceRows.value(sourceKey(routeId, it->issueId, sourceId));
        if (raw.isEmpty())
            return unavailable(done, QStringLiteral("That alternate source is no longer available."));
        it->pendingSourceId = sourceId;
        it->pendingRow = publicTorrentSource(raw, sourceId);
        if (raw.value(QStringLiteral("confidence")).toString() == QLatin1String("weak")) {
            it->confirmingWeak = true;
            bridge.updateDetail(kFeed, routeId, {});
            done({{QStringLiteral("ok"), true}});
            return;
        }
        if (!beginAlternateDownload(bridge, routeId, done)) return;
        done({{QStringLiteral("ok"), true}});
    }});

const bool sourceWeakRegistered = ActionRegistry::add({QStringLiteral("detail.comic.confirmWeakSource"),
    [](const QVariantMap &p) { return identity(p); },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        if (!active(bridge, p, done)) return;
        const QString routeId = p.value(QStringLiteral("id")).toString();
        auto it = sourceStates.find(routeId);
        if (it == sourceStates.end() || !it->open || !it->confirmingWeak)
            return unavailable(done, QStringLiteral("There is no weak source waiting for confirmation."));
        if (!beginAlternateDownload(bridge, routeId, done)) return;
        done({{QStringLiteral("ok"), true}});
    }});

const bool sourceWeakCancelRegistered = ActionRegistry::add({QStringLiteral("detail.comic.cancelWeakSource"),
    [](const QVariantMap &p) { return identity(p); },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        if (!active(bridge, p, done)) return;
        auto it = sourceStates.find(p.value(QStringLiteral("id")).toString());
        if (it != sourceStates.end()) {
            it->confirmingWeak = false;
            it->pendingSourceId.clear();
            it->pendingRow.clear();
            bridge.updateDetail(kFeed, p.value(QStringLiteral("id")).toString(), {});
        }
        done({{QStringLiteral("ok"), true}});
    }});

const bool sourceArchiveRegistered = ActionRegistry::add({QStringLiteral("detail.comic.chooseArchive"),
    [](const QVariantMap &p) { return identity(p) && p.contains(QStringLiteral("fileIndex")); },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        if (!active(bridge, p, done)) return;
        const QString routeId = p.value(QStringLiteral("id")).toString();
        auto *downloads = qobject_cast<ComicDownloader *>(bridge.service(QStringLiteral("Comics")));
        auto it = sourceStates.find(routeId);
        if (!downloads || it == sourceStates.end() || !it->open
            || it->selectionState != QLatin1String("ambiguous"))
            return unavailable(done, QStringLiteral("There is no archive choice waiting."));
        const int fileIndex = p.value(QStringLiteral("fileIndex")).toInt();
        bool visible = false;
        for (const QVariant &value : it->archiveFiles)
            if (value.toMap().value(QStringLiteral("index")).toInt() == fileIndex) visible = true;
        if (!visible) return unavailable(done, QStringLiteral("That archive is no longer available."));
        downloads->chooseTorrentFiles(it->issueId, QVariantList{fileIndex});
        it->selectionState = QStringLiteral("inspecting");
        bridge.updateDetail(kFeed, routeId, {});
        done({{QStringLiteral("ok"), true}});
    }});

const bool sourceIncompleteRegistered = ActionRegistry::add({QStringLiteral("detail.comic.rejectIncomplete"),
    [](const QVariantMap &p) { return identity(p); },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        if (!active(bridge, p, done)) return;
        const QString routeId = p.value(QStringLiteral("id")).toString();
        auto *downloads = qobject_cast<ComicDownloader *>(bridge.service(QStringLiteral("Comics")));
        auto it = sourceStates.find(routeId);
        if (!downloads || it == sourceStates.end() || !it->open)
            return unavailable(done, QStringLiteral("The alternate-source picker is no longer open."));
        downloads->cancelDownload(it->issueId);
        it->acquiring = false;
        it->selectionState = QStringLiteral("results");
        it->missingIssues.clear();
        bridge.updateDetail(kFeed, routeId, {});
        done({{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
            {QStringLiteral("focusManual"), p.value(QStringLiteral("manual")).toBool()}}}});
    }});

const bool sourceCombinedRegistered = ActionRegistry::add({QStringLiteral("detail.comic.confirmCombined"),
    [](const QVariantMap &p) { return identity(p); },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        if (!active(bridge, p, done)) return;
        const QString routeId = p.value(QStringLiteral("id")).toString();
        auto *downloads = qobject_cast<ComicDownloader *>(bridge.service(QStringLiteral("Comics")));
        auto it = sourceStates.find(routeId);
        if (!downloads || it == sourceStates.end() || !it->open
            || it->selectionState != QLatin1String("combined"))
            return unavailable(done, QStringLiteral("There is no combined archive waiting."));
        downloads->confirmCombinedArchive(it->issueId);
        it->selectionState = QStringLiteral("inspecting");
        bridge.updateDetail(kFeed, routeId, {});
        done({{QStringLiteral("ok"), true}});
    }});

const bool sourceCombinedRejectRegistered = ActionRegistry::add({QStringLiteral("detail.comic.rejectCombined"),
    [](const QVariantMap &p) { return identity(p); },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        if (!active(bridge, p, done)) return;
        const QString routeId = p.value(QStringLiteral("id")).toString();
        auto *downloads = qobject_cast<ComicDownloader *>(bridge.service(QStringLiteral("Comics")));
        auto it = sourceStates.find(routeId);
        if (!downloads || it == sourceStates.end() || !it->open)
            return unavailable(done, QStringLiteral("The alternate-source picker is no longer open."));
        downloads->cancelDownload(it->issueId);
        it->acquiring = false;
        it->selectionState = QStringLiteral("results");
        it->combinedFiles.clear();
        bridge.updateDetail(kFeed, routeId, {});
        done({{QStringLiteral("ok"), true}});
    }});

const bool sourceCloseRegistered = ActionRegistry::add({QStringLiteral("detail.comic.closeSources"),
    [](const QVariantMap &p) { return identity(p); },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        if (!active(bridge, p, done)) return;
        const QString routeId = p.value(QStringLiteral("id")).toString();
        auto *downloads = qobject_cast<ComicDownloader *>(bridge.service(QStringLiteral("Comics")));
        auto it = sourceStates.find(routeId);
        if (it != sourceStates.end()) {
            if (downloads && !it->issueId.isEmpty()) {
                downloads->cancelTorrentSourceSearch(it->issueId);
                if (it->acquiring) downloads->cancelDownload(it->issueId);
            }
            clearSourcePrivateRows(routeId, it->issueId);
            it->open = false;
            it->acquiring = false;
            it->confirmingWeak = false;
            it->selectionState = QStringLiteral("results");
            bridge.updateDetail(kFeed, routeId, {});
        }
        done({{QStringLiteral("ok"), true}});
    }});
} // namespace
