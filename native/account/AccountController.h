#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountBootstrapStore.h"
#include "AccountClient.h"
#include "AccountCredentialStore.h"
#include "AccountDeviceIdentity.h"
#include "AccountOneTimeSecretSink.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

class AccountProfileCoordinator;
class SyncEngine;

class AccountController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString mode READ mode NOTIFY modeChanged)
    Q_PROPERTY(QString syncState READ syncState NOTIFY syncStateChanged)
    Q_PROPERTY(QString restoreStage READ restoreStage NOTIFY restoreStageChanged)
    Q_PROPERTY(QString username READ username NOTIFY usernameChanged)
    Q_PROPERTY(QString avatarId READ avatarId NOTIFY avatarIdChanged)
    Q_PROPERTY(bool onboardingRequired READ onboardingRequired NOTIFY onboardingRequiredChanged)
    Q_PROPERTY(int deviceCount READ deviceCount NOTIFY deviceCountChanged)
    Q_PROPERTY(QJsonArray devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool newDeviceProtection READ newDeviceProtection NOTIFY newDeviceProtectionChanged)
    Q_PROPERTY(int pendingOutboxCount READ pendingOutboxCount NOTIFY pendingOutboxCountChanged)
    Q_PROPERTY(bool signOutSyncWarningPending READ signOutSyncWarningPending NOTIFY signOutSyncWarningPendingChanged)
    Q_PROPERTY(QString deletionEffectiveAt READ deletionEffectiveAt NOTIFY deletionEffectiveAtChanged)
    Q_PROPERTY(QString errorCategory READ errorCategory NOTIFY lastErrorChanged)
    Q_PROPERTY(QString lastErrorCode READ lastErrorCode NOTIFY lastErrorChanged)
    Q_PROPERTY(QString lastErrorMessage READ lastErrorMessage NOTIFY lastErrorChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY modeChanged)

public:
    enum class Mode {
        LocalOnly,
        SignedOut,
        Restoring,
        Authenticating,
        AwaitingDeviceApproval,
        AwaitingRecoveryApproval,
        SignedIn,
        Offline,
        Locked,
        DeletionPending,
        Error
    };
    Q_ENUM(Mode)

    enum class SyncState {
        Inactive,
        Idle,
        Retrying,
        Blocked
    };
    Q_ENUM(SyncState)

    enum class RestoreStage {
        None,
        CredentialLookup,
        SessionRefresh,
        Restored,
        Offline
    };
    Q_ENUM(RestoreStage)

    enum class ErrorCategory {
        None,
        Offline,
        Credentials,
        Validation,
        RateLimited,
        Unavailable,
        Security,
        Conflict,
        Protocol,
        Storage,
        Unknown
    };
    Q_ENUM(ErrorCategory)

    explicit AccountController(
        AccountClient *client,
        AccountCredentialStore *credentialStore,
        AccountDeviceIdentity *deviceIdentity,
        AccountBootstrapStore *bootstrapStore,
        AccountOneTimeSecretSink *oneTimeSecretSink,
        QObject *parent = nullptr);

    QString mode() const;
    Mode modeValue() const;

    QString syncState() const;
    SyncState syncStateValue() const;

    QString restoreStage() const;
    RestoreStage restoreStageValue() const;

    QString username() const;
    QString avatarId() const;
    bool onboardingRequired() const;
    int deviceCount() const;
    QJsonArray devices() const;
    bool newDeviceProtection() const;
    int pendingOutboxCount() const;
    bool signOutSyncWarningPending() const;
    QString deletionEffectiveAt() const;
    QString errorCategory() const;
    QString lastErrorCode() const;
    QString lastErrorMessage() const;
    bool busy() const;

    // Native-only identity used by later ProfileContext composition.
    QString accountId() const;
    Q_INVOKABLE QString deviceId() const;

    void setAutomaticPollingEnabled(bool enabled);
    void setProfileCoordinator(
        AccountProfileCoordinator *profileCoordinator);
    void setSyncEngine(
        SyncEngine *syncEngine);

    // Native-only sync/deletion composition seams. They expose no secret.
    void setSyncObservation(SyncState state, int pendingOutboxCount);
    void setDeletionPending(const QDateTime &effectiveAt);
    void clearDeletionPending();

    Q_INVOKABLE void restoreRememberedSession();
    Q_INVOKABLE void continueWithoutAccount();
    // Escape hatch out of guest/local-only mode back to the sign-in choice.
    Q_INVOKABLE void returnToSignIn();

    Q_INVOKABLE void createAccount(
        const QString &username,
        const QString &password);
    Q_INVOKABLE void signIn(
        const QString &username,
        const QString &password);

    Q_INVOKABLE void pollPendingChallenge();
    Q_INVOKABLE void cancelPendingAuthentication();
    Q_INVOKABLE void useRecoveryKeyForPendingDevice(
        const QString &recoveryKey);

    Q_INVOKABLE void recoverPassword(
        const QString &username,
        const QString &recoveryKey,
        const QString &newPassword);

    Q_INVOKABLE void startTrustedRecovery(
        const QString &username,
        const QString &newPassword);

    Q_INVOKABLE void logoutCurrent();
    Q_INVOKABLE void logoutEverywhere();
    Q_INVOKABLE void stayAndRetrySignOut();
    Q_INVOKABLE void signOutAnyway();

    Q_INVOKABLE void changePassword(
        const QString &currentPassword,
        const QString &newPassword);
    Q_INVOKABLE void replaceRecoveryKey(
        const QString &currentPassword);

    Q_INVOKABLE void refreshProfile();
    Q_INVOKABLE void renameUsername(const QString &username);
    Q_INVOKABLE void setBuiltinAvatar(const QString &avatarId);

    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE void revokeDevice(const QString &deviceId);
    Q_INVOKABLE void setNewDeviceProtection(bool enabled);

    Q_INVOKABLE void refreshApprovals();
    Q_INVOKABLE void decideApproval(
        const QString &kind,
        const QString &challengeId,
        bool approve);

    void flushPendingRevocations();

