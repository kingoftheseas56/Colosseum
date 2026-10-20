#include "VinylBoundaryValidation.h"
#include "VinylSessionPersistence.h"

#include <QChar>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>
#include <QHash>

#include <cmath>
#include <initializer_list>
#include <limits>

namespace vinyl {

namespace {

using ErrorCode = SessionPersistenceErrorCode;

template <typename T>
SessionPersistenceResult<T> malformed(QString field, QString detail)
{
    return SessionPersistenceResult<T>::failure(
        ErrorCode::MalformedSnapshot, std::move(field), std::move(detail));
}

QString firstUnknownKey(const QJsonObject &object,
                        std::initializer_list<const char *> allowed)
{
    QSet<QString> allowedKeys;
    for (const char *key : allowed)
        allowedKeys.insert(QString::fromLatin1(key));

    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowedKeys.contains(it.key()))
            return it.key();
    }
    return QString();
}

bool isFiniteNonNegative(double value)
{
    return std::isfinite(value) && value >= 0.0;
}

bool sameReleaseContext(
    const std::optional<ReleaseTrackCoordinate> &left,
    const std::optional<ReleaseTrackCoordinate> &right)
{
    if (left.has_value() != right.has_value())
        return false;
    if (!left.has_value())
        return true;

    return left->releaseGroupMbid() == right->releaseGroupMbid()
        && left->releaseMbid() == right->releaseMbid()
        && left->releaseTrackMbid() == right->releaseTrackMbid()
        && left->recordingMbid() == right->recordingMbid()
        && left->mediumPosition() == right->mediumPosition()
        && left->trackPosition() == right->trackPosition();
}

bool sameOccurrence(const QueueOccurrence &left, const QueueOccurrence &right)
{
    return left.occurrenceId() == right.occurrenceId()
        && left.sameMusicIdentity(right)
        && sameReleaseContext(left.releaseContext(), right.releaseContext());
}

QJsonObject releaseContextToJson(const ReleaseTrackCoordinate &context)
{
    QJsonObject out;
    out.insert(QStringLiteral("releaseGroupMbid"),
               context.releaseGroupMbid().toString());
    out.insert(QStringLiteral("releaseMbid"),
               context.releaseMbid().toString());
    if (context.releaseTrackMbid().has_value()) {
        out.insert(QStringLiteral("releaseTrackMbid"),
                   context.releaseTrackMbid()->toString());
    } else {
        out.insert(QStringLiteral("releaseTrackMbid"), QJsonValue::Null);
    }
    out.insert(QStringLiteral("recordingMbid"),
               context.recordingMbid().toString());
    out.insert(QStringLiteral("mediumPosition"), context.mediumPosition());
    out.insert(QStringLiteral("trackPosition"), context.trackPosition());
    return out;
}

QJsonObject occurrenceToJson(const QueueOccurrence &occurrence)
{
    QJsonObject out;
    out.insert(QStringLiteral("occurrenceId"),
               occurrence.occurrenceId().value());
    out.insert(QStringLiteral("recordingMbid"),
               occurrence.recordingMbid().toString());
    if (occurrence.releaseContext().has_value()) {
        out.insert(QStringLiteral("releaseContext"),
                   releaseContextToJson(*occurrence.releaseContext()));
    } else {
        out.insert(QStringLiteral("releaseContext"), QJsonValue::Null);
    }
    return out;
}

QJsonObject canonicalContainerToJson(const EnqueueContainerRef &container)
{
    QJsonObject out;
    out.insert(QStringLiteral("kind"), enqueueContainerKindName(container.kind()));
    if (container.kind() == EnqueueContainerKind::CanonicalEntity) {
        const CanonicalEntityRef &canonical = *container.canonicalEntity();
        out.insert(QStringLiteral("entityKind"),
                   canonicalEntityKindName(canonical.kind()));
        out.insert(QStringLiteral("mbid"), canonical.mbidString());
    } else {
        out.insert(QStringLiteral("playlistId"), container.playlistId());
    }
    return out;
}

QJsonObject enqueueOriginToJson(const EnqueueOrigin &origin)
{
    QJsonObject out;
    out.insert(QStringLiteral("kind"), enqueueOriginKindName(origin.kind()));
    out.insert(QStringLiteral("batchId"), origin.batchId().value());
    if (origin.container().has_value())
        out.insert(QStringLiteral("container"),
                   canonicalContainerToJson(*origin.container()));
    else
        out.insert(QStringLiteral("container"), QJsonValue::Null);
    return out;
}

QJsonObject displaySnapshotToJson(
    const catalogue::LastKnownDisplaySnapshot &snapshot)
{
    QJsonObject out;
    out.insert(QStringLiteral("primaryText"), snapshot.primaryText);
    QJsonArray artistCredit;
    for (const QString &credit : snapshot.artistCredit)
        artistCredit.append(credit);
    out.insert(QStringLiteral("artistCredit"), artistCredit);
    return out;
}

QJsonObject occurrenceToJsonV2(
    const QueueOccurrence &occurrence,
    const catalogue::LastKnownDisplaySnapshot *displaySnapshot)
{
    QJsonObject out = occurrenceToJson(occurrence);
    if (occurrence.enqueueOrigin().has_value())
        out.insert(QStringLiteral("enqueueOrigin"),
                   enqueueOriginToJson(*occurrence.enqueueOrigin()));
    else
        out.insert(QStringLiteral("enqueueOrigin"), QJsonValue::Null);

    if (displaySnapshot)
        out.insert(QStringLiteral("displaySnapshot"),
                   displaySnapshotToJson(*displaySnapshot));
    else
        out.insert(QStringLiteral("displaySnapshot"), QJsonValue::Null);
    return out;
}

QJsonObject occurrenceToJsonV3(
    const QueueOccurrence &occurrence,
    const catalogue::LastKnownDisplaySnapshot *displaySnapshot)
{
    QJsonObject out;
    out.insert(QStringLiteral("occurrenceId"),
               occurrence.occurrenceId().value());
    if (occurrence.localIdentity().isEmpty()) {
        out.insert(QStringLiteral("identityKind"), QStringLiteral("canonical"));
        out.insert(QStringLiteral("recordingMbid"),
                   occurrence.recordingMbid().toString());
    } else {
        out.insert(QStringLiteral("identityKind"), QStringLiteral("local"));
        out.insert(QStringLiteral("localIdentity"), occurrence.localIdentity());
        if (occurrence.canonicalRecording().has_value()) {
            out.insert(QStringLiteral("verifiedRecordingMbid"),
                       occurrence.canonicalRecording()->toString());
        } else {
            out.insert(QStringLiteral("verifiedRecordingMbid"), QJsonValue::Null);
        }
    }

    if (occurrence.releaseContext().has_value()) {
        out.insert(QStringLiteral("releaseContext"),
                   releaseContextToJson(*occurrence.releaseContext()));
    } else {
        out.insert(QStringLiteral("releaseContext"), QJsonValue::Null);
    }
    if (occurrence.enqueueOrigin().has_value()) {
        out.insert(QStringLiteral("enqueueOrigin"),
                   enqueueOriginToJson(*occurrence.enqueueOrigin()));
    } else {
        out.insert(QStringLiteral("enqueueOrigin"), QJsonValue::Null);
    }
    if (displaySnapshot) {
        out.insert(QStringLiteral("displaySnapshot"),
                   displaySnapshotToJson(*displaySnapshot));
    } else {
        out.insert(QStringLiteral("displaySnapshot"), QJsonValue::Null);
    }
    return out;
}

