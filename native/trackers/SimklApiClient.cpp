#include "SimklApiClient.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>

#include <cmath>
#include <utility>

namespace {

constexpr qint64 kMaximumBodyBytes = 32LL * 1024LL * 1024LL;
constexpr qint64 kRefreshLeewayMs = 24LL * 60LL * 60LL * 1000LL;
// SIMKL's write cap is one POST per second per client and user token; the
// 100 ms headroom mirrors the delivery runtime's own cadence.
constexpr int kPostIntervalMs = 1100;
constexpr qint64 kMaximumPostDelayMs = 60LL * 60 * 1000;

struct RemoteIdentity {
    QString kind;
    QString simklId;
    int season = 0;
    int episode = 0;
};

std::optional<RemoteIdentity> parseRemoteIdentity(const QString &value)
{
    const QStringList pieces = value.split(QLatin1Char(':'));
    bool idOk = false;
    if (pieces.size() == 2
        && (pieces.first() == QLatin1String("movie")
            || pieces.first() == QLatin1String("show")
            || pieces.first() == QLatin1String("anime"))) {
        pieces.at(1).toULongLong(&idOk);
        if (idOk)
            return RemoteIdentity{pieces.first(), pieces.at(1), 0, 0};
    }
    if (pieces.size() == 4
        && (pieces.first() == QLatin1String("episode")
            || pieces.first() == QLatin1String("anime_episode"))) {
        bool seasonOk = false;
        bool episodeOk = false;
        pieces.at(1).toULongLong(&idOk);
        const int season = pieces.at(2).toInt(&seasonOk);
        const int episode = pieces.at(3).toInt(&episodeOk);
        if (idOk && seasonOk && episodeOk && season >= 0 && episode > 0)
            return RemoteIdentity{pieces.first(), pieces.at(1), season, episode};
    }
    // Compatibility with the original movie-only journal format.
    value.toULongLong(&idOk);
    if (idOk)
        return RemoteIdentity{QStringLiteral("movie"), value, 0, 0};
    return std::nullopt;
}

qint64 retryAfterMs(const QNetworkReply *reply)
{
    bool ok = false;
    const qint64 seconds = reply->rawHeader("Retry-After").trimmed().toLongLong(&ok);
    // SIMKL's own retry guidance caps waits at 60 seconds; accept a generous
    // ceiling and never let an absurd header overflow the millisecond value.
    constexpr qint64 kMaximumRetryAfterSeconds = 24LL * 60 * 60;
    return ok && seconds > 0 && seconds <= kMaximumRetryAfterSeconds
        ? seconds * 1000 : 0;
}

QStringList scopesFrom(const QJsonValue &value)
{
    QStringList scopes;
    if (value.isString())
        scopes = value.toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    else if (value.isArray()) {
        for (const QJsonValue &entry : value.toArray()) {
            if (entry.isString())
                scopes.append(entry.toString());
        }
    }
    scopes.removeDuplicates();
    return scopes;
}

QJsonObject mediaObject(const RemoteIdentity &identity)
{
    return {{QStringLiteral("ids"),
             QJsonObject{{QStringLiteral("simkl"), identity.simklId.toLongLong()}}}};
}

bool sameId(const QJsonObject &media, const QString &expected)
{
    const QJsonValue value = media.value(QStringLiteral("ids"))
                                 .toObject().value(QStringLiteral("simkl"));
    if (value.isDouble())
        return QString::number(static_cast<qint64>(value.toDouble())) == expected;
    return value.toString() == expected;
}

} // namespace

SimklApiClient::SimklApiClient(const ProfilePaths &profile,
                               SimklAuthConfiguration configuration,
                               QObject *parent)
    : QObject(parent), m_profile(profile), m_configuration(std::move(configuration))
{
}

bool SimklApiClient::available() const
{
    return (m_profile.kind() == ProfilePaths::Kind::LocalOnly
            || m_profile.kind() == ProfilePaths::Kind::Account)
        && !m_configuration.clientId.isEmpty()
        && m_vault.isAvailable();
}

QString SimklApiClient::userAgent() const
{
    return m_configuration.appName + QLatin1Char('/') + m_configuration.appVersion;
}

