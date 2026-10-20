#include "VinylBoundaryValidation.h"
#include "VinylListenEventPersistence.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <cmath>
#include <initializer_list>
#include <limits>

namespace vinyl {

namespace {

using ErrorCode = ListenPersistenceErrorCode;

template <typename T>
ListenPersistenceResult<T> malformed(QString field, QString detail)
{
    return ListenPersistenceResult<T>::failure(
        ErrorCode::MalformedSnapshot, std::move(field), std::move(detail));
}
QString firstUnknownKey(
    const QJsonObject &object,
    std::initializer_list<const char *> allowed)
{
    QSet<QString> keys;
    for (const char *key : allowed)
        keys.insert(QString::fromLatin1(key));
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!keys.contains(it.key()))
            return it.key();
    }
    return QString();
}

ListenPersistenceResult<qint64> nonNegativeInteger(
    const QJsonValue &value,
    const QString &field)
{
    if (!value.isDouble())
        return malformed<qint64>(
            field, QStringLiteral("Expected integer milliseconds."));
    const qint64 number = value.toInteger(-1);
    if (number < 0 || !std::isfinite(value.toDouble()) || std::floor(value.toDouble()) != value.toDouble()) {
        return malformed<qint64>(
            field, QStringLiteral("Expected finite non-negative integer."));
    }
    return ListenPersistenceResult<qint64>::success(
        number);
}
ListenPersistenceResult<std::optional<qint64>> nullableNonNegativeInteger(
    const QJsonValue &value,
    const QString &field)
{
    if (value.isNull())
        return ListenPersistenceResult<std::optional<qint64>>::success(
            std::nullopt);
    auto parsed = nonNegativeInteger(value, field);
    if (!parsed.ok())
        return ListenPersistenceResult<std::optional<qint64>>::failure(
            parsed.error.code, parsed.error.field, parsed.error.detail);
    return ListenPersistenceResult<std::optional<qint64>>::success(
        *parsed.value);
}

ListenPersistenceResult<quint64> positiveSequence(
    const QJsonValue &value,
    const QString &field)
{
    if (!value.isString())
        return malformed<quint64>(
            field, QStringLiteral("Sequence must be a decimal string."));
    bool ok = false;
    const quint64 sequence = value.toString().toULongLong(&ok);
    if (!ok || sequence == 0)
        return malformed<quint64>(
            field, QStringLiteral("Sequence must be positive."));
    return ListenPersistenceResult<quint64>::success(sequence);
}

QJsonValue sequenceToJson(quint64 sequence)
{
    return QString::number(sequence);
}
template <typename MbidType>
ListenPersistenceResult<MbidType> requiredMbid(
    const QJsonObject &object,
    const QString &key,
    const QString &field)
{
    const QJsonValue value = object.value(key);
    if (!value.isString())
        return malformed<MbidType>(
            field, QStringLiteral("Expected MusicBrainz ID string."));
    auto parsed = MbidType::parse(value.toString(), field);
    if (!parsed.ok())
        return malformed<MbidType>(
            field, QStringLiteral("Persisted MusicBrainz ID is invalid."));
    return ListenPersistenceResult<MbidType>::success(*parsed.value);
}

QJsonObject releaseContextToJson(const ReleaseTrackCoordinate &context)
{
    QJsonObject out;
    out.insert(
        QStringLiteral("releaseGroupMbid"),
        context.releaseGroupMbid().toString());
    out.insert(
        QStringLiteral("releaseMbid"),
        context.releaseMbid().toString());
    out.insert(
        QStringLiteral("releaseTrackMbid"),
        context.releaseTrackMbid().has_value()
            ? QJsonValue(context.releaseTrackMbid()->toString())
            : QJsonValue(QJsonValue::Null));
    out.insert(
        QStringLiteral("recordingMbid"),
        context.recordingMbid().toString());
    out.insert(
        QStringLiteral("mediumPosition"),
        context.mediumPosition());
    out.insert(
        QStringLiteral("trackPosition"),
        context.trackPosition());
    return out;
}

