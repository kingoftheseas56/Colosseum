#pragma once

#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>

namespace server1::media {

enum class HlsV2Mode {
    Local,
    Remote
};

struct ExecutableSearchPaths final {
    QStringList ffmpeg;
    QStringList ffprobe;
    QStringList ffsplit;
};

struct ExecutablePaths final {
    QString ffmpeg;
    QString ffprobe;
    QString ffsplit;
};

class ExecutableLocator final {
public:
    explicit ExecutableLocator(
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment());

    [[nodiscard]] std::optional<QString> locate(const QString &name,
                                                const QStringList &preferred = {}) const;
    [[nodiscard]] ExecutablePaths locateAll(const ExecutableSearchPaths &searchPaths) const;

private:
    QProcessEnvironment environment_;
    mutable ExecutablePaths paths_;
};

struct ProcessSpec final {
    QString program;
    QStringList arguments;
    QProcessEnvironment environment;

    ProcessSpec();
    ProcessSpec(QString program, QStringList arguments,
                QProcessEnvironment environment = QProcessEnvironment::systemEnvironment());
};

struct ProcessResult final {
    quint64 id = 0;
    QByteArray stdoutData;
    QByteArray stderrData;
    int exitCode = -1;
    QProcess::ExitStatus exitStatus = QProcess::NormalExit;
    QProcess::ProcessError error = QProcess::UnknownError;
    QString errorString;
    QString signal;
    bool started = false;
    bool canceled = false;
    bool terminationRequested = false;
    bool forceKilled = false;
};

struct ProcessCallbacks final {
    std::function<void()> onStarted;
    std::function<void(const QByteArray &)> onStdout;
    std::function<void(const QByteArray &)> onStderr;
    std::function<void(const ProcessResult &)> onFinished;
    std::function<void(quint16 port, const QString &url)> onRemoteReady;
};

class ProcessPort : public QObject {
public:
    explicit ProcessPort(QObject *parent = nullptr);
    ~ProcessPort() override;

    Q_DISABLE_COPY_MOVE(ProcessPort)

    [[nodiscard]] quint64 start(const ProcessSpec &spec, ProcessCallbacks callbacks = {});
    [[nodiscard]] bool requestStop(quint64 id, int gracefulTimeoutMs = 250);
    [[nodiscard]] bool cancel(quint64 id);
    [[nodiscard]] bool owns(quint64 id) const;
    [[nodiscard]] bool running(quint64 id) const;
    [[nodiscard]] qsizetype activeCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct RemoteCompletion final {
    int exitCode = -1;
    QString signal;
    QString errorString;
    bool failed = false;
};

struct RemotePollResult final {
    bool ready = false;
    QByteArray data;
    QByteArray stderrData;
    QString errorString;
};

struct RemoteProcessTransport final {
    std::function<std::optional<quint16>(quint16)> reservePort;
    std::function<void(const QString &id, const QString &program,
                       const QStringList &arguments,
                       const QProcessEnvironment &environment,
                       std::function<void(const RemoteCompletion &)> complete)>
        dispatch;
    std::function<void(const QString &id,
                       std::function<void(const RemotePollResult &)> complete)>
        poll;
    std::function<void(const QString &id)> cancel;
};

struct DriverSpec final {
    ProcessSpec process;
    HlsV2Mode mode = HlsV2Mode::Local;
    int remotePollIntervalMs = 500;
};

class ProcessDriver final : public QObject {
public:
    explicit ProcessDriver(ProcessPort &port, RemoteProcessTransport transport = {},
                           QObject *parent = nullptr);
    ~ProcessDriver() override;

    Q_DISABLE_COPY_MOVE(ProcessDriver)

    [[nodiscard]] quint64 start(const DriverSpec &spec, ProcessCallbacks callbacks = {});
    [[nodiscard]] bool disconnect(quint64 id);
    [[nodiscard]] bool cancel(quint64 id);
    [[nodiscard]] bool running(quint64 id) const;
    [[nodiscard]] quint16 port(quint64 id) const;
    [[nodiscard]] QString remoteUrl(quint64 id) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace server1::media