SessionPersistenceResult<int> requiredPositiveInt(
    const QJsonObject &object,
    const QString &key,
    const QString &field)
{
    const QJsonValue value = object.value(key);
    if (!value.isDouble())
        return malformed<int>(field, QStringLiteral("Expected an integer."));

    const double number = value.toDouble();
    if (!std::isfinite(number)
        || number <= 0.0
        || std::floor(number) != number
        || number > static_cast<double>(std::numeric_limits<int>::max())) {
        return malformed<int>(
            field, QStringLiteral("Expected a positive one-based integer."));
    }

    return SessionPersistenceResult<int>::success(static_cast<int>(number));
}

template <typename MbidType>
SessionPersistenceResult<MbidType> requiredMbid(
    const QJsonObject &object,
    const QString &key,
    const QString &field)
{
    const QJsonValue value = object.value(key);
    if (!value.isString()) {
        return malformed<MbidType>(
            field, QStringLiteral("Expected a MusicBrainz ID string."));
    }

    const auto parsed = MbidType::parse(value.toString(), field);
    if (!parsed.ok()) {
        return malformed<MbidType>(
            field, QStringLiteral("Persisted MusicBrainz identity is invalid."));
    }
    return SessionPersistenceResult<MbidType>::success(*parsed.value);
}

SessionPersistenceResult<ReleaseTrackCoordinate> releaseContextFromJson(
    const QJsonObject &object,
    const QString &field)
{
    const QString unknown = firstUnknownKey(
        object,
        {"releaseGroupMbid", "releaseMbid", "releaseTrackMbid",
         "recordingMbid", "mediumPosition", "trackPosition"});
    if (!unknown.isEmpty()) {
        return malformed<ReleaseTrackCoordinate>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown release-context field in schema v1."));
    }

    auto releaseGroup = requiredMbid<ReleaseGroupMbid>(
        object, QStringLiteral("releaseGroupMbid"),
        field + QStringLiteral(".releaseGroupMbid"));
    if (!releaseGroup.ok())
        return SessionPersistenceResult<ReleaseTrackCoordinate>::failure(
            releaseGroup.error.code, releaseGroup.error.field,
            releaseGroup.error.detail);

    auto release = requiredMbid<ReleaseMbid>(
        object, QStringLiteral("releaseMbid"),
        field + QStringLiteral(".releaseMbid"));
    if (!release.ok())
        return SessionPersistenceResult<ReleaseTrackCoordinate>::failure(
            release.error.code, release.error.field, release.error.detail);

    std::optional<ReleaseTrackMbid> releaseTrack;
    const QJsonValue releaseTrackValue =
        object.value(QStringLiteral("releaseTrackMbid"));
    if (!releaseTrackValue.isNull()) {
        if (!releaseTrackValue.isString()) {
            return malformed<ReleaseTrackCoordinate>(
                field + QStringLiteral(".releaseTrackMbid"),
                QStringLiteral("Expected a MusicBrainz ID string or null."));
        }
        const auto parsed = ReleaseTrackMbid::parse(
            releaseTrackValue.toString(),
            field + QStringLiteral(".releaseTrackMbid"));
        if (!parsed.ok()) {
            return malformed<ReleaseTrackCoordinate>(
                field + QStringLiteral(".releaseTrackMbid"),
                QStringLiteral("Persisted Release Track MBID is invalid."));
        }
        releaseTrack = *parsed.value;
    }

    auto recording = requiredMbid<RecordingMbid>(
        object, QStringLiteral("recordingMbid"),
        field + QStringLiteral(".recordingMbid"));
    if (!recording.ok())
        return SessionPersistenceResult<ReleaseTrackCoordinate>::failure(
            recording.error.code, recording.error.field,
            recording.error.detail);

    auto medium = requiredPositiveInt(
        object, QStringLiteral("mediumPosition"),
        field + QStringLiteral(".mediumPosition"));
    if (!medium.ok())
        return SessionPersistenceResult<ReleaseTrackCoordinate>::failure(
            medium.error.code, medium.error.field, medium.error.detail);

    auto track = requiredPositiveInt(
        object, QStringLiteral("trackPosition"),
        field + QStringLiteral(".trackPosition"));
    if (!track.ok())
        return SessionPersistenceResult<ReleaseTrackCoordinate>::failure(
            track.error.code, track.error.field, track.error.detail);

    auto coordinate = ReleaseTrackCoordinate::create(
        *releaseGroup.value, *release.value, releaseTrack, *recording.value,
        *medium.value, *track.value);
    if (!coordinate.ok()) {
        return malformed<ReleaseTrackCoordinate>(
            field, QStringLiteral("Persisted release context is invalid."));
    }

    return SessionPersistenceResult<ReleaseTrackCoordinate>::success(
        *coordinate.value);
}

SessionPersistenceResult<QueueOccurrence> occurrenceFromJson(
    const QJsonObject &object,
    int index)
{
    const QString field = QStringLiteral("queue.items[%1]").arg(index);
    const QString unknown = firstUnknownKey(
        object, {"occurrenceId", "recordingMbid", "releaseContext"});
    if (!unknown.isEmpty()) {
        return malformed<QueueOccurrence>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown queue-item field in schema v1."));
    }

    const QJsonValue occurrenceValue =
        object.value(QStringLiteral("occurrenceId"));
    if (!occurrenceValue.isString()) {
        return malformed<QueueOccurrence>(
            field + QStringLiteral(".occurrenceId"),
            QStringLiteral("Expected a queue occurrence ID string."));
    }
    auto occurrenceId = QueueOccurrenceId::parse(occurrenceValue.toString());
    if (!occurrenceId.ok()) {
        return malformed<QueueOccurrence>(
            field + QStringLiteral(".occurrenceId"),
            QStringLiteral("Persisted queue occurrence ID is invalid."));
    }

    auto recording = requiredMbid<RecordingMbid>(
        object, QStringLiteral("recordingMbid"),
        field + QStringLiteral(".recordingMbid"));
    if (!recording.ok()) {
        return SessionPersistenceResult<QueueOccurrence>::failure(
            recording.error.code, recording.error.field,
            recording.error.detail);
    }

    std::optional<ReleaseTrackCoordinate> releaseContext;
    const QJsonValue contextValue =
        object.value(QStringLiteral("releaseContext"));
    if (!contextValue.isNull()) {
        if (!contextValue.isObject()) {
            return malformed<QueueOccurrence>(
                field + QStringLiteral(".releaseContext"),
                QStringLiteral("Expected release context object or null."));
        }
        auto parsedContext = releaseContextFromJson(
            contextValue.toObject(),
            field + QStringLiteral(".releaseContext"));
        if (!parsedContext.ok()) {
            return SessionPersistenceResult<QueueOccurrence>::failure(
                parsedContext.error.code, parsedContext.error.field,
                parsedContext.error.detail);
        }
        releaseContext = *parsedContext.value;
    }

    auto occurrence = QueueOccurrence::create(
        *occurrenceId.value, *recording.value, releaseContext);
    if (!occurrence.ok()) {
        return SessionPersistenceResult<QueueOccurrence>::failure(
            ErrorCode::InvalidQueueState,
            field,
            QStringLiteral(
                "Persisted queue occurrence violates BA1 identity invariants."));
    }

    return SessionPersistenceResult<QueueOccurrence>::success(
        *occurrence.value);
}

