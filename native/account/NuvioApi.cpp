#include "NuvioApi.h"

#include <QDateTime>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
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
constexpr qint64 kMaxDeviceLifetimeMs = 10 * 60 * 1000;
constexpr qint64 kDeviceSlowDownMs = 5000;

class SingleAttemptUpload final : public QIODevice {
public:
    explicit SingleAttemptUpload(QByteArray payload) : m_payload(std::move(payload)) {
        open(ReadOnly | Unbuffered);
    }

    bool isSequential() const override { return true; }
    bool reset() override { return false; }

    qint64 bytesAvailable() const override {
        return m_payload.size() - m_offset + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override {
        const qint64 count = qMin<qint64>(maxSize, m_payload.size() - m_offset);
        std::copy_n(m_payload.constData() + m_offset, count, data);
        m_offset += count;
        return count;
    }

    qint64 writeData(const char *, qint64) override { return -1; }

private:
    QByteArray m_payload;
    qint64 m_offset = 0;
};

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

QDateTime parseHttpDate(const QByteArray &header) {
    QString value = QString::fromLatin1(header.trimmed());
    if (value.endsWith(QLatin1String(" GMT"))) {
        value.chop(3);
        value += QLatin1String("+0000");
    }
    return QDateTime::fromString(value, Qt::RFC2822Date);
}

qint64 retryDelayMs(const QByteArray &header, qint64 now) {
    bool secondsValid = false;
    const qint64 seconds = header.trimmed().toLongLong(&secondsValid);
    if (secondsValid && seconds >= 0)
        return qMin(seconds, (std::numeric_limits<qint64>::max() - now) / 1000) * 1000;
    const QDateTime date = parseHttpDate(header);
    return date.isValid() ? qMax<qint64>(0, date.toMSecsSinceEpoch() - now) : 30000;
}

qint64 responseTimeMs(const QByteArray &serverDate, qint64 fallback) {
    const QDateTime date = parseHttpDate(serverDate);
    const qint64 timestamp = date.isValid() ? date.toMSecsSinceEpoch() : 0;
    return timestamp > 0 ? timestamp : fallback;
}

QJsonObject deviceSignInRow(const NuvioApi::Reply &reply) {
    const QJsonArray rows = reply.data.toArray();
    return rows.size() == 1 ? rows.first().toObject() : QJsonObject();
}

qint64 devicePollIntervalMs(const QJsonObject &row) {
    const qint64 seconds = row.value(QStringLiteral("poll_interval_seconds")).toInteger();
    return seconds >= 1 && seconds <= kMaxDeviceLifetimeMs / 1000 ? seconds * 1000 : 0;
}
}

NuvioApi::NuvioApi(AccountCredentialStore *vault, QObject *parent, Transport transport)
    : QObject(parent), m_vault(vault), m_transport(std::move(transport)) {
    m_deviceTimer.setSingleShot(true);
    m_deviceTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_deviceTimer, &QTimer::timeout, this, &NuvioApi::pollDeviceSignIn);
}

NuvioApi::~NuvioApi() {
    resetRequests();
}

