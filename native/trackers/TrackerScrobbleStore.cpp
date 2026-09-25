#include "TrackerScrobbleStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace {

constexpr int kSchemaVersion = 3;
constexpr auto kFileName = "tracker-scrobbles.json";

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

QString preferenceKey(TrackerProviderId providerId, const QString &remoteAccountId)
{
    return trackerProviderKey(providerId) + QChar(0x1f) + remoteAccountId;
}

QString actionKey(TrackerScrobbleAction value)
{
    switch (value) {
    case TrackerScrobbleAction::Start: return QStringLiteral("start");
    case TrackerScrobbleAction::Pause: return QStringLiteral("pause");
    case TrackerScrobbleAction::Stop: return QStringLiteral("stop");
    }
    return {};
}

QString operationIdFor(const TrackerScrobbleIntent &intent, quint64 generation)
{
    const QString material = trackerProviderKey(intent.providerId) + QChar(0x1f)
        + intent.remoteAccountId + QChar(0x1f) + QString::number(generation) + QChar(0x1f)
        + intent.canonicalMediaId + QChar(0x1f) + intent.remoteMediaId + QChar(0x1f)
        + intent.playbackSessionId + QChar(0x1f) + QString::number(intent.playbackGeneration)
        + QChar(0x1f) + QString::number(intent.transitionSequence) + QChar(0x1f)
        + actionKey(intent.action) + QChar(0x1f) + QString::number(intent.progressHundredths);
    return QStringLiteral("scrobble-") + QString::fromLatin1(
        QCryptographicHash::hash(material.toUtf8(), QCryptographicHash::Sha256).toHex());
}

std::optional<TrackerScrobbleAction> actionFromKey(const QString &value)
{
    if (value == QLatin1String("start")) return TrackerScrobbleAction::Start;
    if (value == QLatin1String("pause")) return TrackerScrobbleAction::Pause;
    if (value == QLatin1String("stop")) return TrackerScrobbleAction::Stop;
    return std::nullopt;
}

QString stateKey(TrackerScrobbleState value)
{
    switch (value) {
    case TrackerScrobbleState::Pending: return QStringLiteral("pending");
    case TrackerScrobbleState::Delivering: return QStringLiteral("delivering");
    case TrackerScrobbleState::Succeeded: return QStringLiteral("succeeded");
    case TrackerScrobbleState::Waiting: return QStringLiteral("waiting");
    case TrackerScrobbleState::UnknownOutcome: return QStringLiteral("unknown_outcome");
    case TrackerScrobbleState::NeedsAttention: return QStringLiteral("needs_attention");
    case TrackerScrobbleState::Superseded: return QStringLiteral("superseded");
    }
    return {};
}

std::optional<TrackerScrobbleState> stateFromKey(const QString &value)
{
    if (value == QLatin1String("pending")) return TrackerScrobbleState::Pending;
    if (value == QLatin1String("delivering")) return TrackerScrobbleState::Delivering;
    if (value == QLatin1String("succeeded")) return TrackerScrobbleState::Succeeded;
    if (value == QLatin1String("waiting")) return TrackerScrobbleState::Waiting;
    if (value == QLatin1String("unknown_outcome")) return TrackerScrobbleState::UnknownOutcome;
    if (value == QLatin1String("needs_attention")) return TrackerScrobbleState::NeedsAttention;
    if (value == QLatin1String("superseded")) return TrackerScrobbleState::Superseded;
    return std::nullopt;
}

QString reasonKey(TrackerScrobbleReason value)
{
    switch (value) {
    case TrackerScrobbleReason::None: return QStringLiteral("none");
    case TrackerScrobbleReason::ProviderUnavailable: return QStringLiteral("provider_unavailable");
    case TrackerScrobbleReason::RetryableKnownNotApplied: return QStringLiteral("retryable_known_not_applied");
    case TrackerScrobbleReason::RetryLimitReached: return QStringLiteral("retry_limit_reached");
    case TrackerScrobbleReason::AcknowledgementLost: return QStringLiteral("acknowledgement_lost");
    case TrackerScrobbleReason::UnsupportedAction: return QStringLiteral("unsupported_action");
    case TrackerScrobbleReason::MappingChanged: return QStringLiteral("mapping_changed");
    case TrackerScrobbleReason::StalePlayback: return QStringLiteral("stale_playback");
    case TrackerScrobbleReason::ReadbackPresent: return QStringLiteral("readback_present");
    case TrackerScrobbleReason::ReadbackAbsent: return QStringLiteral("readback_absent");
    case TrackerScrobbleReason::ReadbackUncertain: return QStringLiteral("readback_uncertain");
    case TrackerScrobbleReason::UserDiscarded: return QStringLiteral("user_discarded");
    }
    return {};
}