std::optional<EnqueueOriginKind> enqueueOriginKindFromName(const QString &name)
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

SessionPersistenceResult<CanonicalEntityRef> canonicalRefFromJson(
    const QJsonObject &object,
    const QString &field)
{
    const QString unknown =
        firstUnknownKey(object, {"kind", "entityKind", "mbid"});
    if (!unknown.isEmpty()) {
        return malformed<CanonicalEntityRef>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown canonical enqueue-container field."));
    }

    const QJsonValue kindValue = object.value(QStringLiteral("entityKind"));
    const QJsonValue mbidValue = object.value(QStringLiteral("mbid"));
    if (!kindValue.isString() || !mbidValue.isString()) {
        return malformed<CanonicalEntityRef>(
            field,
            QStringLiteral("Canonical enqueue container requires entityKind and mbid strings."));
    }

    const QString kind = kindValue.toString();
    const QString mbid = mbidValue.toString();
    if (kind == QStringLiteral("artist")) {
        const auto parsed = ArtistMbid::parse(mbid);
        if (parsed.ok())
            return SessionPersistenceResult<CanonicalEntityRef>::success(
                CanonicalEntityRef::from(*parsed.value));
    } else if (kind == QStringLiteral("release_group")) {
        const auto parsed = ReleaseGroupMbid::parse(mbid);
        if (parsed.ok())
            return SessionPersistenceResult<CanonicalEntityRef>::success(
                CanonicalEntityRef::from(*parsed.value));
    } else if (kind == QStringLiteral("release")) {
        const auto parsed = ReleaseMbid::parse(mbid);
        if (parsed.ok())
            return SessionPersistenceResult<CanonicalEntityRef>::success(
                CanonicalEntityRef::from(*parsed.value));
    } else if (kind == QStringLiteral("release_track")) {
        const auto parsed = ReleaseTrackMbid::parse(mbid);
        if (parsed.ok())
            return SessionPersistenceResult<CanonicalEntityRef>::success(
                CanonicalEntityRef::from(*parsed.value));
    } else if (kind == QStringLiteral("recording")) {
        const auto parsed = RecordingMbid::parse(mbid);
        if (parsed.ok())
            return SessionPersistenceResult<CanonicalEntityRef>::success(
                CanonicalEntityRef::from(*parsed.value));
    } else if (kind == QStringLiteral("work")) {
        const auto parsed = WorkMbid::parse(mbid);
        if (parsed.ok())
            return SessionPersistenceResult<CanonicalEntityRef>::success(
                CanonicalEntityRef::from(*parsed.value));
    }

    return malformed<CanonicalEntityRef>(
        field,
        QStringLiteral("Canonical enqueue container has invalid entity kind or MBID."));
}

SessionPersistenceResult<EnqueueContainerRef> enqueueContainerFromJson(
    const QJsonObject &object,
    const QString &field)
{
    const QJsonValue kindValue = object.value(QStringLiteral("kind"));
    if (!kindValue.isString()) {
        return malformed<EnqueueContainerRef>(
            field + QStringLiteral(".kind"),
            QStringLiteral("Enqueue container kind must be a string."));
    }

    if (kindValue.toString() == QStringLiteral("canonical")) {
        const QString unknown = firstUnknownKey(
            object, {"kind", "entityKind", "mbid"});
        if (!unknown.isEmpty()) {
            return malformed<EnqueueContainerRef>(
                field + QLatin1Char('.') + unknown,
                QStringLiteral("Unknown canonical enqueue-container field."));
        }
        auto canonical = canonicalRefFromJson(object, field);
        if (!canonical.ok()) {
            return SessionPersistenceResult<EnqueueContainerRef>::failure(
                canonical.error.code, canonical.error.field,
                canonical.error.detail);
        }
        return SessionPersistenceResult<EnqueueContainerRef>::success(
            EnqueueContainerRef::canonical(*canonical.value));
    }

    if (kindValue.toString() == QStringLiteral("playlist")) {
        const QString unknown =
            firstUnknownKey(object, {"kind", "playlistId"});
        if (!unknown.isEmpty()) {
            return malformed<EnqueueContainerRef>(
                field + QLatin1Char('.') + unknown,
                QStringLiteral("Unknown playlist enqueue-container field."));
        }
        const QJsonValue playlistValue =
            object.value(QStringLiteral("playlistId"));
        if (!playlistValue.isString()) {
            return malformed<EnqueueContainerRef>(
                field + QStringLiteral(".playlistId"),
                QStringLiteral("Playlist enqueue container requires playlistId."));
        }
        const auto parsed =
            EnqueueContainerRef::playlist(playlistValue.toString());
        if (!parsed.ok()) {
            return malformed<EnqueueContainerRef>(
                field + QStringLiteral(".playlistId"),
                QStringLiteral("Persisted playlist container ID is invalid."));
        }
        return SessionPersistenceResult<EnqueueContainerRef>::success(
            *parsed.value);
    }

    return malformed<EnqueueContainerRef>(
        field + QStringLiteral(".kind"),
        QStringLiteral("Unknown enqueue container kind."));
}

SessionPersistenceResult<EnqueueOrigin> enqueueOriginFromJson(
    const QJsonObject &object,
    const QString &field)
{
    const QString unknown =
        firstUnknownKey(object, {"kind", "batchId", "container"});
    if (!unknown.isEmpty()) {
        return malformed<EnqueueOrigin>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown enqueue-origin field in schema v2."));
    }

    const QJsonValue kindValue = object.value(QStringLiteral("kind"));
    const QJsonValue batchValue = object.value(QStringLiteral("batchId"));
    if (!kindValue.isString() || !batchValue.isString()) {
        return malformed<EnqueueOrigin>(
            field,
            QStringLiteral("Enqueue origin requires kind and batchId strings."));
    }

    const auto kind = enqueueOriginKindFromName(kindValue.toString());
    const auto batch = EnqueueBatchId::parse(batchValue.toString());
    if (!kind.has_value() || !batch.ok()) {
        return malformed<EnqueueOrigin>(
            field,
            QStringLiteral("Persisted enqueue origin kind or batch ID is invalid."));
    }

    std::optional<EnqueueContainerRef> container;
    const QJsonValue containerValue =
        object.value(QStringLiteral("container"));
    if (!containerValue.isNull()) {
        if (!containerValue.isObject()) {
            return malformed<EnqueueOrigin>(
                field + QStringLiteral(".container"),
                QStringLiteral("Enqueue origin container must be object or null."));
        }
        auto parsed = enqueueContainerFromJson(
            containerValue.toObject(), field + QStringLiteral(".container"));
        if (!parsed.ok()) {
            return SessionPersistenceResult<EnqueueOrigin>::failure(
                parsed.error.code, parsed.error.field, parsed.error.detail);
        }
        container = *parsed.value;
    }

    return SessionPersistenceResult<EnqueueOrigin>::success(
        EnqueueOrigin(*kind, *batch.value, std::move(container)));
}

