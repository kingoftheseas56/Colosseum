// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountController.h"

#include "AccountProfileCoordinator.h"
#include "SyncEngine.h"

#include <QDateTime>
#include <QJsonObject>

#include <limits>

namespace {
constexpr int kRefreshLeadSeconds = 60;
constexpr int kOfflineRefreshRetryMs = 30 * 1000;
constexpr int kChallengePollMs = 2 * 1000;
constexpr int kApprovalRetryMs = 5 * 1000;

QString accountAvatarId(const QJsonObject &body) {
    const QString canonical = body
        .value(QStringLiteral("builtin_avatar_id"))
        .toString();
    if (!canonical.isEmpty())
        return canonical;
    return body.value(QStringLiteral("avatar_id")).toString();
}
}

void AccountController::completeOnboarding() {
    if (!m_onboardingRequired)
        return;

    if (!m_bootstrapStore->setOnboardingCompleted(true))
        return;

    m_onboardingRequired = false;
    emit onboardingRequiredChanged();
}


AccountController::AccountController(
    AccountClient *client,
    AccountCredentialStore *credentialStore,
    AccountDeviceIdentity *deviceIdentity,
    AccountBootstrapStore *bootstrapStore,
    AccountOneTimeSecretSink *oneTimeSecretSink,
    QObject *parent)
    : QObject(parent),
      m_client(client),
      m_credentialStore(credentialStore),
      m_deviceIdentity(deviceIdentity),
      m_bootstrapStore(bootstrapStore),
      m_oneTimeSecretSink(oneTimeSecretSink) {
    Q_ASSERT(m_client);
    Q_ASSERT(m_credentialStore);
    Q_ASSERT(m_deviceIdentity);
    Q_ASSERT(m_bootstrapStore);
    Q_ASSERT(m_oneTimeSecretSink);

    setObjectName(QStringLiteral("accountController"));
    m_onboardingRequired = !m_bootstrapStore->onboardingCompleted();

    m_refreshTimer.setSingleShot(true);
    m_challengeTimer.setSingleShot(true);
    m_approvalTimer.setSingleShot(true);

    connect(
        m_client,
        &AccountClient::completed,
        this,
        &AccountController::handleCompleted);

    connect(
        &m_refreshTimer,
        &QTimer::timeout,
        this,
        [this]() {
            if (m_refreshToken.isEmpty())
                return;

            if (m_mode == Mode::Offline) {
                setSyncStateValue(SyncState::Retrying);
                setRestoreStageValue(RestoreStage::SessionRefresh);
            }

            requestSessionRefresh();
        });

    connect(
        &m_challengeTimer,
        &QTimer::timeout,
        this,
        &AccountController::pollPendingChallenge);

    connect(
        &m_approvalTimer,
        &QTimer::timeout,
        this,
        [this]() {
            if (m_mode == Mode::SignedIn)
                track(m_client->listApprovals(25));
        });
}

QString AccountController::mode() const {
    return modeName(m_mode);
}

AccountController::Mode AccountController::modeValue() const {
    return m_mode;
}

QString AccountController::syncState() const {
    return syncStateName(m_syncState);
}

AccountController::SyncState AccountController::syncStateValue() const {
    return m_syncState;
}

QString AccountController::restoreStage() const {
    return restoreStageName(m_restoreStage);
}

AccountController::RestoreStage AccountController::restoreStageValue() const {
    return m_restoreStage;
}

QString AccountController::username() const {
    return m_username;
}

QString AccountController::avatarId() const {
    return m_avatarId;
}

bool AccountController::onboardingRequired() const {
    return m_onboardingRequired;
}

QJsonArray AccountController::devices() const {
    return m_devices;
}

int AccountController::deviceCount() const {
    return m_deviceCount;
}

bool AccountController::newDeviceProtection() const {
    return m_newDeviceProtection;
}

int AccountController::pendingOutboxCount() const {
    return m_pendingOutboxCount;
}

bool AccountController::signOutSyncWarningPending() const {
    return m_signOutSyncWarningPending;
}

QString AccountController::deletionEffectiveAt() const {
    if (!m_deletionEffectiveAt.isValid())
        return QString();
    return m_deletionEffectiveAt.toUTC().toString(Qt::ISODateWithMs);
}

QString AccountController::errorCategory() const {
    return errorCategoryName(m_errorCategory);
}

QString AccountController::lastErrorCode() const {
    return m_lastErrorCode;
}

QString AccountController::lastErrorMessage() const {
    return m_lastErrorMessage;
}

bool AccountController::busy() const {
    return m_mode == Mode::Restoring
        || m_mode == Mode::Authenticating
        || m_mode == Mode::AwaitingDeviceApproval
        || m_mode == Mode::AwaitingRecoveryApproval;
}

QString AccountController::accountId() const {
    return m_accountId;
}

QString AccountController::deviceId() const {
    return m_deviceId;
}

void AccountController::setAutomaticPollingEnabled(bool enabled) {
    if (m_automaticPollingEnabled == enabled)
        return;

    m_automaticPollingEnabled = enabled;
    if (!enabled) {
        m_challengeTimer.stop();
        m_approvalTimer.stop();
        return;
    }

    if (m_mode == Mode::SignedIn)
        scheduleApprovalPoll();
    else if (m_mode == Mode::AwaitingDeviceApproval
             || m_mode == Mode::AwaitingRecoveryApproval)
        scheduleChallengePoll();
}

void AccountController::setProfileCoordinator(
    AccountProfileCoordinator *profileCoordinator) {
    m_profileCoordinator = profileCoordinator;
}

void AccountController::setSyncEngine(
    SyncEngine *syncEngine) {
    if (m_syncEngine == syncEngine)
        return;

    if (m_syncEngine)
        disconnect(m_syncEngine, nullptr, this, nullptr);

    m_syncEngine = syncEngine;
    if (!m_syncEngine)
        return;

    connect(
        m_syncEngine,
        &SyncEngine::observationChanged,
        this,
        [this](
            SyncEngine::State state,
            int count) {
            SyncState mapped =
                SyncState::Inactive;

            switch (state) {
            case SyncEngine::State::Inactive:
                mapped = SyncState::Inactive;
                break;
            case SyncEngine::State::Idle:
                mapped = SyncState::Idle;
                break;
            case SyncEngine::State::Retrying:
                mapped = SyncState::Retrying;
                break;
            case SyncEngine::State::Blocked:
                mapped = SyncState::Blocked;
                break;
            }

            setSyncObservation(
                mapped,
                count);
        });

    connect(
        m_syncEngine,
        &SyncEngine::signOutFlushFinished,
        this,
        [this](
            bool drained,
            const QString &errorCode,
            const QString &message) {
            if (m_pendingLogout
                == PendingLogout::None) {
                return;
            }

            if (drained) {
                if (m_syncEngine)
                    m_syncEngine->setNetworkEnabled(
                        false);

                clearPendingLogoutWarning();
                continuePendingLogout();
                return;
            }

            if (errorCode
                    == QLatin1String(
                        "sync_persistence_failed")
                || errorCode
                    == QLatin1String(
                        "adapter_snapshot_failed")) {
                m_pendingLogout =
                    PendingLogout::None;
                clearPendingLogoutWarning();

                setError(
                    ErrorCategory::Storage,
                    errorCode,
                    message.isEmpty()
                        ? QStringLiteral(
                              "Unsynced account changes could not be preserved safely.")
                        : message);
                return;
            }

            if (!m_signOutSyncWarningPending) {
                m_signOutSyncWarningPending = true;
                emit signOutSyncWarningPendingChanged();
            }

            if (!errorCode.isEmpty()) {
                setError(
                    errorCode
                            == QLatin1String("offline")
                        ? ErrorCategory::Offline
                        : ErrorCategory::Unavailable,
                    errorCode,
                    message.isEmpty()
                        ? QStringLiteral(
                              "Some changes haven't synced.")
                        : message);
            }
        });

    connect(
        m_syncEngine,
        &SyncEngine::accessTokenRejected,
        this,
        [this]() {
            beginAccessTokenRecovery();
        });
}

void AccountController::setSyncObservation(
    SyncState state,
    int pendingOutboxCount) {
    if (m_mode == Mode::LocalOnly
        || m_mode == Mode::SignedOut
        || m_mode == Mode::Locked
        || m_mode == Mode::DeletionPending
        || m_mode == Mode::Error) {
        setSyncStateValue(SyncState::Inactive);
        setPendingOutboxCountValue(0);
        return;
    }

    setSyncStateValue(state);
    setPendingOutboxCountValue(pendingOutboxCount);
}

