#include "NuvioApi.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>
#include <QUrlQuery>

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

namespace {
constexpr auto kApiOrigin = "https://api.nuvio.tv";
// Public client identifier published in the Nuvio public API reference.
constexpr auto kPublishableKey = "sb_publishable_1Clq8rlTVACkdcZuqr6_AD__xUUC_EN";
constexpr int kDeadlineMs = 15000;
constexpr qsizetype kMaxReplyBytes = 8 * 1024 * 1024;
constexpr qsizetype kMaxQueued = 32;

QString normalizedAccount(const QString &value) {
    const QUuid id(value);
    return id.isNull() ? QString() : id.toString(QUuid::WithoutBraces);
}

bool validToken(const QByteArray &token) {
    return !token.isEmpty() && token.size() <= 65536
        && std::all_of(token.cbegin(), token.cend(), [](unsigned char character) {
            return character >= 0x21 && character <= 0x7e;
        });
}

qint64 retryDelayMs(const QByteArray &header, qint64 now) {
    bool secondsValid = false;
    const qint64 seconds = header.trimmed().toLongLong(&secondsValid);
    if (secondsValid && seconds >= 0)
        return qMin(seconds, (std::numeric_limits<qint64>::max() - now) / 1000) * 1000;
    const QDateTime date = QDateTime::fromString(QString::fromLatin1(header), Qt::RFC2822Date);
    return date.isValid() ? qMax<qint64>(0, date.toMSecsSinceEpoch() - now) : 30000;
}
}

NuvioApi::NuvioApi(AccountCredentialStore *vault, QObject *parent, Transport transport)
    : QObject(parent), m_vault(vault), m_transport(std::move(transport)) {}

NuvioApi::~NuvioApi() {
    resetRequests();
}

void NuvioApi::resetRequests() {
    ++m_generation;
    m_operations.clear();
    m_busy = false;
    if (auto reply = std::exchange(m_reply, {}))
        reply->abort();
}

void NuvioApi::activate(const QString &profileId) {
    resetRequests();
    if (profileId != m_profileId)
        m_savedPaused = false;
    m_profileId = profileId;
    const auto credential = m_vault && !profileId.isEmpty()
        ? m_vault->loadNuvio(profileId) : std::nullopt;
    m_session = credential ? std::optional<Session>(Session{*credential, {}, 0}) : std::nullopt;
    m_saved = m_session.has_value();
    m_paused = credential && (m_savedPaused || !validToken(credential->refreshToken));
}

void NuvioApi::signIn(const QString &email, const QString &password, Completion completion) {
    if (m_profileId.isEmpty() || !m_vault || !m_vault->isAvailable()
        || email.trimmed().isEmpty() || email.size() > 320
        || password.isEmpty() || password.size() > 1024) {
        completion({400, {}, {}});
        return;
    }
    resetRequests();
    m_busy = true;
    send(QStringLiteral("/auth/v1/token?grant_type=password"), false,
         {{"email", email.trimmed()}, {"password", password}}, {},
         [this, completion = std::move(completion)](Reply reply) {
             m_busy = false;
             auto credential = decodeSession(reply);
             if (credential) {
                 m_session = std::move(credential);
                 m_saved = false;
                 m_paused = false;
             }
             completion({credential ? 200 : (reply.status == 200 ? 0 : reply.status), {}, {}});
         });
}

bool NuvioApi::saveSignIn(const QString &nuvioProfileId, int nuvioProfileIndex,
                         const std::function<bool()> &commitSetting) {
    if (m_busy || m_paused || !m_session || !m_vault)
        return false;
    auto candidate = m_session->credential;
    if (m_saved && candidate.nuvioProfileId == nuvioProfileId
        && candidate.nuvioProfileIndex == nuvioProfileIndex) {
        return !commitSetting || commitSetting();
    }
    candidate.nuvioProfileId = nuvioProfileId;
    candidate.nuvioProfileIndex = nuvioProfileIndex;
    const auto previous = m_vault->loadNuvio(m_profileId);
    if (!m_vault->saveNuvio(candidate))
        return false;
    const quint64 generation = m_generation;
    if (commitSetting && !commitSetting()) {
        if (previous)
            m_vault->saveNuvio(*previous);
        else
            m_vault->clearNuvio(candidate.profileId);
        return false;
    }
    if (generation != m_generation)
        return false;
    m_session->credential = std::move(candidate);
    m_saved = true;
    m_savedPaused = false;
    return true;
}