SessionPersistenceResult<QueueOccurrence> occurrenceFromJsonV2(
    const QJsonObject &object,
    int index)
{
    const QString field = QStringLiteral("queue.items[%1]").arg(index);
    const QString unknown = firstUnknownKey(
        object,
        {"occurrenceId", "recordingMbid", "releaseContext",
         "enqueueOrigin", "displaySnapshot"});
    if (!unknown.isEmpty()) {
        return malformed<QueueOccurrence>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown queue-item field in schema v2."));
    }

    QJsonObject identityObject = object;
    identityObject.remove(QStringLiteral("enqueueOrigin"));
    identityObject.remove(QStringLiteral("displaySnapshot"));
    auto base = occurrenceFromJson(identityObject, index);
    if (!base.ok()) {
        return SessionPersistenceResult<QueueOccurrence>::failure(
            base.error.code, base.error.field, base.error.detail);
    }

    std::optional<EnqueueOrigin> origin;
    const QJsonValue originValue =
        object.value(QStringLiteral("enqueueOrigin"));
    if (!originValue.isNull()) {
        if (!originValue.isObject()) {
            return malformed<QueueOccurrence>(
                field + QStringLiteral(".enqueueOrigin"),
                QStringLiteral("Enqueue origin must be object or null."));
        }
        auto parsed = enqueueOriginFromJson(
            originValue.toObject(),
            field + QStringLiteral(".enqueueOrigin"));
        if (!parsed.ok()) {
            return SessionPersistenceResult<QueueOccurrence>::failure(
                parsed.error.code, parsed.error.field, parsed.error.detail);
        }
        origin = *parsed.value;
    }

    auto rebuilt = QueueOccurrence::create(
        base.value->occurrenceId(),
        base.value->recordingMbid(),
        base.value->releaseContext(),
        std::move(origin));
    if (!rebuilt.ok()) {
        return SessionPersistenceResult<QueueOccurrence>::failure(
            ErrorCode::InvalidQueueState, field,
            QStringLiteral("Persisted queue occurrence violates BA1 invariants."));
    }
    return SessionPersistenceResult<QueueOccurrence>::success(*rebuilt.value);
}

SessionPersistenceResult<QueueOccurrence> occurrenceFromJsonV3(
    const QJsonObject &object,
    int index)
{
    const QString field = QStringLiteral("queue.items[%1]").arg(index);
    const QString unknown = firstUnknownKey(
        object,
        {"occurrenceId", "identityKind", "localIdentity", "recordingMbid",
         "verifiedRecordingMbid", "releaseContext", "enqueueOrigin",
         "displaySnapshot"});
    if (!unknown.isEmpty()) {
        return malformed<QueueOccurrence>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown queue-item field in schema v3."));
    }

    const QJsonValue occurrenceValue =
        object.value(QStringLiteral("occurrenceId"));
    if (!occurrenceValue.isString()) {
        return malformed<QueueOccurrence>(
            field + QStringLiteral(".occurrenceId"),
            QStringLiteral("Expected a queue occurrence ID string."));
    }
    const auto occurrenceId = QueueOccurrenceId::parse(occurrenceValue.toString());
    if (!occurrenceId.ok()) {
        return malformed<QueueOccurrence>(
            field + QStringLiteral(".occurrenceId"),
            QStringLiteral("Persisted queue occurrence ID is invalid."));
    }

    const QJsonValue identityKindValue =
        object.value(QStringLiteral("identityKind"));
    if (!identityKindValue.isString()) {
        return malformed<QueueOccurrence>(
            field + QStringLiteral(".identityKind"),
            QStringLiteral("Expected canonical or local identity kind."));
    }
    const QString identityKind = identityKindValue.toString();
    const bool localIdentity = identityKind == QStringLiteral("local");
    if (!localIdentity && identityKind != QStringLiteral("canonical")) {
        return malformed<QueueOccurrence>(
            field + QStringLiteral(".identityKind"),
            QStringLiteral("Unsupported queue-item identity kind."));
    }
    if (!localIdentity
        && (object.contains(QStringLiteral("localIdentity"))
            || object.contains(QStringLiteral("verifiedRecordingMbid")))) {
        return malformed<QueueOccurrence>(
            field + QStringLiteral(".identityKind"),
            QStringLiteral("Canonical queue items must not carry local identity fields."));
    }

    std::optional<RecordingMbid> canonicalRecording;
    if (localIdentity) {
        if (object.contains(QStringLiteral("recordingMbid"))) {
            return malformed<QueueOccurrence>(
                field + QStringLiteral(".recordingMbid"),
                QStringLiteral("Local queue items must use the verified mapping field."));
        }
        const QJsonValue localValue =
            object.value(QStringLiteral("localIdentity"));
        if (!localValue.isString()) {
            return malformed<QueueOccurrence>(
                field + QStringLiteral(".localIdentity"),
                QStringLiteral("Local queue items require a local identity string."));
        }
        if (!object.contains(QStringLiteral("verifiedRecordingMbid"))) {
            return malformed<QueueOccurrence>(
                field + QStringLiteral(".verifiedRecordingMbid"),
                QStringLiteral("Local queue items require a verified mapping field or null."));
        }
        const QJsonValue recordingValue =
            object.value(QStringLiteral("verifiedRecordingMbid"));
        if (!recordingValue.isNull()) {
            if (!recordingValue.isString()) {
                return malformed<QueueOccurrence>(
                    field + QStringLiteral(".verifiedRecordingMbid"),
                    QStringLiteral("Verified local recording mapping must be a string or null."));
            }
            const auto parsedRecording = RecordingMbid::parse(
                recordingValue.toString());
            if (!parsedRecording.ok()) {
                return malformed<QueueOccurrence>(
                    field + QStringLiteral(".verifiedRecordingMbid"),
                    QStringLiteral("Verified local recording mapping is invalid."));
            }
            canonicalRecording = *parsedRecording.value;
        }
    } else {
        const auto parsedRecording = requiredMbid<RecordingMbid>(
            object, QStringLiteral("recordingMbid"),
            field + QStringLiteral(".recordingMbid"));
        if (!parsedRecording.ok()) {
            return SessionPersistenceResult<QueueOccurrence>::failure(
                parsedRecording.error.code,
                parsedRecording.error.field,
                parsedRecording.error.detail);
        }
        canonicalRecording = *parsedRecording.value;
    }

    std::optional<ReleaseTrackCoordinate> releaseContext;
    const QJsonValue contextValue =
        object.value(QStringLiteral("releaseContext"));
    if (!contextValue.isNull()) {
        if (!contextValue.isObject()) {
            return malformed<QueueOccurrence>(
                field + QStringLiteral(".releaseContext"),
                QStringLiteral("Expected release context object or null."));
        }
        const auto parsedContext = releaseContextFromJson(
            contextValue.toObject(), field + QStringLiteral(".releaseContext"));
        if (!parsedContext.ok()) {
            return SessionPersistenceResult<QueueOccurrence>::failure(
                parsedContext.error.code,
                parsedContext.error.field,
                parsedContext.error.detail);
        }
        releaseContext = *parsedContext.value;
    }

    std::optional<EnqueueOrigin> origin;
    const QJsonValue originValue =
        object.value(QStringLiteral("enqueueOrigin"));
    if (!originValue.isNull()) {
        if (!originValue.isObject()) {
            return malformed<QueueOccurrence>(
                field + QStringLiteral(".enqueueOrigin"),
                QStringLiteral("Enqueue origin must be object or null."));
        }
        const auto parsedOrigin = enqueueOriginFromJson(
            originValue.toObject(), field + QStringLiteral(".enqueueOrigin"));
        if (!parsedOrigin.ok()) {
            return SessionPersistenceResult<QueueOccurrence>::failure(
                parsedOrigin.error.code,
                parsedOrigin.error.field,
                parsedOrigin.error.detail);
        }
        origin = *parsedOrigin.value;
    }

    const auto occurrence = localIdentity
        ? QueueOccurrence::createLocal(
              *occurrenceId.value,
              object.value(QStringLiteral("localIdentity")).toString(),
              std::move(canonicalRecording),
              std::move(releaseContext),
              std::move(origin))
        : QueueOccurrence::create(
              *occurrenceId.value,
              *canonicalRecording,
              std::move(releaseContext),
              std::move(origin));
    if (!occurrence.ok()) {
        return SessionPersistenceResult<QueueOccurrence>::failure(
            ErrorCode::InvalidQueueState,
            field,
            QStringLiteral("Persisted queue occurrence violates BA1 identity invariants."));
    }
    return SessionPersistenceResult<QueueOccurrence>::success(*occurrence.value);
}

