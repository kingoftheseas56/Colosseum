#include "streamserver.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace {

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

} // namespace

StreamServer::StreamServer(QObject *parent)
    : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

StreamServer::~StreamServer()
{
    if (m_proc) {
        m_proc->kill();
        m_proc->waitForFinished(2000);
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
    m_pending.clear();
    setEngineUnavailable(true);
    Q_EMIT streamError(message);
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

    QProcessEnvironment penv = QProcessEnvironment::systemEnvironment();
    // Some desktop shells inject Node flags that this bundled runtime rejects at boot.
    penv.remove(QStringLiteral("NODE_OPTIONS"));
    penv.insert(QStringLiteral("NO_HTTPS_SERVER"), QStringLiteral("1"));
    penv.insert(QStringLiteral("APP_PATH"), QDir::toNativeSeparators(cacheDir));

    m_proc = new QProcess(this);
    m_proc->setProcessEnvironment(penv);
    m_proc->setWorkingDirectory(dir);
    m_proc->setProgram(QDir(dir).filePath(runtimeExecutableName()));
    m_proc->setArguments({QStringLiteral("server.js")});
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
        registerThenReady(p.infoHash, p.fileIdx, p.fetch);
}

void StreamServer::registerThenReady(const QString &infoHash, int fileIdx, bool fetch)
{
    const QString hash = infoHash.toLower();
    // Register the torrent with the runtime (it constructs the magnet from the hash).
    // We emit the playable URL regardless of the create result — newer runtimes also
    // auto-create on the first ranged GET that mpv issues — but doing the POST first
    // matches TB2's proven sequence and warms the engine before playback.
    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1/%2/create").arg(m_port).arg(hash)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply *reply = m_nam->post(req, QByteArray("{}"));
    connect(reply, &QNetworkReply::finished, this, [this, reply, hash, fileIdx, fetch]() {
        const auto err = reply->error();
        reply->deleteLater();
        // DEAD-ADOPT SELF-HEAL (2026-07-18): when the engine was ADOPTED (no child of ours,
        // m_proc null) and the port now refuses connections, the external server died out
        // from under us. The child path resets via its finished() handler, but adoption had
        // NO reset — m_port stayed set forever and every later create/manifest hit a dead
        // port (audiobook 'retry' failed deterministically). Reset, re-queue THIS request,
        // and run the ensureStarted probe again — which now spawns our own runtime.
        if (err == QNetworkReply::ConnectionRefusedError && !m_proc && m_port > 0) {
            qWarning("[stream] adopted server on :%d is gone — resetting and relaunching", m_port);
            m_port = -1;
            Q_EMIT readyChanged();
            m_pending.append({hash, fileIdx, fetch});
            ensureStarted();
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
    // Swarm tuning (2026-08-02, Hemanth's "maximise the peers"): the runtime's stock caps are
    // sized for a background service — 35 peer connections and a 1.6 / 2.5 MB/s soft/hard
    // download throttle (server.js getDefaults: btMaxConnections, btDownloadSpeed*Limit).
    // Fine for 1080p, but they turn cold-open buffering into a trickle and starve 4K remuxes.
    // POST /settings extends+persists the runtime's settings; engines read the caps at stream
    // CREATE time (getDefaults runs per engine), which is why flushPending() waits for this
    // POST to settle — the first stream of the session must not race the old caps. Values:
    // 200 connections (the same desktop-scale jump our libtorrent side ratified) and
    // 20 / 40 MB/s caps — above any line speed here, so the line itself is the only limit.
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
            qWarning("[stream] swarm tuning push failed (streams keep stock caps): %s",
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