void NuvioApi::cancelSignIn() {
    activate(m_profileId);
}

bool NuvioApi::disconnectAccount() {
    if (!m_vault || m_profileId.isEmpty() || !m_vault->clearNuvio(m_profileId))
        return false;
    resetRequests();
    m_session.reset();
    m_saved = false;
    m_paused = false;
    m_savedPaused = false;
    return true;
}

QString NuvioApi::accountId() const {
    return m_session ? m_session->credential.accountId : QString();
}

QString NuvioApi::selectedProfileId() const {
    return m_session ? m_session->credential.nuvioProfileId : QString();
}

int NuvioApi::selectedProfileIndex() const {
    return m_session ? m_session->credential.nuvioProfileIndex : 0;
}

void NuvioApi::rpc(const QString &operation, const QJsonObject &body, Completion completion) {
    static const QRegularExpression name(QStringLiteral("\\Async_[a-z_]{1,80}\\z"));
    if (!name.match(operation).hasMatch()) {
        completion({400, {}, {}});
        return;
    }
    enqueue({QStringLiteral("/rest/v1/rpc/") + operation, false, body, std::move(completion)});
}

void NuvioApi::addons(int profileIndex, Completion completion) {
    if (profileIndex < 1 || profileIndex > 6) {
        completion({400, {}, {}});
        return;
    }
    enqueue({QStringLiteral("/rest/v1/addons?select=*&profile_id=eq.%1&order=sort_order")
                 .arg(profileIndex), true, {}, std::move(completion)});
}

void NuvioApi::progressForRemoval(int profileIndex, const QString &contentId, const QString &type,
    qint64 beforeOrAt, int offset, Completion completion) {
    static const QRegularExpression id(QStringLiteral("\\A(?:(?:imdb:)?tt[0-9]{1,12}|tmdb:[1-9][0-9]{0,9})\\z"));
    if (profileIndex < 1 || profileIndex > 6 || !id.match(contentId).hasMatch()
        || (type != QLatin1String("movie") && type != QLatin1String("series"))
        || beforeOrAt <= 0 || offset < 0 || offset >= 20000) {
        completion({400, {}, {}});
        return;
    }
    QUrlQuery query;
    query.addQueryItem("select", "user_id,profile_id,progress_key,content_id,content_type,season,episode,last_watched");
    query.addQueryItem("profile_id", QStringLiteral("eq.%1").arg(profileIndex));
    query.addQueryItem("content_id", QStringLiteral("eq.") + contentId);
    query.addQueryItem("content_type", QStringLiteral("eq.") + type);
    query.addQueryItem("last_watched", QStringLiteral("lte.%1").arg(beforeOrAt));
    query.addQueryItem("order", "progress_key.asc");
    query.addQueryItem("limit", "500");
    query.addQueryItem("offset", QString::number(offset));
    enqueue({QStringLiteral("/rest/v1/watch_progress?") + query.toString(QUrl::FullyEncoded),
        true, {}, std::move(completion)});
}

void NuvioApi::enqueue(Operation operation) {
    if (!m_session || m_paused || m_operations.size() >= kMaxQueued) {
        operation.completion({m_paused ? 401 : 0, {}, {}});
        return;
    }
    m_operations.enqueue(std::move(operation));
    pump();
}

void NuvioApi::pump() {
    if (m_busy || m_operations.isEmpty() || !m_session || m_paused)
        return;
    m_busy = true;
    if (m_session->accessExpiresAtUtcMs <= QDateTime::currentMSecsSinceEpoch() + 30000) {
        refresh();
        return;
    }
    Operation operation = m_operations.dequeue();
    send(operation.path, operation.get, operation.body, m_session->accessToken,
         [this, operation](Reply reply) mutable {
             m_busy = false;
             if (reply.status == 401 && !operation.retried) {
                 operation.retried = true;
                 m_operations.prepend(std::move(operation));
                 m_busy = true;
                 refresh();
                 return;
             }
             if (reply.status == 401)
                 pause();
             operation.completion(std::move(reply));
             pump();
         });
}

