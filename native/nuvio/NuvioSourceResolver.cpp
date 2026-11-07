#include "NuvioSourceResolver.h"

#include "NuvioNetwork.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QSet>
#include <QTimer>

#include <limits>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
constexpr int kParallelWorkers = 4;
constexpr int kMaxHeaders = 32;

bool isTmdbId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral(R"(\A[1-9][0-9]{0,9}\z)"));
    return pattern.match(id).hasMatch();
}

QVariantMap streamHeaders(const QJsonObject &stream)
{
    static const QRegularExpression token(QStringLiteral(R"(\A[!#$%&'*+.^_{}|~0-9a-z-]+\z)"));
    static const QSet<QString> excluded = {
        "host", "connection", "proxy-authorization", "proxy-connection",
        "content-length", "transfer-encoding", "upgrade", "range"
    };
    const auto hints = stream.value("behaviorHints").toObject();
    const auto proxy = hints.value("proxyHeaders").toObject();
    const auto proxyRequest = !proxy.contains("request") && !proxy.contains("response")
        ? proxy : proxy.value("request").toObject();
    QVariantMap headers;
    for (const auto &map : {proxyRequest, hints.value("headers").toObject(),
                            stream.value("headers").toObject(), stream.value("requestHeaders").toObject()}) {
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            const auto name = it.key().toLower();
            if (!it.value().isString() || name.size() > 128 || excluded.contains(name)
                || !token.match(name).hasMatch() || (headers.size() >= kMaxHeaders && !headers.contains(name))) {
                continue;
            }
            const auto value = it.value().toString();
            if (value.size() <= 8192 && !value.contains(QLatin1Char('\r'))
                && !value.contains(QLatin1Char('\n')) && !value.contains(QChar::Null)) {
                headers.insert(name, value);
            }
        }
    }
    return headers;
}

QProcessEnvironment workerEnvironment()
{
    const auto system = QProcessEnvironment::systemEnvironment();
    QProcessEnvironment env;
    // Retain runtime loader, locale and OS keyring variables. JS receives its own
    // synthetic process.env containing only the TMDB placeholders.
    for (const auto &name : {"PATH", "SystemRoot", "WINDIR", "TEMP", "TMP", "TMPDIR",
                            "HOME", "LANG", "LC_ALL", "XDG_RUNTIME_DIR",
                            "DBUS_SESSION_BUS_ADDRESS", "LD_LIBRARY_PATH",
                            "QT_PLUGIN_PATH", "SSL_CERT_FILE", "SSL_CERT_DIR"}) {
        const QString key = QString::fromLatin1(name);
        if (system.contains(key))
            env.insert(key, system.value(key));
    }
#ifdef Q_OS_WIN
    if (system.value("COLOSSEUM_DEV") == "1" && system.value("COLOSSEUM_NUVIO_P1_CREDENTIAL") == "1") {
        env.insert("COLOSSEUM_DEV", "1");
        env.insert("COLOSSEUM_NUVIO_P1_CREDENTIAL", "1");
    }
#endif
    return env;
}
}

NuvioSourceResolver::NuvioSourceResolver(NuvioRepositoryStore *store, QString workerPath,
                                         QObject *parent, int processDeadlineMs)
    : QObject(parent), m_store(store), m_workerPath(std::move(workerPath)),
      m_processDeadlineMs(processDeadlineMs),
      m_retryDirectory(QDir::tempPath() + QStringLiteral("/colosseum-nuvio-XXXXXX"))
{
    connect(m_store, &NuvioRepositoryStore::changed, this, [this] {
        const int id = m_search.id;
        if (id) {
            cancel(id);
            emit finished(id);
        }
    });
}

NuvioSourceResolver::~NuvioSourceResolver()
{
    const auto processes = m_jobs.keys();
    for (auto *process : processes) {
        disconnect(process, nullptr, this, nullptr);
        if (process->state() != NuvioProcess::NotRunning)
            process->kill();
        delete process;
    }
}

