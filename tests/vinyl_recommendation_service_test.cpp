#include <QtTest>
#include <algorithm>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <functional>

#include "vinyl/VinylRecommendationService.h"
#include "account/ActivityStore.h"
#include "account/HistoryStore.h"
#include <QTemporaryDir>

namespace {
constexpr qint64 kFixedNowMs = 1792670400000LL; // 2026-10-22T12:00:00Z

QVariantMap artistSeed(int index)
{
    const QString providerId = QStringLiteral("UC")
        + QString::number(index).rightJustified(22, QLatin1Char('0'));
    return {{QStringLiteral("kind"), QStringLiteral("artist")},
            {QStringLiteral("provider"), QStringLiteral("youtube-music")},
            {QStringLiteral("providerId"), providerId},
            {QStringLiteral("id"), QStringLiteral("ytm:artist:") + providerId},
            {QStringLiteral("name"), QStringLiteral("Fixture Artist %1").arg(index)}};
}

QVariantMap albumCandidate(const QVariantMap &seed, const QString &providerId,
                           const QString &title, const QVariantMap &extra = {})
{
    QVariantMap item{{QStringLiteral("kind"), QStringLiteral("album")},
                     {QStringLiteral("provider"), QStringLiteral("youtube-music")},
                     {QStringLiteral("providerId"), providerId},
                     {QStringLiteral("id"), QStringLiteral("ytm:album:") + providerId},
                     {QStringLiteral("title"), title},
                     {QStringLiteral("cover"), QStringLiteral("https://fixture.invalid/%1.jpg").arg(providerId)},
                     {QStringLiteral("relation"), QStringLiteral("own-release")},
                     {QStringLiteral("artistRefs"), QVariantList{seed}},
                     {QStringLiteral("seedArtistRef"), seed}};
    for (auto it = extra.cbegin(); it != extra.cend(); ++it)
        item.insert(it.key(), it.value());
    return item;
}

class FakePreferences final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList vinylTasteArtists MEMBER m_artists NOTIFY vinylTasteChanged)
    Q_PROPERTY(bool vinylTasteAnswered MEMBER m_answered NOTIFY vinylTasteChanged)
    Q_PROPERTY(QString vinylFeedbackSnapshot MEMBER feedback NOTIFY vinylFeedbackChanged)
    Q_PROPERTY(QString vinylRecommendationExposureSnapshot MEMBER exposure NOTIFY vinylRecommendationExposureChanged)
public:
    int tasteWrites = 0;
    QString feedback,exposure;
    Q_INVOKABLE bool setVinylFeedbackSnapshot(const QString &value) {feedback=value;emit vinylFeedbackChanged();return true;}
    Q_INVOKABLE bool setVinylRecommendationExposureSnapshot(const QString &value) {exposure=value;emit vinylRecommendationExposureChanged();return true;}

    void setTaste(const QVariantList &artists, bool answered = true)
    {
        m_artists = artists;
        m_answered = answered;
        emit vinylTasteChanged();
    }

    Q_INVOKABLE bool setVinylTasteSelections(bool answered, QVariantList artists)
    {
        ++tasteWrites;
        setTaste(artists, answered);
        return true;
    }

signals:
    void vinylTasteChanged();
    void vinylFeedbackChanged();
    void vinylRecommendationExposureChanged();

private:
    QVariantList m_artists;
    bool m_answered = false;
};

class FakeSource final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList queueSentinel MEMBER queueSentinel)
public:
    struct Request {
        QString lane;
        QString operation;
        QString value;
        QVariantMap options;
        int id = -1;
    };

    QVariantList queueSentinel;
    int queueMutationCalls = 0;
    int maxConcurrent = 0;
    QList<QString> cancelledLanes;
    QList<Request> requests;
    QSet<QString> freshCachedArtistIds;
    mutable QList<QString> cachedQueries;

    Q_INVOKABLE int request(const QString &lane, const QString &operation,
                            const QString &value, const QVariantMap &options)
    {
        Request call{lane, operation, value, options, ++m_nextId};
        requests.append(call);
        m_pending.insert(call.id);
        maxConcurrent = qMax(maxConcurrent, m_pending.size());
        return call.id;
    }

    Q_INVOKABLE void cancel(const QString &lane)
    {
        cancelledLanes.append(lane);
        for (const Request &call : requests) {
            if (call.lane == lane)
                m_pending.remove(call.id);
        }
    }

    Q_INVOKABLE QVariantMap cachedRecommendation(const QString &operation, const QString &value,
                                                  const QVariantMap &options) const
    {
        if (operation != QStringLiteral("artistCandidates"))
            return {};
        cachedQueries.append(value);
        if (!freshCachedArtistIds.contains(value))
            return {};
        return replyForSeed(options.value(QStringLiteral("seed")).toMap());
    }

    // These deliberately observable controls let the refresh fixture prove that
    // recommendation discovery never uses playback queue commands.
    Q_INVOKABLE bool replaceQueue(const QVariantList &)
    {
        ++queueMutationCalls;
        return true;
    }
    Q_INVOKABLE void enqueue(const QVariantMap &) { ++queueMutationCalls; }
    Q_INVOKABLE void playNext(const QVariantMap &) { ++queueMutationCalls; }

    QList<Request> pendingRequests(const QString &operation = {}) const
    {
        QList<Request> result;
        for (const Request &call : requests) {
            if (m_pending.contains(call.id)
                && (operation.isEmpty() || call.operation == operation))
                result.append(call);
        }
        return result;
    }

    QList<Request> callsFor(const QString &operation) const
    {
        QList<Request> result;
        for (const Request &call : requests) {
            if (call.operation == operation)
                result.append(call);
        }
        return result;
    }

    Request requestById(int id) const
    {
        for (const Request &call : requests) {
            if (call.id == id)
                return call;
        }
        return {};
    }

    QVariantMap defaultReply(const Request &call, qint64 nowMs = kFixedNowMs) const
    {
        return replyForSeed(call.options.value(QStringLiteral("seed")).toMap(), nowMs);
    }

    void cacheArtistSeed(const QVariantMap &seed)
    {
        freshCachedArtistIds.insert(seed.value(QStringLiteral("providerId")).toString());
    }

    QVariantMap replyForSeed(const QVariantMap &seed, qint64 nowMs = kFixedNowMs) const
    {
        const QString seedId = seed.value(QStringLiteral("providerId")).toString();
        const QString suffix = seedId.right(8);
        const QString albumId = QStringLiteral("MPRE_") + suffix;
        return {{QStringLiteral("version"), 1},
                {QStringLiteral("seed"), seed},
                {QStringLiteral("items"), QVariantList{
                    albumCandidate(seed, albumId,
                                   QStringLiteral("Release for %1").arg(seed.value(QStringLiteral("name")).toString()))}},
                {QStringLiteral("provenance"), QVariantMap{{QStringLiteral("source"), QStringLiteral("youtube-music")}}},
                {QStringLiteral("fetchedAt"), QDateTime::fromMSecsSinceEpoch(nowMs, Qt::UTC)
                     .toString(Qt::ISODateWithMs)},
                {QStringLiteral("complete"), false},
                {QStringLiteral("status"), QStringLiteral("ready")}};
    }

    void complete(const Request &call, const QVariant &payload, const QString &error = {})
    {
        m_pending.remove(call.id);
        emit completed(call.lane, call.id, payload, error);
    }

    // Simulate a transport that delivers an already-cancelled request late.
    void completeLate(const Request &call, const QVariant &payload, const QString &error = {})
    {
        m_pending.remove(call.id);
        emit completed(call.lane, call.id, payload, error);
    }

