#include "ColosseumWebBridge.h"
#include "feeds/ActionRegistry.h"
#include "feeds/FeedRegistry.h"
#include "feeds/FeedValue.h"
#include "WallpaperSchemeHandler.h"

#include "../CollectionStore.h"
#include "../account/HistoryStore.h"
#include "../engine/ExtensionsStore.h"
#include "../engine/LocalDownloads.h"
#include "../engine/MangaDownloader.h"
#include "../engine/MangaTankobanService.h"
#include "../ProgressStore.h"
#include "../SearchHistoryStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QQmlContext>
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

void ColosseumWebBridge::bindNativeContext(QQmlContext *context)
{
    m_nativeContext = context;
    if (auto *downloads = qobject_cast<MangaDownloader *>(service(QStringLiteral("Downloads")))) {
        connect(downloads, &MangaDownloader::progress, this,
                [this] { storeChanged(true); });
        connect(downloads, &MangaDownloader::finished, this,
                [this] { storeChanged(true); });
        connect(downloads, &MangaDownloader::failed, this,
                [this] { storeChanged(true); });
    }
    if (auto *volumes = qobject_cast<MangaTankobanService *>(service(QStringLiteral("TankobanVolumes")))) {
        connect(volumes, &MangaTankobanService::progress, this,
                [this] { storeChanged(true); });
        connect(volumes, &MangaTankobanService::finished, this,
                [this] { storeChanged(true); });
        connect(volumes, &MangaTankobanService::failed, this,
                [this] { storeChanged(true); });
    }
}

QObject *ColosseumWebBridge::service(const QString &name) const
{
    return m_nativeContext ? m_nativeContext->contextProperty(name).value<QObject *>() : nullptr;
}

bool ColosseumWebBridge::detailActive(const QString &feed, const QString &identity) const
{
    for (auto it = m_subscriptions.cbegin(); it != m_subscriptions.cend(); ++it)
        if (it->feed == feed && it->params.value(QStringLiteral("id")).toString() == identity)
            return true;
    return false;
}

bool ColosseumWebBridge::isDetailSubscription(int subscriptionId, const QString &feed,
                                               const QString &identity) const
{
    const auto it = m_subscriptions.constFind(subscriptionId);
    return it != m_subscriptions.cend() && it->feed == feed
        && it->params.value(QStringLiteral("id")).toString() == identity;
}

QVariantMap ColosseumWebBridge::detailParams(const QString &feed, const QString &identity) const
{
    for (auto it = m_subscriptions.cbegin(); it != m_subscriptions.cend(); ++it)
        if (it->feed == feed && it->params.value(QStringLiteral("id")).toString() == identity)
            return it->params;
    return {};
}

QVariantMap ColosseumWebBridge::detailRow(const QString &feed, const QString &identity,
                                          const QString &sectionId,
                                          const QString &rowId) const
{
    for (auto it = m_subscriptions.cbegin(); it != m_subscriptions.cend(); ++it) {
        if (it->feed != feed || it->params.value(QStringLiteral("id")).toString() != identity)
            continue;
        const QVariantList rows = it->sections.value(sectionId).value(QStringLiteral("data"))
            .toMap().value(QStringLiteral("rows")).toList();
        for (const QVariant &value : rows) {
            const QVariantMap row = value.toMap();
            if (row.value(QStringLiteral("id")).toString() == rowId
                || row.value(QStringLiteral("key")).toString() == rowId) return row;
        }
    }
    return {};
}

bool ColosseumWebBridge::updateDetail(const QString &feed, const QString &identity,
                                      const QVariantMap &patch)
{
    bool found = false;
    for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
        if (it->feed != feed || it->params.value(QStringLiteral("id")).toString() != identity)
            continue;
        for (auto field = patch.cbegin(); field != patch.cend(); ++field)
            it->params.insert(field.key(), field.value());
        if (patch.contains(QStringLiteral("view"))) it->visibleCount = 0;
        const int subscriptionId = it.key();
        if (patch.contains(QStringLiteral("sourceTarget")) && it->sections.contains(QStringLiteral("sources"))) {
            QVariantMap loading = it->sections.value(QStringLiteral("sources"));
            loading.insert(QStringLiteral("state"), QStringLiteral("loading"));
            QVariantMap data = loading.value(QStringLiteral("data")).toMap();
            data.insert(QStringLiteral("targetId"), patch.value(QStringLiteral("sourceTarget")));
            data.insert(QStringLiteral("rows"), QVariantList{});
            loading.insert(QStringLiteral("data"), data);
            it->sections.insert(QStringLiteral("sources"), loading);
            publish(subscriptionId, {{QStringLiteral("type"), QStringLiteral("section")},
                                     {QStringLiteral("section"), loading}});
        }
        refresh(subscriptionId);
        found = true;
    }
    return found;
}