signals:
    void modeChanged();
    void syncStateChanged();
    void restoreStageChanged();
    void usernameChanged();
    void avatarIdChanged();
    void onboardingRequiredChanged();
    void deviceCountChanged();
    void devicesChanged();
    void newDeviceProtectionChanged();
    void pendingOutboxCountChanged();
    void signOutSyncWarningPendingChanged();
    void deletionEffectiveAtChanged();
    void lastErrorChanged();

    void signedIn();
    void accountProfileReadyForSync();
    void signedOut();
    void currentDeviceLocked();
    void passwordChangeSucceeded();
    void approvalRequestsChanged(const QJsonArray &requests);
    void accountError(const QString &category, const QString &code, const QString &message);

    // Narrow Account Centre result seams. Failure signals keep `message` first
    // for QML source compatibility while also carrying structured category/code.
    void usernameRenameSucceeded();
    void usernameRenameFailed(
        const QString &message,
        const QString &category,
        const QString &code);
    void builtinAvatarChangeSucceeded();
    void builtinAvatarChangeFailed(
        const QString &message,
        const QString &category,
        const QString &code);
    void recoveryKeyReplacementSucceeded();
    void recoveryKeyReplacementFailed(
        const QString &message,
        const QString &category,
        const QString &code);
    void deviceListRefreshSucceeded();
    void deviceListRefreshFailed(
        const QString &message,
        const QString &category,
        const QString &code);
    void deviceRevokeSucceeded(const QString &deviceId);
    void deviceRevokeFailed(
        const QString &deviceId,
        const QString &message,
        const QString &category,
        const QString &code);