signals:
    void completed(const QString &lane, int requestId, const QVariant &data,
                   const QString &error);

private:
    int m_nextId = 0;
    QSet<int> m_pending;
};

struct HomeFixture {
    FakeSource source;
    FakePreferences preferences;
    VinylRecommendationService service;

    explicit HomeFixture(const QVariantList &seeds, qint64 nowMs = kFixedNowMs,
                         bool beginVisit = true, bool answered = true)
        : service(nullptr, [nowMs] { return nowMs; })
    {
        service.setSource(&source);
        service.setPreferences(&preferences);
        preferences.setTaste(seeds, answered);
        if (beginVisit)
            service.beginHomeVisit();
    }
};

QVariantMap sectionById(const QVariantList &sections, const QString &id)
{
    for (const QVariant &value : sections) {
        const QVariantMap section = value.toMap();
        if (section.value(QStringLiteral("id")).toString() == id
            || section.value(QStringLiteral("key")).toString() == id)
            return section;
    }
    return {};
}

QVariantList sectionItems(const VinylRecommendationService &service, const QString &id)
{
    return sectionById(service.homeSections(), id).value(QStringLiteral("items")).toList();
}

QString providerItemId(const QVariantMap &card)
{
    return card.value(QStringLiteral("destination")).toMap()
        .value(QStringLiteral("providerId")).toString();
}

bool sectionHasProviderId(const VinylRecommendationService &service, const QString &sectionId,
                          const QString &providerId)
{
    for (const QVariant &value : sectionItems(service, sectionId)) {
        if (providerItemId(value.toMap()) == providerId)
            return true;
    }
    return false;
}

QVariantMap savedArtistRecord(const QVariantMap &seed)
{
    QVariantMap row = seed;
    row.insert(QStringLiteral("artistId"), seed.value(QStringLiteral("id")));
    return row;
}

void verifyUiCard(const QVariantMap &card)
{
    QVERIFY(card.contains(QStringLiteral("typedKey")) || card.contains(QStringLiteral("typedkey")));
    QVERIFY(card.contains(QStringLiteral("destination")));
    QVERIFY(card.contains(QStringLiteral("reasons")));
    QVERIFY(card.contains(QStringLiteral("score")));
    QVERIFY(card.contains(QStringLiteral("evidence")));
}

void pumpSuccessfulReplies(FakeSource &source, int maxPasses = 32)
{
    for (int pass = 0; pass < maxPasses; ++pass) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        const QList<FakeSource::Request> pending = source.pendingRequests(QStringLiteral("artistCandidates"));
        if (pending.isEmpty()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            if (source.pendingRequests(QStringLiteral("artistCandidates")).isEmpty())
                break;
            continue;
        }
        for (const FakeSource::Request &call : pending)
            source.complete(call, source.defaultReply(call));
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

QStringList stableSectionSnapshot(const VinylRecommendationService &service)
{
    QStringList result;
    for (const QString &id : {QStringLiteral("from-your-artists"),
                              QStringLiteral("related-discoveries"),
                              QStringLiteral("new-from-your-artists")}) {
        result.append(id);
        for (const QVariant &value : sectionItems(service, id)) {
            const QVariantMap card = value.toMap();
            const QVariantMap destination = card.value(QStringLiteral("destination")).toMap();
            result.append(destination.value(QStringLiteral("provider")).toString() + QLatin1Char(':')
                          + destination.value(QStringLiteral("providerId")).toString());
        }
    }
    return result;
}

QStringList runTwoSeedOrder(bool reverseCompletion)
{
    HomeFixture fixture({artistSeed(1), artistSeed(2)});
    auto waitFor = [](const std::function<bool()> &condition) {
        for (int i = 0; i < 100; ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            if (condition())
                return true;
        }
        return condition();
    };
    if (!waitFor([&] { return fixture.source.callsFor(QStringLiteral("artistCandidates")).size() == 2; }))
        return {};
    QList<FakeSource::Request> pending = fixture.source.pendingRequests(QStringLiteral("artistCandidates"));
    if (pending.size() != 2)
        return {};
    if (reverseCompletion)
        std::reverse(pending.begin(), pending.end());
    for (const FakeSource::Request &call : pending)
        fixture.source.complete(call, fixture.source.defaultReply(call));
    if (!waitFor([&] { return fixture.source.pendingRequests(QStringLiteral("artistCandidates")).isEmpty(); }))
        return {};
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return stableSectionSnapshot(fixture.service);
}
} // namespace

