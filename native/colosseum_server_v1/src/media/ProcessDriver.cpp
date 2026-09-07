#include "server1/ports/ProcessPort.h"

#include <QDateTime>
#include <QHostAddress>
#include <QTcpServer>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <map>
#include <utility>
#include <vector>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <sys/types.h>
#elif defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace server1::media {

namespace {

constexpr qsizetype kMaxRetainedOutputBytes = 64 * 1024;
std::atomic<quint64> nextDriverInstanceId = 1;

struct Child final {
    quint64 id = 0;
    QProcess process;
    ProcessCallbacks callbacks;
    ProcessResult result;
    bool finished = false;
};

struct RemoteChild final {
    quint64 id = 0;
    DriverSpec spec;
    ProcessCallbacks callbacks;
    QString bridgeId;
    QString url;
    quint16 port = 0;
    bool active = true;
    bool ready = false;
    bool completionReceived = false;
    RemoteCompletion completion;
    QByteArray stdoutData;
    QByteArray stderrData;
};

void appendRetained(QByteArray &retained, const QByteArray &bytes)
{
    if (retained.size() >= kMaxRetainedOutputBytes || bytes.isEmpty())
        return;

    const qsizetype remaining = kMaxRetainedOutputBytes - retained.size();
    const qsizetype count = std::min(remaining, static_cast<qsizetype>(bytes.size()));
    retained.append(bytes.constData(), count);
}

quint16 nextRemoteStart()
{
    static quint32 next = 11910;
    next += 10;
    if (next > 65500)
        next = 11920;
    return static_cast<quint16>(next);
}

std::optional<quint16> findAvailablePort(quint16 start)
{
    QTcpServer probe;
    for (quint32 candidate = start; candidate <= 65535; ++candidate) {
        if (probe.listen(QHostAddress::LocalHost, static_cast<quint16>(candidate))) {
            const quint16 selected = probe.serverPort();
            probe.close();
            return selected;
        }
    }
    return std::nullopt;
}

void configureOwnedProcess(QProcess &process)
{
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= CREATE_NEW_PROCESS_GROUP;
    });
#elif defined(Q_OS_UNIX)
    process.setChildProcessModifier([] { ::setpgid(0, 0); });
#else
    Q_UNUSED(process);
#endif
}

void killOwnedProcessTree(QProcess &process)
{
    if (process.state() == QProcess::NotRunning)
        return;

    const qint64 pid = process.processId();
#ifdef Q_OS_WIN
    if (pid > 0) {
        QProcess taskKill;
        taskKill.start(QStringLiteral("taskkill"),
                       {QStringLiteral("/PID"), QString::number(pid),
                        QStringLiteral("/T"), QStringLiteral("/F")});
        if (taskKill.waitForFinished(3000)
            && taskKill.exitStatus() == QProcess::NormalExit
            && taskKill.exitCode() == 0)
            return;
    }
#elif defined(Q_OS_UNIX)
    if (pid > 0 && ::kill(-static_cast<pid_t>(pid), SIGKILL) == 0)
        return;
#else
    Q_UNUSED(pid);
#endif
    process.kill();
}

} // namespace

struct ProcessPort::Impl final {
    ProcessPort *owner = nullptr;
    quint64 nextId = 1;
    std::map<quint64, std::unique_ptr<Child>> children;
    bool shuttingDown = false;

    explicit Impl(ProcessPort *ownerIn)
        : owner(ownerIn)
    {
    }

    Child *child(quint64 id)
    {
        const auto it = children.find(id);
        return it == children.end() ? nullptr : it->second.get();
    }

    void readStdout(Child &child)
    {
        const QByteArray bytes = child.process.readAllStandardOutput();
        if (bytes.isEmpty())
            return;
        appendRetained(child.result.stdoutData, bytes);
        if (child.callbacks.onStdout)
            child.callbacks.onStdout(bytes);
    }

    void readStderr(Child &child)
    {
        const QByteArray bytes = child.process.readAllStandardError();
        if (bytes.isEmpty())
            return;
        appendRetained(child.result.stderrData, bytes);
        if (child.callbacks.onStderr)
            child.callbacks.onStderr(bytes);
    }

