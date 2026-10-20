#include "FeedbackTastePersistence.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>

namespace vinyl {

namespace {

using ErrorCode = TastePersistenceErrorCode;

template <typename T>
TastePersistenceResult<T> malformed(QString field, QString detail)
{
    return TastePersistenceResult<T>::failure(
        ErrorCode::MalformedSnapshot, std::move(field), std::move(detail));
}

QString firstUnknownKey(const QJsonObject &object,
                        std::initializer_list<const char *> allowed)
{
    QSet<QString> keys;
    for (const char *key : allowed)
        keys.insert(QString::fromLatin1(key));
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        if (!keys.contains(it.key()))
            return it.key();
    return QString();
}

TastePersistenceResult<qint64> positiveMs(
    const QJsonValue &value, const QString &field)
{
    if (!value.isDouble())
        return malformed<qint64>(field, QStringLiteral("Expected integer milliseconds."));
    // Qt preserves integral JSON numbers as qint64. Converting through double
    // loses exact milliseconds and rounds qint64::max() up to 2^63.
    const qint64 number = value.toInteger(-1);
    if (number <= 0)
        return malformed<qint64>(field, QStringLiteral("Expected a positive integer."));
    return TastePersistenceResult<qint64>::success(number);
}

TastePersistenceResult<quint64> revisionFromJson(
    const QJsonValue &value, const QString &field)
{
    if (!value.isString())
        return malformed<quint64>(field, QStringLiteral("Revision must be a decimal string."));
    bool ok = false;
    const quint64 revision = value.toString().toULongLong(&ok);
    if (!ok || revision == 0)
        return malformed<quint64>(field, QStringLiteral("Revision must be positive."));
    return TastePersistenceResult<quint64>::success(revision);
}

QJsonValue revisionToJson(quint64 revision)
{
    return QString::number(revision);
}

TastePersistenceResult<CanonicalEntityRef> canonicalFromJson(
    const QJsonObject &object, const QString &field)
{
    const QJsonValue kindValue = object.value(QStringLiteral("kind"));
    const QJsonValue mbidValue = object.value(QStringLiteral("mbid"));
    if (!kindValue.isString() || !mbidValue.isString())
        return malformed<CanonicalEntityRef>(
            field, QStringLiteral("Canonical identity requires kind and MBID."));
    const QString kind = kindValue.toString();
    const QString mbid = mbidValue.toString();

#define VINYL_PARSE_CANONICAL(name, type)     if (kind == QStringLiteral(name)) {         auto id = type::parse(mbid, field + QStringLiteral(".mbid"));         if (id.ok()) return TastePersistenceResult<CanonicalEntityRef>::success(             CanonicalEntityRef::from(*id.value));     }
    VINYL_PARSE_CANONICAL("artist", ArtistMbid)
    VINYL_PARSE_CANONICAL("release_group", ReleaseGroupMbid)
    VINYL_PARSE_CANONICAL("release", ReleaseMbid)
    VINYL_PARSE_CANONICAL("release_track", ReleaseTrackMbid)
    VINYL_PARSE_CANONICAL("recording", RecordingMbid)
    VINYL_PARSE_CANONICAL("work", WorkMbid)
#undef VINYL_PARSE_CANONICAL

    return malformed<CanonicalEntityRef>(
        field, QStringLiteral("Unsupported or invalid canonical identity."));
}

QJsonObject canonicalToJson(const CanonicalEntityRef &canonical)
{
    QJsonObject out;
    out.insert(QStringLiteral("kind"), canonicalEntityKindName(canonical.kind()));
    out.insert(QStringLiteral("mbid"), canonical.mbidString());
    return out;
}

QJsonValue providerRefToJson(const std::optional<ProviderItemRef> &ref)
{
    if (!ref.has_value())
        return QJsonValue::Null;
    QJsonObject out;
    out.insert(QStringLiteral("providerId"), ref->providerId().value());
    out.insert(QStringLiteral("itemId"), ref->itemId().value());
    return out;
}

TastePersistenceResult<std::optional<ProviderItemRef>> providerRefFromJson(
    const QJsonValue &value, const QString &field)
{
    if (value.isNull())
        return TastePersistenceResult<std::optional<ProviderItemRef>>::success(
            std::nullopt);
    if (!value.isObject())
        return malformed<std::optional<ProviderItemRef>>(
            field, QStringLiteral("Provider reference must be object or null."));
    const QJsonObject object = value.toObject();
    const QString unknown = firstUnknownKey(object, {"providerId", "itemId"});
    if (!unknown.isEmpty())
        return malformed<std::optional<ProviderItemRef>>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown provider-reference field."));
    auto provider = ProviderId::parse(object.value(QStringLiteral("providerId")).toString());
    auto item = ProviderItemId::parse(object.value(QStringLiteral("itemId")).toString());
    if (!provider.ok() || !item.ok())
        return malformed<std::optional<ProviderItemRef>>(
            field, QStringLiteral("Invalid provider provenance."));
    return TastePersistenceResult<std::optional<ProviderItemRef>>::success(
        ProviderItemRef(*provider.value, *item.value));
}

std::optional<ExplicitTasteFeedback> feedbackFromName(const QString &name)
{
    if (name == QStringLiteral("neutral")) return ExplicitTasteFeedback::Neutral;
    if (name == QStringLiteral("positive")) return ExplicitTasteFeedback::Positive;
    if (name == QStringLiteral("negative")) return ExplicitTasteFeedback::Negative;
    return std::nullopt;
}

std::optional<BehavioralSignalKind> behaviorKindFromName(const QString &name)
{
    if (name == QStringLiteral("replay")) return BehavioralSignalKind::Replay;
    if (name == QStringLiteral("deliberate_queue")) return BehavioralSignalKind::DeliberateQueue;
    if (name == QStringLiteral("natural_completion")) return BehavioralSignalKind::NaturalCompletion;
    if (name == QStringLiteral("album_completion")) return BehavioralSignalKind::AlbumCompletion;
    if (name == QStringLiteral("library_save")) return BehavioralSignalKind::LibrarySave;
    if (name == QStringLiteral("playlist_add")) return BehavioralSignalKind::PlaylistAdd;
    if (name == QStringLiteral("early_skip")) return BehavioralSignalKind::EarlySkip;
    if (name == QStringLiteral("queue_removal")) return BehavioralSignalKind::QueueRemoval;
    if (name == QStringLiteral("recommendation_avoidance"))
        return BehavioralSignalKind::RecommendationAvoidance;
    if (name == QStringLiteral("passive_autoplay_completion"))
        return BehavioralSignalKind::PassiveAutoplayCompletion;
    return std::nullopt;
}

QJsonObject behaviorToJson(const BehavioralSignalObservation &observation)
{
    QJsonObject out;
    out.insert(QStringLiteral("id"), observation.observationId().value());
    out.insert(QStringLiteral("kind"), behavioralSignalKindName(observation.kind()));
    out.insert(QStringLiteral("occurredAtMs"), observation.occurredAtMs());
    return out;
}

QJsonObject entityToJson(const EntityTasteState &state)
{
    QJsonObject explicitState;
    explicitState.insert(
        QStringLiteral("feedback"),
        explicitTasteFeedbackName(state.explicitFeedback().feedback()));
    explicitState.insert(
        QStringLiteral("revision"),
        state.explicitFeedback().revision() > 0
            ? revisionToJson(state.explicitFeedback().revision())
            : QJsonValue::Null);
    explicitState.insert(
        QStringLiteral("changedAtMs"),
        state.explicitFeedback().revision() > 0
            ? QJsonValue(state.explicitFeedback().changedAtMs())
            : QJsonValue::Null);
    explicitState.insert(
        QStringLiteral("providerFeedbackRef"),
        providerRefToJson(state.explicitFeedback().providerFeedbackRef()));

    const auto &suppression = state.suppression();
    QJsonObject suppressionState;
    suppressionState.insert(QStringLiteral("hidden"), suppression.hidden());
    suppressionState.insert(
        QStringLiteral("hiddenRevision"),
        suppression.hiddenRevision() > 0
            ? revisionToJson(suppression.hiddenRevision()) : QJsonValue::Null);
    suppressionState.insert(
        QStringLiteral("hiddenChangedAtMs"),
        suppression.hiddenRevision() > 0
            ? QJsonValue(suppression.hiddenChangedAtMs()) : QJsonValue::Null);

    suppressionState.insert(
        QStringLiteral("snoozedUntilMs"),
        suppression.snoozedUntilMs().has_value()
            ? QJsonValue(*suppression.snoozedUntilMs()) : QJsonValue::Null);
    suppressionState.insert(
        QStringLiteral("snoozeRevision"),
        suppression.snoozeRevision() > 0
            ? revisionToJson(suppression.snoozeRevision()) : QJsonValue::Null);
    suppressionState.insert(
        QStringLiteral("snoozeChangedAtMs"),
        suppression.snoozeRevision() > 0
            ? QJsonValue(suppression.snoozeChangedAtMs()) : QJsonValue::Null);
    suppressionState.insert(
        QStringLiteral("doNotRecommend"), suppression.doNotRecommend());
    suppressionState.insert(
        QStringLiteral("doNotRecommendRevision"),
        suppression.doNotRecommendRevision() > 0
            ? revisionToJson(suppression.doNotRecommendRevision()) : QJsonValue::Null);
    suppressionState.insert(
        QStringLiteral("doNotRecommendChangedAtMs"),
        suppression.doNotRecommendRevision() > 0
            ? QJsonValue(suppression.doNotRecommendChangedAtMs()) : QJsonValue::Null);

    QVector<BehavioralSignalObservation> orderedBehavior =
        state.behavioralSignals();
    std::sort(
        orderedBehavior.begin(),
        orderedBehavior.end(),
        [](const BehavioralSignalObservation &left,
           const BehavioralSignalObservation &right) {
            return left.observationId().value()
                < right.observationId().value();
        });

    QJsonArray observations;
    for (const auto &observation : orderedBehavior)
        observations.append(behaviorToJson(observation));

    QJsonObject out = canonicalToJson(state.canonical());
    out.insert(QStringLiteral("explicit"), explicitState);

    out.insert(QStringLiteral("suppression"), suppressionState);
    out.insert(QStringLiteral("behavior"), observations);
    return out;
}

TastePersistenceResult<BehavioralSignalObservation> behaviorFromJson(
    const CanonicalEntityRef &canonical,
    const QJsonObject &object,
    const QString &field)
{
    const QString unknown =
        firstUnknownKey(object, {"id", "kind", "occurredAtMs"});
    if (!unknown.isEmpty())
        return malformed<BehavioralSignalObservation>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown behavior field."));

    const QJsonValue idValue = object.value(QStringLiteral("id"));
    const QJsonValue kindValue = object.value(QStringLiteral("kind"));
    if (!idValue.isString() || !kindValue.isString())
        return malformed<BehavioralSignalObservation>(
            field, QStringLiteral("Behavior requires id and kind strings."));
    auto id = BehaviorObservationId::parse(idValue.toString());
    const auto kind = behaviorKindFromName(kindValue.toString());
    auto occurred = positiveMs(
        object.value(QStringLiteral("occurredAtMs")),
        field + QStringLiteral(".occurredAtMs"));
    if (!id.ok() || !kind.has_value() || !occurred.ok())
        return malformed<BehavioralSignalObservation>(
            field, QStringLiteral("Behavior observation is invalid."));
    auto created = BehavioralSignalObservation::create(
        *id.value, canonical, *kind, *occurred.value);
    if (!created.ok())
        return malformed<BehavioralSignalObservation>(
            field, QStringLiteral("Behavior observation violates BE2 invariants."));
    return TastePersistenceResult<BehavioralSignalObservation>::success(
        *created.value);
}

TastePersistenceResult<quint64> requiredRevision(
    const QJsonObject &object,
    const QString &key,
    const QString &field)
{
    return revisionFromJson(object.value(key), field);
}

TastePersistenceResult<qint64> requiredPositiveMs(
    const QJsonObject &object,
    const QString &key,
    const QString &field)
{
    return positiveMs(object.value(key), field);
}

TastePersistenceResult<bool> requiredBool(
    const QJsonObject &object,
    const QString &key,
    const QString &field)
{
    const QJsonValue value = object.value(key);
    if (!value.isBool())
        return malformed<bool>(field, QStringLiteral("Expected boolean."));
    return TastePersistenceResult<bool>::success(value.toBool());
}
TastePersistenceResult<std::optional<qint64>> nullablePositiveMs(
    const QJsonValue &value,
    const QString &field)
{
    if (value.isNull())
        return TastePersistenceResult<std::optional<qint64>>::success(
            std::nullopt);
    auto parsed = positiveMs(value, field);
    if (!parsed.ok())
        return TastePersistenceResult<std::optional<qint64>>::failure(
            parsed.error.code, parsed.error.field, parsed.error.detail);
    return TastePersistenceResult<std::optional<qint64>>::success(
        *parsed.value);
}

TastePersistenceResult<std::optional<quint64>> nullableRevision(
    const QJsonValue &value,
    const QString &field)
{
    if (value.isNull())
        return TastePersistenceResult<std::optional<quint64>>::success(
            std::nullopt);
    auto parsed = revisionFromJson(value, field);
    if (!parsed.ok())
        return TastePersistenceResult<std::optional<quint64>>::failure(
            parsed.error.code, parsed.error.field, parsed.error.detail);
    return TastePersistenceResult<std::optional<quint64>>::success(
        *parsed.value);
}
TastePersistenceResult<bool> restoreExplicitState(
    TasteProfileState &state,
    const CanonicalEntityRef &canonical,
    const QJsonObject &object,
    const QString &field)
{
    const QString unknown = firstUnknownKey(
        object, {"feedback", "revision", "changedAtMs", "providerFeedbackRef"});
    if (!unknown.isEmpty())
        return malformed<bool>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown explicit-feedback field."));

    const QJsonValue feedbackValue = object.value(QStringLiteral("feedback"));
    if (!feedbackValue.isString())
        return malformed<bool>(
            field + QStringLiteral(".feedback"),
            QStringLiteral("Feedback must be a string."));
    const auto feedback = feedbackFromName(feedbackValue.toString());
    if (!feedback.has_value())
        return malformed<bool>(
            field + QStringLiteral(".feedback"),
            QStringLiteral("Unknown feedback value."));

    auto revision = nullableRevision(
        object.value(QStringLiteral("revision")),
        field + QStringLiteral(".revision"));
    if (!revision.ok())
        return TastePersistenceResult<bool>::failure(
            revision.error.code, revision.error.field, revision.error.detail);
    const QJsonValue changedValue = object.value(QStringLiteral("changedAtMs"));
    const QJsonValue providerValue =
        object.value(QStringLiteral("providerFeedbackRef"));

    if (!revision.value->has_value()) {
        if (!changedValue.isNull() || !providerValue.isNull()
            || *feedback != ExplicitTasteFeedback::Neutral) {
            return malformed<bool>(
                field,
                QStringLiteral("Unmutated explicit state must be neutral with null revision metadata."));
        }
        return TastePersistenceResult<bool>::success(true);
    }

    auto changed = positiveMs(
        changedValue, field + QStringLiteral(".changedAtMs"));
    auto provider = providerRefFromJson(
        providerValue, field + QStringLiteral(".providerFeedbackRef"));
    if (!changed.ok() || !provider.ok())
        return malformed<bool>(
            field, QStringLiteral("Explicit feedback metadata is invalid."));

    const auto status = state.applyExplicitFeedback(
        canonical, *feedback, **revision.value, *changed.value, *provider.value);
    if (!status.ok)
        return malformed<bool>(
            field, QStringLiteral("Explicit feedback violates BE2 invariants."));
    return TastePersistenceResult<bool>::success(true);
}
TastePersistenceResult<bool> restoreSuppressionState(
    TasteProfileState &state,
    const CanonicalEntityRef &canonical,
    const QJsonObject &object,
    const QString &field)
{
    const QString unknown = firstUnknownKey(
        object,
        {"hidden", "hiddenRevision", "hiddenChangedAtMs",
         "snoozedUntilMs", "snoozeRevision", "snoozeChangedAtMs",
         "doNotRecommend", "doNotRecommendRevision",
         "doNotRecommendChangedAtMs"});
    if (!unknown.isEmpty())
        return malformed<bool>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown suppression field."));

    auto hidden = requiredBool(
        object, QStringLiteral("hidden"), field + QStringLiteral(".hidden"));
    auto hiddenRevision = nullableRevision(
        object.value(QStringLiteral("hiddenRevision")),
        field + QStringLiteral(".hiddenRevision"));
    if (!hidden.ok() || !hiddenRevision.ok())
        return malformed<bool>(field, QStringLiteral("Invalid hidden state."));

    const QJsonValue hiddenChanged =
        object.value(QStringLiteral("hiddenChangedAtMs"));
    if (hiddenRevision.value->has_value()) {
        auto changed = positiveMs(
            hiddenChanged, field + QStringLiteral(".hiddenChangedAtMs"));
        if (!changed.ok())
            return malformed<bool>(
                field, QStringLiteral("Invalid hidden mutation metadata."));
        const auto status = state.setHidden(
            canonical, *hidden.value, **hiddenRevision.value, *changed.value);
        if (!status.ok)
            return malformed<bool>(
                field, QStringLiteral("Hidden state violates BE2 invariants."));
    } else if (!hiddenChanged.isNull() || *hidden.value) {
        return malformed<bool>(
            field,
            QStringLiteral("Unmutated hidden state must be false with null metadata."));
    }

    auto snoozed = nullablePositiveMs(
        object.value(QStringLiteral("snoozedUntilMs")),
        field + QStringLiteral(".snoozedUntilMs"));
    auto snoozeRevision = nullableRevision(
        object.value(QStringLiteral("snoozeRevision")),
        field + QStringLiteral(".snoozeRevision"));
    if (!snoozed.ok() || !snoozeRevision.ok())
        return malformed<bool>(field, QStringLiteral("Invalid snooze state."));
    const QJsonValue snoozeChanged =
        object.value(QStringLiteral("snoozeChangedAtMs"));
    if (snoozeRevision.value->has_value()) {
        auto changed = positiveMs(
            snoozeChanged, field + QStringLiteral(".snoozeChangedAtMs"));
        if (!changed.ok())
            return malformed<bool>(
                field, QStringLiteral("Invalid snooze mutation metadata."));
        const auto status = state.setSnoozedUntil(
            canonical,
            *snoozed.value,
            **snoozeRevision.value,
            *changed.value);
        if (!status.ok)
            return malformed<bool>(
                field, QStringLiteral("Snooze state violates BE2 invariants."));
    } else if (!snoozeChanged.isNull() || snoozed.value->has_value()) {
        return malformed<bool>(
            field,
            QStringLiteral("Unmutated snooze state requires null value and metadata."));
    }

    auto dnr = requiredBool(
        object,
        QStringLiteral("doNotRecommend"),
        field + QStringLiteral(".doNotRecommend"));
    auto dnrRevision = nullableRevision(
        object.value(QStringLiteral("doNotRecommendRevision")),
        field + QStringLiteral(".doNotRecommendRevision"));
    if (!dnr.ok() || !dnrRevision.ok())
        return malformed<bool>(
            field, QStringLiteral("Invalid do-not-recommend state."));

    const QJsonValue dnrChanged =
        object.value(QStringLiteral("doNotRecommendChangedAtMs"));
    if (dnrRevision.value->has_value()) {
        auto changed = positiveMs(
            dnrChanged,
            field + QStringLiteral(".doNotRecommendChangedAtMs"));
        if (!changed.ok())
            return malformed<bool>(
                field, QStringLiteral("Invalid do-not-recommend metadata."));
        const auto status = state.setDoNotRecommend(
            canonical, *dnr.value, **dnrRevision.value, *changed.value);
        if (!status.ok)
            return malformed<bool>(
                field,
                QStringLiteral("Do-not-recommend state violates BE2 invariants."));
    } else if (!dnrChanged.isNull() || *dnr.value) {
        return malformed<bool>(
            field,
            QStringLiteral("Unmutated do-not-recommend must be false with null metadata."));
    }

    return TastePersistenceResult<bool>::success(true);
}
TastePersistenceResult<bool> restoreEntity(
    TasteProfileState &state,
    const QJsonObject &object,
    const QString &field)
{
    const QString unknown = firstUnknownKey(
        object, {"kind", "mbid", "explicit", "suppression", "behavior"});
    if (!unknown.isEmpty())
        return malformed<bool>(
            field + QLatin1Char('.') + unknown,
            QStringLiteral("Unknown entity-state field."));

    auto canonical = canonicalFromJson(object, field);
    if (!canonical.ok())
        return TastePersistenceResult<bool>::failure(
            canonical.error.code, canonical.error.field, canonical.error.detail);

    const QJsonValue explicitValue = object.value(QStringLiteral("explicit"));
    const QJsonValue suppressionValue =
        object.value(QStringLiteral("suppression"));
    const QJsonValue behaviorValue = object.value(QStringLiteral("behavior"));
    if (!explicitValue.isObject() || !suppressionValue.isObject()
        || !behaviorValue.isArray()) {
        return malformed<bool>(
            field,
            QStringLiteral("Entity state requires explicit/suppression objects and behavior array."));
    }
    auto explicitStatus = restoreExplicitState(
        state,
        *canonical.value,
        explicitValue.toObject(),
        field + QStringLiteral(".explicit"));
    if (!explicitStatus.ok())
        return explicitStatus;

    auto suppressionStatus = restoreSuppressionState(
        state,
        *canonical.value,
        suppressionValue.toObject(),
        field + QStringLiteral(".suppression"));
    if (!suppressionStatus.ok())
        return suppressionStatus;

    int index = 0;
    for (const QJsonValue &value : behaviorValue.toArray()) {
        const QString behaviorField = field + QStringLiteral(".behavior[")
            + QString::number(index) + QLatin1Char(']');
        if (!value.isObject())
            return malformed<bool>(
                behaviorField, QStringLiteral("Behavior entry must be an object."));
        auto observation = behaviorFromJson(
            *canonical.value, value.toObject(), behaviorField);
        if (!observation.ok())
            return TastePersistenceResult<bool>::failure(
                observation.error.code,
                observation.error.field,
                observation.error.detail);
        const auto status = state.recordBehavioralSignal(*observation.value);
        if (!status.ok)
            return malformed<bool>(
                behaviorField,
                QStringLiteral("Behavior entry violates BE2 invariants."));
        ++index;
    }
    return TastePersistenceResult<bool>::success(true);
}

} // namespace
QString tastePersistenceErrorCodeName(TastePersistenceErrorCode code)
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

QString FeedbackTastePersistence::schemaName()
{
    return QStringLiteral("colosseum.vinyl.feedback-taste-state");
}
TastePersistenceResult<QByteArray> FeedbackTastePersistence::capture(
    const TasteProfileState &state)
{
    QVector<const EntityTasteState *> ordered;
    ordered.reserve(state.states().size());
    for (const EntityTasteState &entity : state.states())
        ordered.append(&entity);

    std::sort(
        ordered.begin(),
        ordered.end(),
        [](const EntityTasteState *left, const EntityTasteState *right) {
            const QString leftKey =
                canonicalEntityKindName(left->canonical().kind())
                + QLatin1Char(':') + left->canonical().mbidString();
            const QString rightKey =
                canonicalEntityKindName(right->canonical().kind())
                + QLatin1Char(':') + right->canonical().mbidString();
            return leftKey < rightKey;
        });

    QJsonArray entities;
    for (const EntityTasteState *entity : ordered)
        entities.append(entityToJson(*entity));
    QJsonObject root;
    root.insert(QStringLiteral("schema"), schemaName());
    root.insert(QStringLiteral("version"), SchemaVersion);
    root.insert(QStringLiteral("profileScopeId"), state.profileId().value());
    root.insert(QStringLiteral("entities"), entities);

    return TastePersistenceResult<QByteArray>::success(
        QJsonDocument(root).toJson(QJsonDocument::Compact));
}

TastePersistenceResult<TasteProfileState> FeedbackTastePersistence::restore(
    const ProfileScopeId &expectedProfileId,
    const QByteArray &snapshot)
{
    if (snapshot.trimmed().isEmpty()) {
        return TastePersistenceResult<TasteProfileState>::failure(
            ErrorCode::EmptySnapshot, QStringLiteral("snapshot"));
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(snapshot, &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        return malformed<TasteProfileState>(
            QStringLiteral("snapshot"),
            QStringLiteral("Malformed JSON snapshot."));
    }
    const QJsonObject root = document.object();
    const QString unknown = firstUnknownKey(
        root, {"schema", "version", "profileScopeId", "entities"});
    if (!unknown.isEmpty()) {
        return malformed<TasteProfileState>(
            unknown, QStringLiteral("Unknown root field."));
    }

    if (root.value(QStringLiteral("schema")).toString() != schemaName()) {
        return TastePersistenceResult<TasteProfileState>::failure(
            ErrorCode::UnsupportedSchema, QStringLiteral("schema"));
    }

    const QJsonValue versionValue = root.value(QStringLiteral("version"));
    const qint64 version = versionValue.toInteger(-1);
    if (!versionValue.isDouble()
        || version < MinimumSupportedVersion || version > SchemaVersion) {
        return TastePersistenceResult<TasteProfileState>::failure(
            ErrorCode::UnsupportedVersion, QStringLiteral("version"));
    }
    const QJsonValue profileValue =
        root.value(QStringLiteral("profileScopeId"));
    if (!profileValue.isString()) {
        return malformed<TasteProfileState>(
            QStringLiteral("profileScopeId"),
            QStringLiteral("Profile scope must be a string."));
    }
    auto profile = ProfileScopeId::parse(profileValue.toString());
    if (!profile.ok()) {
        return malformed<TasteProfileState>(
            QStringLiteral("profileScopeId"),
            QStringLiteral("Persisted profile scope is invalid."));
    }
    if (*profile.value != expectedProfileId) {
        return TastePersistenceResult<TasteProfileState>::failure(
            ErrorCode::ProfileMismatch, QStringLiteral("profileScopeId"));
    }

    const QJsonValue entitiesValue = root.value(QStringLiteral("entities"));
    if (!entitiesValue.isArray()) {
        return malformed<TasteProfileState>(
            QStringLiteral("entities"),
            QStringLiteral("Entities must be an array."));
    }

    TasteProfileState restored(expectedProfileId);
    int index = 0;
    for (const QJsonValue &value : entitiesValue.toArray()) {
        const QString field = QStringLiteral("entities[")
            + QString::number(index) + QLatin1Char(']');
        if (!value.isObject()) {
            return malformed<TasteProfileState>(
                field, QStringLiteral("Entity state must be an object."));
        }
        auto status = restoreEntity(restored, value.toObject(), field);
        if (!status.ok()) {
            return TastePersistenceResult<TasteProfileState>::failure(
                status.error.code, status.error.field, status.error.detail);
        }
        ++index;
    }

    return TastePersistenceResult<TasteProfileState>::success(
        std::move(restored));
}

} // namespace vinyl
