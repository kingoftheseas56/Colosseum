#include "TraktApiClient.h"

#include "TraktCodec.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

#include <limits>
#include <cmath>
#include <utility>

namespace {
constexpr qint64 kMaximumBodyBytes = 16LL * 1024LL * 1024LL;
constexpr qint64 kRefreshLeewayMs = 5LL * 60LL * 1000LL;

qint64 retryAfterMs(const QNetworkReply *reply)
{
    bool ok = false;
    const qint64 seconds = reply->rawHeader("Retry-After").trimmed().toLongLong(&ok);
    return ok && seconds > 0 && seconds <= 86400 ? seconds * 1000 : 0;
}
}

TraktApiClient::TraktApiClient(const ProfilePaths &profile, TraktAuthConfiguration configuration, QObject *parent)
    : TraktApiClient(profile, std::move(configuration), nullptr, nullptr, nullptr, parent)
{}

TraktApiClient::TraktApiClient(const ProfilePaths &profile, TraktAuthConfiguration configuration,
                             TrackerCredentialVault *vault, TraktApiTransport *transport,
                             TrackerClock *clock, QObject *parent)
    : QObject(parent), m_profile(profile), m_configuration(std::move(configuration)),
      m_defaultVault(vault ? nullptr : std::make_unique<WindowsTrackerCredentialVault>()),
      m_vault(vault ? vault : m_defaultVault.get()), m_transport(transport), m_clock(clock)
{}

qint64 TraktApiClient::nowMs() const
{
    return m_clock ? m_clock->nowMs() : QDateTime::currentMSecsSinceEpoch();
}

bool TraktApiClient::available() const
{
    return m_active && !m_credentialFailure
        && (m_profile.kind() == ProfilePaths::Kind::LocalOnly || m_profile.kind() == ProfilePaths::Kind::Account)
        && !m_profile.profileId().isEmpty() && traktConfigurationIsValid(m_configuration)
        && m_vault && m_vault->isAvailable();
}

bool TraktApiClient::credentialAccountMatches(const QString &remoteAccountId) const
{
    if (!m_vault || !traktAccountUuidIsCanonical(remoteAccountId)) return false;
    const auto credential = m_vault->loadForProfile(m_profile.profileId(), TrackerProviderId::Trakt);
    return credential && credential->slot.profileId == m_profile.profileId()
        && credential->slot.providerId == TrackerProviderId::Trakt
        && credential->slot.remoteAccountId == remoteAccountId;
}

void TraktApiClient::deactivate()
{
    m_active = false;
    resetForConnection();
}

void TraktApiClient::resetForConnection()
{
    ++m_generation;
    m_credentialFailure = false;
    m_refreshing = false;
    const auto waiters = std::exchange(m_refreshWaiters, QList<CredentialCompletion>{});
    const auto pending = std::exchange(m_pendingRequests, QHash<quint64, Completion>{});
    TraktApiResult cancelled;
    cancelled.cancelled = true;
    cancelled.networkFailure = true; // A dispatched write may already have applied.
    const QPointer<TraktApiClient> guard(this);
    for (const auto &completion : pending) {
        if (!guard) return;
        completion(cancelled);
    }
    for (const auto &waiter : waiters) {
        if (!guard) return;
        waiter(std::nullopt);
    }
}

QUrl TraktApiClient::apiEndpoint(const QString &path, const QUrlQuery &query) const
{
    QUrl url(QStringLiteral("https://api.trakt.tv") + path);
    url.setQuery(query);
    return url;
}

QUrl TraktApiClient::brokerEndpoint(const QString &path) const
{
    QUrl url = m_configuration.brokerBaseUrl;
    QString base = url.path();
    if (base.endsWith(QLatin1Char('/'))) base.chop(1);
    url.setPath(base + path);
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

QNetworkRequest TraktApiClient::apiRequest(const QUrl &url, const TrackerCredential &credential) const
{
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("trakt-api-version", "2");
    request.setRawHeader("trakt-api-key", m_configuration.clientId.toUtf8());
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + credential.accessToken);
    request.setRawHeader("User-Agent", (m_configuration.appName + QLatin1Char('/') + m_configuration.appVersion).toUtf8());
    return request;
}

