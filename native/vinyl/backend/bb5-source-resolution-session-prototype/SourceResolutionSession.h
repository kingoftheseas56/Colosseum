#pragma once

#include "ExactTrackResolver.h"
#include "QueueSessionCoordinator.h"
#include "RelatedVideoContract.h"
#include "ResolvedPlaybackSource.h"

#include <QString>
#include <QVector>
#include <QtGlobal>

#include <optional>
#include <utility>

namespace vinyl {

enum class SourceResolutionErrorCode {
    None,
    NoActiveOccurrence,
    RecordingMismatch,
    ExactResolutionRequired,
    CandidateNotSelectable,
    DuplicateCandidate,
    InvalidSelectionPolicy,
    InvalidClockValue,
    InvalidResolvedSource,
    StaleRequest,
    CoordinatorRejected,
    CounterExhausted,
    FallbackExhausted,
    InvalidMemoryState,
    ProfileMismatch,
};

struct SourceResolutionError final {
    SourceResolutionErrorCode code = SourceResolutionErrorCode::None;
    QString field;
    QString detail;
};

QString sourceResolutionErrorCodeName(SourceResolutionErrorCode code);

template <typename T>
struct SourceResolutionResult final {
    std::optional<T> value;
    SourceResolutionError error;

    bool ok() const noexcept { return value.has_value(); }

    static SourceResolutionResult success(T value)
    {
        SourceResolutionResult out;
        out.value = std::move(value);
        return out;
    }

    static SourceResolutionResult failure(SourceResolutionErrorCode code,
                                          QString field,
                                          QString detail = QString())
    {
        SourceResolutionResult out;
        out.error = SourceResolutionError{
            code, std::move(field), std::move(detail)};
        return out;
    }
};

struct SourceResolutionStatus final {
    bool ok = true;
    bool changed = false;
    SourceResolutionError error;

    static SourceResolutionStatus success(bool changed = true)
    {
        SourceResolutionStatus out;
        out.changed = changed;
        return out;
    }

    static SourceResolutionStatus failure(SourceResolutionErrorCode code,
                                          QString field,
                                          QString detail = QString())
    {
        SourceResolutionStatus out;
        out.ok = false;
        out.error = SourceResolutionError{
            code, std::move(field), std::move(detail)};
        return out;
    }
};

enum class SourceTransportKind {
    Audio,
    Video,
};

QString sourceTransportKindName(SourceTransportKind kind);

enum class SourceResolutionReason {
    Initial,
    Fallback,
    ExpiryRefresh,
    ExplicitSwitch,
};

QString sourceResolutionReasonName(SourceResolutionReason reason);

enum class SourceAttemptFailureKind {
    ResolutionFailed,
    PlaybackFailed,
    Expired,
    InvalidResolvedTransport,
};

QString sourceAttemptFailureKindName(SourceAttemptFailureKind kind);

class SelectableSourceCandidate final
{
public:
    static SourceResolutionResult<SelectableSourceCandidate> fromExactAudio(
        const NormalizedSourceCandidate &candidate,
        const RequestedTrackIdentity &request,
        const AlbumSourceManifest &manifest,
        const ExactTrackResolverPolicy &policy = {});

    static SourceResolutionResult<SelectableSourceCandidate> fromQualifiedVideo(
        const RelatedVideoCandidate &candidate);

    const RecordingMbid &recordingMbid() const noexcept { return m_recordingMbid; }
    const SourceCandidateKey &candidateKey() const noexcept { return m_candidateKey; }
    SourceKind sourceKind() const noexcept { return m_sourceKind; }
    SourceTransportKind transportKind() const noexcept { return m_transportKind; }
    CandidateResolutionMode resolutionMode() const noexcept { return m_resolutionMode; }
    const SourceCandidateFacts &facts() const noexcept { return m_facts; }

private:
    SelectableSourceCandidate(RecordingMbid recordingMbid,
                              SourceCandidateKey candidateKey,
                              SourceKind sourceKind,
                              SourceTransportKind transportKind,
                              CandidateResolutionMode resolutionMode,
                              SourceCandidateFacts facts)
        : m_recordingMbid(std::move(recordingMbid))
        , m_candidateKey(std::move(candidateKey))
        , m_sourceKind(sourceKind)
        , m_transportKind(transportKind)
        , m_resolutionMode(resolutionMode)
        , m_facts(std::move(facts))
    {
    }