std::optional<TrackerScrobbleReason> reasonFromKey(const QString &value)
{
    if (value == QLatin1String("none")) return TrackerScrobbleReason::None;
    if (value == QLatin1String("provider_unavailable")) return TrackerScrobbleReason::ProviderUnavailable;
    if (value == QLatin1String("retryable_known_not_applied")) return TrackerScrobbleReason::RetryableKnownNotApplied;
    if (value == QLatin1String("retry_limit_reached")) return TrackerScrobbleReason::RetryLimitReached;
    if (value == QLatin1String("acknowledgement_lost")) return TrackerScrobbleReason::AcknowledgementLost;
    if (value == QLatin1String("unsupported_action")) return TrackerScrobbleReason::UnsupportedAction;
    if (value == QLatin1String("mapping_changed")) return TrackerScrobbleReason::MappingChanged;
    if (value == QLatin1String("stale_playback")) return TrackerScrobbleReason::StalePlayback;
    if (value == QLatin1String("readback_present")) return TrackerScrobbleReason::ReadbackPresent;
    if (value == QLatin1String("readback_absent")) return TrackerScrobbleReason::ReadbackAbsent;
    if (value == QLatin1String("readback_uncertain")) return TrackerScrobbleReason::ReadbackUncertain;
    if (value == QLatin1String("user_discarded")) return TrackerScrobbleReason::UserDiscarded;
    return std::nullopt;
}

bool validIntent(const TrackerScrobbleIntent &intent)
{
    return safeText(intent.operationId, 160)
        && intent.providerId == TrackerProviderId::Simkl
        && safeText(intent.remoteAccountId, 128)
        && intent.connectionGeneration > 0 && intent.mappingRevision > 0
        && safeText(intent.canonicalMediaId)
        && safeText(intent.remoteMediaId, 128)
        && safeText(intent.playbackSessionId, 128)
        && intent.playbackGeneration > 0 && intent.transitionSequence > 0
        && !actionKey(intent.action).isEmpty()
        && (!intent.closesSession || intent.action != TrackerScrobbleAction::Start)
        && intent.progressHundredths >= 0 && intent.progressHundredths <= 10000
        && intent.createdAtMs > 0 && !stateKey(intent.state).isEmpty()
        && !reasonKey(intent.reason).isEmpty() && intent.attemptCount >= 0
        && intent.attemptCount <= 2
        && (intent.state != TrackerScrobbleState::Pending || intent.attemptCount < 2)
        && (intent.state != TrackerScrobbleState::Delivering || intent.attemptCount > 0);
}

QJsonObject toJson(const TrackerScrobbleIntent &intent)
{
    return {{QStringLiteral("operationId"), intent.operationId},
            {QStringLiteral("providerId"), trackerProviderKey(intent.providerId)},
            {QStringLiteral("remoteAccountId"), intent.remoteAccountId},
            {QStringLiteral("connectionGeneration"), QString::number(intent.connectionGeneration)},
            {QStringLiteral("mappingRevision"), QString::number(intent.mappingRevision)},
            {QStringLiteral("canonicalMediaId"), intent.canonicalMediaId},
            {QStringLiteral("remoteMediaId"), intent.remoteMediaId},
            {QStringLiteral("playbackSessionId"), intent.playbackSessionId},
            {QStringLiteral("closesSession"), intent.closesSession},
            {QStringLiteral("playbackGeneration"), QString::number(intent.playbackGeneration)},
            {QStringLiteral("transitionSequence"), QString::number(intent.transitionSequence)},
            {QStringLiteral("action"), actionKey(intent.action)},
            {QStringLiteral("progressHundredths"), intent.progressHundredths},
            {QStringLiteral("completedLocally"), intent.completedLocally},
            {QStringLiteral("createdAtMs"), QString::number(intent.createdAtMs)},
            {QStringLiteral("state"), stateKey(intent.state)},
            {QStringLiteral("reason"), reasonKey(intent.reason)},
            {QStringLiteral("attemptCount"), intent.attemptCount}};
}

std::optional<TrackerScrobbleIntent> fromJson(const QJsonObject &object)
{
    const auto provider = trackerProviderIdFromKey(
        object.value(QStringLiteral("providerId")).toString());
    const auto action = actionFromKey(object.value(QStringLiteral("action")).toString());
    const auto state = stateFromKey(object.value(QStringLiteral("state")).toString());
    const auto reason = reasonFromKey(object.value(QStringLiteral("reason")).toString());
    bool connectionOk = false;
    bool mappingOk = false;
    bool playbackOk = false;
    bool sequenceOk = false;
    bool createdOk = false;
    const quint64 connection = object.value(QStringLiteral("connectionGeneration")).toString()
        .toULongLong(&connectionOk);
    const quint64 mapping = object.value(QStringLiteral("mappingRevision")).toString()
        .toULongLong(&mappingOk);
    const quint64 playback = object.value(QStringLiteral("playbackGeneration")).toString()
        .toULongLong(&playbackOk);
    const quint64 sequence = object.value(QStringLiteral("transitionSequence")).toString()
        .toULongLong(&sequenceOk);
    const qint64 created = object.value(QStringLiteral("createdAtMs")).toString()
        .toLongLong(&createdOk);
    const QJsonValue progressValue = object.value(QStringLiteral("progressHundredths"));
    const QJsonValue attemptValue = object.value(QStringLiteral("attemptCount"));
    const QJsonValue closesSessionValue = object.value(QStringLiteral("closesSession"));
    if (!provider || !action || !state || !reason || !connectionOk || !mappingOk
        || !playbackOk || !sequenceOk
        || !createdOk || !progressValue.isDouble() || !attemptValue.isDouble()
        || !closesSessionValue.isBool()) {
        return std::nullopt;
    }
    const int progress = progressValue.toInt(-1);
    const int attemptCount = attemptValue.toInt(-1);
    if (double(progress) != progressValue.toDouble()
        || double(attemptCount) != attemptValue.toDouble())
        return std::nullopt;

    TrackerScrobbleIntent intent;
    intent.operationId = object.value(QStringLiteral("operationId")).toString();
    intent.providerId = *provider;
    intent.remoteAccountId = object.value(QStringLiteral("remoteAccountId")).toString();
    intent.connectionGeneration = connection;
    intent.mappingRevision = mapping;
    intent.canonicalMediaId = object.value(QStringLiteral("canonicalMediaId")).toString();
    intent.remoteMediaId = object.value(QStringLiteral("remoteMediaId")).toString();
    intent.playbackSessionId = object.value(QStringLiteral("playbackSessionId")).toString();
    intent.closesSession = closesSessionValue.toBool();
    intent.playbackGeneration = playback;
    intent.transitionSequence = sequence;
    intent.action = *action;
    intent.progressHundredths = progress;
    intent.completedLocally = object.value(QStringLiteral("completedLocally")).toBool();
    intent.createdAtMs = created;
    intent.state = *state;
    intent.reason = *reason;
    intent.attemptCount = attemptCount;
    if (!validIntent(intent))
        return std::nullopt;
    return intent;
}