SessionPersistenceResult<catalogue::LastKnownDisplaySnapshot>
displaySnapshotFromJson(
    const QJsonObject &object,
    const QString &field)
{
    const QString unknown =
        firstUnknownKey(object, {"primaryText", "artistCredit"});
    if (!unknown.isEmpty()) {
        return malformed<catalogue::LastKnownDisplaySnapshot>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown display-snapshot field in schema v2."));
    }

    const QJsonValue primaryValue =
        object.value(QStringLiteral("primaryText"));
    const QJsonValue creditValue =
        object.value(QStringLiteral("artistCredit"));
    if (!primaryValue.isString() || !creditValue.isArray()
        || creditValue.toArray().size() > boundary::MaxArtistCredits) {
        return malformed<catalogue::LastKnownDisplaySnapshot>(
            field,
            QStringLiteral("Display snapshot requires primaryText string and artistCredit array."));
    }

    QStringList artistCredit;
    for (const QJsonValue &value : creditValue.toArray()) {
        if (!value.isString()) {
            return malformed<catalogue::LastKnownDisplaySnapshot>(
                field + QStringLiteral(".artistCredit"),
                QStringLiteral("Display artist credit entries must be strings."));
        }
        artistCredit.append(value.toString());
    }

    if (!boundary::display(primaryValue.toString(), artistCredit))
        return malformed<catalogue::LastKnownDisplaySnapshot>(field, QStringLiteral("Display exceeds text budget."));
    return SessionPersistenceResult<catalogue::LastKnownDisplaySnapshot>::success(
        catalogue::LastKnownDisplaySnapshot{
            primaryValue.toString(), std::move(artistCredit)});
}

SessionPersistenceResult<qint64> nonNegativeIntegerMs(
    const QJsonValue &value,
    const QString &field)
{
    if (!value.isDouble()) {
        return malformed<qint64>(
            field, QStringLiteral("Expected integer milliseconds."));
    }
    const qint64 number = value.toInteger(-1);
    if (number < 0 || !std::isfinite(value.toDouble()) || std::floor(value.toDouble()) != value.toDouble()) {
        return malformed<qint64>(
            field, QStringLiteral("Expected finite non-negative integer milliseconds."));
    }
    return SessionPersistenceResult<qint64>::success(
        number);
}

std::optional<DurableRepeatMode> repeatModeFromName(const QString &name)
{
    if (name == QStringLiteral("off"))
        return DurableRepeatMode::Off;
    if (name == QStringLiteral("queue"))
        return DurableRepeatMode::Queue;
    return std::nullopt;
}

} // namespace

QString sessionPersistenceErrorCodeName(SessionPersistenceErrorCode code)
{
    switch (code) {
    case ErrorCode::None:
        return QStringLiteral("none");
    case ErrorCode::InvalidProfileScopeId:
        return QStringLiteral("invalid_profile_scope_id");
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
    case ErrorCode::InvalidQueueState:
        return QStringLiteral("invalid_queue_state");
    case ErrorCode::InvalidResumeState:
        return QStringLiteral("invalid_resume_state");
    case ErrorCode::InvalidCoordinatorState:
        return QStringLiteral("invalid_coordinator_state");
    }
    return QStringLiteral("unknown");
}

QString durableRepeatModeName(DurableRepeatMode mode)
{
    switch (mode) {
    case DurableRepeatMode::Off:
        return QStringLiteral("off");
    case DurableRepeatMode::Queue:
        return QStringLiteral("queue");
    }
    return QStringLiteral("unknown");
}

QString VinylSessionPersistence::schemaName()
{
    return QStringLiteral("colosseum.vinyl.session-persistence");
}

