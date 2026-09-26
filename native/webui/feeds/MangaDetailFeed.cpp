#include "ActionRegistry.h"
#include "FeedRegistry.h"
#include "FeedValue.h"
#include "../ColosseumWebBridge.h"

#include "../../CollectionStore.h"
#include "../../MangaEngine.h"
#include "../../ProgressStore.h"
#include "../../engine/MalCatalog.h"
#include "../../engine/MangaDownloader.h"
#include "../../engine/MangaTankobanService.h"
#include "../../engine/ExtensionsStore.h"
#include "../../engine/TankobanCatalog.h"

#include <QHash>
#include <QPointer>
#include <QCryptographicHash>
#include <QSharedPointer>
#include <QTimer>
#include <QUuid>

namespace {
const QString kFeed = QStringLiteral("detail.manga");

struct ChapterState {
    QString requestId;
    QString sourceSeriesId;
    QVariantList rows;
    QString error;
    bool loading = false;
};
QHash<QString, ChapterState> chapterStates;
QHash<QString, QVariantList> volumeSources;
QHash<QString, QVariantMap> volumeSourceChoices;
QHash<QString, QVariantMap> chapterSourceChoices;

QString chapterSourceKey(const FeedContext &ctx, const QString &language,
                         const QString &providerId)
{
    static const QString salt = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QByteArray identity = (salt + QLatin1Char('|')
        + ctx.params.value(QStringLiteral("id")).toString() + QLatin1Char('|')
        + language + QLatin1Char('|') + providerId + QLatin1Char('|')
        + QString::number(ctx.subscriptionId) + QLatin1Char('|')
        + QString::number(ctx.generation)).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(
        identity, QCryptographicHash::Sha256).toHex().left(24));
}

QString volumeKey(const QString &seriesId, const QString &volumeId)
{
    return seriesId + QLatin1Char('|') + volumeId;
}

QString keyFor(const QVariantMap &params)
{
    const QVariantMap view = params.value(QStringLiteral("view")).toMap();
    return params.value(QStringLiteral("id")).toString() + QLatin1Char('|')
        + view.value(QStringLiteral("language")).toString() + QLatin1Char('|')
        + view.value(QStringLiteral("sourceKey")).toString();
}

QVariantMap chapterFor(ColosseumWebBridge &bridge, const QString &id, const QString &chapterId)
{
    const QString key = keyFor(bridge.detailParams(kFeed, id));
    for (const QVariant &value : chapterStates.value(key).rows) {
        const QVariantMap chapter = value.toMap();
        if (chapter.value(QStringLiteral("id")).toString() == chapterId) return chapter;
    }
    return {};
}

QVariantMap section(const QString &id, int index, const QString &title,
                    const QString &state, const QVariantMap &data = {}, bool more = false)
{
    auto value = WebFeedValue::section(id, index, title, QStringLiteral("custom"), {}, state, more);
    if (!data.isEmpty()) value.insert(QStringLiteral("data"), data);
    return value;
}

bool valid(const QVariantMap &params)
{
    const QString id = params.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || id.size() > 180) return false;
    const QVariantMap view = params.value(QStringLiteral("view")).toMap();
    const QString mode = view.value(QStringLiteral("mode"), QStringLiteral("volumes")).toString();
    return mode == QLatin1String("volumes") || mode == QLatin1String("chapters");
}

QVariantList initial(const QVariantMap &)
{
    return {section(QStringLiteral("header"), 0, QStringLiteral("Series"), QStringLiteral("loading")),
            section(QStringLiteral("modes"), 1, QStringLiteral("Browse"), QStringLiteral("loading")),
            section(QStringLiteral("volumes"), 2, QStringLiteral("Volumes"), QStringLiteral("loading")),
            section(QStringLiteral("chapters"), 3, QStringLiteral("Chapters"), QStringLiteral("loading")),
            section(QStringLiteral("sources"), 4, QStringLiteral("Sources"), QStringLiteral("loading")),
            section(QStringLiteral("downloads"), 5, QStringLiteral("Downloads"), QStringLiteral("loading"))};
}