    RecordingMbid m_recordingMbid;
    SourceCandidateKey m_candidateKey;
    SourceKind m_sourceKind = SourceKind::Unknown;
    SourceTransportKind m_transportKind = SourceTransportKind::Audio;
    CandidateResolutionMode m_resolutionMode =
        CandidateResolutionMode::StableReference;
    SourceCandidateFacts m_facts;
};

struct SourceSelectionScore final {
    qint64 primary = 0;
    qint64 secondary = 0;
    qint64 tertiary = 0;
    qint64 quaternary = 0;

    bool operator==(const SourceSelectionScore &other) const noexcept
    {
        return primary == other.primary
            && secondary == other.secondary
            && tertiary == other.tertiary
            && quaternary == other.quaternary;
    }
};

class SourceKindScorer
{
public:
    virtual ~SourceKindScorer() = default;
    virtual SourceSelectionScore score(
        const SelectableSourceCandidate &candidate) const = 0;
};

class ReferenceSourceKindScorer final : public SourceKindScorer
{
public:
    SourceSelectionScore score(
        const SelectableSourceCandidate &candidate) const override;
};

class SourceSelectionPolicy final
{
public:
    static SourceResolutionResult<SourceSelectionPolicy> create(
        QVector<SourceKind> kindOrder,
        QVector<SourceProviderId> providerPreference = {});

    static SourceSelectionPolicy vinylReference();

    int kindRank(SourceKind kind) const noexcept;
    int providerRank(const SourceProviderId &provider) const noexcept;

    const QVector<SourceKind> &kindOrder() const noexcept { return m_kindOrder; }
    const QVector<SourceProviderId> &providerPreference() const noexcept
    {
        return m_providerPreference;
    }

private:
    SourceSelectionPolicy(QVector<SourceKind> kindOrder,
                          QVector<SourceProviderId> providerPreference)
        : m_kindOrder(std::move(kindOrder))
        , m_providerPreference(std::move(providerPreference))
    {
    }

    QVector<SourceKind> m_kindOrder;
    QVector<SourceProviderId> m_providerPreference;
};

enum class SourceMemoryDisposition {
    KnownWrong,
    KnownDead,
};

QString sourceMemoryDispositionName(SourceMemoryDisposition disposition);

enum class SourceMemoryOrigin {
    User,
    ConfirmedRuntime,
};

QString sourceMemoryOriginName(SourceMemoryOrigin origin);

struct SourceMemoryPin final {
    RecordingMbid recordingMbid;
    SourceCandidateKey candidateKey;

    SourceMemoryPin(RecordingMbid recording,
                    SourceCandidateKey candidate)
        : recordingMbid(std::move(recording))
        , candidateKey(std::move(candidate))
    {
    }
};

struct SourceMemoryMark final {
    RecordingMbid recordingMbid;
    SourceCandidateKey candidateKey;
    SourceMemoryDisposition disposition = SourceMemoryDisposition::KnownWrong;
    SourceMemoryOrigin origin = SourceMemoryOrigin::User;

    SourceMemoryMark(RecordingMbid recording,
                     SourceCandidateKey candidate,
                     SourceMemoryDisposition value,
                     SourceMemoryOrigin source)
        : recordingMbid(std::move(recording))
        , candidateKey(std::move(candidate))
        , disposition(value)
        , origin(source)
    {
    }
};

class SourceProfileMemory final
{
public:
    static constexpr int SchemaVersion = 1;