void AccountController::setDeletionPending(const QDateTime &effectiveAt) {
    if (!effectiveAt.isValid() || m_accountId.isEmpty())
        return;

    advanceGeneration();
    m_refreshTimer.stop();
    m_challengeTimer.stop();
    m_approvalTimer.stop();

    const QDateTime normalized = effectiveAt.toUTC();
    if (m_deletionEffectiveAt != normalized) {
        m_deletionEffectiveAt = normalized;
        emit deletionEffectiveAtChanged();
    }

    setSyncStateValue(SyncState::Inactive);
    setPendingOutboxCountValue(0);
    setMode(Mode::DeletionPending);
}

void AccountController::clearDeletionPending() {
    if (!m_deletionEffectiveAt.isValid()
        && m_mode != Mode::DeletionPending) {
        return;
    }

    m_deletionEffectiveAt = QDateTime();
    emit deletionEffectiveAtChanged();

    if (!m_client->accessToken().isEmpty() && !m_refreshToken.isEmpty()) {
        setMode(Mode::SignedIn);
        setSyncStateValue(SyncState::Idle);
        scheduleApprovalPoll();
    } else {
        setMode(Mode::SignedOut);
        setSyncStateValue(SyncState::Inactive);
    }
}

void AccountController::restoreRememberedSession() {
    advanceGeneration();
    clearError();
    setSyncStateValue(SyncState::Inactive);
    setRestoreStageValue(RestoreStage::CredentialLookup);

    if (m_bootstrapStore->credentialClearPending()) {
        if (!m_credentialStore->isAvailable()
            || !m_credentialStore->clearActive()) {
            clearVolatileSession();
            setError(
                ErrorCategory::Storage,
                QStringLiteral("secure_store_clear_pending"),
                QStringLiteral(
                    "A previous sign-out is waiting for secure storage."),
                true);
            return;
        }

        m_bootstrapStore->setCredentialClearPending(false);
        clearVolatileSession();
        setRestoreStageValue(RestoreStage::None);
        if (m_bootstrapStore->localOnlyChosen()) {
            if (!prepareLocalOnlyProfile())
                return;
            setMode(Mode::LocalOnly);
        } else {
            setMode(Mode::SignedOut);
        }
        return;
    }

    flushPendingRevocations();

    if (!m_credentialStore->isAvailable()) {
        clearVolatileSession();

        if (m_bootstrapStore->localOnlyChosen()) {
            setRestoreStageValue(RestoreStage::None);
            if (!prepareLocalOnlyProfile())
                return;
            setMode(Mode::LocalOnly);
            return;
        }

        setError(
            ErrorCategory::Storage,
            QStringLiteral("secure_store_unavailable"),
            QStringLiteral("Secure account storage is unavailable."),
            true);
        return;
    }

    const auto credential = m_credentialStore->loadActive();
    if (!credential.has_value()) {
        clearVolatileSession();
        setRestoreStageValue(RestoreStage::None);
        if (m_bootstrapStore->localOnlyChosen()) {
            if (!prepareLocalOnlyProfile())
                return;
            setMode(Mode::LocalOnly);
        } else {
            setMode(Mode::SignedOut);
        }
        return;
    }

    if (!prepareRememberedProfile(*credential))
        return;

    m_accountId = credential->accountId;
    m_deviceId = credential->deviceId;
    m_refreshToken = credential->refreshToken;
    emit accountProfileReadyForSync();

    setRestoreStageValue(RestoreStage::SessionRefresh);
    setMode(Mode::Restoring);
    requestSessionRefresh();
}

void AccountController::continueWithoutAccount() {
    if (!m_refreshToken.isEmpty())
        return;

    advanceGeneration();
    clearError();

    if (!prepareLocalOnlyProfile())
        return;

    m_refreshTimer.stop();
    m_challengeTimer.stop();
    m_approvalTimer.stop();
    m_client->clearAccessToken();

    m_accountId.clear();
    m_deviceId.clear();
    m_pendingDeviceChallenge.clear();
    m_pendingTrustedRecoveryChallenge.clear();

    setUsername(QString());
    setDeviceCount(0);
    setNewDeviceProtectionValue(false);
    setSyncStateValue(SyncState::Inactive);
    setPendingOutboxCountValue(0);
    setRestoreStageValue(RestoreStage::None);

    m_bootstrapStore->setLocalOnlyChosen(true);
    completeOnboarding();
    setMode(Mode::LocalOnly);
}

void AccountController::returnToSignIn() {
    // The inverse of continueWithoutAccount(): leave guest/local-only mode and
    // put the device back at the sign-in choice. The account overlay is
    // mode-driven — SignedOut re-shows the Welcome surface (Create / Sign in /
    // Continue without account) — so this only has to invalidate any in-flight
    // work, forget the persisted local-only choice, and flip the mode. The
    // local-only profile stores stay bound (nothing is destroyed): the user has
    // not signed in yet and may re-choose guest mode, and a real sign-in adopts
    // those stores through the normal path.
    if (m_mode != Mode::LocalOnly)
        return;

    advanceGeneration();
    clearError();
    m_bootstrapStore->setLocalOnlyChosen(false);
    setMode(Mode::SignedOut);
}

void AccountController::createAccount(
    const QString &username,
    const QString &password) {
    if (!m_refreshToken.isEmpty())
        return;

    advanceGeneration();
    clearError();
    m_refreshTimer.stop();
    setRestoreStageValue(RestoreStage::None);
    setMode(Mode::Authenticating);

    track(m_client->createAccount(
        username,
        password,
        deviceInstallId(),
        deviceLabel(),
        devicePlatform()));
}

void AccountController::signIn(
    const QString &username,
    const QString &password) {
    if (!m_refreshToken.isEmpty())
        return;

    advanceGeneration();
    clearError();
    m_refreshTimer.stop();
    setRestoreStageValue(RestoreStage::None);
    setMode(Mode::Authenticating);

    track(m_client->signIn(
        username,
        password,
        deviceInstallId(),
        deviceLabel(),
        devicePlatform()));
}

void AccountController::pollPendingChallenge() {
    if (!m_pendingDeviceChallenge.isEmpty()) {
        track(m_client->pollDeviceChallenge(
            m_pendingDeviceChallenge));
        return;
    }

    if (!m_pendingTrustedRecoveryChallenge.isEmpty()) {
        track(m_client->pollTrustedRecovery(
            m_pendingTrustedRecoveryChallenge));
    }
}



void AccountController::cancelPendingAuthentication() {
    if (!m_refreshToken.isEmpty())
        return;

    advanceGeneration();
    m_challengeTimer.stop();
    m_pendingDeviceChallenge.clear();
    m_pendingTrustedRecoveryChallenge.clear();
    clearError();
    setRestoreStageValue(RestoreStage::None);
    setMode(Mode::SignedOut);
    emit signedOut();
}

void AccountController::useRecoveryKeyForPendingDevice(
    const QString &recoveryKey) {
    if (m_pendingDeviceChallenge.isEmpty())
        return;

    clearError();
    track(m_client->recoverDeviceChallengeWithKey(
        m_pendingDeviceChallenge,
        recoveryKey));
}

void AccountController::recoverPassword(
    const QString &username,
    const QString &recoveryKey,
    const QString &newPassword) {
    if (!m_refreshToken.isEmpty())
        return;

    advanceGeneration();
    clearError();
    setRestoreStageValue(RestoreStage::None);
    setMode(Mode::Authenticating);

    track(m_client->recoverPassword(
        username,
        recoveryKey,
        newPassword));
}

void AccountController::startTrustedRecovery(
    const QString &username,
    const QString &newPassword) {
    if (!m_refreshToken.isEmpty())
        return;

    advanceGeneration();
    clearError();
    setRestoreStageValue(RestoreStage::None);
    setMode(Mode::Authenticating);

    track(m_client->startTrustedRecovery(
        username,
        newPassword,
        deviceInstallId(),
        deviceLabel(),
        devicePlatform()));
}

void AccountController::logoutCurrent() {
    beginLogout(
        PendingLogout::Current);
}

void AccountController::logoutEverywhere() {
    beginLogout(
        PendingLogout::Everywhere);
}

void AccountController::stayAndRetrySignOut() {
    if (m_pendingLogout
        == PendingLogout::None) {
        return;
    }

    clearPendingLogoutWarning();
    clearError();

    if (m_syncEngine
        && m_syncEngine->active()) {
        m_syncEngine->beginSignOutFlush();
        return;
    }

    continuePendingLogout();
}