void capture(ColosseumWebBridge &bridge, FeedContext &ctx)
{
    const QString id = ctx.params.value(QStringLiteral("id")).toString();
    const QVariantMap view = ctx.params.value(QStringLiteral("view")).toMap();
    const QString titleHint = ctx.params.value(QStringLiteral("title")).toString();
    auto *mal = qobject_cast<MalCatalog *>(bridge.service(QStringLiteral("MalCatalog")));
    auto *tankoban = qobject_cast<TankobanCatalog *>(bridge.service(QStringLiteral("TankobanCatalog")));
    auto *volumes = qobject_cast<MangaTankobanService *>(bridge.service(QStringLiteral("TankobanVolumes")));
    auto *manga = qobject_cast<MangaEngine *>(bridge.service(QStringLiteral("Manga")));
    auto *downloads = qobject_cast<MangaDownloader *>(bridge.service(QStringLiteral("Downloads")));
    auto *collection = qobject_cast<CollectionStore *>(bridge.service(QStringLiteral("Collection")));
    auto *progress = qobject_cast<ProgressStore *>(bridge.service(QStringLiteral("Progress")));

    int malId = 0;
    if (id.startsWith(QLatin1String("mal:"))) malId = id.mid(4).toInt();
    if (!malId && mal && !titleHint.isEmpty()) {
        const QVariantList candidates = mal->matchByTitle(titleHint, 0, QStringLiteral("manga"));
        if (candidates.size() == 1) malId = candidates.first().toMap().value(QStringLiteral("mal_id")).toInt();
    }
    const QVariantMap row = mal && malId > 0 ? mal->mangaById(malId) : QVariantMap{};
    ctx.nativeSnapshot.insert(QStringLiteral("catalogue"), row);
    ctx.nativeSnapshot.insert(QStringLiteral("malId"), malId);
    const QString seriesId = malId > 0 ? QStringLiteral("mal:%1").arg(malId) : id;
    if (tankoban && volumes && !row.isEmpty()) {
        QVariantList seeds;
        for (const QVariant &value : tankoban->volumes(malId)) {
            const QVariantMap volume = value.toMap();
            seeds.append(QVariantMap{{QStringLiteral("number"), volume.value(QStringLiteral("number"))},
                                     {QStringLiteral("cover"), volume.value(QStringLiteral("cover"))},
                                     {QStringLiteral("title"), volume.value(QStringLiteral("name"))}});
        }
        if (!seeds.isEmpty() && volumes->volumesForSeries(seriesId).isEmpty()) {
            QVariantList authors = row.value(QStringLiteral("authors")).toList();
            QString author;
            for (const QVariant &value : authors) {
                if (!author.isEmpty()) author += QStringLiteral(", ");
                author += value.toMap().value(QStringLiteral("name")).toString();
            }
            volumes->prepareSeries({{QStringLiteral("seriesId"), seriesId},
                                    {QStringLiteral("title"), row.value(QStringLiteral("title"), titleHint)},
                                    {QStringLiteral("author"), author}}, seeds, {});
        }
        ctx.nativeSnapshot.insert(QStringLiteral("volumes"), volumes->volumesForSeries(seriesId));
        QVariantList volumeStatus;
        for (const QVariant &value : ctx.nativeSnapshot.value(QStringLiteral("volumes")).toList()) {
            const QString volumeId = value.toMap().value(QStringLiteral("id")).toString();
            volumeStatus.append(volumes->statusOf(volumeId));
        }
        ctx.nativeSnapshot.insert(QStringLiteral("volumeStatus"), volumeStatus);
    }
    if (collection) ctx.nativeSnapshot.insert(QStringLiteral("saved"), collection->has(QStringLiteral("tankoban"), seriesId));
    if (progress) {
        ctx.nativeSnapshot.insert(QStringLiteral("volumeProgress"),
            progress->get(QStringLiteral("tankoban"), seriesId));
        ctx.nativeSnapshot.insert(QStringLiteral("chapterProgress"),
            progress->get(QStringLiteral("manga"), seriesId));
    }
    bool chapterEnabled = false;
    if (auto *extensions = qobject_cast<ExtensionsStore *>(bridge.service(QStringLiteral("Extensions")))) {
        for (const QVariant &value : extensions->installed()) {
            const QVariantMap extension = value.toMap();
            if (extension.value(QStringLiteral("id")) == QLatin1String("colosseum.well.tankoyomi"))
                chapterEnabled = extension.value(QStringLiteral("enabled")).toBool();
        }
    }
    ctx.nativeSnapshot.insert(QStringLiteral("chapterEnabled"), chapterEnabled);
    if (manga) {
        const QVariantList languages = manga->chapterLanguages();
        ctx.nativeSnapshot.insert(QStringLiteral("languages"), languages);
        const QString language = view.value(QStringLiteral("language"), manga->chapterDefaultLanguage()).toString();
        ctx.nativeSnapshot.insert(QStringLiteral("language"), language);
        QVariantList providerRows;
        for (const QVariant &value : manga->chapterProviders(language)) {
            QVariantMap provider = value.toMap();
            const QString providerId = provider.value(QStringLiteral("id")).toString();
            if (providerId.isEmpty()) continue;
            const QString key = chapterSourceKey(ctx, language, providerId);
            provider.insert(QStringLiteral("sourceKey"), key);
            chapterSourceChoices.insert(key, {{QStringLiteral("seriesId"), id},
                {QStringLiteral("language"), language}, {QStringLiteral("providerId"), providerId}});
            providerRows.append(provider);
        }
        ctx.nativeSnapshot.insert(QStringLiteral("providers"), providerRows);
        if (view.value(QStringLiteral("mode")) == QLatin1String("chapters") && !chapterEnabled)
            ctx.nativeSnapshot.insert(QStringLiteral("chapterError"),
                QStringLiteral("Tankoyomi is off. Enable it in Extensions to load chapters."));
        if (view.value(QStringLiteral("mode")) == QLatin1String("chapters") && chapterEnabled && !row.isEmpty()) {
            const QString key = keyFor(ctx.params);
            auto &state = chapterStates[key];
            if (state.requestId.isEmpty() && !state.loading && state.rows.isEmpty()) {
                state.loading = true;
                state.requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                const QString requestId = state.requestId;
                QPointer<ColosseumWebBridge> guardedBridge(&bridge);
                const auto success = QSharedPointer<QMetaObject::Connection>::create();
                const auto failure = QSharedPointer<QMetaObject::Connection>::create();
                *success = QObject::connect(manga, &MangaEngine::chapterCatalogueResults, &bridge,
                    [key, requestId, id, guardedBridge, success, failure](const QString &incoming, const QString &sourceId,
                                                         const QVariantList &rows) {
                        if (incoming != requestId) return;
                        QObject::disconnect(*success);
                        QObject::disconnect(*failure);
                        auto &slot = chapterStates[key];
                        slot.loading = false;
                        slot.sourceSeriesId = sourceId;
                        slot.rows = rows;
                        slot.error.clear();
                        if (guardedBridge) guardedBridge->updateDetail(kFeed, id, {});
                    });
                *failure = QObject::connect(manga, &MangaEngine::chapterCatalogueFailed, &bridge,
                    [key, requestId, id, guardedBridge, success, failure](const QString &incoming, const QString &message) {
                        if (incoming != requestId) return;
                        QObject::disconnect(*success);
                        QObject::disconnect(*failure);
                        auto &slot = chapterStates[key];
                        slot.loading = false;
                        slot.error = message.isEmpty() ? QStringLiteral("Chapters are unavailable.") : message;
                        if (guardedBridge) guardedBridge->updateDetail(kFeed, id, {});
                    });
                manga->chapterCatalogueForProfile(requestId,
                    {{QStringLiteral("title"), row.value(QStringLiteral("title"), titleHint)}}, language);
            }
            ctx.nativeSnapshot.insert(QStringLiteral("chapterLoading"), state.loading);
            ctx.nativeSnapshot.insert(QStringLiteral("chapterError"), state.error);
            ctx.nativeSnapshot.insert(QStringLiteral("chapters"), state.rows);
            ctx.nativeSnapshot.insert(QStringLiteral("sourceSeriesId"), state.sourceSeriesId);
        }
    }
    if (downloads) {
        QVariantList states;
        for (const QVariant &value : ctx.nativeSnapshot.value(QStringLiteral("chapters")).toList()) {
            const QString chapterId = value.toMap().value(QStringLiteral("id")).toString();
            states.append(downloads->statusOf(chapterId));
        }
        ctx.nativeSnapshot.insert(QStringLiteral("chapterStatus"), states);
    }
    const QString sourceTarget = ctx.params.value(QStringLiteral("sourceTarget")).toString();
    if (!sourceTarget.isEmpty())
        ctx.nativeSnapshot.insert(QStringLiteral("volumeSources"),
                                  volumeSources.value(volumeKey(id, sourceTarget)));
}