class VinylRecommendationServiceTest final : public QObject {
    Q_OBJECT
private slots:
    void exposureRestoreValidatesProfileWindowAndWholeEnvelope() {
        HomeFixture fixture({artistSeed(1)});pumpSuccessfulReplies(fixture.source);
        ActivityStore activity;fixture.service.setActivityStore(&activity);
        const auto ref=sectionItems(fixture.service,"from-your-artists").first().toMap().value("ref");
        QVERIFY(fixture.service.recordVisibleItems("from-your-artists",fixture.service.homeSnapshot().value("exposureSnapshotId").toString(),{ref},0));
        auto envelope=QJsonDocument::fromJson(fixture.preferences.exposure.toUtf8()).object();
        const auto record=envelope.value("records").toArray().first().toObject();
        auto restore=[&](const QJsonObject &value) {
            fixture.preferences.setVinylRecommendationExposureSnapshot(QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact)));
            return fixture.service.homeSnapshot().value("exposureCount").toInt();
        };
        auto invalid=envelope;invalid["profileId"]="other-profile";QCOMPARE(restore(invalid),0);
        invalid=envelope;auto rows=envelope.value("records").toArray();rows.append(QJsonObject{{"invalid",true}});
        invalid["records"]=rows;QCOMPARE(restore(invalid),0); // No partial-prefix acceptance.
        auto old=record;old["atMs"]=kFixedNowMs-14LL*86400000-1;
        auto future=record;future["atMs"]=kFixedNowMs+1;future["exposureId"]="future-exposure";
        invalid=envelope;invalid["records"]=QJsonArray{old,future};QCOMPARE(restore(invalid),0);
        rows=QJsonArray();for(int i=0;i<2001;++i){auto item=record;item["exposureId"]="bounded-"+QString::number(i);rows.append(item);}
        invalid=envelope;invalid["records"]=rows;QCOMPARE(restore(invalid),0);
        rows.removeLast();envelope["records"]=rows;QCOMPARE(restore(envelope),2000);
        fixture.service.beginHomeVisit();pumpSuccessfulReplies(fixture.source);
        QVERIFY(fixture.service.recordVisibleItems("from-your-artists",fixture.service.homeSnapshot().value("exposureSnapshotId").toString(),{ref},0));
        QCOMPARE(QJsonDocument::fromJson(fixture.preferences.exposure.toUtf8()).object().value("records").toArray().size(),2000);
    }
    void collaboratorExclusionAppliesToSavedLibraryAlbums() {
        const auto a=artistSeed(1),b=artistSeed(2);
        HomeFixture fixture({a});pumpSuccessfulReplies(fixture.source);
        QVariantMap album{{"id","saved-collaboration"},{"title","Collaboration"},{"origin","youtube-music"},
            {"browseId","MPRE_collaboration"},{"providerIds",QVariantMap{{"youtube-music","MPRE_collaboration"}}},
            {"artistIds",QStringList{"ytm:artist:"+a.value("providerId").toString(),"ytm:artist:"+b.value("providerId").toString()}},
            {"artists",QStringList{"A","B"}},{"tracks",QVariantList()}};
        fixture.service.setHomeCollections({{"albums",QVariantList{QVariantMap{{"album",album}}}}});
        QVERIFY(sectionHasProviderId(fixture.service,"from-your-artists","MPRE_collaboration"));
        const QVariantMap artist{{"identity","provider"},{"kind","artist"},{"providerId","youtube-music"},{"itemId",b.value("providerId")}};
        QVERIFY(fixture.service.setFeedback(artist,"exclude-artist",0));
        QVERIFY(!sectionHasProviderId(fixture.service,"from-your-artists","MPRE_collaboration"));
    }
    void postResetExposureSurvivesServiceRestart() {
        ActivityStore activity;QVERIFY(activity.clearAll());QTemporaryDir directory;
        HistoryStore history(directory.filePath("history.ini"));QVERIFY(history.clearAll());
        const auto future=QDateTime::currentMSecsSinceEpoch()+100000;
        HomeFixture fixture({artistSeed(1)},future);pumpSuccessfulReplies(fixture.source);
        fixture.service.setActivityStore(&activity);fixture.service.setHistoryStore(&history);
        const auto card=sectionItems(fixture.service,"from-your-artists").first().toMap();
        QVERIFY(fixture.service.recordVisibleItems("from-your-artists",fixture.service.homeSnapshot().value("exposureSnapshotId").toString(),{card.value("ref")},0));
        const auto stored=fixture.preferences.exposure;QVERIFY(!stored.isEmpty());
        VinylRecommendationService restored(nullptr,[future]{return future;});
        restored.setPreferences(&fixture.preferences);restored.setSource(&fixture.source);
        restored.setActivityStore(&activity);QCOMPARE(fixture.preferences.exposure,stored);
        restored.setHistoryStore(&history);QCOMPARE(fixture.preferences.exposure,stored);
        restored.beginHomeVisit();pumpSuccessfulReplies(fixture.source);
        QCOMPARE(restored.homeSnapshot().value("exposureCount").toInt(),1);
    }
    void qualifiedHistoryChangesRankAndClearRetainsExplicitTaste() {
        HomeFixture fixture({artistSeed(1)});pumpSuccessfulReplies(fixture.source);
        ActivityStore activity;fixture.service.setActivityStore(&activity);
        const auto card=sectionItems(fixture.service,"from-your-artists").first().toMap();
        auto fact=QVariantMap{{"sessionId","qualified"},{"world","vinyl"},{"kind","music"},{"titleKey","album"},
            {"itemKey","track"},{"title","Track"},{"utcOffsetMinutes",0},{"syncable",false}};
        auto start=fact;start.insert("atMs",kFixedNowMs-30000);start.insert("action","start");start.insert("origin","manual");
        start.insert("positionMs",0);start.insert("durationMs",120000);
        start.insert("entityRef",QVariantMap{{"identity","provider"},{"kind","recording"},{"providerId","youtube-music"},{"itemId","Abc123def45"}});
        start.insert("releaseRef",card.value("ref"));QVERIFY(activity.recordMusicAction(start));
        fact.insert("startAtMs",kFixedNowMs-20000);fact.insert("endAtMs",kFixedNowMs-8000);fact.insert("activeMs",12000);fact.insert("rateMilli",1000);
        QVERIFY(activity.recordPlaybackDelta(fact));
        QCOMPARE(fixture.service.homeSnapshot().value("qualifiedManualSessions").toInt(),1);
        QCOMPARE(sectionItems(fixture.service,"from-your-artists").first().toMap().value("score").toLongLong(),card.value("score").toLongLong()+45);
        QVERIFY(fixture.service.setFeedback(card.value("ref").toMap(),"like",0));
        QVERIFY(activity.clearAll());QCOMPARE(fixture.service.homeSnapshot().value("qualifiedManualSessions").toInt(),0);
        QCOMPARE(sectionItems(fixture.service,"from-your-artists").first().toMap().value("score").toLongLong(),card.value("score").toLongLong()+1000);
        QVERIFY(!fixture.preferences.feedback.isEmpty());
    }
    void retainedManualSessionsDriveOnlySpecifiedSeedDecayBoundaries() {
        HomeFixture fixture({artistSeed(1)});pumpSuccessfulReplies(fixture.source);ActivityStore activity;
        const auto release=sectionItems(fixture.service,"from-your-artists").first().toMap().value("ref");
        auto record=[&](int index) {
            QVariantMap common{{"sessionId","manual-"+QString::number(index)},{"world","vinyl"},{"kind","music"},
                {"titleKey","album"},{"itemKey","track"},{"title","Track"},{"utcOffsetMinutes",0},{"syncable",false}};
            auto start=common;start.insert("atMs",kFixedNowMs-30000);start.insert("action","start");start.insert("origin","manual");
            start.insert("positionMs",0);start.insert("durationMs",120000);
            start.insert("entityRef",QVariantMap{{"identity","provider"},{"kind","recording"},{"providerId","local"},{"itemId","track"}});
            start.insert("releaseRef",release);QVERIFY(activity.recordMusicAction(start));
            auto delta=common;delta.insert("startAtMs",kFixedNowMs-20000);delta.insert("endAtMs",kFixedNowMs-8000);delta.insert("activeMs",12000);delta.insert("rateMilli",1000);
            QVERIFY(activity.recordPlaybackDelta(delta));
        };
        for(int i=0;i<30;++i)record(i);
        fixture.service.setActivityStore(&activity);QCOMPARE(fixture.service.homeSnapshot().value("seedMultiplier").toDouble(),1.0);
        fixture.service.setActivityStore(nullptr);record(30);fixture.service.setActivityStore(&activity);
        QCOMPARE(fixture.service.homeSnapshot().value("seedMultiplier").toDouble(),0.75);
        fixture.service.setActivityStore(nullptr);for(int i=31;i<100;++i)record(i);fixture.service.setActivityStore(&activity);
        QCOMPARE(fixture.service.homeSnapshot().value("seedMultiplier").toDouble(),0.75);
        fixture.service.setActivityStore(nullptr);record(100);fixture.service.setActivityStore(&activity);
        QCOMPARE(fixture.service.homeSnapshot().value("qualifiedManualSessions").toInt(),101);
        QCOMPARE(fixture.service.homeSnapshot().value("seedMultiplier").toDouble(),0.5);
        fixture.service.setHomeCollections({{"artists",QVariantList{savedArtistRecord(artistSeed(1))}}});
        pumpSuccessfulReplies(fixture.source);
        const auto reasons=sectionItems(fixture.service,"from-your-artists").first().toMap().value("reasons").toList();
        bool fullSavedAffinity=false;for(const auto &value:reasons)if(value.toMap().value("kind")=="seed_affinity")
            fullSavedAffinity=value.toMap().value("scoreDelta").toInt()==300;
        QVERIFY(fullSavedAffinity);
        fixture.service.setKeepHistory(false);QCOMPARE(fixture.service.homeSnapshot().value("seedMultiplier").toDouble(),1.0);
        QCOMPARE(fixture.service.homeSnapshot().value("qualifiedManualSessions").toInt(),0);
    }
    void feedbackChangesRankingAndSuppressionWithoutStaleProfileWrites() {
        HomeFixture fixture({artistSeed(1)});pumpSuccessfulReplies(fixture.source);
        auto items=sectionItems(fixture.service,"from-your-artists");QVERIFY(!items.isEmpty());
        const auto card=items.first().toMap(),ref=card.value("ref").toMap();
        auto mutate=[&](const QVariantMap &entity,const QString &action,int generation) {
            bool result=false;
            QMetaObject::invokeMethod(&fixture.service,"setFeedback",Qt::DirectConnection,Q_RETURN_ARG(bool,result),
                Q_ARG(QVariantMap,entity),Q_ARG(QString,action),Q_ARG(int,generation));return result;
        };
        QVERIFY(mutate(ref,"like",0));QVERIFY(!fixture.preferences.feedback.isEmpty());
        QCOMPARE(sectionItems(fixture.service,"from-your-artists").first().toMap().value("score").toLongLong(),card.value("score").toLongLong()+1000);
        QVERIFY(mutate(ref,"hide",0));QVERIFY(sectionItems(fixture.service,"from-your-artists").isEmpty());
        QVERIFY(mutate(ref,"unhide",0));QVERIFY(!sectionItems(fixture.service,"from-your-artists").isEmpty());
        const auto snapshot=fixture.preferences.feedback;
        QVERIFY(!mutate(ref,"dislike",42));QCOMPARE(fixture.preferences.feedback,snapshot);
        const auto seed=artistSeed(1);
        const QVariantMap artist{{"identity","provider"},{"kind","artist"},{"providerId","youtube-music"},{"itemId",seed.value("providerId")}};
        QVERIFY(mutate(artist,"exclude-artist",0));QVERIFY(sectionItems(fixture.service,"from-your-artists").isEmpty());
        QVERIFY(mutate(artist,"allow-artist",0));QVERIFY(!sectionItems(fixture.service,"from-your-artists").isEmpty());
    }
    void exposureRequiresVisibleCallAndDeduplicatesVisitWithoutRankingLoop() {
        HomeFixture fixture({artistSeed(1)});pumpSuccessfulReplies(fixture.source);
        ActivityStore activity;fixture.service.setActivityStore(&activity);
        QVERIFY(fixture.preferences.exposure.isEmpty()); // Constructing rows is not exposure.
        const auto row=sectionById(fixture.service.homeSections(),"from-your-artists");
        const auto ref=row.value("items").toList().first().toMap().value("ref").toMap();
        const auto token=fixture.service.homeSnapshot().value("exposureSnapshotId").toString();QVERIFY(!token.isEmpty());
        auto visible=[&](QString shelf,QString snapshot,int generation) {
            bool result=false;QMetaObject::invokeMethod(&fixture.service,"recordVisibleItems",Qt::DirectConnection,Q_RETURN_ARG(bool,result),
                Q_ARG(QString,shelf),Q_ARG(QString,snapshot),Q_ARG(QVariantList,QVariantList{ref}),Q_ARG(int,generation));return result;
        };
        QVERIFY(!visible("unknown",token,0));QVERIFY(fixture.preferences.exposure.isEmpty());
        QVERIFY(visible("from-your-artists",token,0));const auto retained=fixture.preferences.exposure;
        QVERIFY(!retained.isEmpty());QVERIFY(visible("from-your-artists",token,0));QCOMPARE(fixture.preferences.exposure,retained);
        QVERIFY(!visible("from-your-artists",token,42));QCOMPARE(fixture.preferences.exposure,retained);
        fixture.service.setKeepHistory(false);QVERIFY(fixture.preferences.exposure.isEmpty());
        QVERIFY(!visible("from-your-artists",token,0));QVERIFY(fixture.preferences.exposure.isEmpty());
    }
    void optionalIdentityFailureDoesNotMarkFreshCatalogueStale()
    {
        HomeFixture fixture({artistSeed(74)});pumpSuccessfulReplies(fixture.source);
        for(const auto &call:fixture.source.pendingRequests())fixture.source.complete(call,{},"Optional identity unavailable");
        QTRY_COMPARE(fixture.service.homeStatus(),QString("ready"));QVERIFY(fixture.service.homeError().isEmpty());
        QCOMPARE(sectionById(fixture.service.homeSections(),"from-your-artists").value("status").toString(),QString("ready"));
    }
    void laterHomeVisitReevaluatesExpiryAndNewCutoff()
    {
        qint64 now=kFixedNowMs;FakeSource source;FakePreferences prefs;
        VinylRecommendationService service(nullptr,[&now]{return now;});service.setSource(&source);service.setPreferences(&prefs);
        prefs.setTaste({artistSeed(70)});service.beginHomeVisit();
        const auto call=source.pendingRequests("artistCandidates").first();auto data=source.defaultReply(call);
        const auto seed=call.options.value("seed").toMap();const auto date=QDateTime::fromMSecsSinceEpoch(now,Qt::UTC).date().addDays(-89);
        data["items"]=QVariantList{albumCandidate(seed,"MPRE_DATED","Dated",{{"releaseDate",date.toString(Qt::ISODate)}})};
        source.complete(call,data);QTRY_VERIFY(!sectionItems(service,"new-from-your-artists").isEmpty());
        for(const auto &optional:source.pendingRequests())source.complete(optional,{},"Optional fixture offline");
        QCoreApplication::processEvents();now+=2LL*86400000;service.beginHomeVisit();
        QCOMPARE(service.homeSnapshot().value("evaluatedAtMs").toLongLong(),now);
        QVERIFY(sectionItems(service,"new-from-your-artists").isEmpty());
        QCOMPARE(source.callsFor("artistCandidates").size(),2);
    }
    void sourceReplacementRebuildsSeedsAndFencesOldReplies()
    {
        HomeFixture fixture({artistSeed(71)});const auto old=fixture.source.pendingRequests("artistCandidates").first();
        pumpSuccessfulReplies(fixture.source);QVERIFY(!sectionItems(fixture.service,"from-your-artists").isEmpty());
        FakeSource replacement;fixture.service.setSource(&replacement);fixture.service.beginHomeVisit();
        QTRY_VERIFY(!replacement.pendingRequests("artistCandidates").isEmpty());
        const auto call=replacement.pendingRequests("artistCandidates").first();auto data=replacement.defaultReply(call);
        data["items"]=QVariantList{albumCandidate(call.options.value("seed").toMap(),"MPRE_NEW_SOURCE","New Source")};
        replacement.complete(call,data);QTRY_VERIFY(sectionHasProviderId(fixture.service,"from-your-artists","MPRE_NEW_SOURCE"));
        auto late=fixture.source.defaultReply(old);late["items"]=QVariantList{albumCandidate(old.options.value("seed").toMap(),"MPRE_OLD_SOURCE","Old")};
        fixture.source.completeLate(old,late);QCoreApplication::processEvents();
        QVERIFY(!sectionHasProviderId(fixture.service,"from-your-artists","MPRE_OLD_SOURCE"));QCOMPARE(fixture.service.selectedArtists().size(),1);
    }
    void verifiedSeedAliasesAwardOneAffinity()
    {
        HomeFixture fixture({artistSeed(72),artistSeed(73)});
        for(const auto &call:fixture.source.pendingRequests("artistCandidates")){auto data=fixture.source.defaultReply(call);
            data["items"]=QVariantList{albumCandidate(call.options.value("seed").toMap(),"MPRE_SHARED","Shared")};fixture.source.complete(call,data);}
        QTRY_VERIFY(!fixture.source.pendingRequests("recommendationIdentity").isEmpty());
        const QString mbid="11111111-1111-4111-8111-111111111111";
        for(int step=0;step<8;++step){QCoreApplication::processEvents();const auto pending=fixture.source.pendingRequests();
            for(const auto &call:pending){if(call.operation=="recommendationIdentity"){
                const auto seed=call.options.value("seed").toMap();
                QVariantMap mapping{{"status","verified"},{"provider","youtube-music"},{"providerId",seed.value("providerId")},{"kind","artist"},{"mbid",mbid},{"method","external-id"},{"fetchedAtMs",kFixedNowMs}};
                fixture.source.complete(call,QVariantMap{{"version",1},{"seed",seed},{"items",QVariantList()},{"complete",true},{"status","verified"},{"mappingStatus","verified"},{"mbid",mbid},{"mapping",mapping},{"provenance",QVariantMap{{"source","musicbrainz"}}},{"fetchedAt",QDateTime::fromMSecsSinceEpoch(kFixedNowMs,Qt::UTC).toString(Qt::ISODateWithMs)}});
            }else fixture.source.complete(call,{},"Population fixture unavailable");}}
        QCoreApplication::processEvents();const auto items=sectionItems(fixture.service,"from-your-artists");QCOMPARE(items.size(),1);
        const auto card=items.first().toMap();QCOMPARE(card.value("score").toLongLong(),qint64(320));
        int affinities=0;for(const auto &reason:card.value("reasons").toList())if(reason.toMap().value("kind")=="seed_affinity")++affinities;
        QCOMPARE(affinities,1);QCOMPARE(fixture.service.selectedArtists().size(),2);
        QCOMPARE(card.value("destination").toMap().value("id").toString(),QString("ytm:album:MPRE_SHARED"));
    }
    void pickerCommitAutomaticallyDispatchesCandidates()
    {
        HomeFixture fixture({}, kFixedNowMs, false, false);
        const QVariantMap picked = artistSeed(41);
        fixture.service.beginHomeVisit();
        QCOMPARE(fixture.service.onboardingState(), QStringLiteral("draft"));
        fixture.service.toggleDraftArtist(picked);
        QVERIFY(fixture.service.commitTaste(false));
        QCOMPARE(fixture.preferences.tasteWrites, 1);

        QTRY_VERIFY(!fixture.source.callsFor(QStringLiteral("artistCandidates")).isEmpty());
        const QList<FakeSource::Request> calls = fixture.source.callsFor(QStringLiteral("artistCandidates"));
        QCOMPARE(calls.size(), 1);
        QCOMPARE(calls.first().options.value(QStringLiteral("seed")).toMap()
                     .value(QStringLiteral("providerId")).toString(),
                 picked.value(QStringLiteral("providerId")).toString());
    }

    void freshCachedCandidatesAppearBeforeVisitWithoutRequests()
    {
        QVariantList seeds;
        for (int i = 60; i < 68; ++i) {
            const QVariantMap seed = artistSeed(i);
            seeds.append(seed);
        }
        HomeFixture fixture(seeds, kFixedNowMs, false);
        for (const QVariant &value : seeds)
            fixture.source.cacheArtistSeed(value.toMap());

        fixture.service.beginHomeVisit();

        QCOMPARE(fixture.source.requests.size(), 0);
        QCOMPARE(fixture.source.cachedQueries.size(), seeds.size());
        const QVariantList rows = sectionItems(fixture.service, QStringLiteral("from-your-artists"));
        QVERIFY(rows.size() > 6);
        for (const QVariant &value : seeds) {
            const QString expected = QStringLiteral("MPRE_")
                + value.toMap().value(QStringLiteral("providerId")).toString().right(8);
            QVERIFY(sectionHasProviderId(fixture.service, QStringLiteral("from-your-artists"), expected));
        }
    }

    void profileGenerationClearsHomeAndOldCollectionProjection()
    {
        HomeFixture fixture({}, kFixedNowMs, false);
        const QVariantMap saved = artistSeed(50);
        fixture.source.cacheArtistSeed(saved);
        fixture.service.setHomeCollections({{QStringLiteral("artists"),
                                             QVariantList{savedArtistRecord(saved)}}});
        fixture.service.beginHomeVisit();
        QVERIFY(sectionHasProviderId(fixture.service, QStringLiteral("from-your-artists"),
                                     QStringLiteral("MPRE_00000050")));
        QVERIFY(!fixture.service.homeSnapshot().isEmpty());

        fixture.service.setProfileGeneration(fixture.service.profileGeneration() + 1);

        QVERIFY(fixture.service.homeSections().isEmpty());
        QVERIFY(fixture.service.homeCollections().isEmpty());
        QVERIFY(fixture.service.homeSnapshot().isEmpty());
        QVERIFY(fixture.service.selectedArtists().isEmpty());
        fixture.service.beginHomeVisit();
        QVERIFY(fixture.service.homeSections().isEmpty());
        QVERIFY(fixture.source.requests.isEmpty());
    }

    void savedCollectionArtistRemainsSeedAfterTasteRemoval()
    {
        const QVariantMap seed = artistSeed(51);
        HomeFixture fixture({seed}, kFixedNowMs, false);
        fixture.source.cacheArtistSeed(seed);
        fixture.service.setHomeCollections({{QStringLiteral("artists"),
                                             QVariantList{savedArtistRecord(seed)}}});
        fixture.service.beginHomeVisit();
        QVERIFY(sectionHasProviderId(fixture.service, QStringLiteral("from-your-artists"),
                                     QStringLiteral("MPRE_00000051")));

        fixture.preferences.setTaste({}, true);

        QTRY_VERIFY(sectionHasProviderId(fixture.service, QStringLiteral("from-your-artists"),
                                         QStringLiteral("MPRE_00000051")));
        QVERIFY(fixture.service.selectedArtists().isEmpty());
        QCOMPARE(fixture.source.requests.size(), 0);
    }

    void disjointSeedsYieldDifferentRelevantPools()
    {
        HomeFixture first({artistSeed(1)});
        QTRY_VERIFY(!first.source.pendingRequests(QStringLiteral("artistCandidates")).isEmpty());
        pumpSuccessfulReplies(first.source);
        const QVariantList firstRows = sectionItems(first.service, QStringLiteral("from-your-artists"));
        QVERIFY(!firstRows.isEmpty());
        const QString firstId = providerItemId(firstRows.first().toMap());

        HomeFixture second({artistSeed(2)});
        QTRY_VERIFY(!second.source.pendingRequests(QStringLiteral("artistCandidates")).isEmpty());
        pumpSuccessfulReplies(second.source);
        const QVariantList secondRows = sectionItems(second.service, QStringLiteral("from-your-artists"));
        QVERIFY(!secondRows.isEmpty());
        const QString secondId = providerItemId(secondRows.first().toMap());

        QVERIFY(!firstId.isEmpty());
        QVERIFY(!secondId.isEmpty());
        QVERIFY(firstId != secondId);
        QVERIFY(firstRows.first().toMap().value(QStringLiteral("evidence")).isValid());
        QVERIFY(secondRows.first().toMap().value(QStringLiteral("evidence")).isValid());
    }

    void allUnmappedSeedsProduceProviderNative()
    {
        HomeFixture fixture({artistSeed(3), artistSeed(4)});
        QTRY_VERIFY(!fixture.source.pendingRequests(QStringLiteral("artistCandidates")).isEmpty());
        pumpSuccessfulReplies(fixture.source);

        int cards = 0;
        for (const QString &sectionId : {QStringLiteral("from-your-artists"),
                                          QStringLiteral("related-discoveries"),
                                          QStringLiteral("new-from-your-artists")}) {
            for (const QVariant &value : sectionItems(fixture.service, sectionId)) {
                const QVariantMap card = value.toMap();
                verifyUiCard(card);
                const QVariantMap destination = card.value(QStringLiteral("destination")).toMap();
                QCOMPARE(destination.value(QStringLiteral("provider")).toString(),
                         QStringLiteral("youtube-music"));
                QVERIFY(destination.value(QStringLiteral("providerId")).isValid());
                QVERIFY(destination.value(QStringLiteral("id")).toString().startsWith(QStringLiteral("ytm:")));
                const QVariantMap typedKey = card.value(QStringLiteral("typedKey"),
                                                        card.value(QStringLiteral("typedkey"))).toMap();
                if (typedKey.contains(QStringLiteral("identity")))
                    QCOMPARE(typedKey.value(QStringLiteral("identity")).toString(), QStringLiteral("provider"));
                ++cards;
            }
        }
        QVERIFY(cards >= 2);
        QVERIFY(fixture.source.callsFor(QStringLiteral("populationCandidates")).isEmpty());
    }

    void oneFailedLaneKeepsOtherSections()
    {
        HomeFixture fixture({artistSeed(5), artistSeed(6)});
        QTRY_COMPARE(fixture.source.callsFor(QStringLiteral("artistCandidates")).size(), 2);
        const QList<FakeSource::Request> pending = fixture.source.pendingRequests(QStringLiteral("artistCandidates"));
        QCOMPARE(pending.size(), 2);
        const QString failedSeed = artistSeed(5).value(QStringLiteral("providerId")).toString();
        for (const FakeSource::Request &call : pending) {
            const QVariantMap seed = call.options.value(QStringLiteral("seed")).toMap();
            if (seed.value(QStringLiteral("providerId")).toString() == failedSeed)
                fixture.source.complete(call, QVariantMap(), QStringLiteral("fixture offline"));
            else
                fixture.source.complete(call, fixture.source.defaultReply(call));
        }
        QTRY_VERIFY(fixture.source.pendingRequests(QStringLiteral("artistCandidates")).isEmpty());
        QTRY_VERIFY(sectionHasProviderId(fixture.service, QStringLiteral("from-your-artists"),
                                         QStringLiteral("MPRE_00000006")));
        QVERIFY(fixture.service.homeStatus() != QStringLiteral("error"));
        QVERIFY(!sectionItems(fixture.service, QStringLiteral("from-your-artists")).isEmpty());
    }

    void stableInputClockOrder()
    {
        const QStringList forward = runTwoSeedOrder(false);
        const QStringList reverse = runTwoSeedOrder(true);
        QVERIFY(!forward.isEmpty());
        QVERIFY(!reverse.isEmpty());
        QCOMPARE(forward, reverse);
    }

    void retryCannotBoostScore()
    {
        HomeFixture fixture({artistSeed(7)});
        QTRY_VERIFY(!fixture.source.pendingRequests(QStringLiteral("artistCandidates")).isEmpty());
        pumpSuccessfulReplies(fixture.source);
        const QVariantList initialRows = sectionItems(fixture.service, QStringLiteral("from-your-artists"));
        QVERIFY(!initialRows.isEmpty());
        const QVariantMap initialCard = initialRows.first().toMap();
        const QString stableId = providerItemId(initialCard);
        QVERIFY(!stableId.isEmpty());
        const double initialScore = initialCard.value(QStringLiteral("score")).toDouble();

        fixture.service.refreshHome(true);
        QTRY_VERIFY(fixture.source.callsFor(QStringLiteral("artistCandidates")).size() >= 2);
        const QList<FakeSource::Request> pending = fixture.source.pendingRequests(QStringLiteral("artistCandidates"));
        QVERIFY(!pending.isEmpty());
        for (const FakeSource::Request &call : pending)
            fixture.source.complete(call, fixture.source.defaultReply(call));
        QTRY_VERIFY(fixture.source.pendingRequests(QStringLiteral("artistCandidates")).isEmpty());
        const QVariantList retriedRows = sectionItems(fixture.service, QStringLiteral("from-your-artists"));
        QVERIFY(!retriedRows.isEmpty());
        auto retried = std::find_if(retriedRows.cbegin(), retriedRows.cend(), [&](const QVariant &value) {
            return providerItemId(value.toMap()) == stableId;
        });
        QVERIFY(retried != retriedRows.cend());
        QCOMPARE(retried->toMap().value(QStringLiteral("score")).toDouble(), initialScore);
    }

    void lateReplyCannotReplaceNewSeeds()
    {
        HomeFixture fixture({artistSeed(8)});
        QTRY_VERIFY(!fixture.source.pendingRequests(QStringLiteral("artistCandidates")).isEmpty());
        const FakeSource::Request oldRequest = fixture.source.pendingRequests(QStringLiteral("artistCandidates")).first();

        fixture.preferences.setTaste({artistSeed(9)}, true);
        fixture.service.beginHomeVisit();
        QTRY_VERIFY(std::any_of(fixture.source.requests.cbegin(), fixture.source.requests.cend(), [](const auto &call) {
            return call.operation == QStringLiteral("artistCandidates")
                && call.options.value(QStringLiteral("seed")).toMap().value(QStringLiteral("providerId")).toString()
                    == artistSeed(9).value(QStringLiteral("providerId")).toString();
        }));
        const auto newCall = std::find_if(fixture.source.requests.cbegin(), fixture.source.requests.cend(), [](const auto &call) {
            return call.operation == QStringLiteral("artistCandidates")
                && call.options.value(QStringLiteral("seed")).toMap().value(QStringLiteral("providerId")).toString()
                    == artistSeed(9).value(QStringLiteral("providerId")).toString();
        });
        QVERIFY(newCall != fixture.source.requests.cend());
        fixture.source.complete(*newCall, fixture.source.defaultReply(*newCall));
        QTRY_VERIFY(sectionHasProviderId(fixture.service, QStringLiteral("from-your-artists"),
                                         QStringLiteral("MPRE_00000009")));

        fixture.source.completeLate(oldRequest, fixture.source.defaultReply(oldRequest));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QVERIFY(sectionHasProviderId(fixture.service, QStringLiteral("from-your-artists"),
                                    QStringLiteral("MPRE_00000009")));
        QVERIFY(!sectionHasProviderId(fixture.service, QStringLiteral("from-your-artists"),
                                     QStringLiteral("MPRE_00000008")));
    }

    void refreshDoesNotMutateQueue()
    {
        HomeFixture fixture({artistSeed(10)});
        const QVariantList originalQueue{QVariantMap{{QStringLiteral("queueId"), QStringLiteral("keep-a")}},
                                         QVariantMap{{QStringLiteral("queueId"), QStringLiteral("keep-b")}}};
        fixture.source.queueSentinel = originalQueue;
        QTRY_VERIFY(!fixture.source.pendingRequests(QStringLiteral("artistCandidates")).isEmpty());
        pumpSuccessfulReplies(fixture.source);

        QCOMPARE(fixture.source.queueSentinel, originalQueue);
        QCOMPARE(fixture.source.queueMutationCalls, 0);
    }

    void fairnessBoundedRequests()
    {
        QVariantList seeds;
        for (int i = 20; i < 32; ++i)
            seeds.append(artistSeed(i));
        HomeFixture fixture(seeds);
        QTRY_VERIFY(!fixture.source.pendingRequests(QStringLiteral("artistCandidates")).isEmpty());
        pumpSuccessfulReplies(fixture.source);

        const QList<FakeSource::Request> firstRefresh = fixture.source.callsFor(QStringLiteral("artistCandidates"));
        QVERIFY(firstRefresh.size() <= 6);
        QVERIFY(!firstRefresh.isEmpty());
        QVERIFY(fixture.source.maxConcurrent <= 2);
        QSet<QString> firstSeeds;
        for (const FakeSource::Request &call : firstRefresh)
            firstSeeds.insert(call.options.value(QStringLiteral("seed")).toMap()
                                  .value(QStringLiteral("providerId")).toString());
        QCOMPARE(firstSeeds.size(), firstRefresh.size());

        fixture.service.refreshHome(true);
        QTRY_VERIFY(fixture.source.callsFor(QStringLiteral("artistCandidates")).size()
                    > firstRefresh.size());
        pumpSuccessfulReplies(fixture.source);
        const QList<FakeSource::Request> allCalls = fixture.source.callsFor(QStringLiteral("artistCandidates"));
        const int secondRefreshCount = allCalls.size() - firstRefresh.size();
        QVERIFY(secondRefreshCount <= 6);
        QVERIFY(secondRefreshCount > 0);
        QSet<QString> allSeeds = firstSeeds;
        for (int i = firstRefresh.size(); i < allCalls.size(); ++i) {
            allSeeds.insert(allCalls[i].options.value(QStringLiteral("seed")).toMap()
                                .value(QStringLiteral("providerId")).toString());
        }
        QVERIFY(allSeeds.size() > firstSeeds.size());
    }

    void datesOnlyNew()
    {
        HomeFixture fixture({artistSeed(40)});
        QTRY_VERIFY(!fixture.source.pendingRequests(QStringLiteral("artistCandidates")).isEmpty());
        const FakeSource::Request call = fixture.source.pendingRequests(QStringLiteral("artistCandidates")).first();
        const QVariantMap seed = call.options.value(QStringLiteral("seed")).toMap();
        const QDate today = QDateTime::fromMSecsSinceEpoch(kFixedNowMs, Qt::UTC).date();
        const QVariantList rows{
            albumCandidate(seed, QStringLiteral("MPRE_RECENT"), QStringLiteral("Recent Release"),
                           {{QStringLiteral("releaseDate"), today.addDays(-89).toString(Qt::ISODate)}}),
            albumCandidate(seed, QStringLiteral("MPRE_YEAR_ONLY"), QStringLiteral("Year Only"),
                           {{QStringLiteral("year"), QStringLiteral("2026")}}),
            albumCandidate(seed, QStringLiteral("MPRE_OLD"), QStringLiteral("Old Release"),
                           {{QStringLiteral("releaseDate"), today.addDays(-91).toString(Qt::ISODate)}}),
            albumCandidate(seed, QStringLiteral("MPRE_BAD_DATE"), QStringLiteral("Invalid Date"),
                           {{QStringLiteral("releaseDate"), QStringLiteral("2026-19-42")}})};
        QVariantMap payload = fixture.source.defaultReply(call);
        payload.insert(QStringLiteral("items"), rows);
        fixture.source.complete(call, payload);

        QTRY_VERIFY(sectionHasProviderId(fixture.service, QStringLiteral("new-from-your-artists"),
                                         QStringLiteral("MPRE_RECENT")));
        const QVariantList recentItems = sectionItems(fixture.service, QStringLiteral("new-from-your-artists"));
        QVERIFY(!recentItems.isEmpty());
        QVERIFY(!sectionHasProviderId(fixture.service, QStringLiteral("new-from-your-artists"),
                                     QStringLiteral("MPRE_YEAR_ONLY")));
        QVERIFY(!sectionHasProviderId(fixture.service, QStringLiteral("new-from-your-artists"),
                                     QStringLiteral("MPRE_OLD")));
        QVERIFY(!sectionHasProviderId(fixture.service, QStringLiteral("new-from-your-artists"),
                                     QStringLiteral("MPRE_BAD_DATE")));
    }
};

QTEST_GUILESS_MAIN(VinylRecommendationServiceTest)
#include "vinyl_recommendation_service_test.moc"