ListenPersistenceResult<int> positiveInt(
    const QJsonValue &value,
    const QString &field)
{
    if (!value.isDouble())
        return malformed<int>(
            field, QStringLiteral("Expected positive integer."));
    const double number = value.toDouble();
    if (!std::isfinite(number) || number <= 0.0
        || std::floor(number) != number
        || number > static_cast<double>(std::numeric_limits<int>::max())) {
        return malformed<int>(
            field, QStringLiteral("Expected positive integer."));
    }
    return ListenPersistenceResult<int>::success(
        static_cast<int>(number));
}
ListenPersistenceResult<ReleaseTrackCoordinate> releaseContextFromJson(
    const QJsonObject &object,
    const QString &field)
{
    const QString unknown = firstUnknownKey(
        object,
        {"releaseGroupMbid", "releaseMbid", "releaseTrackMbid",
         "recordingMbid", "mediumPosition", "trackPosition"});
    if (!unknown.isEmpty())
        return malformed<ReleaseTrackCoordinate>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown release-context field."));

    auto group = requiredMbid<ReleaseGroupMbid>(
        object,
        QStringLiteral("releaseGroupMbid"),
        field + QStringLiteral(".releaseGroupMbid"));
    auto release = requiredMbid<ReleaseMbid>(
        object,
        QStringLiteral("releaseMbid"),
        field + QStringLiteral(".releaseMbid"));
    auto recording = requiredMbid<RecordingMbid>(
        object,
        QStringLiteral("recordingMbid"),
        field + QStringLiteral(".recordingMbid"));
    if (!group.ok() || !release.ok() || !recording.ok())
        return malformed<ReleaseTrackCoordinate>(
            field, QStringLiteral("Invalid release context identity."));
    std::optional<ReleaseTrackMbid> releaseTrack;
    const QJsonValue releaseTrackValue =
        object.value(QStringLiteral("releaseTrackMbid"));
    if (!releaseTrackValue.isNull()) {
        if (!releaseTrackValue.isString())
            return malformed<ReleaseTrackCoordinate>(
                field + QStringLiteral(".releaseTrackMbid"),
                QStringLiteral("Expected MBID string or null."));
        auto parsed = ReleaseTrackMbid::parse(
            releaseTrackValue.toString(),
            field + QStringLiteral(".releaseTrackMbid"));
        if (!parsed.ok())
            return malformed<ReleaseTrackCoordinate>(
                field + QStringLiteral(".releaseTrackMbid"),
                QStringLiteral("Invalid Release Track MBID."));
        releaseTrack = *parsed.value;
    }

    auto medium = positiveInt(
        object.value(QStringLiteral("mediumPosition")),
        field + QStringLiteral(".mediumPosition"));
    auto track = positiveInt(
        object.value(QStringLiteral("trackPosition")),
        field + QStringLiteral(".trackPosition"));
    if (!medium.ok() || !track.ok())
        return malformed<ReleaseTrackCoordinate>(
            field, QStringLiteral("Invalid release coordinates."));
    auto created = ReleaseTrackCoordinate::create(
        *group.value,
        *release.value,
        std::move(releaseTrack),
        *recording.value,
        *medium.value,
        *track.value);
    if (!created.ok())
        return malformed<ReleaseTrackCoordinate>(
            field, QStringLiteral("Release context violates BA0 invariants."));
    return ListenPersistenceResult<ReleaseTrackCoordinate>::success(
        *created.value);
}

QJsonObject canonicalToJson(const CanonicalEntityRef &canonical)
{
    QJsonObject out;
    out.insert(
        QStringLiteral("kind"),
        canonicalEntityKindName(canonical.kind()));
    out.insert(
        QStringLiteral("mbid"),
        canonical.mbidString());
    return out;
}
ListenPersistenceResult<CanonicalEntityRef> canonicalFromJson(
    const QJsonObject &object,
    const QString &field)
{
    const QString unknown = firstUnknownKey(object, {"kind", "mbid"});
    if (!unknown.isEmpty())
        return malformed<CanonicalEntityRef>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown canonical identity field."));

    const QJsonValue kindValue = object.value(QStringLiteral("kind"));
    const QJsonValue mbidValue = object.value(QStringLiteral("mbid"));
    if (!kindValue.isString() || !mbidValue.isString())
        return malformed<CanonicalEntityRef>(
            field, QStringLiteral("Canonical identity requires kind and MBID."));
    const QString kind = kindValue.toString();
    const QString mbid = mbidValue.toString();

#define VINYL_LISTEN_PARSE_CANONICAL(name, type)     if (kind == QStringLiteral(name)) {         auto id = type::parse(mbid, field + QStringLiteral(".mbid"));         if (id.ok())             return ListenPersistenceResult<CanonicalEntityRef>::success(                 CanonicalEntityRef::from(*id.value));     }
    VINYL_LISTEN_PARSE_CANONICAL("artist", ArtistMbid)
    VINYL_LISTEN_PARSE_CANONICAL("release_group", ReleaseGroupMbid)
    VINYL_LISTEN_PARSE_CANONICAL("release", ReleaseMbid)
    VINYL_LISTEN_PARSE_CANONICAL("release_track", ReleaseTrackMbid)
    VINYL_LISTEN_PARSE_CANONICAL("recording", RecordingMbid)
    VINYL_LISTEN_PARSE_CANONICAL("work", WorkMbid)
#undef VINYL_LISTEN_PARSE_CANONICAL

    return malformed<CanonicalEntityRef>(
        field, QStringLiteral("Unsupported or invalid canonical identity."));
}

