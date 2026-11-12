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
      m_reportClient(this, options.identityDirectory, options.plexOrigin),
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
void HomeServerService::activate(const QString &profileId, const QString &profileRoot,
                                 std::shared_ptr<HomeServerProgressOwner> progress) {
    ++m_profileGeneration;
    cancelPlaybacks();
    cancelRefreshes();
    invalidateRequests();
    m_progress = std::move(progress);
    m_store.deactivate();
    m_connectionStatus.clear();
    m_index.clear();
    m_sourceIdentities.clear();
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
    hideUnavailableProgress();
    emit profileChanged();
    publish();
    syncProgress();
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
    const auto generation = m_profileGeneration;
    if (m_store.connection(id).isEmpty()) return;
    if (!enabled) cancelPlaybacks(id);
    cancelRefresh(id);
    invalidateRequests();
    m_status = m_store.setEnabled(id, enabled) ? QString() : m_store.error();
    const auto owner = m_progress;
    if (owner && !m_store.connection(id).value("enabled").toBool())
        HomeServerProgress::hideConnection(*owner, id);
    if (generation != m_profileGeneration) return;
    if (enabled) reconcileProgress(id);
    publish();
    if (!enabled) syncProgress();
    if (enabled && m_refreshOnActivation) {
        const auto generation = m_profileGeneration;
        QTimer::singleShot(0, this, [this, id, generation] {
            if (generation == m_profileGeneration) refresh(id);
        });
    }
}
void HomeServerService::remove(const QString &id) {
    const auto generation = m_profileGeneration;
    if (m_store.connection(id).isEmpty()) return;
    cancelPlaybacks(id);
    cancelRefresh(id);
    invalidateRequests();
    m_status = m_store.remove(id) ? QString() : m_store.error();
    const auto owner = m_progress;
    if (owner && m_store.connection(id).isEmpty()) HomeServerProgress::hideConnection(*owner, id);
    if (generation != m_profileGeneration) return;
    m_connectionStatus.remove(id);
    if (m_store.connection(id).isEmpty()) {
        m_index.remove(id);
        m_serverMajor.remove(id);
        m_expired.remove(id);
    }
    publish();
    syncProgress();
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
            m_sourceIdentities.clear();
            m_index[id] = result.items;
            m_serverMajor[id] = result.serverMajor;
            m_connectionStatus[id] = tr("Refreshed %1 items.").arg(result.items.size());
            reconcileProgress(id);
            if (generation != m_profileGeneration) return;
        } else if (result.expired) {
            m_expired.insert(id);
            m_connectionStatus[id] = tr("Sign-in expired. Remove this connection and connect again.");
            hideUnavailableProgress();
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
        const auto rows = HomeServerCatalog::matchingSources(m_index.value(id), identity, connection);
        for (const auto &value : rows) {
            const auto row = value.toMap();
            if (m_sourceIdentities.size() >= 4096) m_sourceIdentities.clear();
            m_sourceIdentities.insert(row.value("infoHash").toString(), QJsonObject::fromVariantMap(row.value("homeIdentity").toMap()));
        }
        result.append(rows);
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
    const auto playback = m_playbacks.value(backend);
    if (playback.started && !playback.ended) enqueuePlayback(playback, "stop");
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
    const auto knownIdentity = m_sourceIdentities.value(sourceKey);
    if (!knownIdentity.isEmpty() && HomeServerCatalog::identitiesMatch(item, knownIdentity)) {
        auto ids = item.value("identity").toObject();
        if (ids.value("imdbId").toString().isEmpty() && knownIdentity.contains("imdbId")) {
            ids.insert("imdbId", knownIdentity.value("imdbId")); item.insert("identity", ids);
        }
    }
    if (m_progress) item = HomeServerProgress::withAliases(item, HomeServerProgress::identityAliases(m_progress->entries()));
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
        [this, guard, job, sourceKey, id, item, connection, token, profileGeneration, backendGeneration](HomeServerPlaybackJob::Result result) {
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
                hideUnavailableProgress();
                publish();
            }
            emit playbackFailed(sourceKey, result.error.isEmpty() ? tr("Could not play this server copy.") : result.error);
            return;
        }
        auto *player = dynamic_cast<PlayerBackendContract *>(guard.data());
        if (!player) return;
        Playback playback{guard, sourceKey, id, item, result};
        playback.profileGeneration = profileGeneration;
        playback.connection = connection;
        playback.token = token;
        m_playbacks.insert(guard.data(), playback);
        player->loadSource(result.url, result.headers);
        if (guard && profileGeneration == m_profileGeneration
            && m_backendGenerations.value(guard.data()) == backendGeneration)
            emit playbackResolved(sourceKey, result.transcoded);
    });
    return true;
}