private:
    enum class PendingLogout {
        None,
        Current,
        Everywhere
    };

    quint64 track(quint64 requestId);
    void advanceGeneration();
    bool takeTrackedRequest(quint64 requestId);

    void handleCompleted(
        quint64 requestId,
        AccountOperation operation,
        quint64 accessTokenGeneration,
        const AccountTransportReply &reply);

    bool prepareLocalOnlyProfile();
    bool prepareRememberedProfile(
        const StoredAccountCredential &credential);
    bool prepareProfileForSession(
        const QJsonObject &sessionObject,
        bool accountCreated);
    bool adoptSession(const QJsonObject &sessionObject);
    bool deliverRecoveryKey(
        const QString &recoveryKey,
        AccountRecoveryKeyPurpose purpose);
    void revokeSessionObjectBestEffort(const QJsonObject &sessionObject);

    void handleCreateAccountReply(const AccountTransportReply &reply);
    void handleSignInReply(const AccountTransportReply &reply);
    void handleRefreshReply(const AccountTransportReply &reply);
    void handleDeviceChallengePollReply(const AccountTransportReply &reply);
    void handleTrustedRecoveryPollReply(const AccountTransportReply &reply);

    void setMode(Mode mode);
    void setSyncStateValue(SyncState state);
    void setRestoreStageValue(RestoreStage stage);
    void setUsername(const QString &username);
    void setAvatarId(const QString &avatarId);
    void completeOnboarding();
    void setDeviceCount(int count);
    void setNewDeviceProtectionValue(bool enabled);
    void setPendingOutboxCountValue(int count);

    void setError(
        ErrorCategory category,
        const QString &code,
        const QString &message,
        bool terminal = false);
    void setErrorFromReply(
        const AccountTransportReply &reply,
        bool terminal = false);
    void clearError();

    void clearVolatileSession();
    bool clearStoredSession();
    void beginLogout(PendingLogout logout);
    void continuePendingLogout();
    void clearPendingLogoutWarning();
    void finishLocalSignOut(bool locked);
    void beginAccessTokenRecovery();
    void requestSessionRefresh(bool accessTokenRecovery = false);
    void emitStaleGenerationCompletion(
        AccountOperation operation,
        quint64 requestId,
        const QString &revokedDeviceId);

    void scheduleRefresh(const QString &accessExpiresAt);
    void scheduleChallengePoll();
    void scheduleApprovalPoll(int delayMs = 0);
    void scheduleOfflineRefreshRetry();

    void beginDeviceChallenge(
        const QString &challengeToken,
        const QString &expiresAt);
    void beginTrustedRecoveryChallenge(
        const QString &challengeToken,
        const QString &expiresAt);

    QString deviceInstallId();
    QString deviceLabel() const;
    QString devicePlatform() const;

    static QString modeName(Mode mode);
    static QString syncStateName(SyncState state);
    static QString restoreStageName(RestoreStage stage);
    static QString errorCategoryName(ErrorCategory category);
    static ErrorCategory categoryForReply(const AccountTransportReply &reply);
    static bool isSuccess(const AccountTransportReply &reply);
    static bool operationRequiresActiveSession(AccountOperation operation);

    AccountClient *m_client = nullptr;
    AccountCredentialStore *m_credentialStore = nullptr;
    AccountDeviceIdentity *m_deviceIdentity = nullptr;
    AccountBootstrapStore *m_bootstrapStore = nullptr;
    AccountOneTimeSecretSink *m_oneTimeSecretSink = nullptr;
    AccountProfileCoordinator *m_profileCoordinator = nullptr;
    SyncEngine *m_syncEngine = nullptr;

    Mode m_mode = Mode::SignedOut;
    SyncState m_syncState = SyncState::Inactive;
    RestoreStage m_restoreStage = RestoreStage::None;
    ErrorCategory m_errorCategory = ErrorCategory::None;

    QString m_username;
    QString m_avatarId;
    bool m_onboardingRequired = true;
    QString m_accountId;
    QString m_deviceId;
    QByteArray m_refreshToken;

    int m_deviceCount = 0;
    QJsonArray m_devices;
    bool m_newDeviceProtection = false;
    int m_pendingOutboxCount = 0;
    bool m_signOutSyncWarningPending = false;
    PendingLogout m_pendingLogout = PendingLogout::None;
    QDateTime m_deletionEffectiveAt;

    QString m_lastErrorCode;
    QString m_lastErrorMessage;

    QByteArray m_pendingDeviceChallenge;
    QByteArray m_pendingTrustedRecoveryChallenge;
    bool m_automaticPollingEnabled = true;

    QTimer m_refreshTimer;
    QTimer m_challengeTimer;
    QTimer m_approvalTimer;
    quint64 m_refreshRequestId = 0;
    bool m_accessTokenRecoveryInFlight = false;

    quint64 m_generation = 1;
    QHash<quint64, quint64> m_requestGenerations;
    QHash<quint64, QByteArray> m_pendingRevocationRequests;
    QHash<quint64, QString> m_revokeDeviceRequests;
    QHash<quint64, QString> m_revokeRefreshRequests;
};