    void finish(quint64 id, int exitCode, QProcess::ExitStatus exitStatus)
    {
        Child *current = child(id);
        if (!current || current->finished)
            return;

        readStdout(*current);
        readStderr(*current);
        current->result.exitCode = exitCode;
        current->result.exitStatus = exitStatus;
        if (current->result.signal.isEmpty() && exitStatus == QProcess::CrashExit)
            current->result.signal = QStringLiteral("crash");
        current->finished = true;

        const ProcessResult result = current->result;
        const auto onFinished = current->callbacks.onFinished;
        children.erase(id);
        if (!shuttingDown && onFinished)
            onFinished(result);
    }

    void closeChildren()
    {
        shuttingDown = true;
        for (auto &[id, child] : children) {
            Q_UNUSED(id);
            QObject::disconnect(&child->process, nullptr, owner, nullptr);
            if (child->process.state() != QProcess::NotRunning) {
                killOwnedProcessTree(child->process);
                child->process.waitForFinished(2000);
            }
        }
        children.clear();
    }
};

ProcessSpec::ProcessSpec()
    : environment(QProcessEnvironment::systemEnvironment())
{
}

ProcessSpec::ProcessSpec(QString program, QStringList arguments,
                         QProcessEnvironment environmentIn)
    : program(std::move(program))
    , arguments(std::move(arguments))
    , environment(std::move(environmentIn))
{
}

ProcessPort::ProcessPort(QObject *parent)
    : QObject(parent)
    , impl_(std::make_unique<Impl>(this))
{
}

ProcessPort::~ProcessPort()
{
    impl_->closeChildren();
}

quint64 ProcessPort::start(const ProcessSpec &spec, ProcessCallbacks callbacks)
{
    auto child = std::make_unique<Child>();
    child->id = impl_->nextId++;
    child->callbacks = std::move(callbacks);
    child->result.id = child->id;
    const quint64 id = child->id;
    Child *childPtr = child.get();
    impl_->children.emplace(id, std::move(child));

    QObject::connect(&childPtr->process, &QProcess::started, this, [this, id] {
        Child *current = impl_->child(id);
        if (!current)
            return;
        current->result.started = true;
        if (current->callbacks.onStarted)
            current->callbacks.onStarted();
    });
    QObject::connect(&childPtr->process, &QProcess::readyReadStandardOutput, this, [this, id] {
        if (Child *current = impl_->child(id))
            impl_->readStdout(*current);
    });
    QObject::connect(&childPtr->process, &QProcess::readyReadStandardError, this, [this, id] {
        if (Child *current = impl_->child(id))
            impl_->readStderr(*current);
    });
    QObject::connect(&childPtr->process, &QProcess::errorOccurred, this,
                     [this, id](QProcess::ProcessError error) {
                         Child *current = impl_->child(id);
                         if (!current)
                             return;
                         current->result.error = error;
                         current->result.errorString = current->process.errorString();
                         if (error == QProcess::FailedToStart)
                             impl_->finish(id, -1, QProcess::NormalExit);
                     });
    QObject::connect(&childPtr->process,
                     qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                     [this, id](int exitCode, QProcess::ExitStatus exitStatus) {
                         impl_->finish(id, exitCode, exitStatus);
                     });

    childPtr->process.setProcessEnvironment(spec.environment);
    childPtr->process.setProcessChannelMode(QProcess::SeparateChannels);
    configureOwnedProcess(childPtr->process);
    childPtr->process.start(spec.program, spec.arguments);
    return id;
}

bool ProcessPort::requestStop(quint64 id, int gracefulTimeoutMs)
{
    Child *child = impl_->child(id);
    if (!child || child->process.state() == QProcess::NotRunning)
        return false;

    child->result.terminationRequested = true;
    child->process.terminate();
    QTimer::singleShot(std::max(0, gracefulTimeoutMs), this, [this, id] {
        Child *current = impl_->child(id);
        if (!current || current->process.state() == QProcess::NotRunning)
            return;
        current->result.forceKilled = true;
        current->result.signal = QStringLiteral("SIGKILL");
        killOwnedProcessTree(current->process);
    });
    return true;
}