void HomeServerService::enqueueReport(Report report) {
    if (report.token.isEmpty() || report.connection.isEmpty()) return;
    // Keep lifecycle ordering global, including the final Stop from an outgoing
    // profile. Coalesce only pending samples from the same profile and item.
    if (report.event == "progress" || report.event == "sync") {
        for (int i = m_reports.size() - 1; i >= 0; --i) {
            auto &old = m_reports[i];
            if (old.profileGeneration != report.profileGeneration
                || old.connection.value("id") != report.connection.value("id")
                || old.item.value("id") != report.item.value("id")) continue;
            if (old.event != report.event) break;
            if (old.state.updatedAt <= report.state.updatedAt) old = std::move(report);
            return;
        }
    }
    // A large imported library cannot enqueue unbounded network work. Refresh
    // reconciliation is repeated on the next explicit refresh; lifecycle wins.
    if (m_reports.size() >= 1024) {
        if (report.event == "sync" || report.event == "progress") return;
        for (int i = 0; i < m_reports.size(); ++i)
            if (m_reports[i].event == "sync" || m_reports[i].event == "progress") {
                m_reports.removeAt(i); break;
            }
    }
    m_reports.enqueue(std::move(report));
    drainReports();
}
void HomeServerService::drainReports() {
    if (m_reportJob || m_reports.isEmpty()) return;
    const auto report = m_reports.dequeue();
    auto *job = new HomeServerProgressJob(&m_reportClient, this);
    m_reportJob = job;
    job->start(report.connection, report.item, report.playback, report.token,
        report.event, report.state, [this, job, report](HomeServerProgressJob::Result result) {
        job->deleteLater();
        if (m_reportJob != job) return;
        m_reportJob = nullptr;
        const auto id = report.connection.value("id").toString();
        if (result.expired) {
            for (int i = m_reports.size() - 1; i >= 0; --i)
                if (m_reports[i].profileGeneration == report.profileGeneration
                    && m_reports[i].connection.value("id").toString() == id) m_reports.removeAt(i);
            if (report.profileGeneration == m_profileGeneration && !m_store.connection(id).isEmpty()) {
                m_expired.insert(id);
                m_connectionStatus[id] = tr("Sign-in expired. Remove this connection and connect again.");
                hideUnavailableProgress();
                publish();
            }
        }
        QTimer::singleShot(0, this, &HomeServerService::drainReports);
    });
}
void HomeServerService::enqueuePlayback(const Playback &playback, const QString &event) {
    auto state = playback.state;
    state.writePlayed = event == "stop" && playback.ended;
    enqueueReport({playback.connection, playback.item, playback.resolved, playback.token,
                   event, state, playback.profileGeneration});
}
void HomeServerService::reportPlayback(QObject *backend, const QString &sourceKey,
                                      qint64 positionMs, qint64 durationMs,
                                      bool paused, const QString &event) {
    if (!backend || backend->thread() != thread() || !m_playbacks.contains(backend)
        || positionMs < 0 || durationMs <= 0 || durationMs > 31536000000LL
        || (event != "start" && event != "progress" && event != "stop" && event != "ended")) return;
    auto &playback = m_playbacks[backend];
    if (playback.sourceKey != sourceKey || playback.profileGeneration != m_profileGeneration || playback.ended) return;
    const auto connection = m_store.connection(playback.connectionId);
    if (!connection.value("enabled").toBool() || m_expired.contains(playback.connectionId)) return;
    playback.state.positionMs = qMin(positionMs, durationMs);
    playback.state.durationMs = durationMs;
    playback.state.updatedAt = QDateTime::currentMSecsSinceEpoch();
    playback.state.paused = paused;
    playback.state.played = playback.state.played || playback.item.value("progress").toObject().value("played").toBool();
    if (!playback.started) {
        playback.started = true;
        enqueuePlayback(playback, "start");
        if (event == "start") return;
    } else if (event == "start") return;
    if (event == "ended") {
        playback.ended = true;
        playback.state.played = true;
        playback.state.positionMs = durationMs;
        playback.state.paused = false;
        const auto snapshot = playback;
        enqueuePlayback(snapshot, "stop");
        const auto owner = m_progress;
        auto completedItem = snapshot.item;
        auto completedProgress = completedItem.value("progress").toObject();
        completedProgress.insert("durationMs", durationMs);
        completedItem.insert("progress", completedProgress);
        if (owner) HomeServerProgress::completed(*owner, completedItem, snapshot.sourceKey, snapshot.state.updatedAt);
        if (snapshot.profileGeneration != m_profileGeneration) return;
    } else {
        enqueuePlayback(playback, event);
        if (event == "stop") playback.ended = true;
    }
    if (event != "stop") {
        const auto selected = m_playbacks.value(backend);
        for (const auto &value : m_store.connections()) {
            const auto other = value.toObject();
            const auto id = other.value("id").toString();
            if (id == selected.connectionId || !other.value("enabled").toBool() || m_expired.contains(id)) continue;
            for (const auto &entry : m_index.value(id)) {
                const auto item = entry.toObject();
                if (!HomeServerCatalog::identitiesMatch(selected.item, item)) continue;
                auto state = selected.state;
                state.writePlayed = selected.ended;
                auto target = other; target.insert("serverMajor", m_serverMajor.value(id));
                enqueueReport({target, item, {}, m_store.token(id), "sync", state, m_profileGeneration});
            }
        }
    }
}
void HomeServerService::reconcileProgress(const QString &id, const QString &canonicalId) {
    const auto owner = m_progress;
    const auto generation = m_profileGeneration;
    auto connection = m_store.connection(id);
    if (!owner || !connection.value("enabled").toBool() || m_expired.contains(id) || m_store.token(id).isEmpty()) return;
    connection.insert("serverMajor", m_serverMajor.value(id));
    const auto aliases = HomeServerProgress::identityAliases(owner->entries());
    for (const auto &value : m_index.value(id)) {
        const auto item = HomeServerProgress::withAliases(value.toObject(), aliases);
        if (item.value("kind") != "movie" && item.value("kind") != "episode") continue;
        if (!canonicalId.isEmpty() && HomeServerCatalog::canonicalVideoId(item) != canonicalId) continue;
        bool active = false;
        for (const auto &playback : m_playbacks)
            if (playback.connectionId == id && playback.item.value("id") == item.value("id")) { active = true; break; }
        if (active) continue;
        const auto versions = item.value("versions").toArray();
        if (versions.isEmpty()) continue;
        const auto key = "home-server:" + id + ':' + QString::fromLatin1(QUrl::toPercentEncoding(item.value("id").toString()))
            + ':' + QString::fromLatin1(QUrl::toPercentEncoding(versions.first().toObject().value("id").toString()));
        const auto decision = HomeServerProgress::reconcile(*owner, item, key);
        if (generation != m_profileGeneration || owner != m_progress) return;
        if (decision.localWins)
            enqueueReport({connection, item, {}, m_store.token(id), "sync", decision.state, generation});
    }
}
void HomeServerService::syncProgress(const QString &canonicalId) {
    const auto generation = m_profileGeneration;
    for (const auto &value : m_store.connections()) {
        reconcileProgress(value.toObject().value("id").toString(), canonicalId);
        if (generation != m_profileGeneration) return;
    }
}
void HomeServerService::hideUnavailableProgress() {
    const auto owner = m_progress;
    const auto generation = m_profileGeneration;
    if (!owner) return;
    QSet<QString> hidden;
    for (const auto &value : owner->entries()) {
        const auto resume = value.toMap().value("resume").toMap();
        const auto key = resume.value("infoHash").toString();
        if (!key.startsWith("home-server:") || !resume.value("localPath").toString().isEmpty()) continue;
        const auto id = key.section(':', 1, 1);
        const auto connection = m_store.connection(id);
        if (!available() || !connection.value("enabled").toBool() || m_expired.contains(id) || m_store.token(id).isEmpty()) hidden.insert(id);
    }
    for (const auto &id : hidden) {
        HomeServerProgress::hideConnection(*owner, id);
        if (generation != m_profileGeneration) return;
    }
}