void ColosseumWebBridge::delegateAction(const QString &action,
                                        const QVariantMap &payload,
                                        std::function<void(const QVariantMap &)> complete)
{
    const int requestId = m_nextAction++;
    m_delegatedActions.insert(requestId, std::move(complete));
    emit actionRequested(action, payload, requestId);
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
    m_profileActive = true;
    for (const int id : m_subscriptions.keys()) {
        bindOwnerSignals(id);
        reset(id);
    }
    startRecorderSweep();
}

void ColosseumWebBridge::bindExtensionsStore(ExtensionsStore *extensions)
{
    if (m_extensions) disconnect(m_extensions, nullptr, this, nullptr);
    m_extensions = extensions;
    if (m_extensions)
        connect(m_extensions, &ExtensionsStore::changed, this,
                [this] { storeChanged(false); });
    storeChanged(false);
}

void ColosseumWebBridge::startRecorderSweep()
{
    if (m_recordDirectory.isEmpty() || m_recorderSweepStarted) return;
    m_recorderSweepStarted = true;
    QSaveFile shellFile(QDir(m_recordDirectory).filePath(QStringLiteral("shell.json")));
    if (shellFile.open(QIODevice::WriteOnly)) {
        shellFile.write(QJsonDocument(QJsonObject::fromVariantMap(shellState())).toJson());
        shellFile.commit();
    }
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
    auto recordRoute = [&requests](const QString &world, const QString &source,
                                   const QVariantMap &facet) {
        requests.append({QStringLiteral("seeAll"),
                         {{QStringLiteral("route"), QVariantMap{
                             {QStringLiteral("v"), 1},
                             {QStringLiteral("world"), world},
                             {QStringLiteral("source"), source},
                             {QStringLiteral("facet"), facet},
                             {QStringLiteral("explicit"), false},
                             {QStringLiteral("pageSize"), 24}}}}});
    };
    for (const auto &worldMedium : {
             QPair<QString, QString>{QStringLiteral("Tankoban"), QStringLiteral("manga")},
             {QStringLiteral("Biblio"), QStringLiteral("book")},
             {QStringLiteral("Theatre"), QStringLiteral("movie")}}) {
        QVariantMap facet{{QStringLiteral("medium"), worldMedium.second}};
        recordRoute(worldMedium.first, QStringLiteral("catalogue"), facet);
        recordRoute(worldMedium.first, QStringLiteral("genreIndex"), facet);
        facet.insert(QStringLiteral("name"), worldMedium.first == QLatin1String("Biblio")
            ? QStringLiteral("Fiction & Literature") : QStringLiteral("Action"));
        if (worldMedium.first == QLatin1String("Biblio"))
            facet.insert(QStringLiteral("key"), QStringLiteral("9031"));
        recordRoute(worldMedium.first, QStringLiteral("genre"), facet);
    }
    for (const QString &scope : {QStringLiteral("all"), QStringLiteral("Tankoban"),
                                 QStringLiteral("Biblio"), QStringLiteral("Theatre")}) {
        requests.append({QStringLiteral("search"),
                         {{QStringLiteral("scope"), scope},
                          {QStringLiteral("query"), QString()}}});
        requests.append({QStringLiteral("search"),
                         {{QStringLiteral("scope"), scope},
                          {QStringLiteral("query"), QStringLiteral("Dragon")}}});
    }
    requests.append({QStringLiteral("detail.theatre"),
                     {{QStringLiteral("id"), QStringLiteral("tt0944947")},
                      {QStringLiteral("type"), QStringLiteral("series")},
                      {QStringLiteral("title"), QStringLiteral("Game of Thrones")}}});
    requests.append({QStringLiteral("detail.manga"),
                     {{QStringLiteral("id"), QStringLiteral("mal:13")},
                      {QStringLiteral("title"), QStringLiteral("One Piece")}}});
    for (int i = 0; i < requests.size(); ++i) {
        const auto request = requests.at(i);
        QTimer::singleShot(500 * i, this, [this, request] {
            subscribe(request.first, request.second);
        });
    }
}

