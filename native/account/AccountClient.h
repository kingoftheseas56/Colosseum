#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountTransport.h"

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaType>
#include <QObject>
#include <QString>

enum class AccountOperation {
    CreateAccount,
    SignIn,
    RefreshSession,
    RevokeRefreshToken,
    LogoutCurrent,
    LogoutEverywhere,
    RecoverPassword,
    StartTrustedRecovery,
    PollTrustedRecovery,
    PollDeviceChallenge,
    RecoverDeviceChallengeWithKey,
    ChangePassword,
    ReplaceRecoveryKey,
    GetProfile,
    RenameUsername,
    SetBuiltinAvatar,
    ListDevices,
    RevokeDevice,
    SetNewDeviceProtection,
    ListApprovals,
    DecideApproval,
    SyncPush,
    SyncPull,
    BeginProfileAttachment,
    GetProfileAttachment,
    CommitProfileAttachment,
    SyncSnapshot
};

Q_DECLARE_METATYPE(AccountOperation)

class AccountClient final : public QObject {
    Q_OBJECT

public:
    explicit AccountClient(
        AccountTransport *transport,
        QObject *parent = nullptr);
    ~AccountClient() override;

    void setAccessToken(const QByteArray &accessToken);
    QByteArray accessToken() const;
    quint64 accessTokenGeneration() const;
    void clearAccessToken();

    quint64 createAccount(
        const QString &username,
        const QString &password,
        const QString &deviceInstallId,
        const QString &deviceLabel,
        const QString &platform);

    quint64 signIn(
        const QString &username,
        const QString &password,
        const QString &deviceInstallId,
        const QString &deviceLabel,
        const QString &platform);

    quint64 refreshSession(const QByteArray &refreshToken);
    quint64 revokeRefreshToken(const QByteArray &refreshToken);

    quint64 logoutCurrent();
    quint64 logoutEverywhere();

    quint64 recoverPassword(
        const QString &username,
        const QString &recoveryKey,
        const QString &newPassword);

    quint64 startTrustedRecovery(
        const QString &username,
        const QString &newPassword,
        const QString &deviceInstallId,
        const QString &deviceLabel,
        const QString &platform);

    quint64 pollTrustedRecovery(const QByteArray &challengeToken);
    quint64 pollDeviceChallenge(const QByteArray &challengeToken);

    quint64 recoverDeviceChallengeWithKey(
        const QByteArray &challengeToken,
        const QString &recoveryKey);

    quint64 changePassword(
        const QString &currentPassword,
        const QString &newPassword);

    quint64 replaceRecoveryKey(const QString &currentPassword);

    quint64 getProfile();
    quint64 renameUsername(const QString &username);
    quint64 setBuiltinAvatar(const QString &avatarId);

    quint64 listDevices();
    quint64 revokeDevice(const QString &deviceId);
    quint64 setNewDeviceProtection(bool enabled);

    quint64 listApprovals(int waitSeconds = 0);
    quint64 decideApproval(
        const QString &kind,
        const QString &challengeId,
        bool approve);

    quint64 pushSync(
        const QJsonArray &mutations,
        const QString &attachmentId = QString());

    quint64 pullSync(
        quint64 afterServerSeq);

    quint64 beginProfileAttachment(
        const QString &attachmentId,
        const QString &sourceKind,
        const QString &sourceSemanticDigest);

    quint64 getProfileAttachment(
        const QString &attachmentId);

    quint64 commitProfileAttachment(
        const QString &attachmentId);

    quint64 pullSyncSnapshot(
        const QString &nextPageToken = QString());

signals:
    void completed(
        quint64 requestId,
        AccountOperation operation,
        quint64 accessTokenGeneration,
        const AccountTransportReply &reply);

private:
    struct PendingRequest {
        AccountOperation operation = AccountOperation::CreateAccount;
        quint64 accessTokenGeneration = 0;
    };

    quint64 send(
        AccountOperation operation,
        const QByteArray &method,
        const QString &path,
        const QJsonObject &body,
        bool authenticated,
        int timeoutMs = 15000);

    static QString encodedPathSegment(const QString &value);

    AccountTransport *m_transport = nullptr;
    QByteArray m_accessToken;
    quint64 m_accessTokenGeneration = 1;
    quint64 m_nextRequestId = 1;
    QHash<quint64, PendingRequest> m_pending;
};
