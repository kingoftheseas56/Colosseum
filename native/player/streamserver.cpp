#include "streamserver.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <cmath>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>

namespace {

#ifndef COLOSSEUM_STREAM_START_TIMEOUT_MS
#define COLOSSEUM_STREAM_START_TIMEOUT_MS 10000
#endif

QString runtimeExecutableName()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("stremio-runtime.exe");
#else
    return QStringLiteral("stremio-runtime");
#endif
}

QString engineUnavailableMessage()
{
#if defined(Q_OS_LINUX)
    return QStringLiteral("Streaming engine unavailable. Install or start Stremio Service.");
#else
    return QStringLiteral("Streaming engine unavailable. Repair or reinstall Colosseum.");
#endif
}

QString prepareRuntimeScript(const QString &runtimeDir, const QString &cacheDir, int port)
{
    QFile source(QDir(runtimeDir).filePath(QStringLiteral("server.js")));
    if (!source.open(QIODevice::ReadOnly))
        return {};

    QByteArray script = source.readAll();
    source.close();
    if (!script.contains("11470"))
        return {};

    // Disable the bundle's fixed-range retry, then update every internal default-port URL.
    // Replace 11474 first so a selected port with those digits cannot be rewritten twice.
    script.replace("11474", QByteArray::number(port));
    script.replace("11470", QByteArray::number(port));

    const QString patchedPath = QDir(cacheDir).filePath(QStringLiteral("server-colosseum.js"));
    QSaveFile patched(patchedPath);
    if (!patched.open(QIODevice::WriteOnly) || patched.write(script) != script.size()
        || !patched.commit())
        return {};
    return patchedPath;
}

} // namespace

StreamServer::StreamServer(QObject *parent)
    : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
    connect(this, &StreamServer::readyChanged, this, [this]() {
        if (!ready()) return;
        const auto requests = m_torrentRequests.values();
        for (const auto &request : requests)
            if (!request->reply && !request->retry->isActive()) registerTorrentRequest(request);
    });
}

StreamServer::~StreamServer()
{
    const auto ids = m_torrentRequests.keys();
    for (const auto &id : ids) cancelRequest(id);
    if (m_proc) {
        // Teardown is not a startup failure. Disconnect lifecycle callbacks before
        // stopping the owned runtime so an intentional exit cannot emit the
        // fail-before-ready warning path while this object is being destroyed.
        disconnect(m_proc, nullptr, this, nullptr);
#ifdef Q_OS_WIN
        // The owned node runtime is a console process without a WM_CLOSE handler.
        // QProcess::terminate cannot stop it on Windows; waiting only delays exit.
        // An adopted external service has no m_proc and is never stopped here.
        m_proc->kill();
        m_proc->waitForFinished(2000);
#else
        m_proc->terminate();
        if (!m_proc->waitForFinished(2000)) {
            m_proc->kill();
            m_proc->waitForFinished(2000);
        }
#endif
    }
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
    const bool reportLegacy = !m_pending.isEmpty() || !m_scopedStartup;
    m_pending.clear();
    setEngineUnavailable(true);
    const auto ids = m_torrentRequests.keys();
    for (const auto &id : ids) failTorrentRequest(id, message);
    if (reportLegacy) Q_EMIT streamError(message);
}

QString StreamServer::findRuntimeDir() const
{
    const QString exe = runtimeExecutableName();
    const QString appDir = QCoreApplication::applicationDirPath();

    QStringList candidates;
    // 1) explicit override
    const QString env = qEnvironmentVariable("COLOSSEUM_STREAM_SERVER");
    if (!env.isEmpty())
        candidates << env;
    // 2) shipped next to the Colosseum exe (self-contained copy, gitignored)
    candidates << appDir + QStringLiteral("/stream_server");
    candidates << appDir + QStringLiteral("/../stream_server");
#if defined(Q_OS_WIN)
    // 3a) official Windows Stremio Service install.
    const QString genericData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!genericData.isEmpty())
        candidates << QDir(genericData).filePath(QStringLiteral("Programs/StremioService"));
#elif defined(Q_OS_LINUX)
    // 3b) official Stremio Service deb/rpm payload. A Flatpak service remains adopt-first:
    // when it is already answering on :11470 we never need to enter its sandbox.
    candidates << QStringLiteral("/usr/share/stremio-service");