std::optional<EnqueueOriginKind> enqueueOriginKindFromName(
    const QString &name)
{
    if (name == QStringLiteral("user"))
        return EnqueueOriginKind::User;
    if (name == QStringLiteral("album"))
        return EnqueueOriginKind::Album;
    if (name == QStringLiteral("playlist"))
        return EnqueueOriginKind::Playlist;
    if (name == QStringLiteral("radio"))
        return EnqueueOriginKind::Radio;
    if (name == QStringLiteral("restore"))
        return EnqueueOriginKind::Restore;
    return std::nullopt;
}
QJsonValue enqueueContainerToJson(
    const std::optional<EnqueueContainerRef> &container)
{
    if (!container.has_value())
        return QJsonValue::Null;

    QJsonObject out;
    out.insert(
        QStringLiteral("kind"),
        enqueueContainerKindName(container->kind()));
    if (container->kind() == EnqueueContainerKind::CanonicalEntity) {
        out.insert(
            QStringLiteral("canonical"),
            canonicalToJson(*container->canonicalEntity()));
        out.insert(
            QStringLiteral("playlistId"),
            QJsonValue::Null);
    } else {
        out.insert(
            QStringLiteral("canonical"),
            QJsonValue::Null);
        out.insert(
            QStringLiteral("playlistId"),
            container->playlistId());
    }
    return out;
}
ListenPersistenceResult<std::optional<EnqueueContainerRef>>
enqueueContainerFromJson(
    const QJsonValue &value,
    const QString &field)
{
    if (value.isNull()) {
        return ListenPersistenceResult<
            std::optional<EnqueueContainerRef>>::success(std::nullopt);
    }
    if (!value.isObject())
        return malformed<std::optional<EnqueueContainerRef>>(
            field, QStringLiteral("Container must be object or null."));

    const QJsonObject object = value.toObject();
    const QString unknown =
        firstUnknownKey(object, {"kind", "canonical", "playlistId"});
    if (!unknown.isEmpty())
        return malformed<std::optional<EnqueueContainerRef>>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown enqueue-container field."));

    const QJsonValue kindValue = object.value(QStringLiteral("kind"));
    if (!kindValue.isString())
        return malformed<std::optional<EnqueueContainerRef>>(
            field + QStringLiteral(".kind"),
            QStringLiteral("Container kind must be a string."));
    if (kindValue.toString() == QStringLiteral("canonical")) {
        const QJsonValue canonicalValue =
            object.value(QStringLiteral("canonical"));
        if (!canonicalValue.isObject()
            || !object.value(QStringLiteral("playlistId")).isNull()) {
            return malformed<std::optional<EnqueueContainerRef>>(
                field,
                QStringLiteral("Canonical container has invalid payload."));
        }
        auto canonical = canonicalFromJson(
            canonicalValue.toObject(),
            field + QStringLiteral(".canonical"));
        if (!canonical.ok())
            return ListenPersistenceResult<
                std::optional<EnqueueContainerRef>>::failure(
                    canonical.error.code,
                    canonical.error.field,
                    canonical.error.detail);
        return ListenPersistenceResult<
            std::optional<EnqueueContainerRef>>::success(
                EnqueueContainerRef::canonical(*canonical.value));
    }

    if (kindValue.toString() == QStringLiteral("playlist")) {
        const QJsonValue playlistValue =
            object.value(QStringLiteral("playlistId"));
        if (!playlistValue.isString()
            || !object.value(QStringLiteral("canonical")).isNull()) {
            return malformed<std::optional<EnqueueContainerRef>>(
                field,
                QStringLiteral("Playlist container has invalid payload."));
        }
        auto playlist =
            EnqueueContainerRef::playlist(playlistValue.toString());
        if (!playlist.ok())
            return malformed<std::optional<EnqueueContainerRef>>(
                field,
                QStringLiteral("Invalid playlist container ID."));
        return ListenPersistenceResult<
            std::optional<EnqueueContainerRef>>::success(*playlist.value);
    }

    return malformed<std::optional<EnqueueContainerRef>>(
        field, QStringLiteral("Unknown enqueue-container kind."));
}

QJsonValue enqueueOriginToJson(
    const std::optional<EnqueueOrigin> &origin)
{
    if (!origin.has_value())
        return QJsonValue::Null;
    QJsonObject out;
    out.insert(
        QStringLiteral("kind"),
        enqueueOriginKindName(origin->kind()));
    out.insert(
        QStringLiteral("batchId"),
        origin->batchId().value());
    out.insert(
        QStringLiteral("container"),
        enqueueContainerToJson(origin->container()));
    return out;
}

ListenPersistenceResult<std::optional<EnqueueOrigin>> enqueueOriginFromJson(
    const QJsonValue &value,
    const QString &field)
{
    if (value.isNull())
        return ListenPersistenceResult<
            std::optional<EnqueueOrigin>>::success(std::nullopt);
    if (!value.isObject())
        return malformed<std::optional<EnqueueOrigin>>(
            field, QStringLiteral("Enqueue origin must be object or null."));

    const QJsonObject object = value.toObject();
    const QString unknown =
        firstUnknownKey(object, {"kind", "batchId", "container"});
    if (!unknown.isEmpty())
        return malformed<std::optional<EnqueueOrigin>>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown enqueue-origin field."));

    const QJsonValue kindValue = object.value(QStringLiteral("kind"));
    const QJsonValue batchValue = object.value(QStringLiteral("batchId"));
    if (!kindValue.isString() || !batchValue.isString())
        return malformed<std::optional<EnqueueOrigin>>(
            field, QStringLiteral("Enqueue origin requires kind and batch ID."));
    const auto kind = enqueueOriginKindFromName(kindValue.toString());
    auto batch = EnqueueBatchId::parse(batchValue.toString());
    auto container = enqueueContainerFromJson(
        object.value(QStringLiteral("container")),
        field + QStringLiteral(".container"));
    if (!kind.has_value() || !batch.ok() || !container.ok())
        return malformed<std::optional<EnqueueOrigin>>(
            field, QStringLiteral("Invalid enqueue origin."));

    return ListenPersistenceResult<std::optional<EnqueueOrigin>>::success(
        EnqueueOrigin(*kind, *batch.value, *container.value));
}