QUrl SimklApiClient::endpoint(const QString &path, QUrlQuery query) const
{
    QUrl url(QStringLiteral("https://api.simkl.com") + path);
    query.addQueryItem(QStringLiteral("client_id"), m_configuration.clientId);
    query.addQueryItem(QStringLiteral("app-name"), m_configuration.appName);
    query.addQueryItem(QStringLiteral("app-version"), m_configuration.appVersion);
    url.setQuery(query);
    return url;
}

void SimklApiClient::get(const QString &path,
                         const QUrlQuery &query,
                         Completion completion)
{
    request(QByteArrayLiteral("GET"), path, query, {}, false, std::move(completion));
}

void SimklApiClient::post(const QString &path,
                          const QUrlQuery &query,
                          const QJsonDocument &body,
                          Completion completion)
{
    request(QByteArrayLiteral("POST"), path, query, body, false, std::move(completion));
}

void SimklApiClient::ensureCredential(bool forceRefresh,
                                      CredentialCompletion completion)
{
    if (!available()) {
        completion(std::nullopt);
        return;
    }
    const auto credential = m_vault.loadForProfile(
        m_profile.profileId(), TrackerProviderId::Simkl);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!credential || credential->refreshToken.isEmpty()
        || credential->refreshTokenExpiresAtMs <= now) {
        completion(std::nullopt);
        return;
    }
    if (!forceRefresh && !credential->accessToken.isEmpty()
        && credential->accessTokenExpiresAtMs > now + kRefreshLeewayMs) {
        completion(credential);
        return;
    }
    m_refreshWaiters.append(std::move(completion));
    if (m_refreshing)
        return;
    m_refreshing = true;
    refreshCredential(*credential);
}

void SimklApiClient::refreshCredential(const TrackerCredential &credential)
{
    // The token endpoint POST reserves its slot on the shared write cadence.
    // Waiting callers are already queued behind m_refreshing, so a sub-second
    // gate window only delays them, never drops them.
    postAtCadence([this, credential]() {
        QUrl url = m_configuration.tokenEndpoint;
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/x-www-form-urlencoded"));
        request.setRawHeader("User-Agent", userAgent().toUtf8());
        request.setTransferTimeout(30000);
        QUrlQuery form;
        form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
        form.addQueryItem(QStringLiteral("client_id"), m_configuration.clientId);
        form.addQueryItem(QStringLiteral("refresh_token"),
                          QString::fromUtf8(credential.refreshToken));
        QNetworkReply *reply = m_network.post(
            request, form.query(QUrl::FullyEncoded).toUtf8());
        const auto buffer = std::make_shared<QByteArray>();
        const auto tooLarge = std::make_shared<bool>(false);
        connect(reply, &QIODevice::readyRead, this, [reply, buffer, tooLarge] {
            if (*tooLarge)
                return;
            buffer->append(reply->readAll());
            if (buffer->size() > kMaximumBodyBytes) {
                *tooLarge = true;
                reply->abort();
            }
        });
        finishReply(reply, buffer, tooLarge,
                    [this, credential](const SimklApiResult &result) {
                        finishRefresh(result, credential);
                    });
    });
}

void SimklApiClient::finishRefresh(const SimklApiResult &result,
                                   const TrackerCredential &previous)
{
    std::optional<TrackerCredential> refreshed;
    const QJsonObject object = result.document.object();
    const QByteArray accessToken = object.value(QStringLiteral("access_token"))
                                       .toString().toUtf8();
    const QByteArray refreshToken = object.value(QStringLiteral("refresh_token"))
                                        .toString().toUtf8();
    const qint64 accessSeconds = object.value(QStringLiteral("expires_in")).toInteger();
    qint64 refreshSeconds = object.value(QStringLiteral("refresh_expires_in")).toInteger();
    if (refreshSeconds <= 0)
        refreshSeconds = 180LL * 24LL * 60LL * 60LL;
    if (result.succeeded() && !accessToken.isEmpty() && accessSeconds > 0) {
        TrackerCredential next = previous;
        next.accessToken = accessToken;
        if (!refreshToken.isEmpty())
            next.refreshToken = refreshToken;
        next.accessTokenExpiresAtMs = QDateTime::currentMSecsSinceEpoch()
            + accessSeconds * 1000;
        next.refreshTokenExpiresAtMs = QDateTime::currentMSecsSinceEpoch()
            + refreshSeconds * 1000;
        const QStringList scopes = scopesFrom(object.value(QStringLiteral("scope")));
        if (!scopes.isEmpty())
            next.grantedScopes = scopes;
        if (m_vault.saveAndVerify(next))
            refreshed = next;
    }
    m_refreshing = false;
    const QList<CredentialCompletion> waiters = std::exchange(
        m_refreshWaiters, QList<CredentialCompletion>{});
    for (const CredentialCompletion &waiter : waiters)
        waiter(refreshed);
}