QNetworkRequest TraktApiClient::brokerRequest(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", (m_configuration.appName + QLatin1Char('/') + m_configuration.appVersion).toUtf8());
    return request;
}

void TraktApiClient::get(const QString &path, const QUrlQuery &query, Completion completion)
{
    request(QByteArrayLiteral("GET"), path, query, {}, false, std::move(completion));
}

void TraktApiClient::post(const QString &path, const QJsonDocument &body, Completion completion)
{
    request(QByteArrayLiteral("POST"), path, {}, body, false, std::move(completion));
}

void TraktApiClient::ensureCredential(bool forceRefresh, CredentialCompletion completion)
{
    if (!available()) { completion(std::nullopt); return; }
    if (m_refreshing) { m_refreshWaiters.append(std::move(completion)); return; }
    const auto credential = m_vault->loadForProfile(m_profile.profileId(), TrackerProviderId::Trakt);
    const qint64 now = nowMs();
    if (!credential || credential->slot.profileId != m_profile.profileId()
        || credential->slot.providerId != TrackerProviderId::Trakt
        || !traktAccountUuidIsCanonical(credential->slot.remoteAccountId)
        || credential->refreshToken.isEmpty() || credential->refreshTokenExpiresAtMs <= now) { completion(std::nullopt); return; }
    if (!forceRefresh && !credential->accessToken.isEmpty() && credential->accessTokenExpiresAtMs > now + kRefreshLeewayMs) {
        completion(credential);
        return;
    }
    m_refreshWaiters.append(std::move(completion));
    if (m_refreshing) return;
    m_refreshing = true;
    refreshCredential(*credential);
}

void TraktApiClient::refreshCredential(const TrackerCredential &credential)
{
    const quint64 generation = m_generation;
    brokerPost(QStringLiteral("/v1/trakt/refresh"),
               {{QStringLiteral("refresh_token"), QString::fromUtf8(credential.refreshToken)}},
               [this, credential, generation](const TraktApiResult &result) {
                   if (m_active && generation == m_generation) finishRefresh(result, credential);
               });
}

void TraktApiClient::finishRefresh(const TraktApiResult &result, const TrackerCredential &previous)
{
    std::optional<TrackerCredential> refreshed;
    const auto next = result.succeeded()
        ? traktRotatedCredential(previous, result.document, nowMs())
        : std::nullopt;
    if (next && m_vault->saveAndVerify(*next)) {
        const auto verified = m_vault->loadForProfile(m_profile.profileId(), TrackerProviderId::Trakt);
        if (verified && verified->accessToken == next->accessToken
            && verified->refreshToken == next->refreshToken
            && verified->slot.profileId == next->slot.profileId
            && verified->slot.providerId == TrackerProviderId::Trakt
            && verified->slot.remoteAccountId == next->slot.remoteAccountId
            && verified->accessTokenExpiresAtMs == next->accessTokenExpiresAtMs
            && verified->refreshTokenExpiresAtMs == next->refreshTokenExpiresAtMs
            && verified->grantedScopes == next->grantedScopes)
            refreshed = *verified;
    }
    if (!refreshed) m_credentialFailure = true;
    m_refreshing = false;
    const QList<CredentialCompletion> waiters = std::exchange(m_refreshWaiters, QList<CredentialCompletion>{});
    for (const CredentialCompletion &waiter : waiters) waiter(refreshed);
}

void TraktApiClient::request(const QByteArray &method, const QString &path, QUrlQuery query,
                             const QJsonDocument &body, bool retriedAfterUnauthorized, Completion completion)
{
    ensureCredential(false, [this, method, path, query = std::move(query), body, retriedAfterUnauthorized, completion = std::move(completion)](std::optional<TrackerCredential> credential) mutable {
        if (!credential) { TraktApiResult result; result.statusCode = 401; completion(result); return; }
        issue(method, path, query, body, *credential, retriedAfterUnauthorized, std::move(completion));
    });
}

