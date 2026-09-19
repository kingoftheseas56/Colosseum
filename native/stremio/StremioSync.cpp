#include "StremioSync.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QUuid>

#include <utility>

namespace {
constexpr int kMaximumRetries = 5;
constexpr int kAuthTimeoutMs = 5 * 60 * 1000;
constexpr qsizetype kMaximumCallbackBytes = 16 * 1024;

QByteArray successPage() {
    return QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: 92\r\nConnection: close\r\n\r\n<!doctype html><title>Colosseum</title><p>Sign-in complete. You can return to Colosseum.</p>");
}

QByteArray failurePage() {
    return QByteArrayLiteral("HTTP/1.1 400 Bad Request\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: 89\r\nConnection: close\r\n\r\n<!doctype html><title>Colosseum</title><p>Sign-in could not be completed. Return to Colosseum.</p>");
}
}

StremioSync::StremioSync(const StremioSyncOptions &options, QObject *parent)
    : QObject(parent),
      m_options(options),
      m_stateStore(this) {
    setObjectName(QStringLiteral("stremioSyncState"));
    m_authTimeout.setSingleShot(true);
    m_retryTimer.setSingleShot(true);
    if (!m_options.clock)
        m_options.clock = [] { return QDateTime::currentMSecsSinceEpoch(); };
    connect(&m_callbackServer, &QTcpServer::newConnection,
            this, &StremioSync::handleIncomingConnection);
    connect(&m_authTimeout, &QTimer::timeout, this, [this] {
        cancelAuthentication();
        setStatus(QStringLiteral("notConnected"));
        finishRun();
    });
    connect(&m_retryTimer, &QTimer::timeout, this, &StremioSync::retryPendingNow);
    connect(&m_stateStore, &StremioState::persistenceCommitted,
            this, [this](quint64 generation) {
                const QList<std::function<void(bool)>> continuations =
                    m_persistContinuations.take(generation);
                for (const auto &continuation : continuations)
                    continuation(true);
            });
    connect(&m_stateStore, &StremioState::persistenceFailed,
            this, [this](quint64 generation, const QString &) {
                const QList<std::function<void(bool)>> continuations =
                    m_persistContinuations.take(generation);
                for (const auto &continuation : continuations)
                    continuation(false);
                setStatus(QStringLiteral("paused"));
            });
}

QString StremioSync::status() const { return m_status; }
int StremioSync::pendingCount() const { return m_state.pendingIntents.size(); }
qint64 StremioSync::lastSuccessAt() const { return m_state.lastSuccessAtMs; }
QString StremioSync::activeProfileId() const { return m_profileId; }
bool StremioSync::mergeComplete() const { return m_state.firstMergeComplete; }
quint64 StremioSync::completedRun() const { return m_completedRun; }

bool StremioSync::activateProfile(
    const QString &profileId,
    const QString &statePath,
    bool sealed,
    QString *error) {
    deactivateProfile();
    ++m_bindingGeneration;
    if (sealed || profileId.trimmed().isEmpty() || statePath.trimmed().isEmpty()) {
        setStatus(QStringLiteral("unavailable"));
        return true;
    }
    QString loadError;
    const auto loaded = m_stateStore.load(statePath, &loadError);
    if (!loaded.has_value()) {
        if (error)
            *error = loadError;
        setStatus(QStringLiteral("paused"));
        return false;
    }
    if (!loaded->profileId.isEmpty() && loaded->profileId != profileId) {
        if (error)
            *error = QStringLiteral("The Stremio state belongs to another profile.");
        setStatus(QStringLiteral("paused"));
        return false;
    }
    m_profileId = profileId;
    m_statePath = statePath;
    m_state = *loaded;
    m_state.profileId = profileId;
    m_state.bindingGeneration = m_bindingGeneration;
    m_hasUsableCredential = false;
    if (!m_state.accountId.isEmpty() && m_options.loadCredential) {
        const auto credential = m_options.loadCredential(m_profileId, m_state.accountId);
        m_hasUsableCredential = credential.has_value() && !credential->isEmpty();
    }
    setStatus(m_state.accountId.isEmpty() ? QStringLiteral("notConnected") : QStringLiteral("reconnectRequired"));
    emit stateChanged();
    return true;
}