QJsonObject displaySnapshotToJson(
    const catalogue::LastKnownDisplaySnapshot &snapshot)
{
    QJsonArray credits;
    for (const QString &credit : snapshot.artistCredit)
        credits.append(credit);

    QJsonObject out;
    out.insert(
        QStringLiteral("primaryText"),
        snapshot.primaryText);
    out.insert(
        QStringLiteral("artistCredit"),
        credits);
    return out;
}
ListenPersistenceResult<catalogue::LastKnownDisplaySnapshot>
displaySnapshotFromJson(
    const QJsonObject &object,
    const QString &field)
{
    const QString unknown =
        firstUnknownKey(object, {"primaryText", "artistCredit"});
    if (!unknown.isEmpty())
        return malformed<catalogue::LastKnownDisplaySnapshot>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown display-snapshot field."));

    const QJsonValue primaryValue =
        object.value(QStringLiteral("primaryText"));
    const QJsonValue creditsValue =
        object.value(QStringLiteral("artistCredit"));
    if (!primaryValue.isString() || !creditsValue.isArray()
        || creditsValue.toArray().size() > boundary::MaxArtistCredits)
        return malformed<catalogue::LastKnownDisplaySnapshot>(
            field,
            QStringLiteral("Display snapshot requires text and credit array."));

    QStringList credits;
    for (const QJsonValue &value : creditsValue.toArray()) {
        if (!value.isString())
            return malformed<catalogue::LastKnownDisplaySnapshot>(
                field + QStringLiteral(".artistCredit"),
                QStringLiteral("Artist credit entries must be strings."));
        credits.append(value.toString());
    }
    if (!boundary::display(primaryValue.toString(), credits))
        return malformed<catalogue::LastKnownDisplaySnapshot>(field, QStringLiteral("Display exceeds text budget."));
    return ListenPersistenceResult<
        catalogue::LastKnownDisplaySnapshot>::success(
            catalogue::LastKnownDisplaySnapshot{
                primaryValue.toString(), std::move(credits)});
}

QJsonValue nullableDisplaySnapshotToJson(
    const std::optional<catalogue::LastKnownDisplaySnapshot> &snapshot)
{
    if (!snapshot.has_value())
        return QJsonValue::Null;
    return displaySnapshotToJson(*snapshot);
}

ListenPersistenceResult<
    std::optional<catalogue::LastKnownDisplaySnapshot>>
nullableDisplaySnapshotFromJson(
    const QJsonValue &value,
    const QString &field)
{
    if (value.isNull()) {
        return ListenPersistenceResult<
            std::optional<catalogue::LastKnownDisplaySnapshot>>::success(
                std::nullopt);
    }
    if (!value.isObject())
        return malformed<
            std::optional<catalogue::LastKnownDisplaySnapshot>>(
                field,
                QStringLiteral("Display snapshot must be object or null."));
    auto parsed =
        displaySnapshotFromJson(value.toObject(), field);
    if (!parsed.ok())
        return ListenPersistenceResult<
            std::optional<catalogue::LastKnownDisplaySnapshot>>::failure(
                parsed.error.code,
                parsed.error.field,
                parsed.error.detail);
    return ListenPersistenceResult<
        std::optional<catalogue::LastKnownDisplaySnapshot>>::success(
            *parsed.value);
}

