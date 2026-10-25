#include "nuviostreambackend.h"

#include <QLibrary>

#include <algorithm>

#include <QByteArray>
#include <QDir>
#include <QMetaObject>
#include <functional>
#include <memory>
#include <set>
#include <utility>
#include <vector>
#include <QUrl>

namespace Colosseum::Nuvio {
namespace {

constexpr int kPollIntervalMs = 100;

// Joins threads whose owners must not wait for them. Engine destruction can
// spend seconds collecting resume saves; a destroyed NuvioStreamBackend hands
// that work to a fresh adopted thread and returns immediately, and this
// single process-lifetime reaper joins the adopted threads so nothing is
// leaked or left running unbounded.
class ThreadReaper {
public:
    static ThreadReaper &instance() {
        static ThreadReaper reaper;
        return reaper;
    }

    void adopt(std::thread &&thread) {
        {
            std::lock_guard lock(m_mutex);
            m_adopted.push_back(std::move(thread));
            m_pending = true;
        }
        m_wake.notify_all();
    }

private:
    ThreadReaper() : m_reaper([this] { run(); }) {}

    ~ThreadReaper() {
        {
            std::lock_guard lock(m_mutex);
            m_done = true;
        }
        m_wake.notify_all();
        if (m_reaper.joinable()) {
            m_reaper.join();
        }
    }