void ColosseumWebBridge::suspendProfile()
{
    m_profileActive = false;
    for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it)
        disconnectOwnerSignals(it.value());
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
    if (feed.startsWith(QLatin1String("detail."))) sub.visibleCount = 0;
    m_subscriptions.insert(id, sub);
    bindOwnerSignals(id);
    if (feed == QLatin1String("search") && m_history) {
        const QString query = params.value(QStringLiteral("query")).toString().trimmed();
        if (query.size() >= 2)
            m_history->record(params.value(QStringLiteral("scope")).toString(), query);
    }
    QTimer::singleShot(0, this, [this, id] { reset(id); });
    return {{QStringLiteral("ok"), true}, {QStringLiteral("id"), id},
            {QStringLiteral("generation"), sub.generation + 1}};
}

void ColosseumWebBridge::unsubscribe(int id)
{
    auto it = m_subscriptions.find(id);
    if (it != m_subscriptions.end()) disconnectOwnerSignals(it.value());
    m_subscriptions.remove(id);
    m_recordedEvents.remove(id);
}

void ColosseumWebBridge::disconnectOwnerSignals(Subscription &subscription)
{
    for (const auto &connection : subscription.ownerConnections)
        QObject::disconnect(connection);
    subscription.ownerConnections.clear();
}

void ColosseumWebBridge::bindOwnerSignals(int id)
{
    auto it = m_subscriptions.find(id);
    if (it == m_subscriptions.end()) return;
    disconnectOwnerSignals(it.value());
    if (!m_profileActive) return;
    const auto *entry = FeedRegistry::find(it->feed, it->params);
    if (!entry) return;
    const int profileRevision = m_profileRevision;
    for (const auto &signal : entry->ownerSignals) {
        QObject *owner = service(signal.service);
        if (!owner || !signal.bind) continue;
        const QPointer<QObject> guarded(owner);
        const QString serviceName = signal.service;
        auto connection = signal.bind(owner, this, [this, id, profileRevision,
                                                     guarded, serviceName] {
            if (!m_profileActive || m_profileRevision != profileRevision || !guarded
                || service(serviceName) != guarded.data()
                || !m_subscriptions.contains(id)) return;
            refresh(id);
        });
        it->ownerConnections.append(connection);
    }
}