void AccountController::signOutAnyway() {
    if (m_pendingLogout
        == PendingLogout::None) {
        return;
    }

    if (m_syncEngine)
        m_syncEngine->setNetworkEnabled(
            false);

    clearPendingLogoutWarning();
    continuePendingLogout();
}

void AccountController::changePassword(
    const QString &currentPassword,
    const QString &newPassword) {
    if (m_mode != Mode::SignedIn)
        return;

    clearError();
    track(m_client->changePassword(
        currentPassword,
        newPassword));
}

void AccountController::replaceRecoveryKey(
    const QString &currentPassword) {
    if (m_mode != Mode::SignedIn)
        return;

    clearError();
    track(m_client->replaceRecoveryKey(currentPassword));
}

void AccountController::refreshProfile() {
    if (m_mode != Mode::SignedIn)
        return;

    track(m_client->getProfile());
}

void AccountController::renameUsername(const QString &username) {
    if (m_mode != Mode::SignedIn)
        return;

    clearError();
    track(m_client->renameUsername(username));
}

void AccountController::setBuiltinAvatar(const QString &avatarId) {
    if (m_mode != Mode::SignedIn)
        return;

    clearError();
    track(m_client->setBuiltinAvatar(avatarId));
}

void AccountController::refreshDevices() {
    if (m_mode != Mode::SignedIn)
        return;

    track(m_client->listDevices());
}

void AccountController::revokeDevice(const QString &deviceId) {
    if (m_mode != Mode::SignedIn)
        return;

    const quint64 requestId = track(
        m_client->revokeDevice(deviceId));
    m_revokeDeviceRequests.insert(requestId, deviceId);
}

void AccountController::setNewDeviceProtection(bool enabled) {
    if (m_mode != Mode::SignedIn)
        return;

    clearError();
    track(m_client->setNewDeviceProtection(enabled));
}

void AccountController::refreshApprovals() {
    if (m_mode != Mode::SignedIn)
        return;

    track(m_client->listApprovals(0));
}

void AccountController::decideApproval(
    const QString &kind,
    const QString &challengeId,
    bool approve) {
    if (m_mode != Mode::SignedIn)
        return;

    clearError();
    track(m_client->decideApproval(
        kind,
        challengeId,
        approve));
}

void AccountController::flushPendingRevocations() {
    if (!m_credentialStore->isAvailable())
        return;

    const QList<QByteArray> pending =
        m_credentialStore->pendingRevocations();

    for (const QByteArray &token : pending) {
        if (token.isEmpty())
            continue;

        bool alreadyPending = false;
        for (auto it = m_pendingRevocationRequests.cbegin();
             it != m_pendingRevocationRequests.cend();
             ++it) {
            if (it.value() == token) {
                alreadyPending = true;
                break;
            }
        }
        if (alreadyPending)
            continue;

        const quint64 requestId =
            m_client->revokeRefreshToken(token);
        m_pendingRevocationRequests.insert(
            requestId,
            token);
    }
}

quint64 AccountController::track(quint64 requestId) {
    m_requestGenerations.insert(requestId, m_generation);
    return requestId;
}

void AccountController::advanceGeneration() {
    ++m_generation;
    m_requestGenerations.clear();
    m_revokeDeviceRequests.clear();
    m_revokeRefreshRequests.clear();
    m_refreshRequestId = 0;
    m_accessTokenRecoveryInFlight = false;
}

bool AccountController::takeTrackedRequest(quint64 requestId) {
    const auto it = m_requestGenerations.find(requestId);
    if (it == m_requestGenerations.end())
        return false;

    const quint64 generation = it.value();
    m_requestGenerations.erase(it);
    return generation == m_generation;
}

