#include "ColosseumWebBridge.h"
#include "feeds/ContinueFeed.h"
#include "feeds/FeedValue.h"
#include "feeds/PendingFeeds.h"
#include "WallpaperSchemeHandler.h"

#include "../CollectionStore.h"
#include "../ProgressStore.h"
#include "../SearchHistoryStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <QtConcurrentRun>
#include <utility>

namespace {

QVariantList loading(const QString &feed, const QVariantMap &params)
{
    if (feed == QLatin1String("home")) return HomeFeed::initial();
    if (feed == QLatin1String("seeAll")) return SeeAllFeed::initial();
    if (feed == QLatin1String("search")) return SearchFeed::initial();
    const QString scope = params.value(QStringLiteral("scope")).toString();
    const QString title = QStringLiteral("Loading");
    return {WebFeedValue::section(feed + (scope.isEmpty() ? QString() : QLatin1Char('.') + scope),
                                  0, title, QStringLiteral("list"), {},
                                  QStringLiteral("loading"))};
}

} // namespace

ColosseumWebBridge::ColosseumWebBridge(const WorldFeed::Paths &paths,
                                       WallpaperSchemeHandler *wallpapers,
                                       QObject *parent)
    : QObject(parent), m_paths(paths), m_wallpapers(wallpapers),
      m_recordDirectory(qEnvironmentVariable("COLOSSEUM_WEBUI_RECORD"))
{
    setObjectName(QStringLiteral("colosseumWebBridge"));
    if (!m_recordDirectory.isEmpty())
        QDir().mkpath(m_recordDirectory);
}

QString ColosseumWebBridge::resourceUrl() const
{
    return QStringLiteral("qrc:///developer-webui/index.html");
}

void ColosseumWebBridge::bindPersonalStores(ProgressStore *progress,
                                            CollectionStore *collection,
                                            SearchHistoryStore *history)
{
    if (m_progress) disconnect(m_progress, nullptr, this, nullptr);
    if (m_collection) disconnect(m_collection, nullptr, this, nullptr);
    if (m_history) disconnect(m_history, nullptr, this, nullptr);
    m_progress = progress;
    m_collection = collection;
    m_history = history;
    if (m_progress)
        connect(m_progress, &ProgressStore::changed, this,
                [this] { storeChanged(true); });
    if (m_collection)
        connect(m_collection, &CollectionStore::changed, this,
                [this] { storeChanged(false); });
    if (m_history)
        connect(m_history, &SearchHistoryStore::changed, this,
                [this](const QString &) { storeChanged(false); });
    for (const int id : m_subscriptions.keys())
        reset(id);
    startRecorderSweep();
}

void ColosseumWebBridge::startRecorderSweep()
{
    if (m_recordDirectory.isEmpty() || m_recorderSweepStarted) return;
    m_recorderSweepStarted = true;
    // A real launch can record every v1 feed/tab before Claude's web adapter
    // is installed. These are ordinary subscriptions; the recorder observes
    // precisely the same feedEvent envelopes that a web client would receive.
    QList<QPair<QString, QVariantMap>> requests;
    requests.append({QStringLiteral("home"), {}});
    for (const QString &scope : {QStringLiteral("all"), QStringLiteral("Tankoban"),
                                 QStringLiteral("Biblio"), QStringLiteral("Theatre")})
        requests.append({QStringLiteral("continue"), {{QStringLiteral("scope"), scope}}});
    for (const QString &world : {QStringLiteral("Tankoban"), QStringLiteral("Biblio"),
                                 QStringLiteral("Theatre")}) {
        const QStringList tabs = world == QLatin1String("Tankoban")
            ? QStringList{QStringLiteral("discover"), QStringLiteral("manga"),
                          QStringLiteral("comics"), QStringLiteral("library")}
            : world == QLatin1String("Biblio")
            ? QStringList{QStringLiteral("discover"), QStringLiteral("explore"),
                          QStringLiteral("library")}
            : QStringList{QStringLiteral("discover"), QStringLiteral("movies"),
                          QStringLiteral("shows"), QStringLiteral("anime"),
                          QStringLiteral("library")};
        for (const QString &tab : tabs)
            requests.append({QStringLiteral("world"),
                             {{QStringLiteral("world"), world},
                              {QStringLiteral("tab"), tab}}});
    }
    requests.append({QStringLiteral("seeAll"),
                     {{QStringLiteral("route"), QVariantMap{
                         {QStringLiteral("v"), 1},
                         {QStringLiteral("world"), QStringLiteral("Theatre")},
                         {QStringLiteral("source"), QStringLiteral("catalogue")},
                         {QStringLiteral("explicit"), false},
                         {QStringLiteral("pageSize"), 24}}}}});
    for (const QString &scope : {QStringLiteral("all"), QStringLiteral("Tankoban"),
                                 QStringLiteral("Biblio"), QStringLiteral("Theatre")})
        requests.append({QStringLiteral("search"),
                         {{QStringLiteral("scope"), scope},
                          {QStringLiteral("query"), QStringLiteral("a")}}});
    for (int i = 0; i < requests.size(); ++i) {
        const auto request = requests.at(i);
        QTimer::singleShot(80 * i, this, [this, request] {
            subscribe(request.first, request.second);
        });
    }
}

