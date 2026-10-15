#include "TrackerImportStore.h"

#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace {

constexpr int kSchemaVersion = 2;
constexpr auto kFileName = "tracker-imports.json";

bool setError(QString *out, const QString &message)
{
    if (out)
        *out = message;
    return false;
}

bool safeText(const QString &value, int maximum = 512)
{
    return !value.isEmpty() && value == value.trimmed() && value.size() <= maximum
        && !QDir::isAbsolutePath(value) && !value.contains(QLatin1String(".."));
}

bool safeDisplayTitle(const QString &value)
{
    if (value.size() > 256 || value != value.trimmed())
        return false;
    for (const QChar character : value) {
        if (character.isNull() || character.category() == QChar::Other_Control)
            return false;
    }
    return true;
}

QString cursorKey(TrackerProviderId providerId, const QString &remoteAccountId)
{
    return trackerProviderKey(providerId) + QChar(0x1f) + remoteAccountId;
}

std::optional<QString> optionalCursor(const QHash<QString, QString> &cursors,
                                      TrackerProviderId providerId,
                                      const QString &remoteAccountId)
{
    const auto value = cursors.constFind(cursorKey(providerId, remoteAccountId));
    return value == cursors.cend() ? std::nullopt : std::optional<QString>(*value);
}

bool cursorIs(const QHash<QString, QString> &cursors,
              TrackerProviderId providerId,
              const QString &remoteAccountId,
              const QString &expected)
{
    const auto value = optionalCursor(cursors, providerId, remoteAccountId);
    return expected.isEmpty() ? !value.has_value()
                              : value == std::optional<QString>(expected);
}