void TraktApiClient::issue(const QByteArray &method, const QString &path, const QUrlQuery &query,
                           const QJsonDocument &body, const TrackerCredential &credential,
                           bool retriedAfterUnauthorized, Completion completion)
{
    if (!available() || !path.startsWith(QLatin1Char('/')) || path.startsWith(QStringLiteral("//"))
        || path.contains(QLatin1Char('?')) || path.contains(QLatin1Char('#'))) {
        TraktApiResult failed; failed.statusCode = 401; completion(failed); return;
    }
    dispatch(method, apiRequest(apiEndpoint(path, query), credential), body,
             [this, method, path, query, body, credential, retriedAfterUnauthorized, completion = std::move(completion)](const TraktApiResult &result) mutable {
        if (result.cancelled) { completion(result); return; }
        if (result.statusCode == 401 && !retriedAfterUnauthorized) {
            const auto current = m_vault->loadForProfile(m_profile.profileId(), TrackerProviderId::Trakt);
            if (current && current->accessToken != credential.accessToken && !m_refreshing
                && current->slot.profileId == m_profile.profileId()
                && current->slot.providerId == TrackerProviderId::Trakt
                && current->slot.remoteAccountId == credential.slot.remoteAccountId
                && current->accessTokenExpiresAtMs > nowMs() + kRefreshLeewayMs) {
                issue(method, path, query, body, *current, true, std::move(completion));
                return;
            }
            ensureCredential(true, [this, method, path, query, body, completion = std::move(completion)](std::optional<TrackerCredential> refreshed) mutable {
                if (!refreshed) { TraktApiResult failed; failed.statusCode = 401; completion(failed); return; }
                issue(method, path, query, body, *refreshed, true, std::move(completion));
            });
            return;
        }
        completion(result);
    });
}

void TraktApiClient::brokerPost(const QString &path, const QJsonObject &body, Completion completion)
{
    if (!m_active || !traktConfigurationIsValid(m_configuration)) {
        TraktApiResult failed; failed.statusCode = 401; completion(failed); return;
    }
    dispatch(QByteArrayLiteral("POST"), brokerRequest(brokerEndpoint(path)), QJsonDocument(body), std::move(completion));
}

void TraktApiClient::dispatch(const QByteArray &method, const QNetworkRequest &request,
                              const QJsonDocument &body, Completion completion)
{
    const QPointer<TraktApiClient> guard(this);
    const quint64 requestId = ++m_nextRequestId;
    m_pendingRequests.insert(requestId, std::move(completion));
    Completion fenced = [guard, requestId](const TraktApiResult &result) {
        if (!guard || !guard->m_pendingRequests.contains(requestId)) return;
        auto completion = guard->m_pendingRequests.take(requestId);
        completion(result);
    };
    if (m_transport) {
        m_transport->request(method, request, body, std::move(fenced));
        return;
    }
    QNetworkReply *reply = method == QByteArrayLiteral("GET") ? m_network.get(request)
        : m_network.post(request, body.toJson(QJsonDocument::Compact));
    const auto buffer = std::make_shared<QByteArray>();
    const auto tooLarge = std::make_shared<bool>(false);
    connect(reply, &QIODevice::readyRead, this, [reply, buffer, tooLarge] {
        if (*tooLarge) return;
        buffer->append(reply->readAll());
        if (buffer->size() > kMaximumBodyBytes) { *tooLarge = true; reply->abort(); }
    });
    finishReply(reply, buffer, tooLarge, std::move(fenced));
}

void TraktApiClient::finishReply(QNetworkReply *reply, const std::shared_ptr<QByteArray> &buffer,
                                 const std::shared_ptr<bool> &tooLarge, Completion completion)
{
    connect(reply, &QNetworkReply::finished, this, [reply, buffer, tooLarge, completion = std::move(completion)] {
        QByteArray body = *buffer;
        body += reply->readAll();
        TraktApiResult result;
        result.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.networkFailure = reply->error() != QNetworkReply::NoError
            && (result.statusCode == 0 || (result.statusCode >= 200 && result.statusCode < 300));
        result.payloadTooLarge = *tooLarge || body.size() > kMaximumBodyBytes;
        result.retryAfterMs = retryAfterMs(reply);
        result.page = reply->rawHeader("X-Pagination-Page").toInt();
        result.pageCount = reply->rawHeader("X-Pagination-Page-Count").toInt();
        if (!result.payloadTooLarge && !body.isEmpty()) {
            QJsonParseError error;
            result.document = QJsonDocument::fromJson(body, &error);
            if (error.error != QJsonParseError::NoError) result.document = {};
        }
        reply->deleteLater();
        completion(result);
    });
}