void ColosseumWebBridge::suspendProfile()
{
    if (m_progress) disconnect(m_progress, nullptr, this, nullptr);
    if (m_collection) disconnect(m_collection, nullptr, this, nullptr);
    if (m_history) disconnect(m_history, nullptr, this, nullptr);
    m_progress = nullptr;
    m_collection = nullptr;
    m_history = nullptr;
    ++m_profileRevision;
    emit shellEvent(shellState());
    for (const int id : m_subscriptions.keys())
        reset(id);
}

void ColosseumWebBridge::setAccountPresentation(const QString &mode,
                                                const QString &username)
{
    if (m_accountMode == mode && m_accountUsername == username) return;
    m_accountMode = mode;
    m_accountUsername = username;
    emit shellEvent(shellState());
}

void ColosseumWebBridge::setWallpaper(const QString &url, const QString &kind)
{
    if (m_wallpaperSource == url && m_wallpaperKind == kind) return;
    m_wallpaperSource = url;
    m_wallpaperUrl = kind == QLatin1String("native") ? QString()
                   : m_wallpapers ? m_wallpapers->publicUrl(url) : url;
    m_wallpaperKind = kind;
    emit shellEvent(shellState());
}

void ColosseumWebBridge::setCovered(bool covered)
{
    if (m_covered == covered) return;
    m_covered = covered;
    emit shellEvent(shellState());
}

void ColosseumWebBridge::setShowExplicit(bool show)
{
    if (m_showExplicit == show) return;
    m_showExplicit = show;
    storeChanged(false);
}

QVariantMap ColosseumWebBridge::shellState() const
{
    return {{QStringLiteral("account"), QVariantMap{
                    {QStringLiteral("mode"), m_accountMode},
                    {QStringLiteral("username"), m_accountUsername},
                    {QStringLiteral("initial"), m_accountUsername.left(1).toUpper()}}},
            {QStringLiteral("wallpaper"), QVariantMap{
                    {QStringLiteral("url"), m_wallpaperUrl},
                    {QStringLiteral("kind"), m_wallpaperKind}}},
            {QStringLiteral("covered"), m_covered},
            {QStringLiteral("profileRevision"), m_profileRevision}};
}

