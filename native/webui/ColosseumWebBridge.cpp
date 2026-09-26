#include "ColosseumWebBridge.h"
#include "feeds/ActionRegistry.h"
#include "feeds/FeedRegistry.h"
#include "feeds/FeedValue.h"
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
    const auto *entry = FeedRegistry::find(feed, params);
    return entry && entry->valid(params);
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
    const auto *entry = FeedRegistry::find(it->feed, it->params);
    const QVariantList sections = entry ? entry->initial(it->params) : QVariantList{};
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
    const auto *registered = FeedRegistry::find(it->feed, it->params);
    if (!registered || !registered->build) return;
    const FeedRegistry::Entry entry = *registered;
    const int generation = it->generation;
    const int requestVersion = ++it->requestVersion;
    FeedContext context;
    context.params = it->params;
    context.visibleCount = it->visibleCount;
    context.paths = m_paths;
    context.showExplicit = m_showExplicit;
    if (entry.needsProgress && m_progress)
        context.recent = m_progress->recent(QString(), 0);
    if (entry.needsCollection && m_collection)
        context.collection = m_collection->items(
            context.params.value(QStringLiteral("world")).toString().toLower());
    auto *watcher = new QFutureWatcher<QVariantList>(this);
    connect(watcher, &QFutureWatcher<QVariantList>::finished, this,
            [this, watcher, id, generation, requestVersion] {
        const QVariantList sections = watcher->result();
        watcher->deleteLater();
        applySections(id, generation, requestVersion, sections);
    });
    watcher->setFuture(QtConcurrent::run([entry, context] {
        return entry.build(context);
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

void ColosseumWebBridge::clearSurfaces()
{
    m_surfaces.clear();
}

bool ColosseumWebBridge::hasSurface(const QString &name) const
{
    return m_surfaces.contains(name);
}

QFuture<QVariantMap> ColosseumWebBridge::act(const QString &action,
                                             const QVariantMap &payload)
{
    auto promise = QSharedPointer<QPromise<QVariantMap>>::create();
    promise->start();
    const QFuture<QVariantMap> future = promise->future();
    const auto complete = [promise](const QVariantMap &answer) {
        promise->addResult(answer);
        promise->finish();
    };
    if (action == QLatin1String("shell.surfaces")) {
        const QVariantList names = payload.value(QStringLiteral("names")).toList();
        QSet<QString> registered;
        for (const QVariant &value : names) {
            if (value.metaType().id() != QMetaType::QString) {
                complete(fail(QStringLiteral("Surface names must be strings.")));
                return future;
            }
            const QString name = value.toString();
            if (name.startsWith(QLatin1String("page."))
                || name.startsWith(QLatin1String("detail.")))
                registered.insert(name);
        }
        m_surfaces = registered;
        complete({{QStringLiteral("ok"), true}});
        return future;
    }
    if (const auto *registered = ActionRegistry::find(action)) {
        if (!registered->valid(payload)) {
            complete(fail(QStringLiteral("Action details are invalid.")));
            return future;
        }
        registered->handle(*this, payload, complete);
        return future;
    }
    const QVariantMap item = payload.value(QStringLiteral("item")).toMap();
    const QVariantMap ref = item.value(QStringLiteral("ref")).toMap();
    if (action == QLatin1String("continue.forget")) {
        if (!m_progress || ref.value(QStringLiteral("kind")).toString().isEmpty()
            || ref.value(QStringLiteral("id")).toString().isEmpty())
            complete(fail(QStringLiteral("Continue item is unavailable.")));
        else {
        m_progress->forget(ref.value(QStringLiteral("kind")).toString(),
                           ref.value(QStringLiteral("id")).toString());
            complete({{QStringLiteral("ok"), true}});
        }
        return future;
    }
    if (action == QLatin1String("collection.add")
        || action == QLatin1String("collection.remove")) {
        if (!m_collection || item.isEmpty()) {
            complete(fail(QStringLiteral("Collection item is unavailable.")));
            return future;
        }
        const QString world = item.value(QStringLiteral("world")).toString().toLower();
        const QString key = ref.value(QStringLiteral("id")).toString();
        if (world.isEmpty() || key.isEmpty()) {
            complete(fail(QStringLiteral("Collection identity is missing.")));
            return future;
        }
        QVariantMap entry = ref;
        entry.insert(QStringLiteral("id"), key);
        entry.insert(QStringLiteral("title"), item.value(QStringLiteral("title")));
        entry.insert(QStringLiteral("cover"), item.value(QStringLiteral("cover")));
        entry.insert(QStringLiteral("type"), item.value(QStringLiteral("kind")));
        const bool ok = action == QLatin1String("collection.add")
            ? m_collection->add(world, entry) : m_collection->remove(world, key);
        complete(ok ? QVariantMap{{QStringLiteral("ok"), true}}
                    : fail(QStringLiteral("Collection could not be changed.")));
        return future;
    }
    if (action == QLatin1String("search.history.remove")
        || action == QLatin1String("search.history.clear")) {
        if (!m_history) {
            complete(fail(QStringLiteral("Search history is unavailable.")));
            return future;
        }
        QString scope = payload.value(QStringLiteral("scope")).toString();
        // Contract actions carry only a query. Resolve the current search
        // subscription when the web layer omits its route scope.
        if (scope.isEmpty()) {
            for (const auto &sub : std::as_const(m_subscriptions)) {
                if (sub.feed != QLatin1String("search")) continue;
                const QString candidate = sub.params.value(QStringLiteral("scope")).toString();
                if (!scope.isEmpty() && scope != candidate) {
                    complete(fail(QStringLiteral("Search scope is ambiguous.")));
                    return future;
                }
                scope = candidate;
            }
        }
        if (scope.isEmpty()) {
            complete(fail(QStringLiteral("Search scope is missing.")));
            return future;
        }
        if (action == QLatin1String("search.history.remove"))
            m_history->remove(scope, payload.value(QStringLiteral("query")).toString());
        else
            m_history->clear(scope);
        complete({{QStringLiteral("ok"), true}});
        return future;
    }
    static const QStringList doors{
        QStringLiteral("open"), QStringLiteral("open.vault"),
        QStringLiteral("open.universe"), QStringLiteral("open.universeHall"),
        QStringLiteral("open.native"), QStringLiteral("window.minimize"),
        QStringLiteral("window.fullscreen"), QStringLiteral("window.close")};
    if (!doors.contains(action)) {
        complete(fail(QStringLiteral("Unsupported action.")));
        return future;
    }
    if (action == QLatin1String("open") &&
        (item.isEmpty() || ref.isEmpty())) {
        complete(fail(QStringLiteral("Item identity is missing.")));
        return future;
    }
    if (action == QLatin1String("open")
        && payload.value(QStringLiteral("intent")).toString() != QLatin1String("resume")
        && payload.value(QStringLiteral("intent")).toString() != QLatin1String("nextUp")) {
        QString kind;
        QVariantMap params;
        const QString world = item.value(QStringLiteral("world")).toString();
        const QString itemKind = item.value(QStringLiteral("kind")).toString();
        if (itemKind == QLatin1String("universe")) {
            kind = QStringLiteral("universe");
            params.insert(QStringLiteral("extensionId"), ref.value(QStringLiteral("extensionId")));
            params.insert(QStringLiteral("name"), ref.value(QStringLiteral("name")));
        } else if (world == QLatin1String("Theatre")) {
            kind = QStringLiteral("theatre");
            QString id = ref.value(QStringLiteral("tt")).toString();
            if (id.isEmpty()) id = ref.value(QStringLiteral("id")).toString();
            if (itemKind == QLatin1String("anime") && !ref.value(QStringLiteral("mal_id")).toString().isEmpty())
                id = QStringLiteral("mal:") + ref.value(QStringLiteral("mal_id")).toString();
            params.insert(QStringLiteral("id"), id);
            params.insert(QStringLiteral("type"), itemKind == QLatin1String("movie") ? QStringLiteral("movie") : QStringLiteral("series"));
        } else if (world == QLatin1String("Biblio")) {
            kind = QStringLiteral("book");
            params.insert(QStringLiteral("id"), ref.value(QStringLiteral("id")));
        } else if (world == QLatin1String("Tankoban")) {
            const QString id = ref.value(QStringLiteral("id")).toString();
            kind = itemKind == QLatin1String("comic") || id.startsWith(QLatin1String("gc:"))
                || id.startsWith(QLatin1String("gcd:")) || id.startsWith(QLatin1String("locg:"))
                ? QStringLiteral("comic") : QStringLiteral("manga");
            QString identity = id;
            if (identity.isEmpty() && !ref.value(QStringLiteral("gcdId")).toString().isEmpty())
                identity = QStringLiteral("gcd:") + ref.value(QStringLiteral("gcdId")).toString();
            if (identity.isEmpty() && !ref.value(QStringLiteral("locgId")).toString().isEmpty())
                identity = QStringLiteral("locg:") + ref.value(QStringLiteral("locgId")).toString();
            if (identity.isEmpty()) identity = ref.value(QStringLiteral("mal_id")).toString();
            if (identity.isEmpty()) identity = ref.value(QStringLiteral("seriesId")).toString();
            params.insert(QStringLiteral("id"), identity);
        }
        if (hasSurface(QStringLiteral("detail.") + kind) && !kind.isEmpty()) {
            if (params.value(kind == QLatin1String("universe") ? QStringLiteral("extensionId") : QStringLiteral("id")).toString().isEmpty()) {
                complete(fail(QStringLiteral("Item identity is missing.")));
                return future;
            }
            params.insert(QStringLiteral("title"), item.value(QStringLiteral("title")));
            params.insert(QStringLiteral("cover"), item.value(QStringLiteral("cover")));
            complete({{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
                {QStringLiteral("route"), QVariantMap{{QStringLiteral("name"), QStringLiteral("detail")},
                    {QStringLiteral("kind"), kind}, {QStringLiteral("params"), params}}}}}});
            return future;
        }
    }
    if (action == QLatin1String("open.native")) {
        const QString door = payload.value(QStringLiteral("door")).toString();
        if (hasSurface(QStringLiteral("page.") + door)) {
            QVariantMap params;
            if (door == QLatin1String("extensions") || door == QLatin1String("wallpaperSearch"))
                params.insert(QStringLiteral("world"), payload.value(QStringLiteral("world")));
            complete({{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
                {QStringLiteral("route"), QVariantMap{{QStringLiteral("name"), QStringLiteral("page")},
                    {QStringLiteral("page"), door}, {QStringLiteral("params"), params}}}}}});
            return future;
        }
    }
    const int requestId = m_nextAction++;
    m_pendingActions.insert(requestId, promise);
    emit actionRequested(action, payload, requestId);
    return future;
}

void ColosseumWebBridge::finishAction(int requestId, bool ok,
                                       const QString &error,
                                       const QVariant &result)
{
    const auto promise = m_pendingActions.take(requestId);
    if (!promise) return;
    promise->addResult(ok
        ? QVariantMap{{QStringLiteral("ok"), true}, {QStringLiteral("result"), result}}
        : fail(error.isEmpty() ? QStringLiteral("Action failed.") : error));
    promise->finish();
}