    void run() {
        for (;;) {
            std::vector<std::thread> batch;
            {
                std::unique_lock lock(m_mutex);
                m_wake.wait(lock, [&] { return m_done || m_pending; });
                if (m_done && !m_pending) {
                    return;
                }
                batch.swap(m_adopted);
                m_pending = false;
            }
            for (auto &thread : batch) {
                if (thread.joinable()) {
                    thread.join();
                }
            }
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::vector<std::thread> m_adopted;
    bool m_pending = false;
    bool m_done = false;
    std::thread m_reaper;
};

// Only validated engine-published loopback URLs are ever surfaced: a
// hash-formatted guess, a non-loopback binding, or any unsupported URL shape
// (credentials, query, fragment, out-of-range port, non-canonical stream
// token) is rejected outright.
bool isLoopbackStreamUrl(const QString &url) {
    const QUrl parsed(url, QUrl::StrictMode);
    if (!parsed.isValid()) {
        return false;
    }
    if (parsed.scheme() != QStringLiteral("http") ||
        parsed.host() != QStringLiteral("127.0.0.1")) {
        return false;
    }
    const int port = parsed.port(-1);
    if (port < 1 || port > 65535) {
        return false;
    }
    if (!parsed.userInfo().isEmpty() ||
        parsed.hasQuery() ||
        parsed.hasFragment()) {
        return false;
    }
    const auto path = parsed.path();
    const auto prefix = QStringLiteral("/stream/");
    if (!path.startsWith(prefix) || path.size() <= prefix.size()) {
        return false;
    }
    if (path.indexOf(QLatin1Char('/'), prefix.size()) >= 0) {
        return false;
    }
    const auto token = QStringView{path}.mid(prefix.size());
    if (token.size() > 64) {
        // The engine's stream ids are fixed-size hex tokens.
        return false;
    }
    for (const auto character : token) {
        const auto value = character.unicode();
        const bool hex = (value >= '0' && value <= '9') ||
            (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
        if (!hex) {
            return false;
        }
    }
    return true;
}

std::string sessionIdentity(const std::string &diagnostic) {
    constexpr auto key = "session_instance=";
    const auto pos = diagnostic.find(key);
    if (pos == std::string::npos) return {};
    const auto start = pos + std::char_traits<char>::length(key);
    const auto end = diagnostic.find(';', start);
    return diagnostic.substr(start, end - start);
}

QString magnetForInfoHash(const QString &infoHash) {
    return QStringLiteral("magnet:?xt=urn:btih:") + infoHash;
}

} // namespace

struct NuvioStreamBackend::WorkerState : std::enable_shared_from_this<WorkerState> {
    struct DeliveryGate {
        std::mutex mutex;
        NuvioStreamBackend *receiver = nullptr;
    };

    explicit WorkerState(const EngineApi &api, const Configuration &configuration)
        : m_api(api), m_configuration(configuration), m_engineUnavailable(!api.resolves()) {}

    // The mutex serializes off-thread post scheduling with QObject destruction;
    // Qt itself discards queued invocations when their target is destroyed.
    void postToGui(std::function<void(NuvioStreamBackend *)> delivery) {
        auto gate = m_delivery;
        std::lock_guard lock(gate->mutex);
        if (!gate->receiver) return;
        auto self = shared_from_this();
        QMetaObject::invokeMethod(gate->receiver,
            [gate, self, delivery = std::move(delivery)] {
                NuvioStreamBackend *receiver = nullptr;
                {
                    std::lock_guard lock(gate->mutex);
                    receiver = gate->receiver;
                }
                // This lambda executes on the receiver's GUI thread.
                if (receiver) delivery(receiver);
            }, Qt::QueuedConnection);
    }

    struct ActiveStream {
        QString infoHash;
        int fileIdx = -1;
        bool fetch = false;
        QString streamId;
        QString url;
        quint64 generation = 0;
    };

    // One in-flight metadata->prepare chain per torrent. The engine rejects a
    // concurrent second prepare for the same torrent, and its completions
    // carry the originating request ids this struct tracks.
    struct DemandChain {
        QString infoHash;
        int fileIdx = -1;
        bool fetch = false;             // prefetch ownership (fetchReady)
        bool awaitingMetadata = false;  // add_torrent sent, metadata pending
        bool preparing = false;         // prepare_stream sent, URL pending
        bool superseded = false;        // completion must retire silently
        quint64 generation = 0;
        quint64 sharedFetchGeneration = 0; // download also owns a playing prepare
        std::uint64_t addRequestId = 0;
        std::uint64_t prepareRequestId = 0;
    };

    enum class TaskKind { Demand, Quiesce, VerifyIsolation, Stop, StopPrefetch };

    struct Task {
        TaskKind kind = TaskKind::Demand;
        quint64 generation = 0;
        quint64 epoch = 0;
        QString infoHash;
        int fileIdx = -1;
        bool fetch = false;
        QString magnetOverride; // historical task-only magnet seam (disabled in diagnostic profile)
        QByteArray torrentSource; // pinned local .torrent metadata, no MP4 payload
    };

    // A deferred demand is replayed later exactly as it was asked: the whole
    // task (explicit metadata, custom magnet, file, ownership, fences).
    using DeferredDemand = Task;

    void enqueueTask(Task &&task);
    void workerLoop();

    // Worker-thread helpers.
    bool ensureEngineCreated(QString *diagnostic);
    void handleVerifyIsolation();
    std::string readSessionDiagnostics();

    // Task-only isolation evidence: resolved once from the loaded engine
    // library, outside the pinned v3 API table.
    using SessionDiagnosticsFn = std::size_t (*)(const nuvio_engine *, char *, std::size_t);
    SessionDiagnosticsFn sessionDiagnosticsFn = nullptr;
    bool sessionDiagnosticsResolved = false;
    std::string m_verifiedSessionId; // set only by empty-session verify on this instance
    QString engineLibraryPathForDiagnostics;
    void retireStream(const ActiveStream &stream);
    QString retireActiveRoute();
    bool torrentStillOwned(const QString &torrentId);
    void releaseTorrentIfUnowned(const QString &torrentId);
    void handleDemand(Task task);
    void handleStop();
    void handleStopPrefetch();
    void rememberPrefetch(ActiveStream route);
    bool executionAllowed(bool fetch) const {
        return m_foreground.load() || (fetch && m_backgroundDownloadsActive.load());
    }
    bool chainHasCurrentDemand(const DemandChain &chain) const {
        if (executionAllowed(chain.fetch) && chain.generation ==
            (chain.fetch ? m_fetchGeneration.load() : m_generation.load())) return true;
        return !chain.fetch && chain.sharedFetchGeneration != 0 && executionAllowed(true)
            && chain.sharedFetchGeneration == m_fetchGeneration.load();
    }
    void issueChain(const Task &task);
    void issueWaitingDemands(const QString &torrentId);
    void failChain(DemandChain &chain, const QString &message);
    void completeChain(DemandChain &chain, const ActiveStream &route);
    void processEvents();
    void processStats();
    void finishStartup(const QString &diagnostic, quint64 generation);
    void quiesce();

    // GUI-thread state (guarded by m_stateMutex when touched from worker).
    void setReady(bool ready);
    void setStarting(bool starting);
    void setEngineUnavailable(bool unavailable);
    void emitReady(
        const QString &infoHash,
        int fileIdx,
        bool fetch,
        const QString &url,
        quint64 generation
    );

    EngineApi m_api;
    Configuration m_configuration;

    std::thread m_worker;
    std::mutex m_queueMutex;
    std::condition_variable m_queueWake;
    bool m_shutdown = false;
    std::vector<Task> m_queue;

    // Shared between GUI and worker; written under m_queueMutex.
    // m_generation gates foreground (play) results; m_fetchGeneration gates
    // prefetch results, so a prefetch never suppresses an in-flight
    // foreground publication and vice versa.
    std::atomic<quint64> m_generation{0};
    std::atomic<quint64> m_fetchGeneration{0};
    std::atomic<bool> m_foreground{true};
    std::atomic<bool> m_backgroundDownloadsActive{false};
    std::atomic<quint64> m_epoch{0};

    ActiveStream m_active;              // foreground route (m_stateMutex)
    ActiveStream m_prefetchRoute;       // latest prefetch route (m_stateMutex)
    std::map<QString, DemandChain> m_chains;          // worker-only
    std::optional<DeferredDemand> m_deferredPrefetch; // worker-only
    std::vector<DeferredDemand> m_waiting;            // worker-only
    std::set<QString> m_removing;       // worker-only: remove_torrent awaiting TORRENT_REMOVED
    bool m_statsWatched = false;        // m_queueMutex
    QString m_statsInfoHash;
    int m_statsFileIdx = -1;

    mutable std::mutex m_stateMutex;
    nuvio_engine *m_engine = nullptr;   // worker-only after create
    bool m_ready = false;
    bool m_starting = false;
    bool m_engineUnavailable = false;
    std::shared_ptr<DeliveryGate> m_delivery = std::make_shared<DeliveryGate>();
};

NuvioStreamBackend::NuvioStreamBackend(
    const QString &engineLibraryPath,
    const Configuration &configuration,
    QObject *parent
)
    : NuvioStreamBackend(
        loadEngineApi(engineLibraryPath).api,
        configuration,
        parent
    ) {
    m_state->engineLibraryPathForDiagnostics = engineLibraryPath;
}

NuvioStreamBackend::NuvioStreamBackend(
    const EngineApi &injectedApi,
    const Configuration &configuration,
    QObject *parent
) : QObject(parent),
    m_state(std::make_shared<WorkerState>(injectedApi, configuration)) {
    {
        std::lock_guard lock(m_state->m_delivery->mutex);
        m_state->m_delivery->receiver = this;
    }
    if (m_state->m_api.resolves()) {
        m_state->m_worker = std::thread([state = m_state] { state->workerLoop(); });
    }
}

NuvioStreamBackend::~NuvioStreamBackend() {
    auto state = std::move(m_state);
    if (!state) return;
    {
        std::lock_guard lock(state->m_delivery->mutex);
        state->m_delivery->receiver = nullptr;
    }
    state->m_foreground.store(false);
    ++state->m_epoch;
    ++state->m_generation;
    ++state->m_fetchGeneration;
    {
        std::lock_guard lock(state->m_queueMutex);
        state->m_shutdown = true;
        state->m_queue.clear();
    }
    state->m_queueWake.notify_all();
    // No GUI-thread join: worker owns its independent lifetime, including
    // any slow create/add/poll/destroy already in progress.
    if (state->m_worker.joinable())
        ThreadReaper::instance().adopt(std::move(state->m_worker));
}

bool NuvioStreamBackend::available() const { return m_state->m_api.resolves(); }
bool NuvioStreamBackend::ready() const {
    std::lock_guard lock(m_state->m_stateMutex);
    return m_state->m_foreground.load() && m_state->m_ready;
}
bool NuvioStreamBackend::starting() const {
    std::lock_guard lock(m_state->m_stateMutex);
    return m_state->m_foreground.load() && m_state->m_starting;
}
bool NuvioStreamBackend::engineUnavailable() const {
    std::lock_guard lock(m_state->m_stateMutex);
    return m_state->m_engineUnavailable;
}
void NuvioStreamBackend::play(const QString &infoHash, int fileIdx) {
    if (!available()) {
        emit streamError(QStringLiteral("nuvio engine backend is unavailable"));
        return;
    }
    WorkerState::Task task;
    task.kind = WorkerState::TaskKind::Demand;
    task.generation = ++m_state->m_generation;
    task.epoch = m_state->m_epoch.load();
    task.infoHash = infoHash;
    task.fileIdx = fileIdx;
    task.fetch = false;
    m_state->enqueueTask(std::move(task));
}
void NuvioStreamBackend::playMagnet(const QString &magnet, const QString &infoHash, int fileIdx) {
    if (!available()) {
        emit streamError(QStringLiteral("nuvio engine backend is unavailable"));
        return;
    }
    WorkerState::Task task;
    task.kind = WorkerState::TaskKind::Demand;
    task.generation = ++m_state->m_generation;
    task.epoch = m_state->m_epoch.load();
    task.infoHash = infoHash;
    task.fileIdx = fileIdx;
    task.fetch = false;
    task.magnetOverride = magnet;
    m_state->enqueueTask(std::move(task));
}

void NuvioStreamBackend::playTorrentData(
    const QByteArray &data, const QString &infoHash, int fileIdx
) {
    if (!available() || data.isEmpty()) {
        emit streamError(QStringLiteral("nuvio fixture metadata is unavailable"));
        return;
    }
    WorkerState::Task task;
    task.kind = WorkerState::TaskKind::Demand;
    task.generation = ++m_state->m_generation;
    task.epoch = m_state->m_epoch.load();
    task.infoHash = infoHash;
    task.fileIdx = fileIdx;
    task.fetch = false;
    task.torrentSource = data;
    m_state->enqueueTask(std::move(task));
}

void NuvioStreamBackend::verifyIsolation() {
    if (!available()) {
        emit streamError(QStringLiteral("nuvio engine backend is unavailable"));
        return;
    }
    WorkerState::Task task;
    task.kind = WorkerState::TaskKind::VerifyIsolation;
    m_state->enqueueTask(std::move(task));
}

void NuvioStreamBackend::prefetch(const QString &infoHash, int fileIdx) {
    if (!available()) {
        emit streamError(QStringLiteral("nuvio engine backend is unavailable"));
        return;
    }
    WorkerState::Task task;
    task.kind = WorkerState::TaskKind::Demand;
    task.generation = ++m_state->m_fetchGeneration;
    task.epoch = m_state->m_epoch.load();
    task.infoHash = infoHash;
    task.fileIdx = fileIdx;
    task.fetch = true;
    m_state->enqueueTask(std::move(task));
}
void NuvioStreamBackend::stop() {
    if (!available()) return;
    WorkerState::Task task;
    task.kind = WorkerState::TaskKind::Stop;
    // Fences queued foreground publications and in-flight completions.
    task.generation = ++m_state->m_generation;
    task.epoch = m_state->m_epoch.load();
    m_state->enqueueTask(std::move(task));
}
void NuvioStreamBackend::warmUp() {
    // Deliberately lazy: startup warm-up must not start torrent networking.
}
QString NuvioStreamBackend::streamUrl(const QString &infoHash, int fileIdx) const {
    if (!m_state->m_foreground.load()) return {};
    std::lock_guard lock(m_state->m_stateMutex);
    if (!m_state->m_ready) return {};
    const auto &route = m_state->m_active;
    if (route.infoHash == infoHash && route.fileIdx == fileIdx) return route.url;
    return {};
}
void NuvioStreamBackend::watchStats(const QString &infoHash, int fileIdx) {
    std::lock_guard lock(m_state->m_queueMutex);
    m_state->m_statsWatched = true;
    m_state->m_statsInfoHash = infoHash;
    m_state->m_statsFileIdx = fileIdx;
}
void NuvioStreamBackend::unwatchStats() {
    std::lock_guard lock(m_state->m_queueMutex);
    m_state->m_statsWatched = false;
    m_state->m_statsInfoHash.clear();
    m_state->m_statsFileIdx = -1;
}
void NuvioStreamBackend::setForeground(bool foreground) {
    bool notifyReady = false;
    bool notifyStarting = false;
    {
        std::lock_guard lock(m_state->m_stateMutex);
        const bool prior = m_state->m_foreground.load();
        if (prior == foreground) return;
        // The effective Q_PROPERTY getters are foreground-gated. Snapshot
        // their visible values and retire them at the lifecycle boundary,
        // rather than waiting for an asynchronous worker quiesce.
        notifyReady = prior && m_state->m_ready;
        notifyStarting = prior && m_state->m_starting;
        m_state->m_foreground.store(foreground);
        if (!foreground) {
            m_state->m_ready = false;
            m_state->m_starting = false;
        }
    }
    // Fence older queued route/error/stat callbacks before notifying QML.
    ++m_state->m_generation;
    // A download lease preserves its own generation and callbacks across Home.
    // Playback publications are fenced independently, including queued READY.
    const bool keepDownloads = m_state->m_backgroundDownloadsActive.load();
    if (!keepDownloads) {
        ++m_state->m_epoch;
        ++m_state->m_fetchGeneration;
    }
    qInfo() << "[nuvio014] foreground" << !foreground << "->" << foreground << "epoch"
            << m_state->m_epoch.load() << "generation" << m_state->m_generation.load();
    if (notifyReady) emit readyChanged();
    if (notifyStarting) emit startingChanged();
    if (!foreground) {
        WorkerState::Task task;
        task.kind = keepDownloads ? WorkerState::TaskKind::Stop : WorkerState::TaskKind::Quiesce;
        task.epoch = m_state->m_epoch.load();
        m_state->enqueueTask(std::move(task));
    }
}

void NuvioStreamBackend::setBackgroundDownloadsActive(bool active) {
    if (!available()) return;
    if (m_state->m_backgroundDownloadsActive.exchange(active) == active) return;
    if (active) return; // Granting permission is not a networking demand.
    ++m_state->m_fetchGeneration;
    WorkerState::Task task;
    if (m_state->m_foreground.load()) {
        task.kind = WorkerState::TaskKind::StopPrefetch;
    } else {
        ++m_state->m_epoch;
        ++m_state->m_generation;
        task.kind = WorkerState::TaskKind::Quiesce;
    }
    task.epoch = m_state->m_epoch.load();
    m_state->enqueueTask(std::move(task));
}

void NuvioStreamBackend::stopPrefetch() {
    if (!available()) return;
    ++m_state->m_fetchGeneration;
    WorkerState::Task task;
    task.kind = WorkerState::TaskKind::StopPrefetch;
    task.epoch = m_state->m_epoch.load();
    m_state->enqueueTask(std::move(task));
}

void NuvioStreamBackend::WorkerState::enqueueTask(Task &&task) {
    {
        std::lock_guard lock(m_queueMutex);
        m_queue.push_back(std::move(task));
    }
    m_queueWake.notify_all();
}

void NuvioStreamBackend::WorkerState::workerLoop() {
    std::vector<Task> tasks;
    bool shutdown = false;
    while (!shutdown) {
        tasks.clear();
        {
            std::unique_lock lock(m_queueMutex);
            m_queueWake.wait_for(lock, std::chrono::milliseconds(kPollIntervalMs), [&] {
                return m_shutdown || !m_queue.empty();
            });
            shutdown = m_shutdown;
            tasks.swap(m_queue);
        }
        for (auto &task : tasks) {
            switch (task.kind) {
            case TaskKind::Demand:
                handleDemand(task);
                break;
            case TaskKind::Quiesce:
                quiesce();
                break;
            case TaskKind::VerifyIsolation:
                handleVerifyIsolation();
                break;
            case TaskKind::Stop:
                handleStop();
                break;
            case TaskKind::StopPrefetch:
                handleStopPrefetch();
                break;
            }
            if (shutdown) {
                break;
            }
        }
        if (shutdown) {
            break;
        }
        if (m_engine != nullptr) {
            processEvents();
            processStats();
            // Wait for removals and deferred demands before releasing the last
            // owner. An empty session still runs DHT/network timers otherwise.
            bool idle = !m_configuration.requireFixtureIsolation && m_chains.empty()
                && m_waiting.empty() && !m_deferredPrefetch && m_removing.empty();
            if (idle) {
                std::lock_guard lock(m_stateMutex);
                idle = m_active.streamId.isEmpty() && m_prefetchRoute.streamId.isEmpty();
            }
            if (idle) {
                std::lock_guard lock(m_queueMutex);
                idle = std::none_of(m_queue.begin(), m_queue.end(), [](const Task &queued) {
                    return queued.kind == TaskKind::Demand || queued.kind == TaskKind::VerifyIsolation;
                });
            }
            if (idle) quiesce();
        }
    }
    if (m_engine != nullptr) {
        auto *engine = std::exchange(m_engine, nullptr);
        m_api.destroy(engine);
    }
}

bool NuvioStreamBackend::WorkerState::ensureEngineCreated(QString *diagnostic) {
    if (m_engine != nullptr) {
        return true;
    }
    m_verifiedSessionId.clear(); // a newly created session must be reverified
    if (m_configuration.requireFixtureIsolation) {
        // The isolated engine refuses fixture admission over saved state, and a
        // previous session's resume data or cached payload would also bypass the
        // fixture seeder. Every isolated session therefore starts from empty
        // task-owned roots (diagnostic package only; the prior engine, if any,
        // was already destroyed on this worker).
        for (const auto &root : {m_configuration.dataDirectory, m_configuration.cacheDirectory}) {
            if (root.isEmpty()) continue;
            const bool fresh = QDir(root).removeRecursively() && QDir().mkpath(root);
            qInfo() << "[nuvio013] isolated session root reset" << root << fresh;
        }
    }
    const auto data = m_configuration.dataDirectory.toUtf8();
    const auto cache = m_configuration.cacheDirectory.toUtf8();
    const auto caBundle = m_configuration.tlsCaBundlePath.toUtf8();
    nuvio_engine_config config;
    m_api.config_init_sized(&config, sizeof(config));
    config.data_directory = data.constData();
    config.cache_directory = cache.constData();
    config.memory_cache_capacity_bytes = m_configuration.memoryCacheBytes;
    config.disk_cache_capacity_bytes = m_configuration.diskCacheBytes;
    config.listen_port = 0;
    // Routes are retired explicitly (player close, source switch, background
    // quiesce). The engine's idle expiry would otherwise erase the route of a
    // fully buffered file while it still plays, so a later seek, re-buffer or
    // reload received 404 from its own loopback server.
    config.stream_inactivity_timeout_milliseconds = 0;
    config.upload_mode = NUVIO_ENGINE_UPLOAD_DISABLED;
    config.torrent_profile = NUVIO_ENGINE_TORRENT_PROFILE_BALANCED;
    config.tls_ca_bundle_path = caBundle.isEmpty() ? nullptr : caBundle.constData();
    const auto status = m_api.create(&config, &m_engine);
    if (status != NUVIO_ENGINE_STATUS_OK || m_engine == nullptr) {
        m_engine = nullptr;
        if (diagnostic != nullptr) {
            *diagnostic = QString::fromUtf8(m_api.status_message(status));
        }
        return false;
    }
    const auto snapshot = readSessionDiagnostics();
    qInfo() << QStringLiteral("[nuvio011] session created; effective diagnostics: %1")
                   .arg(QString::fromStdString(snapshot));
    if (m_configuration.requireFixtureIsolation &&
        (snapshot.find("isolation_pass=true") == std::string::npos ||
         snapshot.find("torrent_count=0") == std::string::npos ||
         sessionIdentity(snapshot).empty())) {
        m_api.destroy(std::exchange(m_engine, nullptr));
        if (diagnostic != nullptr) *diagnostic = QStringLiteral("new session failed isolation readback");
        return false;
    }
    return true;
}

void NuvioStreamBackend::WorkerState::handleVerifyIsolation() {
    QString diagnostic;
    if (!ensureEngineCreated(&diagnostic)) {
        qWarning() << "[nuvio] isolation-verify: session creation failed:" << diagnostic;
        return;
    }
    const auto current = readSessionDiagnostics();
    m_verifiedSessionId.clear();
    const bool passed = current.find("isolation_pass=true") != std::string::npos &&
                        current.find("torrent_count=0") != std::string::npos;
    if (passed) m_verifiedSessionId = sessionIdentity(current);
    qInfo() << QStringLiteral("[nuvio011] isolation-verify %1; session=%2; %3")
                   .arg(!m_verifiedSessionId.empty() ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
                        QString::fromStdString(m_verifiedSessionId),
                        QString::fromStdString(current));
}

std::string NuvioStreamBackend::WorkerState::readSessionDiagnostics() {
    if (m_engine == nullptr) {
        return {};
    }
    if (!sessionDiagnosticsResolved) {
        sessionDiagnosticsResolved = true;
        QLibrary library(engineLibraryPathForDiagnostics);
        if (library.load()) {
            sessionDiagnosticsFn = reinterpret_cast<SessionDiagnosticsFn>(
                library.resolve("nuvio_engine_session_diagnostics"));
        }
    }
    if (sessionDiagnosticsFn == nullptr) {
        return {"(session diagnostics export unavailable)"};
    }
    char buffer[2048];
    const auto written = sessionDiagnosticsFn(m_engine, buffer, sizeof(buffer));
    return written == 0 ? std::string{"(empty)"} : std::string(buffer, written);
}

void NuvioStreamBackend::WorkerState::retireStream(const ActiveStream &stream) {
    if (m_engine == nullptr || stream.streamId.isEmpty()) {
        return;
    }
    // Best-effort: the engine replaces prior routes for the same torrent, but
    // stale routes must still be retired explicitly.
    std::uint64_t requestId = 0;
    m_api.stop_stream(m_engine, stream.streamId.toUtf8().constData(), &requestId);
}

QString NuvioStreamBackend::WorkerState::retireActiveRoute() {
    ActiveStream retired;
    bool ownedByDownload = false;
    {
        std::lock_guard lock(m_stateMutex);
        retired = m_active;
        if (retired.infoHash.isEmpty()) {
            return {};
        }
        m_active = ActiveStream{};
        ownedByDownload = !m_prefetchRoute.streamId.isEmpty()
            && m_prefetchRoute.streamId == retired.streamId;
    }
    if (!ownedByDownload) retireStream(retired);
    setReady(false);
    // A prefetch deferred behind the retired foreground source may run now.
    if (m_deferredPrefetch.has_value() &&
        m_deferredPrefetch->infoHash == retired.infoHash) {
        Task task = std::move(*m_deferredPrefetch);
        m_deferredPrefetch.reset();
        enqueueTask(std::move(task));
    }
    return retired.infoHash;
}

bool NuvioStreamBackend::WorkerState::torrentStillOwned(const QString &torrentId) {
    if (m_chains.count(torrentId) != 0) return true;
    for (const auto &waiting : m_waiting)
        if (waiting.infoHash == torrentId) return true;
    if (m_deferredPrefetch.has_value() && m_deferredPrefetch->infoHash == torrentId) return true;
    {
        std::lock_guard lock(m_stateMutex);
        if (m_active.infoHash == torrentId || m_prefetchRoute.infoHash == torrentId) return true;
    }
    std::lock_guard lock(m_queueMutex);
    for (const auto &queued : m_queue)
        if (queued.kind == TaskKind::Demand && queued.infoHash == torrentId) return true;
    return false;
}

// stop_stream only retires a loopback route; the torrent itself keeps its
// session demand until it is removed. A torrent no route, chain or queued
// demand owns is removed so closing or replacing playback ends its transfer.
void NuvioStreamBackend::WorkerState::releaseTorrentIfUnowned(const QString &torrentId) {
    if (m_engine == nullptr || torrentId.isEmpty() || torrentStillOwned(torrentId)) return;
    std::uint64_t requestId = 0;
    const auto status =
        m_api.remove_torrent(m_engine, torrentId.toUtf8().constData(), &requestId);
    if (status == NUVIO_ENGINE_STATUS_OK) m_removing.insert(torrentId);
    qInfo() << "[nuvio013] released torrent" << torrentId << "status"
            << QString::fromUtf8(m_api.status_message(status));
}

void NuvioStreamBackend::WorkerState::handleStop() {
    std::vector<QString> released;
    for (auto chain = m_chains.begin(); chain != m_chains.end();) {
        if (!chain->second.fetch) {
            if (chain->second.sharedFetchGeneration != 0 && executionAllowed(true)
                && chain->second.sharedFetchGeneration == m_fetchGeneration.load()) {
                chain->second.fetch = true;
                chain->second.generation = chain->second.sharedFetchGeneration;
                chain->second.sharedFetchGeneration = 0;
                ++chain;
                continue;
            }
            released.push_back(chain->first);
            chain = m_chains.erase(chain);
        } else {
            ++chain;
        }
    }
    m_waiting.erase(std::remove_if(m_waiting.begin(), m_waiting.end(),
        [&](const DeferredDemand &waiting) {
            if (waiting.fetch) return false;
            released.push_back(waiting.infoHash);
            return true;
        }), m_waiting.end());
    const auto retired = retireActiveRoute();
    if (!retired.isEmpty()) released.push_back(retired);
    setStarting(false);
    qInfo() << "[nuvio013] stop: retired route" << retired << "foreground demands"
            << released.size();
    for (const auto &torrentId : released) releaseTorrentIfUnowned(torrentId);
}

void NuvioStreamBackend::WorkerState::handleStopPrefetch() {
    std::vector<QString> released;
    for (auto chain = m_chains.begin(); chain != m_chains.end();) {
        if (chain->second.fetch) {
            released.push_back(chain->first);
            chain = m_chains.erase(chain);
        } else {
            chain->second.sharedFetchGeneration = 0;
            ++chain;
        }
    }
    m_waiting.erase(std::remove_if(m_waiting.begin(), m_waiting.end(),
        [&](const DeferredDemand &waiting) {
            if (!waiting.fetch) return false;
            released.push_back(waiting.infoHash);
            return true;
        }), m_waiting.end());
    if (m_deferredPrefetch) {
        released.push_back(m_deferredPrefetch->infoHash);
        m_deferredPrefetch.reset();
    }
    ActiveStream retired;
    bool ownedByPlayer = false;
    {
        std::lock_guard lock(m_stateMutex);
        retired = m_prefetchRoute;
        m_prefetchRoute = ActiveStream{};
        ownedByPlayer = !m_active.streamId.isEmpty() && m_active.streamId == retired.streamId;
    }
    if (!ownedByPlayer) retireStream(retired);
    if (!retired.infoHash.isEmpty()) released.push_back(retired.infoHash);
    for (const auto &torrentId : released) releaseTorrentIfUnowned(torrentId);
}

void NuvioStreamBackend::WorkerState::rememberPrefetch(ActiveStream route) {
    ActiveStream replaced;
    bool ownedByPlayer = false;
    {
        std::lock_guard lock(m_stateMutex);
        replaced = m_prefetchRoute;
        m_prefetchRoute = route;
        ownedByPlayer = !m_active.streamId.isEmpty() && m_active.streamId == replaced.streamId;
    }
    if (replaced.streamId != route.streamId && !ownedByPlayer) retireStream(replaced);
    if (replaced.infoHash != route.infoHash) releaseTorrentIfUnowned(replaced.infoHash);
}

void NuvioStreamBackend::WorkerState::handleDemand(Task task) {
    if (task.epoch != m_epoch.load() ||
        task.generation != (task.fetch ? m_fetchGeneration.load() : m_generation.load())) {
        qInfo() << "[nuvio014] demand dropped: stale fence" << task.infoHash << "fetch" << task.fetch
                << "epoch" << task.epoch << "/" << m_epoch.load() << "generation" << task.generation
                << "/" << (task.fetch ? m_fetchGeneration.load() : m_generation.load());
        return;
    }
    if (!executionAllowed(task.fetch)) {
        qInfo() << "[nuvio014] demand dropped: backend in background" << task.infoHash
                << "fetch" << task.fetch;
        // Gated on background state; the next explicit foreground demand
        // reconstructs fresh engine state. A background prefetch is dropped
        // silently; a background play reports the suspension.
        if (!task.fetch) {
            // No callback is published after a background lifecycle boundary.
            // The caller will issue a fresh explicit foreground demand.
        }
        return;
    }

    // The engine prepares one file route per torrent. Another file would
    // silently replace the HTTP route a download is still reading.
    if (!task.fetch) {
        bool conflictsWithDownload = false;
        {
            std::lock_guard lock(m_stateMutex);
            conflictsWithDownload = !m_prefetchRoute.url.isEmpty()
                && m_prefetchRoute.infoHash == task.infoHash
                && m_prefetchRoute.fileIdx != task.fileIdx;
        }
        if (conflictsWithDownload) {
            finishStartup(QStringLiteral("This torrent is being downloaded. Pause the download or choose another source."),
                          task.generation);
            return;
        }
    }

    // Exact foreground source already prepared: reuse its route. Re-preparing
    // would make the engine replace the route and interrupt active playback.
    ActiveStream reusedActive;
    {
        std::lock_guard lock(m_stateMutex);
        if (!m_active.url.isEmpty() &&
            m_active.infoHash == task.infoHash &&
            m_active.fileIdx == task.fileIdx) {
            reusedActive = m_active;
        }
    }
    if (!reusedActive.url.isEmpty()) {
        if (task.fetch) {
            reusedActive.fetch = true;
            reusedActive.generation = task.generation;
            rememberPrefetch(reusedActive);
        } else {
            setStarting(false);
            setReady(true);
        }
        emitReady(reusedActive.infoHash, reusedActive.fileIdx, task.fetch,
                  reusedActive.url, task.generation);
        return;
    }

    if (task.fetch) {
        bool conflictsWithActive = false;
        bool prefetchRouteMatches = false;
        ActiveStream reuseFetch;
        {
            std::lock_guard lock(m_stateMutex);
            conflictsWithActive = !m_active.url.isEmpty() &&
                m_active.infoHash == task.infoHash;
            prefetchRouteMatches = !m_prefetchRoute.url.isEmpty() &&
                m_prefetchRoute.infoHash == task.infoHash &&
                m_prefetchRoute.fileIdx == task.fileIdx;
            reuseFetch = m_prefetchRoute;
        }
        // The engine replaces routes for the SAME torrent: preparing another
        // file of the playing torrent now would destroy playback. Foreground
        // playback owns the torrent; hold the prefetch until playback moves.
        if (conflictsWithActive) {
            m_deferredPrefetch = std::move(task);
            return;
        }
        if (prefetchRouteMatches) {
            emitReady(
                reuseFetch.infoHash,
                reuseFetch.fileIdx,
                true,
                reuseFetch.url,
                task.generation
            );
            return;
        }
    }

    // Re-adding a torrent whose removal is still pending would revive the
    // handle being torn down; wait for the engine's TORRENT_REMOVED instead.
    if (m_removing.count(task.infoHash) != 0) {
        const bool fetch = task.fetch;
        m_waiting.push_back(std::move(task));
        if (!fetch) setStarting(true);
        return;
    }

    const auto chainIt = m_chains.find(task.infoHash);
    if (chainIt != m_chains.end()) {
        auto &chain = chainIt->second;
        if (chain.fileIdx == task.fileIdx) {
            if (task.fetch && !chain.fetch) {
                chain.sharedFetchGeneration = task.generation;
                return;
            }
            if (!task.fetch && chain.fetch) {
                // Promote the in-flight prefetch to the foreground start; its
                // Both owners need the result; closing playback must not
                // discard the download waiting for this prepare.
                chain.sharedFetchGeneration = chain.generation;
                chain.fetch = false;
                const auto retiredTorrent = retireActiveRoute();
                if (retiredTorrent != task.infoHash) releaseTorrentIfUnowned(retiredTorrent);
                setStarting(true);
            }
            chain.superseded = false;
            chain.generation = task.generation;
            return;
        }
        // Different file of the same torrent: the engine accepts only one
        // pending prepare per torrent. Retire the old chain's outcome and run
        // this demand when that chain clears.
        chain.superseded = true;
        m_waiting.push_back(std::move(task));
        return;
    }

    if (!task.fetch) {
        const auto retiredTorrent = retireActiveRoute();
        if (retiredTorrent != task.infoHash) releaseTorrentIfUnowned(retiredTorrent);
        // Playing the prefetched source promotes the remembered route
        // instead of re-preparing it (the engine would replace the route).
        bool promoted = false;
        ActiveStream promotedRoute;
        {
            std::lock_guard lock(m_stateMutex);
            if (!m_prefetchRoute.url.isEmpty() &&
                m_prefetchRoute.infoHash == task.infoHash &&
                m_prefetchRoute.fileIdx == task.fileIdx) {
                m_active = m_prefetchRoute;
                m_active.fetch = false;
                m_active.generation = task.generation;
                // Keep the download's independent ownership of the same route.
                promoted = true;
                promotedRoute = m_active;
            }
        }
        if (promoted) {
            setReady(true);
            emitReady(
                promotedRoute.infoHash,
                promotedRoute.fileIdx,
                false,
                promotedRoute.url,
                task.generation
            );
            return;
        }
    }
    issueChain(task);
}

void NuvioStreamBackend::WorkerState::issueChain(const Task &task) {
    QString diagnostic;
    if (!ensureEngineCreated(&diagnostic)) {
        setEngineUnavailable(true);
        if (!task.fetch) {
            finishStartup(
                QStringLiteral("nuvio engine failed to start: ") + diagnostic,
                task.generation
            );
        }
        return;
    }

    // Admission is closed until explicit verification of the identical
    // session, including a real zero-torrent check immediately before demand.
    if (m_configuration.requireFixtureIsolation) {
        const auto current = readSessionDiagnostics();
        const bool accepted = !m_verifiedSessionId.empty() &&
            sessionIdentity(current) == m_verifiedSessionId &&
            current.find("isolation_pass=true") != std::string::npos &&
            current.find("torrent_count=0") != std::string::npos &&
            !task.torrentSource.isEmpty();
        qInfo() << "[nuvio011] admission" << (accepted ? "PASS" : "DENIED")
                << QString::fromStdString(current);
        if (!accepted) {
            if (!task.fetch)
                finishStartup(QStringLiteral("Nuvio fixture admission blocked by isolation gate"),
                              task.generation);
            return;
        }
    }
    const auto magnet = (task.magnetOverride.isEmpty()
        ? magnetForInfoHash(task.infoHash) : task.magnetOverride).toUtf8();
    nuvio_engine_torrent_request request;
    m_api.torrent_request_init_sized(&request, sizeof(request));
    if (!task.torrentSource.isEmpty()) {
        request.source_type = NUVIO_ENGINE_TORRENT_SOURCE_DATA;
        request.torrent_data =
            reinterpret_cast<const std::uint8_t *>(task.torrentSource.constData());
        request.torrent_data_size = static_cast<std::size_t>(task.torrentSource.size());
    } else {
        request.magnet_uri = magnet.constData();
        request.source_type = NUVIO_ENGINE_TORRENT_SOURCE_MAGNET;
    }
    std::uint64_t requestId = 0;
    const auto status = m_api.add_torrent(m_engine, &request, &requestId);
    if (status != NUVIO_ENGINE_STATUS_OK) {
        if (!task.fetch) {
            finishStartup(
                QStringLiteral("nuvio engine rejected the torrent: ") +
                    QString::fromUtf8(m_api.status_message(status)),
                task.generation
            );
        }
        return;
    }
    DemandChain chain;
    chain.infoHash = task.infoHash;
    chain.fileIdx = task.fileIdx;
    chain.fetch = task.fetch;
    chain.awaitingMetadata = true;
    chain.preparing = false;
    chain.superseded = false;
    chain.generation = task.generation;
    chain.addRequestId = requestId;
    chain.prepareRequestId = 0;
    m_chains[task.infoHash] = std::move(chain);
    if (!task.fetch) {
        setStarting(true);
    }
}

void NuvioStreamBackend::WorkerState::issueWaitingDemands(const QString &torrentId) {
    std::vector<DeferredDemand> eligible;
    for (auto entry = m_waiting.begin(); entry != m_waiting.end();) {
        if (entry->infoHash == torrentId) {
            eligible.push_back(*entry);
            entry = m_waiting.erase(entry);
        } else {
            ++entry;
        }
    }
    for (auto &demand : eligible) {
        // Fence against the demand's own generation counter: a deferred
        // prefetch is current against m_fetchGeneration, not m_generation.
        if (demand.epoch != m_epoch.load() ||
            demand.generation != (demand.fetch ? m_fetchGeneration.load() : m_generation.load()))
            continue;
        enqueueTask(std::move(demand));
    }
}

void NuvioStreamBackend::WorkerState::failChain(DemandChain &chain, const QString &message) {
    const bool wasFetch = chain.fetch;
    const auto generation = chain.generation;
    const auto torrentId = chain.infoHash;
    m_chains.erase(torrentId);
    // A failed prefetch is deliberately silent: the user never asked to play
    // it, so it must not raise a player-facing error. Foreground failures
    // surface through streamError (generation-gated on delivery).
    if (!wasFetch) {
        finishStartup(message, generation);
    }
    issueWaitingDemands(torrentId);
    releaseTorrentIfUnowned(torrentId);
}

void NuvioStreamBackend::WorkerState::completeChain(DemandChain &chain, const ActiveStream &route) {
    const auto torrentId = chain.infoHash;
    const auto generation = chain.generation;
    const bool wasFetch = chain.fetch;
    const quint64 fetchGeneration = wasFetch ? generation : chain.sharedFetchGeneration;
    const bool playCurrent = !wasFetch && executionAllowed(false)
        && generation == m_generation.load();
    const bool fetchCurrent = fetchGeneration != 0 && executionAllowed(true)
        && fetchGeneration == m_fetchGeneration.load();
    m_chains.erase(torrentId);
    if (!playCurrent && !fetchCurrent) {
        retireStream(route);
        issueWaitingDemands(torrentId);
        releaseTorrentIfUnowned(torrentId);
        return;
    }
    if (playCurrent) {
        {
            std::lock_guard lock(m_stateMutex);
            m_active = route;
        }
        setStarting(false);
        setReady(true);
    }
    if (fetchCurrent) {
        ActiveStream downloadRoute = route;
        downloadRoute.fetch = true;
        downloadRoute.generation = fetchGeneration;
        rememberPrefetch(downloadRoute);
        emitReady(route.infoHash, route.fileIdx, true, route.url, fetchGeneration);
    }
    if (playCurrent) emitReady(route.infoHash, route.fileIdx, false, route.url, generation);
    issueWaitingDemands(torrentId);
}

void NuvioStreamBackend::WorkerState::processEvents() {
    for (;;) {
        nuvio_engine_event event;
        m_api.event_init_sized(&event, sizeof(event));
        const auto status = m_api.poll_event(m_engine, &event);
        if (status == NUVIO_ENGINE_STATUS_NO_EVENT) {
            return;
        }
        if (status != NUVIO_ENGINE_STATUS_OK) {
            return;
        }
        const auto torrentId = QString::fromUtf8(event.torrent_id);
        const auto chainIt = m_chains.find(torrentId);
        const bool chainExists = chainIt != m_chains.end();
        const bool idsMatchChain = chainExists &&
            (event.request_id == chainIt->second.addRequestId ||
             event.request_id == chainIt->second.prepareRequestId);
        switch (event.type) {
        case NUVIO_ENGINE_EVENT_TORRENT_METADATA_READY: {
            if (!idsMatchChain || !chainIt->second.awaitingMetadata) {
                break;
            }
            auto &chain = chainIt->second;
            size_t fileCount = 0;
            if (m_api.get_file_count(
                    m_engine,
                    event.torrent_id,
                    &fileCount
                ) != NUVIO_ENGINE_STATUS_OK ||
                static_cast<size_t>(chain.fileIdx) >= fileCount) {
                failChain(
                    chain,
                    QStringLiteral("torrent has no file at the requested index")
                );
                break;
            }
            nuvio_engine_file file{};
            file.struct_size = sizeof(file);
            m_api.get_file(
                m_engine,
                event.torrent_id,
                static_cast<size_t>(chain.fileIdx),
                &file
            );
            nuvio_engine_stream_request request;
            m_api.stream_request_init_sized(&request, sizeof(request));
            request.torrent_id = event.torrent_id;
            request.file_index = static_cast<std::uint32_t>(chain.fileIdx);
            request.filename_hint = nullptr;
            std::uint64_t prepareRequestId = 0;
            const auto prepared =
                m_api.prepare_stream(m_engine, &request, &prepareRequestId);
            if (prepared != NUVIO_ENGINE_STATUS_OK) {
                failChain(
                    chain,
                    QStringLiteral("nuvio engine could not prepare the stream: ") +
                        QString::fromUtf8(m_api.status_message(prepared))
                );
                break;
            }
            chain.awaitingMetadata = false;
            chain.preparing = true;
            chain.prepareRequestId = prepareRequestId;
            break;
        }
        case NUVIO_ENGINE_EVENT_STREAM_PREPARED: {
            if (!idsMatchChain) {
                // A route nobody owns anymore (or an old request's route):
                // retire it so it cannot shadow anything.
                retireStream(ActiveStream{
                    {},
                    -1,
                    false,
                    QString::fromUtf8(event.stream_id),
                    {},
                });
                break;
            }
            auto &chain = chainIt->second;
            if (static_cast<int>(event.file_index) != chain.fileIdx) {
                // Belt alongside the request-id correlation: a completion for
                // a different selected file never satisfies this request.
                retireStream(ActiveStream{
                    {},
                    -1,
                    false,
                    QString::fromUtf8(event.stream_id),
                    {},
                });
                break;
            }
            const auto url = QString::fromUtf8(event.stream_url);
            if (!isLoopbackStreamUrl(url)) {
                retireStream(ActiveStream{
                    {},
                    -1,
                    false,
                    QString::fromUtf8(event.stream_id),
                    {},
                });
                failChain(
                    chain,
                    QStringLiteral("nuvio engine published an invalid stream url")
                );
                break;
            }
            if (chain.superseded ||
                !chainHasCurrentDemand(chain)) {
                // Stale request completed after a newer same-class demand;
                // retire its route and suppress every signal.
                retireStream(ActiveStream{
                    {},
                    -1,
                    false,
                    QString::fromUtf8(event.stream_id),
                    {},
                });
                m_chains.erase(chainIt);
                issueWaitingDemands(torrentId);
                releaseTorrentIfUnowned(torrentId);
                break;
            }
            const ActiveStream route{
                chain.infoHash,
                chain.fileIdx,
                chain.fetch,
                QString::fromUtf8(event.stream_id),
                url,
                chain.generation,
            };
            completeChain(chain, route);
            break;
        }
        case NUVIO_ENGINE_EVENT_STREAM_STOPPED: {
            // The engine no longer serves this route: never publish or reuse it.
            const auto stoppedId = QString::fromUtf8(event.stream_id);
            bool activeStopped = false;
            {
                std::lock_guard lock(m_stateMutex);
                if (!stoppedId.isEmpty() && m_active.streamId == stoppedId) {
                    m_active = ActiveStream{};
                    activeStopped = true;
                }
                if (!stoppedId.isEmpty() && m_prefetchRoute.streamId == stoppedId)
                    m_prefetchRoute = ActiveStream{};
            }
            if (activeStopped) {
                qInfo() << "[nuvio013] engine stopped the active route" << stoppedId;
                setReady(false);
            }
            break;
        }
        case NUVIO_ENGINE_EVENT_TORRENT_REMOVED: {
            if (m_removing.erase(torrentId) != 0) issueWaitingDemands(torrentId);
            break;
        }
        case NUVIO_ENGINE_EVENT_TORRENT_ERROR: {
            if (!idsMatchChain) {
                // An error for a request we do not own (retired, foreign, or
                // already superseded) must not fail the current chain.
                break;
            }
            failChain(chainIt->second, QString::fromUtf8(event.message));
            break;
        }
        default:
            break;
        }
    }
}

void NuvioStreamBackend::WorkerState::processStats() {
    QString infoHash;
    int fileIdx = -1;
    {
        std::lock_guard lock(m_queueMutex);
        if (!m_statsWatched) {
            return;
        }
        infoHash = m_statsInfoHash;
        fileIdx = m_statsFileIdx;
    }
    ActiveStream active;
    {
        std::lock_guard lock(m_stateMutex);
        active = m_active;
    }
    if (active.infoHash != infoHash || active.fileIdx != fileIdx ||
        active.streamId.isEmpty()) {
        return;
    }
    nuvio_engine_stats stats;
    m_api.stats_init_sized(&stats, sizeof(stats));
    if (m_api.get_stats(m_engine, &stats) != NUVIO_ENGINE_STATUS_OK) {
        return;
    }
    nuvio_engine_stream_stats engineStreamStats;
    m_api.stream_stats_init_sized(&engineStreamStats, sizeof(engineStreamStats));
    const auto streamId = active.streamId.toUtf8();
    if (m_api.get_stream_stats(m_engine, streamId.constData(), &engineStreamStats) !=
        NUVIO_ENGINE_STATUS_OK) {
        return;
    }
    const auto verified = static_cast<double>(engineStreamStats.verified_file_bytes);
    const auto length = static_cast<double>(engineStreamStats.file_size);
    QVariantMap mapped;
    mapped.insert(QStringLiteral("peers"), static_cast<int>(stats.connected_peers));
    mapped.insert(QStringLiteral("unchoked"), static_cast<int>(stats.unchoked_peers));
    // Verified file bytes only: route creation is never counted as playback
    // progress.
    mapped.insert(
        QStringLiteral("downloaded"),
        static_cast<qulonglong>(engineStreamStats.verified_file_bytes)
    );
    mapped.insert(QStringLiteral("downloadSpeed"), static_cast<double>(stats.download_rate_bytes_per_second));
    mapped.insert(QStringLiteral("streamProgress"), length > 0 ? verified / length : 0.0);
    mapped.insert(QStringLiteral("streamLen"), static_cast<qulonglong>(engineStreamStats.file_size));
    // The route identity travels with the callback: if the route is retired
    // (source switch, backgrounding) before delivery, the stale notification
    // is dropped instead of publishing a retired route.
    const auto epoch = m_epoch.load();
    postToGui([this, infoHash, fileIdx, mapped, streamId = active.streamId,
               epoch](NuvioStreamBackend *receiver) {
        if (!m_foreground.load() || epoch != m_epoch.load()) return;
        {
            std::lock_guard lock(m_stateMutex);
            if (m_active.streamId != streamId || m_active.infoHash != infoHash ||
                m_active.fileIdx != fileIdx) return;
        }
        emit receiver->streamStats(infoHash, fileIdx, mapped);
    });
}

void NuvioStreamBackend::WorkerState::finishStartup(
    const QString &diagnostic, quint64 generation
) {
    setStarting(false);
    const auto epoch = m_epoch.load();
    postToGui([this, diagnostic, generation, epoch](NuvioStreamBackend *receiver) {
        if (!m_foreground.load() || epoch != m_epoch.load() ||
            generation != m_generation.load()) return;
        emit receiver->streamError(diagnostic);
    });
}

void NuvioStreamBackend::WorkerState::quiesce() {
    m_verifiedSessionId.clear();
    {
        std::lock_guard lock(m_stateMutex);
        retireStream(m_active);
        if (m_active.streamId != m_prefetchRoute.streamId) retireStream(m_prefetchRoute);
        m_active = ActiveStream{};
        m_prefetchRoute = ActiveStream{};
    }
    m_chains.clear();
    m_waiting.clear();
    m_removing.clear();
    m_deferredPrefetch.reset();
    if (m_engine != nullptr) {
        // Ordinary background transition: destruction happens here, on the
        // worker; the GUI thread never waits for it.
        m_api.destroy(m_engine);
        m_engine = nullptr;
    }
    setStarting(false);
    setReady(false);
}

void NuvioStreamBackend::WorkerState::setReady(bool ready) {
    bool changed = false;
    {
        std::lock_guard lock(m_stateMutex);
        // A late worker completion must not resurrect a retired property
        // while the player is backgrounded.
        if (ready && !m_foreground.load()) return;
        changed = m_ready != ready;
        m_ready = ready;
    }
    if (changed) {
        const auto epoch = m_epoch.load();
        postToGui([this, epoch](NuvioStreamBackend *receiver) {
            if (epoch == m_epoch.load() && m_foreground.load())
                emit receiver->readyChanged();
        });
    }
}

void NuvioStreamBackend::WorkerState::setStarting(bool starting) {
    bool changed = false;
    {
        std::lock_guard lock(m_stateMutex);
        if (starting && !m_foreground.load()) return;
        changed = m_starting != starting;
        m_starting = starting;
    }
    if (changed) {
        const auto epoch = m_epoch.load();
        postToGui([this, epoch](NuvioStreamBackend *receiver) {
            if (epoch == m_epoch.load() && m_foreground.load())
                emit receiver->startingChanged();
        });
    }
}

void NuvioStreamBackend::WorkerState::setEngineUnavailable(bool unavailable) {
    bool changed = false;
    {
        std::lock_guard lock(m_stateMutex);
        changed = m_engineUnavailable != unavailable;
        m_engineUnavailable = unavailable;
    }
    if (changed)
        postToGui([](NuvioStreamBackend *receiver) {
            emit receiver->engineUnavailableChanged();
        });
}

void NuvioStreamBackend::WorkerState::emitReady(
    const QString &infoHash, int fileIdx, bool fetch,
    const QString &url, quint64 generation
) {
    const auto epoch = m_epoch.load();
    postToGui([this, infoHash, fileIdx, fetch, url, generation, epoch]
              (NuvioStreamBackend *receiver) {
        if (!executionAllowed(fetch) || epoch != m_epoch.load() ||
            generation != (fetch ? m_fetchGeneration.load() : m_generation.load()))
            return;
        {
            std::lock_guard lock(m_stateMutex);
            const auto matches = [&](const ActiveStream &route) {
                return route.infoHash == infoHash && route.fileIdx == fileIdx &&
                       route.url == url && !route.streamId.isEmpty();
            };
            if (!(fetch ? (matches(m_prefetchRoute) || matches(m_active))
                        : matches(m_active))) return;
        }
        if (fetch)
            emit receiver->fetchReady(url, infoHash, fileIdx);
        else
            emit receiver->streamReady(url, infoHash, fileIdx);
    });
}

} // namespace Colosseum::Nuvio