void AccountController::handleCompleted(
    quint64 requestId,
    AccountOperation operation,
    quint64 accessTokenGeneration,
    const AccountTransportReply &reply) {
    const auto pendingRevoke =
        m_pendingRevocationRequests.find(requestId);
    if (pendingRevoke != m_pendingRevocationRequests.end()) {
        const QByteArray token = pendingRevoke.value();
        m_pendingRevocationRequests.erase(pendingRevoke);

        if (isSuccess(reply))
            m_credentialStore->removePendingRevocation(token);
        return;
    }

    if (!takeTrackedRequest(requestId))
        return;

    if (requestId == m_refreshRequestId)
        m_refreshRequestId = 0;

    if (operationRequiresActiveSession(operation)
        && operation != AccountOperation::LogoutCurrent
        && operation != AccountOperation::LogoutEverywhere
        && (reply.errorCode == QLatin1String("session_revoked")
            || reply.errorCode == QLatin1String("session_invalid"))) {
        QString revokedDeviceId;
        if (operation == AccountOperation::RevokeDevice)
            revokedDeviceId = m_revokeDeviceRequests.take(requestId);

        if (accessTokenGeneration != 0
            && accessTokenGeneration
                != m_client->accessTokenGeneration()) {
            // This reply belongs to a superseded access-token generation:
            // the repair above must not start a second refresh or run
            // teardown for it. But the operation the UI was waiting on
            // (Saving.../Revoking... spinners) still needs a completion
            // signal, or the page's pending flag never clears. Deliver
            // only the operation-specific failure signal here; never
            // touch m_errorCategory/m_lastErrorCode/m_lastErrorMessage
            // and never call beginAccessTokenRecovery() for a stale reply.
            emitStaleGenerationCompletion(
                operation, requestId, revokedDeviceId);
            return;
        }

        beginAccessTokenRecovery();
        return;
    }

    switch (operation) {
    case AccountOperation::CreateAccount:
        handleCreateAccountReply(reply);
        return;

    case AccountOperation::SignIn:
        handleSignInReply(reply);
        return;

    case AccountOperation::RefreshSession:
        handleRefreshReply(reply);
        return;

    case AccountOperation::PollDeviceChallenge:
        handleDeviceChallengePollReply(reply);
        return;

    case AccountOperation::RecoverDeviceChallengeWithKey:
        if (isSuccess(reply)) {
            m_pendingDeviceChallenge.clear();
            m_challengeTimer.stop();

            const QString recoveryKey = reply.body
                .value(QStringLiteral("recovery_key"))
                .toString();
            if (!deliverRecoveryKey(
                    recoveryKey,
                    AccountRecoveryKeyPurpose::DeviceChallengeRecovered)) {
                revokeSessionObjectBestEffort(
                    reply.body.value(
                        QStringLiteral("session")).toObject());
                setError(
                    ErrorCategory::Storage,
                    QStringLiteral("recovery_key_delivery_failed"),
                    QStringLiteral(
                        "The replacement recovery key could not be presented."),
                    true);
                return;
            }

            const QJsonObject sessionObject =
                reply.body.value(
                    QStringLiteral("session")).toObject();

            if (!prepareProfileForSession(
                    sessionObject,
                    false)) {
                return;
            }

            if (!adoptSession(sessionObject)) {
                setError(
                    ErrorCategory::Storage,
                    QStringLiteral("secure_store_unavailable"),
                    QStringLiteral(
                        "The account session could not be stored securely."),
                    true);
            }
            return;
        }

        setErrorFromReply(reply);
        scheduleChallengePoll();
        return;

    case AccountOperation::RecoverPassword:
        if (isSuccess(reply)) {
            const QString recoveryKey = reply.body
                .value(QStringLiteral("recovery_key"))
                .toString();

            clearStoredSession();
            clearVolatileSession();
            setMode(Mode::SignedOut);
            emit signedOut();

            if (!deliverRecoveryKey(
                    recoveryKey,
                    AccountRecoveryKeyPurpose::PasswordRecovered)) {
                setError(
                    ErrorCategory::Storage,
                    QStringLiteral("recovery_key_delivery_failed"),
                    QStringLiteral(
                        "The replacement recovery key could not be presented."),
                    true);
            }
            return;
        }

        setMode(Mode::SignedOut);
        setErrorFromReply(reply);
        return;

    case AccountOperation::StartTrustedRecovery:
        if (isSuccess(reply)) {
            beginTrustedRecoveryChallenge(
                reply.body
                    .value(QStringLiteral("challenge_token"))
                    .toString(),
                reply.body
                    .value(QStringLiteral("challenge_expires_at"))
                    .toString());
            return;
        }

        setMode(Mode::SignedOut);
        setErrorFromReply(reply);
        return;

    case AccountOperation::PollTrustedRecovery:
        handleTrustedRecoveryPollReply(reply);
        return;

    case AccountOperation::LogoutCurrent:
        if (isSuccess(reply)
            || reply.errorCode == QLatin1String("session_invalid")
            || reply.errorCode == QLatin1String("session_revoked")) {
            finishLocalSignOut(false);
            return;
        }

        if (reply.networkError) {
            bool queued = true;
            if (!m_refreshToken.isEmpty()) {
                queued = m_credentialStore->addPendingRevocation(
                    m_refreshToken);
            }

            finishLocalSignOut(false);
            if (!queued
                && m_lastErrorCode
                    != QLatin1String("profile_seal_failed")) {
                setError(
                    ErrorCategory::Storage,
                    QStringLiteral("pending_revocation_store_failed"),
                    QStringLiteral(
                        "Signed out locally, but server revocation could not be queued."));
            }
            return;
        }

        if (m_syncEngine
            && m_syncEngine->active()) {
            m_syncEngine->setNetworkEnabled(
                true);
        }
        setErrorFromReply(reply);
        return;

    case AccountOperation::LogoutEverywhere:
        if (isSuccess(reply)
            || reply.errorCode == QLatin1String("session_invalid")
            || reply.errorCode == QLatin1String("session_revoked")) {
            finishLocalSignOut(false);
            return;
        }

        if (reply.networkError) {
            if (m_syncEngine
                && m_syncEngine->active()) {
                m_syncEngine->setNetworkEnabled(
                    true);
            }
            setMode(Mode::Offline);
            setSyncStateValue(SyncState::Retrying);
            setErrorFromReply(reply);
            return;
        }

        if (m_syncEngine
            && m_syncEngine->active()) {
            m_syncEngine->setNetworkEnabled(
                true);
        }
        setErrorFromReply(reply);
        return;

    case AccountOperation::ChangePassword:
        if (!isSuccess(reply)) {
            setErrorFromReply(reply);
            return;
        }
        emit passwordChangeSucceeded();
        return;

    case AccountOperation::ReplaceRecoveryKey:
        if (isSuccess(reply)) {
            const QString recoveryKey = reply.body
                .value(QStringLiteral("recovery_key"))
                .toString();
            if (!deliverRecoveryKey(
                    recoveryKey,
                    AccountRecoveryKeyPurpose::ManualReplacement)) {
                setError(
                    ErrorCategory::Storage,
                    QStringLiteral("recovery_key_delivery_failed"),
                    QStringLiteral(
                        "The replacement recovery key could not be presented."));
                emit recoveryKeyReplacementFailed(
                    m_lastErrorMessage, errorCategory(), m_lastErrorCode);
                return;
            }
            emit recoveryKeyReplacementSucceeded();
            return;
        }

        setErrorFromReply(reply);
        emit recoveryKeyReplacementFailed(
            m_lastErrorMessage, errorCategory(), m_lastErrorCode);
        return;

    case AccountOperation::GetProfile:
    case AccountOperation::SetNewDeviceProtection:
        if (isSuccess(reply)) {
            if (reply.body.contains(QStringLiteral("username"))) {
                setUsername(
                    reply.body
                        .value(QStringLiteral("username"))
                        .toString());
            }
            if (reply.body.contains(QStringLiteral("builtin_avatar_id"))
                || reply.body.contains(QStringLiteral("avatar_id"))) {
                setAvatarId(accountAvatarId(reply.body));
            }
            if (reply.body.contains(
                    QStringLiteral("protect_new_device_signins"))) {
                setNewDeviceProtectionValue(
                    reply.body
                        .value(
                            QStringLiteral(
                                "protect_new_device_signins"))
                        .toBool());
            }
            return;
        }

        setErrorFromReply(reply);
        return;

    case AccountOperation::RenameUsername:
        if (isSuccess(reply)) {
            if (reply.body.contains(QStringLiteral("username")))
                setUsername(reply.body.value(QStringLiteral("username")).toString());
            else
                refreshProfile();
            if (reply.body.contains(QStringLiteral("builtin_avatar_id"))
                || reply.body.contains(QStringLiteral("avatar_id"))) {
                setAvatarId(accountAvatarId(reply.body));
            }
            emit usernameRenameSucceeded();
            return;
        }
        setErrorFromReply(reply);
        emit usernameRenameFailed(
            m_lastErrorMessage, errorCategory(), m_lastErrorCode);
        return;

    case AccountOperation::SetBuiltinAvatar:
        if (isSuccess(reply)) {
            if (reply.body.contains(QStringLiteral("username")))
                setUsername(reply.body.value(QStringLiteral("username")).toString());
            if (reply.body.contains(QStringLiteral("builtin_avatar_id"))
                || reply.body.contains(QStringLiteral("avatar_id"))) {
                setAvatarId(accountAvatarId(reply.body));
            } else {
                refreshProfile();
            }
            emit builtinAvatarChangeSucceeded();
            return;
        }
        setErrorFromReply(reply);
        emit builtinAvatarChangeFailed(
            m_lastErrorMessage, errorCategory(), m_lastErrorCode);
        return;

    case AccountOperation::ListDevices: {
        const QString reconciledRevoke =
            m_revokeRefreshRequests.take(requestId);

        if (isSuccess(reply)) {
            const QJsonValue devicesValue =
                reply.body.value(QStringLiteral("devices"));
            if (!devicesValue.isArray()) {
                setError(
                    ErrorCategory::Protocol,
                    QStringLiteral("invalid_devices_payload"),
                    QStringLiteral(
                        "The account service returned an invalid trusted-device list."));
                emit deviceListRefreshFailed(
                    m_lastErrorMessage, errorCategory(), m_lastErrorCode);
                return;
            }
            const QJsonArray devices = devicesValue.toArray();
            setDeviceCount(devices.size());
            if (m_devices != devices) {
                m_devices = devices;
                emit devicesChanged();
            }
            if (!reconciledRevoke.isEmpty())
                emit deviceRevokeSucceeded(reconciledRevoke);
            emit deviceListRefreshSucceeded();
            return;
        }

        setErrorFromReply(reply);
        emit deviceListRefreshFailed(
            m_lastErrorMessage, errorCategory(), m_lastErrorCode);
        return;
    }

    case AccountOperation::RevokeDevice: {
        const QString revokedDevice =
            m_revokeDeviceRequests.take(requestId);

        if (isSuccess(reply)) {
            if (!revokedDevice.isEmpty()
                && revokedDevice == m_deviceId) {
                emit deviceRevokeSucceeded(revokedDevice);
                finishLocalSignOut(true);
                return;
            }

            const quint64 refreshRequestId =
                track(m_client->listDevices());
            if (!revokedDevice.isEmpty())
                m_revokeRefreshRequests.insert(
                    refreshRequestId, revokedDevice);
            return;
        }

        setErrorFromReply(reply);
        emit deviceRevokeFailed(
            revokedDevice, m_lastErrorMessage, errorCategory(), m_lastErrorCode);
        return;
    }

    case AccountOperation::ListApprovals:
        if (isSuccess(reply)) {
            emit approvalRequestsChanged(
                reply.body
                    .value(QStringLiteral("approvals"))
                    .toArray());
            scheduleApprovalPoll();
            return;
        }

        if (reply.networkError) {
            scheduleApprovalPoll(kApprovalRetryMs);
            return;
        }

        setErrorFromReply(reply);
        scheduleApprovalPoll(kApprovalRetryMs);
        return;

    case AccountOperation::DecideApproval:
        if (isSuccess(reply)) {
            refreshApprovals();
            return;
        }

        setErrorFromReply(reply);
        return;

    case AccountOperation::RevokeRefreshToken:
    case AccountOperation::SyncPush:
    case AccountOperation::SyncPull:
        return;
    }
}