bool sameIntent(const TrackerScrobbleIntent &left, const TrackerScrobbleIntent &right)
{
    return left.operationId == right.operationId
        && left.providerId == right.providerId
        && left.remoteAccountId == right.remoteAccountId
        && left.connectionGeneration == right.connectionGeneration
        && left.mappingRevision == right.mappingRevision
        && left.canonicalMediaId == right.canonicalMediaId
        && left.remoteMediaId == right.remoteMediaId
        && left.playbackSessionId == right.playbackSessionId
        && left.closesSession == right.closesSession
        && left.playbackGeneration == right.playbackGeneration
        && left.transitionSequence == right.transitionSequence
        && left.action == right.action
        && left.progressHundredths == right.progressHundredths
        && left.completedLocally == right.completedLocally
        && left.createdAtMs == right.createdAtMs;
}

} // namespace

TrackerScrobbleStore::TrackerScrobbleStore(const ProfilePaths &profile)
    : m_profile(profile), m_path(storagePath(profile))
{
    if (m_path.isEmpty()) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker scrobbling is unavailable for this profile.");
    } else {
        load();
    }
}

QString TrackerScrobbleStore::storagePath(const ProfilePaths &profile)
{
    if (profile.kind() == ProfilePaths::Kind::Sealed
        || profile.kind() == ProfilePaths::Kind::LegacyLocal || profile.profileRoot().isEmpty()) {
        return {};
    }
    return QDir::cleanPath(profile.profileRoot() + QLatin1Char('/') + QLatin1String(kFileName));
}

bool TrackerScrobbleStore::healthy(QString *out) const
{
    if (!m_healthy && out)
        *out = m_error;
    return m_healthy;
}

bool TrackerScrobbleStore::enabled(TrackerProviderId providerId,
                                    const QString &remoteAccountId) const
{
    const QString key = preferenceKey(providerId, remoteAccountId);
    for (const Preference &entry : m_preferences) {
        if (preferenceKey(entry.providerId, entry.remoteAccountId) == key)
            return entry.enabled;
    }
    return false;
}

bool TrackerScrobbleStore::setEnabled(TrackerProviderId providerId,
                                       const QString &remoteAccountId,
                                       bool enabledValue,
                                       QString *out)
{
    if (!healthy(out) || providerId != TrackerProviderId::Simkl
        || !safeText(remoteAccountId, 128)) {
        return setError(out, QStringLiteral("Tracker live-playback preference is invalid."));
    }
    QList<Preference> next = m_preferences;
    bool found = false;
    for (Preference &entry : next) {
        if (entry.providerId == providerId && entry.remoteAccountId == remoteAccountId) {
            entry.enabled = enabledValue;
            found = true;
            break;
        }
    }
    if (!found)
        next.append({providerId, remoteAccountId, enabledValue});
    if (!persist(next, m_intents, out))
        return false;
    m_preferences = next;
    return true;
}

bool TrackerScrobbleStore::removePreferenceFor(TrackerProviderId providerId,
                                               const QString &remoteAccountId,
                                               QString *out)
{
    if (!healthy(out) || !trackerProviderIdFromKey(trackerProviderKey(providerId))
        || !safeText(remoteAccountId, 128)) {
        return setError(out, QStringLiteral("Tracker playback settings are invalid."));
    }
    QList<Preference> sourcePreferences = m_preferences;
    sourcePreferences.erase(std::remove_if(sourcePreferences.begin(), sourcePreferences.end(),
        [providerId, &remoteAccountId](const Preference &entry) {
            return entry.providerId == providerId && entry.remoteAccountId == remoteAccountId;
        }), sourcePreferences.end());
    if (sourcePreferences.size() == m_preferences.size())
        return true;
    if (!persist(sourcePreferences, m_intents, out))
        return false;
    m_preferences = sourcePreferences;
    return true;
}

QList<TrackerScrobbleIntent> TrackerScrobbleStore::intents() const
{
    return m_intents;
}