QVariantList build(const FeedContext &ctx)
{
    const QString id = ctx.params.value(QStringLiteral("id")).toString();
    const QVariantMap view = ctx.params.value(QStringLiteral("view")).toMap();
    const QString mode = view.value(QStringLiteral("mode"), QStringLiteral("volumes")).toString();
    const QVariantMap row = ctx.nativeSnapshot.value(QStringLiteral("catalogue")).toMap();
    const int malId = ctx.nativeSnapshot.value(QStringLiteral("malId")).toInt();
    const QString title = row.value(QStringLiteral("title"), ctx.params.value(QStringLiteral("title"))).toString();
    const QString cover = row.value(QStringLiteral("images")).toMap().value(QStringLiteral("jpg")).toMap()
        .value(QStringLiteral("large_image_url"), ctx.params.value(QStringLiteral("cover"))).toString();
    QVariantList genres;
    for (const QVariant &value : row.value(QStringLiteral("genres")).toList())
        genres.append(value.toMap().value(QStringLiteral("name")));
    QString author;
    for (const QVariant &value : row.value(QStringLiteral("authors")).toList()) {
        if (!author.isEmpty()) author += QStringLiteral(", ");
        author += value.toMap().value(QStringLiteral("name")).toString();
    }
    const QVariantList rawVolumes = ctx.nativeSnapshot.value(QStringLiteral("volumes")).toList();
    const QVariantList volumeStates = ctx.nativeSnapshot.value(QStringLiteral("volumeStatus")).toList();
    const QVariantMap volumeProgress = ctx.nativeSnapshot.value(QStringLiteral("volumeProgress")).toMap();
    const QString currentVolume = volumeProgress.value(QStringLiteral("resume")).toMap()
        .value(QStringLiteral("chapterId")).toString();
    QVariantList volumes;
    QVariantList downloadRows;
    for (int i = 0; i < rawVolumes.size(); ++i) {
        const QVariantMap volume = rawVolumes.at(i).toMap();
        const QVariantMap status = volumeStates.value(i).toMap();
        const QString volumeId = volume.value(QStringLiteral("id")).toString();
        const QString state = status.value(QStringLiteral("state"), volume.value(QStringLiteral("state"))).toString();
        const double progress = volumeId == currentVolume
            ? qBound(0.0, volumeProgress.value(QStringLiteral("progress")).toDouble(), 1.0) : 0.0;
        volumes.append(QVariantMap{{QStringLiteral("id"), volumeId},
            {QStringLiteral("number"), volume.value(QStringLiteral("number"))},
            {QStringLiteral("title"), volume.value(QStringLiteral("title"))},
            {QStringLiteral("cover"), volume.value(QStringLiteral("cover"))},
            {QStringLiteral("startChapter"), volume.value(QStringLiteral("chapterStart"))},
            {QStringLiteral("endChapter"), volume.value(QStringLiteral("chapterEnd"))},
            {QStringLiteral("owned"), state == QLatin1String("ready")},
            {QStringLiteral("downloadState"), state}, {QStringLiteral("progress"), progress},
            {QStringLiteral("read"), progress >= 0.85}});
        if (state != QLatin1String("none") && state != QLatin1String("ready"))
            downloadRows.append(QVariantMap{{QStringLiteral("unitKind"), QStringLiteral("volume")},
                {QStringLiteral("unitId"), volumeId}, {QStringLiteral("state"), state},
                {QStringLiteral("done"), status.value(QStringLiteral("done"), 0)},
                {QStringLiteral("total"), status.value(QStringLiteral("total"), 0)},
                {QStringLiteral("error"), status.value(QStringLiteral("error")).toString()}});
    }
    const QVariantList rawChapters = ctx.nativeSnapshot.value(QStringLiteral("chapters")).toList();
    const QVariantList chapterStatesNow = ctx.nativeSnapshot.value(QStringLiteral("chapterStatus")).toList();
    const QVariantMap chapterProgress = ctx.nativeSnapshot.value(QStringLiteral("chapterProgress")).toMap();
    const QString currentChapter = chapterProgress.value(QStringLiteral("resume")).toMap()
        .value(QStringLiteral("chapterId")).toString();
    QVariantList chapters;
    for (int i = 0; i < rawChapters.size(); ++i) {
        const QVariantMap chapter = rawChapters.at(i).toMap();
        const QVariantMap status = chapterStatesNow.value(i).toMap();
        const QString chapterId = chapter.value(QStringLiteral("id")).toString();
        const QString state = status.value(QStringLiteral("state"), QStringLiteral("none")).toString();
        const double progress = chapterId == currentChapter
            ? qBound(0.0, chapterProgress.value(QStringLiteral("progress")).toDouble(), 1.0) : 0.0;
        chapters.append(QVariantMap{{QStringLiteral("id"), chapterId},
            {QStringLiteral("number"), chapter.value(QStringLiteral("number"))},
            {QStringLiteral("title"), chapter.value(QStringLiteral("name"), chapter.value(QStringLiteral("title")))},
            {QStringLiteral("cover"), QString()}, {QStringLiteral("sourceLabel"), chapter.value(QStringLiteral("source"))},
            {QStringLiteral("downloadState"), state}, {QStringLiteral("progress"), progress},
            {QStringLiteral("read"), progress >= 0.85}});
        if (state != QLatin1String("none") && state != QLatin1String("done"))
            downloadRows.append(QVariantMap{{QStringLiteral("unitKind"), QStringLiteral("chapter")},
                {QStringLiteral("unitId"), chapterId}, {QStringLiteral("state"), state},
                {QStringLiteral("done"), status.value(QStringLiteral("done"), 0)},
                {QStringLiteral("total"), status.value(QStringLiteral("total"), 0)},
                {QStringLiteral("error"), status.value(QStringLiteral("error")).toString()}});
    }
    QVariantList languages;
    for (const QVariant &value : ctx.nativeSnapshot.value(QStringLiteral("languages")).toList()) {
        const QVariantMap language = value.toMap();
        languages.append(QVariantMap{{QStringLiteral("code"), language.value(QStringLiteral("code"))},
            {QStringLiteral("label"), language.value(QStringLiteral("label"), language.value(QStringLiteral("name")))},
            {QStringLiteral("providerCount"), language.value(QStringLiteral("enabledProviderCount"), 0)}});
    }
    QVariantList sources = ctx.nativeSnapshot.value(QStringLiteral("volumeSources")).toList();
    const QString sourceTarget = ctx.params.value(QStringLiteral("sourceTarget")).toString();
    if (sourceTarget.isEmpty()) for (const QVariant &value : ctx.nativeSnapshot.value(QStringLiteral("providers")).toList()) {
        const QVariantMap provider = value.toMap();
        sources.append(QVariantMap{{QStringLiteral("key"), provider.value(QStringLiteral("sourceKey"))},
            {QStringLiteral("label"), provider.value(QStringLiteral("name"))},
            {QStringLiteral("language"), ctx.nativeSnapshot.value(QStringLiteral("language"))},
            {QStringLiteral("availability"), provider.value(QStringLiteral("enabled"), true)}});
    }
    const int windowStart = qMax(0, ctx.visibleCount);
    const QVariantList chapterWindow = chapters.mid(windowStart, 100);
    QVariantList out;
    out.append(section(QStringLiteral("header"), 0, QStringLiteral("Series"), row.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
        {{QStringLiteral("schema"), QStringLiteral("manga.header")}, {QStringLiteral("id"), id},
         {QStringLiteral("malId"), malId}, {QStringLiteral("title"), title},
         {QStringLiteral("banner"), cover}, {QStringLiteral("cover"), cover},
         {QStringLiteral("author"), author}, {QStringLiteral("status"), row.value(QStringLiteral("status"))},
         {QStringLiteral("year"), row.value(QStringLiteral("year"))},
         {QStringLiteral("synopsis"), row.value(QStringLiteral("synopsis"))},
         {QStringLiteral("genres"), genres}, {QStringLiteral("score"), row.value(QStringLiteral("score"))},
         {QStringLiteral("saved"), ctx.nativeSnapshot.value(QStringLiteral("saved")).toBool()},
         {QStringLiteral("primaryLabel"), volumes.isEmpty() ? QStringLiteral("Search") : QStringLiteral("Read")},
         {QStringLiteral("primaryState"), volumes.isEmpty() ? QStringLiteral("search") : QStringLiteral("get")}}));
    out.append(section(QStringLiteral("modes"), 1, QStringLiteral("Browse"), QStringLiteral("ready"),
        {{QStringLiteral("schema"), QStringLiteral("manga.modes")}, {QStringLiteral("selected"), mode},
         {QStringLiteral("chapterEnabled"), ctx.nativeSnapshot.value(QStringLiteral("chapterEnabled")).toBool() && !languages.isEmpty()}, {QStringLiteral("languages"), languages},
         {QStringLiteral("selectedLanguage"), ctx.nativeSnapshot.value(QStringLiteral("language"))}}));
    out.append(section(QStringLiteral("volumes"), 2, QStringLiteral("Volumes"), volumes.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
        {{QStringLiteral("schema"), QStringLiteral("manga.volumes")}, {QStringLiteral("rows"), volumes}}));
    QString chapterState = QStringLiteral("empty");
    if (mode == QLatin1String("chapters"))
        chapterState = ctx.nativeSnapshot.value(QStringLiteral("chapterLoading")).toBool() ? QStringLiteral("loading")
            : !ctx.nativeSnapshot.value(QStringLiteral("chapterError")).toString().isEmpty() ? QStringLiteral("error")
            : chapterWindow.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready");
    auto chapterSection = section(QStringLiteral("chapters"), 3, QStringLiteral("Chapters"), chapterState,
        {{QStringLiteral("schema"), QStringLiteral("manga.chapters")},
         {QStringLiteral("sourceSeriesId"), ctx.nativeSnapshot.value(QStringLiteral("sourceSeriesId"))},
         {QStringLiteral("language"), ctx.nativeSnapshot.value(QStringLiteral("language"))},
         {QStringLiteral("windowStart"), windowStart},
         {QStringLiteral("rows"), chapterWindow}},
        chapters.size() > windowStart + chapterWindow.size());
    if (chapterState == QLatin1String("error")) chapterSection.insert(QStringLiteral("error"), ctx.nativeSnapshot.value(QStringLiteral("chapterError")));
    out.append(chapterSection);
    out.append(section(QStringLiteral("sources"), 4, QStringLiteral("Sources"), sources.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
        {{QStringLiteral("schema"), QStringLiteral("manga.sources")},
         {QStringLiteral("targetId"), sourceTarget}, {QStringLiteral("rows"), sources}}));
    out.append(section(QStringLiteral("downloads"), 5, QStringLiteral("Downloads"), downloadRows.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"),
        {{QStringLiteral("schema"), QStringLiteral("manga.downloads")}, {QStringLiteral("rows"), downloadRows}}));
    return out;
}