void AccountController::emitStaleGenerationCompletion(
    AccountOperation operation,
    quint64 requestId,
    const QString &revokedDeviceId) {
    // A stale-generation reply is a real completion for whatever the QML
    // page was waiting on, so the operation-specific signal still has to
    // fire. It must not go through setError()/setErrorFromReply(): those
    // mutate the shared m_errorCategory/m_lastErrorCode/m_lastErrorMessage
    // state and (via setError) emit the generic accountError signal, which
    // would contradict the repair's guarantee that a stale reply leaves
    // error/session state untouched.
    static const QString kStaleMessage = QStringLiteral(
        "The session was refreshed while this request was in flight. "
        "Try again.");
    static const QString kStaleCode =
        QStringLiteral("stale_session_retry");
    const QString staleCategory =
        errorCategoryName(ErrorCategory::Unavailable);

    switch (operation) {
    case AccountOperation::RenameUsername:
        emit usernameRenameFailed(
            kStaleMessage, staleCategory, kStaleCode);
        return;

    case AccountOperation::SetBuiltinAvatar:
        emit builtinAvatarChangeFailed(
            kStaleMessage, staleCategory, kStaleCode);
        return;

    case AccountOperation::ReplaceRecoveryKey:
        emit recoveryKeyReplacementFailed(
            kStaleMessage, staleCategory, kStaleCode);
        return;

    case AccountOperation::ListDevices: {
        const QString reconciledRevoke =
            m_revokeRefreshRequests.take(requestId);
        if (!reconciledRevoke.isEmpty()) {
            emit deviceRevokeFailed(
                reconciledRevoke,
                kStaleMessage,
                staleCategory,
                kStaleCode);
            return;
        }
        emit deviceListRefreshFailed(
            kStaleMessage, staleCategory, kStaleCode);
        return;
    }

    case AccountOperation::RevokeDevice:
        emit deviceRevokeFailed(
            revokedDeviceId,
            kStaleMessage,
            staleCategory,
            kStaleCode);
        return;

    case AccountOperation::ChangePassword:
        // ChangePassword has no dedicated failure signal; the security
        // page's passwordRequestPending is cleared by its accountError
        // handler alone. Emit the signal directly (not via setError()) so
        // the persisted error-state properties stay untouched.
        emit accountError(staleCategory, kStaleCode, kStaleMessage);
        return;

    case AccountOperation::SetNewDeviceProtection:
        // SetNewDeviceProtection has no dedicated failure signal either;
        // AccountSecurityPage.qml clears protectionRequestPending only on
        // newDeviceProtectionChanged or accountError. Emit the signal
        // directly (not via setError()) so the persisted error-state
        // properties stay untouched.
        emit accountError(staleCategory, kStaleCode, kStaleMessage);
        return;

    default:
        // Other tracked operations (GetProfile, ListApprovals,
        // DecideApproval, SyncPush, SyncPull) have no QML busy-flag
        // contract tied to a per-operation failure signal; drop the stale
        // reply silently, matching prior behavior for them.
        return;
    }
}

bool AccountController::prepareLocalOnlyProfile() {
    if (!m_profileCoordinator)
        return true;

    QString profileError;
    if (m_profileCoordinator->prepareLocalOnly(
            &profileError)) {
        return true;
    }

    setError(
        ErrorCategory::Storage,
        QStringLiteral(
            "local_profile_prepare_failed"),
        profileError.trimmed().isEmpty()
            ? QStringLiteral(
                  "Local-only personal state could not be prepared safely.")
            : profileError,
        true);
    return false;
}

bool AccountController::prepareRememberedProfile(
    const StoredAccountCredential &credential) {
    if (!m_profileCoordinator)
        return true;

    QString profileError;
    if (m_profileCoordinator
            ->prepareRememberedAccount(
                credential.accountId,
                &profileError)) {
        return true;
    }

    if (!credential.refreshToken.isEmpty()) {
        if (m_credentialStore->isAvailable()) {
            m_credentialStore->addPendingRevocation(
                credential.refreshToken);
        }

        const quint64 requestId =
            m_client->revokeRefreshToken(
                credential.refreshToken);
        m_pendingRevocationRequests.insert(
            requestId,
            credential.refreshToken);
    }

    clearStoredSession();
    clearVolatileSession();

    setError(
        ErrorCategory::Storage,
        QStringLiteral("profile_prepare_failed"),
        profileError.trimmed().isEmpty()
            ? QStringLiteral(
                  "The remembered account profile could not be opened safely.")
            : profileError,
        true);
    return false;
}

bool AccountController::prepareProfileForSession(
    const QJsonObject &sessionObject,
    bool accountCreated) {
    const QString accountId = sessionObject
        .value(QStringLiteral("account"))
        .toObject()
        .value(QStringLiteral("id"))
        .toString()
        .trimmed();

    if (accountId.isEmpty()) {
        revokeSessionObjectBestEffort(sessionObject);
        setError(
            ErrorCategory::Protocol,
            QStringLiteral("protocol_error"),
            QStringLiteral(
                "The account service returned a session without an account identifier."),
            true);
        return false;
    }

    if (!m_profileCoordinator)
        return true;

    QString profileError;
    const bool prepared = accountCreated
        ? m_profileCoordinator->prepareCreatedAccount(
              accountId,
              &profileError)
        : m_profileCoordinator->prepareAccountSession(
              accountId,
              &profileError);

    if (prepared)
        return true;

    revokeSessionObjectBestEffort(sessionObject);

    if (!m_refreshToken.isEmpty())
        clearStoredSession();
    clearVolatileSession();

    setError(
        ErrorCategory::Storage,
        QStringLiteral("profile_prepare_failed"),
        profileError.trimmed().isEmpty()
            ? QStringLiteral(
                  "The account profile could not be prepared safely.")
            : profileError,
        true);
    return false;
}

bool AccountController::adoptSession(
    const QJsonObject &sessionObject) {
    const QJsonObject accountObject = sessionObject
        .value(QStringLiteral("account"))
        .toObject();
    const QJsonObject deviceObject = sessionObject
        .value(QStringLiteral("device"))
        .toObject();

    const QString accountId = accountObject
        .value(QStringLiteral("id"))
        .toString()
        .trimmed();
    const QString deviceId = deviceObject
        .value(QStringLiteral("id"))
        .toString()
        .trimmed();
    const QString username = accountObject
        .value(QStringLiteral("username"))
        .toString();

    const QByteArray accessToken = sessionObject
        .value(QStringLiteral("access_token"))
        .toString()
        .toLatin1();
    const QByteArray refreshToken = sessionObject
        .value(QStringLiteral("refresh_token"))
        .toString()
        .toLatin1();
    const QString accessExpiresAt = sessionObject
        .value(QStringLiteral("access_expires_at"))
        .toString();

    if (accountId.isEmpty()
        || deviceId.isEmpty()
        || username.trimmed().isEmpty()
        || accessToken.isEmpty()
        || refreshToken.isEmpty()
        || accessExpiresAt.trimmed().isEmpty()) {
        return false;
    }

    if (!m_credentialStore->isAvailable()) {
        m_bootstrapStore->setCredentialClearPending(true);
        clearVolatileSession();

        const quint64 revokeRequest =
            m_client->revokeRefreshToken(refreshToken);
        m_pendingRevocationRequests.insert(
            revokeRequest,
            refreshToken);
        return false;
    }

    StoredAccountCredential credential;
    credential.accountId = accountId;
    credential.deviceId = deviceId;
    credential.refreshToken = refreshToken;

    if (!m_credentialStore->saveActive(credential)) {
        m_bootstrapStore->setCredentialClearPending(true);
        clearStoredSession();
        clearVolatileSession();

        const quint64 revokeRequest =
            m_client->revokeRefreshToken(refreshToken);
        m_pendingRevocationRequests.insert(
            revokeRequest,
            refreshToken);
        return false;
    }

    m_bootstrapStore->setCredentialClearPending(false);

    const bool wasRestoring =
        m_mode == Mode::Restoring
        || m_restoreStage == RestoreStage::SessionRefresh
        || m_restoreStage == RestoreStage::Offline;

    m_accountId = accountId;
    m_deviceId = deviceId;
    m_refreshToken = refreshToken;
    m_client->setAccessToken(accessToken);
    emit accountProfileReadyForSync();

    m_bootstrapStore->setLocalOnlyChosen(false);
    completeOnboarding();

    setUsername(username);
    setNewDeviceProtectionValue(
        accountObject
            .value(
                QStringLiteral(
                    "protect_new_device_signins"))
            .toBool());

    clearError();
    setMode(Mode::SignedIn);
    setSyncStateValue(SyncState::Idle);
    setRestoreStageValue(
        wasRestoring
            ? RestoreStage::Restored
            : RestoreStage::None);

    scheduleRefresh(accessExpiresAt);
    scheduleApprovalPoll();
    flushPendingRevocations();

    emit signedIn();
    return true;
}

bool AccountController::deliverRecoveryKey(
    const QString &recoveryKey,
    AccountRecoveryKeyPurpose purpose) {
    if (recoveryKey.trimmed().isEmpty())
        return false;
    return m_oneTimeSecretSink->presentRecoveryKey(
        recoveryKey,
        purpose);
}

