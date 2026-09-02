// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountClient.h"

#include <QTimer>
#include <QUrl>

AccountClient::AccountClient(
    AccountTransport *transport,
    QObject *parent)
    : QObject(parent),
      m_transport(transport) {
    Q_ASSERT(m_transport);

    qRegisterMetaType<AccountOperation>();
    connect(
        m_transport,
        &AccountTransport::finished,
        this,
        [this](quint64 requestId, const AccountTransportReply &reply) {
            const auto it = m_pending.find(requestId);
            if (it == m_pending.end())
                return;

            const PendingRequest pending = it.value();
            m_pending.erase(it);

            QTimer::singleShot(
                0,
                this,
                [this, requestId, pending, reply]() {
                    emit completed(
                        requestId,
                        pending.operation,
                        pending.accessTokenGeneration,
                        reply);
                });
        });
}

AccountClient::~AccountClient() {
    clearAccessToken();
}

void AccountClient::setAccessToken(const QByteArray &accessToken) {
    if (!m_accessToken.isEmpty())
        m_accessToken.fill('\0');
    m_accessToken = accessToken;
    ++m_accessTokenGeneration;
    if (m_accessTokenGeneration == 0)
        ++m_accessTokenGeneration;
}

QByteArray AccountClient::accessToken() const {
    return m_accessToken;
}

quint64 AccountClient::accessTokenGeneration() const {
    return m_accessTokenGeneration;
}

void AccountClient::clearAccessToken() {
    if (m_accessToken.isEmpty())
        return;

    m_accessToken.fill('\0');
    m_accessToken.clear();
    m_accessToken.squeeze();
    ++m_accessTokenGeneration;
    if (m_accessTokenGeneration == 0)
        ++m_accessTokenGeneration;
}

quint64 AccountClient::createAccount(
    const QString &username,
    const QString &password,
    const QString &deviceInstallId,
    const QString &deviceLabel,
    const QString &platform) {
    QJsonObject body;
    body.insert(QStringLiteral("username"), username);
    body.insert(QStringLiteral("password"), password);
    body.insert(QStringLiteral("device_install_id"), deviceInstallId);
    body.insert(QStringLiteral("device_label"), deviceLabel);
    body.insert(QStringLiteral("platform"), platform);
    return send(
        AccountOperation::CreateAccount,
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/accounts"),
        body,
        false);
}

quint64 AccountClient::signIn(
    const QString &username,
    const QString &password,
    const QString &deviceInstallId,
    const QString &deviceLabel,
    const QString &platform) {
    QJsonObject body;
    body.insert(QStringLiteral("username"), username);
    body.insert(QStringLiteral("password"), password);
    body.insert(QStringLiteral("device_install_id"), deviceInstallId);
    body.insert(QStringLiteral("device_label"), deviceLabel);
    body.insert(QStringLiteral("platform"), platform);
    return send(
        AccountOperation::SignIn,
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions"),
        body,
        false);
}

quint64 AccountClient::refreshSession(const QByteArray &refreshToken) {
    QJsonObject body;
    body.insert(
        QStringLiteral("refresh_token"),
        QString::fromLatin1(refreshToken));
    return send(
        AccountOperation::RefreshSession,
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions/refresh"),
        body,
        false);
}

quint64 AccountClient::revokeRefreshToken(const QByteArray &refreshToken) {
    QJsonObject body;
    body.insert(
        QStringLiteral("refresh_token"),
        QString::fromLatin1(refreshToken));
    return send(
        AccountOperation::RevokeRefreshToken,
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions/revoke-refresh"),
        body,
        false);
}

quint64 AccountClient::logoutCurrent() {
    return send(
        AccountOperation::LogoutCurrent,
        QByteArrayLiteral("DELETE"),
        QStringLiteral("/v1/sessions/current"),
        QJsonObject(),
        true);
}

quint64 AccountClient::logoutEverywhere() {
    return send(
        AccountOperation::LogoutEverywhere,
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sessions/logout-all"),
        QJsonObject(),
        true);
}

quint64 AccountClient::recoverPassword(
    const QString &username,
    const QString &recoveryKey,
    const QString &newPassword) {
    QJsonObject body;
    body.insert(QStringLiteral("username"), username);
    body.insert(QStringLiteral("recovery_key"), recoveryKey);
    body.insert(QStringLiteral("new_password"), newPassword);
    return send(
        AccountOperation::RecoverPassword,
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/password/recover"),
        body,
        false);
}