void SimklApiClient::request(const QByteArray &method,
                             const QString &path,
                             QUrlQuery query,
                             const QJsonDocument &body,
                             bool retriedAfterUnauthorized,
                             Completion completion)
{
    ensureCredential(false,
        [this, method, path, query = std::move(query), body,
         retriedAfterUnauthorized, completion = std::move(completion)](
            std::optional<TrackerCredential> credential) mutable {
            if (!credential) {
                SimklApiResult failed;
                failed.statusCode = 401;
                completion(failed);
                return;
            }
            issue(method, path, std::move(query), body, *credential,
                  retriedAfterUnauthorized, std::move(completion));
        });
}

void SimklApiClient::issue(const QByteArray &method,
                           const QString &path,
                           QUrlQuery query,
                           const QJsonDocument &body,
                           const TrackerCredential &credential,
                           bool retriedAfterUnauthorized,
                           Completion completion)
{
    if (method != QByteArrayLiteral("POST")) {
        issueNow(method, path, query, body, credential, retriedAfterUnauthorized,
                 std::move(completion));
        return;
    }
    postAtCadence([this, method, path, query = std::move(query), body, credential,
                   retriedAfterUnauthorized,
                   completion = std::move(completion)]() mutable {
        issueNow(method, path, query, body, credential, retriedAfterUnauthorized,
                 std::move(completion));
    });
}

void SimklApiClient::postAtCadence(std::function<void()> fire)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // The timeline slot keeps reservation order; the last-actual-send floor
    // keeps the SIMKL write cap even when a stalled event loop delivers
    // several reserved slots in one burst.
    qint64 earliest = m_nextPostAtMs;
    if (m_lastPostSentAtMs > 0)
        earliest = qMax(earliest, m_lastPostSentAtMs + kPostIntervalMs);
    if (now >= earliest) {
        m_nextPostAtMs = now + kPostIntervalMs;
        m_lastPostSentAtMs = now;
        fire();
        return;
    }
    m_nextPostAtMs = earliest + kPostIntervalMs;
    schedulePostAt(earliest, std::move(fire));
}

void SimklApiClient::schedulePostAt(qint64 slotAtMs, std::function<void()> fire)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 delay = qBound<qint64>(0, slotAtMs - now, kMaximumPostDelayMs);
    QTimer::singleShot(static_cast<int>(delay), this,
                       [this, slotAtMs, fire = std::move(fire)]() mutable {
        const qint64 current = QDateTime::currentMSecsSinceEpoch();
        qint64 earliest = slotAtMs;
        if (m_lastPostSentAtMs > 0)
            earliest = qMax(earliest, m_lastPostSentAtMs + kPostIntervalMs);
        if (current < earliest) {
            // The event loop was busy past this slot's deadline (or the clock
            // moved): re-check against the last actual send and defer — never
            // send two writes back to back.
            schedulePostAt(earliest, std::move(fire));
            return;
        }
        m_lastPostSentAtMs = current;
        fire();
    });
}

void SimklApiClient::issueNow(const QByteArray &method,
                              const QString &path,
                              const QUrlQuery &query,
                              const QJsonDocument &body,
                              const TrackerCredential &credential,
                              bool retriedAfterUnauthorized,
                              Completion completion)
{
    QNetworkRequest request(endpoint(path, query));
    request.setRawHeader("User-Agent", userAgent().toUtf8());
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ")
                                              + credential.accessToken);
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(30000);
    QNetworkReply *reply = nullptr;
    if (method == QByteArrayLiteral("POST")) {
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/json"));
        reply = m_network.post(request, body.toJson(QJsonDocument::Compact));
    } else {
        reply = m_network.get(request);
    }
    const auto buffer = std::make_shared<QByteArray>();
    const auto tooLarge = std::make_shared<bool>(false);
    connect(reply, &QIODevice::readyRead, this, [reply, buffer, tooLarge] {
        if (*tooLarge)
            return;
        buffer->append(reply->readAll());
        if (buffer->size() > kMaximumBodyBytes) {
            *tooLarge = true;
            reply->abort();
        }
    });
    finishReply(reply, buffer, tooLarge,
        [this, method, path, query, body, retriedAfterUnauthorized,
         completion = std::move(completion)](const SimklApiResult &result) mutable {
            if (result.statusCode != 401 || retriedAfterUnauthorized) {
                completion(result);
                return;
            }
            ensureCredential(true,
                [this, method, path, query, body, completion = std::move(completion)](
                    std::optional<TrackerCredential> credential) mutable {
                    if (!credential) {
                        SimklApiResult failed;
                        failed.statusCode = 401;
                        completion(failed);
                        return;
                    }
                    issue(method, path, query, body, *credential, true,
                          std::move(completion));
                });
        });
}