void AccountController::revokeSessionObjectBestEffort(
    const QJsonObject &sessionObject) {
    const QByteArray refreshToken = sessionObject
        .value(QStringLiteral("refresh_token"))
        .toString()
        .toLatin1();
    if (refreshToken.isEmpty())
        return;

    if (m_credentialStore->isAvailable())
        m_credentialStore->addPendingRevocation(refreshToken);

    const quint64 requestId =
        m_client->revokeRefreshToken(refreshToken);
    m_pendingRevocationRequests.insert(
        requestId,
        refreshToken);
}

void AccountController::handleCreateAccountReply(
    const AccountTransportReply &reply) {
    if (!isSuccess(reply)) {
        setMode(Mode::SignedOut);
        setErrorFromReply(reply);
        return;
    }

    const QJsonObject sessionObject = reply.body
        .value(QStringLiteral("session"))
        .toObject();
    const QString recoveryKey = reply.body
        .value(QStringLiteral("recovery_key"))
        .toString();

    if (!deliverRecoveryKey(
            recoveryKey,
            AccountRecoveryKeyPurpose::AccountCreated)) {
        revokeSessionObjectBestEffort(sessionObject);
        setError(
            ErrorCategory::Storage,
            QStringLiteral("recovery_key_delivery_failed"),
            QStringLiteral(
                "The recovery key could not be presented."),
            true);
        return;
    }

    if (!prepareProfileForSession(
            sessionObject,
            true)) {
        return;
    }

    if (!adoptSession(sessionObject)) {
        setError(
            ErrorCategory::Storage,
            QStringLiteral("secure_store_unavailable"),
            QStringLiteral(
                "The account session could not be stored securely."),
            true);
    }
}

void AccountController::handleSignInReply(
    const AccountTransportReply &reply) {
    if (!isSuccess(reply)) {
        setMode(Mode::SignedOut);
        setErrorFromReply(reply);
        return;
    }

    const QString status = reply.body
        .value(QStringLiteral("status"))
        .toString();

    if (status == QLatin1String("approval_required")) {
        beginDeviceChallenge(
            reply.body
                .value(QStringLiteral("challenge_token"))
                .toString(),
            reply.body
                .value(QStringLiteral("challenge_expires_at"))
                .toString());
        return;
    }

    if (status != QLatin1String("signed_in")) {
        setError(
            ErrorCategory::Protocol,
            QStringLiteral("protocol_error"),
            QStringLiteral(
                "The account service returned an invalid sign-in state."),
            true);
        return;
    }

    const QJsonObject sessionObject =
        reply.body
            .value(QStringLiteral("session"))
            .toObject();

    if (!prepareProfileForSession(
            sessionObject,
            false)) {
        return;
    }

    if (!adoptSession(sessionObject)) {
        setError(
            ErrorCategory::Storage,
            QStringLiteral("secure_store_unavailable"),
            QStringLiteral(
                "The account session could not be stored securely."),
            true);
    }
}

void AccountController::handleRefreshReply(
    const AccountTransportReply &reply) {
    const bool recoveringAccessToken =
        m_accessTokenRecoveryInFlight;

    if (isSuccess(reply)) {
        const QJsonObject sessionObject =
            reply.body
                .value(QStringLiteral("session"))
                .toObject();

        if (!prepareProfileForSession(
                sessionObject,
                false)) {
            m_accessTokenRecoveryInFlight = false;
            return;
        }

        if (!adoptSession(sessionObject)) {
            m_accessTokenRecoveryInFlight = false;
            setError(
                ErrorCategory::Storage,
                QStringLiteral("secure_store_unavailable"),
                QStringLiteral(
                    "The account session could not be stored securely."),
                true);
            return;
        }

        m_accessTokenRecoveryInFlight = false;
        if (recoveringAccessToken
            && m_syncEngine
            && m_syncEngine->active()) {
            m_syncEngine->setNetworkEnabled(true);
            m_syncEngine->requestImmediateSync();
        }
        return;
    }

    if (reply.errorCode == QLatin1String("session_revoked")
        || reply.errorCode == QLatin1String("session_invalid")) {
        m_accessTokenRecoveryInFlight = false;
        finishLocalSignOut(true);
        return;
    }

    if (reply.networkError
        || (!isSuccess(reply)
            && reply.statusCode >= 500
            && reply.statusCode < 600)) {
        // Transient server conditions (5xx) retry like offline instead of
        // parking the session in terminal Error for the rest of the run.
        setMode(Mode::Offline);
        setSyncStateValue(SyncState::Retrying);
        setRestoreStageValue(RestoreStage::Offline);
        setErrorFromReply(reply);
        scheduleOfflineRefreshRetry();
        return;
    }

    m_accessTokenRecoveryInFlight = false;
    setErrorFromReply(reply, true);
}

void AccountController::handleDeviceChallengePollReply(
    const AccountTransportReply &reply) {
    if (!isSuccess(reply)) {
        if (reply.errorCode
                == QLatin1String("challenge_expired")
            || reply.errorCode
                == QLatin1String("challenge_denied")
            || reply.errorCode
                == QLatin1String("challenge_invalid")) {
            m_pendingDeviceChallenge.clear();
            m_challengeTimer.stop();
            setMode(Mode::SignedOut);
            setErrorFromReply(reply);
            return;
        }

        if (reply.networkError) {
            setMode(Mode::AwaitingDeviceApproval);
            setErrorFromReply(reply);
            scheduleChallengePoll();
            return;
        }

        setErrorFromReply(reply);
        scheduleChallengePoll();
        return;
    }

    const QString status = reply.body
        .value(QStringLiteral("status"))
        .toString();

    if (status == QLatin1String("pending")) {
        setMode(Mode::AwaitingDeviceApproval);
        scheduleChallengePoll();
        return;
    }

    if (status == QLatin1String("signed_in")) {
        m_pendingDeviceChallenge.clear();
        m_challengeTimer.stop();

        const QJsonObject sessionObject =
            reply.body
                .value(QStringLiteral("session"))
                .toObject();

        if (!prepareProfileForSession(
                sessionObject,
                false)) {
            return;
        }

        if (!adoptSession(sessionObject)) {
            setError(
                ErrorCategory::Storage,
                QStringLiteral("secure_store_unavailable"),
                QStringLiteral(
                    "The account session could not be stored securely."),
                true);
        }
        return;
    }

    setMode(Mode::SignedOut);
    setError(
        ErrorCategory::Protocol,
        QStringLiteral("protocol_error"),
        QStringLiteral(
            "The account service returned an invalid approval state."));
}

void AccountController::handleTrustedRecoveryPollReply(
    const AccountTransportReply &reply) {
    if (!isSuccess(reply)) {
        if (reply.errorCode
                == QLatin1String("challenge_expired")
            || reply.errorCode
                == QLatin1String("challenge_denied")
            || reply.errorCode
                == QLatin1String("challenge_invalid")) {
            m_pendingTrustedRecoveryChallenge.clear();
            m_challengeTimer.stop();
            setMode(Mode::SignedOut);
            setErrorFromReply(reply);
            return;
        }

        if (reply.networkError) {
            setMode(Mode::AwaitingRecoveryApproval);
            setErrorFromReply(reply);
            scheduleChallengePoll();
            return;
        }

        setErrorFromReply(reply);
        scheduleChallengePoll();
        return;
    }

    const QString status = reply.body
        .value(QStringLiteral("status"))
        .toString();

    if (status == QLatin1String("pending")) {
        setMode(Mode::AwaitingRecoveryApproval);
        scheduleChallengePoll();
        return;
    }

    if (status == QLatin1String("recovered")) {
        m_pendingTrustedRecoveryChallenge.clear();
        m_challengeTimer.stop();

        clearStoredSession();
        clearVolatileSession();
        setMode(Mode::SignedOut);
        emit signedOut();

        if (!deliverRecoveryKey(
                reply.body
                    .value(QStringLiteral("recovery_key"))
                    .toString(),
                AccountRecoveryKeyPurpose::PasswordRecovered)) {
            setError(
                ErrorCategory::Storage,
                QStringLiteral("recovery_key_delivery_failed"),
                QStringLiteral(
                    "The replacement recovery key could not be presented."),
                true);
        }
        return;
    }

    setMode(Mode::SignedOut);
    setError(
        ErrorCategory::Protocol,
        QStringLiteral("protocol_error"),
        QStringLiteral(
            "The account service returned an invalid recovery state."));
}

void AccountController::setMode(Mode mode) {
    if (m_mode == mode)
        return;

    m_mode = mode;
    emit modeChanged();
}