QJsonObject occurrenceToJson(const QueueOccurrence &occurrence)
{
    QJsonObject out;
    out.insert(
        QStringLiteral("occurrenceId"),
        occurrence.occurrenceId().value());
    out.insert(
        QStringLiteral("recordingMbid"),
        occurrence.recordingMbid().toString());
    out.insert(
        QStringLiteral("releaseContext"),
        occurrence.releaseContext().has_value()
            ? QJsonValue(releaseContextToJson(*occurrence.releaseContext()))
            : QJsonValue(QJsonValue::Null));
    out.insert(
        QStringLiteral("enqueueOrigin"),
        enqueueOriginToJson(occurrence.enqueueOrigin()));
    return out;
}
ListenPersistenceResult<QueueOccurrence> occurrenceFromJson(
    const QJsonObject &object,
    const QString &field)
{
    const QString unknown = firstUnknownKey(
        object,
        {"occurrenceId", "recordingMbid", "releaseContext", "enqueueOrigin"});
    if (!unknown.isEmpty())
        return malformed<QueueOccurrence>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown queue-occurrence field."));

    const QJsonValue occurrenceValue =
        object.value(QStringLiteral("occurrenceId"));
    if (!occurrenceValue.isString())
        return malformed<QueueOccurrence>(
            field + QStringLiteral(".occurrenceId"),
            QStringLiteral("Occurrence ID must be a string."));
    auto occurrenceId =
        QueueOccurrenceId::parse(occurrenceValue.toString());
    auto recording = requiredMbid<RecordingMbid>(
        object,
        QStringLiteral("recordingMbid"),
        field + QStringLiteral(".recordingMbid"));
    if (!occurrenceId.ok() || !recording.ok())
        return malformed<QueueOccurrence>(
            field, QStringLiteral("Invalid queue occurrence identity."));
    std::optional<ReleaseTrackCoordinate> releaseContext;
    const QJsonValue releaseValue =
        object.value(QStringLiteral("releaseContext"));
    if (!releaseValue.isNull()) {
        if (!releaseValue.isObject())
            return malformed<QueueOccurrence>(
                field + QStringLiteral(".releaseContext"),
                QStringLiteral("Release context must be object or null."));
        auto parsed = releaseContextFromJson(
            releaseValue.toObject(),
            field + QStringLiteral(".releaseContext"));
        if (!parsed.ok())
            return ListenPersistenceResult<QueueOccurrence>::failure(
                parsed.error.code, parsed.error.field, parsed.error.detail);
        releaseContext = *parsed.value;
    }

    auto origin = enqueueOriginFromJson(
        object.value(QStringLiteral("enqueueOrigin")),
        field + QStringLiteral(".enqueueOrigin"));
    if (!origin.ok())
        return ListenPersistenceResult<QueueOccurrence>::failure(
            origin.error.code, origin.error.field, origin.error.detail);
    auto occurrence = QueueOccurrence::create(
        *occurrenceId.value,
        *recording.value,
        std::move(releaseContext),
        *origin.value);
    if (!occurrence.ok())
        return malformed<QueueOccurrence>(
            field,
            QStringLiteral("Queue occurrence violates BA1 invariants."));
    return ListenPersistenceResult<QueueOccurrence>::success(
        *occurrence.value);
}

std::optional<ListenEventKind> listenEventKindFromName(
    const QString &name)
{
    if (name == QStringLiteral("start"))
        return ListenEventKind::Start;
    if (name == QStringLiteral("pause"))
        return ListenEventKind::Pause;
    if (name == QStringLiteral("resume"))
        return ListenEventKind::Resume;
    if (name == QStringLiteral("seek"))
        return ListenEventKind::Seek;
    if (name == QStringLiteral("skip"))
        return ListenEventKind::Skip;
    if (name == QStringLiteral("stop"))
        return ListenEventKind::Stop;
    if (name == QStringLiteral("failure"))
        return ListenEventKind::Failure;
    if (name == QStringLiteral("completion"))
        return ListenEventKind::Completion;
    if (name == QStringLiteral("source_change"))
        return ListenEventKind::SourceChange;
    if (name == QStringLiteral("listening_span"))
        return ListenEventKind::ListeningSpan;
    return std::nullopt;
}
QJsonValue sourceTraceToJson(
    const std::optional<ListenSourceTraceId> &source)
{
    return source.has_value()
        ? QJsonValue(source->value())
        : QJsonValue(QJsonValue::Null);
}

ListenPersistenceResult<std::optional<ListenSourceTraceId>>
sourceTraceFromJson(
    const QJsonValue &value,
    const QString &field)
{
    if (value.isNull())
        return ListenPersistenceResult<
            std::optional<ListenSourceTraceId>>::success(std::nullopt);
    if (!value.isString())
        return malformed<std::optional<ListenSourceTraceId>>(
            field, QStringLiteral("Source trace must be string or null."));
    auto parsed = ListenSourceTraceId::parse(value.toString());
    if (!parsed.ok())
        return malformed<std::optional<ListenSourceTraceId>>(
            field, QStringLiteral("Invalid source trace ID."));
    return ListenPersistenceResult<
        std::optional<ListenSourceTraceId>>::success(*parsed.value);
}

QJsonValue playInstanceToJson(
    const std::optional<PlayInstanceId> &playInstance)
{
    return playInstance.has_value()
        ? QJsonValue(playInstance->value())
        : QJsonValue(QJsonValue::Null);
}
ListenPersistenceResult<std::optional<PlayInstanceId>>
playInstanceFromJson(
    const QJsonValue &value,
    const QString &field)
{
    if (value.isNull())
        return ListenPersistenceResult<
            std::optional<PlayInstanceId>>::success(std::nullopt);
    if (!value.isString())
        return malformed<std::optional<PlayInstanceId>>(
            field, QStringLiteral("Play instance must be string or null."));
    auto parsed = PlayInstanceId::parse(value.toString());
    if (!parsed.ok())
        return malformed<std::optional<PlayInstanceId>>(
            field, QStringLiteral("Invalid play instance ID."));
    return ListenPersistenceResult<
        std::optional<PlayInstanceId>>::success(*parsed.value);
}