bool TrackerScrobbleStore::adoptPrivateStateFrom(const TrackerScrobbleStore &source,
                                                  QString *out)
{
    if (!healthy(out) || !source.healthy(out) || this == &source
        || m_profile.profileId() == source.m_profile.profileId()) {
        return setError(out, QStringLiteral("Tracker playback state cannot be adopted between these profiles."));
    }

    QList<TrackerScrobbleIntent> intents = m_intents;
    bool changed = false;

    for (const TrackerScrobbleIntent &stored : source.m_intents) {
        TrackerScrobbleIntent adopted = stored;
        if (adopted.state == TrackerScrobbleState::Delivering) {
            adopted.state = TrackerScrobbleState::UnknownOutcome;
            adopted.reason = TrackerScrobbleReason::AcknowledgementLost;
        }
        const auto duplicate = std::find_if(intents.cbegin(), intents.cend(),
            [&adopted](const TrackerScrobbleIntent &entry) {
                return entry.operationId == adopted.operationId;
            });
        if (duplicate != intents.cend()) {
            if (toJson(*duplicate) != toJson(adopted)) {
                return setError(out, QStringLiteral(
                    "The destination has a different outcome for a tracker playback update."));
            }
            continue;
        }
        intents.append(adopted);
        changed = true;
    }

    if (!changed)
        return true;
    if (!persist(m_preferences, intents, out))
        return false;
    m_intents = intents;
    return true;
}

QList<TrackerScrobbleIntent> TrackerScrobbleStore::unresolvedIntents(
    TrackerProviderId providerId, const QString &remoteAccountId) const
{
    QList<TrackerScrobbleIntent> result;
    for (const TrackerScrobbleIntent &intent : m_intents) {
        if (intent.providerId == providerId && intent.remoteAccountId == remoteAccountId
            && intent.state != TrackerScrobbleState::Succeeded
            && intent.state != TrackerScrobbleState::Superseded) {
            result.push_back(intent);
        }
    }
    return result;
}

std::optional<TrackerScrobbleIntent> TrackerScrobbleStore::intent(
    const QString &operationId) const
{
    for (const TrackerScrobbleIntent &entry : m_intents) {
        if (entry.operationId == operationId)
            return entry;
    }
    return std::nullopt;
}

bool TrackerScrobbleStore::recordIntent(const TrackerScrobbleIntent &intent, QString *out)
{
    if (!healthy(out) || !validIntent(intent)
        || intent.state != TrackerScrobbleState::Pending
        || intent.reason != TrackerScrobbleReason::None || intent.attemptCount != 0)
        return setError(out, QStringLiteral("Tracker scrobble intent is invalid."));
    for (const TrackerScrobbleIntent &existing : m_intents) {
        if (existing.operationId != intent.operationId)
            continue;
        if (sameIntent(existing, intent))
            return true;
        return setError(out, QStringLiteral("Tracker scrobble operation ID was reused."));
    }
    QList<TrackerScrobbleIntent> next = m_intents;
    next.append(intent);
    if (!persist(m_preferences, next, out))
        return false;
    m_intents = next;
    return true;
}

bool TrackerScrobbleStore::deferWithoutAttempt(const QString &operationId,
                                                TrackerScrobbleReason reason,
                                                QString *out)
{
    if (!healthy(out) || !safeText(operationId, 160)
        || reason == TrackerScrobbleReason::None || reasonKey(reason).isEmpty())
        return setError(out, QStringLiteral("Tracker scrobble deferral is invalid."));
    QList<TrackerScrobbleIntent> next = m_intents;
    for (TrackerScrobbleIntent &intent : next) {
        if (intent.operationId != operationId)
            continue;
        if (intent.state != TrackerScrobbleState::Pending || intent.attemptCount != 0)
            return setError(out, QStringLiteral("Tracker scrobble intent cannot be deferred in this state."));
        intent.state = TrackerScrobbleState::Waiting;
        intent.reason = reason;
        if (!persist(m_preferences, next, out))
            return false;
        m_intents = next;
        return true;
    }
    return setError(out, QStringLiteral("Tracker scrobble intent was not found."));
}

bool TrackerScrobbleStore::retryWaitingOnNextEvent(const QString &operationId, QString *out)
{
    if (!healthy(out) || !safeText(operationId, 160))
        return setError(out, QStringLiteral("Tracker scrobble retry is invalid."));
    QList<TrackerScrobbleIntent> next = m_intents;
    for (TrackerScrobbleIntent &intent : next) {
        if (intent.operationId != operationId)
            continue;
        if (intent.state != TrackerScrobbleState::Waiting || intent.attemptCount >= 2
            || (intent.reason != TrackerScrobbleReason::ProviderUnavailable
                && intent.reason != TrackerScrobbleReason::RetryableKnownNotApplied)) {
            return setError(out, QStringLiteral("Tracker scrobble intent is not eligible for event-driven retry."));
        }
        intent.state = TrackerScrobbleState::Pending;
        intent.reason = TrackerScrobbleReason::None;
        if (!persist(m_preferences, next, out))
            return false;
        m_intents = next;
        return true;
    }
    return setError(out, QStringLiteral("Tracker scrobble intent was not found."));
}