void AccountController::setSyncStateValue(SyncState state) {
    if (m_syncState == state)
        return;

    m_syncState = state;
    emit syncStateChanged();
}

void AccountController::setRestoreStageValue(
    RestoreStage stage) {
    if (m_restoreStage == stage)
        return;

    m_restoreStage = stage;
    emit restoreStageChanged();
}

void AccountController::setUsername(
    const QString &username) {
    if (m_username == username)
        return;

    m_username = username;
    emit usernameChanged();
}

void AccountController::setAvatarId(
    const QString &avatarId) {
    const QString normalized = avatarId.trimmed();
    if (m_avatarId == normalized)
        return;
    m_avatarId = normalized;
    emit avatarIdChanged();
}

void AccountController::setDeviceCount(int count) {
    count = qMax(0, count);
    if (m_deviceCount == count)
        return;

    m_deviceCount = count;
    emit deviceCountChanged();
}

void AccountController::setNewDeviceProtectionValue(
    bool enabled) {
    if (m_newDeviceProtection == enabled)
        return;

    m_newDeviceProtection = enabled;
    emit newDeviceProtectionChanged();
}

void AccountController::setPendingOutboxCountValue(
    int count) {
    count = qMax(0, count);
    if (m_pendingOutboxCount == count)
        return;

    m_pendingOutboxCount = count;
    emit pendingOutboxCountChanged();
}

void AccountController::setError(
    ErrorCategory category,
    const QString &code,
    const QString &message,
    bool terminal) {
    const QString safeCode = code.trimmed().isEmpty()
        ? QStringLiteral("account_error")
        : code.trimmed();
    const QString safeMessage = message.trimmed().isEmpty()
        ? QStringLiteral(
            "The account request could not be completed.")
        : message.trimmed();

    const bool changed =
        m_errorCategory != category
        || m_lastErrorCode != safeCode
        || m_lastErrorMessage != safeMessage;

    m_errorCategory = category;
    m_lastErrorCode = safeCode;
    m_lastErrorMessage = safeMessage;

    if (changed)
        emit lastErrorChanged();

    emit accountError(
        errorCategoryName(category),
        safeCode,
        safeMessage);

    if (terminal)
        setMode(Mode::Error);
}

void AccountController::setErrorFromReply(
    const AccountTransportReply &reply,
    bool terminal) {
    setError(
        categoryForReply(reply),
        reply.errorCode,
        reply.errorMessage,
        terminal);
}

void AccountController::clearError() {
    if (m_errorCategory == ErrorCategory::None
        && m_lastErrorCode.isEmpty()
        && m_lastErrorMessage.isEmpty()) {
        return;
    }

    m_errorCategory = ErrorCategory::None;
    m_lastErrorCode.clear();
    m_lastErrorMessage.clear();
    emit lastErrorChanged();
}

void AccountController::clearVolatileSession() {
    m_refreshTimer.stop();
    m_challengeTimer.stop();
    m_approvalTimer.stop();
    m_refreshRequestId = 0;
    m_accessTokenRecoveryInFlight = false;

    m_client->clearAccessToken();

    if (!m_refreshToken.isEmpty())
        m_refreshToken.fill('\0');

    m_accountId.clear();
    m_deviceId.clear();
    m_refreshToken.clear();
    m_refreshToken.squeeze();

    m_pendingDeviceChallenge.clear();
    m_pendingTrustedRecoveryChallenge.clear();

    setUsername(QString());
    setAvatarId(QString());
    setDeviceCount(0);
    setNewDeviceProtectionValue(false);
    setSyncStateValue(SyncState::Inactive);
    setPendingOutboxCountValue(0);
    setRestoreStageValue(RestoreStage::None);

    if (m_deletionEffectiveAt.isValid()) {
        m_deletionEffectiveAt = QDateTime();
        emit deletionEffectiveAtChanged();
    }
}

bool AccountController::clearStoredSession() {
    if (!m_credentialStore->isAvailable()) {
        m_bootstrapStore->setCredentialClearPending(true);
        return false;
    }

    const bool cleared = m_credentialStore->clearActive();
    m_bootstrapStore->setCredentialClearPending(!cleared);
    return cleared;
}

void AccountController::beginLogout(
    PendingLogout logout) {
    if (m_mode != Mode::SignedIn
        && m_mode != Mode::Offline
        && m_mode != Mode::Error) {
        return;
    }

    if (m_pendingLogout
        != PendingLogout::None) {
        return;
    }

    m_pendingLogout = logout;
    clearError();
    m_refreshTimer.stop();

    if (m_syncEngine
        && m_syncEngine->active()) {
        m_syncEngine->beginSignOutFlush();
        return;
    }

    continuePendingLogout();
}

void AccountController::continuePendingLogout() {
    const PendingLogout logout =
        m_pendingLogout;
    if (logout == PendingLogout::None)
        return;

    clearPendingLogoutWarning();
    m_pendingLogout =
        PendingLogout::None;

    if (logout
        == PendingLogout::Current) {
        track(
            m_client->logoutCurrent());
    } else {
        track(
            m_client->logoutEverywhere());
    }
}

void AccountController::clearPendingLogoutWarning() {
    if (!m_signOutSyncWarningPending)
        return;

    m_signOutSyncWarningPending = false;
    emit signOutSyncWarningPendingChanged();
}

void AccountController::finishLocalSignOut(bool locked) {
    advanceGeneration();
    clearPendingLogoutWarning();
    m_pendingLogout =
        PendingLogout::None;

    if (m_syncEngine
        && m_syncEngine->active()) {
        QString ignored;
        m_syncEngine->stopPreservingOutbox(
            &ignored);
    }

    const QString accountId =
        m_accountId;
    QString profileError;
    const bool sealed =
        !m_profileCoordinator
        || accountId.isEmpty()
        || m_profileCoordinator
               ->sealAccountSession(
                   accountId,
                   &profileError);

    clearStoredSession();
    clearVolatileSession();
    clearError();

    m_bootstrapStore->setLocalOnlyChosen(false);

    if (!sealed) {
        setError(
            ErrorCategory::Storage,
            QStringLiteral("profile_seal_failed"),
            profileError.trimmed().isEmpty()
                ? QStringLiteral(
                      "The account profile could not be sealed safely.")
                : profileError,
            true);
        return;
    }

    if (locked) {
        setError(
            ErrorCategory::Security,
            QStringLiteral("session_revoked"),
            QStringLiteral(
                "This device was signed out because its account session was revoked."));
        setMode(Mode::Locked);
        emit currentDeviceLocked();
    } else {
        setMode(Mode::SignedOut);
        emit signedOut();
    }
}

void AccountController::beginAccessTokenRecovery() {
    if (m_refreshToken.isEmpty()
        || m_mode == Mode::LocalOnly
        || m_mode == Mode::SignedOut
        || m_mode == Mode::Locked
        || m_mode == Mode::DeletionPending) {
        return;
    }

    m_accessTokenRecoveryInFlight = true;
    m_refreshTimer.stop();
    m_approvalTimer.stop();

    if (m_syncEngine && m_syncEngine->active())
        m_syncEngine->setNetworkEnabled(false);

    setSyncStateValue(SyncState::Retrying);
    requestSessionRefresh(true);
}

void AccountController::requestSessionRefresh(bool accessTokenRecovery) {
    if (m_refreshToken.isEmpty())
        return;

    if (accessTokenRecovery)
        m_accessTokenRecoveryInFlight = true;

    if (m_refreshRequestId != 0)
        return;

    m_refreshRequestId = track(
        m_client->refreshSession(m_refreshToken));
}

void AccountController::scheduleRefresh(
    const QString &accessExpiresAt) {
    m_refreshTimer.stop();
    if (m_refreshToken.isEmpty())
        return;

    const QDateTime expires = QDateTime::fromString(
        accessExpiresAt,
        Qt::ISODateWithMs);

    if (!expires.isValid()) {
        m_refreshTimer.start(5 * 1000);
        return;
    }

    const qint64 seconds =
        QDateTime::currentDateTimeUtc()
            .secsTo(expires.toUTC());
    const qint64 delaySeconds =
        qMax<qint64>(
            5,
            seconds - kRefreshLeadSeconds);

    m_refreshTimer.start(
        static_cast<int>(
            qMin<qint64>(
                delaySeconds * 1000,
                std::numeric_limits<int>::max())));
}