quint64 AccountClient::startTrustedRecovery(
    const QString &username,
    const QString &newPassword,
    const QString &deviceInstallId,
    const QString &deviceLabel,
    const QString &platform) {
    QJsonObject body;
    body.insert(QStringLiteral("username"), username);
    body.insert(QStringLiteral("new_password"), newPassword);
    body.insert(QStringLiteral("device_install_id"), deviceInstallId);
    body.insert(QStringLiteral("device_label"), deviceLabel);
    body.insert(QStringLiteral("platform"), platform);
    return send(
        AccountOperation::StartTrustedRecovery,
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/password/trusted-recovery"),
        body,
        false);
}

quint64 AccountClient::pollTrustedRecovery(const QByteArray &challengeToken) {
    QJsonObject body;
    body.insert(
        QStringLiteral("challenge_token"),
        QString::fromLatin1(challengeToken));
    return send(
        AccountOperation::PollTrustedRecovery,
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/password/trusted-recovery/poll"),
        body,
        false);
}

quint64 AccountClient::pollDeviceChallenge(const QByteArray &challengeToken) {
    QJsonObject body;
    body.insert(
        QStringLiteral("challenge_token"),
        QString::fromLatin1(challengeToken));
    return send(
        AccountOperation::PollDeviceChallenge,
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/challenges/device/poll"),
        body,
        false);
}

quint64 AccountClient::recoverDeviceChallengeWithKey(
    const QByteArray &challengeToken,
    const QString &recoveryKey) {
    QJsonObject body;
    body.insert(
        QStringLiteral("challenge_token"),
        QString::fromLatin1(challengeToken));
    body.insert(QStringLiteral("recovery_key"), recoveryKey);
    return send(
        AccountOperation::RecoverDeviceChallengeWithKey,
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/challenges/device/recovery-key"),
        body,
        false);
}

quint64 AccountClient::changePassword(
    const QString &currentPassword,
    const QString &newPassword) {
    QJsonObject body;
    body.insert(QStringLiteral("current_password"), currentPassword);
    body.insert(QStringLiteral("new_password"), newPassword);
    return send(
        AccountOperation::ChangePassword,
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/password/change"),
        body,
        true);
}

quint64 AccountClient::replaceRecoveryKey(const QString &currentPassword) {
    QJsonObject body;
    body.insert(QStringLiteral("current_password"), currentPassword);
    return send(
        AccountOperation::ReplaceRecoveryKey,
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/recovery-key/replace"),
        body,
        true);
}

quint64 AccountClient::getProfile() {
    return send(
        AccountOperation::GetProfile,
        QByteArrayLiteral("GET"),
        QStringLiteral("/v1/profile"),
        QJsonObject(),
        true);
}

quint64 AccountClient::renameUsername(const QString &username) {
    QJsonObject body;
    body.insert(QStringLiteral("username"), username);
    return send(
        AccountOperation::RenameUsername,
        QByteArrayLiteral("PATCH"),
        QStringLiteral("/v1/profile/username"),
        body,
        true);
}

quint64 AccountClient::setBuiltinAvatar(const QString &avatarId) {
    QJsonObject body;
    body.insert(QStringLiteral("avatar_id"), avatarId);
    return send(
        AccountOperation::SetBuiltinAvatar,
        QByteArrayLiteral("PUT"),
        QStringLiteral("/v1/profile/avatar/builtin"),
        body,
        true);
}

quint64 AccountClient::listDevices() {
    return send(
        AccountOperation::ListDevices,
        QByteArrayLiteral("GET"),
        QStringLiteral("/v1/devices"),
        QJsonObject(),
        true);
}

quint64 AccountClient::revokeDevice(const QString &deviceId) {
    return send(
        AccountOperation::RevokeDevice,
        QByteArrayLiteral("DELETE"),
        QStringLiteral("/v1/devices/")
            + encodedPathSegment(deviceId),
        QJsonObject(),
        true);
}

quint64 AccountClient::setNewDeviceProtection(bool enabled) {
    QJsonObject body;
    body.insert(QStringLiteral("enabled"), enabled);
    return send(
        AccountOperation::SetNewDeviceProtection,
        QByteArrayLiteral("PUT"),
        QStringLiteral("/v1/security/new-device-protection"),
        body,
        true);
}