bool ProcessPort::cancel(quint64 id)
{
    Child *child = impl_->child(id);
    if (!child || child->process.state() == QProcess::NotRunning)
        return false;

    child->result.canceled = true;
    child->result.terminationRequested = true;
    child->result.forceKilled = true;
    child->result.signal = QStringLiteral("SIGKILL");
    killOwnedProcessTree(child->process);
    return true;
}

bool ProcessPort::owns(quint64 id) const
{
    return impl_->children.find(id) != impl_->children.end();
}

bool ProcessPort::running(quint64 id) const
{
    const auto it = impl_->children.find(id);
    return it != impl_->children.end()
        && it->second->process.state() != QProcess::NotRunning;
}

qsizetype ProcessPort::activeCount() const noexcept
{
    return static_cast<qsizetype>(impl_->children.size());
}

struct ProcessDriver::Impl final : std::enable_shared_from_this<ProcessDriver::Impl> {
    ProcessPort *port = nullptr;
    RemoteProcessTransport transport;
    quint64 instanceId = 0;
    quint64 nextId = 1;
    std::map<quint64, std::unique_ptr<RemoteChild>> remotes;
    std::map<quint64, quint64> localProcessIds;
    std::atomic_bool alive = true;

    Impl(ProcessPort *portIn, RemoteProcessTransport transportIn)
        : port(portIn)
        , transport(std::move(transportIn))
        , instanceId(nextDriverInstanceId.fetch_add(1, std::memory_order_relaxed))
    {
    }

    RemoteChild *remote(quint64 id)
    {
        const auto it = remotes.find(id);
        return it == remotes.end() ? nullptr : it->second.get();
    }

    void finishRemote(quint64 id, ProcessResult result)
    {
        RemoteChild *current = remote(id);
        if (!current || !current->active)
            return;
        current->active = false;
        result.id = id;
        result.stdoutData = current->stdoutData;
        result.stderrData = current->stderrData;
        const auto onFinished = current->callbacks.onFinished;
        remotes.erase(id);
        if (onFinished)
            onFinished(result);
    }

    void pollRemote(quint64 id)
    {
        if (!alive.load())
            return;
        RemoteChild *current = remote(id);
        if (!current || !current->active || current->ready || !transport.poll)
            return;

        const QString bridgeId = current->bridgeId;
        const std::weak_ptr<Impl> weak = shared_from_this();
        transport.poll(current->url, [weak, id, bridgeId](const RemotePollResult &pollResult) {
            const auto impl = weak.lock();
            if (!impl || !impl->alive.load())
                return;
            RemoteChild *remoteChild = impl->remote(id);
            if (!remoteChild || !remoteChild->active || remoteChild->bridgeId != bridgeId)
                return;
            if (!pollResult.stderrData.isEmpty()) {
                appendRetained(remoteChild->stderrData, pollResult.stderrData);
                const auto onStderr = remoteChild->callbacks.onStderr;
                if (onStderr)
                    onStderr(pollResult.stderrData);
                remoteChild = impl->remote(id);
                if (!remoteChild || !remoteChild->active || remoteChild->bridgeId != bridgeId)
                    return;
            }
            if (!pollResult.data.isEmpty()) {
                appendRetained(remoteChild->stdoutData, pollResult.data);
                const auto onStdout = remoteChild->callbacks.onStdout;
                if (onStdout)
                    onStdout(pollResult.data);
                remoteChild = impl->remote(id);
                if (!remoteChild || !remoteChild->active || remoteChild->bridgeId != bridgeId)
                    return;
            }
            if (pollResult.ready) {
                remoteChild->ready = true;
                const auto onRemoteReady = remoteChild->callbacks.onRemoteReady;
                if (onRemoteReady)
                    onRemoteReady(remoteChild->port, remoteChild->url);
                remoteChild = impl->remote(id);
                if (!remoteChild || !remoteChild->active || remoteChild->bridgeId != bridgeId)
                    return;
                if (remoteChild->completionReceived) {
                    ProcessResult result;
                    result.started = true;
                    result.exitCode = remoteChild->completion.exitCode;
                    result.signal = remoteChild->completion.signal;
                    result.errorString = remoteChild->completion.errorString;
                    result.error = remoteChild->completion.failed ? QProcess::Crashed : QProcess::UnknownError;
                    impl->finishRemote(id, result);
                }
                return;
            }
            const std::weak_ptr<Impl> nextWeak = impl;
            QTimer::singleShot(std::max(0, remoteChild->spec.remotePollIntervalMs),
                               [nextWeak, id] {
                                   const auto next = nextWeak.lock();
                                   if (next && next->alive.load())
                                       next->pollRemote(id);
                               });
        });
    }
};

