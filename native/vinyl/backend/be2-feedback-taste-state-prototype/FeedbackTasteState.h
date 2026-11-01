#pragma once

#include "RecommendationEntityRef.h"

#include <QString>
#include <QVector>

#include <optional>
#include <utility>

namespace vinyl {

enum class TasteErrorCode {
    None,
    InvalidBehaviorObservationId,
    InvalidTimestamp,
    InvalidRevision,
    InvalidSnoozeUntil,
    StaleMutation,
    ConflictingMutation,
    ConflictingBehaviorObservation,
};

struct TasteError final {
    TasteErrorCode code = TasteErrorCode::None;
    QString field;
    QString detail;
};

QString tasteErrorCodeName(TasteErrorCode code);

template <typename T>
struct TasteResult final {
    std::optional<T> value;
    TasteError error;

    bool ok() const noexcept { return value.has_value(); }

    static TasteResult success(T value)
    {
        TasteResult out;
        out.value = std::move(value);
        return out;
    }

    static TasteResult failure(TasteErrorCode code,
                               QString field,
                               QString detail = QString())
    {
        TasteResult out;
        out.error = TasteError{code, std::move(field), std::move(detail)};
        return out;
    }
};

struct TasteMutationStatus final {
    bool ok = true;
    bool changed = false;
    TasteError error;

    static TasteMutationStatus success(bool changed = true)
    {
        TasteMutationStatus status;
        status.changed = changed;
        return status;
    }

    static TasteMutationStatus failure(TasteErrorCode code,
                                       QString field,
                                       QString detail = QString())
    {
        TasteMutationStatus status;
        status.ok = false;
        status.error = TasteError{code, std::move(field), std::move(detail)};
        return status;
    }
};

class BehaviorObservationId final
{
public:
    static TasteResult<BehaviorObservationId> parse(const QString &text);

    const QString &value() const noexcept { return m_value; }

    bool operator==(const BehaviorObservationId &other) const noexcept
    {
        return m_value == other.m_value;
    }

private:
    explicit BehaviorObservationId(QString value)
        : m_value(std::move(value))
    {
    }

    QString m_value;
};

enum class ExplicitTasteFeedback {
    Neutral,
    Positive,
    Negative,
};

QString explicitTasteFeedbackName(ExplicitTasteFeedback feedback);

enum class BehavioralSignalKind {
    Replay,
    DeliberateQueue,
    NaturalCompletion,
    AlbumCompletion,
    LibrarySave,
    PlaylistAdd,
    EarlySkip,
    QueueRemoval,
    RecommendationAvoidance,
    PassiveAutoplayCompletion,
};

QString behavioralSignalKindName(BehavioralSignalKind kind);

class BehavioralSignalObservation final
{
public:
    static TasteResult<BehavioralSignalObservation> create(
        BehaviorObservationId observationId,
        RecommendationEntityRef canonical,
        BehavioralSignalKind kind,
        qint64 occurredAtMs);

    const BehaviorObservationId &observationId() const noexcept { return m_observationId; }
    const RecommendationEntityRef &canonical() const noexcept { return m_canonical; }
    BehavioralSignalKind kind() const noexcept { return m_kind; }
    qint64 occurredAtMs() const noexcept { return m_occurredAtMs; }

    bool samePayload(const BehavioralSignalObservation &other) const noexcept;

private:
    BehavioralSignalObservation(BehaviorObservationId observationId,
                                RecommendationEntityRef canonical,
                                BehavioralSignalKind kind,
                                qint64 occurredAtMs)
        : m_observationId(std::move(observationId))
        , m_canonical(std::move(canonical))
        , m_kind(kind)
        , m_occurredAtMs(occurredAtMs)
    {
    }

    BehaviorObservationId m_observationId;
    RecommendationEntityRef m_canonical;
    BehavioralSignalKind m_kind;
    qint64 m_occurredAtMs = 0;
};

class ExplicitTasteFeedbackState final
{
public:
    ExplicitTasteFeedback feedback() const noexcept { return m_feedback; }
    quint64 revision() const noexcept { return m_revision; }
    qint64 changedAtMs() const noexcept { return m_changedAtMs; }
    bool hasExplicitMutation() const noexcept { return m_revision > 0; }

