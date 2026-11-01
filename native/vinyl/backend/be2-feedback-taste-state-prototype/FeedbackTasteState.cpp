#include "FeedbackTasteState.h"

#include <QChar>

namespace vinyl {

namespace {

bool isValidOpaqueId(const QString &text)
{
    if (text.isEmpty() || text.size() > 128 || text.trimmed() != text)
        return false;

    for (const QChar ch : text) {
        if (ch.category() == QChar::Other_Control)
            return false;
    }
    return true;
}

TasteMutationStatus validateMutationVersion(quint64 revision,
                                              qint64 changedAtMs)
{
    if (revision == 0) {
        return TasteMutationStatus::failure(
            TasteErrorCode::InvalidRevision,
            QStringLiteral("revision"),
            QStringLiteral("Mutation revision must be positive."));
    }
    if (changedAtMs <= 0) {
        return TasteMutationStatus::failure(
            TasteErrorCode::InvalidTimestamp,
            QStringLiteral("changed_at_ms"),
            QStringLiteral("Mutation wall timestamp must be positive."));
    }
    return TasteMutationStatus::success(false);
}

TasteMutationStatus applyVersionedBool(bool desired,
                                       quint64 revision,
                                       qint64 changedAtMs,
                                       bool &currentValue,
                                       quint64 &currentRevision,
                                       qint64 &currentChangedAtMs,
                                       const QString &field)
{
    const auto version = validateMutationVersion(revision, changedAtMs);
    if (!version.ok)
        return version;

    if (currentRevision > 0) {
        if (revision < currentRevision) {
            return TasteMutationStatus::failure(
                TasteErrorCode::StaleMutation,
                field,
                QStringLiteral("Older revision cannot replace newer state."));
        }
        if (revision == currentRevision) {
            if (desired == currentValue && changedAtMs == currentChangedAtMs)
                return TasteMutationStatus::success(false);
            return TasteMutationStatus::failure(
                TasteErrorCode::ConflictingMutation,
                field,
                QStringLiteral("Equal-revision mutations disagree."));
        }
    }

    currentValue = desired;
    currentRevision = revision;
    currentChangedAtMs = changedAtMs;
    return TasteMutationStatus::success(true);
}

} // namespace

QString tasteErrorCodeName(TasteErrorCode code)
{
    switch (code) {
    case TasteErrorCode::None:
        return QStringLiteral("none");
    case TasteErrorCode::InvalidBehaviorObservationId:
        return QStringLiteral("invalid_behavior_observation_id");
    case TasteErrorCode::InvalidTimestamp:
        return QStringLiteral("invalid_timestamp");
    case TasteErrorCode::InvalidRevision:
        return QStringLiteral("invalid_revision");
    case TasteErrorCode::InvalidSnoozeUntil:
        return QStringLiteral("invalid_snooze_until");
    case TasteErrorCode::StaleMutation:
        return QStringLiteral("stale_mutation");
    case TasteErrorCode::ConflictingMutation:
        return QStringLiteral("conflicting_mutation");
    case TasteErrorCode::ConflictingBehaviorObservation:
        return QStringLiteral("conflicting_behavior_observation");
    }
    return QStringLiteral("unknown");
}

TasteResult<BehaviorObservationId> BehaviorObservationId::parse(const QString &text)
{
    if (!isValidOpaqueId(text)) {
        return TasteResult<BehaviorObservationId>::failure(
            TasteErrorCode::InvalidBehaviorObservationId,
            QStringLiteral("behavior_observation_id"),
            QStringLiteral("Behavior observation ID must be non-empty, bounded, trimmed, and free of control characters."));
    }
    return TasteResult<BehaviorObservationId>::success(BehaviorObservationId(text));
}

QString explicitTasteFeedbackName(ExplicitTasteFeedback feedback)
{
    switch (feedback) {
    case ExplicitTasteFeedback::Neutral:
        return QStringLiteral("neutral");
    case ExplicitTasteFeedback::Positive:
        return QStringLiteral("positive");
    case ExplicitTasteFeedback::Negative:
        return QStringLiteral("negative");
    }
    return QStringLiteral("unknown");
}

QString behavioralSignalKindName(BehavioralSignalKind kind)
{
    switch (kind) {
    case BehavioralSignalKind::Replay:
        return QStringLiteral("replay");
    case BehavioralSignalKind::DeliberateQueue:
        return QStringLiteral("deliberate_queue");
    case BehavioralSignalKind::NaturalCompletion:
        return QStringLiteral("natural_completion");
    case BehavioralSignalKind::AlbumCompletion:
        return QStringLiteral("album_completion");
    case BehavioralSignalKind::LibrarySave:
        return QStringLiteral("library_save");
    case BehavioralSignalKind::PlaylistAdd:
        return QStringLiteral("playlist_add");
    case BehavioralSignalKind::EarlySkip:
        return QStringLiteral("early_skip");
    case BehavioralSignalKind::QueueRemoval:
        return QStringLiteral("queue_removal");
    case BehavioralSignalKind::RecommendationAvoidance:
        return QStringLiteral("recommendation_avoidance");
    case BehavioralSignalKind::PassiveAutoplayCompletion:
        return QStringLiteral("passive_autoplay_completion");
    }
    return QStringLiteral("unknown");
}

TasteResult<BehavioralSignalObservation> BehavioralSignalObservation::create(
    BehaviorObservationId observationId,
    RecommendationEntityRef canonical,
    BehavioralSignalKind kind,
    qint64 occurredAtMs)
{
    if (occurredAtMs <= 0) {
        return TasteResult<BehavioralSignalObservation>::failure(
            TasteErrorCode::InvalidTimestamp,
            QStringLiteral("occurred_at_ms"),
            QStringLiteral("Behavior observation timestamp must be positive."));
    }

    return TasteResult<BehavioralSignalObservation>::success(
        BehavioralSignalObservation(std::move(observationId),
                                    std::move(canonical),
                                    kind,
                                    occurredAtMs));
}

bool BehavioralSignalObservation::samePayload(
    const BehavioralSignalObservation &other) const noexcept
{
    return m_observationId == other.m_observationId
        && m_canonical == other.m_canonical
        && m_kind == other.m_kind
        && m_occurredAtMs == other.m_occurredAtMs;
}

const EntityTasteState *TasteProfileState::find(
    const RecommendationEntityRef &canonical) const
{
    const int index = indexOf(canonical);
    if (index < 0)
        return nullptr;
    return &m_states.at(index);
}

TasteMutationStatus TasteProfileState::applyExplicitFeedback(
    const RecommendationEntityRef &canonical,
    ExplicitTasteFeedback feedback,
    quint64 revision,
    qint64 changedAtMs,
    std::optional<ProviderItemRef> providerFeedbackRef)
{
    const auto version = validateMutationVersion(revision, changedAtMs);
    if (!version.ok)
        return version;

    const EntityTasteState *existingState = find(canonical);
    if (existingState && existingState->explicitFeedback().revision() > 0) {
        const auto &current = existingState->explicitFeedback();
        if (revision < current.revision()) {
            return TasteMutationStatus::failure(
                TasteErrorCode::StaleMutation,
                QStringLiteral("explicit_feedback"),
                QStringLiteral("Older revision cannot replace newer explicit feedback."));
        }
        if (revision == current.revision()) {
            if (feedback == current.feedback()
                && changedAtMs == current.changedAtMs()
                && providerFeedbackRef == current.providerFeedbackRef()) {
                return TasteMutationStatus::success(false);
            }
            return TasteMutationStatus::failure(
                TasteErrorCode::ConflictingMutation,
                QStringLiteral("explicit_feedback"),
                QStringLiteral("Equal-revision explicit feedback disagrees."));
        }
    }

    auto &state = ensureState(canonical);
    state.m_explicitFeedback.m_feedback = feedback;
    state.m_explicitFeedback.m_revision = revision;
    state.m_explicitFeedback.m_changedAtMs = changedAtMs;
    state.m_explicitFeedback.m_providerFeedbackRef = std::move(providerFeedbackRef);
    return TasteMutationStatus::success(true);
}

TasteMutationStatus TasteProfileState::setHidden(
    const RecommendationEntityRef &canonical,
    bool hidden,
    quint64 revision,
    qint64 changedAtMs)
{
    const auto version = validateMutationVersion(revision, changedAtMs);
    if (!version.ok)
        return version;
    auto &state = ensureState(canonical);
    return applyVersionedBool(hidden,
                              revision,
                              changedAtMs,
                              state.m_suppression.m_hidden,
                              state.m_suppression.m_hiddenRevision,
                              state.m_suppression.m_hiddenChangedAtMs,
                              QStringLiteral("hidden"));
}

TasteMutationStatus TasteProfileState::setSnoozedUntil(
    const RecommendationEntityRef &canonical,
    std::optional<qint64> snoozedUntilMs,
    quint64 revision,
    qint64 changedAtMs)
{
    const auto version = validateMutationVersion(revision, changedAtMs);
    if (!version.ok)
        return version;

    if (snoozedUntilMs.has_value() && *snoozedUntilMs <= changedAtMs) {
        return TasteMutationStatus::failure(
            TasteErrorCode::InvalidSnoozeUntil,
            QStringLiteral("snoozed_until_ms"),
            QStringLiteral("Snooze expiry must be later than its descriptive wall timestamp."));
    }

    const EntityTasteState *existingState = find(canonical);
    if (existingState && existingState->suppression().snoozeRevision() > 0) {
        const auto &current = existingState->suppression();
        if (revision < current.snoozeRevision()) {
            return TasteMutationStatus::failure(
                TasteErrorCode::StaleMutation,
                QStringLiteral("snoozed_until_ms"),
                QStringLiteral("Older revision cannot replace newer snooze state."));
        }
        if (revision == current.snoozeRevision()) {
            if (snoozedUntilMs == current.snoozedUntilMs()
                && changedAtMs == current.snoozeChangedAtMs()) {
                return TasteMutationStatus::success(false);
            }
            return TasteMutationStatus::failure(
                TasteErrorCode::ConflictingMutation,
                QStringLiteral("snoozed_until_ms"),
                QStringLiteral("Equal-revision snooze mutations disagree."));
        }
    }

    auto &state = ensureState(canonical);
    state.m_suppression.m_snoozedUntilMs = snoozedUntilMs;
    state.m_suppression.m_snoozeRevision = revision;
    state.m_suppression.m_snoozeChangedAtMs = changedAtMs;
    return TasteMutationStatus::success(true);
}

TasteMutationStatus TasteProfileState::setDoNotRecommend(
    const RecommendationEntityRef &canonical,
    bool doNotRecommend,
    quint64 revision,
    qint64 changedAtMs)
{
    const auto version = validateMutationVersion(revision, changedAtMs);
    if (!version.ok)
        return version;
    auto &state = ensureState(canonical);
    return applyVersionedBool(doNotRecommend,
                              revision,
                              changedAtMs,
                              state.m_suppression.m_doNotRecommend,
                              state.m_suppression.m_doNotRecommendRevision,
                              state.m_suppression.m_doNotRecommendChangedAtMs,
                              QStringLiteral("do_not_recommend"));
}

TasteMutationStatus TasteProfileState::recordBehavioralSignal(
    BehavioralSignalObservation observation)
{
    for (const auto &state : m_states) {
        for (const auto &existing : state.m_behavioralSignals) {
            if (existing.observationId() == observation.observationId()) {
                if (existing.samePayload(observation))
                    return TasteMutationStatus::success(false);
                return TasteMutationStatus::failure(
                    TasteErrorCode::ConflictingBehaviorObservation,
                    QStringLiteral("behavior_observation_id"),
                    QStringLiteral("Behavior observation ID was reused for different evidence."));
            }
        }
    }

    auto &state = ensureState(observation.canonical());
    state.m_behavioralSignals.append(std::move(observation));
    return TasteMutationStatus::success(true);
}

int TasteProfileState::indexOf(const RecommendationEntityRef &canonical) const
{
    for (int i = 0; i < m_states.size(); ++i) {
        if (m_states.at(i).canonical() == canonical)
            return i;
    }
    return -1;
}

EntityTasteState &TasteProfileState::ensureState(
    const RecommendationEntityRef &canonical)
{
    const int existing = indexOf(canonical);
    if (existing >= 0)
        return m_states[existing];

    m_states.append(EntityTasteState(canonical));
    return m_states.last();
}

} // namespace vinyl