void NuvioApi::refresh() {
    send(QStringLiteral("/auth/v1/token?grant_type=refresh_token"), false,
         {{"refresh_token", QString::fromLatin1(m_session->credential.refreshToken)}}, {},
         [this](Reply reply) {
             m_busy = false;
             auto session = decodeSession(reply, m_session->credential.accountId);
             if (session) {
                 session->credential.nuvioProfileId = m_session->credential.nuvioProfileId;
                 session->credential.nuvioProfileIndex = m_session->credential.nuvioProfileIndex;
             }
             if (session && (!m_saved || m_vault->saveNuvio(session->credential))) {
                 m_session = std::move(session);
                 pump();
                 return;
             }
             if (reply.status == 200 || reply.status == 400 || reply.status == 401
                 || reply.status == 403) {
                 pause();
                 return;
             }
             auto failed = std::exchange(m_operations, {});
             const quint64 generation = m_generation;
             while (!failed.isEmpty() && generation == m_generation)
                 failed.dequeue().completion({reply.status, {}, {}});
         });
}

void NuvioApi::pause() {
    m_paused = true;
    if (m_saved)
        m_savedPaused = true;
    auto failed = std::exchange(m_operations, {});
    const quint64 generation = m_generation;
    while (!failed.isEmpty() && generation == m_generation)
        failed.dequeue().completion({401, {}, {}});
}

void NuvioApi::send(const QString &path, bool get, const QJsonObject &body,
                    const QByteArray &bearer, Completion completion) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now < m_retryAt) {
        completion({429, {}, {}});
        return;
    }
    Completion settled = [self = QPointer<NuvioApi>(this), generation = m_generation,
                          completion = std::move(completion)](Reply reply) {
        if (!self || generation != self->m_generation)
            return;
        if (reply.status == 429 || (reply.status == 503 && !reply.retryAfter.isEmpty())) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            self->m_retryAt = now + retryDelayMs(reply.retryAfter, now);
        }
        completion(std::move(reply));
    };
    if (m_transport)
        m_transport(path, get, body, bearer, std::move(settled));
    else
        sendHttp(path, get, body, bearer, std::move(settled));
}

void NuvioApi::sendHttp(const QString &path, bool get, const QJsonObject &body,
                        const QByteArray &bearer, Completion completion) {
    QNetworkRequest request(QUrl(QLatin1String(kApiOrigin) + path));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("apikey", kPublishableKey);
    request.setRawHeader("Cache-Control", "no-store");
    if (!bearer.isEmpty())
        request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + bearer);
    QNetworkReply *reply = get ? m_network.get(request)
        : m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_reply = reply;
    auto payload = std::make_shared<QByteArray>();
    auto read = [reply, payload]() {
        if (reply->isOpen())
            payload->append(reply->read(kMaxReplyBytes + 1 - payload->size()));
        if (payload->size() > kMaxReplyBytes)
            reply->abort();
    };
    connect(reply, &QNetworkReply::readyRead, this, read);
    QTimer::singleShot(kDeadlineMs, reply, [reply]() {
        if (!reply->isFinished())
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, payload, read, completion = std::move(completion)]() {
        read();
        if (m_reply == reply)
            m_reply.clear();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool success = status >= 200 && status < 300;
        const bool valid = payload->size() <= kMaxReplyBytes
            && (!success || reply->error() == QNetworkReply::NoError);
        reply->deleteLater();
        completion({valid ? status : 0,
                    success && valid && !payload->isEmpty() ? QJsonValue::fromJson(*payload) : QJsonValue(),
                    reply->rawHeader("Retry-After")});
    });
}

std::optional<NuvioApi::Session> NuvioApi::decodeSession(
    const Reply &reply, const QString &expectedAccount) const {
    if (reply.status != 200 || !reply.data.isObject())
        return std::nullopt;
    const QJsonObject data = reply.data.toObject();
    const QString account = data.contains("user")
        ? normalizedAccount(data.value("user").toObject().value("id").toString())
        : expectedAccount;
    const QByteArray access = data.value("access_token").toString().toLatin1();
    const QByteArray refresh = data.value("refresh_token").toString().toLatin1();
    const qint64 expiresIn = data.value("expires_in").toInteger();
    if (account.isEmpty() || (!expectedAccount.isEmpty() && account != expectedAccount)
        || !validToken(access) || !validToken(refresh)
        || expiresIn <= 30 || expiresIn > 7 * 86400) {
        return std::nullopt;
    }
    return Session{{m_profileId, account, refresh, {}, 0}, access,
        QDateTime::currentMSecsSinceEpoch() + expiresIn * 1000};
}