ProcessDriver::ProcessDriver(ProcessPort &port, RemoteProcessTransport transport, QObject *parent)
    : QObject(parent)
    , impl_(std::make_shared<Impl>(&port, std::move(transport)))
{
}

ProcessDriver::~ProcessDriver()
{
    const auto impl = std::move(impl_);
    impl->alive.store(false);

    const std::vector<quint64> remoteIds = [&] {
        std::vector<quint64> ids;
        ids.reserve(impl->remotes.size());
        for (const auto &[id, remote] : impl->remotes) {
            Q_UNUSED(remote);
            ids.push_back(id);
        }
        return ids;
    }();
    for (const quint64 id : remoteIds) {
        if (RemoteChild *remote = impl->remote(id); remote && remote->active) {
            if (impl->transport.cancel)
                impl->transport.cancel(remote->bridgeId);
            remote->active = false;
            impl->remotes.erase(id);
        }
    }

    std::vector<quint64> localProcessIds;
    localProcessIds.reserve(impl->localProcessIds.size());
    for (const auto &[driverId, processId] : impl->localProcessIds) {
        Q_UNUSED(driverId);
        localProcessIds.push_back(processId);
    }
    for (const quint64 processId : localProcessIds)
        (void)impl->port->cancel(processId);
    impl->localProcessIds.clear();
}