#endif

    for (const QString &dir : candidates) {
        const QDir runtimeDir(dir);
        const QFileInfo runtime(runtimeDir.filePath(exe));
        if (runtime.exists() && runtime.isFile() && runtime.isExecutable()
            && QFileInfo::exists(runtimeDir.filePath(QStringLiteral("server.js"))))
            return runtimeDir.absolutePath();
    }
    return {};
}

void StreamServer::warmUp()
{
    m_scopedStartup = false;
    // Deliberately nothing but ensureStarted(): no new state and no second code path. Warming and
    // playing must go through the SAME adopt-first probe, or a warm-up could spawn a child that then
    // clashes with the official service on :11470 -- the silent failure diagnosed 2026-07-05.
    ensureStarted();
}

void StreamServer::ensureStarted()
{
    if (m_proc || m_starting || m_port > 0)
        return;

    // A previous failed attempt may have happened before the user repaired the
    // installation or started the official service. A new attempt gets a clean
    // state; a successful adoption/launch clears it again below.
    setEngineUnavailable(false);
    m_starting = true;
    Q_EMIT startingChanged();

    // Adopt-first: when the OFFICIAL Stremio Service is running it already owns
    // :11470 — a child launch just dies on the port clash (the exact silent
    // failure that stalled season downloads, diagnosed 2026-07-05). Probe, and
    // only spawn our own runtime when nobody answers.
    QNetworkRequest probe(QUrl(QStringLiteral("http://127.0.0.1:11470/settings")));
    probe.setTransferTimeout(1500);
    QNetworkReply *r = m_nam->get(probe);
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        r->deleteLater();
        if (r->error() == QNetworkReply::NoError) {
            m_port = 11470;
            setEngineUnavailable(false);
            m_starting = false;
            qInfo("[stream] adopted the running Stremio server on port %d", m_port);
            Q_EMIT readyChanged();
            Q_EMIT startingChanged();
            pushTunedSettings();   // flushes pending once the caps have landed
            return;
        }
        launchChild();
    });
}

void StreamServer::launchChild()
{
    const QString dir = findRuntimeDir();
    if (dir.isEmpty()) {
        m_starting = false;
        Q_EMIT startingChanged();
        markEngineUnavailable(engineUnavailableMessage());
        return;
    }

    const QString cacheDir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/colosseum-stream");
    QDir().mkpath(cacheDir);

    // Windows can reserve the complete 11470-11474 fallback range (Hyper-V/WSL commonly does),
    // leaving the Stremio runtime alive without an HTTP listener. Give our owned child a free
    // ephemeral port and patch every internal 11470 reference in its self-contained bundle.
    // Official Stremio Service adoption remains on its standard 11470 endpoint above.
    QTcpServer portReservation;
    if (!portReservation.listen(QHostAddress::LocalHost, 0)) {
        m_starting = false;
        Q_EMIT startingChanged();
        markEngineUnavailable(engineUnavailableMessage());
        return;
    }
    const int childPort = portReservation.serverPort();
    portReservation.close();
    const QString runtimeScript = prepareRuntimeScript(dir, cacheDir, childPort);
    if (runtimeScript.isEmpty()) {
        qWarning("[stream] could not prepare the runtime script for a dynamic local port");
        m_starting = false;
        Q_EMIT startingChanged();
        markEngineUnavailable(engineUnavailableMessage());
        return;
    }

    QProcessEnvironment penv = QProcessEnvironment::systemEnvironment();
    // Some desktop shells inject Node flags that this bundled runtime rejects at boot.
    penv.remove(QStringLiteral("NODE_OPTIONS"));
    penv.insert(QStringLiteral("NO_HTTPS_SERVER"), QStringLiteral("1"));
    penv.insert(QStringLiteral("APP_PATH"), QDir::toNativeSeparators(cacheDir));

    m_stdoutBuf.clear();
    m_proc = new QProcess(this);
    m_proc->setProcessEnvironment(penv);
    m_proc->setWorkingDirectory(dir);
    m_proc->setProgram(QDir(dir).filePath(runtimeExecutableName()));
    m_proc->setArguments({QDir::toNativeSeparators(runtimeScript)});
    m_proc->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_proc, &QProcess::readyReadStandardOutput, this, &StreamServer::onStdout);
    QProcess *process = m_proc;
    const auto failBeforeReady = [this](QProcess *failedProcess) {
        if (m_proc != failedProcess || !failedProcess || m_port > 0)
            return;
        m_port = -1;
        m_starting = false;
        m_proc->deleteLater();
        m_proc = nullptr;
        Q_EMIT readyChanged();
        Q_EMIT startingChanged();
        if (!m_engineUnavailable)
            markEngineUnavailable(engineUnavailableMessage());
    };

    connect(process, &QProcess::errorOccurred, this,
            [this, process, failBeforeReady](QProcess::ProcessError error) {
        // FailedToStart can report only errorOccurred(), without a finished()
        // signal. Clean up the terminal process here so a later play() can retry.
        if (m_proc == process && process->state() == QProcess::NotRunning) {
            qWarning("[stream] engine failed before ready: %s", qUtf8Printable(process->errorString()));
            failBeforeReady(process);
        } else if (error == QProcess::FailedToStart) {
            qWarning("[stream] engine reported FailedToStart while still active");
        }
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, failBeforeReady](int, QProcess::ExitStatus) {
                if (m_proc != process)
                    return;
                const bool failedBeforeReady = m_port <= 0;
                if (failedBeforeReady) {
                    failBeforeReady(process);
                    return;
                }
                m_port = -1;
                m_starting = false;
                // Reset the handle so a later play() can relaunch the engine. Without this,
                // m_proc stays non-null after the runtime exits (e.g. a port-11470 clash) and
                // ensureStarted()'s `if (m_proc) return` wedges streaming dead until app restart.
                if (m_proc) { m_proc->deleteLater(); m_proc = nullptr; }
                Q_EMIT readyChanged();
                Q_EMIT startingChanged();
            });

    QTimer::singleShot(COLOSSEUM_STREAM_START_TIMEOUT_MS, process, [this, process]() {
        if (m_proc != process || m_port > 0 || process->state() == QProcess::NotRunning)
            return;
        qWarning("[stream] engine startup timed out before opening its HTTP listener");
        process->kill();
    });

    qInfo("[stream] launching %s", qUtf8Printable(dir));
    m_proc->start();
}