bool TrackerScrobbleStore::supersedeIfNeverApplied(const QString &operationId, QString *out)
{
    if (!healthy(out) || !safeText(operationId, 160))
        return setError(out, QStringLiteral("Tracker scrobble supersession is invalid."));
    QList<TrackerScrobbleIntent> next = m_intents;
    for (TrackerScrobbleIntent &intent : next) {
        if (intent.operationId != operationId)
            continue;
        const bool knownUnsent = intent.state == TrackerScrobbleState::Pending
            || (intent.state == TrackerScrobbleState::Waiting
                && intent.reason != TrackerScrobbleReason::AcknowledgementLost)
            || (intent.state == TrackerScrobbleState::NeedsAttention
                && intent.reason == TrackerScrobbleReason::RetryLimitReached);
        if (!knownUnsent)
            return setError(out, QStringLiteral("Tracker scrobble may already have reached SIMKL."));
        intent.state = TrackerScrobbleState::Superseded;
        intent.reason = TrackerScrobbleReason::StalePlayback;
        if (!persist(m_preferences, next, out))
            return false;
        m_intents = next;
        return true;
    }
    return setError(out, QStringLiteral("Tracker scrobble intent was not found."));
}

bool TrackerScrobbleStore::markNeedsAttention(const QString &operationId,
                                               TrackerScrobbleReason reason,
                                               QString *out)
{
    if (!healthy(out) || !safeText(operationId, 160) || reason == TrackerScrobbleReason::None
        || reasonKey(reason).isEmpty())
        return setError(out, QStringLiteral("Tracker scrobble attention state is invalid."));
    QList<TrackerScrobbleIntent> next = m_intents;
    for (TrackerScrobbleIntent &intent : next) {
        if (intent.operationId != operationId)
            continue;
        if (intent.state != TrackerScrobbleState::Pending
            && intent.state != TrackerScrobbleState::Waiting)
            return setError(out, QStringLiteral("Tracker scrobble cannot enter attention from this state."));
        intent.state = TrackerScrobbleState::NeedsAttention;
        intent.reason = reason;
        if (!persist(m_preferences, next, out))
            return false;
        m_intents = next;
        return true;
    }
    return setError(out, QStringLiteral("Tracker scrobble intent was not found."));
}

bool TrackerScrobbleStore::markDelivering(const QString &operationId, QString *out)
{
    if (!healthy(out) || !safeText(operationId, 160))
        return setError(out, QStringLiteral("Tracker scrobble attempt is invalid."));
    QList<TrackerScrobbleIntent> next = m_intents;
    bool found = false;
    for (TrackerScrobbleIntent &intent : next) {
        if (intent.operationId != operationId)
            continue;
        if (intent.state != TrackerScrobbleState::Pending || intent.attemptCount >= 2)
            return setError(out, QStringLiteral("Tracker scrobble intent is not ready to send."));
        intent.state = TrackerScrobbleState::Delivering;
        intent.reason = TrackerScrobbleReason::None;
        ++intent.attemptCount;
        found = true;
        break;
    }
    if (!found)
        return setError(out, QStringLiteral("Tracker scrobble intent was not found."));
    if (!persist(m_preferences, next, out))
        return false;
    m_intents = next;
    return true;
}

bool TrackerScrobbleStore::markSessionClosed(const QString &operationId, QString *out)
{
    if (!healthy(out) || !safeText(operationId, 160))
        return setError(out, QStringLiteral("Tracker playback close is invalid."));
    QList<TrackerScrobbleIntent> next = m_intents;
    for (TrackerScrobbleIntent &intent : next) {
        if (intent.operationId != operationId)
            continue;
        if (intent.action == TrackerScrobbleAction::Start
            || intent.state == TrackerScrobbleState::Superseded)
            return setError(out, QStringLiteral("Tracker intent cannot close this playback session."));
        if (intent.closesSession)
            return true;
        intent.closesSession = true;
        if (!persist(m_preferences, next, out))
            return false;
        m_intents = next;
        return true;
    }
    return setError(out, QStringLiteral("Tracker scrobble intent was not found."));
}

bool TrackerScrobbleStore::recordAttemptResult(const QString &operationId,
                                                TrackerScrobbleAttemptResult result,
                                                TrackerScrobbleReason reason,
                                                QString *out)
{
    if (!healthy(out) || !safeText(operationId, 160)
        || reasonKey(reason).isEmpty())
        return setError(out, QStringLiteral("Tracker scrobble result is invalid."));
    QList<TrackerScrobbleIntent> next = m_intents;
    for (TrackerScrobbleIntent &intent : next) {
        if (intent.operationId != operationId)
            continue;
        if (intent.state != TrackerScrobbleState::Delivering
            || intent.attemptCount <= 0 || intent.attemptCount > 2)
            return setError(out, QStringLiteral("Tracker scrobble result has no active send."));
        switch (result) {
        case TrackerScrobbleAttemptResult::Succeeded:
            intent.state = TrackerScrobbleState::Succeeded;
            intent.reason = TrackerScrobbleReason::None;
            break;
        case TrackerScrobbleAttemptResult::KnownNotApplied:
            intent.state = intent.attemptCount >= 2
                ? TrackerScrobbleState::NeedsAttention : TrackerScrobbleState::Waiting;
            intent.reason = intent.attemptCount >= 2
                ? TrackerScrobbleReason::RetryLimitReached
                : (reason == TrackerScrobbleReason::None
                       ? TrackerScrobbleReason::RetryableKnownNotApplied : reason);
            break;
        case TrackerScrobbleAttemptResult::UnknownOutcome:
            intent.state = TrackerScrobbleState::UnknownOutcome;
            intent.reason = TrackerScrobbleReason::AcknowledgementLost;
            break;
        case TrackerScrobbleAttemptResult::NeedsAttention:
            intent.state = TrackerScrobbleState::NeedsAttention;
            intent.reason = reason == TrackerScrobbleReason::None
                ? TrackerScrobbleReason::UnsupportedAction : reason;
            break;
        }
        if (!persist(m_preferences, next, out))
            return false;
        m_intents = next;
        return true;
    }
    return setError(out, QStringLiteral("Tracker scrobble intent was not found."));
}