bool ColosseumWebBridge::validFeed(const QString &feed, const QVariantMap &params) const
{
    if (feed == QLatin1String("home")) return params.isEmpty();
    if (feed == QLatin1String("continue"))
        return QStringList{QStringLiteral("all"), QStringLiteral("Tankoban"),
                           QStringLiteral("Biblio"), QStringLiteral("Theatre")}
            .contains(params.value(QStringLiteral("scope")).toString());
    if (feed == QLatin1String("world"))
        return WorldFeed::validTab(params.value(QStringLiteral("world")).toString(),
                                   params.value(QStringLiteral("tab")).toString());
    if (feed == QLatin1String("seeAll"))
        return params.value(QStringLiteral("route")).toMap().value(QStringLiteral("v")).toInt() == 1;
    if (feed == QLatin1String("search"))
        return QStringList{QStringLiteral("all"), QStringLiteral("Tankoban"),
                           QStringLiteral("Biblio"), QStringLiteral("Theatre")}
            .contains(params.value(QStringLiteral("scope")).toString());
    return false;
}

QVariantMap ColosseumWebBridge::subscribe(const QString &feed, const QVariantMap &params)
{
    if (!validFeed(feed, params)) return fail(QStringLiteral("Invalid feed or parameters."));
    const int id = m_nextSubscription++;
    Subscription sub;
    sub.feed = feed;
    sub.params = WebFeedValue::jsonMap(params);
    m_subscriptions.insert(id, sub);
    QTimer::singleShot(0, this, [this, id] { reset(id); });
    return {{QStringLiteral("ok"), true}, {QStringLiteral("id"), id},
            {QStringLiteral("generation"), sub.generation + 1}};
}

void ColosseumWebBridge::unsubscribe(int id)
{
    m_subscriptions.remove(id);
    m_recordedEvents.remove(id);
}

QVariantMap ColosseumWebBridge::more(int id, const QString &sectionId)
{
    auto it = m_subscriptions.find(id);
    if (it == m_subscriptions.end()) return fail(QStringLiteral("Subscription is closed."));
    if (it->feed != QLatin1String("continue") || !it->sections.contains(sectionId)
        || !it->sections.value(sectionId).value(QStringLiteral("hasMore")).toBool())
        return fail(QStringLiteral("No more items in that section."));
    it->visibleCount += 24;
    refresh(id);
    return {{QStringLiteral("ok"), true}};
}

void ColosseumWebBridge::publish(int id, const QVariantMap &event)
{
    auto it = m_subscriptions.find(id);
    if (it == m_subscriptions.end()) return;
    // The event payload has its own `id` for a remove-section event. Keep it
    // nested so the subscription id and section id cannot collide (§3.2).
    const QVariantMap envelope{{QStringLiteral("id"), id},
                               {QStringLiteral("generation"), it->generation},
                               {QStringLiteral("seq"), ++it->seq},
                               {QStringLiteral("event"), event}};
    emit feedEvent(envelope);
    record(id, envelope);
}

void ColosseumWebBridge::reset(int id)
{
    auto it = m_subscriptions.find(id);
    if (it == m_subscriptions.end()) return;
    ++it->generation;
    it->seq = 0;
    ++it->requestVersion;
    it->sections.clear();
    const QVariantList sections = loading(it->feed, it->params);
    for (const QVariant &value : sections) {
        const QVariantMap section = value.toMap();
        it->sections.insert(section.value(QStringLiteral("id")).toString(), section);
    }
    publish(id, {{QStringLiteral("type"), QStringLiteral("reset")},
                 {QStringLiteral("sections"), sections}});
    refresh(id);
}

void ColosseumWebBridge::refresh(int id)
{
    auto it = m_subscriptions.find(id);
    if (it == m_subscriptions.end()) return;
    const QString feed = it->feed;
    if (feed != QLatin1String("continue") && feed != QLatin1String("world"))
        return; // Deliberate Step 3 loading stubs for Home, See All and Search.
    const int generation = it->generation;
    const int requestVersion = ++it->requestVersion;
    const QVariantMap params = it->params;
    const int visibleCount = it->visibleCount;
    const QVariantList recent = m_progress ? m_progress->recent(QString(), 0) : QVariantList{};
    const QString world = params.value(QStringLiteral("world")).toString();
    const QVariantList collection = (m_collection && !world.isEmpty())
        ? m_collection->items(world.toLower()) : QVariantList{};
    const WorldFeed::Paths paths = m_paths;
    const bool showExplicit = m_showExplicit;
    auto *watcher = new QFutureWatcher<QVariantList>(this);
    connect(watcher, &QFutureWatcher<QVariantList>::finished, this,
            [this, watcher, id, generation, requestVersion] {
        const QVariantList sections = watcher->result();
        watcher->deleteLater();
        applySections(id, generation, requestVersion, sections);
    });
    watcher->setFuture(QtConcurrent::run([feed, params, recent, collection, paths,
                                          visibleCount, showExplicit] {
        if (feed == QLatin1String("continue"))
            return ContinueFeed::build(recent, params.value(QStringLiteral("scope")).toString(),
                                       paths.imdb, visibleCount);
        return WorldFeed::build(params.value(QStringLiteral("world")).toString(),
                                params.value(QStringLiteral("tab")).toString(),
                                paths, collection, showExplicit);
    }));
}