void SimklApiClient::finishReply(QNetworkReply *reply,
                                 const std::shared_ptr<QByteArray> &buffer,
                                 const std::shared_ptr<bool> &tooLarge,
                                 Completion completion)
{
    connect(reply, &QNetworkReply::finished, this,
        [reply, buffer, tooLarge, completion = std::move(completion)]() mutable {
            if (!*tooLarge)
                buffer->append(reply->readAll());
            SimklApiResult result;
            result.statusCode = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            result.payloadTooLarge = *tooLarge;
            result.retryAfterMs = retryAfterMs(reply);
            result.networkFailure = !*tooLarge && result.statusCode == 0
                && reply->error() != QNetworkReply::NoError;
            if (!buffer->isEmpty() && !*tooLarge) {
                QJsonParseError parseError;
                result.document = QJsonDocument::fromJson(*buffer, &parseError);
                if (parseError.error != QJsonParseError::NoError)
                    result.document = {};
            }
            reply->deleteLater();
            completion(result);
        });
}

void SimklApiClient::revokeGrantAsync(const std::function<void()> &finished)
{
    const auto credential = m_vault.loadForProfile(
        m_profile.profileId(), TrackerProviderId::Simkl);
    if (!credential || credential->refreshToken.isEmpty()) {
        if (finished)
            finished();
        return;
    }
    revokeTokenAsync(credential->refreshToken, finished);
}

bool SimklApiClient::disconnectThenRevoke(const std::function<bool()> &disconnect)
{
    const auto credential = m_vault.loadForProfile(
        m_profile.profileId(), TrackerProviderId::Simkl);
    const QByteArray refreshToken = credential ? credential->refreshToken : QByteArray{};
    if (!disconnect || !disconnect())
        return false;
    if (!refreshToken.isEmpty())
        revokeTokenAsync(refreshToken, {});
    return true;
}

void SimklApiClient::revokeTokenAsync(const QByteArray &refreshToken,
                                      const std::function<void()> &finished)
{
    postAtCadence([this, refreshToken, finished]() {
        QNetworkRequest request(QUrl(
            QStringLiteral("https://api.simkl.com/oauth2/revoke")));
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/x-www-form-urlencoded"));
        request.setRawHeader("User-Agent", userAgent().toUtf8());
        request.setTransferTimeout(15000);
        QUrlQuery form;
        form.addQueryItem(QStringLiteral("client_id"), m_configuration.clientId);
        form.addQueryItem(QStringLiteral("token"),
                          QString::fromUtf8(refreshToken));
        QNetworkReply *reply = m_network.post(
            request, form.query(QUrl::FullyEncoded).toUtf8());
        connect(reply, &QNetworkReply::finished, this, [reply, finished]() {
            // The endpoint answers 200 for unknown, foreign, and
            // already-revoked tokens alike; there is no outcome to classify.
            reply->deleteLater();
            if (finished)
                finished();
        });
    });
}

QJsonObject SimklApiClient::scrobbleBody(const TrackerScrobbleIntent &intent)
{
    const auto identity = parseRemoteIdentity(intent.remoteMediaId);
    if (!identity)
        return {};
    QJsonObject body{{QStringLiteral("progress"),
                      static_cast<double>(intent.progressHundredths) / 100.0}};
    if (identity->kind == QLatin1String("movie")) {
        body.insert(QStringLiteral("movie"), mediaObject(*identity));
    } else {
        body.insert(identity->kind == QLatin1String("anime_episode")
                        ? QStringLiteral("anime") : QStringLiteral("show"),
                    mediaObject(*identity));
        QJsonObject episode{{QStringLiteral("number"), identity->episode}};
        if (identity->season > 0)
            episode.insert(QStringLiteral("season"), identity->season);
        body.insert(QStringLiteral("episode"), episode);
    }
    return body;
}