void AccountController::scheduleChallengePoll() {
    if (!m_automaticPollingEnabled)
        return;

    if (m_pendingDeviceChallenge.isEmpty()
        && m_pendingTrustedRecoveryChallenge.isEmpty()) {
        return;
    }

    m_challengeTimer.start(kChallengePollMs);
}

void AccountController::scheduleApprovalPoll(int delayMs) {
    if (!m_automaticPollingEnabled
        || m_mode != Mode::SignedIn) {
        return;
    }

    m_approvalTimer.start(qMax(0, delayMs));
}

void AccountController::scheduleOfflineRefreshRetry() {
    if (m_refreshToken.isEmpty())
        return;

    m_refreshTimer.start(kOfflineRefreshRetryMs);
}

void AccountController::beginDeviceChallenge(
    const QString &challengeToken,
    const QString &expiresAt) {
    const QByteArray token = challengeToken.toLatin1();
    const QDateTime expires = QDateTime::fromString(
        expiresAt,
        Qt::ISODateWithMs);

    if (token.isEmpty() || !expires.isValid()) {
        setMode(Mode::SignedOut);
        setError(
            ErrorCategory::Protocol,
            QStringLiteral("protocol_error"),
            QStringLiteral(
                "The account service returned an invalid approval request."));
        return;
    }

    m_pendingTrustedRecoveryChallenge.clear();
    m_pendingDeviceChallenge = token;

    setMode(Mode::AwaitingDeviceApproval);
    scheduleChallengePoll();
}

void AccountController::beginTrustedRecoveryChallenge(
    const QString &challengeToken,
    const QString &expiresAt) {
    const QByteArray token = challengeToken.toLatin1();
    const QDateTime expires = QDateTime::fromString(
        expiresAt,
        Qt::ISODateWithMs);

    if (token.isEmpty() || !expires.isValid()) {
        setMode(Mode::SignedOut);
        setError(
            ErrorCategory::Protocol,
            QStringLiteral("protocol_error"),
            QStringLiteral(
                "The account service returned an invalid recovery request."));
        return;
    }

    m_pendingDeviceChallenge.clear();
    m_pendingTrustedRecoveryChallenge = token;

    setMode(Mode::AwaitingRecoveryApproval);
    scheduleChallengePoll();
}

QString AccountController::deviceInstallId() {
    return m_deviceIdentity->installId();
}

QString AccountController::deviceLabel() const {
    return m_deviceIdentity->label();
}

QString AccountController::devicePlatform() const {
    return m_deviceIdentity->platform();
}

QString AccountController::modeName(Mode mode) {
    switch (mode) {
    case Mode::LocalOnly:
        return QStringLiteral("localOnly");
    case Mode::SignedOut:
        return QStringLiteral("signedOut");
    case Mode::Restoring:
        return QStringLiteral("restoring");
    case Mode::Authenticating:
        return QStringLiteral("authenticating");
    case Mode::AwaitingDeviceApproval:
        return QStringLiteral("awaitingDeviceApproval");
    case Mode::AwaitingRecoveryApproval:
        return QStringLiteral("awaitingRecoveryApproval");
    case Mode::SignedIn:
        return QStringLiteral("signedIn");
    case Mode::Offline:
        return QStringLiteral("offline");
    case Mode::Locked:
        return QStringLiteral("locked");
    case Mode::DeletionPending:
        return QStringLiteral("deletionPending");
    case Mode::Error:
        return QStringLiteral("error");
    }

    return QStringLiteral("error");
}

QString AccountController::syncStateName(
    SyncState state) {
    switch (state) {
    case SyncState::Inactive:
        return QStringLiteral("inactive");
    case SyncState::Idle:
        return QStringLiteral("idle");
    case SyncState::Retrying:
        return QStringLiteral("retrying");
    case SyncState::Blocked:
        return QStringLiteral("blocked");
    }

    return QStringLiteral("inactive");
}

QString AccountController::restoreStageName(
    RestoreStage stage) {
    switch (stage) {
    case RestoreStage::None:
        return QStringLiteral("none");
    case RestoreStage::CredentialLookup:
        return QStringLiteral("credentialLookup");
    case RestoreStage::SessionRefresh:
        return QStringLiteral("sessionRefresh");
    case RestoreStage::Restored:
        return QStringLiteral("restored");
    case RestoreStage::Offline:
        return QStringLiteral("offline");
    }

    return QStringLiteral("none");
}

QString AccountController::errorCategoryName(
    ErrorCategory category) {
    switch (category) {
    case ErrorCategory::None:
        return QStringLiteral("none");
    case ErrorCategory::Offline:
        return QStringLiteral("offline");
    case ErrorCategory::Credentials:
        return QStringLiteral("credentials");
    case ErrorCategory::Validation:
        return QStringLiteral("validation");
    case ErrorCategory::RateLimited:
        return QStringLiteral("rateLimited");
    case ErrorCategory::Unavailable:
        return QStringLiteral("unavailable");
    case ErrorCategory::Security:
        return QStringLiteral("security");
    case ErrorCategory::Conflict:
        return QStringLiteral("conflict");
    case ErrorCategory::Protocol:
        return QStringLiteral("protocol");
    case ErrorCategory::Storage:
        return QStringLiteral("storage");
    case ErrorCategory::Unknown:
        return QStringLiteral("unknown");
    }

    return QStringLiteral("unknown");
}

AccountController::ErrorCategory
AccountController::categoryForReply(
    const AccountTransportReply &reply) {
    if (reply.networkError
        || reply.errorCode == QLatin1String("network_error")) {
        return ErrorCategory::Offline;
    }

    const QString code = reply.errorCode;
    if (code == QLatin1String("invalid_credentials"))
        return ErrorCategory::Credentials;

    if (code == QLatin1String("invalid_username")
        || code == QLatin1String("invalid_password")
        || code == QLatin1String("invalid_request")) {
        return ErrorCategory::Validation;
    }

    if (code == QLatin1String("rate_limited"))
        return ErrorCategory::RateLimited;

    if (code == QLatin1String("avatar_unavailable")
        || code == QLatin1String("service_error")
        || code == QLatin1String("service_unavailable")) {
        return ErrorCategory::Unavailable;
    }

    if (code == QLatin1String("session_invalid")
        || code == QLatin1String("session_revoked")
        || code == QLatin1String("challenge_invalid")
        || code == QLatin1String("challenge_expired")
        || code == QLatin1String("challenge_denied")
        || code == QLatin1String("trusted_device_unavailable")) {
        return ErrorCategory::Security;
    }

    if (code == QLatin1String("username_unavailable")
        || code == QLatin1String("username_change_cooldown")
        || reply.statusCode == 409) {
        return ErrorCategory::Conflict;
    }

    if (code == QLatin1String("redirect_not_allowed")
        || code == QLatin1String("transport_configuration")
        || code == QLatin1String("protocol_error")) {
        return ErrorCategory::Protocol;
    }

    if (code == QLatin1String("secure_store_unavailable")
        || code == QLatin1String("recovery_key_delivery_failed")
        || code == QLatin1String("pending_revocation_store_failed")) {
        return ErrorCategory::Storage;
    }

    return ErrorCategory::Unknown;
}

bool AccountController::isSuccess(
    const AccountTransportReply &reply) {
    return reply.statusCode >= 200
        && reply.statusCode < 300
        && !reply.networkError;
}

bool AccountController::operationRequiresActiveSession(
    AccountOperation operation) {
    switch (operation) {
    case AccountOperation::LogoutCurrent:
    case AccountOperation::LogoutEverywhere:
    case AccountOperation::ChangePassword:
    case AccountOperation::ReplaceRecoveryKey:
    case AccountOperation::GetProfile:
    case AccountOperation::RenameUsername:
    case AccountOperation::SetBuiltinAvatar:
    case AccountOperation::ListDevices:
    case AccountOperation::RevokeDevice:
    case AccountOperation::SetNewDeviceProtection:
    case AccountOperation::ListApprovals:
    case AccountOperation::DecideApproval:
    case AccountOperation::SyncPush:
    case AccountOperation::SyncPull:
        return true;

    case AccountOperation::CreateAccount:
    case AccountOperation::SignIn:
    case AccountOperation::RefreshSession:
    case AccountOperation::RevokeRefreshToken:
    case AccountOperation::RecoverPassword:
    case AccountOperation::StartTrustedRecovery:
    case AccountOperation::PollTrustedRecovery:
    case AccountOperation::PollDeviceChallenge:
    case AccountOperation::RecoverDeviceChallengeWithKey:
        return false;
    }

    return false;
}