int NuvioSourceResolver::resolve(const QString &mediaId, const QString &mediaType, int season, int episode)
{
    const QString type = mediaType == "series" ? QStringLiteral("tv") : mediaType;
    if (type != "movie" && type != "tv")
        return 0;

    QString tmdb = mediaId;
    if (tmdb.startsWith("tmdb:"))
        tmdb = tmdb.mid(5);
    QString imdb;
    if (!isTmdbId(tmdb)) {
        tmdb.clear();
        static const QRegularExpression imdbPattern(
            QStringLiteral(R"(\A(tt[0-9]{6,12})(?::([0-9]{1,5}):([0-9]{1,5}))?\z)"));
        const auto match = imdbPattern.match(mediaId);
        if (!match.hasMatch())
            return 0;
        imdb = match.captured(1);
        if (type == "tv" && !match.captured(2).isEmpty()) {
            if (season < 0)
                season = match.captured(2).toInt();
            if (episode < 1)
                episode = match.captured(3).toInt();
        }
        tmdb = m_tmdbIds.value(type + imdb);
    }
    if (type == "movie") {
        season = 1;
        episode = 1;
    }
    if (season < 0 || episode < 1 || season > 99999 || episode > 99999)
        return 0;

    auto plugins = m_store->enabledPlugins(type);
    if (plugins.isEmpty()
#ifndef Q_OS_ANDROID
        || !QFileInfo(m_workerPath).isFile()
#endif
    )
        return 0;

    if (m_search.id) {
        const int previous = m_search.id;
        cancel(previous);
        emit finished(previous);
    }
    if (m_nextSearchId == std::numeric_limits<int>::max())
        m_nextSearchId = 0;
    const int id = ++m_nextSearchId;
    m_search = {id, imdb, tmdb, type, season, episode, false, std::move(plugins)};
    // QML stores the request ID before any completion can arrive.
    QTimer::singleShot(0, this, &NuvioSourceResolver::pump);
    return id;
}

void NuvioSourceResolver::cancel(int requestId)
{
    if (!requestId || m_search.id != requestId)
        return;
    m_search = {};
    const auto processes = m_jobs.keys();
    for (auto *process : processes) {
        if (m_jobs.value(process).searchId == requestId)
            process->kill();
    }
}

void NuvioSourceResolver::pump()
{
    if (m_pumping || !m_search.id)
        return;
    QScopedValueRollback guard(m_pumping, true);

    if (m_search.tmdbId.isEmpty() && !m_search.queued.isEmpty()) {
        if (!m_search.lookingUp && m_jobs.size() < kParallelWorkers) {
            m_search.lookingUp = true;
            startWorker({QStringLiteral("--lookup"), m_search.imdbId, m_search.type,
                         m_retryDirectory.isValid() ? m_retryDirectory.path() : QString()}, true);
        }
        return;
    }

    while (!m_search.queued.isEmpty() && m_jobs.size() < kParallelWorkers) {
        const auto plugin = m_search.queued.takeFirst();
        startWorker({QStringLiteral("--plugin"), plugin.scriptPath, QString::fromLatin1(plugin.sha256),
                     m_search.tmdbId, m_search.type, QString::number(m_search.season),
                     QString::number(m_search.episode),
                     m_retryDirectory.isValid() ? m_retryDirectory.path() : QString()}, false, plugin);
    }
    if (m_search.queued.isEmpty()) {
        for (const auto &job : std::as_const(m_jobs)) {
            if (job.searchId == m_search.id)
                return;
        }
        finishSearch();
    }
}

void NuvioSourceResolver::startWorker(const QStringList &arguments, bool lookup,
                                     const NuvioRepositoryStore::Plugin &plugin)
{
    auto *process = new NuvioProcess(this);
    process->setProgram(m_workerPath);
    process->setArguments(arguments);
    process->setProcessEnvironment(workerEnvironment());
    process->setProcessChannelMode(NuvioProcess::SeparateChannels);
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](NuvioProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    m_jobs.insert(process, {m_search.id, lookup, plugin, {}});
    connect(process, &NuvioProcess::readyReadStandardOutput, this, [this, process] {
        drain(process);
    });
    connect(process, &NuvioProcess::readyReadStandardError, process, [process] {
        process->readAllStandardError();
    });
    connect(process, &NuvioProcess::errorOccurred, this, [this, process](NuvioProcess::ProcessError error) {
        if (error == NuvioProcess::FailedToStart)
            workerFinished(process, false);
    });
    connect(process, &NuvioProcess::finished, this,
            [this, process](int code, NuvioProcess::ExitStatus status) {
        workerFinished(process, code == 0 && status == NuvioProcess::NormalExit);
    });
    QTimer::singleShot(m_processDeadlineMs, process, [this, process] {
        auto job = m_jobs.find(process);
        if (job != m_jobs.end()) {
            job->rejected = true;
            job->timedOut = true;
            process->kill();
        }
    });
#ifdef COLOSSEUM_NUVIO_ANDROID_PROOF
    connect(process, &NuvioProcess::started, this, [this, process] {
        const auto job = m_jobs.constFind(process);
        if (job != m_jobs.cend())
            emit proofWorkerStarted(job->searchId, process->processId(),
                                    process->slotIndex(), process->requestMonotonicMs());
    });
#endif
    process->start(QIODevice::ReadOnly);
}

