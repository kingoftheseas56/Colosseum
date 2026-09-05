#include "streamserver.h"

#include "../colosseum_server/runtime/ColosseumServerRuntime.h"

#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace {

QString engineUnavailableMessage()
{
    return QStringLiteral("Native streaming runtime unavailable. Repair or reinstall Colosseum.");
}

} // namespace

StreamServer::StreamServer(QObject *parent)
    : StreamServer(nullptr, parent)
{
}

StreamServer::StreamServer(TorrentEngine *torrentEngine, QObject *parent)
    : QObject(parent)
    , m_torrentEngine(torrentEngine)
{
    m_nam = new QNetworkAccessManager(this);
}

StreamServer::~StreamServer()
{
    if (m_runtime)
        m_runtime->stop();
}

void StreamServer::setEngineUnavailable(bool unavailable)
{
    if (m_engineUnavailable == unavailable)
        return;
    m_engineUnavailable = unavailable;
    Q_EMIT engineUnavailableChanged();
}

void StreamServer::markEngineUnavailable(const QString &message)
{
    // Requests queued before a terminal startup failure belong to the failed
    // attempt. Do not replay them later if the installation is repaired.
    m_pending.clear();
    setEngineUnavailable(true);
    Q_EMIT streamError(message);
}

void StreamServer::warmUp()
{
    ensureStarted();
}

void StreamServer::ensureStarted()
{
    if (m_starting || m_port > 0)
        return;

    setEngineUnavailable(false);
    m_starting = true;
    Q_EMIT startingChanged();

    const QString cacheDir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/colosseum-stream");
    QDir().mkpath(cacheDir);

    colosseum::server::runtime::ColosseumServerRuntimeOptions options;
    options.appPath = QDir::toNativeSeparators(cacheDir);
    options.settingsDirectory = options.appPath;
    // The native player consumes the advertised URL, so an ephemeral loopback
    // port avoids colliding with any independently installed service.
    options.httpPort = 0;
    options.enableTls = false;
    options.torrentEngine = m_torrentEngine;
    m_runtime = std::make_unique<colosseum::server::runtime::ColosseumServerRuntime>(
        std::move(options));
    if (!m_runtime->start()) {
        const QString error = m_runtime->lastError();
        m_runtime.reset();
        m_starting = false;
        Q_EMIT startingChanged();
        markEngineUnavailable(error.isEmpty() ? engineUnavailableMessage() : error);
        return;
    }

    m_port = m_runtime->httpUrl().port();
    setEngineUnavailable(false);
    m_starting = false;
    qInfo("[stream] native engine ready on port %d", m_port);
    Q_EMIT readyChanged();
    Q_EMIT startingChanged();
    pushTunedSettings();
}

QString StreamServer::streamUrl(const QString &infoHash, int fileIdx) const
{
    if (m_port <= 0 || infoHash.isEmpty() || fileIdx < 0)
        return {};
    return QStringLiteral("http://127.0.0.1:%1/%2/%3")
        .arg(m_port)
        .arg(infoHash.toLower())
        .arg(fileIdx);
}

void StreamServer::play(const QString &infoHash, int fileIdx)
{
    if (infoHash.isEmpty()) {
        Q_EMIT streamError(QStringLiteral("This source has no torrent hash to play."));
        return;
    }
    if (m_port > 0) {
        registerThenReady(infoHash, fileIdx, false);
        return;
    }
    m_pending.append({infoHash, fileIdx, false});
    ensureStarted();
}

void StreamServer::prefetch(const QString &infoHash, int fileIdx)
{
    if (infoHash.isEmpty()) {
        Q_EMIT streamError(QStringLiteral("This source has no torrent hash to fetch."));
        return;
    }
    if (m_port > 0) {
        registerThenReady(infoHash, fileIdx, true);
        return;
    }
    m_pending.append({infoHash, fileIdx, true});
    ensureStarted();
}

void StreamServer::flushPending()
{
    const auto pend = m_pending;
    m_pending.clear();
    for (const Pending &p : pend)
        registerThenReady(p.infoHash, p.fileIdx, p.fetch, p.connectionRefusedRetries);
}