    explicit SourceProfileMemory(ProfileScopeId profileId)
        : m_profileId(std::move(profileId))
    {
    }

    const ProfileScopeId &profileId() const noexcept { return m_profileId; }
    const QVector<SourceMemoryPin> &pins() const noexcept { return m_pins; }
    const QVector<SourceMemoryMark> &marks() const noexcept { return m_marks; }

    SourceResolutionStatus setUserPin(
        const RecordingMbid &recording,
        SourceCandidateKey candidateKey);
    SourceResolutionStatus clearUserPin(const RecordingMbid &recording);

    SourceResolutionStatus markKnownWrong(
        const RecordingMbid &recording,
        SourceCandidateKey candidateKey,
        SourceMemoryOrigin origin);
    SourceResolutionStatus markKnownDead(
        const RecordingMbid &recording,
        SourceCandidateKey candidateKey,
        SourceMemoryOrigin origin);
    SourceResolutionStatus clearMark(
        const RecordingMbid &recording,
        const SourceCandidateKey &candidateKey);

    std::optional<SourceCandidateKey> userPinFor(
        const RecordingMbid &recording) const;
    std::optional<SourceMemoryDisposition> dispositionFor(
        const RecordingMbid &recording,
        const SourceCandidateKey &candidateKey) const;
    bool excludes(const RecordingMbid &recording,
                  const SourceCandidateKey &candidateKey) const;

    QString toCanonicalTextV1() const;
    static SourceResolutionResult<SourceProfileMemory> fromCanonicalTextV1(
        const ProfileScopeId &expectedProfile, const QString &text);

private:
    SourceResolutionStatus setMark(
        const RecordingMbid &recording,
        SourceCandidateKey candidateKey,
        SourceMemoryDisposition disposition,
        SourceMemoryOrigin origin);
    void sortCanonical();

    ProfileScopeId m_profileId;
    QVector<SourceMemoryPin> m_pins;
    QVector<SourceMemoryMark> m_marks;
};

class SourceSelector final
{
public:
    static SourceResolutionResult<QVector<SelectableSourceCandidate>> order(
        const RecordingMbid &recording,
        QVector<SelectableSourceCandidate> candidates,
        const SourceSelectionPolicy &policy,
        const SourceKindScorer &scorer,
        const SourceProfileMemory &memory);
};

class SourceResolutionRequest final
{
public:
    const QueueOccurrenceId &occurrenceId() const noexcept
    {
        return m_occurrenceId;
    }
    const RecordingMbid &recordingMbid() const noexcept
    {
        return m_recordingMbid;
    }
    const SourceCandidateKey &candidateKey() const noexcept
    {
        return m_candidateKey;
    }
    SourceKind sourceKind() const noexcept { return m_sourceKind; }
    SourceTransportKind transportKind() const noexcept
    {
        return m_transportKind;
    }
    SourceResolutionReason reason() const noexcept { return m_reason; }
    quint64 sessionSequence() const noexcept { return m_sessionSequence; }
    quint64 requestSequence() const noexcept { return m_requestSequence; }

    bool operator==(const SourceResolutionRequest &other) const noexcept
    {
        return m_occurrenceId == other.m_occurrenceId
            && m_recordingMbid == other.m_recordingMbid
            && m_candidateKey == other.m_candidateKey
            && m_sourceKind == other.m_sourceKind
            && m_transportKind == other.m_transportKind
            && m_reason == other.m_reason
            && m_sessionSequence == other.m_sessionSequence
            && m_requestSequence == other.m_requestSequence;
    }

    bool operator!=(const SourceResolutionRequest &other) const noexcept
    {
        return !(*this == other);
    }

private:
    friend class SourceResolutionSession;