void NuvioSourceResolver::drain(NuvioProcess *process)
{
    auto job = m_jobs.find(process);
    const auto chunk = process->readAllStandardOutput();
    if (job == m_jobs.end() || job->rejected)
        return;
    if (chunk.size() > NuvioWorkerProtocol::maxOutputBytes + 2 - job->output.size()) {
        job->rejected = true;
        process->kill();
        return;
    }
    job->output += chunk;
}

void NuvioSourceResolver::workerFinished(NuvioProcess *process, bool success)
{
    drain(process);
    auto it = m_jobs.find(process);
    if (it == m_jobs.end())
        return;
    const Job job = std::move(*it);
    m_jobs.erase(it);
#ifdef COLOSSEUM_NUVIO_ANDROID_PROOF
    const auto proofReport = QJsonDocument::fromJson(job.output).object();
    const QString outcome = job.timedOut || proofReport.value("error").toString() == "deadline"
        ? QStringLiteral("deadline") : !success && process->processId() > 0 ? QStringLiteral("crashed")
        : success && !job.rejected && proofReport.value("status").toString() == "OK"
            ? QStringLiteral("ok") : QStringLiteral("failed");
    emit proofWorkerFinished(job.searchId, process->processId(), outcome);
#endif
    process->deleteLater();
    if (m_search.id == job.searchId) {
        const QJsonObject report = success && !job.rejected
            ? QJsonDocument::fromJson(job.output).object() : QJsonObject();
        const QJsonArray streams = report.value("status").toString() == "OK"
            ? report.value("streams").toArray() : QJsonArray();
        if (job.lookup) {
            m_search.lookingUp = false;
            const int id = streams.isEmpty() ? 0 : streams.first().toObject().value("tmdbId").toInt();
            if (id > 0) {
                m_search.tmdbId = QString::number(id);
                if (m_tmdbIds.size() >= 256)
                    m_tmdbIds.clear();
                m_tmdbIds.insert(m_search.type + m_search.imdbId, m_search.tmdbId);
            } else {
                m_search.queued.clear();
            }
        } else {
            const auto rows = directRows(streams, job.plugin);
            if (!rows.isEmpty())
                emit streamsReady(job.searchId, rows);
        }
    }
    // A start failure can complete synchronously while pump is on the stack.
    QTimer::singleShot(0, this, &NuvioSourceResolver::pump);
}

void NuvioSourceResolver::finishSearch()
{
    const int id = m_search.id;
    m_search = {};
    if (id)
        emit finished(id);
}

QVariantList NuvioSourceResolver::directRows(const QJsonArray &streams,
                                             const NuvioRepositoryStore::Plugin &plugin) const
{
    QVariantList rows;
    for (const auto &value : streams) {
        if (rows.size() >= NuvioWorkerProtocol::maxStreams)
            break;
        const auto stream = value.toObject();
        const QString url = stream.value("url").toString();
        if (url.size() > 16384 || !stream.value("infoHash").toString().isEmpty()
            || !NuvioNetwork::publicUrl(QUrl(url, QUrl::StrictMode), true)) {
            continue;
        }
        rows.append(QVariantMap{
            {"url", url},
            {"name", stream.value("name").toString().left(256)},
            {"title", stream.value("title").toString().left(1024)},
            {"pluginName", plugin.name}, {"pluginId", plugin.id}, {"repoId", plugin.repoId},
            {"headers", streamHeaders(stream)}
        });
    }
    return rows;
}
