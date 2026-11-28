#include <QtTest>
#include <algorithm>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QMap>
#include <QProcess>
#include <QProcessEnvironment>
#include <functional>

#include "vinyl/VinylRecommendationService.h"
#include "vinyl/VinylProvider.h"
#include "vinyl/VinylRecommendationIdentity.h"
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

QVariantMap localTasteArtist(int index)
{
    const QString id = QStringLiteral("fixture-gate-artist-%1").arg(index);
    return {{QStringLiteral("kind"), QStringLiteral("artist")},
            {QStringLiteral("provider"), QStringLiteral("local")},
            {QStringLiteral("providerId"), id},
            {QStringLiteral("name"), QStringLiteral("Fixture Local Artist %1").arg(index)}};
}

QVariantList withFiveDistinctTasteArtists(QVariantList seeds)
{
    QSet<QString> identities;
    for (const QVariant &value : seeds) {
        const QVariantMap row = value.toMap();
        const QString canonical = row.value(QStringLiteral("mappingStatus")) == QStringLiteral("verified")
            ? row.value(QStringLiteral("canonicalMbid")).toString().toLower() : QString();
        const QString identity = canonical.isEmpty()
            ? row.value(QStringLiteral("provider")).toString() + QLatin1Char(':')
                + row.value(QStringLiteral("providerId")).toString()
            : QStringLiteral("musicbrainz:") + canonical;
        if (!identity.endsWith(QLatin1Char(':')))
            identities.insert(identity);
    }
    for (int index = 0; identities.size() < 5; ++index) {
        const QVariantMap row = localTasteArtist(index);
        seeds.append(row);
        identities.insert(QStringLiteral("local:") + row.value(QStringLiteral("providerId")).toString());
    }
    return seeds;
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
    Q_PROPERTY(QString vinylHomeSelectionSnapshot MEMBER selection)
    Q_PROPERTY(QVariantList vinylCratePins MEMBER cratePins NOTIFY vinylCratePinsChanged)
public:
    int tasteWrites = 0;
    int exposureWrites = 0;
    bool rejectExposureWrites = false;
    QString feedback,exposure,selection;
    QVariantList cratePins;
    int cratePinWrites = 0;
    Q_INVOKABLE bool setVinylCratePins(const QVariantList &value) {++cratePinWrites;cratePins=value;emit vinylCratePinsChanged();return true;}
    Q_INVOKABLE bool setVinylFeedbackSnapshot(const QString &value) {feedback=value;emit vinylFeedbackChanged();return true;}
    Q_INVOKABLE bool setVinylRecommendationExposureSnapshot(const QString &value) {
        ++exposureWrites;
        if (rejectExposureWrites) return false;
        exposure=value;emit vinylRecommendationExposureChanged();return true;
    }
    Q_INVOKABLE bool setVinylHomeSelectionSnapshot(const QString &value) {selection=value;return true;}

    void setTaste(const QVariantList &artists, bool answered = true)
    {
        m_artists = artists;
        m_answered = answered;
        emit vinylTasteChanged();
    }

    void replaceTasteForProfileSwitch(const QVariantList &artists, bool answered = true)
    {
        m_artists = artists;
        m_answered = answered;
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
    void vinylCratePinsChanged();

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
    bool runtimeFixtures = false;
    bool manualDiscography = false;
    bool manualSimilarity = false;
    bool manualListening = false;
    QList<QString> cancelledLanes;
    QList<Request> requests;
    QSet<QString> freshCachedArtistIds;
    QMap<QString,QVariantMap> cachedCharts;
    mutable QList<QString> cachedQueries;

    ~FakeSource() override
    {
        if (m_fixtureProcess) {
            m_fixtureProcess->closeWriteChannel();
            if (!m_fixtureProcess->waitForFinished(1000)) {
                m_fixtureProcess->kill();m_fixtureProcess->waitForFinished(1000);
            }
        }
    }

    Q_INVOKABLE int request(const QString &lane, const QString &operation,
                            const QString &value, const QVariantMap &options)
    {
        // Recommendation service tests exercise Home ranking, not the separate
        // listening provider lanes. Keep those requests from affecting the
        // bounded Home concurrency fixtures.
        if (lane.startsWith(QStringLiteral("home-listening-")) && !runtimeFixtures && !manualListening)
            return -1;
        if (!runtimeFixtures && (operation=="chartCandidates" || (operation=="similarArtists" && !manualSimilarity)
                                 || (operation=="recommendationDiscography" && !manualDiscography)))
            return -1; // Existing ranker fixtures deliberately provide no optional metadata.
        Request call{lane, operation, value, options, ++m_nextId};
        requests.append(call);
        m_pending.insert(call.id);
        maxConcurrent = qMax(maxConcurrent, m_pending.size());
        if (runtimeFixtures) {
            // Use the existing newline-stdio format with one pure fixture
            // process, so this owner integration test need not boot Python for
            // every metadata request. Provider pacing has its own native test.
            if (!m_fixtureProcess) {
                m_fixtureProcess=new QProcess(this);
                auto environment=QProcessEnvironment::systemEnvironment();
                environment.insert("VINYL_TEST_V1_POOL","1");
                m_fixtureProcess->setProcessEnvironment(environment);
                connect(m_fixtureProcess,&QProcess::readyReadStandardOutput,this,[this] {
                    m_fixtureOutput+=m_fixtureProcess->readAllStandardOutput();
                    int newline=-1;
                    while ((newline=m_fixtureOutput.indexOf('\n'))>=0) {
                        const auto reply=QJsonDocument::fromJson(m_fixtureOutput.left(newline)).object();
                        m_fixtureOutput.remove(0,newline+1);
                        if (m_fixtureRequests.isEmpty()) continue;
                        const auto call=m_fixtureRequests.dequeue();
                        if (!m_pending.contains(call.id)) continue;
                        if (!reply.value("ok").toBool())
                            complete(call,{},reply.value("error").toString("The pure Home fixture failed."));
                        else complete(call,reply.value("data").toVariant());
                    }
                });
                m_fixtureProcess->start(qEnvironmentVariable("COLOSSEUM_VINYL_PYTHON"),
                    {"-I",QFINDTESTDATA("vinyl-live/fake_provider.py"),"--serve"});
            }
            auto input=call.options;input["operation"]=call.operation;input["value"]=call.value;
            m_fixtureRequests.enqueue(call);
            m_fixtureProcess->write(QJsonDocument::fromVariant(input).toJson(QJsonDocument::Compact)+"\n");
        }
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
        if (operation == QStringLiteral("chartCandidates"))
            return cachedCharts.value(value);
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
    QProcess *m_fixtureProcess=nullptr;
    QByteArray m_fixtureOutput;
    QQueue<Request> m_fixtureRequests;
};

// Most unit fixtures assert a specific publication phase using an in-memory
// source. Deliver the queued visit turn without also draining provider replies.
void runHomeVisit(VinylRecommendationService &service)
{
    service.beginHomeVisit();
    auto *timer = service.findChild<QTimer *>("homeVisitTimer");
    QVERIFY(timer && timer->isActive());
    timer->stop();
    QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
}

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
        const QVariantList taste = beginVisit && answered
            ? withFiveDistinctTasteArtists(seeds) : seeds;
        preferences.setTaste(taste, answered);
        if (beginVisit)
            runHomeVisit(service);
    }
};

// Deliver the single-shot input window deterministically; no wall-clock sleep.
void expireHomeInputs(VinylRecommendationService &service)
{
    auto timer = service.findChild<QTimer *>("homeInputsTimer");
    QVERIFY(timer);
    if (!timer->isActive())
        return;
    timer->stop();
    QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
}

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

QVariantMap homeRow(const VinylRecommendationService &service, const QString &id)
{
    return sectionById(service.homeSourcePool().value("rows").toList(), id);
}

void recordFixturePlay(ActivityStore &activity, int artist, qint64 at, int activeMs, bool skipped=false,
                       bool includeArtistName=true)
{
    const auto seed=artistSeed(artist);
    const auto id=seed.value("providerId");
    QVariantMap fact{{"sessionId",QString("played-%1-%2").arg(artist).arg(at)},{"world","vinyl"},{"kind","music"},
        {"titleKey","album"},{"itemKey","track"},{"title","Track"},{"utcOffsetMinutes",0},{"syncable",false}};
    auto start=fact;start["atMs"]=at;start["action"]="start";start["origin"]="manual";
    start["positionMs"]=0;start["durationMs"]=120000;
    start["entityRef"]=QVariantMap{{"identity","provider"},{"kind","recording"},{"providerId","youtube-music"},{"itemId","Abc123def45"}};
    start["artistRefs"]=QVariantList{QVariantMap{{"identity","provider"},{"kind","artist"},{"providerId","youtube-music"},{"itemId",id}}};
    if(includeArtistName)start["artistNames"]=QVariantMap{{id.toString(),seed.value("name")}};
    QVERIFY(activity.recordMusicAction(start));
    if(skipped){
        auto outcome=start;outcome["atMs"]=at+1000+activeMs;outcome["action"]="skip";outcome["positionMs"]=activeMs;
        QVERIFY(activity.recordMusicAction(outcome));
    }
    fact["startAtMs"]=at+1000;fact["endAtMs"]=at+1000+activeMs;fact["activeMs"]=activeMs;fact["rateMilli"]=1000;
    QVERIFY(activity.recordPlaybackDelta(fact));
}

QSet<QString> requestedArtists(const FakeSource &source, const QString &operation)
{
    QSet<QString> ids;
    for(const auto &call:source.callsFor(operation))ids.insert(call.value);
    return ids;
}

QString fixtureArtistMbid(int artist)
{
    return QString("b0000000-0000-4000-8000-%1").arg(artist,12,10,QLatin1Char('0'));
}