void ColosseumWebBridge::applySections(int id, int generation, int requestVersion,
                                        const QVariantList &sections)
{
    auto it = m_subscriptions.find(id);
    if (it == m_subscriptions.end() || it->generation != generation
        || it->requestVersion != requestVersion) return;
    QHash<QString, QVariantMap> next;
    for (const QVariant &value : sections) {
        const QVariantMap section = value.toMap();
        const QString key = section.value(QStringLiteral("id")).toString();
        if (key.isEmpty()) continue;
        next.insert(key, section);
        if (it->sections.value(key) != section)
            publish(id, {{QStringLiteral("type"), QStringLiteral("section")},
                         {QStringLiteral("section"), section}});
    }
    const QStringList oldKeys = it->sections.keys();
    for (const QString &key : oldKeys) {
        if (!next.contains(key))
            publish(id, {{QStringLiteral("type"), QStringLiteral("remove")},
                         {QStringLiteral("id"), key}});
    }
    it->sections = next;
}

void ColosseumWebBridge::storeChanged(bool progress)
{
    for (const int id : m_subscriptions.keys()) {
        auto it = m_subscriptions.find(id);
        if (it == m_subscriptions.end()) continue;
        if (progress && it->feed == QLatin1String("continue")) {
            if (it->pendingProgress) continue;
            it->pendingProgress = true;
            QTimer::singleShot(1000, this, [this, id] {
                auto sub = m_subscriptions.find(id);
                if (sub == m_subscriptions.end()) return;
                sub->pendingProgress = false;
                refresh(id);
            });
        } else if (!progress && it->feed == QLatin1String("world")) {
            refresh(id);
        }
    }
}

void ColosseumWebBridge::record(int id, const QVariantMap &event)
{
    if (m_recordDirectory.isEmpty()) return;
    const auto it = m_subscriptions.constFind(id);
    if (it == m_subscriptions.cend()) return;
    const QByteArray paramJson = QJsonDocument(QJsonObject::fromVariantMap(it->params))
                                     .toJson(QJsonDocument::Compact);
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(
        paramJson, QCryptographicHash::Sha256).toHex().left(12));
    const QString path = QDir(m_recordDirectory).filePath(
        QStringLiteral("%1__%2.json").arg(it->feed, hash));
    m_recordedEvents[id].append(QVariantMap{
        {QStringLiteral("t"), QDateTime::currentMSecsSinceEpoch()},
        {QStringLiteral("id"), id},
        {QStringLiteral("generation"), event.value(QStringLiteral("generation"))},
        {QStringLiteral("seq"), event.value(QStringLiteral("seq"))},
        {QStringLiteral("event"), event.value(QStringLiteral("event"))}});
    const QVariantMap body{{QStringLiteral("feed"), it->feed},
                           {QStringLiteral("params"), it->params},
                           {QStringLiteral("events"), m_recordedEvents.value(id)}};
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject::fromVariantMap(body)).toJson());
        file.commit();
    }
}

QVariantMap ColosseumWebBridge::fail(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}