QVariantMap ColosseumWebBridge::more(int id, const QString &sectionId)
{
    auto it = m_subscriptions.find(id);
    if (it == m_subscriptions.end()) return fail(QStringLiteral("Subscription is closed."));
    if ((it->feed != QLatin1String("continue") && it->feed != QLatin1String("seeAll")
         && it->feed != QLatin1String("detail.theatre")
         && it->feed != QLatin1String("detail.manga"))
        || !it->sections.contains(sectionId)
        || !it->sections.value(sectionId).value(QStringLiteral("hasMore")).toBool())
        return fail(QStringLiteral("No more items in that section."));
    it->visibleCount += (it->feed.startsWith(QLatin1String("detail.")) ? 100 : 24);
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
    context.subscriptionId = id;
    context.generation = generation;
    context.visibleCount = it->visibleCount;
    context.paths = m_paths;
    context.showExplicit = m_showExplicit;
    if (entry.needsProgress && m_progress)
        context.recent = m_progress->recent(QString(), 0);
    if (entry.needsCollection && m_collection)
        context.collection = m_collection->items((it->feed == QLatin1String("seeAll")
            ? context.params.value(QStringLiteral("route")).toMap().value(QStringLiteral("world"))
            : context.params.value(QStringLiteral("world"))).toString().toLower());
    if (entry.needsExtensions && m_extensions)
        context.extensions = m_extensions->installed();
    if (entry.needsHistory && m_history)
        context.history = m_history->list(
            context.params.value(QStringLiteral("scope")).toString());
    if (entry.capture)
        entry.capture(*this, context);
    if (it->feed == QLatin1String("world")
        && context.params.value(QStringLiteral("world")).toString() == QLatin1String("Theatre")
        && context.params.value(QStringLiteral("tab")).toString() == QLatin1String("library")) {
        auto *history = qobject_cast<HistoryStore *>(service(QStringLiteral("ProfileHistory")));
        for (const QVariant &value : context.collection) {
            const QVariantMap row = value.toMap();
            const QString key = row.value(QStringLiteral("id")).toString();
            if (key.isEmpty()) continue;
            QVariantMap facts;
            if (m_progress) {
                facts.insert(QStringLiteral("mark"), m_progress->watchedMark(key));
                facts.insert(QStringLiteral("manual"), m_progress->watchedMarkIsManual(key));
                facts.insert(QStringLiteral("actionAt"), m_progress->watchedMarkActionAt(key));
            }
            if (history && row.value(QStringLiteral("type")).toString() != QLatin1String("series"))
                facts.insert(QStringLiteral("completedAt"), history->get(
                    QStringLiteral("movie"), key).value(QStringLiteral("completedAt")));
            context.libraryFacts.insert(key, facts);
        }
        if (auto *downloads = qobject_cast<LocalDownloads *>(service(QStringLiteral("LocalDownloads")))) {
            QSet<QString> downloaded;
            for (const QVariant &value : downloads->series(QStringLiteral("theatre"))) {
                const QVariantMap series = value.toMap();
                const QString key = series.value(QStringLiteral("key")).toString();
                const QVariantList items = downloads->items(QStringLiteral("theatre"), key);
                QString id = items.isEmpty() ? QString() : items.first().toMap()
                    .value(QStringLiteral("id")).toString();
                if (series.value(QStringLiteral("kind")).toString() != QLatin1String("movie")) {
                    const QStringList parts = id.split(QLatin1Char(':'));
                    if (parts.size() > 2) id = parts.mid(0, parts.size() - 2).join(QLatin1Char(':'));
                } else if (id.isEmpty() && key.startsWith(QLatin1String("movie:"))) {
                    id = key.mid(6);
                }
                if (!id.isEmpty()) downloaded.insert(id);
            }
            context.downloadedIds = downloaded.values();
        }
    }
    auto *watcher = new QFutureWatcher<QVariantList>(this);
    connect(watcher, &QFutureWatcher<QVariantList>::finished, this,
            [this, watcher, id, generation, requestVersion, entry, context] {
        const QVariantList sections = watcher->result();
        watcher->deleteLater();
        applySections(id, generation, requestVersion, sections);
        if (!entry.enrich) return;
        auto sub = m_subscriptions.constFind(id);
        if (sub == m_subscriptions.cend() || sub->generation != generation
            || sub->requestVersion != requestVersion) return;
        FeedContext followUp = context;
        followUp.baseSections = sections;
        if (entry.enrichCapture)
            entry.enrichCapture(*this, followUp);
        auto *second = new QFutureWatcher<QVariantList>(this);
        connect(second, &QFutureWatcher<QVariantList>::finished, this,
                [this, second, id, generation, requestVersion] {
            const QVariantList updated = second->result();
            second->deleteLater();
            applySections(id, generation, requestVersion, updated);
        });
        second->setFuture(QtConcurrent::run([entry, followUp] {
            return entry.enrich(followUp);
        }));
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
        if (progress && (it->feed == QLatin1String("detail.theatre")
            || it->feed == QLatin1String("detail.manga")
            || it->feed == QLatin1String("continue")
            || (it->feed == QLatin1String("world")
                && it->params.value(QStringLiteral("world")).toString() == QLatin1String("Theatre")))) {
            if (it->pendingProgress) continue;
            it->pendingProgress = true;
            QTimer::singleShot(1000, this, [this, id] {
                auto sub = m_subscriptions.find(id);
                if (sub == m_subscriptions.end()) return;
                sub->pendingProgress = false;
                refresh(id);
            });
        } else if (!progress && (it->feed == QLatin1String("world")
                                 || it->feed == QLatin1String("search")
                                 || it->feed == QLatin1String("home"))) {
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
    if (auto delegated = m_delegatedActions.take(requestId)) {
        delegated(ok ? QVariantMap{{QStringLiteral("ok"), true}, {QStringLiteral("result"), result}}
                     : fail(error.isEmpty() ? QStringLiteral("Action failed.") : error));
        return;
    }
    const auto promise = m_pendingActions.take(requestId);
    if (!promise) return;
    promise->addResult(ok
        ? QVariantMap{{QStringLiteral("ok"), true}, {QStringLiteral("result"), result}}
        : fail(error.isEmpty() ? QStringLiteral("Action failed.") : error));
    promise->finish();
}