bool TrackerScrobbleStore::reconcileUnknown(const QString &operationId,
                                             TrackerScrobbleReadback result,
                                             QString *out)
{
    if (!healthy(out) || !safeText(operationId, 160))
        return setError(out, QStringLiteral("Tracker scrobble readback is invalid."));
    QList<TrackerScrobbleIntent> next = m_intents;
    for (TrackerScrobbleIntent &intent : next) {
        if (intent.operationId != operationId)
            continue;
        if (intent.state != TrackerScrobbleState::UnknownOutcome)
            return setError(out, QStringLiteral("Readback is allowed only for an unknown scrobble outcome."));
        switch (result) {
        case TrackerScrobbleReadback::ExactPresent:
            intent.state = TrackerScrobbleState::Succeeded;
            intent.reason = TrackerScrobbleReason::ReadbackPresent;
            break;
        case TrackerScrobbleReadback::Absent:
            // Never replay a lifecycle edge after its player session has ended.
            // A missing close still needs attention because SIMKL may hold the old session.
            intent.state = intent.action == TrackerScrobbleAction::Start
                ? TrackerScrobbleState::Superseded : TrackerScrobbleState::NeedsAttention;
            intent.reason = intent.action == TrackerScrobbleAction::Start
                ? TrackerScrobbleReason::StalePlayback : TrackerScrobbleReason::ReadbackAbsent;
            break;
        case TrackerScrobbleReadback::Indeterminate:
            intent.state = TrackerScrobbleState::NeedsAttention;
            intent.reason = TrackerScrobbleReason::ReadbackUncertain;
            break;
        }
        if (!persist(m_preferences, next, out))
            return false;
        m_intents = next;
        return true;
    }
    return setError(out, QStringLiteral("Tracker scrobble intent was not found."));
}

bool TrackerScrobbleStore::recoverUnattemptedIntents(QString *out)
{
    if (!healthy(out))
        return false;
    QList<TrackerScrobbleIntent> next = m_intents;
    bool changed = false;
    for (TrackerScrobbleIntent &intent : next) {
        if (intent.state != TrackerScrobbleState::Pending)
            continue;
        intent.state = intent.action == TrackerScrobbleAction::Start
            ? TrackerScrobbleState::Superseded : TrackerScrobbleState::NeedsAttention;
        intent.reason = TrackerScrobbleReason::StalePlayback;
        changed = true;
    }
    if (!changed)
        return true;
    if (!persist(m_preferences, next, out))
        return false;
    m_intents = next;
    return true;
}

bool TrackerScrobbleStore::hasUnknownOutcome(TrackerProviderId providerId,
                                               const QString &remoteAccountId) const
{
    for (const TrackerScrobbleIntent &intent : m_intents) {
        if (intent.providerId == providerId && intent.remoteAccountId == remoteAccountId
            && intent.state == TrackerScrobbleState::UnknownOutcome)
            return true;
    }
    return false;
}

bool TrackerScrobbleStore::hasBlockingOutcome(TrackerProviderId providerId,
                                               const QString &remoteAccountId) const
{
    for (const TrackerScrobbleIntent &intent : m_intents) {
        if (intent.providerId != providerId || intent.remoteAccountId != remoteAccountId)
            continue;
        if (intent.state != TrackerScrobbleState::Succeeded
            && intent.state != TrackerScrobbleState::Superseded)
            return true;
    }
    return false;
}

bool TrackerScrobbleStore::resumeKnownUnsentAfterReconnect(
    TrackerProviderId providerId,
    const QString &remoteAccountId,
    quint64 connectionGeneration,
    QString *out)
{
    if (!healthy(out) || !trackerProviderIdFromKey(trackerProviderKey(providerId))
        || !safeText(remoteAccountId, 128) || connectionGeneration == 0) {
        return setError(out, QStringLiteral("Tracker playback updates could not be resumed."));
    }
    if (providerId != TrackerProviderId::Simkl)
        return true;

    const auto isKnownUnsentForReconnect = [&](const TrackerScrobbleIntent &intent) {
        if (intent.providerId != providerId || intent.remoteAccountId != remoteAccountId
            || intent.connectionGeneration >= connectionGeneration) {
            return false;
        }
        const bool pending = intent.state == TrackerScrobbleState::Pending;
        const bool waitingKnownNotApplied = intent.state == TrackerScrobbleState::Waiting
            && intent.attemptCount < 2
            && (intent.reason == TrackerScrobbleReason::ProviderUnavailable
                || intent.reason == TrackerScrobbleReason::RetryableKnownNotApplied);
        return pending || waitingKnownNotApplied;
    };

    QSet<QString> occupiedIds;
    for (const TrackerScrobbleIntent &intent : m_intents) {
        if (!isKnownUnsentForReconnect(intent))
            occupiedIds.insert(intent.operationId);
    }

    QList<TrackerScrobbleIntent> next;
    bool changed = false;
    for (const TrackerScrobbleIntent &stored : m_intents) {
        if (!isKnownUnsentForReconnect(stored)) {
            next.append(stored);
            continue;
        }
        TrackerScrobbleIntent intent = stored;
        const bool safeProtectiveClose = intent.closesSession
            && hasOpenPlaybackForSession(providerId, remoteAccountId,
                                         intent.playbackSessionId);
        if (!safeProtectiveClose) {
            // Start/pause edges describe a particular live player moment. A
            // reconnect cannot prove that moment is still current, so never
            // replay it. Likewise, a close is obsolete when no remote session
            // is known to remain open.
            intent.state = TrackerScrobbleState::Superseded;
            intent.reason = TrackerScrobbleReason::StalePlayback;
            next.append(intent);
            changed = true;
            continue;
        }

        intent.connectionGeneration = connectionGeneration;
        if (stored.state == TrackerScrobbleState::Waiting) {
            intent.state = TrackerScrobbleState::Pending;
            intent.reason = TrackerScrobbleReason::None;
        }
        intent.operationId = operationIdFor(intent, connectionGeneration);
        // Preserve an equivalent operation already issued for this generation.
        if (!occupiedIds.contains(intent.operationId)) {
            occupiedIds.insert(intent.operationId);
            next.append(intent);
        }
        changed = true;
    }
    if (!changed)
        return true;
    if (!persist(m_preferences, next, out))
        return false;
    m_intents = next;
    return true;
}

