#include "HomeServerService.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QUrlQuery>
#include "player/playerbackendcontract.h"

HomeServerService::HomeServerService(QObject *parent)
    : HomeServerService(HomeServerServiceOptions{}, parent) {}
HomeServerService::HomeServerService(HomeServerServiceOptions options, QObject *parent)
    : QObject(parent), m_client(this, options.identityDirectory, options.plexOrigin),
      m_dataClient(this, options.identityDirectory, options.plexOrigin),
      m_store(options.secrets ? options.secrets : createAccountSecretBackend()),
      m_refreshOnActivation(options.refreshOnActivation), m_android(options.android),
      m_videoDecodeCapabilities(std::move(options.videoDecodeCapabilities)) {
    m_pinTimer.setSingleShot(true);
    m_pinTimer.setInterval(qBound(10, options.pinPollIntervalMs, 5000));
    connect(&m_pinTimer, &QTimer::timeout, this, &HomeServerService::pollPlex);
}
HomeServerService::~HomeServerService() {
    ++m_generation;
    ++m_profileGeneration;
    m_pinTimer.stop();
    cancelPlaybacks();
    cancelRefreshes();
    m_client.cancelAll();
}
void HomeServerService::publish() { ++m_revision; emit changed(); }
void HomeServerService::invalidateRequests() {
    ++m_generation;
    m_pinTimer.stop();
    m_client.cancelAll();
    m_busy = false;
    m_pinId = 0;
    m_pinDeadline = 0;
    m_pinCode.clear();
    m_pinUrl.clear();
    m_plexCandidates = {};
    m_httpConsentRequired = false;
}
void HomeServerService::activate(const QString &profileId, const QString &profileRoot) {
    ++m_profileGeneration;
    cancelPlaybacks();
    cancelRefreshes();
    invalidateRequests();
    m_store.deactivate();
    m_connectionStatus.clear();
    m_index.clear();
    m_serverMajor.clear();
    m_expired.clear();
    m_profileId.clear();
    m_profileRoot.clear();
    m_status.clear();
    if (!profileId.isEmpty() && !profileRoot.isEmpty()) {
        if (!QDir().mkpath(profileRoot) || !m_store.activate(profileId, profileRoot))
            m_status = m_store.error().isEmpty() ? tr("Profile storage is unavailable.") : m_store.error();
        else {
            m_profileId = profileId;
            m_profileRoot = QFileInfo(profileRoot).canonicalFilePath();
            if (!m_client.isReady()) m_status = m_client.identityError();
            else if (!m_dataClient.isReady()) m_status = m_dataClient.identityError();
            for (const auto &value : m_store.connections()) {
                const auto id = value.toObject().value("id").toString();
                m_index.insert(id, m_store.items(id));
            }
        }
    }
    emit profileChanged();
    publish();
    if (m_refreshOnActivation) scheduleRefreshes();
}
void HomeServerService::deactivate() { activate({}, {}); }
bool HomeServerService::available() const { return m_store.available() && m_client.isReady() && m_dataClient.isReady(); }
QVariantList HomeServerService::connections() const {
    QVariantList rows;
    for (const auto &value : m_store.connections()) {
        const auto object = value.toObject();
        const auto id = object.value("id").toString();
        auto row = object.toVariantMap();
        row.insert("needsReconnect", m_expired.contains(id) || m_store.token(id).isEmpty());
        row.insert("status", m_connectionStatus.value(id));
        row.insert("refreshing", m_refreshes.contains(id) || m_refreshQueue.contains(id));
        row.insert("itemCount", itemCount(id));
        rows.append(row);
    }
    return rows;
}
QVariantList HomeServerService::plexServers() const {
    QVariantList rows;
    for (const auto &value : m_plexCandidates) {
        const auto object = value.toObject();
        rows.append(QVariantMap{{"id", object.value("id").toString()},
            {"name", object.value("name").toString()}, {"available", object.value("available").toBool()}});
    }
    return rows;
}
bool HomeServerService::isInsecureAddress(const QString &address) const {
    return HomeServerClient::normalizeAddress(address).scheme() == QLatin1String("http");
}
bool HomeServerService::canSignIn() {
    if (!available()) {
        m_status = !m_client.isReady() ? m_client.identityError()
            : (!m_dataClient.isReady() ? m_dataClient.identityError()
               : (m_store.error().isEmpty() ? tr("Secure profile storage is unavailable.") : m_store.error()));
        publish();
        return false;
    }
    return !m_busy;
}
void HomeServerService::finishSignIn(const QString &message) {
    m_pinTimer.stop();
    m_busy = false;
    m_status = message;
    publish();
}
void HomeServerService::saveAuthenticated(const QJsonObject &metadata, const QString &token) {
    const auto profileGeneration = m_profileGeneration;
    const auto id = m_store.add(metadata, token);
    if (id.isEmpty()) { finishSignIn(m_store.error()); return; }
    m_plexCandidates = {};
    m_pinCode.clear();
    m_pinUrl.clear();
    m_pinId = 0;
    m_httpConsentRequired = false;
    finishSignIn(tr("Connected to %1.").arg(metadata.value("name").toString()));
    if (profileGeneration != m_profileGeneration) return;
    emit connectionAdded(id);
    if (m_refreshOnActivation) {
        QTimer::singleShot(0, this, [this, id, profileGeneration] {
            if (profileGeneration == m_profileGeneration) refresh(id);
        });
    }
}
void HomeServerService::connectServer(const QString &provider, const QString &address,
                                     const QString &username, const QString &password, bool allowHttp) {
    if (!canSignIn()) return;
    invalidateRequests();
    const auto origin = HomeServerClient::normalizeAddress(address);
    if (origin.isEmpty() || username.trimmed().isEmpty()) {
        finishSignIn(tr("Enter a server address and username.")); return;
    }
    if (origin.scheme() == QLatin1String("http") && !allowHttp) {
        m_httpConsentRequired = true;
        finishSignIn(tr("Acknowledge the HTTP warning before connecting.")); return;
    }
    const auto generation = m_generation;
    m_busy = true;
    m_status = tr("Signing in…");
    publish();
    if (generation != m_generation) return;
    m_client.authenticate(provider, origin.toString(), username, password, allowHttp,
        [this, generation, provider, origin](HomeServerClient::Response response) {
        if (generation != m_generation) return;
        if (!response.ok()) {
            finishSignIn(response.expired() ? tr("Sign-in failed. Check your username and password.") : response.error);
            return;
        }
        const auto body = response.body.toObject();
        const auto user = body.value("User").toObject();
        const auto token = body.value("AccessToken").toString();
        m_client.request(provider, origin, QStringLiteral("/System/Info"), "GET", token, {},
            [this, generation, provider, origin, user, body, token](HomeServerClient::Response info) {
            if (generation != m_generation) return;
            if (info.expired()) { finishSignIn(tr("Sign-in expired. Try again.")); return; }
            const auto system = info.ok() ? info.body.toObject() : QJsonObject{};
            const auto authenticatedId = body.value("ServerId").toString();
            const auto systemId = system.value("Id").toString();
            if (!authenticatedId.isEmpty() && !systemId.isEmpty() && authenticatedId != systemId) {
                finishSignIn(tr("The server identity changed while signing in.")); return;
            }
            QString name = system.value("ServerName").toString();
            if (name.trimmed().isEmpty()) name = origin.authority() + origin.path();
            saveAuthenticated({{"provider", provider}, {"origin", origin.toString()}, {"name", name},
                {"userId", user.value("Id")}, {"userName", user.value("Name").toString()},
                {"serverId", authenticatedId.isEmpty() ? systemId : authenticatedId}}, token);
        });
    });
}
void HomeServerService::beginPlex() {
    if (!canSignIn()) return;
    invalidateRequests();
    const auto generation = m_generation;
    m_busy = true;
    m_status = tr("Requesting a Plex sign-in code…");
    publish();
    if (generation != m_generation) return;
    m_client.createPlexPin([this, generation](HomeServerClient::Response response) {
        if (generation != m_generation) return;
        if (!response.ok()) { finishSignIn(response.error); return; }
        const auto pin = response.body.toObject();
        m_pinId = pin.value("id").toInteger();
        m_pinCode = pin.value("code").toString();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const auto date = QDateTime::fromString(pin.value("expiresAt").toString(), Qt::ISODateWithMs);
        const qint64 expiresIn = pin.value("expiresIn").toInteger(300);
        m_pinDeadline = date.isValid() ? date.toMSecsSinceEpoch() : now + qBound(qint64(1), expiresIn, qint64(300)) * 1000;
        m_pinDeadline = qMin(m_pinDeadline, now + 300000);
        QUrl url(QStringLiteral("https://plex.tv/link"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("pin"), m_pinCode);
        url.setQuery(query);
        m_pinUrl = url.toString();
        m_status = tr("Open Plex sign-in and enter the code.");
        publish();
        m_pinTimer.start();
    });
}
void HomeServerService::pollPlex() {
    if (!m_busy || m_pinId == 0) return;
    if (QDateTime::currentMSecsSinceEpoch() >= m_pinDeadline) {
        m_pinCode.clear(); m_pinUrl.clear();
        finishSignIn(tr("Plex sign-in expired. Try again.")); return;
    }
    const auto generation = m_generation;
    m_client.pollPlexPin(m_pinId, m_pinCode, [this, generation](HomeServerClient::Response response) {
        if (generation != m_generation) return;
        if (!response.ok()) { finishSignIn(tr("Plex sign-in failed. Try again.")); return; }
        const auto token = response.body.toObject().value("authToken").toString();
        if (token.isEmpty()) { m_pinTimer.start(); return; }
        m_pinId = 0; m_pinCode.clear(); m_pinUrl.clear();
        m_status = tr("Finding your Plex servers…");
        publish();
        if (generation != m_generation) return;
        m_client.discoverPlexServers(token, [this, generation](HomeServerClient::Response servers) {
            if (generation != m_generation) return;
            if (!servers.ok()) {
                finishSignIn(servers.expired() ? tr("Plex sign-in expired. Try again.") : servers.error); return;
            }
            m_plexCandidates = servers.body.toArray();
            finishSignIn(m_plexCandidates.isEmpty() ? tr("No Plex Media Server is available for this account.")
                                                   : tr("Choose your Plex server."));
        });
    });
}
void HomeServerService::selectPlexServer(const QString &id, bool allowHttp) {
    if (!canSignIn()) return;
    for (const auto &value : m_plexCandidates) {
        const auto candidate = value.toObject();
        if (candidate.value("id").toString() != id) continue;
        if (!candidate.value("available").toBool()) {
            finishSignIn(tr("That Plex server is offline.")); return;
        }
        const auto generation = m_generation;
        m_busy = true; m_httpConsentRequired = false;
        m_status = tr("Connecting to your Plex server…");
        publish();
        probePlex(candidate, 0, allowHttp, false, generation);
        return;
    }
    finishSignIn(tr("Choose a server from the current Plex sign-in."));
}
void HomeServerService::probePlex(const QJsonObject &candidate, int endpoint, bool allowHttp,
                                 bool skippedHttp, quint64 generation) {
    if (generation != m_generation) return;
    const auto endpoints = candidate.value("endpoints").toArray();
    if (endpoint >= endpoints.size() || endpoint >= 16) {
        m_httpConsentRequired = skippedHttp;
        finishSignIn(skippedHttp ? tr("This server has an HTTP connection. Acknowledge the warning to try it.")
                                : tr("Could not reach this Plex server.")); return;
    }
    const QUrl origin(endpoints.at(endpoint).toObject().value("origin").toString());
    if (origin.scheme() == QLatin1String("http") && !allowHttp) {
        probePlex(candidate, endpoint + 1, allowHttp, true, generation); return;
    }
    m_client.verifyPlexServer(origin, candidate.value("token").toString(), allowHttp,
        [this, candidate, endpoint, allowHttp, skippedHttp, generation, origin](HomeServerClient::Response response) {
        if (generation != m_generation) return;
        if (response.expired()) { finishSignIn(tr("Plex sign-in expired. Sign in again.")); return; }
        if (!response.ok() || response.body.toObject().value("id") != candidate.value("id")) {
            probePlex(candidate, endpoint + 1, allowHttp, skippedHttp, generation); return;
        }
        saveAuthenticated({{"provider", "plex"}, {"origin", origin.toString()},
            {"name", candidate.value("name")}, {"userId", "plex"}, {"userName", "Plex"},
            {"serverId", candidate.value("id")}}, candidate.value("token").toString());
    });
}
void HomeServerService::cancelSignIn() {
    invalidateRequests(); m_status.clear(); publish();
}
void HomeServerService::setEnabled(const QString &id, bool enabled) {
    if (m_store.connection(id).isEmpty()) return;
    if (!enabled) cancelPlaybacks(id);
    cancelRefresh(id);
    invalidateRequests();
    m_status = m_store.setEnabled(id, enabled) ? QString() : m_store.error();
    publish();
    if (enabled && m_refreshOnActivation) {
        const auto generation = m_profileGeneration;
        QTimer::singleShot(0, this, [this, id, generation] {
            if (generation == m_profileGeneration) refresh(id);
        });
    }
}
void HomeServerService::remove(const QString &id) {
    if (m_store.connection(id).isEmpty()) return;
    cancelPlaybacks(id);
    cancelRefresh(id);
    invalidateRequests();
    m_status = m_store.remove(id) ? QString() : m_store.error();
    m_connectionStatus.remove(id);
    if (m_store.connection(id).isEmpty()) {
        m_index.remove(id);
        m_serverMajor.remove(id);
        m_expired.remove(id);
    }
    publish();
}

int HomeServerService::itemCount(const QString &id) const { return m_index.value(id).size(); }

void HomeServerService::cancelRefresh(const QString &id) {
    const auto job = m_refreshes.take(id);
    if (job) { job->cancel(); job->deleteLater(); }
    m_refreshQueue.removeAll(id);
}

void HomeServerService::cancelRefreshes() {
    const auto ids = m_refreshes.keys();
    for (const auto &id : ids) cancelRefresh(id);
    m_refreshQueue.clear();
    m_dataClient.cancelAll();
}

void HomeServerService::scheduleRefreshes() {
    const auto generation = m_profileGeneration;
    QTimer::singleShot(0, this, [this, generation] {
        if (generation != m_profileGeneration) return;
        for (const auto &value : m_store.connections()) {
            const auto row = value.toObject();
            if (row.value("enabled").toBool()) refresh(row.value("id").toString());
        }
    });
}

void HomeServerService::drainRefreshQueue() {
    if (m_refreshQueue.isEmpty() || m_refreshes.size() >= 3) return;
    const auto id = m_refreshQueue.takeFirst();
    refresh(id);
}

void HomeServerService::refresh(const QString &id) {
    const auto row = m_store.connection(id);
    if (!available() || row.isEmpty() || !row.value("enabled").toBool() || m_refreshes.contains(id)) return;
    if (m_expired.contains(id)) {
        m_connectionStatus[id] = tr("Sign-in expired. Remove this connection and connect again.");
        publish(); return;
    }
    const auto token = m_store.token(id);
    if (token.isEmpty()) {
        m_expired.insert(id);
        m_connectionStatus[id] = tr("Sign-in expired. Remove this connection and connect again.");
        publish(); return;
    }
    if (m_refreshes.size() >= 3) {
        if (!m_refreshQueue.contains(id)) m_refreshQueue.append(id);
        m_connectionStatus[id] = tr("Waiting to refresh…");
        publish(); return;
    }
    m_refreshQueue.removeAll(id);
    auto *job = new HomeServerRefresh(&m_dataClient, this);
    m_refreshes.insert(id, job);
    const auto generation = m_profileGeneration;
    m_connectionStatus[id] = tr("Refreshing…");
    publish();
    if (generation != m_profileGeneration || m_refreshes.value(id) != job) return;
    job->start(row, token, [this, id, generation, job](HomeServerRefresh::Result result) {
        job->deleteLater();
        if (generation != m_profileGeneration || m_refreshes.value(id) != job) return;
        m_refreshes.remove(id);
        const auto current = m_store.connection(id);
        if (current.isEmpty() || !current.value("enabled").toBool()) { drainRefreshQueue(); return; }
        if (result.ok() && m_store.replaceItems(id, result.items)) {
            m_index[id] = result.items;
            m_serverMajor[id] = result.serverMajor;
            m_connectionStatus[id] = tr("Refreshed %1 items.").arg(result.items.size());
        } else if (result.expired) {
            m_expired.insert(id);
            m_connectionStatus[id] = tr("Sign-in expired. Remove this connection and connect again.");
        } else {
            m_connectionStatus[id] = result.ok() ? m_store.error() : result.error;
        }
        publish();
        if (generation == m_profileGeneration) drainRefreshQueue();
    });
}

QVariantList HomeServerService::sources(const QVariantMap &identity) const {
    QVariantList result;
    if (!available()) return result;
    for (const auto &value : m_store.connections()) {
        const auto connection = value.toObject();
        const auto id = connection.value("id").toString();
        if (!connection.value("enabled").toBool() || m_expired.contains(id) || m_store.token(id).isEmpty()) continue;
        result.append(HomeServerCatalog::matchingSources(m_index.value(id), identity, connection));
    }
    return result;
}

void HomeServerService::cancelBackend(QObject *backend, bool stop) {
    const auto job = m_playbackJobs.take(backend);
    const bool owned = job || m_playbacks.contains(backend);
    if (!owned && !m_backendGenerations.contains(backend)) return;
    ++m_backendGenerations[backend];
    if (job) { job->cancel(); job->deleteLater(); }
    m_pendingSources.remove(backend);
    m_playbacks.remove(backend);
    if (owned && stop && backend)
        if (auto *player = dynamic_cast<PlayerBackendContract *>(backend)) player->stopPlayback();
}

void HomeServerService::cancelPlayback(QObject *backend) { if (backend) cancelBackend(backend, true); }

void HomeServerService::cancelPlaybacks(const QString &connectionId) {
    QSet<QObject *> backends;
    for (auto it = m_pendingSources.cbegin(); it != m_pendingSources.cend(); ++it)
        if (connectionId.isEmpty() || it.value().section(':', 1, 1) == connectionId) backends.insert(it.key());
    for (auto it = m_playbacks.cbegin(); it != m_playbacks.cend(); ++it)
        if (connectionId.isEmpty() || it.value().connectionId == connectionId) backends.insert(it.key());
    for (auto *backend : backends) cancelBackend(backend, true);
}

bool HomeServerService::loadPlayback(QObject *backend, const QString &sourceKey,
                                    qint64 resumeMs, bool forceTranscode) {
    Q_UNUSED(resumeMs); // The existing player seeks on the full VOD timeline after load.
    if (!available() || !backend || backend->thread() != thread() || !dynamic_cast<PlayerBackendContract *>(backend)) return false;
    const QPointer<QObject> guard(backend);
    const auto profileGeneration = m_profileGeneration;
    const auto parts = sourceKey.split(':');
    if (parts.size() != 4 || parts.first() != QLatin1String("home-server") || sourceKey.size() > 16384) return false;
    const auto id = parts.at(1);
    auto connection = m_store.connection(id);
    if (connection.isEmpty() || !connection.value("enabled").toBool() || m_expired.contains(id)) return false;
    const auto token = m_store.token(id);
    if (token.isEmpty()) return false;
    const auto itemId = QUrl::fromPercentEncoding(parts.at(2).toUtf8());
    const auto versionId = QUrl::fromPercentEncoding(parts.at(3).toUtf8());
    QJsonObject item;
    bool versionFound = false;
    for (const auto &value : m_index.value(id)) {
        const auto candidate = value.toObject();
        if (candidate.value("id").toString() != itemId) continue;
        item = candidate;
        for (const auto &version : item.value("versions").toArray())
            if (version.toObject().value("id").toString() == versionId) versionFound = true;
        break;
    }
    if (item.isEmpty() || !versionFound || HomeServerCatalog::canonicalVideoId(item).isEmpty()) return false;
    cancelBackend(backend, true);
    if (!guard || profileGeneration != m_profileGeneration) return false;
    if (!m_observedBackends.contains(backend)) {
        m_observedBackends.insert(backend);
        connect(backend, &QObject::destroyed, this, [this, backend] {
            cancelBackend(backend, false);
            m_observedBackends.remove(backend);
            m_backendGenerations.remove(backend);
        });
    }
    auto *job = new HomeServerPlaybackJob(&m_dataClient, this);
    m_playbackJobs[backend] = job;
    m_pendingSources[backend] = sourceKey;
    const auto backendGeneration = m_backendGenerations.value(backend);
    connection.insert("serverMajor", m_serverMajor.value(id));
    job->start(connection, item, versionId, token, {m_android, m_videoDecodeCapabilities, forceTranscode},
        [this, guard, job, sourceKey, id, item, profileGeneration, backendGeneration](HomeServerPlaybackJob::Result result) {
        job->deleteLater();
        if (!guard || profileGeneration != m_profileGeneration
            || m_backendGenerations.value(guard.data()) != backendGeneration
            || m_playbackJobs.value(guard.data()) != job) return;
        m_playbackJobs.remove(guard.data());
        m_pendingSources.remove(guard.data());
        const auto current = m_store.connection(id);
        if (current.isEmpty() || !current.value("enabled").toBool()) return;
        if (!result.ok()) {
            if (result.expired) {
                m_expired.insert(id);
                m_connectionStatus[id] = tr("Sign-in expired. Remove this connection and connect again.");
                publish();
            }
            emit playbackFailed(sourceKey, result.error.isEmpty() ? tr("Could not play this server copy.") : result.error);
            return;
        }
        auto *player = dynamic_cast<PlayerBackendContract *>(guard.data());
        if (!player) return;
        m_playbacks.insert(guard.data(), Playback{guard, sourceKey, id, item, result});
        player->loadSource(result.url, result.headers);
        if (guard && profileGeneration == m_profileGeneration
            && m_backendGenerations.value(guard.data()) == backendGeneration)
            emit playbackResolved(sourceKey, result.transcoded);
    });
    return true;
}