QVariantMap similarityCard(int artist, int rank, bool lastfm)
{
    const auto name=artistSeed(artist).value("name");
    const auto mbid=fixtureArtistMbid(artist);
    const QString source=lastfm?"lastfm":"listenbrainz";
    const QString sourceId=lastfm?"mbid:"+mbid:mbid;
    const QString hash=QString::fromLatin1(QCryptographicHash::hash(sourceId.toUtf8(),QCryptographicHash::Sha256).toHex().left(24));
    return {{"catalogueCard",true},{"source",source},{"kind","artists"},{"id",source+":artists:"+hash},
        {"title",name},{"artist",name},{"sourceIds",QVariantMap{{"id",sourceId}}},
        {"hints",QVariantMap{{"mbidEvidence",QVariantMap{{"artist",mbid}}}}},
        {"rank",rank},{"similarity",lastfm?1.0/rank:1000.0/rank}};
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

int selectedYoutubeMusicArtists(const VinylRecommendationService &service)
{
    int count = 0;
    for (const QVariant &value : service.selectedArtists())
        if (value.toMap().value(QStringLiteral("provider")) == QStringLiteral("youtube-music"))
            ++count;
    return count;
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

void pumpAllHomeReplies(FakeSource &source, int maxPasses = 64)
{
    for (int pass = 0; pass < maxPasses; ++pass) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        const QList<FakeSource::Request> pending = source.pendingRequests();
        if (pending.isEmpty()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            if (source.pendingRequests().isEmpty())
                break;
            continue;
        }
        for (const FakeSource::Request &call : pending) {
            if (call.operation == QStringLiteral("artistCandidates"))
                source.complete(call, source.defaultReply(call));
            else
                source.complete(call, {}, QStringLiteral("Optional fixture unavailable"));
        }
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

QStringList carouselKeys(const VinylRecommendationService &service)
{
    QStringList keys;
    for (const QVariant &value : service.homeCarouselItems()) {
        const QVariantMap card = value.toMap();
        const QString key = card.value(QStringLiteral("key"), card.value(QStringLiteral("id"))).toString();
        if (!key.isEmpty())
            keys.append(key);
    }
    return keys;
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
    void beginHomeVisitDoesNotWaitForCatalogueCache()
    {
        QTemporaryDir cache;
        QVERIFY(cache.isValid());
        VinylProvider provider({}, nullptr, 128LL * 1024 * 1024,
                               [] { return kFixedNowMs; }, cache.path());
        VinylRecommendationService service(nullptr, [] { return kFixedNowMs; });
        FakePreferences prefs;
        service.setPreferences(&prefs);
        service.setSource(&provider);
        QVariantList seeds;
        for (int i = 0; i < 48; ++i)
            seeds.append(artistSeed(i));
        prefs.setTaste(seeds);
        // Use the production seed projection and v2 cache keys/envelopes.
        for (const auto &entry : service.selectedArtists()) {
            const auto seed = entry.toMap();
            const auto id = seed.value("providerId").toString();
            const QVariantMap input{{"operation","artistCandidates"},{"value",id},
                                    {"cacheSchema",2},{"seed",seed}};
            const auto key = QString::fromLatin1(QCryptographicHash::hash(
                QJsonDocument::fromVariant(input).toJson(QJsonDocument::Compact),
                QCryptographicHash::Sha256).toHex());
            QVariantList items;
            for (int j = 0; j < 48; ++j)
                items.append(albumCandidate(seed, "MPRE_" + id.right(8) + QString::number(j),
                                            QString("Fixture album %1").arg(j)));
            const QVariantMap data{{"version",1},{"seed",seed},{"items",items},
                {"provenance",QVariantMap{{"source","youtube-music"}}},
                {"fetchedAt",QDateTime::fromMSecsSinceEpoch(kFixedNowMs, Qt::UTC).toString(Qt::ISODateWithMs)},
                {"complete",true},{"status","ready"}};
            QFile file(cache.filePath(key + ".json"));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write(QJsonDocument::fromVariant(QVariantMap{{"version",2},
                {"requestKey",key},{"storedAtMs",kFixedNowMs},{"ttlMs",6 * 3600000},{"data",data}})
                .toJson(QJsonDocument::Compact)) > 0);
        }
        QCOMPARE(service.selectedArtists().size(), seeds.size());
        QElapsedTimer timer;
        timer.start();
        service.beginHomeVisit();
        const auto elapsedNs = timer.nsecsElapsed();
        qInfo() << "beginHomeVisit (48 cached seeds):" << elapsedNs / 1e6 << "ms";
        QVERIFY(elapsedNs < 20'000'000);
        QTRY_COMPARE(service.homeStatus(), QString("ready"));
        QVERIFY(!sectionById(service.homeSections(), "from-your-artists").value("items").toList().isEmpty());

        // A disk batch already running must not publish into the next profile.
        service.refreshHome(true);
        QVERIFY(!service.findChildren<QFutureWatcherBase *>().isEmpty());
        service.setProfileGeneration(service.profileGeneration()+1);
        QTRY_VERIFY(service.findChildren<QFutureWatcherBase *>().isEmpty());
        QVERIFY(service.homeSections().isEmpty());
        QVERIFY(service.homeSnapshot().isEmpty());
    }
    void activityChangesCoalesceWhileHomeIsVisible() {
        qint64 now = kFixedNowMs;
        VinylRecommendationService service(nullptr, [&now] { return now; });
        ActivityStore activity;
        service.setActivityStore(&activity);
        runHomeVisit(service);
        QSignalSpy rebuilt(&service, &VinylRecommendationService::homeChanged);
        for (int i = 0; i < 20; ++i) {
            now += 50;
            emit activity.changed();
        }
        QCOMPARE(rebuilt.size(), 0);
        auto timer = service.findChild<QTimer *>("homeInputsTimer");
        QVERIFY(timer);
        QVERIFY(timer->isSingleShot());
        QCOMPARE(timer->interval(), 2000);
        QVERIFY(timer->isActive());
        now = kFixedNowMs + 2000;
        timer->stop();
        QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
        QCOMPARE(rebuilt.size(), 1);
        QCOMPARE(service.homeSnapshot().value("evaluatedAtMs").toLongLong(), now);
        QVERIFY(!timer->isActive());
        emit activity.changed();
        QCOMPARE(rebuilt.size(), 1);
        QVERIFY(timer->isActive()); // The next burst gets another window.
    }
    void collectionChangesShareTheDeferredVisibilityWindow() {
        HomeFixture fixture({}, kFixedNowMs, false, false);
        runHomeVisit(fixture.service);
        QSignalSpy rebuilt(&fixture.service, &VinylRecommendationService::homeChanged);
        for (int i = 0; i < 20; ++i)
            fixture.service.setHomeCollections({{"revision", i}});
        QCOMPARE(rebuilt.size(), 0);
        expireHomeInputs(fixture.service);
        QCOMPARE(rebuilt.size(), 1);
        QVERIFY(QMetaObject::invokeMethod(&fixture.service, "leaveHome", Qt::DirectConnection));
        fixture.service.setHomeCollections({{"revision", 21}});
        expireHomeInputs(fixture.service);
        QCOMPARE(rebuilt.size(), 1);
        runHomeVisit(fixture.service);
        QCOMPARE(rebuilt.size(), 2);
    }
    void activityChangesWaitUntilHomeIsVisitedAgain() {
        HomeFixture fixture({}, kFixedNowMs, false, false);
        ActivityStore activity;
        fixture.service.setActivityStore(&activity);
        runHomeVisit(fixture.service);
        emit activity.changed(); // Leaving must cancel a pending visible window too.
        QVERIFY(QMetaObject::invokeMethod(&fixture.service, "leaveHome", Qt::DirectConnection));
        QSignalSpy rebuilt(&fixture.service, &VinylRecommendationService::homeChanged);
        for (int i = 0; i < 20; ++i)
            emit activity.changed();
        QCOMPARE(rebuilt.size(), 0);
        auto timer = fixture.service.findChild<QTimer *>("homeInputsTimer");
        QVERIFY(timer);
        QVERIFY(!timer->isActive());
        runHomeVisit(fixture.service);
        QCOMPARE(rebuilt.size(), 1);
    }
    void historyResetClearsPrivateStateBeforeDeferredRebuild() {
        HomeFixture fixture({}, kFixedNowMs, false, false);
        ActivityStore activity;
        fixture.service.setActivityStore(&activity);
        runHomeVisit(fixture.service);
        fixture.preferences.exposure = "private exposure";
        fixture.preferences.selection = "private selection";
        QSignalSpy rebuilt(&fixture.service, &VinylRecommendationService::homeChanged);
        fixture.service.setKeepHistory(false);
        QVERIFY(fixture.preferences.exposure.isEmpty());
        QVERIFY(fixture.preferences.selection.isEmpty());
        QVERIFY(fixture.service.historySnapshot().isEmpty());
        QVERIFY(fixture.service.homeCarouselItems().isEmpty());
        QVERIFY(fixture.service.homeSnapshot().isEmpty()); // Clear, not a new projection.
        auto timer = fixture.service.findChild<QTimer *>("homeInputsTimer");
        QVERIFY(timer);
        QVERIFY(timer->isActive());
        expireHomeInputs(fixture.service);
        fixture.service.setKeepHistory(true);
        QVERIFY(QMetaObject::invokeMethod(&fixture.service, "leaveHome", Qt::DirectConnection));
        fixture.preferences.exposure = "post-reset exposure";
        fixture.preferences.selection = "post-reset selection";
        QVERIFY(activity.clearAll());
        QVERIFY(fixture.preferences.exposure.isEmpty());
        QVERIFY(fixture.preferences.selection.isEmpty());
        QVERIFY(fixture.service.historySnapshot().isEmpty());
        QVERIFY(fixture.service.homeSnapshot().isEmpty());
        QVERIFY(!timer->isActive());
    }
    void feedbackStillRebuildsImmediatelyWithPendingActivity() {
        HomeFixture fixture({}, kFixedNowMs, false, false);
        ActivityStore activity;
        fixture.service.setActivityStore(&activity);
        runHomeVisit(fixture.service);
        emit activity.changed();
        QSignalSpy rebuilt(&fixture.service, &VinylRecommendationService::homeChanged);
        const QVariantMap ref{{"identity","provider"},{"kind","artist"},
            {"providerId","youtube-music"},{"itemId",artistSeed(1).value("providerId")}};
        QVERIFY(fixture.service.setFeedback(ref, "like", fixture.service.profileGeneration()));
        QCOMPARE(rebuilt.size(), 1);
        auto timer = fixture.service.findChild<QTimer *>("homeInputsTimer");
        QVERIFY(timer);
        QVERIFY(!timer->isActive());
    }
    void multiSongRadioFeedsTheExistingRanker() {
        QVariantList tastes;
        for (int i=1;i<=5;++i) tastes.append(artistSeed(i));
        HomeFixture fixture(tastes);
        FakeSource::Request lastSongCall;
        QVariantMap lastSongReply;
        auto song=[](const QVariantMap &seed,const QString &video,const QString &album,int credited) {
            const auto credit=artistSeed(credited);
            const QVariantList credits{credit};
            const QString title="Song "+video;
            const QVariantMap track{{"id","ytm:track:"+video},{"videoId",video},{"title",title},
                {"seconds",180},{"available",true},{"artistRefs",credits},
                {"artists",QStringList{credit.value("name").toString()}},
                {"artistIds",QStringList{"ytm:artist:"+credit.value("providerId").toString()}}};
            return QVariantMap{{"kind","song"},{"provider","youtube-music"},{"providerId",video},
                {"id","ytm:track:"+video},{"title",title},{"seconds",180},{"artistRefs",credits},
                {"seedArtistRef",seed},{"relation","song-radio"},{"candidateSources",QStringList{"radio"}},
                {"albumRef",QVariantMap{{"kind","album"},{"provider","youtube-music"},{"providerId",album}}},
                {"record",QVariantMap{{"id","ytm:track:"+video},{"type","Song"},{"origin","youtube-music"},
                    {"title",title},{"artists",track.value("artists")},{"artistIds",track.value("artistIds")},
                    {"tracks",QVariantList{track}}}}};
        };
        for (int pass=0;pass<100;++pass) {
            QCoreApplication::processEvents(QEventLoop::AllEvents,20);
            const auto pending=fixture.source.pendingRequests();
            if(pending.isEmpty()&&pass>10) break;
            for(const auto &call:pending) {
                const auto seed=call.options.value("seed").toMap();
                if(call.operation=="artistCandidates") {
                    auto response=fixture.source.defaultReply(call);
                    auto items=response.value("items").toList();
                    for(int i=0;i<4;++i) {
                        const auto video=QString("S%1%2").arg(seed.value("providerId").toString().right(2)).arg(i).leftJustified(11,'x');
                        auto row=song(seed,video,"MPRE_seed_"+QString::number(i),seed.value("providerId").toString().right(2).toInt());
                        row["relation"]="top-recording";row.remove("record");row.remove("candidateSources");
                        items.append(row);
                    }
                    response["items"]=items;fixture.source.complete(call,response);
                }else if(call.operation=="songCandidates") {
                    QVariantList items;
                    const int artist=seed.value("providerId").toString().right(2).toInt();
                    for(int i=0;i<5;++i) {
                        const auto video=QString("R%1%2%3").arg(artist).arg(call.value.mid(3,1)).arg(i).leftJustified(11,'y');
                        items.append(song(seed,video,"MPRE_result_"+QString::number(artist)+"_"+QString::number(i/2),20+artist+i));
                    }
                    lastSongCall=call;
                    lastSongReply=QVariantMap{{"version",1},{"seed",seed},{"items",items},
                        {"provenance",QVariantMap{{"source","youtube-music"}}},
                        {"fetchedAt",QDateTime::fromMSecsSinceEpoch(kFixedNowMs,Qt::UTC).toString(Qt::ISODateWithMs)},
                        {"complete",false},{"status","ready"}};
                    fixture.source.complete(call,lastSongReply);
                }else fixture.source.complete(call,{},"Optional fixture unavailable");
            }
        }
        QCOMPARE(fixture.source.callsFor("songCandidates").size(),10);
        const auto picks=fixture.service.homeQuickPicks();
        QVERIFY(picks.size()>=6);
        QSet<QString> videos,albums;
        for(const auto &value:picks) {
            const auto card=value.toMap();
            verifyUiCard(card);
            QVERIFY(card.value("destination").toMap().value("playable").toBool());
            const auto record=card.value("record").toMap();
            const auto track=record.value("tracks").toList().value(card.value("trackIndex").toInt()).toMap();
            const auto video=track.value("videoId").toString();
            QVERIFY(!video.isEmpty());QVERIFY(!videos.contains(video));videos.insert(video);
            const auto album=card.value("albumRef").toMap().value("providerId").toString();
            QVERIFY(!album.isEmpty());QVERIFY(!albums.contains(album));albums.insert(album);
        }
        QCOMPARE(fixture.source.queueMutationCalls,0);
        QVERIFY(fixture.service.homeSnapshot().value("songRecommendations").toMap().value("seedSongCount").toInt()>=10);
        const auto initialSeeds=fixture.service.homeSnapshot().value("songRecommendations").toMap().value("seedSongs").toList();
        const auto excluded=artistSeed(21);
        const auto excludedRef=VinylRecommendationIdentity::fromCatalogue(excluded).value("ref").toMap();
        QVERIFY(fixture.service.setFeedback(excludedRef,"exclude-artist",fixture.service.profileGeneration()));
        QCoreApplication::processEvents(QEventLoop::AllEvents,20);
        QVERIFY(!fixture.service.homeQuickPicks().isEmpty());
        for(const auto &entry:fixture.service.homeQuickPicks())for(const auto &credit:entry.toMap().value("artistRefs").toList())
            QVERIFY(credit.toMap().value("providerId")!=excluded.value("providerId"));
        QCOMPARE(fixture.source.callsFor("songCandidates").size(),10);
        fixture.service.refreshHomeSelection();
        QCoreApplication::processEvents(QEventLoop::AllEvents,20);
        const auto refreshedSeeds=fixture.service.homeSnapshot().value("songRecommendations").toMap().value("seedSongs").toList();
        QVERIFY(initialSeeds!=refreshedSeeds);
        QVERIFY2(!fixture.service.homeQuickPicks().isEmpty(),"Refresh must retain usable picks while replacements load");
        for(int pass=0;pass<30;++pass) {
            QCoreApplication::processEvents(QEventLoop::AllEvents,20);
            const auto pending=fixture.source.pendingRequests();
            if(pending.isEmpty())break;
            for(const auto &call:pending)fixture.source.complete(call,{},"Simulated provider outage");
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents,20);
        QVERIFY2(!fixture.service.homeQuickPicks().isEmpty(),"Provider failure must preserve the last usable candidate pool");
        QCOMPARE(fixture.service.homeSnapshot().value("songRecommendations").toMap().value("status").toString(),QStringLiteral("partial"));
        for(const auto &entry:fixture.service.homeQuickPicks())for(const auto &credit:entry.toMap().value("artistRefs").toList())
            QVERIFY(credit.toMap().value("providerId")!=excluded.value("providerId"));
        fixture.service.setProfileGeneration(10);
        QVERIFY(fixture.service.homeQuickPicks().isEmpty());
        fixture.source.completeLate(lastSongCall,lastSongReply);
        QCoreApplication::processEvents(QEventLoop::AllEvents,20);
        QVERIFY(fixture.service.homeQuickPicks().isEmpty());
        QCOMPARE(fixture.source.queueMutationCalls,0);
    }

    void directSongRecommendationsCannotChangeTheirPlaybackIdentity() {
        const auto seed=artistSeed(1);
        const QString video="abcdefghijk";
        const QVariantList credits{seed};
        const QVariantMap track{{"id","ytm:track:"+video},{"videoId",video},{"title","Exact Song"},
            {"seconds",180},{"available",true},{"artistRefs",credits},
            {"artists",QStringList{"Fixture"}},{"artistIds",QStringList{"ytm:artist:"+seed.value("providerId").toString()}}};
        QVariantMap item{{"kind","song"},{"provider","youtube-music"},{"providerId",video},{"id","ytm:track:"+video},
            {"title","Exact Song"},{"seconds",180},{"artistRefs",credits},{"seedArtistRef",seed},
            {"relation","song-radio"},{"candidateSources",QStringList{"radio"}},
            {"record",QVariantMap{{"id","ytm:track:"+video},{"type","Song"},{"origin","youtube-music"},
                {"tracks",QVariantList{track}}}}};
        QVariantMap response{{"version",1},{"seed",seed},{"items",QVariantList{item}},
            {"provenance",QVariantMap{{"source","youtube-music"}}},
            {"fetchedAt",QDateTime::fromMSecsSinceEpoch(kFixedNowMs,Qt::UTC).toString(Qt::ISODateWithMs)},
            {"complete",false},{"status","ready"}};
        const QVariantMap recording{{"kind","song"},{"provider","youtube-music"},{"providerId",video},{"seedArtistRef",seed}};
        const QVariantMap options{{"seed",seed},{"recording",recording}};
        QVariantMap normalized;
        QVERIFY(VinylProvider::normalizeHome("songCandidates",video,options,response,&normalized));
        const auto accepted=normalized.value("items").toList().first().toMap();
        QVERIFY(accepted.value("destination").toMap().value("playable").toBool());
        QCOMPARE(accepted.value("mappingStatus").toString(),QStringLiteral("unresolved"));
        auto wrong=track;wrong["videoId"]="wrongtrack1";
        auto record=item.value("record").toMap();record["tracks"]=QVariantList{wrong};item["record"]=record;
        response["items"]=QVariantList{item};
        QVERIFY(!VinylProvider::normalizeHome("songCandidates",video,options,response,&normalized));
    }

    void visibleExposurePersistsWithoutPublishingHomeModels() {
        HomeFixture fixture({artistSeed(1)});
        ActivityStore activity;
        fixture.service.setActivityStore(&activity);
        pumpAllHomeReplies(fixture.source);
        const auto sections = fixture.service.homeSections();
        const auto snapshot = fixture.service.homeSnapshot();
        const auto carousel = fixture.service.homeCarouselItems();
        const auto listening = fixture.service.homeListeningCards();
        const auto quickPicks = fixture.service.homeQuickPicks();
        const auto mix = fixture.service.homeMixView();
        const int revision = fixture.service.homeRevision();
        const auto items = sectionItems(fixture.service, "from-your-artists");
        QVERIFY(!items.isEmpty());
        const auto ref = items.first().toMap().value("ref").toMap();
        const auto token = snapshot.value("exposureSnapshotId").toString();
        const int writesBefore = fixture.preferences.exposureWrites;
        QSignalSpy homeChanged(&fixture.service, &VinylRecommendationService::homeChanged);
        QSignalSpy listeningChanged(&fixture.service, &VinylRecommendationService::listeningChanged);

        fixture.preferences.rejectExposureWrites = true;
        QVERIFY(!fixture.service.recordVisibleItems("from-your-artists", token, {ref}, 0));
        QVERIFY(fixture.preferences.exposure.isEmpty());
        QCOMPARE(fixture.preferences.exposureWrites, writesBefore + 1);
        QCOMPARE(homeChanged.count(), 0);
        QCOMPARE(listeningChanged.count(), 0);

        fixture.preferences.rejectExposureWrites = false;
        QElapsedTimer timer;
        timer.start();
        QVERIFY(fixture.service.recordVisibleItems("from-your-artists", token, {ref}, 0));
        const qint64 elapsedNs = timer.nsecsElapsed();
        const auto persisted = fixture.preferences.exposure;
        const auto records = QJsonDocument::fromJson(persisted.toUtf8()).object().value("records").toArray();
        QCOMPARE(records.size(), 1);
        QCOMPARE(records.first().toObject().value("entity").toObject().toVariantMap(), ref);
        QCOMPARE(records.first().toObject().value("shelf").toString(), QString("from-your-artists"));
        QCOMPARE(records.first().toObject().value("rankPosition").toInt(), 1);
        QCOMPARE(fixture.preferences.exposureWrites, writesBefore + 2);
        qInfo() << "Exposure persistence ns:" << elapsedNs << "homeChanged:" << homeChanged.count()
                << "listeningChanged:" << listeningChanged.count()
                << "revision delta:" << fixture.service.homeRevision() - revision;
        QCOMPARE(homeChanged.count(), 0);
        QCOMPARE(listeningChanged.count(), 0);
        QCOMPARE(fixture.service.homeRevision(), revision);
        QCOMPARE(fixture.service.homeSnapshot(), snapshot);
        QCOMPARE(fixture.service.homeSections(), sections);
        QCOMPARE(fixture.service.homeCarouselItems(), carousel);
        QCOMPARE(fixture.service.homeListeningCards(), listening);
        QCOMPARE(fixture.service.homeQuickPicks(), quickPicks);
        QCOMPARE(fixture.service.homeMixView(), mix);

        QVERIFY(fixture.service.recordVisibleItems("from-your-artists", token, {ref}, 0));
        QCOMPARE(fixture.preferences.exposure, persisted);
        QCOMPARE(fixture.preferences.exposureWrites, writesBefore + 2);
        QCOMPARE(homeChanged.count(), 0);
        QCOMPARE(listeningChanged.count(), 0);
        // Diagnostics remain a snapshot of composition until another real rebuild.
        fixture.service.setHomeCollections({{"songs", QVariantList()}});
        expireHomeInputs(fixture.service);
        QCOMPARE(fixture.service.homeSnapshot().value("exposureCount").toInt(), 1);
    }

    void visibleExposureDoesNotRerankTheCurrentVisit() {
        // A single real pick is valid without the old five-artist gate.
        HomeFixture fixture({artistSeed(1)},kFixedNowMs,false);
        ActivityStore activity;fixture.service.setActivityStore(&activity);
        runHomeVisit(fixture.service);
        const auto call=fixture.source.pendingRequests("artistCandidates").first();
        auto payload=fixture.source.defaultReply(call);QVariantList candidates;
        for(int i=0;i<16;++i)candidates.append(albumCandidate(call.options.value("seed").toMap(),
            QString("MPRE_%1").arg(i,8,10,QLatin1Char('0')),QString("Release %1").arg(i)));
        payload["items"]=candidates;fixture.source.complete(call,payload);QCoreApplication::processEvents();
        const auto before=sectionItems(fixture.service,"from-your-artists");QCOMPARE(before.size(),12);
        const auto token=fixture.service.homeSnapshot().value("exposureSnapshotId").toString();
        const auto ref=before.first().toMap().value("ref").toMap();
        QVERIFY(fixture.service.recordVisibleItems("from-your-artists",token,{ref},0));
        QCOMPARE(sectionItems(fixture.service,"from-your-artists"),before);
        // An unrelated input refresh within the visit must not apply the new
        // exposure either. The fatigue is retained for the next Home visit.
        fixture.service.setHomeCollections({{"songs",QVariantList()}});
        expireHomeInputs(fixture.service);
        QCOMPARE(sectionItems(fixture.service,"from-your-artists"),before);
        runHomeVisit(fixture.service);
        QVERIFY(sectionItems(fixture.service,"from-your-artists")!=before);
        QVERIFY(!fixture.preferences.exposure.isEmpty());
    }
    void implicitOwnArtistCreditsRespectSuppressionAndRecovery() {
        HomeFixture fixture({artistSeed(1)});
        const auto call=fixture.source.pendingRequests("artistCandidates").first();
        auto payload=fixture.source.defaultReply(call);
        auto own=payload.value("items").toList().first().toMap();own["artistRefs"]=QVariantList();
        const auto seed=call.options.value("seed").toMap();
        const auto other=artistSeed(2);auto related=other;related["title"]=other.value("name");
        related["relation"]="related-artist";related["seedArtistRef"]=seed;related["artistRefs"]=QVariantList();
        auto credit=seed;credit["name"]="Provider Credit";
        const auto credited=albumCandidate(seed,"MPRE_provider_credit","Credited Album",{{"artistRefs",QVariantList{credit}}});
        payload["items"]=QVariantList{own,related,credited};fixture.source.complete(call,payload);QCoreApplication::processEvents();
        const auto cards=sectionItems(fixture.service,"from-your-artists");
        QCOMPARE(cards.size(),2);
        for(const auto &entry:cards) {
            const auto card=entry.toMap();
            const auto credits=card.value("artistRefs").toList();
            QCOMPARE(credits.size(),1);
            QCOMPARE(credits.first().toMap().value("name").toString(),
                     card.value("title")==own.value("title")?seed.value("name").toString():QString("Provider Credit"));
        }
        const QVariantMap artist{{"identity","provider"},{"kind","artist"},{"providerId","youtube-music"},{"itemId",seed.value("providerId")}};
        QVERIFY(fixture.service.setFeedback(artist,"exclude-artist",0));
        QVERIFY(sectionItems(fixture.service,"from-your-artists").isEmpty());
        QCOMPARE(sectionItems(fixture.service,"related-discoveries").size(),1);
        const auto suppressed=fixture.service.homeSnapshot().value("suppressedItems").toList();
        QVERIFY(std::any_of(suppressed.begin(),suppressed.end(),[&](const auto &value){
            const auto item=value.toMap();return item.value("title")==own.value("title")&&!item.value("excludedArtistRefs").toList().isEmpty();
        }));
        QVERIFY(fixture.service.setFeedback(artist,"allow-artist",0));
        QCOMPARE(sectionItems(fixture.service,"from-your-artists").size(),2);
    }
    void retainedLibraryRowsRequireQualifiedHistoryAndOrderByEvidence() {
        HomeFixture fixture({});ActivityStore activity;
        auto album=[](QString id) {return QVariantMap{{"id",id},{"title",id},{"origin","local"},
            {"artistIds",QStringList{"library-artist"}},{"artists",QStringList{"Library Artist"}},
            {"tracks",QVariantList{QVariantMap{{"id",id+"-track"},{"title","Track"}}}}};};
        fixture.service.setHomeCollections({{"albums",QVariantList{QVariantMap{{"album",album("older")}},
            QVariantMap{{"album",album("frequent")}},QVariantMap{{"album",album("unknown")}}}}});
        fixture.service.setActivityStore(&activity);
        QVERIFY(sectionItems(fixture.service,"rediscover-library").isEmpty());
        QVERIFY(sectionItems(fixture.service,"less-played").isEmpty());
        auto listen=[&](QString id,int index,qint64 at,bool qualify=true,QString origin="manual") {
            QVariantMap fact{{"sessionId",id+QString::number(index)},{"world","vinyl"},{"kind","music"},
                {"titleKey",id},{"itemKey",id+"-track"},{"title","Track"},{"syncable",false},{"utcOffsetMinutes",0}};
            auto start=fact;start.insert("atMs",at);start.insert("action","start");start.insert("origin",origin);
            start.insert("positionMs",0);start.insert("durationMs",60000);
            start.insert("entityRef",QVariantMap{{"identity","provider"},{"kind","recording"},{"providerId","local"},{"itemId",id+"-track"}});
            start.insert("releaseRef",QVariantMap{{"identity","provider"},{"kind","release"},{"providerId","local"},{"itemId",id}});
            start.insert("artistRefs",QVariantList{QVariantMap{{"identity","provider"},{"kind","artist"},{"providerId","local"},{"itemId","library-artist"}}});
            QVERIFY(activity.recordMusicAction(start));if(!qualify)return;
            fact.insert("startAtMs",at+1000);fact.insert("endAtMs",at+13000);fact.insert("activeMs",12000);fact.insert("rateMilli",1000);
            QVERIFY(activity.recordPlaybackDelta(fact));
        };
        listen("older",0,kFixedNowMs-3LL*86400000);
        listen("frequent",0,kFixedNowMs-2LL*86400000);
        listen("frequent",1,kFixedNowMs-86400000);
        listen("unknown",0,kFixedNowMs-30000,false);
        expireHomeInputs(fixture.service);
        auto rediscover=sectionItems(fixture.service,"rediscover-library");QCOMPARE(rediscover.size(),1);
        QCOMPARE(providerItemId(rediscover.first().toMap()),QString("frequent")); // Leave the lower-count alternative to its own row.
        auto less=sectionItems(fixture.service,"less-played");QCOMPARE(less.size(),1);
        QCOMPARE(providerItemId(less.first().toMap()),QString("older"));
        QVERIFY(!fixture.service.homeSnapshot().value("historyComplete").toBool());
        listen("frequent",2,kFixedNowMs-60000,true,"album-continuation");
        expireHomeInputs(fixture.service);
        const auto crate=fixture.service.homeSourcePool().value("crate").toList();
        auto frequentAlbum=std::find_if(crate.cbegin(),crate.cend(),[](const QVariant &value){return value.toMap().value("kind")=="albums"&&value.toMap().value("id")=="frequent";});
        auto frequentArtist=std::find_if(crate.cbegin(),crate.cend(),[](const QVariant &value){return value.toMap().value("kind")=="artists";});
        QVERIFY(frequentAlbum!=crate.cend());QCOMPARE(frequentAlbum->toMap().value("playCount").toInt(),2);
        QVERIFY(frequentArtist!=crate.cend());QCOMPARE(frequentArtist->toMap().value("playCount").toInt(),3);
        QVERIFY(fixture.service.setFeedback(less.first().toMap().value("ref").toMap(),"hide",0));
        QVERIFY(!sectionHasProviderId(fixture.service,"rediscover-library","older"));
        fixture.service.setKeepHistory(false);
        QVERIFY(sectionItems(fixture.service,"rediscover-library").isEmpty());
        QVERIFY(sectionItems(fixture.service,"less-played").isEmpty());
        fixture.service.setKeepHistory(true);QVERIFY(activity.clearAll());
        QVERIFY(sectionItems(fixture.service,"rediscover-library").isEmpty());
    }
    void equalOrUnknownCountsDoNotClaimLessPlayed() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        ActivityStore activity(profile.filePath("activity.sqlite"));
        HomeFixture fixture({});fixture.service.setActivityStore(&activity);
        QVariantList albums;
        for(const QString id:{QString("one"),QString("two")}) {
            albums.append(QVariantMap{{"album",QVariantMap{{"id",id},{"title",id},{"origin","local"}}}});
        }
        fixture.service.setHomeCollections({{"albums",albums}});
        QVERIFY(sectionItems(fixture.service,"less-played").isEmpty());
        QVERIFY(sectionItems(fixture.service,"rediscover-library").isEmpty());
        for(const QString id:{QString("one"),QString("two")}) {
            QVariantMap fact{{"sessionId",id},{"world","vinyl"},{"kind","music"},{"titleKey",id},{"itemKey",id},
                {"title",id},{"syncable",false},{"utcOffsetMinutes",0}};
            auto start=fact;start.insert("atMs",kFixedNowMs-30000);start.insert("action","start");start.insert("origin","manual");
            start.insert("positionMs",0);start.insert("durationMs",60000);
            start.insert("entityRef",QVariantMap{{"identity","provider"},{"kind","recording"},{"providerId","local"},{"itemId",id+"-track"}});
            start.insert("releaseRef",QVariantMap{{"identity","provider"},{"kind","release"},{"providerId","local"},{"itemId",id}});
            QVERIFY(activity.recordMusicAction(start));fact.insert("startAtMs",kFixedNowMs-20000);fact.insert("endAtMs",kFixedNowMs-8000);fact.insert("activeMs",12000);fact.insert("rateMilli",1000);
            QVERIFY(activity.recordPlaybackDelta(fact));
        }
        expireHomeInputs(fixture.service);
        QTRY_COMPARE(sectionItems(fixture.service,"rediscover-library").size(),2);
        QVERIFY(sectionItems(fixture.service,"less-played").isEmpty());
    }
    void savedSongsUseTheirExistingLibraryRouteAndTypedListeningIdentity() {
        HomeFixture fixture({});ActivityStore activity;fixture.service.setActivityStore(&activity);
        const QVariantMap track{{"id","owned-track"},{"title","Owned track"},{"localPath","fixture.wav"}};
        fixture.service.setHomeCollections({{"songs",QVariantList{QVariantMap{{"id","saved-song-id"},{"title","Owned track"},
            {"artist","Owned artist"},{"row",QVariantMap{{"trackId","owned-track"},{"track",track}}}}}}});
        QVariantMap fact{{"sessionId","song-listen"},{"world","vinyl"},{"kind","music"},{"titleKey","album"},
            {"itemKey","track"},{"title","Track"},{"syncable",false},{"utcOffsetMinutes",0}};
        auto start=fact;start.insert("atMs",kFixedNowMs-30000);start.insert("action","start");start.insert("origin","manual");
        start.insert("positionMs",0);start.insert("durationMs",60000);
        start.insert("entityRef",QVariantMap{{"identity","provider"},{"kind","recording"},{"providerId","local"},{"itemId","owned-track"}});
        QVERIFY(activity.recordMusicAction(start));fact.insert("startAtMs",kFixedNowMs-20000);fact.insert("endAtMs",kFixedNowMs-8000);fact.insert("activeMs",12000);fact.insert("rateMilli",1000);
        QVERIFY(activity.recordPlaybackDelta(fact));
        expireHomeInputs(fixture.service);
        const auto items=sectionItems(fixture.service,"rediscover-library");QCOMPARE(items.size(),1);
        const auto card=items.first().toMap();QCOMPARE(card.value("kind").toString(),QString("saved-song"));
        QCOMPARE(card.value("ref").toMap().value("itemId").toString(),QString("owned-track"));
        QCOMPARE(card.value("destination").toMap().value("id").toString(),QString("saved-song-id"));
    }
    void pendingAndUnavailableCatalogueHaveActionableShelfState() {
        HomeFixture fixture({artistSeed(1)});
        auto section=sectionById(fixture.service.homeSections(),"from-your-artists");
        QCOMPARE(section.value("status").toString(),QString("loading"));
        QVERIFY(section.value("items").toList().isEmpty());
        const auto pending=fixture.source.pendingRequests("artistCandidates");QVERIFY(!pending.isEmpty());
        fixture.source.complete(pending.first(),{},"Offline fixture");QCoreApplication::processEvents();
        section=sectionById(fixture.service.homeSections(),"from-your-artists");
        QCOMPARE(section.value("status").toString(),QString("error"));QVERIFY(section.value("canRetry").toBool());
        QVERIFY(section.value("items").toList().isEmpty());
    }
    void exposureRestoreValidatesProfileWindowAndWholeEnvelope() {
        HomeFixture fixture({artistSeed(1)});pumpSuccessfulReplies(fixture.source);
        ActivityStore activity;fixture.service.setActivityStore(&activity);
        const auto ref=sectionItems(fixture.service,"from-your-artists").first().toMap().value("ref");
        QVERIFY(fixture.service.recordVisibleItems("from-your-artists",fixture.service.homeSnapshot().value("exposureSnapshotId").toString(),{ref},0));
        auto envelope=QJsonDocument::fromJson(fixture.preferences.exposure.toUtf8()).object();
        const auto record=envelope.value("records").toArray().first().toObject();
        auto restore=[&](const QJsonObject &value) {
            fixture.preferences.setVinylRecommendationExposureSnapshot(QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact)));
            expireHomeInputs(fixture.service);
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
        runHomeVisit(fixture.service);pumpSuccessfulReplies(fixture.source);
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
        expireHomeInputs(fixture.service);
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
        runHomeVisit(restored);pumpSuccessfulReplies(fixture.source);
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
        expireHomeInputs(fixture.service);
        QCOMPARE(fixture.service.homeSnapshot().value("qualifiedManualSessions").toInt(),1);
        QCOMPARE(sectionItems(fixture.service,"from-your-artists").first().toMap().value("score").toLongLong(),card.value("score").toLongLong()+45);
        QVERIFY(fixture.service.setFeedback(card.value("ref").toMap(),"like",0));
        QVERIFY(activity.clearAll());expireHomeInputs(fixture.service);
        QCOMPARE(fixture.service.homeSnapshot().value("qualifiedManualSessions").toInt(),0);
        QCOMPARE(sectionItems(fixture.service,"from-your-artists").first().toMap().value("score").toLongLong(),card.value("score").toLongLong()+1000);
        QVERIFY(!fixture.preferences.feedback.isEmpty());
    }
    void playedArtistCandidatesDoNotBlockCorePublication()
    {
        ActivityStore activity;
        const auto picked=artistSeed(1);
        const auto played=artistSeed(11).value("providerId").toString();
        HomeFixture fixture({picked},kFixedNowMs,false);
        fixture.source.cacheArtistSeed(picked);
        fixture.service.setActivityStore(&activity);
        recordFixturePlay(activity,11,kFixedNowMs-30000,12000);
        runHomeVisit(fixture.service);

        QCOMPARE(fixture.service.homeStatus(),QString("ready"));
        QVERIFY(fixture.source.callsFor("artistCandidates").isEmpty());
        QTRY_VERIFY(requestedArtists(fixture.source,"artistCandidates").contains(played));
        QVERIFY(!fixture.source.pendingRequests("artistCandidates").isEmpty());
        QCOMPARE(fixture.service.homeStatus(),QString("ready"));
        QVERIFY(sectionHasProviderId(fixture.service,"from-your-artists","MPRE_00000001"));
    }
    void listeningSeedsAlbumsAndDeepCutsWithoutPicks()
    {
        ActivityStore activity;
        HomeFixture fixture({},kFixedNowMs,false,false);
        auto &service=fixture.service;
        auto &source=fixture.source;
        source.manualListening=true;
        service.setActivityStore(&activity);
        QSet<QString> played;
        for(int index:{11,12,13}) {
            recordFixturePlay(activity,index,kFixedNowMs-index*30000,10000);
            played.insert(artistSeed(index).value("providerId").toString());
        }
        runHomeVisit(service);
        for(int pass=0;pass<24;++pass){
            QCoreApplication::processEvents(QEventLoop::AllEvents,20);
            const auto pending=source.pendingRequests("artistCandidates");
            if(pending.isEmpty())break;
            for(const auto &call:pending){
                auto reply=source.defaultReply(call);
                const auto seed=call.options.value("seed").toMap();
                const auto name=artistSeed(call.value.right(2).toInt()).value("name");
                auto credit=seed;credit["name"]=name;
                QVariantList albums;
                for(int n=0;n<4;++n){
                    const auto id=QString("MPRE_played_%1_%2").arg(call.value.right(2)).arg(n);
                    albums.append(albumCandidate(seed,id,"Album "+id,{{"artistRefs",QVariantList{credit}}}));
                }
                reply["items"]=albums;reply["artist"]=QVariantMap{{"name",name}};
                source.complete(call,reply);
            }
        }
        QVERIFY(service.selectedArtists().isEmpty());
        QCOMPARE(requestedArtists(source,"artistCandidates"),played); // Fails without organic seeds.
        const auto seedNameFor=[](const QString &providerId){
            for(int index:{11,12,13}) {
                const auto seed=artistSeed(index);
                if(seed.value("providerId").toString()==providerId)return seed.value("name").toString();
            }
            return QString();
        };
        const auto checkCard=[&](const QVariantMap &card){
            const auto credits=card.value("artistRefs").toList();
            QVERIFY(!credits.isEmpty());
            const auto seedName=seedNameFor(credits.first().toMap().value("providerId").toString());
            QVERIFY(!seedName.isEmpty());
            // The reason names the played artist by display name, never by provider id.
            QCOMPARE(card.value("reason").toString(),QString("Because you played %1").arg(seedName));
            for(const auto &value:credits) {
                const auto creditName=value.toMap().value("name").toString();
                QCOMPARE(creditName,seedName);
                QVERIFY(!creditName.startsWith("UC"));
            }
        };
        const auto albums=homeRow(service,"albums-for-you").value("items").toList();
        QVERIFY(albums.size()>=8);
        QSet<QString> credited;
        for(const auto &entry:albums){
            const auto card=entry.toMap();
            checkCard(card);
            credited.insert(card.value("artistRefs").toList().value(0).toMap().value("providerId").toString());
        }
        QCOMPARE(credited,played);
        for(const auto &call:source.callsFor("artistCandidates"))
            QCOMPARE(call.options.value("seed").toMap().value("name").toString(),seedNameFor(call.value));
        for(int pass=0;pass<40;++pass){
            QCoreApplication::processEvents(QEventLoop::AllEvents,20);
            const auto pending=source.pendingRequests();if(pending.isEmpty())break;
            for(const auto &call:pending){
                auto reply=source.defaultReply(call);
                if(call.operation=="album"){
                    const auto index=call.value.section('_',2,2).toInt();
                    const auto seed=artistSeed(index);
                    const QStringList ids{seed.value("id").toString()},names{seed.value("name").toString()};
                    const auto video=QString("DeepSong%1").arg(index,3,10,QLatin1Char('0'));
                    const QVariantMap track{{"id","ytm:track:"+video},{"videoId",video},{"title","Album track"},
                        {"seconds",170},{"artistIds",ids},{"artists",names},{"available",true}};
                    reply={{"id",call.value},{"browseId",call.value},{"title",call.value},{"origin","youtube-music"},
                        {"year",2004},{"type","Album"},{"tracks",QVariantList{track}},{"artistIds",ids},{"artists",names}};
                }else if(call.operation=="artistTopTracks"){
                    QCOMPARE(service.homeStatus(),QString("ready"));
                    reply["status"]="ready";reply["complete"]=true;
                    reply["provenance"]=QVariantMap{{"source","lastfm"},{"generator","artist-top-tracks"},{"method","artist.getTopTracks"}};
                    reply["items"]=QVariantList{QVariantMap{{"title","A different hit"},{"artist",call.options.value("seed").toMap().value("name")}}};
                }else{source.complete(call,{},"Optional fixture unavailable");continue;}
                source.complete(call,reply);
            }
        }
        QCOMPARE(requestedArtists(source,"artistTopTracks"),played);
        const auto deep=homeRow(service,"deep-cuts").value("items").toList();
        QCOMPARE(deep.size(),played.size());
        QSet<QString> deepArtists;
        for(const auto &entry:deep) {
            const auto card=entry.toMap();
            checkCard(card);
            deepArtists.insert(card.value("artistRefs").toList().value(0).toMap().value("providerId").toString());
        }
        QCOMPARE(deepArtists,played);

        const auto before=source.requests.size();
        service.setKeepHistory(false);
        QVERIFY(service.homeSourcePool().isEmpty());
        expireHomeInputs(service);pumpAllHomeReplies(source);
        QCOMPARE(source.requests.size(),before);
        QVERIFY(service.homeSourcePool().value("rows").toList().isEmpty());
    }
    void listeningSeedsUseRemainingCapacityAndRefreshOnlyOnMembershipChange()
    {
        ActivityStore activity;
        QVariantList picks;
        QSet<QString> explicitIds;
        for(int index:{1,2,3,4,5}){
            picks.append(artistSeed(index));explicitIds.insert(artistSeed(index).value("providerId").toString());
        }
        HomeFixture fixture(picks,kFixedNowMs,false);
        const auto id=[](int index){return artistSeed(index).value("providerId").toString();};
        const auto at=[](int seconds){return kFixedNowMs-seconds*1000LL;};
        fixture.service.setActivityStore(&activity);
        const auto legacyArtist=artistSeed(12);
        const QVariantMap legacyAlbum{{"artistIds",QStringList{"ytm:artist:"+id(12)}},
            {"artists",QStringList{legacyArtist.value("name").toString()}}};
        fixture.service.setHomeCollections({{"catalogueAlbums",QVariantList{legacyAlbum}}});
        recordFixturePlay(activity,11,at(180),12000);
        recordFixturePlay(activity,11,at(160),12000);
        recordFixturePlay(activity,12,at(140),12000,false,false); // Use the exact catalogue channel for an older fact.
        for(int seconds:{260,240,220})recordFixturePlay(activity,13,at(seconds)-75LL*86400000,12000);
        recordFixturePlay(activity,1,at(120),12000); // Already picked; keep its explicit slot.
        runHomeVisit(fixture.service);
        pumpAllHomeReplies(fixture.source);
        QCOMPARE(requestedArtists(fixture.source,"artistCandidates"),explicitIds+QSet<QString>{id(11)});
        const auto firstHash=fixture.service.homeSnapshot().value("seedHash");
        QVERIFY(!firstHash.toString().isEmpty());
        const auto before=fixture.source.callsFor("artistCandidates").size();
        recordFixturePlay(activity,11,at(100),12000);
        expireHomeInputs(fixture.service);pumpAllHomeReplies(fixture.source);
        QCOMPARE(fixture.service.homeSnapshot().value("seedHash"),firstHash);
        QCOMPARE(fixture.source.callsFor("artistCandidates").size(),before);
        for(int seconds:{80,60,40})recordFixturePlay(activity,12,at(seconds),12000,false,false);
        expireHomeInputs(fixture.service);pumpAllHomeReplies(fixture.source);
        QVERIFY(fixture.service.homeSnapshot().value("seedHash")!=firstHash);
        QCOMPARE(fixture.source.callsFor("artistCandidates").size(),before+1);
        const auto legacyCall=fixture.source.callsFor("artistCandidates").last();
        QCOMPARE(legacyCall.value,id(12));
        QCOMPARE(legacyCall.options.value("seed").toMap().value("name"),legacyArtist.value("name"));
        QVERIFY(!sectionHasProviderId(fixture.service,"from-your-artists","MPRE_00000011"));
        QVERIFY(sectionHasProviderId(fixture.service,"from-your-artists","MPRE_00000012"));
        QCOMPARE(fixture.service.selectedArtists().size(),picks.size());
        QCOMPARE(fixture.preferences.tasteWrites,0);
    }
    void emptyOrIneligibleHistoryDoesNotStartPersonalRequests()
    {
        ActivityStore activity;
        HomeFixture fixture({},kFixedNowMs,false,false);
        fixture.service.setActivityStore(&activity);
        runHomeVisit(fixture.service);
        pumpAllHomeReplies(fixture.source);
        QVERIFY(fixture.source.requests.isEmpty());
        QVERIFY(fixture.service.homeSourcePool().value("rows").toList().isEmpty());
        const auto ref=[](int index){return VinylRecommendationIdentity::fromCatalogue(artistSeed(index)).value("ref").toMap();};
        QVERIFY(fixture.service.setFeedback(ref(23),"hide",0));
        QVERIFY(fixture.service.setFeedback(ref(24),"exclude-artist",0));
        recordFixturePlay(activity,21,kFixedNowMs-160000,9999);
        recordFixturePlay(activity,22,kFixedNowMs-140000,12000,true); // Explicit skip even after 10 s.
        recordFixturePlay(activity,23,kFixedNowMs-120000,12000);
        recordFixturePlay(activity,24,kFixedNowMs-100000,12000);
        recordFixturePlay(activity,25,kFixedNowMs-80000,12000,false,false); // ID-only legacy fact with no catalogue name.
        expireHomeInputs(fixture.service);pumpAllHomeReplies(fixture.source);
        QVERIFY(fixture.source.requests.isEmpty());
        QVERIFY(fixture.service.homeSourcePool().value("rows").toList().isEmpty());
    }
    void becauseYouPlayedAndArtistsLikeYoursUseSimilarityWithoutKnownArtists()
    {
        ActivityStore activity;
        HomeFixture fixture({artistSeed(1),artistSeed(2),artistSeed(3),artistSeed(4)},kFixedNowMs,false);
        auto &service=fixture.service;
        auto &source=fixture.source;
        source.manualSimilarity=true;
        service.setActivityStore(&activity);
        recordFixturePlay(activity,20,kFixedNowMs-100000,12000); // Played but not picked.
        recordFixturePlay(activity,1,kFixedNowMs-70000,12000);   // Anchor.
        recordFixturePlay(activity,3,kFixedNowMs-40000,9000);   // Too short to anchor.
        runHomeVisit(service);
        for(int pass=0;pass<80;++pass) {
            QCoreApplication::processEvents(QEventLoop::AllEvents,20);
            const auto pending=source.pendingRequests();
            if(pending.isEmpty())break;
            for(const auto &call:pending) {
                auto reply=source.defaultReply(call);
                const auto seed=call.options.value("seed").toMap();
                const int index=call.value.right(2).toInt();
                if(call.operation=="artistCandidates") {
                    QVariantList items;
                    for(int n=0;n<(index<5?6:2);++n) {
                        const auto id=QString("MPRE_%1_%2").arg(index).arg(n);
                        items.append(albumCandidate(seed,id,"Album "+id));
                    }
                    if(index==1)for(int other:{2,20,21,22,23,24}) {
                        auto item=artistSeed(other);
                        item["title"]=item.value("name");item["relation"]="related-artist";
                        item["seedArtistRef"]=seed;item["artistRefs"]=QVariantList{artistSeed(other)};
                        items.append(item);
                    }
                    reply["items"]=items;
                } else if(call.operation=="recommendationIdentity"&&index<5) {
                    const auto mbid=fixtureArtistMbid(index);
                    reply["items"]=QVariantList();reply["provenance"]=QVariantMap{{"source","musicbrainz"}};
                    reply["complete"]=true;reply["status"]="verified";reply["mbid"]=mbid;
                    reply["artist"]=QVariantMap{{"id",mbid},{"name",seed.value("name")}};
                    reply["mapping"]=QVariantMap{{"status","verified"},{"kind","artist"},{"provider","youtube-music"},
                        {"providerId",call.value},{"mbid",mbid},{"method","external-id"},{"fetchedAtMs",kFixedNowMs}};
                } else if(call.operation=="similarArtists") {
                    const bool lastfm=call.options.value("source")=="lastfm";
                    QVariantList items;
                    const QList<int> artists=index==1?(lastfm?QList<int>{20,2,22,23,24,25,26,27,28,29,21}
                                                           :QList<int>{20,21})
                                                       :index==2?QList<int>{22}:QList<int>{};
                    int rank=0;for(int other:artists)items.append(similarityCard(other,++rank,lastfm));
                    reply["items"]=items;reply["complete"]=true;reply["status"]=items.isEmpty()?"empty":"ready";
                    auto provenance=QVariantMap{{"source",call.options.value("source")},{"generator","artist-similarity"}};
                    if(lastfm){provenance["method"]="artist.getSimilar";provenance["artistName"]=call.options.value("artistName");}
                    else provenance["algorithm"]="fixture";
                    reply["provenance"]=provenance;
                } else {source.complete(call,{},"Optional fixture unavailable");continue;}
                source.complete(call,reply);
            }
        }
        QVERIFY(source.pendingRequests().isEmpty());
        QTRY_VERIFY(!homeRow(service,"because-you-played").isEmpty());
        const auto row=homeRow(service,"because-you-played");
        QCOMPARE(row.value("title").toString(),QString("More like Fixture Artist 1"));
        const auto played=row.value("items").toList();
        QSet<QString> neighbors;
        for(const auto &entry:played) {
            const auto card=entry.toMap();
            QCOMPARE(card.value("kind").toString(),QString("albums"));
            const auto credit=card.value("artistRefs").toList().first().toMap();
            const auto name=credit.value("name").toString();
            QVERIFY(name!=artistSeed(20).value("name").toString()); // Negative control: known-artist gate.
            QVERIFY(name!=artistSeed(2).value("name").toString());
            QVERIFY(!neighbors.contains(name));neighbors.insert(name);
            QCOMPARE(card.value("reason").toString(),QString("Listeners of Fixture Artist 1 also play %1").arg(name));
        }
        QCOMPARE(played.size(),4);
        const auto discoveries=homeRow(service,"artists-like-yours").value("items").toList();
        QVERIFY(!discoveries.isEmpty());
        QStringList names;
        for(const auto &value:discoveries) {
            const auto card=value.toMap();names.append(card.value("title").toString());
            const auto reasons=card.value("reasons").toList();
            QVERIFY(std::any_of(reasons.cbegin(),reasons.cend(),[](const QVariant &entry){
                return entry.toMap().value("kind")=="calibrated_generator_score";
            }));
        }
        QVERIFY(!names.contains("Fixture Artist 20"));QVERIFY(!names.contains("Fixture Artist 2"));
        QVERIFY(names.contains("Fixture Artist 21")&&names.contains("Fixture Artist 23"));
        QVERIFY(names.indexOf("Fixture Artist 21")<names.indexOf("Fixture Artist 23")); // Two sources vs. one.
        QVERIFY(names.indexOf("Fixture Artist 22")<names.indexOf("Fixture Artist 21")); // Support across two seeds.
        const auto rows=service.homeSourcePool().value("rows").toList();
        QVERIFY(!homeRow(service,"albums-for-you").isEmpty());
        QSet<QString> keys;
        for(const auto &value:rows)for(const auto &entry:value.toMap().value("items").toList()) {
            const auto rowKey=value.toMap().value("key").toString();
            const auto card=entry.toMap();const auto key=card.value("key").toString();
            QVERIFY2(!key.isEmpty(),qPrintable(rowKey+": missing key for "+card.value("id").toString()));
            QVERIFY2(!keys.contains(key),qPrintable(rowKey+": repeated key "+key));
            keys.insert(key);
        }
        const auto ref=played.first().toMap().value("ref").toMap();
        const auto token=service.homeSnapshot().value("exposureSnapshotId").toString();
        QVERIFY(service.recordVisibleItems("because-you-played",token,{ref},0));
        QVERIFY(fixture.preferences.exposure.contains("because-you-played"));
        // Three surviving artists (even with spare albums) cannot fill the row.
        const auto credit=played.first().toMap().value("artistRefs").toList().first().toMap();
        const auto blocked=VinylRecommendationIdentity::fromCatalogue(credit).value("ref").toMap();
        QVERIFY(service.setFeedback(blocked,"exclude-artist",0));
        QVERIFY(homeRow(service,"because-you-played").isEmpty());
        QVERIFY(activity.clearAll());expireHomeInputs(service);
        QVERIFY(homeRow(service,"because-you-played").isEmpty());
    }
    void deepCutsRequireAgeAndArtistHitEvidence_data()
    {
        QTest::addColumn<bool>("lastfmAvailable");
        QTest::newRow("top-tracks-ready")<<true;
        QTest::newRow("top-tracks-failure")<<false;
    }
    void deepCutsRequireAgeAndArtistHitEvidence()
    {
        QFETCH(bool,lastfmAvailable);
        auto seed=artistSeed(1);seed["name"]="50 Cent";
        HomeFixture fixture({seed},kFixedNowMs,false);
        auto &service=fixture.service;
        auto &source=fixture.source;
        source.manualListening=true;
        runHomeVisit(service);
        for(int pass=0;pass<40;++pass) {
            QCoreApplication::processEvents(QEventLoop::AllEvents,20);
            const auto pending=source.pendingRequests();if(pending.isEmpty())break;
            for(const auto &call:pending) {
                auto reply=source.defaultReply(call);
                if(call.operation=="artistCandidates") {
                    const auto bound=call.options.value("seed").toMap();
                    reply["items"]=QVariantList{albumCandidate(bound,"MPRE_hit","Hit album"),
                        albumCandidate(bound,"MPRE_deep","Deep album"),albumCandidate(bound,"MPRE_recent","Recent album")};
                } else if(call.operation=="album") {
                    const QStringList ids{"ytm:artist:"+seed.value("providerId").toString()}, names{"50 Cent"};
                    const auto track=[&](const QString &id,const QString &title) {
                        return QVariantMap{{"id","ytm:track:"+id},{"videoId",id},{"title",title},{"seconds",170},
                            {"artistIds",ids},{"artists",names},{"available",true}};
                    };
                    const QVariantList tracks=call.value=="MPRE_hit"?QVariantList{track("HitUpload01","In Da Club (2004 Remastered) [Explicit] - Radio Edit")}
                        :call.value=="MPRE_deep"?QVariantList{track("DeepTrack01","Deep track one"),track("DeepTrack02","Deep track two")}
                        :QVariantList{track("RecentTrk01","Recent non-hit")};
                    reply={{"id",call.value},{"browseId",call.value},{"title",call.value},{"origin","youtube-music"},
                        {"year",call.value=="MPRE_recent"?2023:2004},{"type","Album"},{"tracks",tracks},
                        {"artistIds",ids},{"artists",names},{"cover","https://fixture.invalid/original.jpg"}};
                } else if(call.operation=="artistTopTracks") {
                    QCOMPARE(service.homeStatus(),QString("ready"));
                    QVERIFY(homeRow(service,"deep-cuts").isEmpty());
                    if(!lastfmAvailable){source.complete(call,{},"Optional fixture unavailable");continue;}
                    reply["status"]="ready";reply["complete"]=true;
                    reply["provenance"]=QVariantMap{{"source","lastfm"},{"generator","artist-top-tracks"},{"method","artist.getTopTracks"}};
                    // Last.fm supplies title/artist, not a matching YouTube video ID.
                    reply["items"]=QVariantList{QVariantMap{{"title","in da club!"},{"artist","50 Cent"}},
                        QVariantMap{{"title","Deep track one"},{"artist","Another artist"}},
                        QVariantMap{{"title","Deep track two"},{"artist","Another artist"}}};
                } else {source.complete(call,{},"Optional fixture unavailable");continue;}
                source.complete(call,reply);
            }
        }
        QTRY_COMPARE(source.callsFor("artistTopTracks").size(),1);
        QTRY_COMPARE(service.homeSourcePool().value("pendingCount").toInt(),0);
        QTRY_VERIFY(!homeRow(service,"quick-picks").isEmpty());
        const auto deep=homeRow(service,"deep-cuts").value("items").toList();
        if(!lastfmAvailable){QVERIFY(deep.isEmpty());return;}
        for(const auto &value:deep) {
            const auto card=value.toMap();
            QVERIFY(!card.value("title").toString().startsWith("In Da Club")); // Negative control: title gate.
            QVERIFY(card.value("title")!="Recent non-hit");
        }
        QCOMPARE(deep.size(),1);
        const auto card=deep.first().toMap();
        QVERIFY(card.value("title").toString().startsWith("Deep track"));
        QCOMPARE(card.value("record").toMap().value("id").toString(),QString("MPRE_deep"));
        QCOMPARE(card.value("cover").toString(),QString("https://fixture.invalid/original.jpg"));
        for(const auto &value:homeRow(service,"quick-picks").value("items").toList())
            QVERIFY(value.toMap().value("record").toMap().value("id")!="MPRE_deep");
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
        expireHomeInputs(fixture.service);
        QCOMPARE(fixture.service.homeSnapshot().value("seedMultiplier").toDouble(),0.75);
        fixture.service.setActivityStore(nullptr);for(int i=31;i<100;++i)record(i);fixture.service.setActivityStore(&activity);
        expireHomeInputs(fixture.service);
        QCOMPARE(fixture.service.homeSnapshot().value("seedMultiplier").toDouble(),0.75);
        fixture.service.setActivityStore(nullptr);record(100);fixture.service.setActivityStore(&activity);
        expireHomeInputs(fixture.service);
        QCOMPARE(fixture.service.homeSnapshot().value("qualifiedManualSessions").toInt(),101);
        QCOMPARE(fixture.service.homeSnapshot().value("seedMultiplier").toDouble(),0.5);
        fixture.service.setHomeCollections({{"artists",QVariantList{savedArtistRecord(artistSeed(1))}}});
        expireHomeInputs(fixture.service);
        pumpSuccessfulReplies(fixture.source);
        const auto reasons=sectionItems(fixture.service,"from-your-artists").first().toMap().value("reasons").toList();
        bool fullSavedAffinity=false;for(const auto &value:reasons)if(value.toMap().value("kind")=="seed_affinity")
            fullSavedAffinity=value.toMap().value("scoreDelta").toInt()==300;
        QVERIFY(fullSavedAffinity);
        fixture.service.setKeepHistory(false);expireHomeInputs(fixture.service);
        QCOMPARE(fixture.service.homeSnapshot().value("seedMultiplier").toDouble(),1.0);
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
    void failedCanonicalWorkBacksOffAcrossVisitsAndRefresh()
    {
        qint64 now=kFixedNowMs;
        FakeSource source;
        source.manualDiscography=true;
        FakePreferences prefs;
        VinylRecommendationService service(nullptr,[&now]{return now;});
        service.setSource(&source);
        service.setPreferences(&prefs);
        const auto failing=artistSeed(74), verified=artistSeed(75);
        const auto failedId=failing.value("providerId").toString(), verifiedId=verified.value("providerId").toString();
        const QString mbid="11111111-1111-4111-8111-111111111111";
        prefs.setTaste(withFiveDistinctTasteArtists({failing,verified}));
        auto drain=[&]{
            for(int pass=0;pass<40;++pass){
                QCoreApplication::processEvents();
                const auto pending=source.pendingRequests();
                if(pending.isEmpty())break;
                for(const auto &call:pending){
                    auto data=source.defaultReply(call,now);
                    if(call.operation=="artistCandidates")source.complete(call,data);
                    else if(call.operation=="recommendationIdentity"&&call.value==verifiedId){
                        data["items"]=QVariantList();
                        data["provenance"]=QVariantMap{{"source","musicbrainz"}};
                        data["complete"]=true;
                        data["status"]="verified";
                        data["mbid"]=mbid;
                        data["artist"]=QVariantMap{{"id",mbid},{"name","Fixture"}};
                        data["mapping"]=QVariantMap{{"status","verified"},{"kind","artist"},{"provider","youtube-music"},
                            {"providerId",verifiedId},{"mbid",mbid},{"method","external-id"},{"fetchedAtMs",now}};
                        source.complete(call,data);
                    }else source.complete(call,{},"Optional fixture unavailable");
                }
            }
            QVERIFY(source.pendingRequests().isEmpty());
        };
        auto attempts=[&](const QString &op,const QString &value){
            const auto calls=source.callsFor(op);
            return std::count_if(calls.cbegin(),calls.cend(),[&](const auto &call){return call.value==value;});
        };
        runHomeVisit(service);
        drain();
        QCOMPARE(attempts("recommendationIdentity",failedId),1);
        QCOMPARE(attempts("recommendationDiscography",mbid),1);
        const auto coreCalls=source.callsFor("artistCandidates").size();
        service.leaveHome();
        runHomeVisit(service);
        drain();
        QCOMPARE(source.callsFor("artistCandidates").size(),coreCalls);
        service.refreshHome(true);
        drain();
        QCOMPARE(attempts("recommendationIdentity",failedId),1);
        QCOMPARE(attempts("recommendationDiscography",mbid),1);
        now+=301000;
        service.refreshHome(true);
        drain();
        QCOMPARE(attempts("recommendationIdentity",failedId),2);
        QCOMPARE(attempts("recommendationDiscography",mbid),2);
    }
    void freshCoreIsReadyWhileOptionalMetadataIsStillPending()
    {
        HomeFixture fixture({artistSeed(74)});
        const auto call=fixture.source.pendingRequests("artistCandidates").first();
        fixture.source.complete(call,fixture.source.defaultReply(call));
        QTRY_VERIFY(!fixture.source.pendingRequests("recommendationIdentity").isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(fixture.service.homeStatus(),QString("ready"),1000);
        QVERIFY(fixture.service.homeError().isEmpty());
        QVERIFY(!fixture.source.cancelledLanes.contains("home-identity"));
    }
    void homeRetryReusesFreshChartsButRequestsMissingFeeds()
    {
        QFile file(QFINDTESTDATA("fixtures/vinyl-source-pool/transport-envelopes.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto examples=QJsonDocument::fromJson(file.readAll()).object().value("examples").toArray();
        QVERIFY(!examples.isEmpty());
        auto chart=examples.first().toObject().value("response").toObject().toVariantMap();
        chart["fetchedAt"]=QDateTime::fromMSecsSinceEpoch(kFixedNowMs-3600000,Qt::UTC).toString(Qt::ISODateWithMs);
        chart["_cached"]=true;
        HomeFixture fixture({},kFixedNowMs,false,false);
        fixture.source.runtimeFixtures=true;
        fixture.source.cachedCharts.insert("apple:us:albums",chart);
        fixture.service.refreshHome(true);
        QTRY_COMPARE_WITH_TIMEOUT(fixture.source.callsFor("chartCandidates").size(),21,10000);
        QTRY_VERIFY_WITH_TIMEOUT(fixture.source.pendingRequests().isEmpty(),10000);
        for(const auto &call:fixture.source.callsFor("chartCandidates")){
            QVERIFY(call.value!="apple:us:albums");
            QVERIFY(call.options.value("refresh").toBool());
        }
    }
    void fiveSeedRuntimeFixturesReachTheActualMergedHomePool()
    {
        const auto now=QDateTime::fromString("2026-11-13T12:00:00.000Z",Qt::ISODateWithMs).toMSecsSinceEpoch();
        const QStringList names{"Mariya Takeuchi",QString::fromUtf8("Björk"),"Ilaiyaraaja","Daft Punk","SZA"};
        QVariantList seeds;
        for (int index=0;index<5;++index) { auto seed=artistSeed(index+1);seed["name"]=names[index];seeds.append(seed); }
        HomeFixture fixture(seeds,now,false,true);
        fixture.source.runtimeFixtures=true;
        runHomeVisit(fixture.service);
        QTRY_VERIFY_WITH_TIMEOUT(!fixture.source.callsFor("chartCandidates").isEmpty(),5000);
        QTRY_VERIFY_WITH_TIMEOUT(!homeRow(fixture.service,"artists-like-yours").isEmpty()
            && fixture.service.property("homeSourcePool").toMap().value("pendingCount",-1).toInt()==0
            && fixture.source.pendingRequests().isEmpty(),90000);
        const auto pool=fixture.service.property("homeSourcePool").toMap();
        const auto crate=pool.value("crate").toList();
        QCOMPARE(fixture.service.selectedArtists().size(),5);
        QCOMPARE(crate.size(),16);
        QSet<QString> crateKeys,crateAlbums,represented;QMap<QString,int> crateKinds,artistCounts;QStringList crateOrder;
        for(const auto &value:crate){const auto card=value.toMap();const auto key=card.value("key").toString();
            QVERIFY(!key.isEmpty());QVERIFY(!crateKeys.contains(key));crateKeys.insert(key);
            const auto kind=card.value("kind").toString();++crateKinds[kind];crateOrder.append(kind);
            if(kind=="artists"){
                const auto seed=card.value("seedArtist").toMap();
                QCOMPARE(card.value("title"),seed.value("name"));
                QCOMPARE(card.value("id").toString(),"home-radio:"+seed.value("providerId").toString());
                QCOMPARE(card.value("key"),card.value("id"));
                QCOMPARE(card.value("artistId").toString(),"ytm:artist:"+seed.value("providerId").toString());
            }
            QSet<QString> credits;
            for(const auto &value:card.value("artistRefs").toList())credits.insert(value.toMap().value("providerId").toString());
            if(kind=="artists")credits.insert(card.value("seedArtist").toMap().value("providerId").toString());
            for(const auto &seed:seeds){const auto id=seed.toMap().value("providerId").toString();if(!credits.contains(id))continue;
                if(artistCounts.value(id)==3)QCOMPARE(represented.size(),5);
                QVERIFY(++artistCounts[id]<=4);represented.insert(id);
            }
            if(kind=="albums"){
                const auto id=card.value("providerId").toString();QVERIFY(!crateAlbums.contains(id));crateAlbums.insert(id);
            }
            if(crateOrder.size()>1){const auto prior=crate[crateOrder.size()-2].toMap();
                const auto song=kind=="songs"?card:prior,album=kind=="albums"?card:prior;
                if(song.value("kind")=="songs"&&album.value("kind")=="albums"){
                    const auto albumId=song.value("albumRef").toMap().value("providerId").toString();
                    QVERIFY(albumId.isEmpty()||albumId!=album.value("providerId").toString());
                }
            }
            QVERIFY(!card.value("pinned").toBool());QVERIFY(!card.value("unavailable").toBool());
            QVERIFY(card.value("status")!="loading" && card.value("status")!="error");
        }
        QCOMPARE(crateKinds.value("artists"),5);QCOMPARE(crateKinds.value("radio"),0);QCOMPARE(crateKinds.value("mixes"),2);
        QCOMPARE(crateOrder,(QStringList{"albums","artists","songs","albums","mixes","artists","albums","songs",
            "artists","albums","songs","artists","mixes","albums","artists","songs"}));
        QCOMPARE(crateKinds.value("albums"),5);QCOMPARE(crateKinds.value("songs"),4);
        QVERIFY(fixture.preferences.cratePins.isEmpty());QCOMPARE(fixture.preferences.cratePinWrites,0);
        QCOMPARE(fixture.preferences.tasteWrites,0);
        QCOMPARE(fixture.source.callsFor("chartCandidates").size(),22);
        QSet<QString> spellings;
        for (const auto &call:fixture.source.callsFor("similarArtists"))
            if (call.options.value("source")=="lastfm") spellings.insert(call.options.value("artistName").toString());
        QVERIFY(spellings.contains("Mariya Takeuchi"));
        QVERIFY(spellings.contains(QString::fromUtf8("竹内まりや")));
        for (const auto &call:fixture.source.callsFor("recommendationDiscography")) {
            QCOMPARE(call.options.value("since").toString(),QString("2025-11-13"));
            QCOMPARE(call.options.value("until").toString(),QString("2026-11-13"));
        }
        QVERIFY(fixture.source.callsFor("recommendationDiscography").size()>5);
        const auto rows=pool.value("rows").toList();
        for(const QString &key:{"mixes-radio","new-from-your-artists","albums-for-you","artists-like-yours","quick-picks"})
            QVERIFY2(!sectionById(rows,key).isEmpty(),qPrintable(key));
        for (const auto &entry:rows) {
            const auto row=entry.toMap();
            if(row.value("key")!="quick-picks")QVERIFY(row.value("items").toList().size()>=8);
            QVERIFY(row.value("key")!="rediscover" && row.value("key")!="because-you-played" && row.value("key")!="deep-cuts"); // Fixture album years are unknown.
            if (row.value("key")=="new-from-your-artists")
                for (const auto &item:row.value("items").toList())
                    QCOMPARE(item.toMap().value("dateSource").toString(),QString("musicbrainz"));
        }
        QVERIFY(pool.value("charts").toMap().value("albums").toList().size()>=5);
        QVERIFY(pool.value("charts").toMap().value("songs").toList().size()>=5);
        QVERIFY(pool.value("charts").toMap().value("artists").toList().size()>=5);
    }
    void initialCatalogueWaitStartsAfterSynchronousHomePublication()
    {
        bool projected=false;
        HomeFixture fixture(withFiveDistinctTasteArtists({artistSeed(71),artistSeed(72),artistSeed(73)}),kFixedNowMs,false);
        connect(&fixture.service,&VinylRecommendationService::homeChanged,&fixture.service,[&] {
            if(projected || fixture.service.homeStatus()!=QString("loading"))return;
            projected=true;
            QTest::qSleep(8500); // A synchronous QML observer, before provider dispatch.
        });
        runHomeVisit(fixture.service);
        QVERIFY(projected);
        QTest::qWait(50);
        QCOMPARE(fixture.source.pendingRequests("artistCandidates").size(),2);
        QVERIFY(fixture.service.homeError().isEmpty());
    }

    void queuedArtistsKeepTheirTurnWhileCoreRequestsMakeProgress()
    {
        HomeFixture fixture({artistSeed(71),artistSeed(72),artistSeed(73)});
        QTRY_COMPARE(fixture.source.pendingRequests("artistCandidates").size(),2);
        QTest::qWait(5000);
        const auto first=fixture.source.pendingRequests("artistCandidates").first();
        fixture.source.complete(first,fixture.source.defaultReply(first));
        QTRY_COMPARE(fixture.source.callsFor("artistCandidates").size(),3);
        QTest::qWait(4000);
        QCOMPARE(fixture.source.pendingRequests("artistCandidates").size(),2);
        QVERIFY(!fixture.source.cancelledLanes.contains("home-seed-0"));
        QVERIFY(!fixture.source.cancelledLanes.contains("home-seed-1"));
        pumpSuccessfulReplies(fixture.source);
        QTRY_COMPARE(fixture.service.homeStatus(),QString("ready"));
    }
    void stalledCoreStillStopsWithoutWaitingForEveryQueuedArtist()
    {
        HomeFixture fixture({artistSeed(71),artistSeed(72),artistSeed(73)});
        QTRY_COMPARE(fixture.source.pendingRequests("artistCandidates").size(),2);
        QTRY_VERIFY_WITH_TIMEOUT(fixture.source.pendingRequests("artistCandidates").isEmpty(),10000);
        QCOMPARE(fixture.source.callsFor("artistCandidates").size(),2);
        QVERIFY(!fixture.service.homeError().isEmpty());
    }
    void crateFillKeepsExistingAlternativesWithoutRepeatsOrArtistOverload()
    {
        QVariantList seeds;for(int i=1;i<=5;++i)seeds.append(artistSeed(i));HomeFixture fixture(seeds);
        auto song=[](const QVariantMap &seed,const QString &video,const QString &album){
            const QVariantList credits{seed};const QString title="Song "+video;
            const QVariantMap track{{"id","ytm:track:"+video},{"videoId",video},{"title",title},{"seconds",180},{"available",true},
                {"artistIds",QStringList{"ytm:artist:"+seed.value("providerId").toString()}},{"artists",QStringList{seed.value("name").toString()}}};
            return QVariantMap{{"kind","song"},{"provider","youtube-music"},{"providerId",video},{"id","ytm:track:"+video},
                {"title",title},{"seconds",180},{"artistRefs",credits},{"seedArtistRef",seed},{"relation","song-radio"},{"candidateSources",QStringList{"radio"}},
                {"albumRef",QVariantMap{{"kind","album"},{"provider","youtube-music"},{"providerId",album}}},
                {"record",QVariantMap{{"id","ytm:track:"+video},{"type","Song"},{"origin","youtube-music"},{"title",title},
                    {"artistIds",track.value("artistIds")},{"artists",track.value("artists")},{"tracks",QVariantList{track}}}}};
        };
        for(int pass=0;pass<128;++pass){QCoreApplication::processEvents(QEventLoop::AllEvents,20);
            const auto pending=fixture.source.pendingRequests();if(pending.isEmpty()&&pass>10)break;
            for(const auto &call:pending){const auto seed=call.options.value("seed").toMap();const auto suffix=seed.value("providerId").toString().right(2);
                if(call.operation=="artistCandidates"||call.operation=="songCandidates"){
                    auto reply=fixture.source.defaultReply(call);QVariantList items;
                    // Four supplied artists can fill 16 under the four-per-artist cap;
                    // three supplied artists plus two artist-only cards can supply at most 14.
                    if(call.operation=="artistCandidates"&&seed.value("providerId")==seeds[4].toMap().value("providerId")){reply["items"]=items;fixture.source.complete(call,reply);continue;}
                    if(call.operation=="artistCandidates")for(int i=0;i<6;++i){
                        const auto album=albumCandidate(seed,"MPRE_crate_"+suffix+"_"+QString::number(i),"Album "+suffix+QString::number(i));
                        items.append(album);items.append(album); // Duplicate evidence is not a second album tile.
                        auto top=song(seed,QString("T%1%2").arg(suffix).arg(i).leftJustified(11,'x'),album.value("providerId").toString());
                        top["relation"]="top-recording";top.remove("record");top.remove("candidateSources");items.append(top);
                    }else for(int i=0;i<6;++i)items.append(song(seed,QString("R%1%2").arg(suffix).arg(i).leftJustified(11,'y'),"MPRE_crate_"+suffix+"_"+QString::number(i)));
                    reply["items"]=items;fixture.source.complete(call,reply);
                }else fixture.source.complete(call,{},"Optional fixture unavailable");
            }
        }
        const auto crate=fixture.service.homeSourcePool().value("crate").toList();QCOMPARE(crate.size(),16);
        QCOMPARE(std::count_if(crate.cbegin(),crate.cend(),[](const QVariant &v){return v.toMap().value("kind")=="artists";}),5);
        QSet<QString> keys,albums,represented;QMap<QString,int> counts;
        for(int i=0;i<crate.size();++i){const auto card=crate[i].toMap();const auto key=card.value("key").toString();
            QVERIFY(!keys.contains(key));keys.insert(key);QSet<QString> credits;
            for(const auto &v:card.value("artistRefs").toList())credits.insert(v.toMap().value("providerId").toString());
            if(card.value("kind")=="artists")credits.insert(card.value("seedArtist").toMap().value("providerId").toString());
            for(const auto &id:credits){if(counts.value(id)==3)QCOMPARE(represented.size(),5);QVERIFY(++counts[id]<=4);represented.insert(id);}
            if(card.value("kind")=="albums"){const auto id=card.value("providerId").toString();QVERIFY(!albums.contains(id));albums.insert(id);}
            if(i>0){const auto prior=crate[i-1].toMap();const auto track=card.value("kind")=="songs"?card:prior,album=card.value("kind")=="albums"?card:prior;
                if(track.value("kind")=="songs"&&album.value("kind")=="albums")QVERIFY(track.value("albumRef").toMap().value("providerId")!=album.value("providerId"));}
            if(i>1)QVERIFY(card.value("kind")!=crate[i-1].toMap().value("kind")||card.value("kind")!=crate[i-2].toMap().value("kind"));
        }
        QVERIFY(fixture.preferences.cratePins.isEmpty());QCOMPARE(fixture.preferences.cratePinWrites,0);QCOMPARE(fixture.preferences.tasteWrites,0);
    }
    void crateUsesNextExistingAlbumsWhenOptionalSongAndMixLanesFail()
    {
        QVariantList seeds;for(int i=1;i<=5;++i)seeds.append(artistSeed(i));HomeFixture fixture(seeds);
        for(int pass=0;pass<96;++pass){QCoreApplication::processEvents(QEventLoop::AllEvents,20);
            const auto pending=fixture.source.pendingRequests();if(pending.isEmpty()&&pass>10)break;
            for(const auto &call:pending){
                if(call.operation=="artistCandidates"){
                    const auto seed=call.options.value("seed").toMap();auto reply=fixture.source.defaultReply(call);QVariantList items;
                    for(int i=0;i<6;++i)items.append(albumCandidate(seed,"MPRE_next_"+seed.value("providerId").toString()+QString::number(i),"Next album "+QString::number(i)));
                    reply["items"]=items;fixture.source.complete(call,reply);
                }else fixture.source.complete(call,{},"Optional fixture unavailable");
            }
        }
        const auto crate=fixture.service.homeSourcePool().value("crate").toList();QCOMPARE(crate.size(),16);
        for(int i=2;i<crate.size();++i)QVERIFY(crate[i].toMap().value("kind")!=crate[i-1].toMap().value("kind")||crate[i].toMap().value("kind")!=crate[i-2].toMap().value("kind"));
    }
    void cratePinPriorityPreservesAnUnavoidableThreeAlbumRun()
    {
        HomeFixture fixture({artistSeed(71),artistSeed(72),artistSeed(73)});
        pumpSuccessfulReplies(fixture.source);
        const auto automatic=fixture.service.homeSourcePool().value("crate").toList();
        QVariantList albums;
        for(const auto &entry:automatic)if(entry.toMap().value("kind")=="albums")albums.append(entry);
        QCOMPARE(albums.size(),3);
        const int generation=fixture.service.profileGeneration();
        for(const auto &entry:albums)QVERIFY(fixture.service.setCratePin(entry.toMap(),true,generation));
        const auto crate=fixture.service.homeSourcePool().value("crate").toList();
        QVERIFY(crate.size()>3);
        for(int index=0;index<3;++index){
            QCOMPARE(crate[index].toMap().value("key"),albums[index].toMap().value("key"));
            QCOMPARE(crate[index].toMap().value("kind").toString(),QString("albums"));
            QVERIFY(crate[index].toMap().value("pinned").toBool());
        }
        QVERIFY(crate[3].toMap().value("kind")!="albums");
        QCOMPARE(fixture.preferences.cratePins.size(),3);
    }
    void cratePinUsesTheCurrentPoolAndSurvivesMissingMetadata()
    {
        HomeFixture fixture({artistSeed(74)});
        const auto call=fixture.source.pendingRequests("artistCandidates").first();
        fixture.source.complete(call,fixture.source.defaultReply(call));
        QTRY_VERIFY(!sectionItems(fixture.service,"from-your-artists").isEmpty());
        const auto card=sectionItems(fixture.service,"from-your-artists").first().toMap();
        const auto automaticCrate=fixture.service.homeSourcePool().value("crate").toList();
        QVERIFY(std::any_of(automaticCrate.begin(),automaticCrate.end(),[&](const QVariant &value){return value.toMap().value("key")==card.value("key");}));
        const int generation=fixture.service.profileGeneration();
        QVERIFY(!fixture.service.setCratePin(card,true,generation-1));
        QVERIFY(fixture.service.setCratePin(card,true,generation));
        QVERIFY(fixture.service.isCratePinned(card,generation));
        QCOMPARE(fixture.preferences.cratePins.size(),1);
        QCOMPARE(fixture.preferences.cratePins.first().toMap().value("artist"),card.value("subtitle"));
        QVERIFY(!fixture.preferences.cratePins.first().toMap().contains("artistRefs"));
        auto crate=fixture.service.homeSourcePool().value("crate").toList();
        QCOMPARE(crate.size(),automaticCrate.size());
        QCOMPARE(crate.first().toMap().value("key"),card.value("key"));
        QVERIFY(crate.first().toMap().value("pinned").toBool());
        fixture.service.setSource(nullptr);
        QTRY_COMPARE(fixture.service.homeSourcePool().value("crate").toList().size(),1);
        crate=fixture.service.homeSourcePool().value("crate").toList();
        QCOMPARE(crate.size(),1);
        QVERIFY(crate.first().toMap().value("unavailable").toBool());
        QVERIFY(fixture.service.setCratePin(crate.first().toMap(),false,generation));
        QVERIFY(fixture.preferences.cratePins.isEmpty());
        QVERIFY(fixture.service.homeSourcePool().value("crate").toList().isEmpty());
        const QVariantMap song{{"id","owned-song"},{"kind","saved-song"},{"title","Known song"},
            {"artist","Known artist"},{"albumTitle","Known album"},
            {"row",QVariantMap{{"trackId","owned-track"},{"albumId","owned-album"},
                {"track",QVariantMap{{"id","owned-track"}}},{"edition",QVariantMap{{"origin","local"}}}}}};
        fixture.service.setHomeCollections({{"catalogueSongs",QVariantList{song}}});
        runHomeVisit(fixture.service);
        QVERIFY(fixture.service.setCratePin(song,true,generation));
        QCOMPARE(fixture.preferences.cratePins.first().toMap().value("artist").toString(),QString("Known artist"));
        fixture.service.setHomeCollections({});
        VinylRecommendationService restored;
        restored.setPreferences(&fixture.preferences);
        runHomeVisit(restored);
        QTRY_COMPARE(restored.homeSourcePool().value("crate").toList().size(),1);
        const auto unavailable=restored.homeSourcePool().value("crate").toList().first().toMap();
        QVERIFY(unavailable.value("unavailable").toBool());
        QCOMPARE(unavailable.value("artist").toString(),QString("Known artist"));
        QCOMPARE(unavailable.value("albumTitle").toString(),QString("Known album"));
        QVERIFY(!unavailable.contains("record"));QVERIFY(!unavailable.contains("artistRefs"));
    }
    void unansweredNativeScopePreparesChartsWithoutSavingTaste()
    {
        HomeFixture fixture({},QDateTime::fromString("2026-11-13T12:00:00.000Z",Qt::ISODateWithMs).toMSecsSinceEpoch(),false,false);
        QVERIFY(fixture.service.canEnterVinyl());
        QCOMPARE(fixture.service.minimumArtists(),1);
        QVERIFY(fixture.service.selectedArtists().isEmpty());
        fixture.source.runtimeFixtures=true;
        runHomeVisit(fixture.service);
        QCOMPARE(fixture.service.onboardingState(),QString("unanswered"));
        QTRY_VERIFY_WITH_TIMEOUT(fixture.service.homeSourcePool().value("charts").toMap().value("albums").toList().size()>=5,10000);
        QVERIFY(fixture.service.homeSourcePool().value("rows").toList().isEmpty());
        QVERIFY(fixture.source.callsFor("artistCandidates").isEmpty());
        QCOMPARE(fixture.preferences.tasteWrites,0);
        fixture.service.beginTasteEdit();
        QCOMPARE(fixture.service.onboardingState(),QString("editing"));
        fixture.service.cancelTasteEdit();
        QCOMPARE(fixture.service.onboardingState(),QString("unanswered"));
        QVERIFY(fixture.service.canEnterVinyl());
        QCOMPARE(fixture.preferences.tasteWrites,0);
    }
    void laterHomeVisitReevaluatesExpiryAndNewCutoff()
    {
        qint64 now=kFixedNowMs;FakeSource source;FakePreferences prefs;
        VinylRecommendationService service(nullptr,[&now]{return now;});service.setSource(&source);service.setPreferences(&prefs);
        prefs.setTaste(withFiveDistinctTasteArtists({artistSeed(70)}));runHomeVisit(service);
        const auto call=source.pendingRequests("artistCandidates").first();auto data=source.defaultReply(call);
        const auto seed=call.options.value("seed").toMap();const auto date=QDateTime::fromMSecsSinceEpoch(now,Qt::UTC).date().addMonths(-12).addDays(1);
        data["items"]=QVariantList{albumCandidate(seed,"MPRE_DATED","Dated",{{"releaseDate",date.toString(Qt::ISODate)}})};
        source.complete(call,data);QTRY_VERIFY(!sectionItems(service,"new-from-your-artists").isEmpty());
        for(const auto &optional:source.pendingRequests())source.complete(optional,{},"Optional fixture offline");
        QCoreApplication::processEvents();now+=2LL*86400000;runHomeVisit(service);
        QCOMPARE(service.homeSnapshot().value("evaluatedAtMs").toLongLong(),now);
        QVERIFY(sectionItems(service,"new-from-your-artists").isEmpty());
        QCOMPARE(source.callsFor("artistCandidates").size(),2);
    }
    void sourceReplacementRebuildsSeedsAndFencesOldReplies()
    {
        HomeFixture fixture({artistSeed(71)});const auto old=fixture.source.pendingRequests("artistCandidates").first();
        pumpSuccessfulReplies(fixture.source);QVERIFY(!sectionItems(fixture.service,"from-your-artists").isEmpty());
        FakeSource replacement;fixture.service.setSource(&replacement);runHomeVisit(fixture.service);
        QTRY_VERIFY(!replacement.pendingRequests("artistCandidates").isEmpty());
        const auto call=replacement.pendingRequests("artistCandidates").first();auto data=replacement.defaultReply(call);
        data["items"]=QVariantList{albumCandidate(call.options.value("seed").toMap(),"MPRE_NEW_SOURCE","New Source")};
        replacement.complete(call,data);QTRY_VERIFY(sectionHasProviderId(fixture.service,"from-your-artists","MPRE_NEW_SOURCE"));
        auto late=fixture.source.defaultReply(old);late["items"]=QVariantList{albumCandidate(old.options.value("seed").toMap(),"MPRE_OLD_SOURCE","Old")};
        fixture.source.completeLate(old,late);QCoreApplication::processEvents();
        QVERIFY(!sectionHasProviderId(fixture.service,"from-your-artists","MPRE_OLD_SOURCE"));
        QCOMPARE(selectedYoutubeMusicArtists(fixture.service),1);
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
        QCOMPARE(affinities,1);QCOMPARE(selectedYoutubeMusicArtists(fixture.service),2);
        QCOMPARE(card.value("destination").toMap().value("id").toString(),QString("ytm:album:MPRE_SHARED"));
    }
    void pickerCommitAutomaticallyDispatchesCandidates()
    {
        HomeFixture fixture({}, kFixedNowMs, false, false);
        runHomeVisit(fixture.service);
        fixture.service.beginTasteEdit();
        QCOMPARE(fixture.service.onboardingState(), QStringLiteral("editing"));
        QVERIFY(fixture.service.canEnterVinyl());
        QCOMPARE(fixture.service.minimumArtists(), 1);
        for (int i = 41; i < 46; ++i)
            fixture.service.toggleDraftArtist(artistSeed(i));
        QCOMPARE(fixture.service.draftArtistCount(), 5);
        QVERIFY(fixture.service.commitTaste(false));
        QCOMPARE(fixture.preferences.tasteWrites, 1);
        QVERIFY(fixture.service.canEnterVinyl());

        QTRY_VERIFY(!fixture.source.callsFor(QStringLiteral("artistCandidates")).isEmpty());
        pumpSuccessfulReplies(fixture.source);
        const QList<FakeSource::Request> calls = fixture.source.callsFor(QStringLiteral("artistCandidates"));
        QCOMPARE(calls.size(), 5);
        QSet<QString> dispatchedSeeds;
        for (const FakeSource::Request &call : calls)
            dispatchedSeeds.insert(call.options.value(QStringLiteral("seed")).toMap()
                                       .value(QStringLiteral("providerId")).toString());
        QCOMPARE(dispatchedSeeds.size(), 5);
    }

    void nativeScopeAccessIsIndependentOfSavedArtists()
    {
        HomeFixture empty({}, kFixedNowMs, false, true);
        QCOMPARE(empty.service.minimumArtists(),1);
        QVERIFY(empty.service.canEnterVinyl());
        QCOMPARE(empty.service.onboardingState(), QStringLiteral("answered"));

        QVariantList four;
        for (int i = 50; i < 54; ++i)
            four.append(artistSeed(i));
        four.append(artistSeed(50));
        HomeFixture returning(four, kFixedNowMs, false, true);
        QVERIFY(returning.service.canEnterVinyl());
        QCOMPARE(returning.service.selectedArtists().size(), 4);
        QCOMPARE(returning.service.onboardingState(), QStringLiteral("answered"));

        four.append(artistSeed(54));
        HomeFixture eligible(four, kFixedNowMs, false, true);
        QVERIFY(eligible.service.canEnterVinyl());
        QCOMPARE(eligible.service.selectedArtists().size(), 5);
        QCOMPARE(eligible.service.onboardingState(), QStringLiteral("answered"));
    }

    void draftCountingStillCollapsesVerifiedAliases()
    {
        const QString canonicalMbid = QStringLiteral("11111111-1111-4111-8111-111111111111");
        QVariantMap aliasA = artistSeed(60);
        aliasA.insert(QStringLiteral("mappingStatus"), QStringLiteral("verified"));
        aliasA.insert(QStringLiteral("canonicalMbid"), canonicalMbid);
        QVariantMap aliasB = artistSeed(61);
        aliasB.insert(QStringLiteral("mappingStatus"), QStringLiteral("verified"));
        aliasB.insert(QStringLiteral("canonicalMbid"), canonicalMbid.toUpper());
        QVariantList saved{aliasA, aliasB, artistSeed(62), artistSeed(63), artistSeed(64)};
        HomeFixture fixture(saved, kFixedNowMs, false, true);

        QCOMPARE(fixture.service.selectedArtists().size(), 5);
        QCOMPARE(fixture.service.draftArtistCount(), 4);
        QVERIFY(fixture.service.canEnterVinyl());
        QCOMPARE(fixture.service.onboardingState(), QStringLiteral("answered"));
        fixture.service.beginTasteEdit();
        QVERIFY(fixture.service.commitTaste(false));
        QCOMPARE(fixture.preferences.tasteWrites,1);
        QCOMPARE(fixture.service.draftArtistCount(),4);
    }

    void tastePickerSavesOneArtistAndKeepsCancelForEdits()
    {
        HomeFixture fixture({}, kFixedNowMs, false, false);
        runHomeVisit(fixture.service);
        fixture.service.beginTasteEdit();
        QCOMPARE(fixture.service.draftArtistCount(),0);
        QVERIFY(!fixture.service.commitTaste(false));
        QCOMPARE(fixture.preferences.tasteWrites,0);
        QVERIFY(fixture.service.commitTaste(true));
        QCOMPARE(fixture.preferences.tasteWrites, 0);
        QCOMPARE(fixture.service.onboardingState(),QString("unanswered"));
        QVERIFY(fixture.service.canEnterVinyl());
        fixture.service.beginTasteEdit();
        fixture.service.toggleDraftArtist(artistSeed(70));
        QCOMPARE(fixture.service.draftArtistCount(),1);
        QVERIFY(fixture.service.commitTaste(false));
        QCOMPARE(fixture.preferences.tasteWrites,1);
        QCOMPARE(fixture.service.selectedArtists().size(),1);
        QTRY_VERIFY(!fixture.source.callsFor("artistCandidates").isEmpty());
        QCOMPARE(fixture.source.callsFor("artistCandidates").first().options.value("seed").toMap().value("providerId"),
                 artistSeed(70).value("providerId"));
        fixture.service.beginTasteEdit();
        fixture.service.toggleDraftArtist(artistSeed(75));
        QVERIFY(fixture.service.commitTaste(true));
        QCOMPARE(fixture.preferences.tasteWrites,1);
        QCOMPARE(fixture.service.selectedArtists().size(),1);
        QVERIFY(fixture.service.canEnterVinyl());
    }

    void returningProfileCanSaveFewerArtists()
    {
        QVariantList legacy{artistSeed(80), artistSeed(81), artistSeed(82), artistSeed(83)};
        HomeFixture fixture(legacy, kFixedNowMs, false, true);

        QCOMPARE(fixture.service.onboardingState(), QStringLiteral("answered"));
        QVERIFY(fixture.service.canEnterVinyl());
        QCOMPARE(fixture.service.selectedArtists().size(), 4);
        QCOMPARE(fixture.service.draftArtists().size(), 4);
        fixture.service.beginTasteEdit();
        QCOMPARE(fixture.service.onboardingState(), QStringLiteral("editing"));
        fixture.service.toggleDraftArtist(artistSeed(80));
        QCOMPARE(fixture.service.draftArtistCount(), 3);
        fixture.service.cancelTasteEdit();
        QCOMPARE(fixture.service.onboardingState(), QStringLiteral("answered"));
        QCOMPARE(fixture.service.selectedArtists().size(), 4);
        QCOMPARE(fixture.service.draftArtistCount(), 4);
        QCOMPARE(fixture.preferences.tasteWrites,0);
        fixture.service.beginTasteEdit();
        fixture.service.toggleDraftArtist(artistSeed(80));
        QCOMPARE(fixture.service.draftArtistCount(),3);
        QVERIFY(fixture.service.commitTaste(false));
        QCOMPARE(fixture.preferences.tasteWrites,1);
        QCOMPARE(fixture.service.selectedArtists().size(),3);
        QVERIFY(fixture.service.canEnterVinyl());
    }

    void eligibleTasteEditCancelPreservesSavedArtists()
    {
        QVariantList five;
        for (int i = 90; i < 95; ++i)
            five.append(artistSeed(i));
        HomeFixture fixture(five, kFixedNowMs, false, true);

        QVERIFY(fixture.service.canEnterVinyl());
        fixture.service.beginTasteEdit();
        QCOMPARE(fixture.service.onboardingState(), QStringLiteral("editing"));
        fixture.service.toggleDraftArtist(artistSeed(90));
        fixture.service.toggleDraftArtist(artistSeed(95));
        QCOMPARE(fixture.service.draftArtistCount(), 5);
        fixture.service.cancelTasteEdit();
        QCOMPARE(fixture.service.onboardingState(), QStringLiteral("answered"));
        QCOMPARE(fixture.service.selectedArtists().size(), 5);
        QVERIFY(fixture.service.canEnterVinyl());
        QCOMPARE(fixture.service.draftArtistCount(), 5);
    }

    void blankTastePickerUsesSavedSuggestionsWithoutMarketCharts()
    {
        HomeFixture fixture({}, kFixedNowMs, false, false);
        fixture.service.setSuggestions({artistSeed(100)});
        runHomeVisit(fixture.service);
        fixture.service.beginTasteEdit();

        QCOMPARE(fixture.service.artistResults().value(QStringLiteral("status")).toString(), QStringLiteral("ready"));
        QCOMPARE(fixture.service.artistResults().value(QStringLiteral("items")).toList().size(), 1);
        QCOMPARE(fixture.source.requests.size(), 0);

        fixture.service.searchArtists(QStringLiteral("Nina Simone"));
        QTRY_COMPARE(fixture.source.callsFor(QStringLiteral("cataloguePage")).size(), 1);
        const auto request = fixture.source.callsFor(QStringLiteral("cataloguePage")).first();
        QVERIFY(!request.options.contains(QStringLiteral("region")));
    }

    void freshCachedCandidatesAppearWithoutCoreRequests()
    {
        QVariantList seeds;
        for (int i = 60; i < 68; ++i) {
            const QVariantMap seed = artistSeed(i);
            seeds.append(seed);
        }
        HomeFixture fixture(seeds, kFixedNowMs, false);
        for (const QVariant &value : seeds)
            fixture.source.cacheArtistSeed(value.toMap());

        runHomeVisit(fixture.service);

        QVERIFY(fixture.source.callsFor("artistCandidates").isEmpty());
        QVERIFY(!fixture.source.callsFor("recommendationIdentity").isEmpty());
        QCOMPARE(fixture.source.cachedQueries.size(), seeds.size());
        const QVariantList rows = sectionItems(fixture.service, QStringLiteral("from-your-artists"));
        QVERIFY(rows.size() > 6);
        for (const QVariant &value : seeds) {
            const QString expected = QStringLiteral("MPRE_")
                + value.toMap().value(QStringLiteral("providerId")).toString().right(8);
            QVERIFY(sectionHasProviderId(fixture.service, QStringLiteral("from-your-artists"), expected));
        }
    }

    void dailyCarouselSelectionPersistsAcrossRefreshAndRestart()
    {
        qint64 now = kFixedNowMs;
        FakeSource source;
        FakePreferences preferences;
        QVariantList seeds;
        for (int i = 120; i < 126; ++i)
            seeds.append(artistSeed(i));

        VinylRecommendationService service(nullptr, [&now] { return now; });
        service.setSource(&source);
        service.setPreferences(&preferences);
        preferences.setTaste(seeds, true);
        runHomeVisit(service);
        pumpAllHomeReplies(source);

        QVERIFY(sectionItems(service, QStringLiteral("from-your-artists")).size() > 5);
        QStringList initial = carouselKeys(service);
        QCOMPARE(initial.size(), 5);
        QSet<QString> uniqueInitial;
        for (const QString &key : initial)
            uniqueInitial.insert(key);
        QCOMPARE(uniqueInitial.size(), 5);
        auto snapshot = QJsonDocument::fromJson(preferences.selection.toUtf8()).object();
        const QJsonArray initialSavedKeys = snapshot.value(QStringLiteral("keys")).toArray();
        QCOMPARE(initialSavedKeys.size(), 5);
        QSet<QString> uniqueInitialSaved;
        for (const QJsonValue &value : initialSavedKeys)
            uniqueInitialSaved.insert(value.toString());
        QCOMPARE(uniqueInitialSaved.size(), 5);
        QCOMPARE(snapshot.value(QStringLiteral("refreshSerial")).toInt(), 0);

        runHomeVisit(service);
        pumpAllHomeReplies(source);
        QCOMPARE(carouselKeys(service), initial);

        service.refreshHomeSelection();
        const QStringList refreshed = carouselKeys(service);
        QCOMPARE(refreshed.size(), 5);
        QVERIFY(refreshed != initial);
        snapshot = QJsonDocument::fromJson(preferences.selection.toUtf8()).object();
        QCOMPARE(snapshot.value(QStringLiteral("refreshSerial")).toInt(), 1);
        const QJsonArray refreshedSavedKeys = snapshot.value(QStringLiteral("keys")).toArray();
        QCOMPARE(refreshedSavedKeys.size(), 5);
        QSet<QString> uniqueRefreshedSaved;
        for (const QJsonValue &value : refreshedSavedKeys)
            uniqueRefreshedSaved.insert(value.toString());
        QCOMPARE(uniqueRefreshedSaved.size(), 5);
        pumpAllHomeReplies(source);
        QCOMPARE(carouselKeys(service), refreshed);

        VinylRecommendationService restored(nullptr, [&now] { return now; });
        restored.setSource(&source);
        restored.setPreferences(&preferences);
        runHomeVisit(restored);
        pumpAllHomeReplies(source);
        QCOMPARE(carouselKeys(restored), refreshed);

        now += 24LL * 60 * 60 * 1000;
        restored.setHomeCollections({{QStringLiteral("songs"), QVariantList()}});
        expireHomeInputs(restored);
        const QStringList nextDay = carouselKeys(restored);
        QCOMPARE(nextDay.size(), 5);
        QVERIFY(nextDay != refreshed);
        bool includesUnseen = false;
        for (const QString &key : nextDay)
            if (!refreshed.contains(key))
                includesUnseen = true;
        QVERIFY(includesUnseen);
        snapshot = QJsonDocument::fromJson(preferences.selection.toUtf8()).object();
        QCOMPARE(snapshot.value(QStringLiteral("day")).toString(),
                 QDateTime::fromMSecsSinceEpoch(now).date().toString(Qt::ISODate));
        const QJsonArray nextDaySavedKeys = snapshot.value(QStringLiteral("keys")).toArray();
        QCOMPARE(nextDaySavedKeys.size(), 5);
        QSet<QString> uniqueNextDaySaved;
        for (const QJsonValue &value : nextDaySavedKeys)
            uniqueNextDaySaved.insert(value.toString());
        QCOMPARE(uniqueNextDaySaved.size(), 5);
    }

    void profileGenerationClearsHomeAndOldCollectionProjection()
    {
        HomeFixture fixture(withFiveDistinctTasteArtists({}), kFixedNowMs, false);
        const QVariantMap saved = artistSeed(50);
        fixture.source.cacheArtistSeed(saved);
        fixture.service.setHomeCollections({{QStringLiteral("artists"),
                                             QVariantList{savedArtistRecord(saved)}}});
        runHomeVisit(fixture.service);
        QVERIFY(sectionHasProviderId(fixture.service, QStringLiteral("from-your-artists"),
                                     QStringLiteral("MPRE_00000050")));
        QVERIFY(!fixture.service.homeSnapshot().isEmpty());

        fixture.preferences.replaceTasteForProfileSwitch({}, true);
        fixture.service.setProfileGeneration(fixture.service.profileGeneration() + 1);

        QVERIFY(fixture.service.homeSections().isEmpty());
        QVERIFY(fixture.service.homeCollections().isEmpty());
        QVERIFY(fixture.service.homeSnapshot().isEmpty());
        QVERIFY(fixture.service.selectedArtists().isEmpty());
        runHomeVisit(fixture.service);
        QVERIFY(fixture.service.homeSections().isEmpty());
        QVERIFY(fixture.source.pendingRequests().isEmpty());
    }

    void savedCollectionArtistRemainsAvailableAfterTasteReset()
    {
        const QVariantMap seed = artistSeed(51);
        const QVariantList taste{localTasteArtist(0)};
        HomeFixture fixture(taste, kFixedNowMs, false);
        fixture.source.cacheArtistSeed(seed);
        fixture.service.setHomeCollections({{QStringLiteral("artists"),
                                             QVariantList{savedArtistRecord(seed)}}});
        runHomeVisit(fixture.service);
        QVERIFY(sectionHasProviderId(fixture.service, QStringLiteral("from-your-artists"),
                                     QStringLiteral("MPRE_00000051")));

        fixture.preferences.setTaste({}, true);
        QVERIFY(fixture.service.canEnterVinyl());
        QVERIFY(fixture.service.selectedArtists().isEmpty());
        QTRY_VERIFY(sectionHasProviderId(fixture.service,"from-your-artists","MPRE_00000051"));

        fixture.preferences.setTaste(taste, true);
        QTRY_VERIFY(sectionHasProviderId(fixture.service, QStringLiteral("from-your-artists"),
                                         QStringLiteral("MPRE_00000051")));
        QCOMPARE(fixture.service.selectedArtists().size(), 1);
        QVERIFY(fixture.source.callsFor("artistCandidates").isEmpty());
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

        fixture.preferences.setTaste(withFiveDistinctTasteArtists({artistSeed(9)}), true);
        runHomeVisit(fixture.service);
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
                           {{QStringLiteral("releaseDate"), today.addMonths(-12).toString(Qt::ISODate)}}),
            albumCandidate(seed, QStringLiteral("MPRE_MID_YEAR"), QStringLiteral("Six Months Ago"),
                           {{QStringLiteral("releaseDate"), today.addMonths(-6).toString(Qt::ISODate)}}),
            albumCandidate(seed, QStringLiteral("MPRE_YEAR_ONLY"), QStringLiteral("Year Only"),
                           {{QStringLiteral("year"), QStringLiteral("2026")}}),
            albumCandidate(seed, QStringLiteral("MPRE_OLD"), QStringLiteral("Old Release"),
                           {{QStringLiteral("releaseDate"), today.addMonths(-12).addDays(-1).toString(Qt::ISODate)}}),
            albumCandidate(seed, QStringLiteral("MPRE_FUTURE"), QStringLiteral("Future Release"),
                           {{QStringLiteral("releaseDate"), today.addDays(1).toString(Qt::ISODate)}}),
            albumCandidate(seed, QStringLiteral("MPRE_MONTH_ONLY"), QStringLiteral("Uncertain Day"),
                           {{QStringLiteral("releaseDate"), today.toString(QStringLiteral("yyyy-MM"))}}),
            albumCandidate(seed, QStringLiteral("MPRE_BAD_DATE"), QStringLiteral("Invalid Date"),
                           {{QStringLiteral("releaseDate"), QStringLiteral("2026-19-42")}})};
        QVariantMap payload = fixture.source.defaultReply(call);
        payload.insert(QStringLiteral("items"), rows);
        fixture.source.complete(call, payload);

        QTRY_VERIFY(sectionHasProviderId(fixture.service, QStringLiteral("new-from-your-artists"),
                                         QStringLiteral("MPRE_RECENT")));
        const QVariantList recentItems = sectionItems(fixture.service, QStringLiteral("new-from-your-artists"));
        QVERIFY(!recentItems.isEmpty());
        QVERIFY(sectionHasProviderId(fixture.service, QStringLiteral("new-from-your-artists"),
                                    QStringLiteral("MPRE_MID_YEAR")));
        QVERIFY(!sectionHasProviderId(fixture.service, QStringLiteral("new-from-your-artists"),
                                     QStringLiteral("MPRE_YEAR_ONLY")));
        QVERIFY(!sectionHasProviderId(fixture.service, QStringLiteral("new-from-your-artists"),
                                     QStringLiteral("MPRE_OLD")));
        QVERIFY(!sectionHasProviderId(fixture.service, QStringLiteral("new-from-your-artists"),
                                     QStringLiteral("MPRE_BAD_DATE")));
        QVERIFY(!sectionHasProviderId(fixture.service, QStringLiteral("new-from-your-artists"),
                                     QStringLiteral("MPRE_FUTURE")));
        QVERIFY(!sectionHasProviderId(fixture.service, QStringLiteral("new-from-your-artists"),
                                     QStringLiteral("MPRE_MONTH_ONLY")));
    }
};

QTEST_GUILESS_MAIN(VinylRecommendationServiceTest)
#include "vinyl_recommendation_service_test.moc"