void TraktApiClient::revokeAccessTokenAsync(const QByteArray &accessToken, const std::function<void()> &finished)
{
    if (accessToken.isEmpty()) { if (finished) finished(); return; }
    brokerPost(QStringLiteral("/v1/trakt/revoke"),
               {{QStringLiteral("token"), QString::fromUtf8(accessToken)}},
               [finished](const TraktApiResult &) { if (finished) finished(); });
}

void TraktApiClient::revokeGrantAsync(const std::function<void()> &finished)
{
    const auto credential = m_vault->loadForProfile(m_profile.profileId(), TrackerProviderId::Trakt);
    revokeAccessTokenAsync(credential ? credential->accessToken : QByteArray{}, finished);
}

bool TraktApiClient::disconnectThenRevoke(const std::function<bool()> &disconnect)
{
    const auto credential = m_vault->loadForProfile(m_profile.profileId(), TrackerProviderId::Trakt);
    const QByteArray token = credential ? credential->accessToken : QByteArray{};
    if (!disconnect) return false;
    const bool completed = disconnect();
    // Queue cleanup can fail after the local connection and credential have
    // already been removed. That partial disconnect still fences old work.
    const bool credentialRemoved = credential
        && !m_vault->loadForProfile(m_profile.profileId(), TrackerProviderId::Trakt);
    if (!completed && !credentialRemoved) return false;
    resetForConnection();
    revokeAccessTokenAsync(token, {});
    return completed;
}

void TraktApiClient::send(const TrackerScrobbleIntent &intent, SendCompletion completion)
{
    const auto credential = m_vault->loadForProfile(m_profile.profileId(), TrackerProviderId::Trakt);
    if (!available() || intent.providerId != TrackerProviderId::Trakt || !credential
        || !traktAccountUuidIsCanonical(intent.remoteAccountId)
        || intent.remoteAccountId != credential->slot.remoteAccountId) {
        completion(TraktScrobbleSendResult::NeedsAttention); return;
    }
    const QJsonObject body = traktScrobbleBody(intent.remoteMediaId, intent.progressHundredths);
    if (body.isEmpty()) { completion(TraktScrobbleSendResult::NeedsAttention); return; }
    QString action;
    switch (intent.action) {
    case TrackerScrobbleAction::Start: action = QStringLiteral("start"); break;
    case TrackerScrobbleAction::Pause: action = QStringLiteral("pause"); break;
    case TrackerScrobbleAction::Stop: action = QStringLiteral("stop"); break;
    }
    post(QStringLiteral("/scrobble/") + action, QJsonDocument(body),
         [intent, action, completion = std::move(completion)](const TraktApiResult &result) {
        if (result.succeeded()) {
            const auto identity = traktParseRemoteMediaId(intent.remoteMediaId);
            const QJsonObject object = result.document.object();
            const QString actualAction = object.value(QStringLiteral("action")).toString();
            const QJsonObject media = object.value(identity->kind == TraktMediaKind::Movie
                ? QStringLiteral("movie") : QStringLiteral("episode")).toObject();
            const qint64 id = media.value(QStringLiteral("ids")).toObject().value(QStringLiteral("trakt")).toInteger();
            const bool actionMatches = intent.action == TrackerScrobbleAction::Stop
                ? (actualAction == QLatin1String("scrobble") || actualAction == QLatin1String("pause"))
                : actualAction == action;
            const double progress = object.value(QStringLiteral("progress")).toDouble(-1);
            completion(actionMatches && QString::number(id) == identity->traktId
                && std::isfinite(progress) && qAbs(progress * 100.0 - intent.progressHundredths) <= 0.5
                ? TraktScrobbleSendResult::Succeeded : TraktScrobbleSendResult::UnknownOutcome);
        }
        else if (result.networkFailure || result.payloadTooLarge || result.statusCode >= 500)
            completion(TraktScrobbleSendResult::UnknownOutcome);
        else if (result.statusCode == 429 || result.statusCode == 422)
            completion(TraktScrobbleSendResult::KnownNotApplied);
        else completion(TraktScrobbleSendResult::NeedsAttention);
    });
}