QVariantMap ColosseumWebBridge::act(const QString &action,
                                    const QVariantMap &payload)
{
    const QVariantMap item = payload.value(QStringLiteral("item")).toMap();
    const QVariantMap ref = item.value(QStringLiteral("ref")).toMap();
    if (action == QLatin1String("continue.forget")) {
        if (!m_progress || ref.value(QStringLiteral("kind")).toString().isEmpty()
            || ref.value(QStringLiteral("id")).toString().isEmpty())
            return fail(QStringLiteral("Continue item is unavailable."));
        m_progress->forget(ref.value(QStringLiteral("kind")).toString(),
                           ref.value(QStringLiteral("id")).toString());
        return {{QStringLiteral("ok"), true}};
    }
    if (action == QLatin1String("collection.add")
        || action == QLatin1String("collection.remove")) {
        if (!m_collection || item.isEmpty())
            return fail(QStringLiteral("Collection item is unavailable."));
        const QString world = item.value(QStringLiteral("world")).toString().toLower();
        const QString key = ref.value(QStringLiteral("id")).toString();
        if (world.isEmpty() || key.isEmpty())
            return fail(QStringLiteral("Collection identity is missing."));
        QVariantMap entry = ref;
        entry.insert(QStringLiteral("id"), key);
        entry.insert(QStringLiteral("title"), item.value(QStringLiteral("title")));
        entry.insert(QStringLiteral("cover"), item.value(QStringLiteral("cover")));
        entry.insert(QStringLiteral("type"), item.value(QStringLiteral("kind")));
        const bool ok = action == QLatin1String("collection.add")
            ? m_collection->add(world, entry) : m_collection->remove(world, key);
        return ok ? QVariantMap{{QStringLiteral("ok"), true}}
                  : fail(QStringLiteral("Collection could not be changed."));
    }
    if (action == QLatin1String("search.history.remove")
        || action == QLatin1String("search.history.clear")) {
        if (!m_history) return fail(QStringLiteral("Search history is unavailable."));
        QString scope = payload.value(QStringLiteral("scope")).toString();
        // Contract actions carry only a query. Resolve the current search
        // subscription when the web layer omits its route scope.
        if (scope.isEmpty()) {
            for (const auto &sub : std::as_const(m_subscriptions)) {
                if (sub.feed != QLatin1String("search")) continue;
                const QString candidate = sub.params.value(QStringLiteral("scope")).toString();
                if (!scope.isEmpty() && scope != candidate)
                    return fail(QStringLiteral("Search scope is ambiguous."));
                scope = candidate;
            }
        }
        if (scope.isEmpty()) return fail(QStringLiteral("Search scope is missing."));
        if (action == QLatin1String("search.history.remove"))
            m_history->remove(scope, payload.value(QStringLiteral("query")).toString());
        else
            m_history->clear(scope);
        return {{QStringLiteral("ok"), true}};
    }
    static const QStringList doors{
        QStringLiteral("open"), QStringLiteral("open.vault"),
        QStringLiteral("open.universe"), QStringLiteral("open.universeHall"),
        QStringLiteral("open.native"), QStringLiteral("window.minimize"),
        QStringLiteral("window.fullscreen"), QStringLiteral("window.close")};
    if (!doors.contains(action)) return fail(QStringLiteral("Unsupported action."));
    if (action == QLatin1String("open") &&
        (item.isEmpty() || ref.isEmpty()))
        return fail(QStringLiteral("Item identity is missing."));
    const int requestId = m_nextAction++;
    emit actionRequested(action, payload, requestId);
    const QVariantMap result = m_finishedActions.take(requestId);
    return result.isEmpty() ? fail(QStringLiteral("Native destination is unavailable."))
                            : result;
}

void ColosseumWebBridge::finishAction(int requestId, bool ok,
                                       const QString &error,
                                       const QVariant &result)
{
    m_finishedActions.insert(requestId, ok
        ? QVariantMap{{QStringLiteral("ok"), true}, {QStringLiteral("result"), result}}
        : fail(error.isEmpty() ? QStringLiteral("Action failed.") : error));
}