void StreamServer::registerThenReady(const QString &infoHash, int fileIdx, bool fetch,
                                     int connectionRefusedRetries)
{
    const QString hash = infoHash.toLower();
    // Register the torrent with the runtime (it constructs the magnet from the hash).
    // We emit the playable URL regardless of the create result — newer runtimes also
    // auto-create on the first ranged GET that mpv issues — but doing the POST first
    // matches TB2's proven sequence and warms the engine before playback.
    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1/%2/create").arg(m_port).arg(hash)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply *reply = m_nam->post(req, QByteArray("{}"));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, hash, fileIdx, fetch, connectionRefusedRetries]() {
        const auto err = reply->error();
        reply->deleteLater();
        // A vanished native listener gets one fresh runtime generation for this
        // request. Carry the budget with the pending request so a second refusal
        // cannot recursively create another generation forever.
        if (err == QNetworkReply::ConnectionRefusedError && m_runtime && m_port > 0) {
            const int failedPort = m_port;
            m_runtime->stop();
            m_runtime.reset();
            m_port = -1;
            Q_EMIT readyChanged();

            if (connectionRefusedRetries < 1) {
                qWarning("[stream] native server on :%d is gone — resetting and retrying once",
                         failedPort);
                m_pending.append({hash, fileIdx, fetch, connectionRefusedRetries + 1});
                ensureStarted();
            } else {
                qWarning("[stream] native server on :%d refused the recovery retry — giving up",
                         failedPort);
                markEngineUnavailable(engineUnavailableMessage());
            }
            return;
        }
        if (err != QNetworkReply::NoError)
            qWarning("[stream] create warning: %s", qUtf8Printable(reply->errorString()));
        const QString url = streamUrl(hash, fileIdx);
        if (url.isEmpty())
            Q_EMIT streamError(QStringLiteral("Stream engine not ready."));
        else if (fetch)
            Q_EMIT fetchReady(url, hash, fileIdx);
        else
            Q_EMIT streamReady(url, hash, fileIdx);
    });
}

void StreamServer::pushTunedSettings()
{
    // Persist the player's desktop-scale swarm policy through the same settings route exposed
    // by the native server. The native TorrentEngine applies its own libtorrent session policy
    // at start; this request keeps the server-settings surface and future engine creation in
    // sync. Pending registrations wait for the write so their settings are not reordered.
    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1/settings").arg(m_port)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(3000);
    const QByteArray body = QByteArrayLiteral(
        "{\"btMaxConnections\":200,"
        "\"btDownloadSpeedSoftLimit\":20971520,"
        "\"btDownloadSpeedHardLimit\":41943040}");
    QNetworkReply *r = m_nam->post(req, body);
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        r->deleteLater();
        if (r->error() == QNetworkReply::NoError)
            qInfo("[stream] swarm tuning pushed (200 conns, 20/40 MB/s caps)");
        else
            qWarning("[stream] swarm tuning push failed (native defaults remain active): %s",
                     qUtf8Printable(r->errorString()));
        flushPending();   // tuning is best-effort; playback must never be blocked by it
    });
}

void StreamServer::watchStats(const QString &infoHash, int fileIdx)
{
    if (infoHash.isEmpty() || fileIdx < 0)
        return;
    m_statsHash = infoHash.toLower();
    m_statsIdx = fileIdx;
    m_statsInflight = false;
    if (!m_statsTimer) {
        m_statsTimer = new QTimer(this);
        m_statsTimer->setInterval(1000);   // Popcorn Time's stats cadence (streamer.js updateStats)
        connect(m_statsTimer, &QTimer::timeout, this, &StreamServer::pollStats);
    }
    m_statsTimer->start();
    pollStats();                           // first sample immediately, not one second late
}

void StreamServer::unwatchStats()
{
    if (m_statsTimer)
        m_statsTimer->stop();
    m_statsHash.clear();
    m_statsIdx = -1;
}

void StreamServer::pollStats()
{
    if (m_port <= 0 || m_statsHash.isEmpty() || m_statsInflight)
        return;
    m_statsInflight = true;
    const QString hash = m_statsHash;
    const int idx = m_statsIdx;
    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1/%2/%3/stats.json")
                                 .arg(m_port).arg(hash).arg(idx)));
    req.setTransferTimeout(900);           // must resolve inside the 1 s cadence
    QNetworkReply *r = m_nam->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r, hash, idx]() {
        m_statsInflight = false;
        r->deleteLater();
        if (hash != m_statsHash || idx != m_statsIdx)   // watch retargeted/stopped mid-flight
            return;
        if (r->error() != QNetworkReply::NoError)
            return;                        // silent miss: the face falls back to its static line
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        if (o.isEmpty())
            return;
        QVariantMap stats;
        stats.insert(QStringLiteral("peers"),          o.value(QLatin1String("peers")).toInt());
        stats.insert(QStringLiteral("unchoked"),       o.value(QLatin1String("unchoked")).toInt());
        stats.insert(QStringLiteral("downloaded"),     o.value(QLatin1String("downloaded")).toDouble());
        stats.insert(QStringLiteral("downloadSpeed"),  o.value(QLatin1String("downloadSpeed")).toDouble());
        stats.insert(QStringLiteral("streamProgress"), o.value(QLatin1String("streamProgress")).toDouble());
        stats.insert(QStringLiteral("streamLen"),      o.value(QLatin1String("streamLen")).toDouble());
        Q_EMIT streamStats(hash, idx, stats);
    });
}