int TrackerScrobbleStore::discardKnownUnsent(TrackerProviderId providerId,
                                             const QString &remoteAccountId,
                                             QString *out)
{
    if (!healthy(out) || !trackerProviderIdFromKey(trackerProviderKey(providerId))
        || !safeText(remoteAccountId, 128)) {
        setError(out, QStringLiteral("Tracker playback updates could not be discarded."));
        return -1;
    }
    if (providerId != TrackerProviderId::Simkl)
        return 0;
    QList<TrackerScrobbleIntent> next = m_intents;
    int discarded = 0;
    for (TrackerScrobbleIntent &intent : next) {
        if (intent.providerId != providerId || intent.remoteAccountId != remoteAccountId)
            continue;
        const bool knownUnsent = intent.state == TrackerScrobbleState::Pending
            || (intent.state == TrackerScrobbleState::Waiting
                && intent.reason != TrackerScrobbleReason::AcknowledgementLost)
            || (intent.state == TrackerScrobbleState::NeedsAttention
                && intent.reason == TrackerScrobbleReason::RetryLimitReached);
        if (!knownUnsent)
            continue;
        intent.state = TrackerScrobbleState::Superseded;
        intent.reason = TrackerScrobbleReason::UserDiscarded;
        ++discarded;
    }
    if (discarded == 0)
        return 0;
    if (!persist(m_preferences, next, out))
        return -1;
    m_intents = next;
    return discarded;
}

bool TrackerScrobbleStore::hasOpenPlayback(TrackerProviderId providerId,
                                            const QString &remoteAccountId,
                                            const QString &exceptSessionId) const
{
    QHash<QString, bool> providerMayHaveOpenPlayback;
    for (const TrackerScrobbleIntent &intent : m_intents) {
        if (intent.providerId != providerId || intent.remoteAccountId != remoteAccountId
            || intent.playbackSessionId == exceptSessionId
            || intent.state == TrackerScrobbleState::Superseded) {
            continue;
        }

        if (intent.state == TrackerScrobbleState::Succeeded) {
            providerMayHaveOpenPlayback.insert(intent.playbackSessionId,
                                                !intent.closesSession);
        } else if (intent.state == TrackerScrobbleState::Delivering
                   || intent.state == TrackerScrobbleState::UnknownOutcome
                   || (intent.state == TrackerScrobbleState::NeedsAttention
                       && intent.reason == TrackerScrobbleReason::ReadbackUncertain)) {
            // Until a send or readback is decisive, assume SIMKL may still have
            // an open session. Unsent/known-not-applied intents do not overwrite
            // the last confirmed provider state.
            providerMayHaveOpenPlayback.insert(intent.playbackSessionId, true);
        }
    }
    for (bool open : providerMayHaveOpenPlayback) {
        if (open)
            return true;
    }
    return false;
}

bool TrackerScrobbleStore::hasOpenPlaybackForSession(TrackerProviderId providerId,
                                                       const QString &remoteAccountId,
                                                       const QString &sessionId) const
{
    if (!safeText(sessionId, 128))
        return false;
    bool providerMayHaveOpenPlayback = false;
    for (const TrackerScrobbleIntent &intent : m_intents) {
        if (intent.providerId != providerId || intent.remoteAccountId != remoteAccountId
            || intent.playbackSessionId != sessionId
            || intent.state == TrackerScrobbleState::Superseded) {
            continue;
        }

        if (intent.state == TrackerScrobbleState::Succeeded) {
            providerMayHaveOpenPlayback = !intent.closesSession;
        } else if (intent.state == TrackerScrobbleState::Delivering
                   || intent.state == TrackerScrobbleState::UnknownOutcome
                   || (intent.state == TrackerScrobbleState::NeedsAttention
                       && intent.reason == TrackerScrobbleReason::ReadbackUncertain)) {
            providerMayHaveOpenPlayback = true;
        }
    }
    return providerMayHaveOpenPlayback;
}