SessionPersistenceResult<QByteArray> VinylSessionPersistence::capture(
    const ProfileScopeId &profileId,
    const VinylQueue &queue,
    const PlaybackSession &session,
    SessionCoordinatorIntent coordinatorIntent,
    QVector<QueueDisplaySnapshot> displaySnapshots)
{
    if (queue.size() > boundary::MaxQueueItems || displaySnapshots.size() > boundary::MaxQueueItems)
        return SessionPersistenceResult<QByteArray>::failure(ErrorCode::InvalidQueueState, QStringLiteral("queue"), QStringLiteral("Queue exceeds item budget."));
    int snapshotVersion = SchemaVersion;
    for (const QueueOccurrence &occurrence : queue.items()) {
        if (!occurrence.localIdentity().isEmpty()) {
            snapshotVersion = MaximumSupportedVersion;
            break;
        }
    }
    QHash<QString, catalogue::LastKnownDisplaySnapshot> displayByOccurrence;
    for (const QueueDisplaySnapshot &entry : displaySnapshots) {
        if (!boundary::display(entry.display.primaryText, entry.display.artistCredit)
            || !queue.find(entry.occurrenceId)
            || displayByOccurrence.contains(entry.occurrenceId.value())) {
            return SessionPersistenceResult<QByteArray>::failure(
                ErrorCode::InvalidQueueState,
                QStringLiteral("queue.items.displaySnapshot"),
                QStringLiteral(
                    "Display snapshots must name unique occurrences in the persisted queue."));
        }
        displayByOccurrence.insert(
            entry.occurrenceId.value(), entry.display);
    }

    QJsonArray items;
    for (const QueueOccurrence &occurrence : queue.items()) {
        const auto it =
            displayByOccurrence.constFind(occurrence.occurrenceId().value());
        const catalogue::LastKnownDisplaySnapshot *display =
            it == displayByOccurrence.constEnd() ? nullptr : &it.value();
        items.append(snapshotVersion == 3
                         ? occurrenceToJsonV3(occurrence, display)
                         : occurrenceToJsonV2(occurrence, display));
    }

    QJsonObject queueObject;
    queueObject.insert(QStringLiteral("items"), items);
    if (queue.activeOccurrenceId().has_value()) {
        queueObject.insert(QStringLiteral("activeOccurrenceId"),
                           queue.activeOccurrenceId()->value());
    } else {
        queueObject.insert(
            QStringLiteral("activeOccurrenceId"), QJsonValue::Null);
    }

    if (queue.shuffleOriginalOrder().has_value()) {
        QJsonArray originalOrder;
        for (const QueueOccurrenceId &id : *queue.shuffleOriginalOrder())
            originalOrder.append(id.value());
        QJsonObject shuffleObject;
        shuffleObject.insert(
            QStringLiteral("originalOrder"), originalOrder);
        queueObject.insert(QStringLiteral("shuffle"), shuffleObject);
    } else {
        queueObject.insert(QStringLiteral("shuffle"), QJsonValue::Null);
    }

    QJsonValue resumeValue = QJsonValue::Null;
    const PlaybackState state = session.state();
    const bool resumable = state == PlaybackState::Loading
        || state == PlaybackState::Playing
        || state == PlaybackState::Paused;

    if (resumable) {
        if (!queue.activeOccurrenceId().has_value()
            || !session.activeOccurrence().has_value()) {
            return SessionPersistenceResult<QByteArray>::failure(
                ErrorCode::InvalidResumeState,
                QStringLiteral("resume.occurrenceId"),
                QStringLiteral(
                    "A resumable BA2 lifecycle requires one active BA1 occurrence."));
        }

        const QueueOccurrence *queueActive = queue.activeOccurrence();
        if (!queueActive
            || !sameOccurrence(*queueActive, *session.activeOccurrence())) {
            return SessionPersistenceResult<QByteArray>::failure(
                ErrorCode::InvalidResumeState,
                QStringLiteral("resume.occurrenceId"),
                QStringLiteral(
                    "BA1 active occurrence and BA2 lifecycle binding disagree."));
        }

        std::optional<double> positionSeconds;
        if (session.pendingSeek().has_value()) {
            positionSeconds = session.pendingSeek()->targetSeconds();
        } else if (session.positionSeconds().has_value()) {
            positionSeconds = session.positionSeconds();
        }

        QJsonObject resumeObject;
        resumeObject.insert(QStringLiteral("occurrenceId"),
                            queueActive->occurrenceId().value());
        resumeObject.insert(QStringLiteral("playWhenReady"),
                            session.playWhenReady());
        if (positionSeconds.has_value()) {
            const auto positionMs = boundary::milliseconds(*positionSeconds);
            if (!positionMs.has_value())
                return SessionPersistenceResult<QByteArray>::failure(ErrorCode::InvalidResumeState, QStringLiteral("resume.positionMs"));
            resumeObject.insert(QStringLiteral("positionMs"), *positionMs);
        }
        resumeValue = resumeObject;
    }

    if (coordinatorIntent.pendingInput.has_value()) {
        if (!queue.activeOccurrenceId().has_value()
            || coordinatorIntent.pendingInput->occurrenceId
                != *queue.activeOccurrenceId()
            ) {
            return SessionPersistenceResult<QByteArray>::failure(
                ErrorCode::InvalidCoordinatorState,
                QStringLiteral("coordinator.pendingInput"),
                QStringLiteral(
                    "Pending input must name the active queue occurrence with matching queue identity."));
        }
    }

    QJsonObject coordinatorObject;
    coordinatorObject.insert(
        QStringLiteral("repeatMode"),
        durableRepeatModeName(coordinatorIntent.repeatMode));
    if (coordinatorIntent.pendingInput.has_value()) {
        QJsonObject pendingInputObject;
        pendingInputObject.insert(
            QStringLiteral("occurrenceId"),
            coordinatorIntent.pendingInput->occurrenceId.value());
        pendingInputObject.insert(
            QStringLiteral("playWhenReady"),
            coordinatorIntent.pendingInput->playWhenReady);
        coordinatorObject.insert(
            QStringLiteral("pendingInput"), pendingInputObject);
    } else {
        coordinatorObject.insert(
            QStringLiteral("pendingInput"), QJsonValue::Null);
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), schemaName());
    root.insert(QStringLiteral("version"), snapshotVersion);
    root.insert(QStringLiteral("profileScopeId"), profileId.value());
    root.insert(QStringLiteral("queue"), queueObject);
    root.insert(QStringLiteral("resume"), resumeValue);
    root.insert(QStringLiteral("coordinator"), coordinatorObject);

    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (bytes.size() > boundary::MaxSnapshotBytes)
        return SessionPersistenceResult<QByteArray>::failure(ErrorCode::InvalidQueueState, QStringLiteral("snapshot"));
    return SessionPersistenceResult<QByteArray>::success(bytes);
}