bool identity(const QVariantMap &payload)
{
    return !payload.value(QStringLiteral("id")).toString().isEmpty();
}
void unavailable(ActionRegistry::Completion done, const QString &message)
{
    done({{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}});
}
bool active(ColosseumWebBridge &bridge, const QVariantMap &payload, ActionRegistry::Completion &done)
{
    if (bridge.detailActive(kFeed, payload.value(QStringLiteral("id")).toString())) return true;
    unavailable(done, QStringLiteral("This series is no longer open."));
    return false;
}

const bool feedRegistered = [] {
    FeedRegistry::Entry entry;
    entry.name = kFeed;
    entry.valid = &valid;
    entry.initial = &initial;
    entry.build = &build;
    entry.capture = &capture;
    return FeedRegistry::add(entry);
}();
const bool modeRegistered = ActionRegistry::add({QStringLiteral("detail.manga.selectMode"),
    [](const QVariantMap &p) { return identity(p) && (p.value(QStringLiteral("mode")) == QLatin1String("volumes")
        || p.value(QStringLiteral("mode")) == QLatin1String("chapters")); },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        if (!active(bridge, p, done)) return;
        auto *manga = qobject_cast<MangaEngine *>(bridge.service(QStringLiteral("Manga")));
        const QString id = p.value(QStringLiteral("id")).toString();
        const QVariantMap currentView = bridge.detailParams(kFeed, id).value(QStringLiteral("view")).toMap();
        const QString language = p.value(QStringLiteral("language"),
            currentView.value(QStringLiteral("language"), manga ? manga->chapterDefaultLanguage() : QString())).toString();
        if (p.value(QStringLiteral("mode")) == QLatin1String("chapters")) {
            bool enabled = false;
            if (auto *extensions = qobject_cast<ExtensionsStore *>(bridge.service(QStringLiteral("Extensions"))))
                for (const QVariant &value : extensions->installed()) {
                    const QVariantMap extension = value.toMap();
                    if (extension.value(QStringLiteral("id")) == QLatin1String("colosseum.well.tankoyomi"))
                        enabled = extension.value(QStringLiteral("enabled")).toBool();
                }
            if (!enabled || !manga)
                return unavailable(done, QStringLiteral("Enable Tankoyomi in Extensions to load chapters."));
        }
        if (manga && !language.isEmpty()) {
            bool known = false;
            for (const QVariant &value : manga->chapterLanguages())
                if (value.toMap().value(QStringLiteral("code")).toString() == language) known = true;
            if (!known) return unavailable(done, QStringLiteral("This chapter language is unavailable."));
        }
        bridge.updateDetail(kFeed, id,
            {{QStringLiteral("view"), QVariantMap{{QStringLiteral("mode"), p.value(QStringLiteral("mode"))},
                                                  {QStringLiteral("language"), language}}},
             {QStringLiteral("sourceTarget"), QString()}});
        done({{QStringLiteral("ok"), true}});
    }});