bool TrackerScrobbleStore::load()
{
    QFile file(m_path);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker scrobble state could not be opened.");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    const QJsonObject root = document.object();
    const int schemaVersion = root.value(QStringLiteral("version")).toInt(-1);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || (schemaVersion != 1 && schemaVersion != 2 && schemaVersion != kSchemaVersion)
        || root.value(QStringLiteral("profileId")).toString() != m_profile.profileId()
        || !root.value(QStringLiteral("preferences")).isArray()
        || !root.value(QStringLiteral("intents")).isArray()) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker scrobble state is malformed or foreign.");
        return false;
    }

    QList<Preference> preferences;
    QSet<QString> preferenceKeys;
    for (const QJsonValue &value : root.value(QStringLiteral("preferences")).toArray()) {
        if (!value.isObject()) {
            m_healthy = false;
            m_error = QStringLiteral("Tracker scrobble preference is malformed.");
            return false;
        }
        const QJsonObject object = value.toObject();
        const auto provider = trackerProviderIdFromKey(
            object.value(QStringLiteral("providerId")).toString());
        const QString account = object.value(QStringLiteral("remoteAccountId")).toString();
        const QString key = provider ? preferenceKey(*provider, account) : QString();
        if (!provider || *provider != TrackerProviderId::Simkl || !safeText(account, 128)
            || !object.value(QStringLiteral("enabled")).isBool()
            || preferenceKeys.contains(key)) {
            m_healthy = false;
            m_error = QStringLiteral("Tracker scrobble preference is invalid or duplicated.");
            return false;
        }
        preferenceKeys.insert(key);
        preferences.append({*provider, account, object.value(QStringLiteral("enabled")).toBool()});
    }

    QList<TrackerScrobbleIntent> intents;
    QSet<QString> operationIds;
    for (const QJsonValue &value : root.value(QStringLiteral("intents")).toArray()) {
        if (!value.isObject()) {
            m_healthy = false;
            m_error = QStringLiteral("Tracker scrobble intent is malformed.");
            return false;
        }
        QJsonObject object = value.toObject();
        if (schemaVersion == 1) {
            object.insert(QStringLiteral("attemptCount"), 0);
            object.insert(QStringLiteral("closesSession"), false);
        }
        const auto intent = fromJson(object);
        if (!intent || operationIds.contains(intent->operationId)) {
            m_healthy = false;
            m_error = QStringLiteral("Tracker scrobble intent is invalid or duplicated.");
            return false;
        }
        operationIds.insert(intent->operationId);
        intents.append(*intent);
    }
    bool recovered = schemaVersion != kSchemaVersion;
    for (TrackerScrobbleIntent &intent : intents) {
        if (intent.state != TrackerScrobbleState::Delivering)
            continue;
        intent.state = TrackerScrobbleState::UnknownOutcome;
        intent.reason = TrackerScrobbleReason::AcknowledgementLost;
        recovered = true;
    }
    if (recovered && !persist(preferences, intents, &m_error)) {
        m_healthy = false;
        if (m_error.isEmpty())
            m_error = QStringLiteral("Interrupted SIMKL playback delivery could not be recovered safely.");
        return false;
    }
    m_preferences = preferences;
    m_intents = intents;
    return true;
}

bool TrackerScrobbleStore::persist(const QList<Preference> &preferences,
                                    const QList<TrackerScrobbleIntent> &intents,
                                    QString *out) const
{
    QJsonArray preferenceArray;
    QSet<QString> preferenceKeys;
    for (const Preference &preference : preferences) {
        const QString key = preferenceKey(preference.providerId, preference.remoteAccountId);
        if (preference.providerId != TrackerProviderId::Simkl
            || !safeText(preference.remoteAccountId, 128) || preferenceKeys.contains(key)) {
            return setError(out, QStringLiteral("Tracker scrobble preference persistence is invalid."));
        }
        preferenceKeys.insert(key);
        preferenceArray.append(QJsonObject{
            {QStringLiteral("providerId"), trackerProviderKey(preference.providerId)},
            {QStringLiteral("remoteAccountId"), preference.remoteAccountId},
            {QStringLiteral("enabled"), preference.enabled}});
    }

    QJsonArray intentArray;
    QSet<QString> operationIds;
    for (const TrackerScrobbleIntent &intent : intents) {
        if (!validIntent(intent) || operationIds.contains(intent.operationId))
            return setError(out, QStringLiteral("Tracker scrobble intent persistence is invalid."));
        operationIds.insert(intent.operationId);
        intentArray.append(toJson(intent));
    }

    const QFileInfo info(m_path);
    if (!QDir().mkpath(info.dir().absolutePath()))
        return setError(out, QStringLiteral("Tracker scrobble directory could not be created."));
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return setError(out, QStringLiteral("Tracker scrobble state could not be written."));
    file.write(QJsonDocument({{QStringLiteral("version"), kSchemaVersion},
                              {QStringLiteral("profileId"), m_profile.profileId()},
                              {QStringLiteral("preferences"), preferenceArray},
                              {QStringLiteral("intents"), intentArray}})
                   .toJson(QJsonDocument::Compact));
    if (!file.commit())
        return setError(out, QStringLiteral("Tracker scrobble state could not be saved atomically."));
    if (out)
        out->clear();
    return true;
}