bool TraktApiClient::playbackContains(const QJsonDocument &document, const TrackerScrobbleIntent &intent)
{
    for (const TraktRemoteFact &fact : traktParsePlayback(document)) {
        if (fact.remoteMediaId == intent.remoteMediaId
            && qAbs(fact.exactProgress * 100.0 - intent.progressHundredths) <= 0.5
            && fact.occurredAtMs >= intent.createdAtMs - 1000
            && fact.occurredAtMs <= intent.createdAtMs + 120000) return true;
    }
    return false;
}

bool TraktApiClient::historyContains(const QJsonDocument &document, const TrackerScrobbleIntent &intent)
{
    for (const TraktRemoteFact &fact : traktParseHistory(document))
        if (fact.remoteMediaId == intent.remoteMediaId
            && fact.occurredAtMs >= intent.createdAtMs - 1000
            && fact.occurredAtMs <= intent.createdAtMs + 120000) return true;
    return false;
}

void TraktApiClient::readback(const TrackerScrobbleIntent &intent, ReadbackCompletion completion)
{
    const auto credential = m_vault->loadForProfile(m_profile.profileId(), TrackerProviderId::Trakt);
    if (!available() || intent.providerId != TrackerProviderId::Trakt || !credential
        || intent.remoteAccountId != credential->slot.remoteAccountId
        || intent.createdAtMs <= 0 || intent.createdAtMs > std::numeric_limits<qint64>::max() - 120000) {
        completion(TraktScrobbleReadbackResult::Indeterminate); return;
    }
    if (intent.action == TrackerScrobbleAction::Start) {
        completion(TraktScrobbleReadbackResult::Indeterminate);
        return;
    }
    if (intent.action == TrackerScrobbleAction::Pause || intent.progressHundredths < 8000) {
        QUrlQuery query; query.addQueryItem(QStringLiteral("extended"), QStringLiteral("full"));
        get(QStringLiteral("/sync/playback"), query, [intent, completion = std::move(completion)](const TraktApiResult &result) {
            if (!result.succeeded() || !result.document.isArray() || result.pageCount > 1
                || (!result.document.array().isEmpty() && traktParsePlayback(result.document).size() != result.document.array().size()))
                completion(TraktScrobbleReadbackResult::Indeterminate);
            else completion(playbackContains(result.document, intent) ? TraktScrobbleReadbackResult::ExactPresent : TraktScrobbleReadbackResult::Absent);
        });
        return;
    }
    const auto identity = traktParseRemoteMediaId(intent.remoteMediaId);
    if (!identity) { completion(TraktScrobbleReadbackResult::Indeterminate); return; }
    const QString type = identity->kind == TraktMediaKind::Movie ? QStringLiteral("movies") : QStringLiteral("episodes");
    QUrlQuery query; query.addQueryItem(QStringLiteral("limit"), QStringLiteral("20")); query.addQueryItem(QStringLiteral("extended"), QStringLiteral("full"));
    query.addQueryItem(QStringLiteral("start_at"), QDateTime::fromMSecsSinceEpoch(intent.createdAtMs - 1000, Qt::UTC).toString(Qt::ISODateWithMs));
    query.addQueryItem(QStringLiteral("end_at"), QDateTime::fromMSecsSinceEpoch(intent.createdAtMs + 120000, Qt::UTC).toString(Qt::ISODateWithMs));
    get(QStringLiteral("/sync/history/") + type + QLatin1Char('/') + identity->traktId, query,
        [intent, completion = std::move(completion)](const TraktApiResult &result) {
        if (!result.succeeded() || !result.document.isArray() || result.pageCount > 1
            || (!result.document.array().isEmpty() && traktParseHistory(result.document).size() != result.document.array().size()))
            completion(TraktScrobbleReadbackResult::Indeterminate);
        else completion(historyContains(result.document, intent) ? TraktScrobbleReadbackResult::ExactPresent : TraktScrobbleReadbackResult::Absent);
    });
}