const bool sourceRegistered = ActionRegistry::add({QStringLiteral("detail.manga.selectSource"),
    [](const QVariantMap &p) { return identity(p) && !p.value(QStringLiteral("sourceKey")).toString().isEmpty(); },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        if (!active(bridge, p, done)) return;
        auto *manga = qobject_cast<MangaEngine *>(bridge.service(QStringLiteral("Manga")));
        if (!manga) return unavailable(done, QStringLiteral("Chapter providers are unavailable."));
        const QString id = p.value(QStringLiteral("id")).toString();
        const QVariantMap current = bridge.detailParams(kFeed, id).value(QStringLiteral("view")).toMap();
        const QString language = current.value(QStringLiteral("language"), manga->chapterDefaultLanguage()).toString();
        const QString sourceKey = p.value(QStringLiteral("sourceKey")).toString();
        if (bridge.detailRow(kFeed, id, QStringLiteral("sources"), sourceKey).isEmpty())
            return unavailable(done, QStringLiteral("This chapter source is no longer available."));
        const QVariantMap choice = chapterSourceChoices.value(sourceKey);
        if (choice.value(QStringLiteral("seriesId")) != id
            || choice.value(QStringLiteral("language")) != language)
            return unavailable(done, QStringLiteral("This chapter source is no longer available."));
        const QString providerId = choice.value(QStringLiteral("providerId")).toString();
        bool known = false;
        for (const QVariant &value : manga->chapterProviders(language))
            if (value.toMap().value(QStringLiteral("id")).toString() == providerId
                && value.toMap().value(QStringLiteral("enabled")).toBool()) known = true;
        if (!known || !manga->moveChapterProvider(language, providerId, 0))
            return unavailable(done, QStringLiteral("This chapter source is unavailable."));
        bridge.updateDetail(kFeed, id,
            {{QStringLiteral("view"), QVariantMap{{QStringLiteral("mode"), QStringLiteral("chapters")},
                                                  {QStringLiteral("language"), language},
                                                  {QStringLiteral("sourceKey"), providerId}}},
             {QStringLiteral("sourceTarget"), QString()}});
        done({{QStringLiteral("ok"), true}});
    }});