SessionPersistenceResult<RestoredSessionState>
VinylSessionPersistence::restore(
    const ProfileScopeId &expectedProfileId,
    const QByteArray &snapshot)
{
    if (snapshot.size() > boundary::MaxSnapshotBytes)
        return malformed<RestoredSessionState>(QStringLiteral("snapshot"), QStringLiteral("Snapshot exceeds byte budget."));
    if (snapshot.trimmed().isEmpty()) {
        return SessionPersistenceResult<RestoredSessionState>::failure(
            ErrorCode::EmptySnapshot,
            QStringLiteral("snapshot"),
            QStringLiteral("Session snapshot is empty."));
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(snapshot, &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        return malformed<RestoredSessionState>(
            QStringLiteral("snapshot"),
            QStringLiteral("Session snapshot is not valid JSON object data."));
    }

    const QJsonObject root = document.object();

    const QJsonValue schemaValue = root.value(QStringLiteral("schema"));
    if (!schemaValue.isString()
        || schemaValue.toString() != schemaName()) {
        return SessionPersistenceResult<RestoredSessionState>::failure(
            ErrorCode::UnsupportedSchema,
            QStringLiteral("schema"),
            QStringLiteral("Session snapshot schema is unsupported."));
    }

    const QJsonValue versionValue = root.value(QStringLiteral("version"));
    if (!versionValue.isDouble()
        || std::floor(versionValue.toDouble()) != versionValue.toDouble()) {
        return SessionPersistenceResult<RestoredSessionState>::failure(
            ErrorCode::UnsupportedVersion,
            QStringLiteral("version"),
            QStringLiteral("Session snapshot version is unsupported."));
    }
    const int version = versionValue.toInt(-1);
    if (version < MinimumSupportedVersion
        || version > MaximumSupportedVersion) {
        return SessionPersistenceResult<RestoredSessionState>::failure(
            ErrorCode::UnsupportedVersion,
            QStringLiteral("version"),
            QStringLiteral("Session snapshot version is unsupported."));
    }

    const QString rootUnknown = version == 1
        ? firstUnknownKey(
              root, {"schema", "version", "profileScopeId", "queue", "resume"})
        : firstUnknownKey(
              root, {"schema", "version", "profileScopeId", "queue", "resume",
                     "coordinator"});
    if (!rootUnknown.isEmpty()) {
        return malformed<RestoredSessionState>(
            rootUnknown,
            QStringLiteral("Unknown root field in declared session schema version."));
    }

    const QJsonValue profileValue =
        root.value(QStringLiteral("profileScopeId"));
    if (!profileValue.isString()) {
        return malformed<RestoredSessionState>(
            QStringLiteral("profileScopeId"),
            QStringLiteral("Expected profile scope ID string."));
    }
    const auto persistedProfile =
        ProfileScopeId::parse(profileValue.toString());
    if (!persistedProfile.ok()) {
        return malformed<RestoredSessionState>(
            QStringLiteral("profileScopeId"),
            QStringLiteral("Persisted profile scope ID is invalid."));
    }
    if (*persistedProfile.value != expectedProfileId) {
        return SessionPersistenceResult<RestoredSessionState>::failure(
            ErrorCode::ProfileMismatch,
            QStringLiteral("profileScopeId"),
            QStringLiteral(
                "Session snapshot belongs to a different profile scope."));
    }

    const QJsonValue queueValue = root.value(QStringLiteral("queue"));
    if (!queueValue.isObject()) {
        return malformed<RestoredSessionState>(
            QStringLiteral("queue"),
            QStringLiteral("Expected queue object."));
    }
    const QJsonObject queueObject = queueValue.toObject();
    const QString queueUnknown = version == 1
        ? firstUnknownKey(queueObject, {"items", "activeOccurrenceId"})
        : firstUnknownKey(
              queueObject, {"items", "activeOccurrenceId", "shuffle"});
    if (!queueUnknown.isEmpty()) {
        return malformed<RestoredSessionState>(
            QStringLiteral("queue.") + queueUnknown,
            QStringLiteral("Unknown queue field in declared session schema version."));
    }

    const QJsonValue itemsValue =
        queueObject.value(QStringLiteral("items"));
    if (!itemsValue.isArray() || itemsValue.toArray().size() > boundary::MaxQueueItems) {
        return malformed<RestoredSessionState>(
            QStringLiteral("queue.items"),
            QStringLiteral("Expected ordered queue item array."));
    }

    VinylQueue queue;
    QVector<QueueDisplaySnapshot> displaySnapshots;
    const QJsonArray items = itemsValue.toArray();
    for (int i = 0; i < items.size(); ++i) {
        if (!items.at(i).isObject()) {
            return malformed<RestoredSessionState>(
                QStringLiteral("queue.items[%1]").arg(i),
                QStringLiteral("Expected queue item object."));
        }

        const QJsonObject itemObject = items.at(i).toObject();
        auto occurrence = version == 1
            ? occurrenceFromJson(itemObject, i)
            : version == 2
                ? occurrenceFromJsonV2(itemObject, i)
                : occurrenceFromJsonV3(itemObject, i);
        if (!occurrence.ok()) {
            return SessionPersistenceResult<RestoredSessionState>::failure(
                occurrence.error.code, occurrence.error.field,
                occurrence.error.detail);
        }

        if (version >= 2) {
            const QJsonValue displayValue =
                itemObject.value(QStringLiteral("displaySnapshot"));
            if (!displayValue.isNull()) {
                if (!displayValue.isObject()) {
                    return malformed<RestoredSessionState>(
                        QStringLiteral("queue.items[%1].displaySnapshot").arg(i),
                        QStringLiteral("Display snapshot must be object or null."));
                }
                auto display = displaySnapshotFromJson(
                    displayValue.toObject(),
                    QStringLiteral("queue.items[%1].displaySnapshot").arg(i));
                if (!display.ok()) {
                    return SessionPersistenceResult<RestoredSessionState>::failure(
                        display.error.code, display.error.field,
                        display.error.detail);
                }
                displaySnapshots.append(
                    QueueDisplaySnapshot{
                        occurrence.value->occurrenceId(), *display.value});
            }
        }

        const QueueMutationStatus appended = queue.append(*occurrence.value);
        if (!appended.ok) {
            return SessionPersistenceResult<RestoredSessionState>::failure(
                ErrorCode::InvalidQueueState,
                QStringLiteral("queue.items[%1].occurrenceId").arg(i),
                QStringLiteral(
                    "Persisted queue contains duplicate or invalid occurrence identity."));
        }
    }

    const QJsonValue activeValue =
        queueObject.value(QStringLiteral("activeOccurrenceId"));
    if (!activeValue.isNull()) {
        if (!activeValue.isString()) {
            return malformed<RestoredSessionState>(
                QStringLiteral("queue.activeOccurrenceId"),
                QStringLiteral(
                    "Expected active occurrence ID string or null."));
        }
        auto activeId = QueueOccurrenceId::parse(activeValue.toString());
        if (!activeId.ok()) {
            return SessionPersistenceResult<RestoredSessionState>::failure(
                ErrorCode::InvalidQueueState,
                QStringLiteral("queue.activeOccurrenceId"),
                QStringLiteral("Persisted active occurrence ID is invalid."));
        }
        const QueueMutationStatus activated = queue.activate(*activeId.value);
        if (!activated.ok) {
            return SessionPersistenceResult<RestoredSessionState>::failure(
                ErrorCode::InvalidQueueState,
                QStringLiteral("queue.activeOccurrenceId"),
                QStringLiteral(
                    "Persisted active occurrence does not exist in the queue."));
        }
    }

    if (version >= 2) {
        const QJsonValue shuffleValue =
            queueObject.value(QStringLiteral("shuffle"));
        if (!shuffleValue.isNull()) {
            if (!shuffleValue.isObject()) {
                return malformed<RestoredSessionState>(
                    QStringLiteral("queue.shuffle"),
                    QStringLiteral("Shuffle state must be object or null."));
            }
            const QJsonObject shuffleObject = shuffleValue.toObject();
            const QString shuffleUnknown =
                firstUnknownKey(shuffleObject, {"originalOrder"});
            if (!shuffleUnknown.isEmpty()) {
                return malformed<RestoredSessionState>(
                    QStringLiteral("queue.shuffle.") + shuffleUnknown,
                    QStringLiteral("Unknown shuffle field in schema v2."));
            }
            const QJsonValue originalOrderValue =
                shuffleObject.value(QStringLiteral("originalOrder"));
            if (!originalOrderValue.isArray()) {
                return malformed<RestoredSessionState>(
                    QStringLiteral("queue.shuffle.originalOrder"),
                    QStringLiteral("Shuffle original order must be an array."));
            }

            QVector<QueueOccurrenceId> originalOrder;
            for (const QJsonValue &idValue : originalOrderValue.toArray()) {
                if (!idValue.isString()) {
                    return malformed<RestoredSessionState>(
                        QStringLiteral("queue.shuffle.originalOrder"),
                        QStringLiteral("Shuffle occurrence IDs must be strings."));
                }
                const auto id = QueueOccurrenceId::parse(idValue.toString());
                if (!id.ok()) {
                    return SessionPersistenceResult<RestoredSessionState>::failure(
                        ErrorCode::InvalidQueueState,
                        QStringLiteral("queue.shuffle.originalOrder"),
                        QStringLiteral("Persisted shuffle occurrence ID is invalid."));
                }
                originalOrder.append(*id.value);
            }

            const QueueMutationStatus restoredShuffle =
                queue.restoreShuffleOriginalOrder(std::move(originalOrder));
            if (!restoredShuffle.ok) {
                return SessionPersistenceResult<RestoredSessionState>::failure(
                    ErrorCode::InvalidQueueState,
                    QStringLiteral("queue.shuffle.originalOrder"),
                    QStringLiteral(
                        "Persisted shuffle original order is not a queue permutation."));
            }
        }
    }

    std::optional<SessionResumeIntent> resumeIntent;
    const QJsonValue resumeValue = root.value(QStringLiteral("resume"));
    if (!resumeValue.isNull()) {
        if (!resumeValue.isObject()) {
            return malformed<RestoredSessionState>(
                QStringLiteral("resume"),
                QStringLiteral("Expected resume object or null."));
        }
        const QJsonObject resumeObject = resumeValue.toObject();
        const QString resumeUnknown = version == 1
            ? firstUnknownKey(
                  resumeObject,
                  {"occurrenceId", "playWhenReady", "positionSeconds"})
            : firstUnknownKey(
                  resumeObject,
                  {"occurrenceId", "playWhenReady", "positionMs"});
        if (!resumeUnknown.isEmpty()) {
            return malformed<RestoredSessionState>(
                QStringLiteral("resume.") + resumeUnknown,
                QStringLiteral(
                    "Unknown resume field in declared session schema version; live work cannot be restored."));
        }

        if (!queue.activeOccurrenceId().has_value()) {
            return SessionPersistenceResult<RestoredSessionState>::failure(
                ErrorCode::InvalidResumeState,
                QStringLiteral("resume.occurrenceId"),
                QStringLiteral(
                    "Resume intent requires an active BA1 queue occurrence."));
        }

        const QJsonValue resumeIdValue =
            resumeObject.value(QStringLiteral("occurrenceId"));
        if (!resumeIdValue.isString()) {
            return malformed<RestoredSessionState>(
                QStringLiteral("resume.occurrenceId"),
                QStringLiteral("Expected queue occurrence ID string."));
        }
        auto resumeId = QueueOccurrenceId::parse(resumeIdValue.toString());
        if (!resumeId.ok()) {
            return SessionPersistenceResult<RestoredSessionState>::failure(
                ErrorCode::InvalidResumeState,
                QStringLiteral("resume.occurrenceId"),
                QStringLiteral("Persisted resume occurrence ID is invalid."));
        }
        if (*resumeId.value != *queue.activeOccurrenceId()) {
            return SessionPersistenceResult<RestoredSessionState>::failure(
                ErrorCode::InvalidResumeState,
                QStringLiteral("resume.occurrenceId"),
                QStringLiteral(
                    "Resume intent does not name the active queue occurrence."));
        }

        const QJsonValue playValue =
            resumeObject.value(QStringLiteral("playWhenReady"));
        if (!playValue.isBool()) {
            return malformed<RestoredSessionState>(
                QStringLiteral("resume.playWhenReady"),
                QStringLiteral("Expected boolean playback intent."));
        }

        std::optional<qint64> positionMs;
        if (version == 1
            && resumeObject.contains(QStringLiteral("positionSeconds"))) {
            const QJsonValue positionValue =
                resumeObject.value(QStringLiteral("positionSeconds"));
            if (!positionValue.isDouble()
                || !isFiniteNonNegative(positionValue.toDouble())) {
                return SessionPersistenceResult<RestoredSessionState>::failure(
                    ErrorCode::InvalidResumeState,
                    QStringLiteral("resume.positionSeconds"),
                    QStringLiteral(
                        "Resume position must be finite and non-negative."));
            }
            const auto convertedMs = boundary::milliseconds(positionValue.toDouble());
            if (!convertedMs.has_value())
                return SessionPersistenceResult<RestoredSessionState>::failure(ErrorCode::InvalidResumeState, QStringLiteral("resume.positionSeconds"));
            positionMs = *convertedMs;
        } else if (version >= 2
                   && resumeObject.contains(QStringLiteral("positionMs"))) {
            const auto parsedPosition = nonNegativeIntegerMs(
                resumeObject.value(QStringLiteral("positionMs")),
                QStringLiteral("resume.positionMs"));
            if (!parsedPosition.ok()) {
                return SessionPersistenceResult<RestoredSessionState>::failure(
                    ErrorCode::InvalidResumeState,
                    parsedPosition.error.field,
                    parsedPosition.error.detail);
            }
            positionMs = *parsedPosition.value;
        }

        resumeIntent.emplace(
            *resumeId.value, playValue.toBool(), positionMs);
    }

    SessionCoordinatorIntent coordinatorIntent;
    if (version >= 2) {
        const QJsonValue coordinatorValue =
            root.value(QStringLiteral("coordinator"));
        if (!coordinatorValue.isObject()) {
            return malformed<RestoredSessionState>(
                QStringLiteral("coordinator"),
                QStringLiteral("Coordinator intent must be an object in schema v2."));
        }
        const QJsonObject coordinatorObject = coordinatorValue.toObject();
        const QString coordinatorUnknown = firstUnknownKey(
            coordinatorObject, {"repeatMode", "pendingInput"});
        if (!coordinatorUnknown.isEmpty()) {
            return malformed<RestoredSessionState>(
                QStringLiteral("coordinator.") + coordinatorUnknown,
                QStringLiteral("Unknown coordinator field in schema v2."));
        }

        const QJsonValue repeatValue =
            coordinatorObject.value(QStringLiteral("repeatMode"));
        if (!repeatValue.isString()) {
            return malformed<RestoredSessionState>(
                QStringLiteral("coordinator.repeatMode"),
                QStringLiteral("Repeat mode must be a string."));
        }
        const auto repeatMode = repeatModeFromName(repeatValue.toString());
        if (!repeatMode.has_value()) {
            return SessionPersistenceResult<RestoredSessionState>::failure(
                ErrorCode::InvalidCoordinatorState,
                QStringLiteral("coordinator.repeatMode"),
                QStringLiteral("Persisted repeat mode is unsupported."));
        }
        coordinatorIntent.repeatMode = *repeatMode;

        const QJsonValue pendingValue =
            coordinatorObject.value(QStringLiteral("pendingInput"));
        if (!pendingValue.isNull()) {
            if (!pendingValue.isObject()) {
                return malformed<RestoredSessionState>(
                    QStringLiteral("coordinator.pendingInput"),
                    QStringLiteral("Pending input must be object or null."));
            }
            const QJsonObject pendingObject = pendingValue.toObject();
            const QString pendingUnknown = firstUnknownKey(
                pendingObject, {"occurrenceId", "playWhenReady"});
            if (!pendingUnknown.isEmpty()) {
                return malformed<RestoredSessionState>(
                    QStringLiteral("coordinator.pendingInput.")
                        + pendingUnknown,
                    QStringLiteral("Unknown pending-input field in schema v2."));
            }

            const QJsonValue pendingIdValue =
                pendingObject.value(QStringLiteral("occurrenceId"));
            const QJsonValue pendingPlayValue =
                pendingObject.value(QStringLiteral("playWhenReady"));
            if (!pendingIdValue.isString() || !pendingPlayValue.isBool()) {
                return malformed<RestoredSessionState>(
                    QStringLiteral("coordinator.pendingInput"),
                    QStringLiteral(
                        "Pending input requires occurrenceId string and playWhenReady boolean."));
            }

            const auto pendingId =
                QueueOccurrenceId::parse(pendingIdValue.toString());
            if (!pendingId.ok()
                || !queue.activeOccurrenceId().has_value()
                || *pendingId.value != *queue.activeOccurrenceId()) {
                return SessionPersistenceResult<RestoredSessionState>::failure(
                    ErrorCode::InvalidCoordinatorState,
                    QStringLiteral("coordinator.pendingInput.occurrenceId"),
                    QStringLiteral(
                        "Pending input must name the active queue occurrence."));
            }

            coordinatorIntent.pendingInput =
                SessionPendingInputIntent{
                    *pendingId.value, pendingPlayValue.toBool()};
        }
    }

    RestoredSessionState restored{
        std::move(queue),
        std::move(resumeIntent),
        std::move(coordinatorIntent),
        std::move(displaySnapshots)};
    return SessionPersistenceResult<RestoredSessionState>::success(
        std::move(restored));
}

} // namespace vinyl