void NuvioApi::resetRequests() {
    ++m_generation;
    m_deviceTimer.stop();
    m_deviceSignIn.reset();
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

void NuvioApi::startDeviceSignIn(DeviceSignInCallback callback) {
    if (m_profileId.isEmpty() || !m_vault || !m_vault->isAvailable()) {
        callback(DeviceSignInState::Failed, {}, 400);
        return;
    }
    resetRequests();
    m_busy = true;
    // Construct the nested aggregate explicitly for Clang.
    auto &flow = m_deviceSignIn.emplace(DeviceSignIn{});
    flow.callback = std::move(callback);
    flow.nonce = QUuid::createUuid().toString(QUuid::WithoutBraces);
#ifdef Q_OS_ANDROID
    const QString deviceType = QStringLiteral("android");
#else
    const QString deviceType = QStringLiteral("desktop");
#endif
    send(QStringLiteral("/rest/v1/rpc/start_device_login_session"), false,
         {{QStringLiteral("p_device_nonce"), flow.nonce},
          {QStringLiteral("p_redirect_base_url"), QStringLiteral("https://nuvio.tv/link")},
          {QStringLiteral("p_device_name"), QStringLiteral("Colosseum")},
          {QStringLiteral("p_device_type"), deviceType}}, {},
         [this](Reply reply) {
             if (reply.status != 200) {
                 finishDeviceSignIn(DeviceSignInState::Failed, reply.status);
                 return;
             }
             const QJsonObject row = deviceSignInRow(reply);
             const QString userCode = row.value(QStringLiteral("user_code")).toString();
             const QString deviceCode = row.value(QStringLiteral("device_code")).toString();
             const QDateTime expires = QDateTime::fromString(
                 row.value(QStringLiteral("expires_at")).toString(), Qt::ISODateWithMs);
             const qint64 intervalMs = devicePollIntervalMs(row);
             static const QRegularExpression userFormat(QStringLiteral("\\A[A-Z0-9]{6}\\z"));
             static const QRegularExpression deviceFormat(QStringLiteral("\\A[A-Za-z0-9_-]{12,128}\\z"));
             if (!userFormat.match(userCode).hasMatch() || !deviceFormat.match(deviceCode).hasMatch()
                 || !expires.isValid() || expires.timeSpec() == Qt::LocalTime || intervalMs == 0) {
                 finishDeviceSignIn(DeviceSignInState::Failed, 0);
                 return;
             }
             const qint64 ttlMs = expires.toMSecsSinceEpoch()
                 - responseTimeMs(reply.serverDate, QDateTime::currentMSecsSinceEpoch());
             if (ttlMs <= 0) {
                 finishDeviceSignIn(DeviceSignInState::Expired, 410);
                 return;
             }
             if (ttlMs > kMaxDeviceLifetimeMs) {
                 finishDeviceSignIn(DeviceSignInState::Failed, 0);
                 return;
             }
             auto &active = *m_deviceSignIn;
             active.deviceCode = deviceCode;
             active.expires.setRemainingTime(ttlMs, Qt::PreciseTimer);
             active.pollIntervalMs = intervalMs;
             const auto notify = active.callback;
             scheduleDevicePoll();
             notify(DeviceSignInState::Waiting, userCode, 200);
         }, true);
}

void NuvioApi::scheduleDevicePoll(qint64 minimumDelayMs) {
    auto &flow = *m_deviceSignIn;
    flow.requestInFlight = false;
    const qint64 delayMs = qMax(flow.pollIntervalMs, minimumDelayMs);
    m_deviceTimer.start(static_cast<int>(qMin(delayMs, flow.expires.remainingTime())));
}

void NuvioApi::pollDeviceSignIn() {
    if (!m_deviceSignIn)
        return;
    auto &flow = *m_deviceSignIn;
    if (flow.expires.hasExpired()) {
        finishDeviceSignIn(DeviceSignInState::Expired, 410);
        return;
    }
    // During an in-flight poll or exchange, the timer only enforces expiry.
    m_deviceTimer.start(static_cast<int>(qMax<qint64>(1, flow.expires.remainingTime())));
    if (flow.requestInFlight)
        return;
    flow.requestInFlight = true;
    send(QStringLiteral("/rest/v1/rpc/poll_tv_login_session"), false,
         {{QStringLiteral("p_code"), flow.deviceCode}, {QStringLiteral("p_device_nonce"), flow.nonce}}, {},
         [this](Reply reply) {
             auto &active = *m_deviceSignIn;
             if (active.expires.hasExpired()) {
                 finishDeviceSignIn(DeviceSignInState::Expired, 410);
                 return;
             }
             if (reply.status == 429 || (reply.status == 503 && !reply.retryAfter.isEmpty())) {
                 scheduleDevicePoll(retryDelayMs(reply.retryAfter,
                     responseTimeMs(reply.serverDate, QDateTime::currentMSecsSinceEpoch())));
                 return;
             }
             const QJsonObject row = deviceSignInRow(reply);
             const QString state = row.value(QStringLiteral("status")).toString();
             if (reply.status == 410 || (reply.status == 200 && state == QLatin1String("expired"))) {
                 finishDeviceSignIn(DeviceSignInState::Expired, 410);
                 return;
             }
             if (reply.status != 200) {
                 finishDeviceSignIn(DeviceSignInState::Failed, reply.status);
                 return;
             }
             if (state == QLatin1String("approved")) {
                 exchangeDeviceSignIn();
                 return;
             }
             if (state != QLatin1String("pending") && state != QLatin1String("slow_down")) {
                 finishDeviceSignIn(DeviceSignInState::Failed, 0);
                 return;
             }
             if (state == QLatin1String("slow_down"))
                 active.pollIntervalMs += kDeviceSlowDownMs;
             active.pollIntervalMs = qMax(active.pollIntervalMs, devicePollIntervalMs(row));
             scheduleDevicePoll(reply.retryAfter.isEmpty() ? 0 : retryDelayMs(reply.retryAfter,
                 responseTimeMs(reply.serverDate, QDateTime::currentMSecsSinceEpoch())));
         }, true);
}

void NuvioApi::exchangeDeviceSignIn() {
    auto &flow = *m_deviceSignIn;
    const QJsonObject body{
        {QStringLiteral("code"), std::exchange(flow.deviceCode, {})},
        {QStringLiteral("device_nonce"), std::exchange(flow.nonce, {})}};
    send(QStringLiteral("/functions/v1/tv-logins-exchange"), false, body, {},
         [this](Reply reply) {
             if (m_deviceSignIn->expires.hasExpired()) {
                 finishDeviceSignIn(DeviceSignInState::Expired, 410);
                 return;
             }
             auto candidate = decodeSession(reply, {}, true);
             if (!candidate) {
                 finishDeviceSignIn(DeviceSignInState::Failed, reply.status == 200 ? 0 : reply.status);
                 return;
             }
             if (!candidate->credential.accountId.isEmpty()) {
                 stageDeviceSession(std::move(*candidate));
                 return;
             }
             m_deviceSignIn->exchangeSession = std::move(candidate);
             const QByteArray bearer = m_deviceSignIn->exchangeSession->accessToken;
             send(QStringLiteral("/auth/v1/user"), true, {}, bearer,
                  [this](Reply user) {
                      if (m_deviceSignIn->expires.hasExpired()) {
                          finishDeviceSignIn(DeviceSignInState::Expired, 410);
                          return;
                      }
                      const QString account = normalizedAccount(
                          user.data.toObject().value(QStringLiteral("id")).toString());
                      if (user.status != 200 || account.isEmpty()) {
                          finishDeviceSignIn(DeviceSignInState::Failed, user.status == 200 ? 0 : user.status);
                          return;
                      }
                      auto &session = *m_deviceSignIn->exchangeSession;
                      session.credential.accountId = account;
                      stageDeviceSession(std::move(session));
                  });
         }, true);
}

void NuvioApi::stageDeviceSession(Session session) {
    m_session = std::move(session);
    m_saved = false;
    m_paused = false;
    finishDeviceSignIn(DeviceSignInState::Complete, 200);
}

void NuvioApi::finishDeviceSignIn(DeviceSignInState state, int status) {
    auto callback = std::move(m_deviceSignIn->callback);
    resetRequests();
    callback(state, {}, status);
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

void NuvioApi::cancelPendingRequests() {
    resetRequests();
}

void NuvioApi::createMobileSettings(int profileIndex, const QJsonObject &settings, Completion completion) {
    if (profileIndex < 1 || profileIndex > 6 || accountId().isEmpty()) {
        completion({400, {}, {}});
        return;
    }
    enqueue({QStringLiteral("/rest/v1/profile_settings_blobs?on_conflict=user_id,profile_id,platform&select=updated_at"),
        false, {{"user_id", accountId()}, {"profile_id", profileIndex}, {"platform", "mobile"},
            {"settings_json", settings}}, std::move(completion)});
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

void NuvioApi::verifyProfilePin(int profileIndex, const QString &pin, Completion completion) {
    static const QRegularExpression format(QStringLiteral("\\A[0-9]{4}\\z"));
    if (profileIndex < 1 || profileIndex > 6 || !format.match(pin).hasMatch()) {
        completion({400, {}, {}});
        return;
    }
    enqueue({QStringLiteral("/rest/v1/rpc/verify_profile_pin"), false,
             {{QStringLiteral("p_profile_id"), profileIndex}, {QStringLiteral("p_pin"), pin}},
             std::move(completion), UnauthorizedRetry::Forbidden});
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
    const bool singleAttempt = operation.unauthorizedRetry == UnauthorizedRetry::Forbidden;
    const QJsonObject body = singleAttempt ? std::exchange(operation.body, {}) : operation.body;
    send(operation.path, operation.get, body, m_session->accessToken,
         [this, operation](Reply reply) mutable {
             m_busy = false;
             if (reply.status == 401 && operation.unauthorizedRetry == UnauthorizedRetry::Allowed) {
                 operation.unauthorizedRetry = UnauthorizedRetry::Used;
                 m_operations.prepend(std::move(operation));
                 m_busy = true;
                 refresh();
                 return;
             }
             if (reply.status == 401)
                 pause();
             operation.completion(std::move(reply));
             pump();
         }, singleAttempt);
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
                    const QByteArray &bearer, Completion completion, bool singleAttempt) {
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
            const qint64 delay = retryDelayMs(reply.retryAfter, responseTimeMs(reply.serverDate, now));
            self->m_retryAt = now + qMin(delay, std::numeric_limits<qint64>::max() - now);
        }
        completion(std::move(reply));
    };
    if (m_transport)
        m_transport(path, get, body, bearer, std::move(settled));
    else
        sendHttp(path, get, body, bearer, std::move(settled), singleAttempt);
}

void NuvioApi::sendHttp(const QString &path, bool get, const QJsonObject &body,
                        const QByteArray &bearer, Completion completion, bool singleAttempt) {
    QNetworkRequest request(QUrl(QLatin1String(kApiOrigin) + path));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("apikey", kPublishableKey);
    request.setRawHeader("Cache-Control", "no-store");
    // The guarded RPC can compare an existing row. A first insert must also
    // tolerate another client creating the row without overwriting its fields.
    if (path.startsWith(QLatin1String("/rest/v1/profile_settings_blobs?")))
        request.setRawHeader("Prefer", "resolution=ignore-duplicates,return=representation");
    if (!bearer.isEmpty())
        request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + bearer);

    QNetworkReply *reply;
    if (get) {
        reply = m_network.get(request);
    } else {
        QByteArray uploadBody = QJsonDocument(body).toJson(QJsonDocument::Compact);
        if (singleAttempt) {
            // Qt can resend buffered uploads after a connection failure. An
            // unbuffered HTTP/1.1 upload that cannot rewind prevents replay.
            request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
            request.setAttribute(QNetworkRequest::DoNotBufferUploadDataAttribute, true);
            request.setHeader(QNetworkRequest::ContentLengthHeader, uploadBody.size());
            auto *upload = new SingleAttemptUpload(std::move(uploadBody));
            reply = m_network.post(request, upload);
            upload->setParent(reply);
        } else {
            reply = m_network.post(request, uploadBody);
        }
    }
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
                    reply->rawHeader("Retry-After"),
                    reply->rawHeader("Date")});
    });
}

std::optional<NuvioApi::Session> NuvioApi::decodeSession(
    const Reply &reply, const QString &expectedAccount, bool resolveMissingAccount) const {
    if (reply.status != 200 || !reply.data.isObject())
        return std::nullopt;
    const QJsonObject data = reply.data.toObject();
    const QJsonValue user = data.value("user");
    const QString account = !user.isUndefined()
        ? normalizedAccount(user.toObject().value("id").toString())
        : expectedAccount;
    const QByteArray access = data.value("access_token").toString().toLatin1();
    const QByteArray refresh = data.value("refresh_token").toString().toLatin1();
    const qint64 expiresIn = data.value("expires_in").toInteger();
    const bool canResolve = resolveMissingAccount && (user.isUndefined() || user.isNull());
    if ((account.isEmpty() && !canResolve) || (!expectedAccount.isEmpty() && account != expectedAccount)
        || !validToken(access) || !validToken(refresh)
        || expiresIn <= 30 || expiresIn > 7 * 86400) {
        return std::nullopt;
    }
    return Session{{m_profileId, account, refresh, {}, 0}, access,
        QDateTime::currentMSecsSinceEpoch() + expiresIn * 1000};
}