const bool volumeSourcesRegistered = ActionRegistry::add({QStringLiteral("detail.manga.loadVolumeSources"),
    [](const QVariantMap &p) { return identity(p) && !p.value(QStringLiteral("unitId")).toString().isEmpty(); },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        if (!active(bridge, p, done)) return;
        const QString id = p.value(QStringLiteral("id")).toString();
        const QString unitId = p.value(QStringLiteral("unitId")).toString();
        if (bridge.detailRow(kFeed, id, QStringLiteral("volumes"), unitId).isEmpty())
            return unavailable(done, QStringLiteral("This volume is no longer available."));
        bool nyaaEnabled = false;
        if (auto *extensions = qobject_cast<ExtensionsStore *>(bridge.service(QStringLiteral("Extensions"))))
            for (const QVariant &value : extensions->installed()) {
                const QVariantMap extension = value.toMap();
                if (extension.value(QStringLiteral("id")) == QLatin1String("colosseum.well.nyaa"))
                    nyaaEnabled = extension.value(QStringLiteral("enabled")).toBool();
            }
        if (!nyaaEnabled) return unavailable(done, QStringLiteral("Enable the Nyaa source in Extensions first."));
        auto *volumes = qobject_cast<MangaTankobanService *>(bridge.service(QStringLiteral("TankobanVolumes")));
        if (!volumes) return unavailable(done, QStringLiteral("Volume sources are unavailable."));
        const QString sourceId = volumeKey(id, unitId);
        volumeSources.remove(sourceId);
        bridge.updateDetail(kFeed, id, {{QStringLiteral("sourceTarget"), unitId}});
        const auto connection = QSharedPointer<QMetaObject::Connection>::create();
        const auto finished = QSharedPointer<bool>::create(false);
        QPointer<ColosseumWebBridge> guarded(&bridge);
        *connection = QObject::connect(volumes, &MangaTankobanService::sourcesReady, &bridge,
            [sourceId, id, unitId, guarded, connection, finished, done](const QString &incoming,
                                                                const QVariantList &results) {
                if (incoming != unitId || *finished) return;
                *finished = true;
                QObject::disconnect(*connection);
                QVariantList publicRows;
                for (const QVariant &value : results) {
                    const QVariantMap candidate = value.toMap();
                    const QString hash = candidate.value(QStringLiteral("infoHash")).toString();
                    if (candidate.value(QStringLiteral("kind")) != QLatin1String("nyaa")
                        || hash.isEmpty() || !candidate.value(QStringLiteral("enabled")).toBool())
                        continue;
                    const QString key = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    publicRows.append(QVariantMap{{QStringLiteral("key"), key},
                        {QStringLiteral("label"), candidate.value(QStringLiteral("releaseTitle"))},
                        {QStringLiteral("language"), QString()},
                        {QStringLiteral("availability"), QStringLiteral("available")}});
                    volumeSourceChoices.insert(key, {{QStringLiteral("seriesId"), id},
                        {QStringLiteral("volumeId"), unitId}, {QStringLiteral("infoHash"), hash}});
                }
                volumeSources.insert(sourceId, publicRows);
                if (guarded) guarded->updateDetail(kFeed, id, {});
                done({{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
                    {QStringLiteral("count"), publicRows.size()}}}});
            });
        QTimer::singleShot(25000, &bridge, [connection, finished, done]() {
            if (*finished) return;
            *finished = true;
            QObject::disconnect(*connection);
            done({{QStringLiteral("ok"), false},
                  {QStringLiteral("error"), QStringLiteral("Volume source search timed out.")}});
        });
        volumes->searchSources(unitId);
    }});