    SourceResolutionRequest(QueueOccurrenceId occurrenceId,
                            RecordingMbid recordingMbid,
                            SourceCandidateKey candidateKey,
                            SourceKind sourceKind,
                            SourceTransportKind transportKind,
                            SourceResolutionReason reason,
                            quint64 sessionSequence,
                            quint64 requestSequence)
        : m_occurrenceId(std::move(occurrenceId))
        , m_recordingMbid(std::move(recordingMbid))
        , m_candidateKey(std::move(candidateKey))
        , m_sourceKind(sourceKind)
        , m_transportKind(transportKind)
        , m_reason(reason)
        , m_sessionSequence(sessionSequence)
        , m_requestSequence(requestSequence)
    {
    }

    QueueOccurrenceId m_occurrenceId;
    RecordingMbid m_recordingMbid;
    SourceCandidateKey m_candidateKey;
    SourceKind m_sourceKind = SourceKind::Unknown;
    SourceTransportKind m_transportKind = SourceTransportKind::Audio;
    SourceResolutionReason m_reason = SourceResolutionReason::Initial;
    quint64 m_sessionSequence = 0;
    quint64 m_requestSequence = 0;
};

enum class SourceReplacementPath {
    CoordinatorRestartActive,
};

QString sourceReplacementPathName(SourceReplacementPath path);

struct SourceReplacementFacts final {
    QueueOccurrenceId occurrenceId;
    RecordingMbid recordingMbid;
    SourceCandidateKey candidateKey;
    SourceTransportKind transportKind = SourceTransportKind::Audio;
    SourceReplacementPath path = SourceReplacementPath::CoordinatorRestartActive;
    std::optional<PlayInstanceId> continuationOf;
    QueueSessionStatus coordinatorStatus;

    SourceReplacementFacts(QueueOccurrenceId occurrence,
                           RecordingMbid recording,
                           SourceCandidateKey candidate,
                           SourceTransportKind transport,
                           std::optional<PlayInstanceId> continuation,
                           QueueSessionStatus status)
        : occurrenceId(std::move(occurrence))
        , recordingMbid(std::move(recording))
        , candidateKey(std::move(candidate))
        , transportKind(transport)
        , continuationOf(std::move(continuation))
        , coordinatorStatus(std::move(status))
    {
    }
};

enum class SourceFallbackTerminalReason {
    ExhaustedCandidates,
};

QString sourceFallbackTerminalReasonName(SourceFallbackTerminalReason reason);

struct SourceFallbackTerminal final {
    QueueOccurrenceId occurrenceId;
    RecordingMbid recordingMbid;
    SourceFallbackTerminalReason reason =
        SourceFallbackTerminalReason::ExhaustedCandidates;
    SourceAttemptFailureKind lastFailure =
        SourceAttemptFailureKind::ResolutionFailed;
    QVector<QString> attemptedCandidateKeys;

    SourceFallbackTerminal(QueueOccurrenceId occurrence,
                           RecordingMbid recording,
                           SourceAttemptFailureKind failure,
                           QVector<QString> attempts)
        : occurrenceId(std::move(occurrence))
        , recordingMbid(std::move(recording))
        , lastFailure(failure)
        , attemptedCandidateKeys(std::move(attempts))
    {
    }
};

enum class SourceResolutionAction {
    NeedResolution,
    PlaybackRestarted,
    Terminal,
    IgnoredStale,
    NoChange,
    Failed,
};

QString sourceResolutionActionName(SourceResolutionAction action);

struct SourceResolutionOutcome final {
    SourceResolutionAction action = SourceResolutionAction::NoChange;
    std::optional<SourceResolutionRequest> nextRequest;
    std::optional<SourceReplacementFacts> replacement;
    std::optional<SourceFallbackTerminal> terminal;
    SourceResolutionError error;

    bool ok() const noexcept
    {
        return action != SourceResolutionAction::Failed;
    }
};

class SourceResolutionSession final
{
public:
    SourceResolutionSession(VinylQueue &queue,
                            PlaybackSession &playbackSession,
                            QueueSessionCoordinator &coordinator,
                            SourceSelectionPolicy policy,
                            const SourceKindScorer &scorer,
                            SourceProfileMemory &memory,
                            ProfileScopeId expectedProfile)
        : m_queue(queue)
        , m_playbackSession(playbackSession)
        , m_coordinator(coordinator)
        , m_policy(std::move(policy))
        , m_scorer(scorer)
        , m_memory(memory)
        , m_expectedProfile(std::move(expectedProfile))
    {
    }

