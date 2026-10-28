#include "VinylListenEventPersistence.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace {

int g_checks = 0;

void require(bool condition, const char *message)
{
    ++g_checks;
    if (condition)
        return;
    qCritical().noquote() << "FAIL:" << message;
    std::exit(1);
}

template <typename T>
T mustDomain(vinyl::DomainResult<T> result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}
template <typename T>
T mustQueue(vinyl::QueueResult<T> result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustPlayback(vinyl::PlaybackSessionResult<T> result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustListen(vinyl::ListenLedgerResult<T> result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}

template <typename T>
T mustPersist(vinyl::ListenPersistenceResult<T> result, const char *message)
{
    require(result.ok(), message);
    return *result.value;
}
vinyl::ListenSessionId session(const QString &value)
{
    return mustListen(
        vinyl::ListenSessionId::parse(value),
        "listen session should parse");
}

vinyl::ListenSourceTraceId source(const QString &value)
{
    return mustListen(
        vinyl::ListenSourceTraceId::parse(value),
        "source trace should parse");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace vinyl;

    const ProfileScopeId profile = mustDomain(
        ProfileScopeId::parse(QStringLiteral("profile-a")),
        "profile should parse");
    const RecordingMbid recording = mustDomain(
        RecordingMbid::parse(
            QStringLiteral("11111111-1111-4111-8111-111111111111")),
        "recording should parse");
    const QueueOccurrenceId occurrenceId = mustQueue(
        QueueOccurrenceId::parse(QStringLiteral("occ-1")),
        "occurrence ID should parse");
    const EnqueueBatchId batchId = mustQueue(
        EnqueueBatchId::parse(QStringLiteral("batch-1")),
        "batch ID should parse");
    const QueueOccurrence occurrence = mustQueue(
        QueueOccurrence::create(
            occurrenceId,
            recording,
            std::nullopt,
            EnqueueOrigin(
                EnqueueOriginKind::User,
                batchId,
                std::nullopt)),
        "queue occurrence should construct");

    PlaybackSession playback;
    const PlaybackWorkToken token1 = mustPlayback(
        playback.beginOccurrence(occurrence),
        "first play should begin");
    const PlayInstanceId play1 = mustDomain(
        PlayInstanceId::parse(QStringLiteral("play-1")),
        "play 1 should parse");
    const catalogue::LastKnownDisplaySnapshot display{
        QStringLiteral("Known Song"),
        {QStringLiteral("Known Artist")}};

    const ListenContext context1 = mustListen(
        ListenContext::create(
            profile,
            session(QStringLiteral("listen-session")),
            occurrence,
            token1,
            play1,
            std::nullopt,
            display),
        "first listen context should construct");

    ListenEventLedger ledger(profile);
    ListenEventRecorder recorder(ledger);
    require(recorder.start(
                context1,
                100000,
                1000,
                0,
                1000,
                source(QStringLiteral("source-a"))).ok,
            "first play should start");
    require(recorder.sample(
                token1, 99000, 2000, 1000, 1000).ok,
            "backward wall clock with forward monotonic time should count");
    require(recorder.sample(
                token1, 98000, 3000, 2000, 1000).ok,
            "second backward wall-clock sample should count");

    const PlaybackWorkToken token2 = mustPlayback(
        playback.beginOccurrence(occurrence),
        "same occurrence retry should begin");
    const PlayInstanceId play2 = mustDomain(
        PlayInstanceId::parse(QStringLiteral("play-2")),
        "play 2 should parse");
    const ListenContext context2 = mustListen(
        ListenContext::create(
            profile,
            session(QStringLiteral("listen-session")),
            occurrence,
            token2,
            play2,
            play1,
            display),
        "continuation context should construct");
    require(recorder.start(
                context2,
                97000,
                4000,
                2000,
                1000,
                source(QStringLiteral("source-b"))).ok,
            "continuation should start as a new play instance");
    require(recorder.sample(
                token2, 96000, 5000, 3000, 1000).ok,
            "continuation sample should count");
    require(recorder.failure(
                token2, 95000, 6000, 4000, 1000).ok,
            "continuation playback failure should remain a factual terminal cause");

    PlaybackSession restartedPlayback;
    const PlaybackWorkToken restartedToken = mustPlayback(
        restartedPlayback.beginOccurrence(occurrence),
        "simulated restart should begin at generation one again");
    require(restartedToken.generation() == token1.generation(),
            "fixture must reproduce process-local generation collision");
    const PlayInstanceId play3 = mustDomain(
        PlayInstanceId::parse(QStringLiteral("play-after-restart")),
        "post-restart play ID should parse");
    const ListenContext context3 = mustListen(
        ListenContext::create(
            profile,
            session(QStringLiteral("listen-session-after-restart")),
            occurrence,
            restartedToken,
            play3),
        "post-restart listen context should construct");
    ListenEventRecorder restartedRecorder(ledger);
    require(restartedRecorder.start(
                context3, 120000, 100, 0, 1000).ok,
            "post-restart play should append despite repeated BA2 generation");
    require(restartedRecorder.stop(
                restartedToken, 121000, 1100, 1000, 1000).ok,
            "post-restart play should close");
    const QByteArray bytes = mustPersist(
        VinylListenEventPersistence::capture(ledger),
        "BA4 durable capture should succeed");
    require(!bytes.contains("generation"),
            "durable BA4 format must not persist BA2 generation");
    require(!bytes.contains("workToken"),
            "durable BA4 format must not persist PlaybackWorkToken");
    require(bytes.contains("\"kind\":\"failure\""),
            "BA4 v3 durable format must persist playback failure distinctly");

    const QueueOccurrence localOccurrence = mustQueue(
        QueueOccurrence::createLocal(
            mustQueue(
                QueueOccurrenceId::parse(QStringLiteral("local-occ-unmapped")),
                "local occurrence ID should parse"),
            QStringLiteral("vault:ba4-unmapped-track")),
        "unmapped host-local occurrence should construct");
    PlaybackSession localPlayback;
    const PlaybackWorkToken localToken = mustPlayback(
        localPlayback.beginOccurrence(localOccurrence),
        "unmapped local playback should begin");
    const ListenContext localContext = mustListen(
        ListenContext::create(
            profile,
            session(QStringLiteral("local-listen-session")),
            localOccurrence,
            localToken,
            mustDomain(
                PlayInstanceId::parse(QStringLiteral("local-play-instance")),
                "local play instance should parse")),
        "unmapped local listen context should construct");
    ListenEventLedger localLedger(profile);
    ListenEventRecorder localRecorder(localLedger);
    require(localRecorder.start(
                localContext, 100000, 1000, 0, 1000).ok,
            "unmapped local event should remain valid in the transient ledger");
    ListenPersistenceResult<QByteArray> localCapture;
    bool localCaptureThrew = false;
    try {
        localCapture = VinylListenEventPersistence::capture(localLedger);
    } catch (const std::bad_optional_access &) {
        localCaptureThrew = true;
    }
    require(!localCaptureThrew,
            "BA4 capture must return a typed error instead of throwing on an unmapped local occurrence");
    require(!localCapture.ok()
                && localCapture.error.code == ListenPersistenceErrorCode::InvalidState,
            "BA4 persistence must reject a host-local occurrence it cannot represent");

    const QueueOccurrence mappedLocalOccurrence = mustQueue(
        QueueOccurrence::createLocal(
            mustQueue(
                QueueOccurrenceId::parse(QStringLiteral("local-occ-mapped")),
                "mapped local occurrence ID should parse"),
            QStringLiteral("vault:ba4-mapped-track"),
            std::optional<RecordingMbid>{recording}),
        "verified mapped local occurrence should construct");
    PlaybackSession mappedLocalPlayback;
    const PlaybackWorkToken mappedLocalToken = mustPlayback(
        mappedLocalPlayback.beginOccurrence(mappedLocalOccurrence),
        "mapped local playback should begin");
    const ListenContext mappedLocalContext = mustListen(
        ListenContext::create(
            profile,
            session(QStringLiteral("mapped-local-listen-session")),
            mappedLocalOccurrence,
            mappedLocalToken,
            mustDomain(
                PlayInstanceId::parse(QStringLiteral("mapped-local-play-instance")),
                "mapped local play instance should parse")),
        "mapped local listen context should construct");
    ListenEventLedger mappedLocalLedger(profile);
    ListenEventRecorder mappedLocalRecorder(mappedLocalLedger);
    require(mappedLocalRecorder.start(
                mappedLocalContext, 100000, 1000, 0, 1000).ok,
            "mapped local event should remain valid in the transient ledger");
    const auto mappedLocalCapture =
        VinylListenEventPersistence::capture(mappedLocalLedger);
    require(!mappedLocalCapture.ok()
                && mappedLocalCapture.error.code
                    == ListenPersistenceErrorCode::InvalidState,
            "BA4 must reject mapped local identities rather than serialize them as canonical-only history");

    const RestoredListenLedger restored = mustPersist(
        VinylListenEventPersistence::restore(profile, bytes),
        "BA4 durable round-trip should restore");
    require(restored.totalListeningMs() == 5000,
            "qualified listening time must round-trip");
    require(restored.events.size() == ledger.events().size(),
            "all durable listen facts must round-trip");
    require(restored.events.constFirst().observedAtMs == 100000
                && restored.events.at(1).observedAtMs == 98000,
            "backward descriptive wall-clock times must survive round-trip");
    bool foundContinuation = false;
    bool foundRestartPlay = false;
    bool foundFailure = false;
    for (const DurableListenEvent &event : restored.events) {
        if (event.playInstanceId == play2
            && event.continuationOf == std::optional<PlayInstanceId>{play1}) {
            foundContinuation = true;
        }
        if (event.playInstanceId == play3)
            foundRestartPlay = true;
        if (event.kind == ListenEventKind::Failure)
            foundFailure = true;
    }
    require(foundContinuation,
            "same-occurrence source continuation link must round-trip");
    require(foundRestartPlay,
            "durable PlayInstanceId must distinguish post-restart play");
    require(foundFailure,
            "playback failure must round-trip as failure rather than stop");
    require(restored.events.constFirst().displaySnapshot.has_value()
                && restored.events.constFirst().displaySnapshot->primaryText
                    == QStringLiteral("Known Song"),
            "non-authoritative display snapshot must round-trip as display data");
    const QByteArray bytesAgain = mustPersist(
        VinylListenEventPersistence::capture(ledger),
        "second BA4 capture should succeed");
    require(bytes == bytesAgain,
            "same BA4 ledger must serialize to identical canonical bytes");

    QJsonObject historical = QJsonDocument::fromJson(bytes).object();
    QJsonArray historicalEvents = historical.value(QStringLiteral("events")).toArray();
    for (int i = 0; i < historicalEvents.size(); ++i) {
        QJsonObject event = historicalEvents.at(i).toObject();
        if (event.value(QStringLiteral("kind")).toString()
            == QStringLiteral("failure")) {
            event.insert(QStringLiteral("kind"), QStringLiteral("stop"));
            historicalEvents.replace(i, event);
        }
    }
    historical.insert(QStringLiteral("events"), historicalEvents);

    QJsonObject v2 = historical;
    v2.insert(QStringLiteral("version"), 2);
    const RestoredListenLedger migratedV2 = mustPersist(
        VinylListenEventPersistence::restore(
            profile,
            QJsonDocument(v2).toJson(QJsonDocument::Compact)),
        "supported historical BA4 v2 fixture should restore");
    require(migratedV2.events.size() == restored.events.size(),
            "BA4 v2 migration must preserve historical durable facts");

    QJsonObject v1 = historical;
    v1.insert(QStringLiteral("version"), 1);
    const RestoredListenLedger migrated = mustPersist(
        VinylListenEventPersistence::restore(
            profile,
            QJsonDocument(v1).toJson(QJsonDocument::Compact)),
        "supported BA4 v1 fixture should restore");
    require(migrated.events.size() == restored.events.size(),
            "BA4 v1 migration must preserve historical durable facts");

    const auto malformed = VinylListenEventPersistence::restore(
        profile, QByteArrayLiteral("{bad-json"));
    require(!malformed.ok()
                && malformed.error.code
                    == ListenPersistenceErrorCode::MalformedSnapshot,
            "malformed BA4 JSON must return typed corruption error");
    QJsonObject unknown = QJsonDocument::fromJson(bytes).object();
    unknown.insert(QStringLiteral("futureField"), 1);
    const auto unknownResult = VinylListenEventPersistence::restore(
        profile,
        QJsonDocument(unknown).toJson(QJsonDocument::Compact));
    require(!unknownResult.ok()
                && unknownResult.error.code
                    == ListenPersistenceErrorCode::MalformedSnapshot,
            "unknown BA4 fields must fail closed inside a declared version");

    QJsonObject future = QJsonDocument::fromJson(bytes).object();
    future.insert(
        QStringLiteral("version"),
        VinylListenEventPersistence::SchemaVersion + 1);
    const auto futureResult = VinylListenEventPersistence::restore(
        profile,
        QJsonDocument(future).toJson(QJsonDocument::Compact));
    require(!futureResult.ok()
                && futureResult.error.code
                    == ListenPersistenceErrorCode::UnsupportedVersion,
            "future BA4 versions must fail closed");


    const QString repairCase = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (repairCase.isEmpty() || repairCase == "integer-boundary") {
        auto root = QJsonDocument::fromJson(bytes).object(); auto rows = root.value("events").toArray();
        auto row = rows.at(0).toObject(); row.insert("observedAtMs", QJsonValue(std::numeric_limits<qint64>::max())); rows[0] = row; root.insert("events", rows);
        auto maxValue = VinylListenEventPersistence::restore(profile, QJsonDocument(root).toJson());
        require(maxValue.ok() && maxValue.value->events.at(0).observedAtMs == std::numeric_limits<qint64>::max(),
            "M0R exact listen timestamp must survive qint64 JSON boundary");
        root.insert("version", 4294967299.0);
        require(!VinylListenEventPersistence::restore(profile, QJsonDocument(root).toJson()).ok(), "M0R listen version must not wrap");
    }
    if (repairCase.isEmpty() || repairCase == "display-budget") {
        auto root = QJsonDocument::fromJson(bytes).object(); auto rows = root.value("events").toArray();
        auto row = rows.at(0).toObject(); row.insert("displaySnapshot", QJsonObject{{"primaryText", QString(4097, QLatin1Char('x'))},{"artistCredit", QJsonArray{}}});
        rows[0] = row; root.insert("events", rows);
        require(!VinylListenEventPersistence::restore(profile, QJsonDocument(root).toJson()).ok(), "M0R oversized listen display must fail");
    }
    if (repairCase.isEmpty() || repairCase == "container-budget") {
        require(!VinylListenEventPersistence::restore(profile, bytes + QByteArray(8*1024*1024, ' ')).ok(), "M0R oversized listen container must fail");
    }

    qInfo().noquote()
        << "VINYL_BA4_PERSISTENCE_OK checks=" << g_checks
        << "qt=" << qVersion();
    return 0;
}