QJsonObject eventToJson(const ListenEvent &event)
{
    QJsonObject out;
    out.insert(QStringLiteral("kind"), listenEventKindName(event.kind()));
    out.insert(
        QStringLiteral("sessionId"),
        event.context().sessionId().value());
    out.insert(
        QStringLiteral("occurrence"),
        occurrenceToJson(event.context().occurrence()));
    out.insert(
        QStringLiteral("playInstanceId"),
        event.context().playInstanceId().value());
    out.insert(
        QStringLiteral("continuationOf"),
        playInstanceToJson(event.context().continuationOf()));
    out.insert(
        QStringLiteral("displaySnapshot"),
        nullableDisplaySnapshotToJson(event.context().displaySnapshot()));
    out.insert(
        QStringLiteral("sequence"),
        sequenceToJson(event.sequence()));
    out.insert(
        QStringLiteral("observedAtMs"),
        event.observedAtMs());
    out.insert(
        QStringLiteral("monotonicElapsedMs"),
        event.monotonicElapsedMs());
    out.insert(
        QStringLiteral("positionMs"),
        event.positionMs());
    out.insert(
        QStringLiteral("fromPositionMs"),
        event.fromPositionMs().has_value()
            ? QJsonValue(*event.fromPositionMs())
            : QJsonValue(QJsonValue::Null));
    out.insert(
        QStringLiteral("toPositionMs"),
        event.toPositionMs().has_value()
            ? QJsonValue(*event.toPositionMs())
            : QJsonValue(QJsonValue::Null));
    out.insert(
        QStringLiteral("activeMs"),
        event.activeMs().has_value()
            ? QJsonValue(*event.activeMs())
            : QJsonValue(QJsonValue::Null));
    out.insert(
        QStringLiteral("sourceBefore"),
        sourceTraceToJson(event.sourceBefore()));
    out.insert(
        QStringLiteral("sourceAfter"),
        sourceTraceToJson(event.sourceAfter()));
    return out;
}

ListenPersistenceResult<DurableListenEvent> eventFromJson(
    const QJsonObject &object,
    const QString &field)
{
    const QString unknown = firstUnknownKey(
        object,
        {"kind", "sessionId", "occurrence", "playInstanceId",
         "continuationOf", "displaySnapshot", "sequence",
         "observedAtMs", "monotonicElapsedMs", "positionMs",
         "fromPositionMs", "toPositionMs", "activeMs",
         "sourceBefore", "sourceAfter"});
    if (!unknown.isEmpty())
        return malformed<DurableListenEvent>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown listen-event field."));

    const QJsonValue kindValue = object.value(QStringLiteral("kind"));
    const QJsonValue sessionValue =
        object.value(QStringLiteral("sessionId"));
    const QJsonValue occurrenceValue =
        object.value(QStringLiteral("occurrence"));
    const QJsonValue playValue =
        object.value(QStringLiteral("playInstanceId"));
    if (!kindValue.isString() || !sessionValue.isString()
        || !occurrenceValue.isObject() || !playValue.isString()) {
        return malformed<DurableListenEvent>(
            field,
            QStringLiteral("Listen event has malformed identity fields."));
    }

    const auto kind = listenEventKindFromName(kindValue.toString());
    auto session = ListenSessionId::parse(sessionValue.toString());
    auto occurrence = occurrenceFromJson(
        occurrenceValue.toObject(),
        field + QStringLiteral(".occurrence"));
    auto play = PlayInstanceId::parse(playValue.toString());
    if (!kind.has_value() || !session.ok()
        || !occurrence.ok() || !play.ok()) {
        return malformed<DurableListenEvent>(
            field, QStringLiteral("Listen event identity is invalid."));
    }

    auto continuation = playInstanceFromJson(
        object.value(QStringLiteral("continuationOf")),
        field + QStringLiteral(".continuationOf"));
    auto display = nullableDisplaySnapshotFromJson(
        object.value(QStringLiteral("displaySnapshot")),
        field + QStringLiteral(".displaySnapshot"));
    auto sequence = positiveSequence(
        object.value(QStringLiteral("sequence")),
        field + QStringLiteral(".sequence"));
    auto observed = nonNegativeInteger(
        object.value(QStringLiteral("observedAtMs")),
        field + QStringLiteral(".observedAtMs"));
    auto monotonic = nonNegativeInteger(
        object.value(QStringLiteral("monotonicElapsedMs")),
        field + QStringLiteral(".monotonicElapsedMs"));
    auto position = nonNegativeInteger(
        object.value(QStringLiteral("positionMs")),
        field + QStringLiteral(".positionMs"));
    auto fromPosition = nullableNonNegativeInteger(
        object.value(QStringLiteral("fromPositionMs")),
        field + QStringLiteral(".fromPositionMs"));
    auto toPosition = nullableNonNegativeInteger(
        object.value(QStringLiteral("toPositionMs")),
        field + QStringLiteral(".toPositionMs"));
    auto active = nullableNonNegativeInteger(
        object.value(QStringLiteral("activeMs")),
        field + QStringLiteral(".activeMs"));
    auto sourceBefore = sourceTraceFromJson(
        object.value(QStringLiteral("sourceBefore")),
        field + QStringLiteral(".sourceBefore"));
    auto sourceAfter = sourceTraceFromJson(
        object.value(QStringLiteral("sourceAfter")),
        field + QStringLiteral(".sourceAfter"));

    if (!continuation.ok() || !display.ok() || !sequence.ok()
        || !observed.ok() || !monotonic.ok() || !position.ok()
        || !fromPosition.ok() || !toPosition.ok() || !active.ok()
        || !sourceBefore.ok() || !sourceAfter.ok()) {
        return malformed<DurableListenEvent>(
            field, QStringLiteral("Listen event payload is invalid."));
    }
    if (continuation.value->has_value()
        && **continuation.value == *play.value) {
        return malformed<DurableListenEvent>(
            field,
            QStringLiteral("A play instance cannot continue itself."));
    }
    if (active.value->has_value() && **active.value <= 0) {
        return malformed<DurableListenEvent>(
            field + QStringLiteral(".activeMs"),
            QStringLiteral("Qualified listening duration must be positive."));
    }
    if (*kind == ListenEventKind::ListeningSpan
        && (!active.value->has_value()
            || !fromPosition.value->has_value()
            || !toPosition.value->has_value())) {
        return malformed<DurableListenEvent>(
            field,
            QStringLiteral("Listening span requires duration and position bounds."));
    }
    if (*kind == ListenEventKind::Seek
        && (!fromPosition.value->has_value()
            || !toPosition.value->has_value())) {
        return malformed<DurableListenEvent>(
            field,
            QStringLiteral("Seek requires from/to positions."));
    }
    return ListenPersistenceResult<DurableListenEvent>::success(
        DurableListenEvent{
            *kind,
            *session.value,
            *occurrence.value,
            *play.value,
            *continuation.value,
            *display.value,
            *sequence.value,
            *observed.value,
            *monotonic.value,
            *position.value,
            *fromPosition.value,
            *toPosition.value,
            *active.value,
            *sourceBefore.value,
            *sourceAfter.value});
}

} // namespace

