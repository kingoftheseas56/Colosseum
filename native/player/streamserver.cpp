#include "streamserver.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

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

QString StreamServer::findRuntimeDir() const
{
    const QString exe = QStringLiteral("stremio-runtime.exe");
    const QString appDir = QCoreApplication::applicationDirPath();

    QStringList candidates;
    // 1) explicit override
    const QString env = qEnvironmentVariable("COLOSSEUM_STREAM_SERVER");
    if (!env.isEmpty())
        candidates << env;
    // 2) shipped next to the Colosseum exe (self-contained copy, gitignored)
    candidates << appDir + QStringLiteral("/stream_server");
    candidates << appDir + QStringLiteral("/../stream_server");
    // 3) the OFFICIAL Stremio Service install — the canonical server on this machine
    //    (Hemanth's call 2026-07-05: the vendored copy fell behind; official wins)
    candidates << QStringLiteral("C:/Users/Suprabha/AppData/Local/Programs/StremioService");
    // 4) fall back to Tankoban 2's vendored runtime
    candidates << QStringLiteral("C:/Users/Suprabha/Desktop/Tankoban 2/resources/stream_server");

    for (const QString &dir : candidates) {
        if (QFileInfo::exists(dir + QLatin1Char('/') + exe))
            return QDir(dir).absolutePath();
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
            m_starting = false;
            qInfo("[stream] adopted the running Stremio server on port %d", m_port);
            Q_EMIT readyChanged();
            Q_EMIT startingChanged();
            flushPending();
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
        Q_EMIT streamError(QStringLiteral("Stream engine not found (stremio-runtime.exe missing)."));
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
    m_proc->setProgram(dir + QStringLiteral("/stremio-runtime.exe"));
    m_proc->setArguments({QStringLiteral("server.js")});
    m_proc->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_proc, &QProcess::readyReadStandardOutput, this, &StreamServer::onStdout);
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_port <= 0 && m_proc)
            Q_EMIT streamError(QStringLiteral("Stream engine failed to start: %1").arg(m_proc->errorString()));
    });
    connect(m_proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
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
            m_starting = false;
            qInfo("[stream] ready on port %d", m_port);
            Q_EMIT readyChanged();
            Q_EMIT startingChanged();
            flushPending();
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

#include "moc_streamserver.cpp"