void StreamServer::onStdout()
{
    m_stdoutBuf += QString::fromUtf8(m_proc->readAllStandardOutput());

    if (m_port <= 0) {
        // "EngineFS server started at http://127.0.0.1:11470"
        static const QRegularExpression re(
            QStringLiteral("EngineFS server started at http://127\\.0\\.0\\.1:(\\d+)"));
        const auto m = re.match(m_stdoutBuf);
        if (m.hasMatch()) {
            m_port = m.captured(1).toInt();
            setEngineUnavailable(false);
            m_starting = false;
            qInfo("[stream] ready on port %d", m_port);
            Q_EMIT readyChanged();
            Q_EMIT startingChanged();
            pushTunedSettings();   // flushes pending once the caps have landed
        }
    }
    // keep the buffer from growing unbounded once we're up
    if (m_stdoutBuf.size() > 8192)
        m_stdoutBuf = m_stdoutBuf.right(2048);
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
    m_scopedStartup = false;
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
    m_scopedStartup = false;
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

void StreamServer::requestManifest(const QString &requestId, const QString &infoHash)
{
    beginTorrentRequest(requestId, infoHash, -1);
}

void StreamServer::requestFile(const QString &requestId, const QString &infoHash, int fileIdx)
{
    if (fileIdx < 0) {
        cancelRequest(requestId);
        Q_EMIT torrentRequestError(requestId, QStringLiteral("Invalid torrent file index."));
        return;
    }
    beginTorrentRequest(requestId, infoHash, fileIdx);
}

void StreamServer::cancelRequest(const QString &requestId)
{
    const auto request = m_torrentRequests.take(requestId);
    if (!request) return;
    request->deadline->stop();
    request->deadline->deleteLater();
    request->retry->stop();
    request->retry->deleteLater();
    // Removing the token first makes even a synchronous abort completion stale.
    if (request->reply) request->reply->abort();
}

void StreamServer::failTorrentRequest(const QString &id, const QString &message)
{
    if (!m_torrentRequests.contains(id)) return;
    cancelRequest(id);
    Q_EMIT torrentRequestError(id, message);
}

void StreamServer::beginTorrentRequest(const QString &id, const QString &hash, int fileIdx)
{
    cancelRequest(id);
    static const QRegularExpression validHash(QStringLiteral("^[a-fA-F0-9]{40}$"));
    if (id.isEmpty() || !validHash.match(hash).hasMatch()) {
        Q_EMIT torrentRequestError(id, QStringLiteral("Invalid torrent request or info hash."));
        return;
    }
    auto request = QSharedPointer<TorrentRequest>::create();
    request->id = id;
    request->hash = hash.toLower();
    request->fileIdx = fileIdx;
    request->deadline = new QTimer(this);
    request->retry = new QTimer(this);
    request->deadline->setSingleShot(true);
    request->retry->setSingleShot(true);
    m_torrentRequests.insert(id, request);
    connect(request->deadline, &QTimer::timeout, this, [this, request]() {
        if (m_torrentRequests.value(request->id) == request)
            failTorrentRequest(request->id, QStringLiteral("Torrent metadata request timed out."));
    });
    connect(request->retry, &QTimer::timeout, this, [this, request]() {
        registerTorrentRequest(request);
    });
    request->deadline->start(45000);
    if (ready()) registerTorrentRequest(request);
    else {
        if (!m_starting && !m_proc && m_pending.isEmpty()) m_scopedStartup = true;
        ensureStarted();
    }
}

void StreamServer::registerTorrentRequest(const QSharedPointer<TorrentRequest> &request)
{
    if (m_torrentRequests.value(request->id) != request || request->reply) return;
    if (!ready()) { ensureStarted(); return; }
    ++request->attempts;
    const int port = m_port;
    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1/%2/create")
                                .arg(port).arg(request->hash)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(12000);
    QNetworkReply *reply = m_nam->post(req, QByteArrayLiteral("{}"));
    request->reply = reply;
    reply->setReadBufferSize(2 * 1024 * 1024 + 1);
    // Reject oversized metadata during receipt, before reading it into a document.
    connect(reply, &QNetworkReply::readyRead, this, [this, request, reply]() {
        if (m_torrentRequests.value(request->id) == request && reply->bytesAvailable() > 2 * 1024 * 1024)
            failTorrentRequest(request->id, QStringLiteral("Torrent metadata exceeds the size limit."));
    });
    connect(reply, &QNetworkReply::finished, this, [this, request, reply, port]() {
        reply->deleteLater();
        if (m_torrentRequests.value(request->id) != request) return;
        request->reply.clear();
        const QByteArray body = reply->readAll();
        if (body.size() > 2 * 1024 * 1024) {
            failTorrentRequest(request->id, QStringLiteral("Torrent metadata exceeds the size limit."));
            return;
        }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(body, &parseError);
        const auto files = document.object().value(QStringLiteral("files")).toArray();
        if (reply->error() != QNetworkReply::NoError || parseError.error != QJsonParseError::NoError
            || files.isEmpty()) {
            if (request->attempts < 3) request->retry->start(1000);
            else failTorrentRequest(request->id, QStringLiteral("Torrent metadata unavailable after three attempts."));
            return;
        }
        QVariantList manifest;
        for (int i = 0; i < files.size(); ++i) {
            const auto file = files.at(i).toObject();
            const QString name = file.value(QStringLiteral("name")).toString();
            const double length = file.value(QStringLiteral("length")).toDouble(-1);
            if (name.isEmpty() || !std::isfinite(length) || length <= 0
                || length > 9007199254740991.0 || std::floor(length) != length) continue;
            manifest.append(QVariantMap{{QStringLiteral("fileIdx"), i},
                                        {QStringLiteral("name"), name},
                                        {QStringLiteral("path"), file.value(QStringLiteral("path")).toString(name)},
                                        {QStringLiteral("length"), static_cast<qint64>(length)}});
        }
        if (manifest.isEmpty()) {
            failTorrentRequest(request->id, QStringLiteral("Torrent manifest has no valid files."));
            return;
        }
        if (request->fileIdx < 0) {
            cancelRequest(request->id);
            Q_EMIT torrentManifestReady(request->id, request->hash, manifest);
            return;
        }
        bool found = false;
        for (const auto &file : manifest)
            if (file.toMap().value(QStringLiteral("fileIdx")).toInt() == request->fileIdx) found = true;
        if (!found || m_port != port) {
            failTorrentRequest(request->id, QStringLiteral("Selected torrent file is unavailable."));
            return;
        }
        const QString url = streamUrl(request->hash, request->fileIdx);
        cancelRequest(request->id);
        Q_EMIT torrentStreamReady(request->id, url, request->hash, request->fileIdx);
    });
}

void StreamServer::flushPending()
{
    const auto pend = m_pending;
    m_pending.clear();
    for (const Pending &p : pend)
        registerThenReady(p.infoHash, p.fileIdx, p.fetch);
}

void StreamServer::registerThenReady(const QString &infoHash, int fileIdx, bool fetch)
{
    const QString hash = infoHash.toLower();
    const int requestPort = m_port;
    // Register the torrent with the runtime (it constructs the magnet from the hash).
    // We emit the playable URL regardless of the create result — newer runtimes also
    // auto-create on the first ranged GET that mpv issues — but doing the POST first
    // matches TB2's proven sequence and warms the engine before playback.
    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1/%2/create").arg(m_port).arg(hash)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply *reply = m_nam->post(req, QByteArray("{}"));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, hash, fileIdx, fetch, requestPort]() {
        const auto err = reply->error();
        reply->deleteLater();
        // A process handle is not a health signal: the runtime can stay alive after losing
        // its listener. Re-queue this request and retire either an owned or adopted server.
        if (err == QNetworkReply::ConnectionRefusedError) {
            if (m_port > 0 && m_port != requestPort) {
                registerThenReady(hash, fileIdx, fetch);
                return;
            }
            m_pending.append({hash, fileIdx, fetch});
            if (m_port == requestPort)
                recoverDeadServer(requestPort);
            else
                ensureStarted();
            return;
        }
        if (err != QNetworkReply::NoError) {
            qWarning("[stream] create warning: %s", qUtf8Printable(reply->errorString()));
        } else {
            m_consecutiveHealthRestarts = 0;
        }
        const QString url = streamUrl(hash, fileIdx);
        if (url.isEmpty())
            Q_EMIT streamError(QStringLiteral("Stream engine not ready."));
        else if (fetch)
            Q_EMIT fetchReady(url, hash, fileIdx);
        else
            Q_EMIT streamReady(url, hash, fileIdx);
    });
}