    const std::optional<ProviderItemRef> &providerFeedbackRef() const noexcept
    {
        return m_providerFeedbackRef;
    }

private:
    ExplicitTasteFeedback m_feedback = ExplicitTasteFeedback::Neutral;
    quint64 m_revision = 0;
    qint64 m_changedAtMs = 0;
    std::optional<ProviderItemRef> m_providerFeedbackRef;

    friend class TasteProfileState;
};

class RecommendationSuppressionState final
{
public:
    bool hidden() const noexcept { return m_hidden; }
    quint64 hiddenRevision() const noexcept { return m_hiddenRevision; }
    qint64 hiddenChangedAtMs() const noexcept { return m_hiddenChangedAtMs; }

    const std::optional<qint64> &snoozedUntilMs() const noexcept
    {
        return m_snoozedUntilMs;
    }

    quint64 snoozeRevision() const noexcept { return m_snoozeRevision; }
    qint64 snoozeChangedAtMs() const noexcept { return m_snoozeChangedAtMs; }
    bool doNotRecommend() const noexcept { return m_doNotRecommend; }
    quint64 doNotRecommendRevision() const noexcept { return m_doNotRecommendRevision; }
    qint64 doNotRecommendChangedAtMs() const noexcept { return m_doNotRecommendChangedAtMs; }

    bool isSnoozedAt(qint64 atMs) const noexcept
    {
        return atMs > 0 && m_snoozedUntilMs.has_value() && atMs < *m_snoozedUntilMs;
    }

private:
    bool m_hidden = false;
    quint64 m_hiddenRevision = 0;
    qint64 m_hiddenChangedAtMs = 0;
    std::optional<qint64> m_snoozedUntilMs;
    quint64 m_snoozeRevision = 0;
    qint64 m_snoozeChangedAtMs = 0;
    bool m_doNotRecommend = false;
    quint64 m_doNotRecommendRevision = 0;
    qint64 m_doNotRecommendChangedAtMs = 0;

    friend class TasteProfileState;
};

class EntityTasteState final
{
public:
    const RecommendationEntityRef &canonical() const noexcept { return m_canonical; }
    const ExplicitTasteFeedbackState &explicitFeedback() const noexcept { return m_explicitFeedback; }
    const RecommendationSuppressionState &suppression() const noexcept { return m_suppression; }
    const QVector<BehavioralSignalObservation> &behavioralSignals() const noexcept
    {
        return m_behavioralSignals;
    }

private:
    explicit EntityTasteState(RecommendationEntityRef canonical)
        : m_canonical(std::move(canonical))
    {
    }

    RecommendationEntityRef m_canonical;
    ExplicitTasteFeedbackState m_explicitFeedback;
    RecommendationSuppressionState m_suppression;
    QVector<BehavioralSignalObservation> m_behavioralSignals;

    friend class TasteProfileState;
};

class TasteProfileState final
{
public:
    explicit TasteProfileState(ProfileScopeId profileId)
        : m_profileId(std::move(profileId))
    {
    }

    const ProfileScopeId &profileId() const noexcept { return m_profileId; }
    int size() const noexcept { return m_states.size(); }
    const QVector<EntityTasteState> &states() const noexcept { return m_states; }

    const EntityTasteState *find(const RecommendationEntityRef &canonical) const;

    TasteMutationStatus applyExplicitFeedback(
        const RecommendationEntityRef &canonical,
        ExplicitTasteFeedback feedback,
        quint64 revision,
        qint64 changedAtMs,
        std::optional<ProviderItemRef> providerFeedbackRef = std::nullopt);

    TasteMutationStatus setHidden(const RecommendationEntityRef &canonical,
                                  bool hidden,
                                  quint64 revision,
                                  qint64 changedAtMs);

    TasteMutationStatus setSnoozedUntil(const RecommendationEntityRef &canonical,
                                        std::optional<qint64> snoozedUntilMs,
                                        quint64 revision,
                                        qint64 changedAtMs);

    TasteMutationStatus setDoNotRecommend(const RecommendationEntityRef &canonical,
                                          bool doNotRecommend,
                                          quint64 revision,
                                          qint64 changedAtMs);

    TasteMutationStatus recordBehavioralSignal(BehavioralSignalObservation observation);

private:
    int indexOf(const RecommendationEntityRef &canonical) const;
    EntityTasteState &ensureState(const RecommendationEntityRef &canonical);

    ProfileScopeId m_profileId;
    QVector<EntityTasteState> m_states;
};

} // namespace vinyl