void SimklApiClient::send(const TrackerScrobbleIntent &intent,
                          SendCompletion completion)
{
    const QJsonObject body = scrobbleBody(intent);
    if (body.isEmpty()) {
        completion(SimklScrobbleSendResult::NeedsAttention);
        return;
    }
    QString action;
    switch (intent.action) {
    case TrackerScrobbleAction::Start: action = QStringLiteral("start"); break;
    case TrackerScrobbleAction::Pause: action = QStringLiteral("pause"); break;
    case TrackerScrobbleAction::Stop: action = QStringLiteral("stop"); break;
    }
    post(QStringLiteral("/scrobble/") + action, {}, QJsonDocument(body),
        [completion = std::move(completion)](const SimklApiResult &result) {
            if (result.succeeded() || result.statusCode == 409)
                completion(SimklScrobbleSendResult::Succeeded);
            else if (result.networkFailure || result.payloadTooLarge
                     || result.statusCode >= 500)
                completion(SimklScrobbleSendResult::UnknownOutcome);
            else if (result.statusCode == 429)
                completion(SimklScrobbleSendResult::KnownNotApplied);
            else
                completion(SimklScrobbleSendResult::NeedsAttention);
        });
}

bool SimklApiClient::readbackContains(const QJsonDocument &document,
                                      const TrackerScrobbleIntent &intent)
{
    const auto identity = parseRemoteIdentity(intent.remoteMediaId);
    if (!identity)
        return false;
    const QJsonArray rows = document.isArray() ? document.array() : QJsonArray{};
    for (const QJsonValue &value : rows) {
        const QJsonObject row = value.toObject();
        if (identity->kind == QLatin1String("movie")) {
            if (!sameId(row.value(QStringLiteral("movie")).toObject(), identity->simklId))
                continue;
        } else {
            const QJsonObject media = row.value(
                identity->kind == QLatin1String("anime_episode")
                    ? QStringLiteral("anime") : QStringLiteral("show")).toObject();
            if (!sameId(media, identity->simklId))
                continue;
            const QJsonObject episode = row.value(QStringLiteral("episode")).toObject();
            const int number = episode.value(QStringLiteral("number")).toInt(
                episode.value(QStringLiteral("episode")).toInt());
            if (number != identity->episode
                || (identity->season > 0
                    && episode.value(QStringLiteral("season")).toInt() != identity->season))
                continue;
        }
        const double progress = row.value(QStringLiteral("progress")).toDouble(-1.0);
        const double expected = static_cast<double>(intent.progressHundredths) / 100.0;
        if (progress < 0.0 || std::abs(progress - expected) <= 0.02)
            return true;
    }
    return false;
}

void SimklApiClient::readback(const TrackerScrobbleIntent &intent,
                              ReadbackCompletion completion)
{
    if (intent.action == TrackerScrobbleAction::Stop
        && intent.progressHundredths >= 8000) {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("extended"), QStringLiteral("simkl_ids_only"));
        get(QStringLiteral("/sync/all-items/movies/completed"), query,
            [intent, completion = std::move(completion)](const SimklApiResult &result) {
                const auto identity = parseRemoteIdentity(intent.remoteMediaId);
                if (!result.succeeded() || !result.document.isObject() || !identity) {
                    completion(SimklScrobbleReadbackResult::Indeterminate);
                    return;
                }
                for (const QJsonValue &value : result.document.object()
                         .value(QStringLiteral("movies")).toArray()) {
                    if (sameId(value.toObject().value(QStringLiteral("movie")).toObject(),
                               identity->simklId)) {
                        completion(SimklScrobbleReadbackResult::ExactPresent);
                        return;
                    }
                }
                completion(SimklScrobbleReadbackResult::Absent);
            });
        return;
    }
    get(QStringLiteral("/sync/playback"), {},
        [intent, completion = std::move(completion)](const SimklApiResult &result) {
            if (!result.succeeded() || !result.document.isArray()) {
                completion(SimklScrobbleReadbackResult::Indeterminate);
                return;
            }
            completion(readbackContains(result.document, intent)
                ? SimklScrobbleReadbackResult::ExactPresent
                : SimklScrobbleReadbackResult::Absent);
        });
}