QString stableBatchId(const TrackerImportBatchDraft &draft)
{
    const QString material = trackerProviderKey(draft.providerId) + QChar(0x1f)
        + draft.remoteAccountId + QChar(0x1f) + QString::number(draft.connectionGeneration)
        + QChar(0x1f) + draft.snapshotId;
    return QStringLiteral("import-") + QString::fromLatin1(
        QCryptographicHash::hash(material.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString stableOperationId(const QString &batchId, const TrackerImportRemoteItem &item)
{
    QString material = batchId + QChar(0x1f) + item.providerItemId + QChar(0x1f)
        + trackerProviderKey(item.remote.providerId) + QChar(0x1f) + item.remote.remoteAccountId
        + QChar(0x1f) + item.remote.remoteMediaId + QChar(0x1f);
    if (item.mapping) {
        material += item.mapping->canonical.canonicalMediaId + QChar(0x1f)
            + item.mapping->canonical.historyKind + QChar(0x1f)
            + item.mapping->canonical.historyId;
    }
    material += QChar(0x1f) + QString::number(item.progress) + QChar(0x1f)
        + (item.completed ? QStringLiteral("complete") : QStringLiteral("incomplete"));
    if (item.exactProgressTarget) {
        material += QChar(0x1f) + item.exactProgressTarget->canonicalMediaId + QChar(0x1f)
            + item.exactProgressTarget->kind + QChar(0x1f)
            + item.exactProgressTarget->id + QChar(0x1f)
            + QString::number(item.exactProgressTarget->fraction, 'g', 17) + QChar(0x1f)
            + (item.exactProgressTarget->completed ? QStringLiteral("target-complete")
                                                    : QStringLiteral("target-incomplete"));
    }
    return QStringLiteral("import-op-") + QString::fromLatin1(
        QCryptographicHash::hash(material.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool validRemote(const TrackerRemoteMediaKey &remote)
{
    return !trackerProviderKey(remote.providerId).isEmpty()
        && safeText(remote.remoteAccountId, 128) && safeText(remote.remoteMediaId);
}

bool validMapping(const TrackerTitleMapping &mapping)
{
    return validRemote(mapping.remote) && safeText(mapping.canonical.canonicalMediaId)
        && safeText(mapping.canonical.historyKind, 64) && safeText(mapping.canonical.historyId)
        && safeText(mapping.canonical.displayName) && mapping.revision > 0;
}

bool sameRemote(const TrackerRemoteMediaKey &left, const TrackerRemoteMediaKey &right)
{
    return left.providerId == right.providerId && left.remoteAccountId == right.remoteAccountId
        && left.remoteMediaId == right.remoteMediaId;
}

bool sameMapping(const TrackerTitleMapping &left, const TrackerTitleMapping &right)
{
    return sameRemote(left.remote, right.remote)
        && left.canonical.canonicalMediaId == right.canonical.canonicalMediaId
        && left.canonical.historyKind == right.canonical.historyKind
        && left.canonical.historyId == right.canonical.historyId
        && left.canonical.displayName == right.canonical.displayName
        && left.provenance == right.provenance && left.revision == right.revision;
}

bool sameProgress(const TrackerImportedProgressValue &left,
                  const TrackerImportedProgressValue &right);

bool sameOptionalMapping(const std::optional<TrackerTitleMapping> &left,
                         const std::optional<TrackerTitleMapping> &right)
{
    return left.has_value() == right.has_value()
        && (!left || sameMapping(*left, *right));
}

bool connectionIsCurrent(const TrackerConnectionStore *connections,
                         TrackerProviderId providerId,
                         const QString &remoteAccountId,
                         quint64 generation)
{
    if (!connections || !connections->healthy())
        return false;
    const auto active = connections->connection(providerId);
    return active && active->state == TrackerConnectionState::Connected
        && active->remoteAccountId == remoteAccountId
        && active->connectionGeneration == generation;
}

bool sameOptionalProgress(const std::optional<TrackerImportedProgressValue> &left,
                          const std::optional<TrackerImportedProgressValue> &right)
{
    return left.has_value() == right.has_value()
        && (!left || sameProgress(*left, *right));
}

bool validProgressTarget(const TrackerImportProgressTarget &target)
{
    return safeText(target.canonicalMediaId) && safeText(target.kind, 64) && safeText(target.id)
        && std::isfinite(target.fraction) && target.fraction >= 0.0
        && target.fraction <= 1.0;
}

bool sameProgressTarget(const TrackerImportProgressTarget &left,
                        const TrackerImportProgressTarget &right)
{
    return left.canonicalMediaId == right.canonicalMediaId
        && left.kind == right.kind && left.id == right.id
        && qFuzzyCompare(left.fraction + 1.0, right.fraction + 1.0)
        && left.completed == right.completed;
}

bool sameProgressTargetIdentity(const TrackerImportProgressTarget &left,
                                const TrackerImportProgressTarget &right)
{
    return left.kind == right.kind && left.id == right.id;
}

bool sameOptionalProgressTarget(
    const std::optional<TrackerImportProgressTarget> &left,
    const std::optional<TrackerImportProgressTarget> &right)
{
    return left.has_value() == right.has_value()
        && (!left || sameProgressTarget(*left, *right));
}

bool sameOptionalProgressTargetIdentity(
    const std::optional<TrackerImportProgressTarget> &left,
    const std::optional<TrackerImportProgressTarget> &right)
{
    return left.has_value() == right.has_value()
        && (!left || sameProgressTargetIdentity(*left, *right));
}

bool sameRemoteFactExceptMapping(const TrackerImportRemoteItem &left,
                                 const TrackerImportRemoteItem &right)
{
    const bool mappingChanged = !sameOptionalMapping(left.mapping, right.mapping);
    return left.providerItemId == right.providerItemId && sameRemote(left.remote, right.remote)
        && left.progress == right.progress && left.completed == right.completed
        && left.supported == right.supported && left.duplicate == right.duplicate
        && left.contradictsNativeHistory == right.contradictsNativeHistory
        && (mappingChanged
                || (sameOptionalProgressTargetIdentity(left.exactProgressTarget, right.exactProgressTarget)
                    && sameOptionalProgress(left.localAtPreview, right.localAtPreview)));
}

bool validProgress(const TrackerImportedProgressValue &value)
{
    return safeText(value.canonicalMediaId) && safeText(value.historyKind, 64)
        && safeText(value.historyId) && value.progress >= 0 && value.revision >= 0
        && (value.ownerRevisionToken.isEmpty() || value.ownerRevisionToken.size() == 64)
        && (!value.exactProgressTarget || validProgressTarget(*value.exactProgressTarget));
}

bool sameProgress(const TrackerImportedProgressValue &left,
                  const TrackerImportedProgressValue &right)
{
    return left.canonicalMediaId == right.canonicalMediaId
        && left.historyKind == right.historyKind && left.historyId == right.historyId
        && left.progress == right.progress && left.completed == right.completed
        && left.revision == right.revision
        && left.nativeWitnessedHistory == right.nativeWitnessedHistory
        && left.ownerRevisionToken == right.ownerRevisionToken
        && sameOptionalProgressTarget(left.exactProgressTarget, right.exactProgressTarget);
}

bool sameOwnedProgressSnapshot(const TrackerImportedProgressValue &left,
                               const TrackerImportedProgressValue &right)
{
    // `progress`, `completed`, and native-history witness are provider/evidence
    // facts in this DTO. A native Progress owner can only report the exact local
    // target and its durable revision; do not infer a local edit from fields it
    // cannot authoritatively reconstruct.
    return left.canonicalMediaId == right.canonicalMediaId
        && left.historyKind == right.historyKind && left.historyId == right.historyId
        && left.revision == right.revision
        && left.ownerRevisionToken == right.ownerRevisionToken
        && sameOptionalProgressTarget(left.exactProgressTarget, right.exactProgressTarget);
}

QString classificationKey(TrackerImportClassification value)
{
    switch (value) {
    case TrackerImportClassification::NewProgress: return QStringLiteral("new_progress");
    case TrackerImportClassification::ExactMatch: return QStringLiteral("exact_match");
    case TrackerImportClassification::RemoteAdvance: return QStringLiteral("remote_advance");
    case TrackerImportClassification::Disagreement: return QStringLiteral("disagreement");
    case TrackerImportClassification::NeedsMatching: return QStringLiteral("needs_matching");
    case TrackerImportClassification::Unsupported: return QStringLiteral("unsupported");
    case TrackerImportClassification::Duplicate: return QStringLiteral("duplicate");
    }
    return {};
}

std::optional<TrackerImportClassification> classificationFromKey(const QString &value)
{
    static const QHash<QString, TrackerImportClassification> values{
        {QStringLiteral("new_progress"), TrackerImportClassification::NewProgress},
        {QStringLiteral("exact_match"), TrackerImportClassification::ExactMatch},
        {QStringLiteral("remote_advance"), TrackerImportClassification::RemoteAdvance},
        {QStringLiteral("disagreement"), TrackerImportClassification::Disagreement},
        {QStringLiteral("needs_matching"), TrackerImportClassification::NeedsMatching},
        {QStringLiteral("unsupported"), TrackerImportClassification::Unsupported},
        {QStringLiteral("duplicate"), TrackerImportClassification::Duplicate}};
    const auto it = values.constFind(value);
    return it == values.cend() ? std::nullopt : std::optional<TrackerImportClassification>(*it);
}

QString resolutionKey(TrackerImportResolution value)
{
    switch (value) {
    case TrackerImportResolution::None: return QStringLiteral("none");
    case TrackerImportResolution::UseProviderProgress: return QStringLiteral("use_provider_progress");
    case TrackerImportResolution::KeepColosseum: return QStringLiteral("keep_colosseum");
    case TrackerImportResolution::LeaveUnresolved: return QStringLiteral("leave_unresolved");
    }
    return {};
}

std::optional<TrackerImportResolution> resolutionFromKey(const QString &value)
{
    static const QHash<QString, TrackerImportResolution> values{
        {QStringLiteral("none"), TrackerImportResolution::None},
        {QStringLiteral("use_provider_progress"), TrackerImportResolution::UseProviderProgress},
        {QStringLiteral("keep_colosseum"), TrackerImportResolution::KeepColosseum},
        {QStringLiteral("leave_unresolved"), TrackerImportResolution::LeaveUnresolved}};
    const auto it = values.constFind(value);
    return it == values.cend() ? std::nullopt : std::optional<TrackerImportResolution>(*it);
}

QString stateKey(TrackerImportItemState value)
{
    switch (value) {
    case TrackerImportItemState::ReviewRequired: return QStringLiteral("review_required");
    case TrackerImportItemState::AwaitingApply: return QStringLiteral("awaiting_apply");
    case TrackerImportItemState::Applying: return QStringLiteral("applying");
    case TrackerImportItemState::Applied: return QStringLiteral("applied");
    case TrackerImportItemState::KeptLocal: return QStringLiteral("kept_local");
    case TrackerImportItemState::Unresolved: return QStringLiteral("unresolved");
    case TrackerImportItemState::NonMutating: return QStringLiteral("non_mutating");
    case TrackerImportItemState::Superseded: return QStringLiteral("superseded");
    }
    return {};
}

std::optional<TrackerImportItemState> stateFromKey(const QString &value)
{
    static const QHash<QString, TrackerImportItemState> values{
        {QStringLiteral("review_required"), TrackerImportItemState::ReviewRequired},
        {QStringLiteral("awaiting_apply"), TrackerImportItemState::AwaitingApply},
        {QStringLiteral("applying"), TrackerImportItemState::Applying},
        {QStringLiteral("applied"), TrackerImportItemState::Applied},
        {QStringLiteral("kept_local"), TrackerImportItemState::KeptLocal},
        {QStringLiteral("unresolved"), TrackerImportItemState::Unresolved},
        {QStringLiteral("non_mutating"), TrackerImportItemState::NonMutating},
        {QStringLiteral("superseded"), TrackerImportItemState::Superseded}};
    const auto it = values.constFind(value);
    return it == values.cend() ? std::nullopt : std::optional<TrackerImportItemState>(*it);
}

QJsonObject mappingToJson(const TrackerTitleMapping &mapping)
{
    return {{QStringLiteral("providerId"), trackerProviderKey(mapping.remote.providerId)},
            {QStringLiteral("remoteAccountId"), mapping.remote.remoteAccountId},
            {QStringLiteral("remoteMediaId"), mapping.remote.remoteMediaId},
            {QStringLiteral("canonicalMediaId"), mapping.canonical.canonicalMediaId},
            {QStringLiteral("historyKind"), mapping.canonical.historyKind},
            {QStringLiteral("historyId"), mapping.canonical.historyId},
            {QStringLiteral("displayName"), mapping.canonical.displayName},
            {QStringLiteral("provenance"), mapping.provenance == TrackerMappingProvenance::ExactProviderIdentity
                                               ? QStringLiteral("exact") : QStringLiteral("user")},
            {QStringLiteral("revision"), QString::number(mapping.revision)}};
}

std::optional<TrackerTitleMapping> mappingFromJson(const QJsonObject &object)
{
    const auto provider = trackerProviderIdFromKey(object.value(QStringLiteral("providerId")).toString());
    const QString provenance = object.value(QStringLiteral("provenance")).toString();
    bool revisionOk = false;
    const quint64 revision = object.value(QStringLiteral("revision")).toString().toULongLong(&revisionOk);
    if (!provider || !revisionOk || (provenance != QLatin1String("exact") && provenance != QLatin1String("user")))
        return std::nullopt;
    TrackerTitleMapping mapping{{*provider,
                                 object.value(QStringLiteral("remoteAccountId")).toString(),
                                 object.value(QStringLiteral("remoteMediaId")).toString()},
                                {object.value(QStringLiteral("canonicalMediaId")).toString(),
                                 object.value(QStringLiteral("historyKind")).toString(),
                                 object.value(QStringLiteral("historyId")).toString(),
                                 object.value(QStringLiteral("displayName")).toString()},
                                provenance == QLatin1String("exact")
                                    ? TrackerMappingProvenance::ExactProviderIdentity
                                    : TrackerMappingProvenance::UserConfirmed,
                                revision};
    return validMapping(mapping) ? std::optional<TrackerTitleMapping>(mapping) : std::nullopt;
}

QJsonObject progressToJson(const TrackerImportedProgressValue &value)
{
    QJsonObject object{{QStringLiteral("canonicalMediaId"), value.canonicalMediaId},
                       {QStringLiteral("historyKind"), value.historyKind},
                       {QStringLiteral("historyId"), value.historyId},
                       {QStringLiteral("progress"), value.progress},
                       {QStringLiteral("completed"), value.completed},
                       {QStringLiteral("revision"), QString::number(value.revision)},
                       {QStringLiteral("nativeWitnessedHistory"), value.nativeWitnessedHistory},
                       {QStringLiteral("ownerRevisionToken"), value.ownerRevisionToken}};
    if (value.exactProgressTarget) {
        object.insert(QStringLiteral("exactProgressTarget"), QJsonObject{
            {QStringLiteral("canonicalMediaId"), value.exactProgressTarget->canonicalMediaId},
            {QStringLiteral("kind"), value.exactProgressTarget->kind},
            {QStringLiteral("id"), value.exactProgressTarget->id},
            {QStringLiteral("fraction"), value.exactProgressTarget->fraction},
            {QStringLiteral("completed"), value.exactProgressTarget->completed}});
    }
    return object;
}

std::optional<TrackerImportProgressTarget> progressTargetFromJson(const QJsonValue &value)
{
    if (!value.isObject())
        return std::nullopt;
    const QJsonObject object = value.toObject();
    const QJsonValue fraction = object.value(QStringLiteral("fraction"));
    if (!fraction.isDouble())
        return std::nullopt;
    TrackerImportProgressTarget target{object.value(QStringLiteral("canonicalMediaId")).toString(),
                                       object.value(QStringLiteral("kind")).toString(),
                                       object.value(QStringLiteral("id")).toString(),
                                       fraction.toDouble(),
                                       object.value(QStringLiteral("completed")).toBool()};
    return validProgressTarget(target)
        ? std::optional<TrackerImportProgressTarget>(target) : std::nullopt;
}

std::optional<TrackerImportedProgressValue> progressFromJson(const QJsonObject &object)
{
    bool revisionOk = false;
    const qint64 revision = object.value(QStringLiteral("revision")).toString().toLongLong(&revisionOk);
    const QJsonValue progress = object.value(QStringLiteral("progress"));
    if (!revisionOk || !progress.isDouble())
        return std::nullopt;
    const double rawProgress = progress.toDouble();
    if (rawProgress < 0 || std::floor(rawProgress) != rawProgress || rawProgress > std::numeric_limits<int>::max())
        return std::nullopt;
    TrackerImportedProgressValue value{object.value(QStringLiteral("canonicalMediaId")).toString(),
                                       object.value(QStringLiteral("historyKind")).toString(),
                                       object.value(QStringLiteral("historyId")).toString(),
                                       static_cast<int>(rawProgress),
                                       object.value(QStringLiteral("completed")).toBool(), revision,
                                       object.value(QStringLiteral("nativeWitnessedHistory")).toBool()};
    value.ownerRevisionToken = object.value(QStringLiteral("ownerRevisionToken")).toString();
    if (object.contains(QStringLiteral("exactProgressTarget"))) {
        value.exactProgressTarget = progressTargetFromJson(
            object.value(QStringLiteral("exactProgressTarget")));
        if (!value.exactProgressTarget)
            return std::nullopt;
    }
    return validProgress(value) ? std::optional<TrackerImportedProgressValue>(value) : std::nullopt;
}

TrackerImportClassification classify(const TrackerImportRemoteItem &item)
{
    if (!item.supported)
        return TrackerImportClassification::Unsupported;
    if (!item.mapping)
        return TrackerImportClassification::NeedsMatching;
    if (item.duplicate)
        return TrackerImportClassification::Duplicate;
    if (!item.exactProgressTarget || !validProgressTarget(*item.exactProgressTarget))
        return TrackerImportClassification::Unsupported;
    if (!item.localAtPreview)
        return TrackerImportClassification::NewProgress;
    const TrackerImportedProgressValue &local = *item.localAtPreview;
    if (!local.exactProgressTarget
        || !sameProgressTargetIdentity(*local.exactProgressTarget, *item.exactProgressTarget)) {
        return TrackerImportClassification::Disagreement;
    }
    if (qFuzzyCompare(local.exactProgressTarget->fraction + 1.0,
                      item.exactProgressTarget->fraction + 1.0)
        && local.exactProgressTarget->completed == item.exactProgressTarget->completed) {
        return TrackerImportClassification::ExactMatch;
    }
    const bool fractionDidNotRegress =
        item.exactProgressTarget->fraction >= local.exactProgressTarget->fraction;
    const bool completionDidNotRegress =
        !local.exactProgressTarget->completed || item.exactProgressTarget->completed;
    const bool strictlyAdvanced =
        item.exactProgressTarget->fraction > local.exactProgressTarget->fraction
        || (item.exactProgressTarget->completed && !local.exactProgressTarget->completed);
    const bool remoteAdvance = fractionDidNotRegress
        && completionDidNotRegress && strictlyAdvanced;
    return remoteAdvance && !item.contradictsNativeHistory
        ? TrackerImportClassification::RemoteAdvance
        : TrackerImportClassification::Disagreement;
}

TrackerImportItemState initialState(TrackerImportClassification classification)
{
    return classification == TrackerImportClassification::ExactMatch
            || classification == TrackerImportClassification::Unsupported
            || classification == TrackerImportClassification::Duplicate
        ? TrackerImportItemState::NonMutating : TrackerImportItemState::ReviewRequired;
}

bool resolutionAllowed(TrackerImportClassification classification, TrackerImportResolution resolution)
{
    switch (classification) {
    case TrackerImportClassification::NewProgress:
    case TrackerImportClassification::RemoteAdvance:
        return resolution == TrackerImportResolution::UseProviderProgress
            || resolution == TrackerImportResolution::LeaveUnresolved;
    case TrackerImportClassification::Disagreement:
        return resolution == TrackerImportResolution::UseProviderProgress
            || resolution == TrackerImportResolution::KeepColosseum
            || resolution == TrackerImportResolution::LeaveUnresolved;
    case TrackerImportClassification::NeedsMatching:
        return resolution == TrackerImportResolution::LeaveUnresolved;
    case TrackerImportClassification::ExactMatch:
    case TrackerImportClassification::Unsupported:
    case TrackerImportClassification::Duplicate:
        return false;
    }
    return false;
}

bool stateResolutionValid(const TrackerImportItem &item)
{
    switch (item.state) {
    case TrackerImportItemState::ReviewRequired:
        return item.resolution == TrackerImportResolution::None;
    case TrackerImportItemState::AwaitingApply:
    case TrackerImportItemState::Applying:
    case TrackerImportItemState::Applied:
        return item.resolution == TrackerImportResolution::UseProviderProgress
            && resolutionAllowed(item.classification, item.resolution);
    case TrackerImportItemState::KeptLocal:
        return item.resolution == TrackerImportResolution::KeepColosseum
            && resolutionAllowed(item.classification, item.resolution);
    case TrackerImportItemState::Unresolved:
        return item.resolution == TrackerImportResolution::LeaveUnresolved
            && resolutionAllowed(item.classification, item.resolution);
    case TrackerImportItemState::NonMutating:
        return item.resolution == TrackerImportResolution::None
            && (item.classification == TrackerImportClassification::ExactMatch
                || item.classification == TrackerImportClassification::Unsupported
                || item.classification == TrackerImportClassification::Duplicate);
    case TrackerImportItemState::Superseded:
        return item.resolution == TrackerImportResolution::LeaveUnresolved;
    }
    return false;
}

bool resolved(const TrackerImportItem &item)
{
    return item.state == TrackerImportItemState::Applied
        || item.state == TrackerImportItemState::KeptLocal
        || item.state == TrackerImportItemState::Unresolved
        || item.state == TrackerImportItemState::NonMutating
        || item.state == TrackerImportItemState::Superseded;
}

bool hasSettledInitialImport(const QList<TrackerImportBatch> &batches,
                             TrackerProviderId providerId,
                             const QString &remoteAccountId)
{
    return std::any_of(batches.cbegin(), batches.cend(), [providerId, &remoteAccountId](const TrackerImportBatch &batch) {
        return batch.initialImport && batch.confirmed && batch.pageComplete && batch.cursorCommitted
            && batch.providerId == providerId && batch.remoteAccountId == remoteAccountId;
    });
}

struct PreviousImportCheckpoint {
    int remoteProgress = 0;
    bool remoteCompleted = false;
    std::optional<TrackerImportedProgressValue> localProgress;
    bool unresolved = false;
};

std::optional<PreviousImportCheckpoint> previousCheckpoint(
    const QList<TrackerImportBatch> &batches,
    const TrackerRemoteMediaKey &remote)
{
    for (auto batchIt = batches.crbegin(); batchIt != batches.crend(); ++batchIt) {
        if (!batchIt->confirmed || !batchIt->cursorCommitted
            || batchIt->providerId != remote.providerId
            || batchIt->remoteAccountId != remote.remoteAccountId)
            continue;
        for (auto itemIt = batchIt->items.crbegin(); itemIt != batchIt->items.crend(); ++itemIt) {
            if (!sameRemote(itemIt->remote.remote, remote))
                continue;
            if (itemIt->state == TrackerImportItemState::Unresolved
                || itemIt->state == TrackerImportItemState::Superseded)
                return PreviousImportCheckpoint{itemIt->remote.progress,
                                                itemIt->remote.completed,
                                                std::nullopt, true};
            const bool usable = itemIt->state == TrackerImportItemState::Applied
                || itemIt->state == TrackerImportItemState::KeptLocal
                || (itemIt->state == TrackerImportItemState::NonMutating
                    && itemIt->classification == TrackerImportClassification::ExactMatch);
            if (usable && itemIt->localBaselineCaptured) {
                return PreviousImportCheckpoint{itemIt->remote.progress,
                                                itemIt->remote.completed,
                                                itemIt->localAfterSettlement, false};
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

TrackerImportClassification classifyAgainstSettledState(
    const TrackerImportRemoteItem &item,
    bool initialImport,
    const QList<TrackerImportBatch> &batches)
{
    TrackerImportClassification classification = classify(item);
    if (initialImport || classification == TrackerImportClassification::Unsupported
        || classification == TrackerImportClassification::NeedsMatching
        || classification == TrackerImportClassification::Duplicate)
        return classification;
    const auto previous = previousCheckpoint(batches, item.remote);
    if (!previous)
        return classification;
    if (previous->unresolved)
        return TrackerImportClassification::Disagreement;
    const bool providerChanged = previous->remoteProgress != item.progress
        || previous->remoteCompleted != item.completed;
    const bool localChanged = previous->localProgress.has_value()
        != item.localAtPreview.has_value()
        || (previous->localProgress && item.localAtPreview
            && !sameOwnedProgressSnapshot(*previous->localProgress, *item.localAtPreview));
    if (providerChanged && localChanged)
        return TrackerImportClassification::Disagreement;
    return classification;
}

bool validDraft(const TrackerImportBatchDraft &draft)
{
    if (trackerProviderKey(draft.providerId).isEmpty() || !safeText(draft.remoteAccountId, 128)
        || draft.connectionGeneration == 0 || !safeText(draft.snapshotId)
        || (!draft.baseCursor.isEmpty() && !safeText(draft.baseCursor))
        || !safeText(draft.proposedCursor)) {
        return false;
    }
    QSet<QString> ids;
    for (const TrackerImportRemoteItem &item : draft.items) {
        if (!safeText(item.providerItemId) || !validRemote(item.remote)
            || item.remote.providerId != draft.providerId
            || item.remote.remoteAccountId != draft.remoteAccountId || item.progress < 0
            || !safeDisplayTitle(item.displayTitle)
            || ids.contains(item.providerItemId)) {
            return false;
        }
        ids.insert(item.providerItemId);
        if (item.mapping && (!validMapping(*item.mapping) || !sameRemote(item.remote, item.mapping->remote)))
            return false;
        if (item.exactProgressTarget
            && (!validProgressTarget(*item.exactProgressTarget)
                || (item.mapping && item.exactProgressTarget->canonicalMediaId
                    != item.mapping->canonical.canonicalMediaId))) {
            return false;
        }
        if (item.localAtPreview && (!validProgress(*item.localAtPreview)
                                    || (item.mapping && (item.localAtPreview->canonicalMediaId
                                        != item.mapping->canonical.canonicalMediaId
                                        || item.localAtPreview->historyKind != item.mapping->canonical.historyKind
                                        || item.localAtPreview->historyId != item.mapping->canonical.historyId
                                        || (item.localAtPreview->exactProgressTarget
                                            && item.exactProgressTarget
                                            && !sameProgressTargetIdentity(
                                                *item.localAtPreview->exactProgressTarget,
                                                *item.exactProgressTarget)))))) {
            return false;
        }
    }
    return true;
}

QJsonObject itemToJson(const TrackerImportItem &item)
{
    QJsonObject object{{QStringLiteral("itemId"), item.itemId},
                       {QStringLiteral("operationId"), item.operationId},
                       {QStringLiteral("providerItemId"), item.remote.providerItemId},
                       {QStringLiteral("providerId"), trackerProviderKey(item.remote.remote.providerId)},
                       {QStringLiteral("remoteAccountId"), item.remote.remote.remoteAccountId},
                       {QStringLiteral("remoteMediaId"), item.remote.remote.remoteMediaId},
                       {QStringLiteral("progress"), item.remote.progress},
                       {QStringLiteral("completed"), item.remote.completed},
                       {QStringLiteral("supported"), item.remote.supported},
                       {QStringLiteral("displayTitle"), item.remote.displayTitle},
                       {QStringLiteral("duplicate"), item.remote.duplicate},
                       {QStringLiteral("contradictsNativeHistory"), item.remote.contradictsNativeHistory},
                       {QStringLiteral("classification"), classificationKey(item.classification)},
                       {QStringLiteral("resolution"), resolutionKey(item.resolution)},
                       {QStringLiteral("state"), stateKey(item.state)},
                       {QStringLiteral("localBaselineCaptured"), item.localBaselineCaptured}};
    if (item.remote.exactProgressTarget) {
        object.insert(QStringLiteral("exactProgressTarget"), QJsonObject{
            {QStringLiteral("canonicalMediaId"), item.remote.exactProgressTarget->canonicalMediaId},
            {QStringLiteral("kind"), item.remote.exactProgressTarget->kind},
            {QStringLiteral("id"), item.remote.exactProgressTarget->id},
            {QStringLiteral("fraction"), item.remote.exactProgressTarget->fraction},
            {QStringLiteral("completed"), item.remote.exactProgressTarget->completed}});
    }
    if (item.remote.mapping)
        object.insert(QStringLiteral("mapping"), mappingToJson(*item.remote.mapping));
    if (item.remote.localAtPreview)
        object.insert(QStringLiteral("localAtPreview"), progressToJson(*item.remote.localAtPreview));
    if (item.localAfterSettlement)
        object.insert(QStringLiteral("localAfterSettlement"), progressToJson(*item.localAfterSettlement));
    return object;
}

std::optional<TrackerImportItem> itemFromJson(const QJsonObject &object)
{
    const auto provider = trackerProviderIdFromKey(object.value(QStringLiteral("providerId")).toString());
    const auto classification = classificationFromKey(object.value(QStringLiteral("classification")).toString());
    const auto resolution = resolutionFromKey(object.value(QStringLiteral("resolution")).toString());
    const auto state = stateFromKey(object.value(QStringLiteral("state")).toString());
    const QJsonValue progress = object.value(QStringLiteral("progress"));
    if (!provider || !classification || !resolution || !state || !progress.isDouble())
        return std::nullopt;
    const double rawProgress = progress.toDouble();
    if (rawProgress < 0 || std::floor(rawProgress) != rawProgress || rawProgress > std::numeric_limits<int>::max())
        return std::nullopt;
    TrackerImportRemoteItem remote{object.value(QStringLiteral("providerItemId")).toString(),
                                   {*provider, object.value(QStringLiteral("remoteAccountId")).toString(),
                                    object.value(QStringLiteral("remoteMediaId")).toString()},
                                   std::nullopt, static_cast<int>(rawProgress),
                                   object.value(QStringLiteral("completed")).toBool(),
                                   object.value(QStringLiteral("supported")).toBool(),
                                   object.value(QStringLiteral("duplicate")).toBool(),
                                   object.value(QStringLiteral("contradictsNativeHistory")).toBool(),
                                   std::nullopt,
                                   object.value(QStringLiteral("displayTitle")).toString()};
    if (object.contains(QStringLiteral("displayTitle"))
        && !object.value(QStringLiteral("displayTitle")).isString()) {
        return std::nullopt;
    }
    if (object.contains(QStringLiteral("exactProgressTarget"))) {
        remote.exactProgressTarget = progressTargetFromJson(
            object.value(QStringLiteral("exactProgressTarget")));
        if (!remote.exactProgressTarget)
            return std::nullopt;
    }
    if (object.contains(QStringLiteral("mapping"))) {
        const auto mapping = object.value(QStringLiteral("mapping")).isObject()
            ? mappingFromJson(object.value(QStringLiteral("mapping")).toObject()) : std::nullopt;
        if (!mapping)
            return std::nullopt;
        remote.mapping = *mapping;
    }
    if (object.contains(QStringLiteral("localAtPreview"))) {
        const auto local = object.value(QStringLiteral("localAtPreview")).isObject()
            ? progressFromJson(object.value(QStringLiteral("localAtPreview")).toObject()) : std::nullopt;
        if (!local)
            return std::nullopt;
        remote.localAtPreview = *local;
    }
    std::optional<TrackerImportedProgressValue> localAfterSettlement;
    if (object.contains(QStringLiteral("localAfterSettlement"))) {
        const auto local = object.value(QStringLiteral("localAfterSettlement")).isObject()
            ? progressFromJson(object.value(QStringLiteral("localAfterSettlement")).toObject()) : std::nullopt;
        if (!local)
            return std::nullopt;
        localAfterSettlement = *local;
    }
    TrackerImportItem item{object.value(QStringLiteral("itemId")).toString(),
                           object.value(QStringLiteral("operationId")).toString(), remote,
                           *classification, *resolution, *state, localAfterSettlement,
                           object.value(QStringLiteral("localBaselineCaptured")).toBool()};
    if (!stateResolutionValid(item))
        return std::nullopt;
    if ((localAfterSettlement && !item.localBaselineCaptured)
        || (item.localBaselineCaptured && !remote.mapping))
        return std::nullopt;
    if (localAfterSettlement && remote.mapping
        && (localAfterSettlement->canonicalMediaId != remote.mapping->canonical.canonicalMediaId
            || localAfterSettlement->historyKind != remote.mapping->canonical.historyKind
            || localAfterSettlement->historyId != remote.mapping->canonical.historyId))
        return std::nullopt;
    return safeText(item.itemId) && safeText(item.operationId) && validDraft({remote.remote.providerId,
                  remote.remote.remoteAccountId, 1, QStringLiteral("snapshot"), QStringLiteral("cursor"),
                  true, true, {remote}}) ? std::optional<TrackerImportItem>(item) : std::nullopt;
}

QJsonObject batchToJson(const TrackerImportBatch &batch)
{
    QJsonArray items;
    for (const TrackerImportItem &item : batch.items)
        items.append(itemToJson(item));
    return {{QStringLiteral("batchId"), batch.batchId},
            {QStringLiteral("providerId"), trackerProviderKey(batch.providerId)},
            {QStringLiteral("remoteAccountId"), batch.remoteAccountId},
            {QStringLiteral("connectionGeneration"), QString::number(batch.connectionGeneration)},
            {QStringLiteral("snapshotId"), batch.snapshotId},
            {QStringLiteral("proposedCursor"), batch.proposedCursor},
            {QStringLiteral("baseCursor"), batch.baseCursor},
            {QStringLiteral("initialImport"), batch.initialImport},
            {QStringLiteral("pageComplete"), batch.pageComplete},
            {QStringLiteral("confirmed"), batch.confirmed},
            {QStringLiteral("cursorCommitted"), batch.cursorCommitted},
            {QStringLiteral("items"), items}};
}

std::optional<TrackerImportBatch> batchFromJson(const QJsonObject &object)
{
    const auto provider = trackerProviderIdFromKey(object.value(QStringLiteral("providerId")).toString());
    bool generationOk = false;
    const quint64 generation = object.value(QStringLiteral("connectionGeneration")).toString().toULongLong(&generationOk);
    if (!provider || !generationOk || generation == 0 || !object.value(QStringLiteral("items")).isArray())
        return std::nullopt;
    TrackerImportBatch batch{object.value(QStringLiteral("batchId")).toString(), *provider,
                             object.value(QStringLiteral("remoteAccountId")).toString(), generation,
                             object.value(QStringLiteral("snapshotId")).toString(),
                             object.value(QStringLiteral("proposedCursor")).toString(),
                             object.value(QStringLiteral("initialImport")).toBool(),
                             object.value(QStringLiteral("pageComplete")).toBool(),
                             object.value(QStringLiteral("confirmed")).toBool(), {},
                             object.value(QStringLiteral("baseCursor")).toString(),
                             object.value(QStringLiteral("cursorCommitted")).toBool()};
    if (!safeText(batch.batchId) || !safeText(batch.remoteAccountId, 128)
        || !safeText(batch.snapshotId) || !safeText(batch.proposedCursor)
        || (!batch.baseCursor.isEmpty() && !safeText(batch.baseCursor))) {
        return std::nullopt;
    }
    QSet<QString> ids;
    for (const QJsonValue &value : object.value(QStringLiteral("items")).toArray()) {
        const auto item = value.isObject() ? itemFromJson(value.toObject()) : std::nullopt;
        if (!item || ids.contains(item->itemId) || ids.contains(item->remote.providerItemId))
            return std::nullopt;
        ids.insert(item->itemId);
        ids.insert(item->remote.providerItemId);
        if (item->remote.remote.providerId != batch.providerId
            || item->remote.remote.remoteAccountId != batch.remoteAccountId
            || item->operationId != stableOperationId(batch.batchId, item->remote)) {
            return std::nullopt;
        }
        batch.items.append(*item);
    }
    return batch;
}

} // namespace

TrackerImportStore::TrackerImportStore(const ProfilePaths &profile,
                                       const TrackerMappingStore *mappings,
                                       const TrackerConnectionStore *connections)
    : m_profile(profile), m_mappings(mappings), m_connections(connections),
      m_path(storagePath(profile))
{
    if (!m_mappings || !m_mappings->healthy() || !m_connections || !m_connections->healthy()) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker imports need healthy profile mapping and connection stores.");
    } else if (m_path.isEmpty()) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker imports are unavailable for this profile.");
    } else {
        load();
    }
}

QString TrackerImportStore::storagePath(const ProfilePaths &profile)
{
    if (profile.kind() == ProfilePaths::Kind::Sealed
        || profile.kind() == ProfilePaths::Kind::LegacyLocal || profile.profileRoot().isEmpty()) {
        return {};
    }
    return QDir::cleanPath(profile.profileRoot() + QLatin1Char('/') + QLatin1String(kFileName));
}

bool TrackerImportStore::healthy(QString *out) const
{
    if (!m_healthy && out)
        *out = m_error;
    return m_healthy;
}

std::optional<TrackerImportBatch> TrackerImportStore::createPreview(const TrackerImportBatchDraft &draft,
                                                                      QString *out)
{
    if (!healthy(out) || !validDraft(draft)) {
        setError(out, QStringLiteral("Tracker import preview is invalid."));
        return std::nullopt;
    }
    if (!connectionIsCurrent(m_connections, draft.providerId, draft.remoteAccountId,
                             draft.connectionGeneration)) {
        setError(out, QStringLiteral("Tracker import preview belongs to a stale connection generation."));
        return std::nullopt;
    }
    for (const TrackerImportRemoteItem &item : draft.items) {
        if (!item.mapping)
            continue;
        const auto current = m_mappings->mapping(item.remote);
        if (!current || !sameMapping(*current, *item.mapping)) {
            setError(out, QStringLiteral("Tracker import preview does not match the current confirmed title mapping."));
            return std::nullopt;
        }
    }
    const QString batchId = stableBatchId(draft);
    if (const auto existing = batch(batchId)) {
        // A provider snapshot is immutable. The only permitted revalidation
        // change is a newly confirmed local mapping for an item still in
        // review; it clears its old decision and recomputes classification.
        if (existing->providerId != draft.providerId
            || existing->remoteAccountId != draft.remoteAccountId
            || existing->connectionGeneration != draft.connectionGeneration
            || existing->snapshotId != draft.snapshotId
            || existing->baseCursor != draft.baseCursor
            || existing->proposedCursor != draft.proposedCursor
            || existing->initialImport != draft.initialImport
            || existing->pageComplete != draft.pageComplete
            || existing->items.size() != draft.items.size()) {
            setError(out, QStringLiteral("Tracker import snapshot changed after preview."));
            return std::nullopt;
        }
        TrackerImportBatch refreshed = *existing;
        bool changed = false;
        for (const TrackerImportRemoteItem &fresh : draft.items) {
            auto old = std::find_if(refreshed.items.begin(), refreshed.items.end(), [&fresh](const TrackerImportItem &item) {
                return item.itemId == fresh.providerItemId;
            });
            if (old == refreshed.items.end() || !sameRemoteFactExceptMapping(old->remote, fresh)) {
                setError(out, QStringLiteral("Tracker import snapshot changed after preview."));
                return std::nullopt;
            }
            if (sameOptionalMapping(old->remote.mapping, fresh.mapping))
                continue;
            if (existing->confirmed) {
                setError(out, QStringLiteral("Tracker title mapping changed after import confirmation."));
                return std::nullopt;
            }
            old->remote = fresh;
            old->operationId = stableOperationId(batchId, fresh);
            old->classification = classify(fresh);
            old->resolution = TrackerImportResolution::None;
            old->state = initialState(old->classification);
            old->localAfterSettlement.reset();
            old->localBaselineCaptured = false;
            changed = true;
        }
        if (!changed)
            return existing;
        if (!commitBatch(refreshed, m_cursors, out))
            return std::nullopt;
        return batch(batchId);
    }
    TrackerImportBatch result;
    result.batchId = batchId;
    result.providerId = draft.providerId;
    result.remoteAccountId = draft.remoteAccountId;
    result.connectionGeneration = draft.connectionGeneration;
    result.snapshotId = draft.snapshotId;
    result.proposedCursor = draft.proposedCursor;
    result.initialImport = draft.initialImport;
    result.pageComplete = draft.pageComplete;
    result.baseCursor = draft.baseCursor;
    for (const TrackerImportRemoteItem &remote : draft.items) {
        const TrackerImportClassification classification = classifyAgainstSettledState(
            remote, draft.initialImport, m_batches);
        TrackerImportItem newItem{remote.providerItemId,
                                  stableOperationId(result.batchId, remote),
                                  remote, classification, TrackerImportResolution::None,
                                  initialState(classification)};
        if (classification == TrackerImportClassification::ExactMatch && remote.localAtPreview) {
            newItem.localAfterSettlement = remote.localAtPreview;
            newItem.localBaselineCaptured = true;
        }
        result.items.append(newItem);
    }
    QList<TrackerImportBatch> next = m_batches;
    // A newly fetched page can move an unresolved decision forward, but never
    // applies the old payload. Supersession is committed with that fresh page.
    if (!draft.initialImport && draft.pageComplete
        && cursorIs(m_cursors, draft.providerId, draft.remoteAccountId, draft.baseCursor)) {
        for (TrackerImportBatch &older : next) {
            if (!older.confirmed || !older.cursorCommitted
                || older.providerId != draft.providerId
                || older.remoteAccountId != draft.remoteAccountId)
                continue;
            for (TrackerImportItem &oldItem : older.items) {
                if (oldItem.state != TrackerImportItemState::Unresolved)
                    continue;
                const auto fresh = std::find_if(result.items.cbegin(), result.items.cend(), [&oldItem](const TrackerImportItem &candidate) {
                    return candidate.remote.mapping
                        && sameRemote(candidate.remote.remote, oldItem.remote.remote);
                });
                if (fresh != result.items.cend())
                    oldItem.state = TrackerImportItemState::Superseded;
            }
        }
    }
    next.append(result);
    if (!persist(next, m_cursors, out))
        return std::nullopt;
    m_batches = next;
    return result;
}

std::optional<TrackerImportBatch> TrackerImportStore::batch(const QString &batchId) const
{
    for (const TrackerImportBatch &entry : m_batches) {
        if (entry.batchId == batchId)
            return entry;
    }
    return std::nullopt;
}

QList<TrackerImportBatch> TrackerImportStore::batches() const { return m_batches; }

bool TrackerImportStore::adoptPrivateStateFrom(const TrackerImportStore &source,
                                                QString *out)
{
    if (!healthy(out) || !source.healthy(out) || this == &source
        || m_profile.profileId() == source.m_profile.profileId()) {
        return setError(out, QStringLiteral("Tracker import state cannot be adopted between these profiles."));
    }

    QList<TrackerImportBatch> candidate = m_batches;
    QHash<QString, QString> cursors = m_cursors;
    bool changed = false;
    for (const TrackerImportBatch &batch : source.m_batches) {
        const auto duplicate = std::find_if(candidate.cbegin(), candidate.cend(),
            [&batch](const TrackerImportBatch &entry) { return entry.batchId == batch.batchId; });
        if (duplicate != candidate.cend()) {
            if (batchToJson(*duplicate) != batchToJson(batch)) {
                return setError(out, QStringLiteral(
                    "The destination has a different review for this tracker import batch."));
            }
            continue;
        }
        candidate.append(batch);
        changed = true;
    }
    for (auto it = source.m_cursors.cbegin(); it != source.m_cursors.cend(); ++it) {
        const auto existing = cursors.constFind(it.key());
        if (existing != cursors.cend()) {
            if (existing.value() != it.value()) {
                return setError(out, QStringLiteral(
                    "The destination has a different confirmed tracker import cursor."));
            }
            continue;
        }
        cursors.insert(it.key(), it.value());
        changed = true;
    }
    if (!changed)
        return true;
    if (!persist(candidate, cursors, out))
        return false;
    m_batches = candidate;
    m_cursors = cursors;
    return true;
}

bool TrackerImportStore::resolve(const QString &batchId, const QString &itemId,
                                 TrackerImportResolution resolution, QString *out)
{
    if (!healthy(out) || !safeText(batchId) || !safeText(itemId)
        || resolution == TrackerImportResolution::None) {
        return setError(out, QStringLiteral("Tracker import resolution is invalid."));
    }
    QList<TrackerImportBatch> next = m_batches;
    auto batchIt = std::find_if(next.begin(), next.end(), [&batchId](const TrackerImportBatch &batch) {
        return batch.batchId == batchId;
    });
    if (batchIt == next.end())
        return setError(out, QStringLiteral("Tracker import preview is no longer reviewable."));
    if (!connectionIsCurrent(m_connections, batchIt->providerId, batchIt->remoteAccountId,
                             batchIt->connectionGeneration))
        return setError(out, QStringLiteral("Tracker import preview belongs to a stale connection generation."));
    auto itemIt = std::find_if(batchIt->items.begin(), batchIt->items.end(), [&itemId](const TrackerImportItem &item) {
        return item.itemId == itemId;
    });
    const bool previouslyConfirmedQueue = batchIt->confirmed
        && itemIt != batchIt->items.end()
        && itemIt->state == TrackerImportItemState::Unresolved;
    if (itemIt == batchIt->items.end()
        || (batchIt->confirmed && !previouslyConfirmedQueue)
        || (itemIt->state != TrackerImportItemState::ReviewRequired
            && itemIt->state != TrackerImportItemState::Unresolved)
        || !resolutionAllowed(itemIt->classification, resolution)) {
        return setError(out, QStringLiteral("Tracker import choice is not available for this item."));
    }
    if (previouslyConfirmedQueue && resolution == TrackerImportResolution::UseProviderProgress) {
        return setError(out, QStringLiteral("A fresh tracker snapshot is required before applying queued progress."));
    }
    itemIt->resolution = resolution;
    itemIt->localAfterSettlement.reset();
    itemIt->localBaselineCaptured = false;
    itemIt->state = resolution == TrackerImportResolution::UseProviderProgress
        ? TrackerImportItemState::AwaitingApply
        : resolution == TrackerImportResolution::KeepColosseum
            ? TrackerImportItemState::KeptLocal : TrackerImportItemState::Unresolved;
    if (!persist(next, m_cursors, out))
        return false;
    m_batches = next;
    return true;
}

bool TrackerImportStore::resolveSelected(const QString &batchId,
                                         const QStringList &itemIds,
                                         TrackerImportResolution resolution,
                                         QString *out)
{
    if (!healthy(out) || !safeText(batchId) || itemIds.isEmpty()
        || resolution == TrackerImportResolution::None) {
        return setError(out, QStringLiteral("Tracker import group resolution is invalid."));
    }
    QSet<QString> selected;
    for (const QString &itemId : itemIds) {
        if (!safeText(itemId) || selected.contains(itemId))
            return setError(out, QStringLiteral("Tracker import group contains an invalid or duplicate item."));
        selected.insert(itemId);
    }

    QList<TrackerImportBatch> next = m_batches;
    auto batchIt = std::find_if(next.begin(), next.end(), [&batchId](const TrackerImportBatch &batch) {
        return batch.batchId == batchId;
    });
    if (batchIt == next.end())
        return setError(out, QStringLiteral("Tracker import preview is no longer reviewable."));
    if (!connectionIsCurrent(m_connections, batchIt->providerId, batchIt->remoteAccountId,
                             batchIt->connectionGeneration)) {
        return setError(out, QStringLiteral("Tracker import preview belongs to a stale connection generation."));
    }

    for (const QString &itemId : selected) {
        const auto item = std::find_if(batchIt->items.cbegin(), batchIt->items.cend(),
            [&itemId](const TrackerImportItem &entry) { return entry.itemId == itemId; });
        const bool previouslyConfirmedQueue = batchIt->confirmed
            && item != batchIt->items.cend()
            && item->state == TrackerImportItemState::Unresolved;
        if (item == batchIt->items.cend()
            || (batchIt->confirmed && !previouslyConfirmedQueue)
            || (item->state != TrackerImportItemState::ReviewRequired
                && item->state != TrackerImportItemState::Unresolved)
            || !resolutionAllowed(item->classification, resolution)) {
            return setError(out, QStringLiteral("The selected tracker items do not all allow this choice."));
        }
        if (previouslyConfirmedQueue
            && resolution == TrackerImportResolution::UseProviderProgress) {
            return setError(out, QStringLiteral("A fresh tracker snapshot is required before applying queued progress."));
        }
    }

    for (TrackerImportItem &item : batchIt->items) {
        if (!selected.contains(item.itemId))
            continue;
        item.resolution = resolution;
        item.localAfterSettlement.reset();
        item.localBaselineCaptured = false;
        item.state = resolution == TrackerImportResolution::UseProviderProgress
            ? TrackerImportItemState::AwaitingApply
            : resolution == TrackerImportResolution::KeepColosseum
                ? TrackerImportItemState::KeptLocal : TrackerImportItemState::Unresolved;
    }
    if (!persist(next, m_cursors, out))
        return false;
    m_batches = next;
    return true;
}

bool TrackerImportStore::resolveAll(const QString &batchId,
                                    TrackerImportClassification classification,
                                    TrackerImportResolution resolution,
                                    QString *out)
{
    if (!healthy(out) || !safeText(batchId) || resolution == TrackerImportResolution::None)
        return setError(out, QStringLiteral("Tracker import bulk resolution is invalid."));
    QList<TrackerImportBatch> next = m_batches;
    auto batchIt = std::find_if(next.begin(), next.end(), [&batchId](const TrackerImportBatch &entry) {
        return entry.batchId == batchId;
    });
    if (batchIt == next.end())
        return setError(out, QStringLiteral("Tracker import preview is no longer reviewable."));
    if (!connectionIsCurrent(m_connections, batchIt->providerId, batchIt->remoteAccountId,
                             batchIt->connectionGeneration))
        return setError(out, QStringLiteral("Tracker import preview belongs to a stale connection generation."));
    int changed = 0;
    if (batchIt->confirmed && resolution == TrackerImportResolution::UseProviderProgress
        && std::any_of(batchIt->items.cbegin(), batchIt->items.cend(), [classification](const TrackerImportItem &item) {
            return item.classification == classification && item.state == TrackerImportItemState::Unresolved;
        })) {
        return setError(out, QStringLiteral("A fresh tracker snapshot is required before applying queued progress."));
    }
    for (TrackerImportItem &item : batchIt->items) {
        if (item.classification != classification
            || (item.state != TrackerImportItemState::ReviewRequired
                && !(batchIt->confirmed && item.state == TrackerImportItemState::Unresolved)))
            continue;
        if (!resolutionAllowed(item.classification, resolution))
            return setError(out, QStringLiteral("Tracker import bulk choice is not available for every item."));
        item.resolution = resolution;
        item.localAfterSettlement.reset();
        item.localBaselineCaptured = false;
        item.state = resolution == TrackerImportResolution::UseProviderProgress
            ? TrackerImportItemState::AwaitingApply
            : resolution == TrackerImportResolution::KeepColosseum
                ? TrackerImportItemState::KeptLocal : TrackerImportItemState::Unresolved;
        ++changed;
    }
    if (changed == 0)
        return setError(out, QStringLiteral("Tracker import bulk choice has no eligible items."));
    if (!persist(next, m_cursors, out))
        return false;
    m_batches = next;
    return true;
}

bool TrackerImportStore::confirm(const QString &batchId, QString *out)
{
    if (!healthy(out) || !safeText(batchId))
        return setError(out, QStringLiteral("Tracker import confirmation is invalid."));
    QList<TrackerImportBatch> next = m_batches;
    auto it = std::find_if(next.begin(), next.end(), [&batchId](const TrackerImportBatch &batch) {
        return batch.batchId == batchId;
    });
    if (it == next.end() || !it->pageComplete)
        return setError(out, QStringLiteral("Tracker import page is incomplete."));
    if (!connectionIsCurrent(m_connections, it->providerId, it->remoteAccountId,
                             it->connectionGeneration))
        return setError(out, QStringLiteral("Tracker import preview belongs to a stale connection generation."));
    if (it->confirmed)
        return true;
    for (const TrackerImportItem &item : it->items) {
        if (item.state == TrackerImportItemState::ReviewRequired
            || item.state == TrackerImportItemState::Applying) {
            return setError(out, QStringLiteral("Every tracker import item must be resolved before confirmation."));
        }
    }
    it->confirmed = true;
    if (!persist(next, m_cursors, out))
        return false;
    m_batches = next;
    return true;
}

bool TrackerImportStore::applyConfirmed(const QString &batchId, TrackerImportOwner *owner, QString *out)
{
    if (!healthy(out) || !owner || !safeText(batchId))
        return setError(out, QStringLiteral("Tracker import owner is unavailable."));
    auto it = std::find_if(m_batches.begin(), m_batches.end(), [&batchId](const TrackerImportBatch &batch) {
        return batch.batchId == batchId;
    });
    if (it == m_batches.end() || !it->confirmed || !it->pageComplete)
        return setError(out, QStringLiteral("Tracker import is not confirmed."));
    return applyBatch(batchId, owner, out);
}

bool TrackerImportStore::applyRoutineSafe(const QString &batchId,
                                          TrackerImportOwner *owner,
                                          bool automaticPullApproved,
                                          QString *out)
{
    if (!healthy(out) || !owner || !safeText(batchId) || !automaticPullApproved)
        return setError(out, QStringLiteral("Automatic tracker pull is not available."));
    const auto stored = batch(batchId);
    if (!stored || stored->initialImport || stored->confirmed || !stored->pageComplete)
        return setError(out, QStringLiteral("Tracker pull cannot be applied automatically."));
    if (!connectionIsCurrent(m_connections, stored->providerId, stored->remoteAccountId,
                             stored->connectionGeneration))
        return setError(out, QStringLiteral("Tracker pull belongs to a stale connection generation."));
    if (!hasSettledInitialImport(m_batches, stored->providerId, stored->remoteAccountId))
        return setError(out, QStringLiteral("A confirmed initial tracker import is required before automatic pulls."));

    TrackerImportBatch working = *stored;
    const bool progressSourceRemoved = owner->importedProgressSourceRemovalSuppressed(
        stored->providerId, stored->remoteAccountId);
    for (TrackerImportItem &item : working.items) {
        if (item.state != TrackerImportItemState::ReviewRequired)
            continue;
        if (item.classification == TrackerImportClassification::RemoteAdvance
            && !progressSourceRemoved) {
            item.resolution = TrackerImportResolution::UseProviderProgress;
            item.state = TrackerImportItemState::AwaitingApply;
        } else {
            // New, lower/conflicting, ambiguous, or deliberately removed
            // Progress sources remain visible without an implicit mutation.
            item.resolution = TrackerImportResolution::LeaveUnresolved;
            item.state = TrackerImportItemState::Unresolved;
        }
    }
    working.confirmed = true;
    if (!commitBatch(working, m_cursors, out))
        return false;
    return applyBatch(batchId, owner, out);
}

void TrackerImportStore::applyConfirmedAsync(const QString &batchId,
                                              TrackerImportOwner *owner,
                                              CompletionCallback callback)
{
    if (!healthy() || !owner || !safeText(batchId)) {
        if (callback)
            callback(false, QStringLiteral("Tracker import owner is unavailable."));
        return;
    }
    const auto stored = batch(batchId);
    if (!stored || !stored->confirmed || !stored->pageComplete) {
        if (callback)
            callback(false, QStringLiteral("Tracker import is not confirmed."));
        return;
    }
    applyBatchAsync(batchId, owner, std::move(callback));
}

void TrackerImportStore::applyRoutineSafeAsync(const QString &batchId,
                                                TrackerImportOwner *owner,
                                                bool automaticPullApproved,
                                                CompletionCallback callback)
{
    if (!healthy() || !owner || !safeText(batchId) || !automaticPullApproved) {
        if (callback)
            callback(false, QStringLiteral("Automatic tracker pull is not available."));
        return;
    }
    const auto stored = batch(batchId);
    if (!stored || stored->initialImport || stored->confirmed || !stored->pageComplete
        || !connectionIsCurrent(m_connections, stored->providerId,
                                stored->remoteAccountId, stored->connectionGeneration)
        || !hasSettledInitialImport(m_batches, stored->providerId,
                                    stored->remoteAccountId)) {
        if (callback)
            callback(false, QStringLiteral("Tracker pull cannot be applied automatically."));
        return;
    }

    TrackerImportBatch working = *stored;
    const bool progressSourceRemoved = owner->importedProgressSourceRemovalSuppressed(
        stored->providerId, stored->remoteAccountId);
    for (TrackerImportItem &item : working.items) {
        if (item.state != TrackerImportItemState::ReviewRequired)
            continue;
        if (item.classification == TrackerImportClassification::RemoteAdvance
            && !progressSourceRemoved) {
            item.resolution = TrackerImportResolution::UseProviderProgress;
            item.state = TrackerImportItemState::AwaitingApply;
        } else {
            item.resolution = TrackerImportResolution::LeaveUnresolved;
            item.state = TrackerImportItemState::Unresolved;
        }
    }
    working.confirmed = true;
    QString error;
    if (!commitBatch(working, m_cursors, &error)) {
        if (callback)
            callback(false, error);
        return;
    }
    applyBatchAsync(batchId, owner, std::move(callback));
}

void TrackerImportStore::applyBatchAsync(const QString &batchId,
                                          TrackerImportOwner *owner,
                                          CompletionCallback callback)
{
    if (!owner || !safeText(batchId) || m_asyncBatches.contains(batchId)) {
        if (callback)
            callback(false, QStringLiteral("Tracker import is already applying or unavailable."));
        return;
    }
    m_asyncBatches.insert(batchId);
    QPointer<TrackerImportStore> self(this);
    const CompletionCallback finish = [self, batchId, callback](bool accepted,
                                                                 const QString &error) {
        if (self)
            self->m_asyncBatches.remove(batchId);
        if (callback)
            callback(accepted, error);
    };

    struct ApplyState {
        std::function<void(const std::shared_ptr<ApplyState> &)> advance;
        bool finished = false;
    };
    const auto state = std::make_shared<ApplyState>();
    state->advance = [self, owner, batchId, finish](
                         const std::shared_ptr<ApplyState> &state) {
        if (!self || state->finished)
            return;
        const auto storedBatch = self->batch(batchId);
        if (!storedBatch || !storedBatch->confirmed || !storedBatch->pageComplete) {
            state->finished = true;
            finish(false, QStringLiteral("Tracker import is no longer confirmed."));
            return;
        }
        TrackerImportBatch working = *storedBatch;
        if (!connectionIsCurrent(self->m_connections, working.providerId,
                                 working.remoteAccountId, working.connectionGeneration)) {
            state->finished = true;
            finish(false, QStringLiteral("Tracker import belongs to a stale connection generation."));
            return;
        }
        if (!working.cursorCommitted
            && !cursorIs(self->m_cursors, working.providerId,
                         working.remoteAccountId, working.baseCursor)) {
            state->finished = true;
            finish(false, QStringLiteral("Tracker import page is no longer the next cursor page."));
            return;
        }

        auto pending = std::find_if(working.items.begin(), working.items.end(),
            [](const TrackerImportItem &item) {
                return item.state == TrackerImportItemState::AwaitingApply
                    || item.state == TrackerImportItemState::Applying;
            });
        if (pending == working.items.end()) {
            bool changed = false;
            for (TrackerImportItem &item : working.items) {
                if (item.state != TrackerImportItemState::KeptLocal
                    || item.localBaselineCaptured || !item.remote.mapping
                    || !item.remote.exactProgressTarget)
                    continue;
                item.localAfterSettlement = owner->currentProgress(
                    *item.remote.mapping, *item.remote.exactProgressTarget);
                if (item.localAfterSettlement && item.remote.localAtPreview)
                    item.localAfterSettlement->nativeWitnessedHistory =
                        item.remote.localAtPreview->nativeWitnessedHistory;
                item.localBaselineCaptured = true;
                changed = true;
            }
            for (const TrackerImportItem &item : working.items) {
                if (!resolved(item)) {
                    state->finished = true;
                    finish(false, QStringLiteral("Tracker import page is not settled."));
                    return;
                }
            }
            if (!connectionIsCurrent(self->m_connections, working.providerId,
                                     working.remoteAccountId, working.connectionGeneration)
                || (!working.cursorCommitted
                    && !cursorIs(self->m_cursors, working.providerId,
                                 working.remoteAccountId, working.baseCursor))) {
                state->finished = true;
                finish(false, QStringLiteral("Tracker import cursor or connection changed before checkpoint."));
                return;
            }
            QHash<QString, QString> cursors = self->m_cursors;
            if (!working.cursorCommitted) {
                cursors.insert(cursorKey(working.providerId, working.remoteAccountId),
                               working.proposedCursor);
                working.cursorCommitted = true;
                changed = true;
            }
            QString error;
            if (!changed || !self->commitBatch(working, cursors, &error)) {
                // Even an already committed cursor is persisted only after the
                // durable owner receipt. A repeat call is an idempotent no-op.
                if (!changed && working.cursorCommitted) {
                    state->finished = true;
                    finish(true, QString());
                    return;
                }
                state->finished = true;
                finish(false, error.isEmpty()
                    ? QStringLiteral("Tracker import checkpoint could not be committed.") : error);
                return;
            }
            state->finished = true;
            finish(true, QString());
            return;
        }

        const QString itemId = pending->itemId;
        auto settleUnsupported = [self, state, finish, working, itemId](const QString &reason) mutable {
            if (!self || state->finished)
                return;
            auto currentBatch = self->batch(working.batchId);
            if (!currentBatch) {
                state->finished = true;
                finish(false, QStringLiteral("Tracker import batch disappeared."));
                return;
            }
            TrackerImportBatch next = *currentBatch;
            auto item = std::find_if(next.items.begin(), next.items.end(),
                [&itemId](const TrackerImportItem &candidate) { return candidate.itemId == itemId; });
            if (item == next.items.end()) {
                state->finished = true;
                finish(false, QStringLiteral("Tracker import item disappeared."));
                return;
            }
            item->classification = TrackerImportClassification::Unsupported;
            item->resolution = TrackerImportResolution::None;
            item->state = TrackerImportItemState::NonMutating;
            item->localAfterSettlement.reset();
            item->localBaselineCaptured = false;
            QString error;
            if (!self->commitBatch(next, self->m_cursors, &error)) {
                state->finished = true;
                finish(false, error);
                return;
            }
            Q_UNUSED(reason);
            QTimer::singleShot(0, self.data(), [state] { state->advance(state); });
        };

        TrackerImportItem item = *pending;
        if (!item.remote.mapping || !item.remote.exactProgressTarget) {
            settleUnsupported(QStringLiteral("The item has no exact native Progress target."));
            return;
        }
        const auto currentMapping = self->m_mappings
            ? self->m_mappings->mapping(item.remote.remote) : std::nullopt;
        if (!currentMapping || !sameMapping(*currentMapping, *item.remote.mapping)) {
            pending->classification = TrackerImportClassification::NeedsMatching;
            pending->resolution = TrackerImportResolution::None;
            pending->state = TrackerImportItemState::ReviewRequired;
            pending->localAfterSettlement.reset();
            pending->localBaselineCaptured = false;
            working.confirmed = false;
            QString error;
            if (!self->commitBatch(working, self->m_cursors, &error)) {
                state->finished = true;
                finish(false, error);
                return;
            }
            state->finished = true;
            finish(false, QStringLiteral("Tracker title mapping changed while this import was under review."));
            return;
        }
        if (item.state == TrackerImportItemState::AwaitingApply) {
            auto markApplying = std::find_if(working.items.begin(), working.items.end(),
                [&itemId](const TrackerImportItem &candidate) { return candidate.itemId == itemId; });
            markApplying->state = TrackerImportItemState::Applying;
            QString error;
            if (!self->commitBatch(working, self->m_cursors, &error)) {
                state->finished = true;
                finish(false, error);
                return;
            }
            item.state = TrackerImportItemState::Applying;
        }
        if (!connectionIsCurrent(self->m_connections, working.providerId,
                                 working.remoteAccountId, working.connectionGeneration)) {
            state->finished = true;
            finish(false, QStringLiteral("Tracker connection changed before import application."));
            return;
        }

        owner->applyImportedProgressAsync(
            item.operationId, *item.remote.mapping, *item.remote.exactProgressTarget,
            item.remote.localAtPreview, item.remote.progress, item.remote.completed,
            [self, owner, state, finish, working, item](
                TrackerImportOwnerApplyResult result,
                std::optional<TrackerImportedProgressValue> resultingProgress,
                const QString &ownerError) mutable {
                if (!self || state->finished)
                    return;
                const auto currentBatch = self->batch(working.batchId);
                if (!currentBatch || !connectionIsCurrent(
                        self->m_connections, currentBatch->providerId,
                        currentBatch->remoteAccountId, currentBatch->connectionGeneration)) {
                    state->finished = true;
                    finish(false, QStringLiteral("Tracker connection changed before import receipt settlement."));
                    return;
                }
                TrackerImportBatch next = *currentBatch;
                auto currentItem = std::find_if(next.items.begin(), next.items.end(),
                    [&item](const TrackerImportItem &candidate) {
                        return candidate.itemId == item.itemId;
                    });
                if (currentItem == next.items.end()
                    || currentItem->state != TrackerImportItemState::Applying) {
                    state->finished = true;
                    finish(false, QStringLiteral("Tracker import no longer owns this applying item."));
                    return;
                }
                if (result == TrackerImportOwnerApplyResult::Stale) {
                    currentItem->classification = TrackerImportClassification::Disagreement;
                    currentItem->resolution = TrackerImportResolution::None;
                    currentItem->state = TrackerImportItemState::ReviewRequired;
                    currentItem->localAfterSettlement.reset();
                    currentItem->localBaselineCaptured = false;
                    next.confirmed = false;
                    QString error;
                    if (!self->commitBatch(next, self->m_cursors, &error)) {
                        state->finished = true;
                        finish(false, error);
                        return;
                    }
                    state->finished = true;
                    finish(false, QStringLiteral("Local progress changed while this import was under review."));
                    return;
                }
                if (result == TrackerImportOwnerApplyResult::Unsupported) {
                    currentItem->classification = TrackerImportClassification::Unsupported;
                    currentItem->resolution = TrackerImportResolution::None;
                    currentItem->state = TrackerImportItemState::NonMutating;
                    currentItem->localAfterSettlement.reset();
                    currentItem->localBaselineCaptured = false;
                    QString error;
                    if (!self->commitBatch(next, self->m_cursors, &error)) {
                        state->finished = true;
                        finish(false, error);
                        return;
                    }
                    QTimer::singleShot(0, self.data(), [state] { state->advance(state); });
                    return;
                }
                if (result == TrackerImportOwnerApplyResult::Failed) {
                    state->finished = true;
                    finish(false, ownerError.isEmpty()
                        ? QStringLiteral("Tracker import outcome is not yet known.") : ownerError);
                    return;
                }
                if (!resultingProgress || !item.remote.mapping
                    || !item.remote.exactProgressTarget
                    || resultingProgress->canonicalMediaId
                        != item.remote.mapping->canonical.canonicalMediaId
                    || resultingProgress->historyKind
                        != item.remote.mapping->canonical.historyKind
                    || resultingProgress->historyId
                        != item.remote.mapping->canonical.historyId
                    || resultingProgress->progress != item.remote.progress
                    || resultingProgress->completed != item.remote.completed
                    || resultingProgress->nativeWitnessedHistory
                        != (item.remote.localAtPreview
                            && item.remote.localAtPreview->nativeWitnessedHistory)
                    || !resultingProgress->exactProgressTarget
                    || !sameProgressTarget(*resultingProgress->exactProgressTarget,
                                           *item.remote.exactProgressTarget)) {
                    state->finished = true;
                    finish(false, QStringLiteral("Tracker import owner returned a mismatched receipt."));
                    return;
                }
                currentItem->state = TrackerImportItemState::Applied;
                currentItem->localAfterSettlement = resultingProgress;
                currentItem->localBaselineCaptured = true;
                QString error;
                if (!self->commitBatch(next, self->m_cursors, &error)) {
                    state->finished = true;
                    finish(false, error);
                    return;
                }
                QTimer::singleShot(0, self.data(), [state] { state->advance(state); });
            });
    };
    state->advance(state);
}

void TrackerImportStore::recoverAsync(TrackerImportOwner *owner,
                                       CompletionCallback callback)
{
    if (!healthy() || !owner) {
        if (callback)
            callback(false, QStringLiteral("Tracker import owner is unavailable."));
        return;
    }
    QStringList batchIds;
    for (const TrackerImportBatch &batchValue : m_batches) {
        if (!batchValue.confirmed || !batchValue.pageComplete
            || !connectionIsCurrent(m_connections, batchValue.providerId,
                                    batchValue.remoteAccountId,
                                    batchValue.connectionGeneration))
            continue;
        bool needsRecovery = !batchValue.cursorCommitted;
        for (const TrackerImportItem &item : batchValue.items) {
            needsRecovery = needsRecovery
                || item.state == TrackerImportItemState::AwaitingApply
                || item.state == TrackerImportItemState::Applying
                || (item.state == TrackerImportItemState::KeptLocal
                    && !item.localBaselineCaptured);
        }
        if (needsRecovery)
            batchIds.append(batchValue.batchId);
    }
    QPointer<TrackerImportStore> self(this);
    struct RecoveryState {
        QStringList batchIds;
        qsizetype index = 0;
        std::function<void(const std::shared_ptr<RecoveryState> &)> advance;
        bool finished = false;
    };
    const auto state = std::make_shared<RecoveryState>();
    state->batchIds = std::move(batchIds);
    const CompletionCallback finish = [self, callback](bool accepted, const QString &error) {
        Q_UNUSED(self);
        if (callback)
            callback(accepted, error);
    };
    state->advance = [self, owner, finish](
                         const std::shared_ptr<RecoveryState> &state) {
        if (!self || state->finished)
            return;
        if (state->index >= state->batchIds.size()) {
            state->finished = true;
            finish(true, QString());
            return;
        }
        const QString batchId = state->batchIds.at(state->index++);
            self->applyBatchAsync(batchId, owner,
            [state, finish, self](bool accepted, const QString &error) {
                if (state->finished)
                    return;
                if (!accepted) {
                    state->finished = true;
                    finish(false, error);
                    return;
                }
                QTimer::singleShot(0, self.data(), [state] { state->advance(state); });
            });
    };
    state->advance(state);
}

bool TrackerImportStore::recover(TrackerImportOwner *owner, QString *out)
{
    if (!healthy(out) || !owner)
        return setError(out, QStringLiteral("Tracker import owner is unavailable."));
    QList<QString> batchIds;
    for (const TrackerImportBatch &batch : m_batches) {
        if (!batch.confirmed || !batch.pageComplete)
            continue;
        bool needsRecovery = !batch.cursorCommitted;
        for (const TrackerImportItem &item : batch.items) {
            needsRecovery = needsRecovery
                || item.state == TrackerImportItemState::AwaitingApply
                || item.state == TrackerImportItemState::Applying
                || (item.state == TrackerImportItemState::KeptLocal && !item.localBaselineCaptured);
        }
        if (!needsRecovery)
            continue;
        // Reconnection retires the generation used by an in-flight page. Keep
        // it durably pending for user attention, but do not let settled old
        // pages poison recovery or replay against the new connection.
        if (!connectionIsCurrent(m_connections, batch.providerId, batch.remoteAccountId,
                                 batch.connectionGeneration))
            continue;
        batchIds.append(batch.batchId);
    }
    for (const QString &batchId : batchIds) {
        if (!applyBatch(batchId, owner, out))
            return false;
    }
    return true;
}

std::optional<QString> TrackerImportStore::confirmedCursor(TrackerProviderId providerId,
                                                            const QString &remoteAccountId) const
{
    const auto value = m_cursors.constFind(cursorKey(providerId, remoteAccountId));
    return value == m_cursors.cend() ? std::nullopt : std::optional<QString>(*value);
}

bool TrackerImportStore::applyBatch(const QString &batchId, TrackerImportOwner *owner, QString *out)
{
    const auto stored = batch(batchId);
    if (!stored || !owner)
        return setError(out, QStringLiteral("Tracker import application is unavailable."));
    // Work on an owned copy. commitBatch atomically replaces m_batches, so a
    // reference into that list cannot span a receipt boundary.
    TrackerImportBatch working = *stored;
    TrackerImportBatch *batch = &working;
    if (!connectionIsCurrent(m_connections, batch->providerId, batch->remoteAccountId,
                             batch->connectionGeneration))
        return setError(out, QStringLiteral("Tracker import belongs to a stale connection generation."));
    if (!batch->cursorCommitted
        && !cursorIs(m_cursors, batch->providerId, batch->remoteAccountId, batch->baseCursor)) {
        return setError(out, QStringLiteral("Tracker import page is no longer the next cursor page."));
    }
    for (TrackerImportItem &item : batch->items) {
        if (item.state != TrackerImportItemState::AwaitingApply
            && item.state != TrackerImportItemState::Applying) {
            continue;
        }
        if (!item.remote.mapping)
            return setError(out, QStringLiteral("Tracker import item has no confirmed title mapping."));
        if (!item.remote.exactProgressTarget) {
            item.classification = TrackerImportClassification::Unsupported;
            item.resolution = TrackerImportResolution::None;
            item.state = TrackerImportItemState::NonMutating;
            item.localAfterSettlement.reset();
            item.localBaselineCaptured = false;
            if (!commitBatch(*batch, m_cursors, out))
                return false;
            continue;
        }
        const auto currentMapping = m_mappings->mapping(item.remote.remote);
        if (!currentMapping || !sameMapping(*currentMapping, *item.remote.mapping)) {
            item.classification = TrackerImportClassification::NeedsMatching;
            item.resolution = TrackerImportResolution::None;
            item.state = TrackerImportItemState::ReviewRequired;
            item.localAfterSettlement.reset();
            item.localBaselineCaptured = false;
            batch->confirmed = false;
            if (!commitBatch(*batch, m_cursors, out))
                return false;
            return setError(out, QStringLiteral("Tracker title mapping changed while this import was under review."));
        }
        if (item.state != TrackerImportItemState::Applying) {
            item.state = TrackerImportItemState::Applying;
            if (!commitBatch(*batch, m_cursors, out))
                return false;
        }
        if (!connectionIsCurrent(m_connections, batch->providerId, batch->remoteAccountId,
                                 batch->connectionGeneration))
            return setError(out, QStringLiteral("Tracker connection changed before import application."));
        std::optional<TrackerImportedProgressValue> resultingProgress;
        const TrackerImportOwnerApplyResult result = owner->applyImportedProgressWithTarget(
            item.operationId, *item.remote.mapping, *item.remote.exactProgressTarget,
            item.remote.localAtPreview, item.remote.progress, item.remote.completed,
            &resultingProgress, out);
        if (result == TrackerImportOwnerApplyResult::Stale) {
            item.classification = TrackerImportClassification::Disagreement;
            item.resolution = TrackerImportResolution::None;
            item.state = TrackerImportItemState::ReviewRequired;
            item.localAfterSettlement.reset();
            item.localBaselineCaptured = false;
            batch->confirmed = false;
            if (!commitBatch(*batch, m_cursors, out))
                return false;
            return setError(out, QStringLiteral("Local progress changed while this import was under review."));
        }
        if (result == TrackerImportOwnerApplyResult::Unsupported) {
            item.classification = TrackerImportClassification::Unsupported;
            item.resolution = TrackerImportResolution::None;
            item.state = TrackerImportItemState::NonMutating;
            item.localAfterSettlement.reset();
            item.localBaselineCaptured = false;
            if (!commitBatch(*batch, m_cursors, out))
                return false;
            continue;
        }
        if (result == TrackerImportOwnerApplyResult::Failed) {
            if (out && out->isEmpty())
                *out = QStringLiteral("Tracker import owner outcome is unknown.");
            return false;
        }
        if (!resultingProgress || resultingProgress->canonicalMediaId != item.remote.mapping->canonical.canonicalMediaId
            || resultingProgress->historyKind != item.remote.mapping->canonical.historyKind
            || resultingProgress->historyId != item.remote.mapping->canonical.historyId
            || resultingProgress->progress != item.remote.progress
            || resultingProgress->completed != item.remote.completed
            || resultingProgress->nativeWitnessedHistory
                != (item.remote.localAtPreview && item.remote.localAtPreview->nativeWitnessedHistory)
            || !resultingProgress->exactProgressTarget
            || !sameProgressTarget(*resultingProgress->exactProgressTarget,
                                   *item.remote.exactProgressTarget)) {
            return setError(out, QStringLiteral("Tracker import owner returned a mismatched receipt."));
        }
        item.state = TrackerImportItemState::Applied;
        item.localAfterSettlement = resultingProgress;
        item.localBaselineCaptured = true;
        if (!commitBatch(*batch, m_cursors, out))
            return false;
    }
    for (TrackerImportItem &item : batch->items) {
        if (item.state != TrackerImportItemState::KeptLocal || item.localBaselineCaptured
            || !item.remote.mapping)
            continue;
        if (!item.remote.exactProgressTarget)
            return setError(out, QStringLiteral("Kept tracker progress has no exact Colosseum target."));
        item.localAfterSettlement = owner->currentProgress(
            *item.remote.mapping, *item.remote.exactProgressTarget);
        if (item.localAfterSettlement && item.remote.localAtPreview)
            item.localAfterSettlement->nativeWitnessedHistory =
                item.remote.localAtPreview->nativeWitnessedHistory;
        item.localBaselineCaptured = true;
    }
    for (const TrackerImportItem &item : batch->items) {
        if (!resolved(item))
            return setError(out, QStringLiteral("Tracker import page is not settled."));
    }
    if (batch->cursorCommitted)
        return commitBatch(*batch, m_cursors, out);
    if (!connectionIsCurrent(m_connections, batch->providerId, batch->remoteAccountId,
                             batch->connectionGeneration)
        || !cursorIs(m_cursors, batch->providerId, batch->remoteAccountId, batch->baseCursor)) {
        return setError(out, QStringLiteral("Tracker import cursor or connection changed before checkpoint."));
    }
    QHash<QString, QString> cursors = m_cursors;
    cursors.insert(cursorKey(batch->providerId, batch->remoteAccountId), batch->proposedCursor);
    batch->cursorCommitted = true;
    return commitBatch(*batch, cursors, out);
}

bool TrackerImportStore::commitBatch(const TrackerImportBatch &batch,
                                     const QHash<QString, QString> &cursors,
                                     QString *out)
{
    QList<TrackerImportBatch> next = m_batches;
    bool replaced = false;
    for (TrackerImportBatch &entry : next) {
        if (entry.batchId == batch.batchId) {
            entry = batch;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        return setError(out, QStringLiteral("Tracker import batch disappeared."));
    if (!persist(next, cursors, out))
        return false;
    m_batches = next;
    m_cursors = cursors;
    return true;
}

bool TrackerImportStore::load()
{
    QFile file(m_path);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker import journal could not be opened.");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const QJsonObject root = document.object();
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || root.value(QStringLiteral("version")).toInt() != kSchemaVersion
        || root.value(QStringLiteral("profileId")).toString() != m_profile.profileId()
        || !root.value(QStringLiteral("batches")).isArray()
        || !root.value(QStringLiteral("cursors")).isArray()) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker import journal is malformed or foreign.");
        return false;
    }
    QList<TrackerImportBatch> batches;
    QSet<QString> ids;
    for (const QJsonValue &value : root.value(QStringLiteral("batches")).toArray()) {
        const auto batch = value.isObject() ? batchFromJson(value.toObject()) : std::nullopt;
        if (!batch || ids.contains(batch->batchId)) {
            m_healthy = false;
            m_error = QStringLiteral("Tracker import batch is invalid or duplicated.");
            return false;
        }
        ids.insert(batch->batchId);
        batches.append(*batch);
    }
    QHash<QString, QString> cursors;
    for (const QJsonValue &value : root.value(QStringLiteral("cursors")).toArray()) {
        const QJsonObject cursor = value.toObject();
        const auto provider = trackerProviderIdFromKey(cursor.value(QStringLiteral("providerId")).toString());
        const QString account = cursor.value(QStringLiteral("remoteAccountId")).toString();
        const QString token = cursor.value(QStringLiteral("cursor")).toString();
        const QString key = provider ? cursorKey(*provider, account) : QString();
        if (!value.isObject() || !provider || !safeText(account, 128) || !safeText(token)
            || cursors.contains(key)) {
            m_healthy = false;
            m_error = QStringLiteral("Tracker import cursor is invalid or duplicated.");
            return false;
        }
        cursors.insert(key, token);
    }
    m_batches = batches;
    m_cursors = cursors;
    return true;
}

bool TrackerImportStore::persist(const QList<TrackerImportBatch> &batches,
                                 const QHash<QString, QString> &cursors,
                                 QString *out) const
{
#ifdef COLOSSEUM_TRACKER_IMPORT_TESTING
    if (m_forcePersistenceFailure)
        return setError(out, QStringLiteral("Tracker import journal persistence test failure."));
#endif
    QJsonArray serializedBatches;
    QSet<QString> batchIds;
    for (const TrackerImportBatch &batch : batches) {
        if (!batchFromJson(batchToJson(batch)) || batchIds.contains(batch.batchId))
            return setError(out, QStringLiteral("Tracker import journal persistence is invalid."));
        batchIds.insert(batch.batchId);
        serializedBatches.append(batchToJson(batch));
    }
    QJsonArray serializedCursors;
    for (auto it = cursors.cbegin(); it != cursors.cend(); ++it) {
        const QStringList parts = it.key().split(QChar(0x1f));
        const auto provider = parts.size() == 2 ? trackerProviderIdFromKey(parts.at(0)) : std::nullopt;
        if (!provider || !safeText(parts.at(1), 128) || !safeText(it.value()))
            return setError(out, QStringLiteral("Tracker import cursor persistence is invalid."));
        serializedCursors.append(QJsonObject{{QStringLiteral("providerId"), trackerProviderKey(*provider)},
                                             {QStringLiteral("remoteAccountId"), parts.at(1)},
                                             {QStringLiteral("cursor"), it.value()}});
    }
    const QFileInfo info(m_path);
    if (!QDir().mkpath(info.dir().absolutePath()))
        return setError(out, QStringLiteral("Tracker import journal directory could not be created."));
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return setError(out, QStringLiteral("Tracker import journal could not be written."));
    file.write(QJsonDocument({{QStringLiteral("version"), kSchemaVersion},
                              {QStringLiteral("profileId"), m_profile.profileId()},
                              {QStringLiteral("batches"), serializedBatches},
                              {QStringLiteral("cursors"), serializedCursors}}).toJson(QJsonDocument::Compact));
    if (!file.commit())
        return setError(out, QStringLiteral("Tracker import journal could not be saved atomically."));
    return true;
}