    SourceResolutionResult<SourceResolutionRequest> beginForActive(
        QVector<SelectableSourceCandidate> candidates,
        bool playWhenReady,
        SourceResolutionReason reason = SourceResolutionReason::Initial,
        std::optional<PlayInstanceId> continuationOf = std::nullopt);

    SourceResolutionOutcome acceptResolved(
        const SourceResolutionRequest &request,
        const ResolvedTrackSource &source,
        qint64 nowEpochMs);

    SourceResolutionOutcome resolutionFailed(
        const SourceResolutionRequest &request);

    SourceResolutionOutcome playbackFailed(
        const PlaybackWorkToken &expectedWorkToken);

    SourceResolutionOutcome refreshIfExpired(qint64 nowEpochMs);

    bool reconcileWithCoordinator();
    void invalidate() noexcept;

    const std::optional<SourceResolutionRequest> &pendingRequest() const noexcept;
    const std::optional<ResolvedTrackSource> &currentSource() const noexcept;
    std::optional<QueueOccurrenceId> occurrenceId() const;
    const SourceProfileMemory &memory() const noexcept { return m_memory; }

private:
    struct ActiveState final {
        QueueOccurrenceId occurrenceId;
        RecordingMbid recordingMbid;
        QVector<SelectableSourceCandidate> orderedCandidates;
        QVector<QString> attemptedKeys;
        bool playWhenReady = false;
        std::optional<PlayInstanceId> continuationOf;
        quint64 sessionSequence = 0;
        int currentIndex = -1;
        std::optional<SourceResolutionRequest> pendingRequest;
        std::optional<ResolvedTrackSource> currentSource;
        std::optional<PlaybackWorkToken> currentPlaybackToken;
        bool terminal = false;

        ActiveState(QueueOccurrenceId occurrence,
                    RecordingMbid recording,
                    QVector<SelectableSourceCandidate> ordered,
                    bool playReady,
                    std::optional<PlayInstanceId> continuation,
                    quint64 sequence)
            : occurrenceId(std::move(occurrence))
            , recordingMbid(std::move(recording))
            , orderedCandidates(std::move(ordered))
            , playWhenReady(playReady)
            , continuationOf(std::move(continuation))
            , sessionSequence(sequence)
        {
        }
    };

    bool coordinatorStillArms(const ActiveState &state) const;
    bool requestIsCurrent(const SourceResolutionRequest &request) const;

    SourceResolutionResult<SourceResolutionRequest> issueCandidateRequest(
        int index,
        SourceResolutionReason reason,
        bool recordAttempt);

    SourceResolutionOutcome fallbackAfter(
        SourceAttemptFailureKind failure);

    SourceResolutionOutcome freshExpiryRequest(
        const SourceResolutionRequest &expiredRequest);

    SourceResolutionOutcome staleOutcome(
        QString detail,
        bool invalidateState);

    SourceResolutionOutcome failedOutcome(
        SourceResolutionErrorCode code,
        QString field,
        QString detail);

    int nextUnattemptedIndex() const;
    const SelectableSourceCandidate *currentCandidate() const;
    static bool containsKey(const QVector<QString> &keys,
                            const QString &key);
    VinylQueue &m_queue;
    PlaybackSession &m_playbackSession;
    QueueSessionCoordinator &m_coordinator;
    SourceSelectionPolicy m_policy;
    const SourceKindScorer &m_scorer;
    SourceProfileMemory &m_memory;
    ProfileScopeId m_expectedProfile;
    quint64 m_sessionSequence = 0;
    quint64 m_requestSequence = 0;
    std::optional<ActiveState> m_active;
};

} // namespace vinyl