quint64 AccountClient::listApprovals(int waitSeconds) {
    waitSeconds = qBound(0, waitSeconds, 25);
    const int timeoutMs = qBound(
        10000,
        waitSeconds * 1000 + 10000,
        35000);
    QString path = QStringLiteral("/v1/approvals");
    if (waitSeconds > 0) {
        path += QStringLiteral("?wait_seconds=")
            + QString::number(waitSeconds);
    }
    return send(
        AccountOperation::ListApprovals,
        QByteArrayLiteral("GET"),
        path,
        QJsonObject(),
        true,
        timeoutMs);
}

quint64 AccountClient::decideApproval(
    const QString &kind,
    const QString &challengeId,
    bool approve) {
    QJsonObject body;
    body.insert(
        QStringLiteral("decision"),
        approve
            ? QStringLiteral("approve")
            : QStringLiteral("deny"));
    return send(
        AccountOperation::DecideApproval,
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/approvals/")
            + encodedPathSegment(kind)
            + QLatin1Char('/')
            + encodedPathSegment(challengeId),
        body,
        true);
}

quint64 AccountClient::pushSync(
    const QJsonArray &mutations,
    const QString &attachmentId) {
    QJsonObject body;
    body.insert(
        QStringLiteral("mutations"),
        mutations);
    // Ordinary pushes omit the envelope-level attachment tag entirely.
    if (!attachmentId.isEmpty())
        body.insert(
            QStringLiteral(
                "attachment_id"),
            attachmentId);
    return send(
        AccountOperation::SyncPush,
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/sync/push"),
        body,
        true);
}

quint64 AccountClient::pullSync(
    quint64 afterServerSeq) {
    return send(
        AccountOperation::SyncPull,
        QByteArrayLiteral("GET"),
        QStringLiteral("/v1/sync/pull?after=")
            + QString::number(afterServerSeq),
        QJsonObject(),
        true);
}

quint64 AccountClient::beginProfileAttachment(
    const QString &attachmentId,
    const QString &sourceKind,
    const QString &sourceSemanticDigest) {
    QJsonObject body;
    body.insert(
        QStringLiteral(
            "attachment_id"),
        attachmentId);
    body.insert(
        QStringLiteral("source_kind"),
        sourceKind);
    body.insert(
        QStringLiteral(
            "source_semantic_digest"),
        sourceSemanticDigest);
    return send(
        AccountOperation::BeginProfileAttachment,
        QByteArrayLiteral("POST"),
        QStringLiteral("/v1/profile/attachments"),
        body,
        true);
}

quint64 AccountClient::getProfileAttachment(
    const QString &attachmentId) {
    return send(
        AccountOperation::GetProfileAttachment,
        QByteArrayLiteral("GET"),
        QStringLiteral(
            "/v1/profile/attachments/")
            + encodedPathSegment(attachmentId),
        QJsonObject(),
        true);
}

quint64 AccountClient::commitProfileAttachment(
    const QString &attachmentId) {
    return send(
        AccountOperation::CommitProfileAttachment,
        QByteArrayLiteral("POST"),
        QStringLiteral(
            "/v1/profile/attachments/")
            + encodedPathSegment(attachmentId)
            + QStringLiteral("/commit"),
        QJsonObject(),
        true);
}

quint64 AccountClient::pullSyncSnapshot(
    const QString &nextPageToken) {
    QString path =
        QStringLiteral("/v1/sync/snapshot");
    // The continuation key rides the query string only when a page
    // token exists; the first page carries no query at all.
    if (!nextPageToken.isEmpty())
        path += QStringLiteral("?after_key=")
            + encodedPathSegment(nextPageToken);
    return send(
        AccountOperation::SyncSnapshot,
        QByteArrayLiteral("GET"),
        path,
        QJsonObject(),
        true);
}

quint64 AccountClient::send(
    AccountOperation operation,
    const QByteArray &method,
    const QString &path,
    const QJsonObject &body,
    bool authenticated,
    int timeoutMs) {
    const quint64 requestId = m_nextRequestId++;

    AccountTransportRequest request;
    request.method = method;
    request.path = path;
    request.body = body;
    request.timeoutMs = qMax(1, timeoutMs);
    if (authenticated)
        request.bearerToken = m_accessToken;

    PendingRequest pending;
    pending.operation = operation;
    pending.accessTokenGeneration =
        authenticated ? m_accessTokenGeneration : 0;
    m_pending.insert(requestId, pending);
    m_transport->send(requestId, request);
    return requestId;
}

QString AccountClient::encodedPathSegment(const QString &value) {
    return QString::fromLatin1(
        QUrl::toPercentEncoding(value.trimmed()));
}