const bool readRegistered = ActionRegistry::add({QStringLiteral("detail.manga.read"),
    [](const QVariantMap &p) { return identity(p) && !p.value(QStringLiteral("unitId")).toString().isEmpty(); },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        if (!active(bridge, p, done)) return;
        const QString id = p.value(QStringLiteral("id")).toString();
        const QString kind = p.value(QStringLiteral("unitKind")).toString();
        const QString unitId = p.value(QStringLiteral("unitId")).toString();
        if (kind != QLatin1String("volume") && kind != QLatin1String("chapter"))
            return unavailable(done, QStringLiteral("This reading unit is invalid."));
        const QVariantMap row = kind == QLatin1String("volume")
            ? bridge.detailRow(kFeed, id, QStringLiteral("volumes"), unitId)
            : chapterFor(bridge, id, unitId);
        if (row.isEmpty()) return unavailable(done, QStringLiteral("This reading unit is no longer available."));
        const QVariantMap params = bridge.detailParams(kFeed, id);
        if (kind == QLatin1String("chapter")) {
            auto *downloads = qobject_cast<MangaDownloader *>(bridge.service(QStringLiteral("Downloads")));
            if (!downloads) return unavailable(done, QStringLiteral("Chapter downloads are unavailable."));
            if (!downloads->isDownloaded(unitId)) {
                downloads->downloadChapter(unitId, id, params.value(QStringLiteral("title")).toString(),
                                           row.value(QStringLiteral("name"), row.value(QStringLiteral("title"))).toString());
                const QString state = downloads->statusOf(unitId).value(QStringLiteral("state")).toString();
                if (state != QLatin1String("queued") && state != QLatin1String("downloading")
                    && state != QLatin1String("done"))
                    return unavailable(done, QStringLiteral("This chapter could not be queued."));
                bridge.updateDetail(kFeed, id, {});
                done({{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
                    {QStringLiteral("state"), QStringLiteral("queued")}, {QStringLiteral("jobId"), unitId}}}});
                return;
            }
        } else {
            auto *volumes = qobject_cast<MangaTankobanService *>(bridge.service(QStringLiteral("TankobanVolumes")));
            if (!volumes || volumes->statusOf(unitId).value(QStringLiteral("state")) != QLatin1String("ready")
                || volumes->localPages(unitId).isEmpty())
                return unavailable(done, QStringLiteral("This volume needs an available download source."));
        }
        QString title = params.value(QStringLiteral("title")).toString();
        if (title.isEmpty() && id.startsWith(QLatin1String("mal:")))
            if (auto *mal = qobject_cast<MalCatalog *>(bridge.service(QStringLiteral("MalCatalog"))))
                title = mal->mangaById(id.mid(4).toInt()).value(QStringLiteral("title")).toString();
        bridge.delegateAction(QStringLiteral("detail.manga.openReader"),
            {{QStringLiteral("id"), id}, {QStringLiteral("title"), title},
             {QStringLiteral("unitKind"), kind}, {QStringLiteral("unitId"), unitId}}, done);
    }});