QString listenPersistenceErrorCodeName(ListenPersistenceErrorCode code)
{
    switch (code) {
    case ErrorCode::None:
        return QStringLiteral("none");
    case ErrorCode::EmptySnapshot:
        return QStringLiteral("empty_snapshot");
    case ErrorCode::MalformedSnapshot:
        return QStringLiteral("malformed_snapshot");
    case ErrorCode::UnsupportedSchema:
        return QStringLiteral("unsupported_schema");
    case ErrorCode::UnsupportedVersion:
        return QStringLiteral("unsupported_version");
    case ErrorCode::ProfileMismatch:
        return QStringLiteral("profile_mismatch");
    case ErrorCode::InvalidState:
        return QStringLiteral("invalid_state");
    }
    return QStringLiteral("unknown");
}

qint64 RestoredListenLedger::totalListeningMs() const noexcept
{
    qint64 total = 0;
    for (const DurableListenEvent &event : events) {
        if (event.kind == ListenEventKind::ListeningSpan
            && event.activeMs.has_value()) {
            total += *event.activeMs;
        }
    }
    return total;
}

QString VinylListenEventPersistence::schemaName()
{
    return QStringLiteral("colosseum.vinyl.listen-event-ledger");
}
ListenPersistenceResult<QByteArray> VinylListenEventPersistence::capture(
    const ListenEventLedger &ledger)
{
    if (ledger.size() > boundary::MaxRows)
        return ListenPersistenceResult<QByteArray>::failure(ErrorCode::InvalidState, QStringLiteral("events"));
    QJsonArray events;
    quint64 expectedSequence = 1;
    for (const ListenEvent &event : ledger.events()) {
        if (event.sequence() != expectedSequence) {
            return ListenPersistenceResult<QByteArray>::failure(
                ErrorCode::InvalidState,
                QStringLiteral("events.sequence"),
                QStringLiteral("Live ledger sequence must be contiguous."));
        }
        events.append(eventToJson(event));
        ++expectedSequence;
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), schemaName());
    root.insert(QStringLiteral("version"), SchemaVersion);
    root.insert(
        QStringLiteral("profileScopeId"),
        ledger.profileId().value());
    root.insert(QStringLiteral("events"), events);

    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (bytes.size() > boundary::MaxSnapshotBytes)
        return ListenPersistenceResult<QByteArray>::failure(ErrorCode::InvalidState, QStringLiteral("snapshot"));
    return ListenPersistenceResult<QByteArray>::success(bytes);
}
ListenPersistenceResult<RestoredListenLedger>
VinylListenEventPersistence::restore(
    const ProfileScopeId &expectedProfileId,
    const QByteArray &snapshot)
{
    if (snapshot.size() > boundary::MaxSnapshotBytes)
        return malformed<RestoredListenLedger>(QStringLiteral("snapshot"), QStringLiteral("Snapshot exceeds byte budget."));
    if (snapshot.trimmed().isEmpty()) {
        return ListenPersistenceResult<RestoredListenLedger>::failure(
            ErrorCode::EmptySnapshot, QStringLiteral("snapshot"));
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(snapshot, &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        return malformed<RestoredListenLedger>(
            QStringLiteral("snapshot"),
            QStringLiteral("Malformed JSON snapshot."));
    }

    const QJsonObject root = document.object();
    const QString unknown = firstUnknownKey(
        root, {"schema", "version", "profileScopeId", "events"});
    if (!unknown.isEmpty()) {
        return malformed<RestoredListenLedger>(
            unknown, QStringLiteral("Unknown root field."));
    }
    if (root.value(QStringLiteral("schema")).toString() != schemaName()) {
        return ListenPersistenceResult<RestoredListenLedger>::failure(
            ErrorCode::UnsupportedSchema, QStringLiteral("schema"));
    }

    const QJsonValue versionValue = root.value(QStringLiteral("version"));
    if (!versionValue.isDouble()
        || std::floor(versionValue.toDouble()) != versionValue.toDouble()) {
        return ListenPersistenceResult<RestoredListenLedger>::failure(
            ErrorCode::UnsupportedVersion, QStringLiteral("version"));
    }
    const int version = versionValue.toInt(-1);
    if (version < MinimumSupportedVersion || version > SchemaVersion) {
        return ListenPersistenceResult<RestoredListenLedger>::failure(
            ErrorCode::UnsupportedVersion, QStringLiteral("version"));
    }

    const QJsonValue profileValue =
        root.value(QStringLiteral("profileScopeId"));
    if (!profileValue.isString()) {
        return malformed<RestoredListenLedger>(
            QStringLiteral("profileScopeId"),
            QStringLiteral("Profile scope must be a string."));
    }
    auto profile = ProfileScopeId::parse(profileValue.toString());
    if (!profile.ok()) {
        return malformed<RestoredListenLedger>(
            QStringLiteral("profileScopeId"),
            QStringLiteral("Persisted profile scope is invalid."));
    }
    if (*profile.value != expectedProfileId) {
        return ListenPersistenceResult<RestoredListenLedger>::failure(
            ErrorCode::ProfileMismatch, QStringLiteral("profileScopeId"));
    }

    const QJsonValue eventsValue = root.value(QStringLiteral("events"));
    if (!eventsValue.isArray() || eventsValue.toArray().size() > boundary::MaxRows) {
        return malformed<RestoredListenLedger>(
            QStringLiteral("events"),
            QStringLiteral("Events must be an array."));
    }

    QVector<DurableListenEvent> restoredEvents;
    restoredEvents.reserve(eventsValue.toArray().size());
    QHash<QString, QString> playOccurrence;
    quint64 expectedSequence = 1;
    int index = 0;
    for (const QJsonValue &value : eventsValue.toArray()) {
        const QString field = QStringLiteral("events[")
            + QString::number(index) + QLatin1Char(']');
        if (!value.isObject()) {
            return malformed<RestoredListenLedger>(
                field, QStringLiteral("Event must be an object."));
        }
        auto event = eventFromJson(value.toObject(), field);
        if (!event.ok()) {
            return ListenPersistenceResult<RestoredListenLedger>::failure(
                event.error.code, event.error.field, event.error.detail);
        }
        if (event.value->sequence != expectedSequence) {
            return malformed<RestoredListenLedger>(
                field + QStringLiteral(".sequence"),
                QStringLiteral("Event sequence must be contiguous."));
        }

        const QString playId = event.value->playInstanceId.value();
        const QString occurrenceId =
            event.value->occurrence.occurrenceId().value();
        const auto existing = playOccurrence.constFind(playId);
        if (existing != playOccurrence.constEnd()
            && existing.value() != occurrenceId) {
            return malformed<RestoredListenLedger>(
                field + QStringLiteral(".playInstanceId"),
                QStringLiteral("PlayInstanceId cannot identify two queue occurrences."));
        }
        playOccurrence.insert(playId, occurrenceId);

        if (event.value->continuationOf.has_value()) {
            const auto prior = playOccurrence.constFind(
                event.value->continuationOf->value());
            if (prior == playOccurrence.constEnd()
                || prior.value() != occurrenceId) {
                return malformed<RestoredListenLedger>(
                    field + QStringLiteral(".continuationOf"),
                    QStringLiteral("Continuation must reference an earlier play instance for the same occurrence."));
            }
        }

        restoredEvents.append(*event.value);
        ++expectedSequence;
        ++index;
    }

    return ListenPersistenceResult<RestoredListenLedger>::success(
        RestoredListenLedger{
            expectedProfileId, std::move(restoredEvents)});
}

} // namespace vinyl