quint64 ProcessDriver::start(const DriverSpec &spec, ProcessCallbacks callbacks)
{
    const quint64 driverId = impl_->nextId++;
    if (spec.mode == HlsV2Mode::Local) {
        const std::weak_ptr<Impl> weak = impl_;
        ProcessCallbacks localCallbacks;
        localCallbacks.onStarted = [weak, started = std::move(callbacks.onStarted)]() mutable {
            const auto impl = weak.lock();
            if (!impl || !impl->alive.load())
                return;
            if (started)
                started();
        };
        localCallbacks.onStdout = [weak, stdoutCallback = std::move(callbacks.onStdout)](
                                       const QByteArray &bytes) mutable {
            const auto impl = weak.lock();
            if (!impl || !impl->alive.load())
                return;
            if (stdoutCallback)
                stdoutCallback(bytes);
        };
        localCallbacks.onStderr = [weak, stderrCallback = std::move(callbacks.onStderr)](
                                       const QByteArray &bytes) mutable {
            const auto impl = weak.lock();
            if (!impl || !impl->alive.load())
                return;
            if (stderrCallback)
                stderrCallback(bytes);
        };
        localCallbacks.onRemoteReady = [weak, ready = std::move(callbacks.onRemoteReady)](
                                           quint16 port, const QString &url) mutable {
            const auto impl = weak.lock();
            if (!impl || !impl->alive.load())
                return;
            if (ready)
                ready(port, url);
        };
        localCallbacks.onFinished = [weak, driverId,
                                     finished = std::move(callbacks.onFinished)](
                                        const ProcessResult &result) mutable {
            const auto impl = weak.lock();
            if (!impl || !impl->alive.load())
                return;
            ProcessResult mapped = result;
            mapped.id = driverId;
            impl->localProcessIds.erase(driverId);
            if (finished)
                finished(mapped);
        };
        const quint64 processId = impl_->port->start(spec.process, std::move(localCallbacks));
        impl_->localProcessIds.emplace(driverId, processId);
        return driverId;
    }

    auto remoteChild = std::make_unique<RemoteChild>();
    remoteChild->id = driverId;
    remoteChild->spec = spec;
    remoteChild->callbacks = std::move(callbacks);
    remoteChild->bridgeId = QStringLiteral("process-driver-%1-%2")
                                .arg(impl_->instanceId)
                                .arg(driverId);
    const quint16 requestedStart = nextRemoteStart();
    const auto selected = impl_->transport.reservePort
        ? impl_->transport.reservePort(requestedStart)
        : findAvailablePort(requestedStart);
    if (!impl_->transport.dispatch || !impl_->transport.poll) {
        ProcessResult result;
        result.id = driverId;
        result.error = QProcess::FailedToStart;
        result.errorString = QStringLiteral("remote transport is incomplete");
        if (remoteChild->callbacks.onFinished)
            remoteChild->callbacks.onFinished(result);
        return driverId;
    }

    // The source keeps the requested port when portfinder cannot reserve one,
    // then dispatches the bridge with that deterministic fallback.
    remoteChild->port = selected.value_or(requestedStart);
    remoteChild->url = QStringLiteral("http://127.0.0.1:%1/%2.mp4")
                           .arg(remoteChild->port)
                           .arg(QDateTime::currentMSecsSinceEpoch());
    QStringList arguments = spec.process.arguments;
    arguments << QStringLiteral("-listen") << QStringLiteral("1") << remoteChild->url;
    const QString bridgeId = remoteChild->bridgeId;
    const QString program = spec.process.program;
    const QProcessEnvironment environment = spec.process.environment;
    impl_->remotes.emplace(driverId, std::move(remoteChild));
    if (RemoteChild *current = impl_->remote(driverId); current && current->callbacks.onStarted)
        current->callbacks.onStarted();
    if (!impl_->remote(driverId))
        return driverId;
    const std::weak_ptr<Impl> weak = impl_;
    impl_->transport.dispatch(bridgeId, program, arguments, environment,
                              [weak, driverId, bridgeId](const RemoteCompletion &completion) {
                                  const auto impl = weak.lock();
                                  if (!impl || !impl->alive.load())
                                      return;
                                  RemoteChild *current = impl->remote(driverId);
                                  if (!current || !current->active || current->bridgeId != bridgeId)
                                      return;
                                  current->completionReceived = true;
                                  current->completion = completion;
                                  if (completion.failed || current->ready) {
                                      if (completion.failed && impl->transport.cancel)
                                          impl->transport.cancel(current->bridgeId);
                                      ProcessResult result;
                                      result.started = true;
                                      result.exitCode = completion.exitCode;
                                      result.signal = completion.signal;
                                      result.error = completion.failed ? QProcess::Crashed
                                                                       : QProcess::UnknownError;
                                      result.errorString = completion.errorString;
                                       impl->finishRemote(driverId, result);
                                  }
                              });
    QTimer::singleShot(std::max(0, spec.remotePollIntervalMs),
                       [weak, driverId] {
                           const auto impl = weak.lock();
                           if (impl && impl->alive.load())
                               impl->pollRemote(driverId);
                       });
    return driverId;
}

bool ProcessDriver::disconnect(quint64 id)
{
    if (const auto local = impl_->localProcessIds.find(id); local != impl_->localProcessIds.end()) {
        const bool canceled = impl_->port->cancel(local->second);
        impl_->localProcessIds.erase(local);
        return canceled;
    }

    RemoteChild *remoteChild = impl_->remote(id);
    if (!remoteChild || !remoteChild->active)
        return false;
    if (impl_->transport.cancel)
        impl_->transport.cancel(remoteChild->bridgeId);
    ProcessResult result;
    result.id = id;
    result.started = true;
    result.canceled = true;
    result.terminationRequested = true;
    impl_->finishRemote(id, result);
    return true;
}

bool ProcessDriver::cancel(quint64 id)
{
    return disconnect(id);
}

bool ProcessDriver::running(quint64 id) const
{
    if (const auto local = impl_->localProcessIds.find(id); local != impl_->localProcessIds.end())
        return impl_->port->running(local->second);
    const auto remoteChild = impl_->remotes.find(id);
    return remoteChild != impl_->remotes.end() && remoteChild->second->active;
}

quint16 ProcessDriver::port(quint64 id) const
{
    const auto remoteChild = impl_->remotes.find(id);
    return remoteChild == impl_->remotes.end() ? 0 : remoteChild->second->port;
}

QString ProcessDriver::remoteUrl(quint64 id) const
{
    const auto remoteChild = impl_->remotes.find(id);
    return remoteChild == impl_->remotes.end() ? QString() : remoteChild->second->url;
}

} // namespace server1::media