const bool markRegistered = ActionRegistry::add({QStringLiteral("detail.manga.markRead"),
    [](const QVariantMap &p) { return identity(p) && !p.value(QStringLiteral("unitId")).toString().isEmpty()
        && p.value(QStringLiteral("read")).metaType().id() == QMetaType::Bool; },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        if (!active(bridge, p, done)) return;
        unavailable(done, QStringLiteral("A reliable read mark is unavailable for this unit."));
    }});
const bool downloadRegistered = ActionRegistry::add({QStringLiteral("detail.manga.download"),
    [](const QVariantMap &p) { return identity(p) && !p.value(QStringLiteral("unitId")).toString().isEmpty(); },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        if (!active(bridge, p, done)) return;
        const QString id = p.value(QStringLiteral("id")).toString();
        const QString kind = p.value(QStringLiteral("unitKind")).toString();
        const QString unitId = p.value(QStringLiteral("unitId")).toString();
        if (kind == QLatin1String("volume")) {
            const QString sourceKey = p.value(QStringLiteral("sourceKey")).toString();
            const QVariantMap choice = volumeSourceChoices.value(sourceKey);
            if (choice.value(QStringLiteral("seriesId")) != id
                || choice.value(QStringLiteral("volumeId")) != unitId
                || bridge.detailRow(kFeed, id, QStringLiteral("sources"), sourceKey).isEmpty())
                return unavailable(done, QStringLiteral("Choose an available volume source first."));
            auto *volumes = qobject_cast<MangaTankobanService *>(bridge.service(QStringLiteral("TankobanVolumes")));
            if (!volumes) return unavailable(done, QStringLiteral("Volume downloads are unavailable."));
            volumes->downloadNyaa(unitId, choice.value(QStringLiteral("infoHash")).toString());
            const QString state = volumes->statusOf(unitId).value(QStringLiteral("state")).toString();
            if (state != QLatin1String("resolving") && state != QLatin1String("downloading")
                && state != QLatin1String("ingesting") && state != QLatin1String("packing")
                && state != QLatin1String("ready"))
                return unavailable(done, QStringLiteral("This volume could not be queued."));
            bridge.updateDetail(kFeed, id, {});
            done({{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
                {QStringLiteral("jobId"), unitId}}}});
            return;
        }
        if (kind != QLatin1String("chapter"))
            return unavailable(done, QStringLiteral("This download unit is invalid."));
        const QVariantMap row = chapterFor(bridge, id, unitId);
        if (row.isEmpty()) return unavailable(done, QStringLiteral("This chapter is no longer available."));
        auto *downloads = qobject_cast<MangaDownloader *>(bridge.service(QStringLiteral("Downloads")));
        if (!downloads) return unavailable(done, QStringLiteral("Chapter downloads are unavailable."));
        downloads->downloadChapter(unitId, id,
            bridge.detailParams(kFeed, id).value(QStringLiteral("title")).toString(),
            row.value(QStringLiteral("name"), row.value(QStringLiteral("title"))).toString());
        const QString state = downloads->statusOf(unitId).value(QStringLiteral("state")).toString();
        if (state == QLatin1String("none"))
            return unavailable(done, QStringLiteral("This chapter could not be queued."));
        bridge.updateDetail(kFeed, id, {});
        done({{QStringLiteral("ok"), true}, {QStringLiteral("result"), QVariantMap{
            {QStringLiteral("jobId"), unitId}}}});
    }});
const bool collectionRegistered = ActionRegistry::add({QStringLiteral("detail.manga.collection"),
    [](const QVariantMap &p) { return identity(p) && p.value(QStringLiteral("saved")).metaType().id() == QMetaType::Bool; },
    [](ColosseumWebBridge &bridge, const QVariantMap &p, ActionRegistry::Completion done) {
        if (!active(bridge, p, done)) return;
        auto *collection = qobject_cast<CollectionStore *>(bridge.service(QStringLiteral("Collection")));
        if (!collection || !collection->healthy()) return unavailable(done, QStringLiteral("Collection is unavailable."));
        const QString id = p.value(QStringLiteral("id")).toString();
        const bool ok = p.value(QStringLiteral("saved")).toBool()
            ? collection->add(QStringLiteral("tankoban"),
                {{QStringLiteral("id"), id}, {QStringLiteral("type"), QStringLiteral("manga")},
                 {QStringLiteral("title"), p.value(QStringLiteral("title")).toString()},
                 {QStringLiteral("cover"), p.value(QStringLiteral("cover")).toString()},
                 {QStringLiteral("payload"), QVariantMap{}}})
            : collection->remove(QStringLiteral("tankoban"), id);
        if (!ok) return unavailable(done, QStringLiteral("Collection could not be saved."));
        bridge.updateDetail(kFeed, id, {});
        done({{QStringLiteral("ok"), true}});
    }});
} // namespace