void StremioSync::deactivateProfile() {
    cancelAuthentication();
    m_retryTimer.stop();
    ++m_bindingGeneration;
    m_profileId.clear();
    m_statePath.clear();
    m_state = {};
    m_hasUsableCredential = false;
    emit stateChanged();
}

bool StremioSync::startBrowserAuthentication(QString *error) {
    if (m_profileId.isEmpty() || m_statePath.isEmpty()) {
        if (error)
            *error = QStringLiteral("A Stremio profile is not active.");
        return false;
    }
    if (!endpointAllowed()) {
        if (error)
            *error = QStringLiteral("The Stremio endpoint is not permitted.");
        return false;
    }
    cancelAuthentication();
    QByteArray nonce(32, '\0');
    for (char &byte : nonce)
        byte = static_cast<char>(QRandomGenerator::system()->generate() & 0xff);
    m_callbackPath = QStringLiteral("/stremio/")
        + QString::fromLatin1(nonce.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    if (!m_callbackServer.listen(QHostAddress::LocalHost, 0)) {
        if (error)
            *error = QStringLiteral("The Stremio callback listener could not start.");
        return false;
    }
    ++m_authAttempt;
    const QUrl callback(QStringLiteral("http://127.0.0.1:%1%2")
        .arg(m_callbackServer.serverPort()).arg(m_callbackPath));
    const QUrl login = StremioCodec::browserLoginUrl(callback);
    setStatus(QStringLiteral("connecting"));
    m_authTimeout.start(kAuthTimeoutMs);
    emit browserLoginRequested(login);
    if (m_options.browserOpener)
        m_options.browserOpener(login);
    return true;
}

void StremioSync::cancelAuthentication() {
    ++m_authAttempt;
    m_authTimeout.stop();
    m_callbackServer.close();
    if (m_callbackSocket)
        m_callbackSocket->disconnectFromHost();
    m_callbackSocket = nullptr;
    m_callbackBuffer.clear();
    m_callbackPath.clear();
    if (m_identityReply) {
        m_identityReply->abort();
        m_identityReply = nullptr;
    }
}

void StremioSync::setCredentialCallbacks(
    std::function<bool(const QString &, const QString &, const QByteArray &)> save,
    std::function<bool(const QString &)> clear,
    std::function<std::optional<QByteArray>(const QString &, const QString &)> load) {
    m_options.saveCredential = std::move(save);
    m_options.clearCredential = std::move(clear);
    m_options.loadCredential = std::move(load);
}

bool StremioSync::queueIntent(
    const QString &kind,
    const QJsonObject &desired,
    QString *operationId) {
    if (m_profileId.isEmpty() || kind.trimmed().isEmpty() || desired.isEmpty())
        return false;
    StremioPendingIntent intent;
    intent.operationId = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
    intent.kind = kind.trimmed();
    intent.desired = desired;
    m_state.pendingIntents.append(intent);
    if (operationId)
        *operationId = intent.operationId;
    const ProfileBinding binding{m_profileId, m_bindingGeneration};
    persist([this, operation = intent.operationId, binding](bool committed) {
        if (committed)
            dispatchIntent(operation, binding);
    });
    emit stateChanged();
    return true;
}

bool StremioSync::acknowledgeLocalReceipt(const QString &operationId) {
    StremioPendingIntent *intent = intentFor(operationId);
    if (!intent)
        return false;
    intent->localReceiptDurable = true;
    persist([this, operationId](bool committed) {
        if (committed)
            removeSatisfiedIntent(operationId);
    });
    emit stateChanged();
    return true;
}

void StremioSync::retryPendingNow() {
    const ProfileBinding binding{m_profileId, m_bindingGeneration};
    if (!bindingCurrent(binding))
        return;
    const qint64 now = m_options.clock();
    for (const StremioPendingIntent &intent : std::as_const(m_state.pendingIntents)) {
        if (!intent.remoteAcknowledged && intent.attempts < kMaximumRetries && intent.retryAtMs <= now)
            dispatchIntent(intent.operationId, binding);
    }
}

bool StremioSync::fixtureEndpointAllowed() const {
    return m_options.allowTaggedLoopbackFixture
        && StremioCodec::isTaggedLoopbackEndpoint(m_options.apiEndpoint);
}

bool StremioSync::endpointAllowed() const {
    return StremioCodec::isProductionEndpoint(m_options.apiEndpoint) || fixtureEndpointAllowed();
}

void StremioSync::handleIncomingConnection() {
    while (m_callbackServer.hasPendingConnections()) {
        QTcpSocket *socket = m_callbackServer.nextPendingConnection();
        if (!socket)
            return;
        if (m_callbackSocket) {
            socket->write(failurePage());
            socket->disconnectFromHost();
            socket->deleteLater();
            continue;
        }
        m_callbackSocket = socket;
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { handleCallbackSocket(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void StremioSync::handleCallbackSocket(QTcpSocket *socket) {
    if (!socket || socket != m_callbackSocket)
        return;
    m_callbackBuffer += socket->readAll();
    if (m_callbackBuffer.size() > kMaximumCallbackBytes) {
        socket->write(failurePage());
        socket->disconnectFromHost();
        m_callbackSocket = nullptr;
        return;
    }
    if (!m_callbackBuffer.contains("\r\n\r\n"))
        return;
    const StremioLoopbackCallback callback = StremioCodec::decodeLoopbackCallback(m_callbackBuffer, m_callbackPath);
    socket->write(callback.accepted ? successPage() : failurePage());
    socket->disconnectFromHost();
    m_callbackSocket = nullptr;
    m_callbackBuffer.clear();
    if (!callback.accepted)
        return;
    const ProfileBinding binding{m_profileId, m_bindingGeneration};
    const quint64 attempt = m_authAttempt;
    m_callbackServer.close();
    validateAuthKey(callback.authKey, binding, attempt);
}

void StremioSync::validateAuthKey(
    const QByteArray &authKey,
    const ProfileBinding &binding,
    quint64 attempt) {
    if (!bindingCurrent(binding) || attempt != m_authAttempt)
        return;
    QUrl endpoint = m_options.apiEndpoint;
    QString path = endpoint.path();
    if (!path.endsWith(QLatin1Char('/')))
        path += QLatin1Char('/');
    endpoint.setPath(path + QStringLiteral("getUser"));
    QNetworkRequest request(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QJsonObject body;
    body.insert(QStringLiteral("authKey"), QString::fromUtf8(authKey));
    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_identityReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, binding, attempt, authKey] {
        const bool current = bindingCurrent(binding) && attempt == m_authAttempt && reply == m_identityReply;
        if (!current) {
            reply->deleteLater();
            return;
        }
        const QNetworkReply::NetworkError networkError = reply->error();
        QJsonParseError parseError;
        const QJsonDocument response = QJsonDocument::fromJson(reply->readAll(), &parseError);
        reply->deleteLater();
        m_identityReply = nullptr;
        StremioAccountIdentity identity;
        QString error;
        if (networkError != QNetworkReply::NoError || parseError.error != QJsonParseError::NoError
            || !response.isObject() || !StremioCodec::decodeGetUserResult(response.object(), &identity, &error)) {
            cancelAuthentication();
            setStatus(QStringLiteral("notConnected"));
            finishRun();
            return;
        }
        if (!m_options.saveCredential
            || !m_options.saveCredential(binding.profileId, identity.accountId, authKey)) {
            if (bindingCurrent(binding)) {
                if (m_options.clearCredential)
                    m_options.clearCredential(binding.profileId);
                setStatus(QStringLiteral("notConnected"));
                finishRun();
            }
            return;
        }
        m_hasUsableCredential = true;
        m_state.accountId = identity.accountId;
        m_state.displayName = identity.displayName;
        m_state.bindingGeneration = binding.generation;
        persist([this, binding](bool committed) {
            if (!committed || !bindingCurrent(binding)) {
                if (bindingCurrent(binding)) {
                    if (m_options.clearCredential)
                        m_options.clearCredential(binding.profileId);
                    m_hasUsableCredential = false;
                    setStatus(QStringLiteral("notConnected"));
                    finishRun();
                }
                return;
            }
            m_authTimeout.stop();
            setStatus(QStringLiteral("synced"));
            finishRun();
        });
    });
}

void StremioSync::persist(std::function<void(bool)> continuation) {
    if (m_statePath.isEmpty()) {
        if (continuation)
            continuation(false);
        return;
    }
    const quint64 generation = m_stateStore.saveAsync(m_statePath, m_state);
    if (continuation)
        m_persistContinuations[generation].append(std::move(continuation));
}

void StremioSync::dispatchIntent(const QString &operationId, const ProfileBinding &binding) {
    if (!bindingCurrent(binding) || !m_hasUsableCredential || !m_options.intentSender)
        return;
    StremioPendingIntent *intent = intentFor(operationId);
    if (!intent || intent->remoteAcknowledged || intent->attempts >= kMaximumRetries)
        return;
    const StremioPendingIntent copy = *intent;
    m_options.intentSender(copy, [this, operationId, binding](bool accepted, bool authenticationFailure) {
        handleIntentResult(operationId, binding, accepted, authenticationFailure);
    });
}

void StremioSync::handleIntentResult(
    const QString &operationId,
    const ProfileBinding &binding,
    bool accepted,
    bool authenticationFailure) {
    if (!bindingCurrent(binding))
        return;
    StremioPendingIntent *intent = intentFor(operationId);
    if (!intent)
        return;
    if (accepted) {
        intent->remoteAcknowledged = true;
        m_state.lastSuccessAtMs = m_options.clock();
        persist([this, operationId](bool committed) {
            if (committed)
                removeSatisfiedIntent(operationId);
        });
        emit stateChanged();
        return;
    }
    if (authenticationFailure) {
        setStatus(QStringLiteral("reconnectRequired"));
        return;
    }
    ++intent->attempts;
    if (intent->attempts >= kMaximumRetries) {
        setStatus(QStringLiteral("syncFailed"));
        persist();
        return;
    }
    const qint64 delay = qMin<qint64>(60 * 1000, 1000LL << qMin(intent->attempts, 5));
    intent->retryAtMs = m_options.clock() + delay;
    persist();
    m_retryTimer.start(static_cast<int>(delay));
    emit stateChanged();
}

void StremioSync::removeSatisfiedIntent(const QString &operationId) {
    StremioPendingIntent *intent = intentFor(operationId);
    if (!intent || !intent->remoteAcknowledged || !intent->localReceiptDurable)
        return;
    for (qsizetype index = 0; index < m_state.pendingIntents.size(); ++index) {
        if (m_state.pendingIntents.at(index).operationId == operationId) {
            m_state.pendingIntents.removeAt(index);
            persist();
            emit stateChanged();
            return;
        }
    }
}

bool StremioSync::bindingCurrent(const ProfileBinding &binding) const {
    return !binding.profileId.isEmpty()
        && binding.profileId == m_profileId
        && binding.generation == m_bindingGeneration;
}

StremioPendingIntent *StremioSync::intentFor(const QString &operationId) {
    for (StremioPendingIntent &intent : m_state.pendingIntents) {
        if (intent.operationId == operationId)
            return &intent;
    }
    return nullptr;
}

void StremioSync::setStatus(const QString &statusValue) {
    if (m_status == statusValue)
        return;
    m_status = statusValue;
    emit stateChanged();
}

void StremioSync::finishRun() {
    ++m_completedRun;
    emit stateChanged();
}