void StreamServer::recoverDeadServer(int failedPort)
{
    // A delayed reply from the retired generation must never kill its healthy replacement.
    if (m_port != failedPort)
        return;
    const int deadPort = m_port;
    m_port = -1;
    const bool wasStarting = m_starting;
    m_starting = false;
    Q_EMIT readyChanged();
    if (wasStarting)
        Q_EMIT startingChanged();

    if (m_proc) {
        QProcess *deadProcess = m_proc;
        disconnect(deadProcess, nullptr, this, nullptr);
        m_proc = nullptr;
        connect(deadProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                deadProcess, &QObject::deleteLater);
        deadProcess->kill();
        if (deadProcess->state() == QProcess::NotRunning)
            deadProcess->deleteLater();
    }

    ++m_consecutiveHealthRestarts;
    if (m_consecutiveHealthRestarts > 1) {
        qWarning("[stream] server on :%d stayed unreachable after restart", deadPort);
        markEngineUnavailable(engineUnavailableMessage());
        return;
    }

    qWarning("[stream] server on :%d is unreachable — restarting", deadPort);
    QTimer::singleShot(0, this, &StreamServer::ensureStarted);
}

void StreamServer::pushTunedSettings()
{
    // Swarm tuning (2026-08-02, Hemanth's "maximise the peers"): the runtime's stock caps are
    // sized for a background service — 35 peer connections and a 1.6 / 2.5 MB/s soft/hard
    // download throttle (server.js getDefaults: btMaxConnections, btDownloadSpeed*Limit).
    // Fine for 1080p, but they turn cold-open buffering into a trickle and starve 4K remuxes.
    // POST /settings extends+persists the runtime's settings; engines read the caps at stream
    // CREATE time (getDefaults runs per engine), which is why flushPending() waits for this
    // POST to settle — the first stream of the session must not race the old caps. Values:
    // 200 connections (the same desktop-scale jump our libtorrent side ratified) and
    // 20 / 40 MB/s caps — above any line speed here, so the line itself is the only limit.
    const int requestPort = m_port;
    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1/settings").arg(requestPort)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(3000);
    const QByteArray body = QByteArrayLiteral(
        "{\"btMaxConnections\":200,"
        "\"btDownloadSpeedSoftLimit\":20971520,"
        "\"btDownloadSpeedHardLimit\":41943040}");
    QNetworkReply *r = m_nam->post(req, body);
    connect(r, &QNetworkReply::finished, this, [this, r, requestPort]() {
        const auto err = r->error();
        r->deleteLater();
        if (err == QNetworkReply::ConnectionRefusedError) {
            recoverDeadServer(requestPort);
            return;
        }
        if (err == QNetworkReply::NoError) {
            m_consecutiveHealthRestarts = 0;
            qInfo("[stream] swarm tuning pushed (200 conns, 20/40 MB/s caps)");
        } else {
            qWarning("[stream] swarm tuning push failed (streams keep stock caps): %s",
                     qUtf8Printable(r->errorString()));
        }
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
