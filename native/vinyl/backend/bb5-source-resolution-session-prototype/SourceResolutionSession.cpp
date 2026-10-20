#include "SourceResolutionSession.h"

#include <QByteArray>
#include <QStringList>

#include <algorithm>
#include <limits>
#include <tuple>

namespace vinyl {
namespace {

QString encodeOpaque(const QString &value)
{
    return QString::fromLatin1(
        value.toUtf8().toBase64(
            QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

std::optional<QString> decodeOpaque(const QString &value)
{
    const QByteArray decoded = QByteArray::fromBase64(
        value.toLatin1(), QByteArray::Base64UrlEncoding);
    if (decoded.isNull())
        return std::nullopt;
    return QString::fromUtf8(decoded);
}

int kindIndex(const QVector<SourceKind> &order, SourceKind kind)
{
    for (qsizetype i = 0; i < order.size(); ++i) {
        if (order.at(i) == kind)
            return static_cast<int>(i);
    }
    return std::numeric_limits<int>::max();
}

bool sameRecordingAndCandidate(const SourceMemoryPin &item,
                               const RecordingMbid &recording,
                               const SourceCandidateKey &candidate)
{
    return item.recordingMbid == recording
        && item.candidateKey == candidate;
}

bool sameRecordingAndCandidate(const SourceMemoryMark &item,
                               const RecordingMbid &recording,
                               const SourceCandidateKey &candidate)
{
    return item.recordingMbid == recording
        && item.candidateKey == candidate;
}

QString dispositionToken(SourceMemoryDisposition disposition)
{
    switch (disposition) {
    case SourceMemoryDisposition::KnownWrong:
        return QStringLiteral("wrong");
    case SourceMemoryDisposition::KnownDead:
        return QStringLiteral("dead");
    }
    return QStringLiteral("unknown");
}

std::optional<SourceMemoryDisposition> parseDisposition(const QString &text)
{
    if (text == QStringLiteral("wrong"))
        return SourceMemoryDisposition::KnownWrong;
    if (text == QStringLiteral("dead"))
        return SourceMemoryDisposition::KnownDead;
    return std::nullopt;
}

QString originToken(SourceMemoryOrigin origin)
{
    switch (origin) {
    case SourceMemoryOrigin::User:
        return QStringLiteral("user");
    case SourceMemoryOrigin::ConfirmedRuntime:
        return QStringLiteral("confirmed_runtime");
    }
    return QStringLiteral("unknown");
}

std::optional<SourceMemoryOrigin> parseOrigin(const QString &text)
{
    if (text == QStringLiteral("user"))
        return SourceMemoryOrigin::User;
    if (text == QStringLiteral("confirmed_runtime"))
        return SourceMemoryOrigin::ConfirmedRuntime;
    return std::nullopt;
}

std::optional<SourceCandidateKey> parseMemoryCandidate(
    const QString &providerText,
    const QString &candidateEncoded)
{
    const auto provider = SourceProviderId::parse(providerText);
    const auto candidateText = decodeOpaque(candidateEncoded);
    if (!provider.ok() || !candidateText.has_value())
        return std::nullopt;

    const auto candidate = SourceCandidateId::parse(*candidateText);
    if (!candidate.ok())
        return std::nullopt;

    return SourceCandidateKey(*provider.value, *candidate.value);
}

bool scoreGreater(const SourceSelectionScore &left,
                  const SourceSelectionScore &right)
{
    return std::tie(left.primary,
                    left.secondary,
                    left.tertiary,
                    left.quaternary)
        > std::tie(right.primary,
                   right.secondary,
                   right.tertiary,
                   right.quaternary);
}

} // namespace

QString sourceResolutionErrorCodeName(SourceResolutionErrorCode code)
{
    switch (code) {
    case SourceResolutionErrorCode::None:
        return QStringLiteral("none");
    case SourceResolutionErrorCode::NoActiveOccurrence:
        return QStringLiteral("no_active_occurrence");
    case SourceResolutionErrorCode::RecordingMismatch:
        return QStringLiteral("recording_mismatch");
    case SourceResolutionErrorCode::ExactResolutionRequired:
        return QStringLiteral("exact_resolution_required");
    case SourceResolutionErrorCode::CandidateNotSelectable:
        return QStringLiteral("candidate_not_selectable");
    case SourceResolutionErrorCode::DuplicateCandidate:
        return QStringLiteral("duplicate_candidate");
    case SourceResolutionErrorCode::InvalidSelectionPolicy:
        return QStringLiteral("invalid_selection_policy");
    case SourceResolutionErrorCode::InvalidClockValue:
        return QStringLiteral("invalid_clock_value");
    case SourceResolutionErrorCode::InvalidResolvedSource:
        return QStringLiteral("invalid_resolved_source");
    case SourceResolutionErrorCode::StaleRequest:
        return QStringLiteral("stale_request");
    case SourceResolutionErrorCode::CoordinatorRejected:
        return QStringLiteral("coordinator_rejected");
    case SourceResolutionErrorCode::CounterExhausted:
        return QStringLiteral("counter_exhausted");
    case SourceResolutionErrorCode::FallbackExhausted:
        return QStringLiteral("fallback_exhausted");
    case SourceResolutionErrorCode::ProfileMismatch:
        return QStringLiteral("profile_mismatch");
    case SourceResolutionErrorCode::InvalidMemoryState:
        return QStringLiteral("invalid_memory_state");
    }
    return QStringLiteral("unknown");
}

QString sourceTransportKindName(SourceTransportKind kind)
{
    switch (kind) {
    case SourceTransportKind::Audio:
        return QStringLiteral("audio");
    case SourceTransportKind::Video:
        return QStringLiteral("video");
    }
    return QStringLiteral("unknown");
}

QString sourceResolutionReasonName(SourceResolutionReason reason)
{
    switch (reason) {
    case SourceResolutionReason::Initial:
        return QStringLiteral("initial");
    case SourceResolutionReason::Fallback:
        return QStringLiteral("fallback");
    case SourceResolutionReason::ExpiryRefresh:
        return QStringLiteral("expiry_refresh");
    case SourceResolutionReason::ExplicitSwitch:
        return QStringLiteral("explicit_switch");
    }
    return QStringLiteral("unknown");
}

QString sourceAttemptFailureKindName(SourceAttemptFailureKind kind)
{
    switch (kind) {
    case SourceAttemptFailureKind::ResolutionFailed:
        return QStringLiteral("resolution_failed");
    case SourceAttemptFailureKind::PlaybackFailed:
        return QStringLiteral("playback_failed");
    case SourceAttemptFailureKind::Expired:
        return QStringLiteral("expired");
    case SourceAttemptFailureKind::InvalidResolvedTransport:
        return QStringLiteral("invalid_resolved_transport");
    }
    return QStringLiteral("unknown");
}

SourceResolutionResult<SelectableSourceCandidate>
SelectableSourceCandidate::fromExactAudio(
    const NormalizedSourceCandidate &candidate,
    const RequestedTrackIdentity &request,
    const AlbumSourceManifest &manifest,
    const ExactTrackResolverPolicy &policy)
{
    if (candidate.requestedRecordingMbid() != request.recordingMbid()) {
        return SourceResolutionResult<SelectableSourceCandidate>::failure(
            SourceResolutionErrorCode::RecordingMismatch,
            QStringLiteral("candidate.requestedRecordingMbid"),
            QStringLiteral("BB1 candidate and BB2 request must retain the same Recording MBID."));
    }

    if (!manifest.sourceCandidateKey().has_value()
        || *manifest.sourceCandidateKey() != candidate.provenance().key()) {
        return SourceResolutionResult<SelectableSourceCandidate>::failure(
            SourceResolutionErrorCode::CandidateNotSelectable,
            QStringLiteral("manifest.sourceCandidateKey"),
            QStringLiteral("Exact-track evidence must be bound to this exact provider candidate."));
    }

    const ExactTrackResolution resolution =
        ExactTrackResolver::resolve(request, manifest, policy);
    if (resolution.outcome != ExactTrackOutcome::Exact
        || !resolution.selectedEntryId.has_value()) {
        return SourceResolutionResult<SelectableSourceCandidate>::failure(
            SourceResolutionErrorCode::ExactResolutionRequired,
            QStringLiteral("exactTrackResolution"),
            QStringLiteral("Only BB2 Exact outcomes are selectable; ambiguous/nonexact evidence fails closed."));
    }

    if (candidate.failure().has_value() && !candidate.failure()->retryable) {
        return SourceResolutionResult<SelectableSourceCandidate>::failure(
            SourceResolutionErrorCode::CandidateNotSelectable,
            QStringLiteral("candidate.failure"),
            QStringLiteral("A currently non-retryable candidate failure cannot enter this attempt chain."));
    }

    return SourceResolutionResult<SelectableSourceCandidate>::success(
        SelectableSourceCandidate(
            candidate.requestedRecordingMbid(),
            candidate.provenance().key(),
            candidate.sourceKind(),
            SourceTransportKind::Audio,
            candidate.resolutionMode(),
            candidate.facts()));
}

SourceResolutionResult<SelectableSourceCandidate>
SelectableSourceCandidate::fromQualifiedVideo(
    const RelatedVideoCandidate &candidate)
{
    if (!candidate.eligibleForAudioVideoSwitch()) {
        return SourceResolutionResult<SelectableSourceCandidate>::failure(
            SourceResolutionErrorCode::CandidateNotSelectable,
            QStringLiteral("candidate.qualification"),
            QStringLiteral("Only BF2 Audio | Video-qualified candidates enter direct video resolution."));
    }

    const auto provider = SourceProviderId::parse(
        candidate.provenance().providerItem().providerId().value());
    const auto item = SourceCandidateId::parse(
        candidate.provenance().providerItem().itemId().value());
    if (!provider.ok() || !item.ok()) {
        return SourceResolutionResult<SelectableSourceCandidate>::failure(
            SourceResolutionErrorCode::CandidateNotSelectable,
            QStringLiteral("candidate.provenance"),
            QStringLiteral("BF2 provider provenance could not be represented as a source candidate key."));
    }

    SourceCandidateFacts facts;
    facts.durationMs = candidate.facts().durationMs;
    facts.title = candidate.facts().title;

    return SourceResolutionResult<SelectableSourceCandidate>::success(
        SelectableSourceCandidate(
            candidate.recordingMbid(),
            SourceCandidateKey(*provider.value, *item.value),
            SourceKind::ExternalResolver,
            SourceTransportKind::Video,
            CandidateResolutionMode::Refreshable,
            std::move(facts)));
}

SourceSelectionScore ReferenceSourceKindScorer::score(
    const SelectableSourceCandidate &candidate) const
{
    SourceSelectionScore score;
    if (candidate.transportKind() == SourceTransportKind::Video)
        return score;

    const auto &facts = candidate.facts();
    score.primary = facts.lossless.value_or(false) ? 1 : 0;
    score.secondary = facts.bitrateKbps.value_or(0);
    score.tertiary = facts.sampleRateHz.value_or(0);
    score.quaternary = facts.bitDepth.value_or(0);
    return score;
}

SourceResolutionResult<SourceSelectionPolicy> SourceSelectionPolicy::create(
    QVector<SourceKind> kindOrder,
    QVector<SourceProviderId> providerPreference)
{
    if (kindOrder.isEmpty()) {
        return SourceResolutionResult<SourceSelectionPolicy>::failure(
            SourceResolutionErrorCode::InvalidSelectionPolicy,
            QStringLiteral("kindOrder"),
            QStringLiteral("Source-kind order must be explicit and non-empty."));
    }

    for (qsizetype i = 0; i < kindOrder.size(); ++i) {
        for (qsizetype j = i + 1; j < kindOrder.size(); ++j) {
            if (kindOrder.at(i) == kindOrder.at(j)) {
                return SourceResolutionResult<SourceSelectionPolicy>::failure(
                    SourceResolutionErrorCode::InvalidSelectionPolicy,
                    QStringLiteral("kindOrder"),
                    QStringLiteral("Source-kind order must not contain duplicates."));
            }
        }
    }

    for (qsizetype i = 0; i < providerPreference.size(); ++i) {
        for (qsizetype j = i + 1; j < providerPreference.size(); ++j) {
            if (providerPreference.at(i) == providerPreference.at(j)) {
                return SourceResolutionResult<SourceSelectionPolicy>::failure(
                    SourceResolutionErrorCode::InvalidSelectionPolicy,
                    QStringLiteral("providerPreference"),
                    QStringLiteral("Provider preference must not contain duplicates."));
            }
        }
    }

    return SourceResolutionResult<SourceSelectionPolicy>::success(
        SourceSelectionPolicy(
            std::move(kindOrder),
            std::move(providerPreference)));
}

SourceSelectionPolicy SourceSelectionPolicy::vinylReference()
{
    return SourceSelectionPolicy(
        {
            SourceKind::LocalFile,
            SourceKind::PeerToPeer,
            SourceKind::Http,
            SourceKind::ProviderMediated,
            SourceKind::ExternalResolver,
            SourceKind::Unknown,
        },
        {});
}

int SourceSelectionPolicy::kindRank(SourceKind kind) const noexcept
{
    return kindIndex(m_kindOrder, kind);
}

int SourceSelectionPolicy::providerRank(
    const SourceProviderId &provider) const noexcept
{
    for (qsizetype i = 0; i < m_providerPreference.size(); ++i) {
        if (m_providerPreference.at(i) == provider)
            return static_cast<int>(i);
    }
    return std::numeric_limits<int>::max();
}

QString sourceMemoryDispositionName(SourceMemoryDisposition disposition)
{
    return dispositionToken(disposition);
}

QString sourceMemoryOriginName(SourceMemoryOrigin origin)
{
    return originToken(origin);
}

SourceResolutionStatus SourceProfileMemory::setUserPin(
    const RecordingMbid &recording,
    SourceCandidateKey candidateKey)
{
    for (auto &pin : m_pins) {
        if (pin.recordingMbid == recording) {
            if (pin.candidateKey == candidateKey)
                return SourceResolutionStatus::success(false);
            pin.candidateKey = std::move(candidateKey);
            sortCanonical();
            return SourceResolutionStatus::success(true);
        }
    }

    m_pins.push_back(SourceMemoryPin(recording, std::move(candidateKey)));
    sortCanonical();
    return SourceResolutionStatus::success(true);
}

SourceResolutionStatus SourceProfileMemory::clearUserPin(
    const RecordingMbid &recording)
{
    const auto before = m_pins.size();
    m_pins.erase(
        std::remove_if(
            m_pins.begin(),
            m_pins.end(),
            [&recording](const SourceMemoryPin &pin) {
                return pin.recordingMbid == recording;
            }),
        m_pins.end());
    return SourceResolutionStatus::success(m_pins.size() != before);
}

SourceResolutionStatus SourceProfileMemory::markKnownWrong(
    const RecordingMbid &recording,
    SourceCandidateKey candidateKey,
    SourceMemoryOrigin origin)
{
    return setMark(
        recording,
        std::move(candidateKey),
        SourceMemoryDisposition::KnownWrong,
        origin);
}

SourceResolutionStatus SourceProfileMemory::markKnownDead(
    const RecordingMbid &recording,
    SourceCandidateKey candidateKey,
    SourceMemoryOrigin origin)
{
    return setMark(
        recording,
        std::move(candidateKey),
        SourceMemoryDisposition::KnownDead,
        origin);
}

SourceResolutionStatus SourceProfileMemory::clearMark(
    const RecordingMbid &recording,
    const SourceCandidateKey &candidateKey)
{
    const auto before = m_marks.size();
    m_marks.erase(
        std::remove_if(
            m_marks.begin(),
            m_marks.end(),
            [&](const SourceMemoryMark &mark) {
                return sameRecordingAndCandidate(
                    mark, recording, candidateKey);
            }),
        m_marks.end());
    return SourceResolutionStatus::success(m_marks.size() != before);
}

std::optional<SourceCandidateKey> SourceProfileMemory::userPinFor(
    const RecordingMbid &recording) const
{
    for (const auto &pin : m_pins) {
        if (pin.recordingMbid == recording)
            return pin.candidateKey;
    }
    return std::nullopt;
}

std::optional<SourceMemoryDisposition> SourceProfileMemory::dispositionFor(
    const RecordingMbid &recording,
    const SourceCandidateKey &candidateKey) const
{
    for (const auto &mark : m_marks) {
        if (sameRecordingAndCandidate(mark, recording, candidateKey))
            return mark.disposition;
    }
    return std::nullopt;
}

bool SourceProfileMemory::excludes(
    const RecordingMbid &recording,
    const SourceCandidateKey &candidateKey) const
{
    return dispositionFor(recording, candidateKey).has_value();
}

SourceResolutionStatus SourceProfileMemory::setMark(
    const RecordingMbid &recording,
    SourceCandidateKey candidateKey,
    SourceMemoryDisposition disposition,
    SourceMemoryOrigin origin)
{
    for (auto &mark : m_marks) {
        if (sameRecordingAndCandidate(mark, recording, candidateKey)) {
            const bool changed =
                mark.disposition != disposition || mark.origin != origin;
            mark.disposition = disposition;
            mark.origin = origin;
            sortCanonical();
            return SourceResolutionStatus::success(changed);
        }
    }

    m_marks.push_back(SourceMemoryMark(
        recording, std::move(candidateKey), disposition, origin));
    sortCanonical();
    return SourceResolutionStatus::success(true);
}

void SourceProfileMemory::sortCanonical()
{
    std::sort(
        m_pins.begin(),
        m_pins.end(),
        [](const SourceMemoryPin &left, const SourceMemoryPin &right) {
            if (left.recordingMbid != right.recordingMbid)
                return left.recordingMbid.toString()
                    < right.recordingMbid.toString();
            return left.candidateKey.stableKey()
                < right.candidateKey.stableKey();
        });
    std::sort(
        m_marks.begin(),
        m_marks.end(),
        [](const SourceMemoryMark &left, const SourceMemoryMark &right) {
            if (left.recordingMbid != right.recordingMbid)
                return left.recordingMbid.toString()
                    < right.recordingMbid.toString();
            return left.candidateKey.stableKey()
                < right.candidateKey.stableKey();
        });
}

QString SourceProfileMemory::toCanonicalTextV1() const
{
    QStringList lines;
    lines << QStringLiteral("colosseum.vinyl.source-memory|1|%1")
                 .arg(encodeOpaque(m_profileId.value()));

    for (const auto &pin : m_pins) {
        lines << QStringLiteral("pin|%1|%2|%3")
                     .arg(
                         pin.recordingMbid.toString(),
                         pin.candidateKey.providerId().value(),
                         encodeOpaque(
                             pin.candidateKey.candidateId().value()));
    }

    for (const auto &mark : m_marks) {
        lines << QStringLiteral("mark|%1|%2|%3|%4|%5")
                     .arg(
                         mark.recordingMbid.toString(),
                         mark.candidateKey.providerId().value(),
                         encodeOpaque(
                             mark.candidateKey.candidateId().value()),
                         dispositionToken(mark.disposition),
                         originToken(mark.origin));
    }

    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

SourceResolutionResult<SourceProfileMemory>
SourceProfileMemory::fromCanonicalTextV1(
    const ProfileScopeId &expectedProfile, const QString &text)
{
    QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();

    if (lines.isEmpty())
        return SourceResolutionResult<SourceProfileMemory>::failure(
            SourceResolutionErrorCode::InvalidMemoryState,
            QStringLiteral("header"),
            QStringLiteral("Source-memory document is empty."));

    const QStringList header = lines.first().split(QLatin1Char('|'));
    if (header.size() != 3
        || header.at(0) != QStringLiteral("colosseum.vinyl.source-memory")
        || header.at(1) != QStringLiteral("1")) {
        return SourceResolutionResult<SourceProfileMemory>::failure(
            SourceResolutionErrorCode::InvalidMemoryState,
            QStringLiteral("header"),
            QStringLiteral("Source-memory schema header/version is invalid."));
    }

    const auto profileText = decodeOpaque(header.at(2));
    if (!profileText.has_value()) {
        return SourceResolutionResult<SourceProfileMemory>::failure(
            SourceResolutionErrorCode::InvalidMemoryState,
            QStringLiteral("profileId"),
            QStringLiteral("Profile scope encoding is invalid."));
    }

    const auto profile = ProfileScopeId::parse(*profileText);
    if (!profile.ok()) {
        return SourceResolutionResult<SourceProfileMemory>::failure(
            SourceResolutionErrorCode::InvalidMemoryState,
            QStringLiteral("profileId"),
            QStringLiteral("Profile scope is invalid."));
    }

    if (!(expectedProfile == *profile.value)) {
        return SourceResolutionResult<SourceProfileMemory>::failure(
            SourceResolutionErrorCode::ProfileMismatch,
            QStringLiteral("profileId"),
            QStringLiteral("Source memory belongs to another profile."));
    }
    SourceProfileMemory memory(*profile.value);
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QStringList fields = lines.at(i).split(QLatin1Char('|'));
        if (fields.isEmpty()) {
            return SourceResolutionResult<SourceProfileMemory>::failure(
                SourceResolutionErrorCode::InvalidMemoryState,
                QStringLiteral("line"),
                QStringLiteral("Blank source-memory rows are not allowed."));
        }

        if (fields.at(0) == QStringLiteral("pin") && fields.size() == 4) {
            const auto recording = RecordingMbid::parse(fields.at(1));
            const auto key = parseMemoryCandidate(fields.at(2), fields.at(3));
            if (!recording.ok() || !key.has_value()) {
                return SourceResolutionResult<SourceProfileMemory>::failure(
                    SourceResolutionErrorCode::InvalidMemoryState,
                    QStringLiteral("pin"),
                    QStringLiteral("Source-memory pin contains invalid identity."));
            }
            memory.setUserPin(*recording.value, *key);
            continue;
        }

        if (fields.at(0) == QStringLiteral("mark") && fields.size() == 6) {
            const auto recording = RecordingMbid::parse(fields.at(1));
            const auto key = parseMemoryCandidate(fields.at(2), fields.at(3));
            const auto disposition = parseDisposition(fields.at(4));
            const auto origin = parseOrigin(fields.at(5));
            if (!recording.ok() || !key.has_value()
                || !disposition.has_value() || !origin.has_value()) {
                return SourceResolutionResult<SourceProfileMemory>::failure(
                    SourceResolutionErrorCode::InvalidMemoryState,
                    QStringLiteral("mark"),
                    QStringLiteral("Source-memory mark contains invalid identity or state."));
            }
            memory.setMark(
                *recording.value,
                *key,
                *disposition,
                *origin);
            continue;
        }

        return SourceResolutionResult<SourceProfileMemory>::failure(
            SourceResolutionErrorCode::InvalidMemoryState,
            QStringLiteral("line"),
            QStringLiteral("Unknown or malformed source-memory row."));
    }

    return SourceResolutionResult<SourceProfileMemory>::success(
        std::move(memory));
}

SourceResolutionResult<QVector<SelectableSourceCandidate>>
SourceSelector::order(
    const RecordingMbid &recording,
    QVector<SelectableSourceCandidate> candidates,
    const SourceSelectionPolicy &policy,
    const SourceKindScorer &scorer,
    const SourceProfileMemory &memory)
{
    QVector<SelectableSourceCandidate> filtered;
    filtered.reserve(candidates.size());

    for (const auto &candidate : candidates) {
        if (candidate.recordingMbid() != recording) {
            return SourceResolutionResult<QVector<SelectableSourceCandidate>>::failure(
                SourceResolutionErrorCode::RecordingMismatch,
                QStringLiteral("candidates"),
                QStringLiteral("All selectable candidates must retain the active Recording MBID."));
        }
        if (policy.kindRank(candidate.sourceKind())
            == std::numeric_limits<int>::max()) {
            return SourceResolutionResult<QVector<SelectableSourceCandidate>>::failure(
                SourceResolutionErrorCode::InvalidSelectionPolicy,
                QStringLiteral("kindOrder"),
                QStringLiteral("Every selectable candidate kind must have an explicit cross-kind rank."));
        }
        if (!memory.excludes(recording, candidate.candidateKey()))
            filtered.push_back(candidate);
    }

    std::sort(
        filtered.begin(),
        filtered.end(),
        [&](const SelectableSourceCandidate &left,
            const SelectableSourceCandidate &right) {
            const auto pin = memory.userPinFor(recording);
            const bool leftPinned =
                pin.has_value() && *pin == left.candidateKey();
            const bool rightPinned =
                pin.has_value() && *pin == right.candidateKey();
            if (leftPinned != rightPinned)
                return leftPinned;

            const int leftKind = policy.kindRank(left.sourceKind());
            const int rightKind = policy.kindRank(right.sourceKind());
            if (leftKind != rightKind)
                return leftKind < rightKind;

            const SourceSelectionScore leftScore = scorer.score(left);
            const SourceSelectionScore rightScore = scorer.score(right);
            if (!(leftScore == rightScore))
                return scoreGreater(leftScore, rightScore);

            const int leftProvider =
                policy.providerRank(left.candidateKey().providerId());
            const int rightProvider =
                policy.providerRank(right.candidateKey().providerId());
            if (leftProvider != rightProvider)
                return leftProvider < rightProvider;

            return left.candidateKey().stableKey()
                < right.candidateKey().stableKey();
        });

    for (qsizetype i = 1; i < filtered.size(); ++i) {
        if (filtered.at(i - 1).candidateKey()
            == filtered.at(i).candidateKey()) {
            return SourceResolutionResult<QVector<SelectableSourceCandidate>>::failure(
                SourceResolutionErrorCode::DuplicateCandidate,
                QStringLiteral("candidates"),
                QStringLiteral("A source candidate may appear only once in an attempt chain."));
        }
    }

    return SourceResolutionResult<QVector<SelectableSourceCandidate>>::success(
        std::move(filtered));
}

QString sourceReplacementPathName(SourceReplacementPath path)
{
    switch (path) {
    case SourceReplacementPath::CoordinatorRestartActive:
        return QStringLiteral("coordinator_restart_active");
    }
    return QStringLiteral("unknown");
}

QString sourceFallbackTerminalReasonName(SourceFallbackTerminalReason reason)
{
    switch (reason) {
    case SourceFallbackTerminalReason::ExhaustedCandidates:
        return QStringLiteral("exhausted_candidates");
    }
    return QStringLiteral("unknown");
}

QString sourceResolutionActionName(SourceResolutionAction action)
{
    switch (action) {
    case SourceResolutionAction::NeedResolution:
        return QStringLiteral("need_resolution");
    case SourceResolutionAction::PlaybackRestarted:
        return QStringLiteral("playback_restarted");
    case SourceResolutionAction::Terminal:
        return QStringLiteral("terminal");
    case SourceResolutionAction::IgnoredStale:
        return QStringLiteral("ignored_stale");
    case SourceResolutionAction::NoChange:
        return QStringLiteral("no_change");
    case SourceResolutionAction::Failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("unknown");
}

SourceResolutionResult<SourceResolutionRequest>
SourceResolutionSession::beginForActive(
    QVector<SelectableSourceCandidate> candidates,
    bool playWhenReady,
    SourceResolutionReason reason,
    std::optional<PlayInstanceId> continuationOf)
{
    if (!(m_memory.profileId() == m_expectedProfile)) {
        invalidate();
        return SourceResolutionResult<SourceResolutionRequest>::failure(
            SourceResolutionErrorCode::ProfileMismatch,
            QStringLiteral("profileId"),
            QStringLiteral("Source session memory belongs to another profile."));
    }
    const QueueOccurrence *active = m_queue.activeOccurrence();
    if (!active) {
        return SourceResolutionResult<SourceResolutionRequest>::failure(
            SourceResolutionErrorCode::NoActiveOccurrence,
            QStringLiteral("activeOccurrence"),
            QStringLiteral("Source resolution requires an exact BA1 active occurrence."));
    }

    auto ordered = SourceSelector::order(
        active->recordingMbid(),
        std::move(candidates),
        m_policy,
        m_scorer,
        m_memory);
    if (!ordered.ok())
        return SourceResolutionResult<SourceResolutionRequest>::failure(
            ordered.error.code,
            ordered.error.field,
            ordered.error.detail);

    if (ordered.value->isEmpty()) {
        return SourceResolutionResult<SourceResolutionRequest>::failure(
            SourceResolutionErrorCode::CandidateNotSelectable,
            QStringLiteral("candidates"),
            QStringLiteral("No Exact non-excluded source candidate is selectable."));
    }

    if (m_sessionSequence == std::numeric_limits<quint64>::max()) {
        return SourceResolutionResult<SourceResolutionRequest>::failure(
            SourceResolutionErrorCode::CounterExhausted,
            QStringLiteral("sessionSequence"),
            QStringLiteral("Source-resolution session sequence is exhausted."));
    }

    ++m_sessionSequence;
    m_active.emplace(
        active->occurrenceId(),
        active->recordingMbid(),
        std::move(*ordered.value),
        playWhenReady,
        std::move(continuationOf),
        m_sessionSequence);

    QueueSessionStatus awaiting =
        m_coordinator.awaitInput(active->occurrenceId(), playWhenReady);
    if (!awaiting.ok) {
        m_active.reset();
        return SourceResolutionResult<SourceResolutionRequest>::failure(
            SourceResolutionErrorCode::CoordinatorRejected,
            QStringLiteral("coordinator.awaitInput"),
            awaiting.error.detail);
    }

    return issueCandidateRequest(0, reason, true);
}

SourceResolutionResult<SourceResolutionRequest>
SourceResolutionSession::issueCandidateRequest(
    int index,
    SourceResolutionReason reason,
    bool recordAttempt)
{
    if (!m_active.has_value()
        || index < 0
        || index >= m_active->orderedCandidates.size()) {
        return SourceResolutionResult<SourceResolutionRequest>::failure(
            SourceResolutionErrorCode::CandidateNotSelectable,
            QStringLiteral("candidateIndex"),
            QStringLiteral("Source attempt index is outside the ordered candidate set."));
    }

    if (m_requestSequence == std::numeric_limits<quint64>::max()) {
        return SourceResolutionResult<SourceResolutionRequest>::failure(
            SourceResolutionErrorCode::CounterExhausted,
            QStringLiteral("requestSequence"),
            QStringLiteral("Source-resolution request sequence is exhausted."));
    }

    ++m_requestSequence;
    m_active->currentIndex = index;

    const auto &candidate = m_active->orderedCandidates.at(index);
    const QString stableKey = candidate.candidateKey().stableKey();
    if (recordAttempt
        && !containsKey(m_active->attemptedKeys, stableKey)) {
        m_active->attemptedKeys.push_back(stableKey);
    }

    SourceResolutionRequest request(
        m_active->occurrenceId,
        m_active->recordingMbid,
        candidate.candidateKey(),
        candidate.sourceKind(),
        candidate.transportKind(),
        reason,
        m_active->sessionSequence,
        m_requestSequence);

    m_active->pendingRequest = request;
    return SourceResolutionResult<SourceResolutionRequest>::success(
        std::move(request));
}

bool SourceResolutionSession::coordinatorStillArms(
    const ActiveState &state) const
{
    const auto activeId = m_queue.activeOccurrenceId();
    if (!activeId.has_value() || *activeId != state.occurrenceId)
        return false;

    const QueueOccurrence *active = m_queue.activeOccurrence();
    if (!active || active->recordingMbid() != state.recordingMbid)
        return false;

    const auto &pending = m_coordinator.pendingInput();
    if (pending.has_value())
        return pending->occurrenceId == state.occurrenceId;

    const auto &bound = m_playbackSession.activeOccurrence();
    return bound.has_value()
        && bound->occurrenceId() == state.occurrenceId;
}

bool SourceResolutionSession::requestIsCurrent(
    const SourceResolutionRequest &request) const
{
    return m_active.has_value()
        && m_active->pendingRequest.has_value()
        && *m_active->pendingRequest == request
        && request.occurrenceId() == m_active->occurrenceId
        && request.recordingMbid() == m_active->recordingMbid
        && coordinatorStillArms(*m_active);
}

SourceResolutionOutcome SourceResolutionSession::staleOutcome(
    QString detail,
    bool invalidateState)
{
    if (invalidateState)
        m_active.reset();

    SourceResolutionOutcome out;
    out.action = SourceResolutionAction::IgnoredStale;
    out.error = SourceResolutionError{
        SourceResolutionErrorCode::StaleRequest,
        QStringLiteral("request"),
        std::move(detail)};
    return out;
}

SourceResolutionOutcome SourceResolutionSession::failedOutcome(
    SourceResolutionErrorCode code,
    QString field,
    QString detail)
{
    SourceResolutionOutcome out;
    out.action = SourceResolutionAction::Failed;
    out.error = SourceResolutionError{
        code, std::move(field), std::move(detail)};
    return out;
}

SourceResolutionOutcome SourceResolutionSession::acceptResolved(
    const SourceResolutionRequest &request,
    const ResolvedTrackSource &source,
    qint64 nowEpochMs)
{
    if (!(m_memory.profileId() == m_expectedProfile)) {
        invalidate();
        return failedOutcome(SourceResolutionErrorCode::ProfileMismatch,
                             QStringLiteral("profileId"),
                             QStringLiteral("Source session memory belongs to another profile."));
    }
    if (nowEpochMs < 0) {
        return failedOutcome(
            SourceResolutionErrorCode::InvalidClockValue,
            QStringLiteral("nowEpochMs"),
            QStringLiteral("Current epoch time must be non-negative."));
    }

    if (!m_active.has_value())
        return staleOutcome(
            QStringLiteral("No source-resolution session is active."),
            false);

    if (!requestIsCurrent(request)) {
        const bool stillSameOccurrence =
            coordinatorStillArms(*m_active);
        return staleOutcome(
            QStringLiteral("Resolver output does not belong to the current occurrence/request fence."),
            !stillSameOccurrence);
    }

    if (source.recordingMbid() != request.recordingMbid()
        || source.sourceCandidate() != request.candidateKey()
        || source.sourceKind() != request.sourceKind()) {
        m_active->pendingRequest.reset();
        return fallbackAfter(
            SourceAttemptFailureKind::InvalidResolvedTransport);
    }

    if (source.isExpiredAt(nowEpochMs)) {
        m_active->pendingRequest.reset();
        return freshExpiryRequest(request);
    }

    auto load = MpvPlaybackSourceAdapter::toPlaybackLoadInput(
        source, request.recordingMbid());
    if (!load.ok()) {
        m_active->pendingRequest.reset();
        return fallbackAfter(
            SourceAttemptFailureKind::InvalidResolvedTransport);
    }

    QueueSessionStatus restarted =
        m_coordinator.restartActive(
            request.occurrenceId(),
            *load.value);
    if (!restarted.ok) {
        const bool stillSameOccurrence =
            m_active.has_value()
            && coordinatorStillArms(*m_active);
        if (!stillSameOccurrence
            || restarted.error.code
                == QueueSessionErrorCode::ActiveOccurrenceMismatch
            || restarted.error.code
                == QueueSessionErrorCode::ReplacementNotArmed) {
            return staleOutcome(
                QStringLiteral("Coordinator rejected a source result after the active replacement fence changed."),
                true);
        }
        return failedOutcome(
            SourceResolutionErrorCode::CoordinatorRejected,
            QStringLiteral("coordinator.restartActive"),
            restarted.error.detail);
    }

    m_active->pendingRequest.reset();
    m_active->currentSource = source;
    m_active->currentPlaybackToken = m_playbackSession.activeWorkToken();

    SourceResolutionOutcome out;
    out.action = SourceResolutionAction::PlaybackRestarted;
    out.replacement.emplace(
        request.occurrenceId(),
        request.recordingMbid(),
        request.candidateKey(),
        request.transportKind(),
        m_active->continuationOf,
        std::move(restarted));
    return out;
}

SourceResolutionOutcome SourceResolutionSession::resolutionFailed(
    const SourceResolutionRequest &request)
{
    if (!(m_memory.profileId() == m_expectedProfile)) {
        invalidate();
        return failedOutcome(SourceResolutionErrorCode::ProfileMismatch,
                             QStringLiteral("profileId"),
                             QStringLiteral("Source session memory belongs to another profile."));
    }
    if (!m_active.has_value())
        return staleOutcome(
            QStringLiteral("Late resolver failure arrived after source-session teardown."),
            false);

    if (!requestIsCurrent(request)) {
        const bool stillSameOccurrence =
            coordinatorStillArms(*m_active);
        return staleOutcome(
            QStringLiteral("Resolver failure does not belong to the current occurrence/request fence."),
            !stillSameOccurrence);
    }

    m_active->pendingRequest.reset();
    return fallbackAfter(SourceAttemptFailureKind::ResolutionFailed);
}

SourceResolutionOutcome SourceResolutionSession::playbackFailed(
    const PlaybackWorkToken &expectedWorkToken)
{
    if (!(m_memory.profileId() == m_expectedProfile)) {
        invalidate();
        return failedOutcome(SourceResolutionErrorCode::ProfileMismatch,
                             QStringLiteral("profileId"),
                             QStringLiteral("Source session memory belongs to another profile."));
    }
    if (!m_active.has_value()
        || !m_active->currentPlaybackToken.has_value()
        || *m_active->currentPlaybackToken != expectedWorkToken
        || m_active->pendingRequest.has_value()
        || m_playbackSession.state() != PlaybackState::Failed) {
        return staleOutcome(
            QStringLiteral("Playback failure belongs to another source session."),
            false);
    }

    if (!coordinatorStillArms(*m_active)) {
        return staleOutcome(
            QStringLiteral("Playback failure arrived after active occurrence/replacement authority changed."),
            true);
    }

    m_active->pendingRequest.reset();
    return fallbackAfter(SourceAttemptFailureKind::PlaybackFailed);
}

SourceResolutionOutcome SourceResolutionSession::fallbackAfter(
    SourceAttemptFailureKind failure)
{
    if (!m_active.has_value())
        return failedOutcome(
            SourceResolutionErrorCode::FallbackExhausted,
            QStringLiteral("session"),
            QStringLiteral("Fallback requested without an active source session."));

    m_active->currentPlaybackToken.reset();
    const int next = nextUnattemptedIndex();
    if (next < 0) {
        m_active->terminal = true;
        m_active->pendingRequest.reset();

        SourceResolutionOutcome out;
        out.action = SourceResolutionAction::Terminal;
        out.terminal.emplace(
            m_active->occurrenceId,
            m_active->recordingMbid,
            failure,
            m_active->attemptedKeys);
        return out;
    }

    QueueSessionStatus awaiting =
        m_coordinator.awaitInput(
            m_active->occurrenceId,
            m_active->playWhenReady);
    if (!awaiting.ok) {
        return staleOutcome(
            QStringLiteral("Coordinator no longer accepts async fallback for this occurrence."),
            true);
    }

    auto nextRequest =
        issueCandidateRequest(
            next,
            SourceResolutionReason::Fallback,
            true);
    if (!nextRequest.ok()) {
        return failedOutcome(
            nextRequest.error.code,
            nextRequest.error.field,
            nextRequest.error.detail);
    }

    SourceResolutionOutcome out;
    out.action = SourceResolutionAction::NeedResolution;
    out.nextRequest = *nextRequest.value;
    return out;
}

SourceResolutionOutcome SourceResolutionSession::freshExpiryRequest(
    const SourceResolutionRequest &expiredRequest)
{
    if (!m_active.has_value())
        return staleOutcome(
            QStringLiteral("Expiry refresh belongs to a closed source session."),
            false);

    if (expiredRequest.reason() == SourceResolutionReason::ExpiryRefresh) {
        return fallbackAfter(SourceAttemptFailureKind::Expired);
    }

    auto refresh = issueCandidateRequest(
        m_active->currentIndex,
        SourceResolutionReason::ExpiryRefresh,
        false);
    if (!refresh.ok()) {
        return failedOutcome(
            refresh.error.code,
            refresh.error.field,
            refresh.error.detail);
    }

    SourceResolutionOutcome out;
    out.action = SourceResolutionAction::NeedResolution;
    out.nextRequest = *refresh.value;
    return out;
}

SourceResolutionOutcome SourceResolutionSession::refreshIfExpired(
    qint64 nowEpochMs)
{
    if (!(m_memory.profileId() == m_expectedProfile)) {
        invalidate();
        return failedOutcome(SourceResolutionErrorCode::ProfileMismatch,
                             QStringLiteral("profileId"),
                             QStringLiteral("Source session memory belongs to another profile."));
    }
    if (nowEpochMs < 0) {
        return failedOutcome(
            SourceResolutionErrorCode::InvalidClockValue,
            QStringLiteral("nowEpochMs"),
            QStringLiteral("Current epoch time must be non-negative."));
    }

    if (!m_active.has_value() || !m_active->currentSource.has_value()
        || m_active->pendingRequest.has_value() || m_active->terminal) {
        SourceResolutionOutcome out;
        out.action = SourceResolutionAction::NoChange;
        return out;
    }

    if (!coordinatorStillArms(*m_active)) {
        return staleOutcome(
            QStringLiteral("Current transport no longer belongs to the coordinated active occurrence."),
            true);
    }

    if (!m_active->currentSource->isExpiredAt(nowEpochMs)) {
        SourceResolutionOutcome out;
        out.action = SourceResolutionAction::NoChange;
        return out;
    }

    QueueSessionStatus awaiting =
        m_coordinator.awaitInput(
            m_active->occurrenceId,
            m_active->playWhenReady);
    if (!awaiting.ok) {
        return staleOutcome(
            QStringLiteral("Coordinator no longer accepts expiry re-resolution for this occurrence."),
            true);
    }

    auto refresh = issueCandidateRequest(
        m_active->currentIndex,
        SourceResolutionReason::ExpiryRefresh,
        false);
    if (!refresh.ok()) {
        return failedOutcome(
            refresh.error.code,
            refresh.error.field,
            refresh.error.detail);
    }

    SourceResolutionOutcome out;
    out.action = SourceResolutionAction::NeedResolution;
    out.nextRequest = *refresh.value;
    return out;
}

int SourceResolutionSession::nextUnattemptedIndex() const
{
    if (!m_active.has_value())
        return -1;

    for (qsizetype i = 0; i < m_active->orderedCandidates.size(); ++i) {
        const QString key =
            m_active->orderedCandidates.at(i).candidateKey().stableKey();
        if (!containsKey(m_active->attemptedKeys, key))
            return static_cast<int>(i);
    }
    return -1;
}

const SelectableSourceCandidate *
SourceResolutionSession::currentCandidate() const
{
    if (!m_active.has_value()
        || m_active->currentIndex < 0
        || m_active->currentIndex >= m_active->orderedCandidates.size()) {
        return nullptr;
    }
    return &m_active->orderedCandidates.at(m_active->currentIndex);
}

bool SourceResolutionSession::containsKey(
    const QVector<QString> &keys,
    const QString &key)
{
    return std::find(keys.cbegin(), keys.cend(), key)
        != keys.cend();
}

bool SourceResolutionSession::reconcileWithCoordinator()
{
    if (!m_active.has_value())
        return false;

    if (coordinatorStillArms(*m_active))
        return false;

    m_active.reset();
    return true;
}

void SourceResolutionSession::invalidate() noexcept
{
    m_active.reset();
}

const std::optional<SourceResolutionRequest> &
SourceResolutionSession::pendingRequest() const noexcept
{
    static const std::optional<SourceResolutionRequest> none;
    return m_active.has_value() ? m_active->pendingRequest : none;
}

const std::optional<ResolvedTrackSource> &
SourceResolutionSession::currentSource() const noexcept
{
    static const std::optional<ResolvedTrackSource> none;
    return m_active.has_value() ? m_active->currentSource : none;
}

std::optional<QueueOccurrenceId>
SourceResolutionSession::occurrenceId() const
{
    if (!m_active.has_value())
        return std::nullopt;
    return m_active->occurrenceId;
}

} // namespace vinyl
