#include "server1/ports/ProcessPort.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

using namespace server1::media;

constexpr qsizetype sourceVisibleOutputRetentionLimit = 64 * 1024;

void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

template <typename Predicate>
void waitUntil(Predicate predicate, int timeoutMs, const char *message)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (timer.elapsed() > timeoutMs)
            throw std::runtime_error(message);
    }
}

struct ScriptCommand final {
    QString program;
    QStringList prefixArguments;
};

ScriptCommand pythonCommand()
{
    for (const auto &name : {QStringLiteral("python"), QStringLiteral("python3")}) {
        const QString executable = QStandardPaths::findExecutable(name);
        if (!executable.isEmpty())
            return {executable, {}};
    }

    const QString launcher = QStandardPaths::findExecutable(QStringLiteral("py"));
    if (!launcher.isEmpty())
        return {launcher, {QStringLiteral("-3")}};

    throw std::runtime_error("python interpreter is required for the scripted child fixture");
}

QString writeFixture(const QTemporaryDir &directory)
{
    const QString path = QDir(directory.path()).filePath(QStringLiteral("m00_child_fixture.py"));
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "cannot write child fixture");
    file.write(R"PY(import os
import signal
import subprocess
import sys
import time

mode = sys.argv[1]
if mode == "binary":
    sys.stdout.buffer.write(b"\x00OUT\xffBIN")
    sys.stdout.buffer.flush()
    sys.stderr.buffer.write(b"\x00ERR\xfe")
    sys.stderr.buffer.flush()
    sys.exit(23)
elif mode == "large-output":
    sys.stdout.buffer.write(b"O" * (70 * 1024))
    sys.stdout.buffer.flush()
    sys.stderr.buffer.write(b"E" * (70 * 1024))
    sys.stderr.buffer.flush()
    sys.exit(0)
elif mode == "environment":
    sys.stdout.buffer.write(os.environ.get("M00_CHILD_ENV", "missing").encode("utf-8"))
    sys.stdout.buffer.flush()
    sys.exit(0)
elif mode == "ignore-term":
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, lambda _signum, _frame: None)
    while True:
        time.sleep(0.05)
elif mode == "sleep":
    time.sleep(10)
elif mode == "pending-output":
    sys.stdout.buffer.write(b"pending-out")
    sys.stdout.buffer.flush()
    sys.stderr.buffer.write(b"pending-err")
    sys.stderr.buffer.flush()
    while True:
        time.sleep(0.05)
elif mode == "spawn-grandchild":
    child = subprocess.Popen([sys.executable, __file__, "sleep"])
    sys.stdout.buffer.write(str(child.pid).encode("ascii"))
    sys.stdout.buffer.flush()
    while True:
        time.sleep(0.05)
elif mode == "probe":
    pid = int(sys.argv[2])
    deadline = time.time() + 3
    while time.time() < deadline:
        try:
            os.kill(pid, 0)
            if os.name == "posix":
                try:
                    with open(f"/proc/{pid}/stat", "r", encoding="ascii") as stat:
                        if stat.read().split()[2] == "Z":
                            break
                except (FileNotFoundError, IndexError):
                    break
        except OSError:
            break
        time.sleep(0.05)
    else:
        raise SystemExit(1)
else:
    raise SystemExit("unknown fixture mode")
)PY");
    file.close();
    return path;
}

ProcessSpec fixtureSpec(const ScriptCommand &python, const QString &fixture, const QString &mode)
{
    ProcessSpec spec;
    spec.program = python.program;
    spec.arguments = python.prefixArguments;
    spec.arguments << fixture << mode;
    spec.environment = QProcessEnvironment::systemEnvironment();
    return spec;
}

void caseM00_01()
{
    QTemporaryDir directory;
    require(directory.isValid(), "temporary directory unavailable");

    const QString executable = QCoreApplication::applicationFilePath();
    const QString unusualDirectory = QDir(directory.path()).filePath(QStringLiteral("space Δ"));
    require(QDir().mkpath(unusualDirectory), "cannot create unusual executable directory");
    const QString pathCandidate = QDir(unusualDirectory).filePath(QStringLiteral("fixture tool.exe"));
    require(QFile::copy(executable, pathCandidate), "cannot create PATH executable candidate");

    const QString overrideCandidate = QDir(unusualDirectory).filePath(QStringLiteral("override tool.exe"));
    require(QFile::copy(executable, overrideCandidate), "cannot create override executable candidate");
    const QString environmentCandidate = QDir(unusualDirectory).filePath(QStringLiteral("environment tool.exe"));
    require(QFile::copy(executable, environmentCandidate),
            "cannot create environment executable candidate");

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), unusualDirectory);
    environment.insert(QStringLiteral("FFMPEG_BIN"), environmentCandidate);
    ExecutableLocator locator(environment);

    const QString missing = QDir(unusualDirectory).filePath(QStringLiteral("missing.exe"));
    const auto overridden = locator.locate(QStringLiteral("fixture tool.exe"), {overrideCandidate});
    require(overridden && *overridden == overrideCandidate,
            "present override must win over PATH lookup");

    const auto environmentLocated = locator.locate(
        QStringLiteral("fixture tool.exe"),
        {missing, environment.value(QStringLiteral("FFMPEG_BIN")), pathCandidate});
    require(environmentLocated && *environmentLocated == environmentCandidate,
            "environment override must win after argv override and before PATH lookup");

    const auto pathLocated = locator.locate(QStringLiteral("fixture tool.exe"), {missing});
    require(pathLocated && *pathLocated == pathCandidate,
            "missing override must fall through to PATH lookup");

    const QString directoryCandidate = QDir(unusualDirectory).filePath(QStringLiteral("not-launchable"));
    require(QDir().mkpath(directoryCandidate), "cannot create non-file candidate");
    QProcessEnvironment noPath = environment;
    noPath.insert(QStringLiteral("PATH"), QString());
    ExecutableLocator noPathLocator(noPath);
    const auto directoryLocated = noPathLocator.locate(QStringLiteral("not-launchable"), {directoryCandidate});
    require(directoryLocated && *directoryLocated == directoryCandidate,
            "accessible directories must remain executable candidates like the source locator");

    const QString textCandidate = QDir(unusualDirectory).filePath(QStringLiteral("refuses.txt"));
    QFile textFile(textCandidate);
    require(textFile.open(QIODevice::WriteOnly), "cannot create launch-refusal candidate");
    textFile.write("not an executable");
    textFile.close();

    ProcessPort port;
    ProcessSpec refusal;
    refusal.program = textCandidate;
    refusal.environment = environment;
    std::optional<ProcessResult> result;
    (void)port.start(refusal, ProcessCallbacks {
        {},
        {},
        {},
        [&](const ProcessResult &finished) { result = finished; },
        {}
    });
    waitUntil([&] { return result.has_value(); }, 3000, "launch refusal did not complete");
    require(!result->started, "non-executable launch must not report started");
    require(result->error == QProcess::FailedToStart,
            "non-executable launch must expose FailedToStart");

    std::cout << "M00-01 PASS\n";
}

void caseM00_02()
{
    QTemporaryDir directory;
    require(directory.isValid(), "temporary directory unavailable");
    const ScriptCommand python = pythonCommand();
    const QString fixture = writeFixture(directory);

    ProcessPort binaryPort;
    ProcessSpec binary = fixtureSpec(python, fixture, QStringLiteral("binary"));
    std::optional<ProcessResult> binaryResult;
    (void)binaryPort.start(binary, ProcessCallbacks {
        {},
        {},
        {},
        [&](const ProcessResult &finished) { binaryResult = finished; },
        {}
    });
    waitUntil([&] { return binaryResult.has_value(); }, 5000, "binary fixture did not complete");

    QByteArray expectedStdout;
    expectedStdout.append('\0');
    expectedStdout.append("OUT", 3);
    expectedStdout.append(char(0xff));
    expectedStdout.append("BIN", 3);
    QByteArray expectedStderr;
    expectedStderr.append('\0');
    expectedStderr.append("ERR", 3);
    expectedStderr.append(char(0xfe));
    require(binaryResult->started, "binary fixture must report started");
    require(binaryResult->stdoutData == expectedStdout, "stdout bytes must be collected losslessly");
    require(binaryResult->stderrData == expectedStderr, "stderr bytes must be collected losslessly");
    require(binaryResult->exitCode == 23, "unsuccessful exit code must be preserved");
    require(binaryResult->exitStatus == QProcess::NormalExit, "normal unsuccessful exit must stay normal");
    require(!binaryResult->canceled, "ordinary exit must not be marked canceled");

    ProcessPort boundedPort;
    ProcessSpec largeOutput = fixtureSpec(python, fixture, QStringLiteral("large-output"));
    std::optional<ProcessResult> boundedResult;
    qsizetype callbackStdoutBytes = 0;
    qsizetype callbackStderrBytes = 0;
    (void)boundedPort.start(largeOutput, ProcessCallbacks {
        {},
        [&](const QByteArray &bytes) { callbackStdoutBytes += bytes.size(); },
        [&](const QByteArray &bytes) { callbackStderrBytes += bytes.size(); },
        [&](const ProcessResult &finished) { boundedResult = finished; },
        {}
    });
    waitUntil([&] { return boundedResult.has_value(); }, 5000,
              "large-output fixture did not complete");
    require(callbackStdoutBytes == 70 * 1024,
            "stdout callback must receive the complete source stream");
    require(callbackStderrBytes == 70 * 1024,
            "stderr callback must receive the complete source stream");
    require(boundedResult->stdoutData.size() <= sourceVisibleOutputRetentionLimit,
            "retained stdout must have a bounded size");
    require(boundedResult->stderrData.size() <= sourceVisibleOutputRetentionLimit,
            "retained stderr must have a bounded size");

    ProcessPort environmentPort;
    ProcessSpec environment = fixtureSpec(python, fixture, QStringLiteral("environment"));
    environment.environment.insert(QStringLiteral("M00_CHILD_ENV"), QStringLiteral("binary-safe-value"));
    std::optional<ProcessResult> environmentResult;
    (void)environmentPort.start(environment, ProcessCallbacks {
        {},
        {},
        {},
        [&](const ProcessResult &finished) { environmentResult = finished; },
        {}
    });
    waitUntil([&] { return environmentResult.has_value(); }, 5000, "environment fixture did not complete");
    require(environmentResult->stdoutData == QByteArray("binary-safe-value"),
            "child environment must be passed without argv mutation");

    ProcessPort stopPort;
    ProcessSpec ignoresTerm = fixtureSpec(python, fixture, QStringLiteral("ignore-term"));
    std::optional<ProcessResult> stopResult;
    bool stopStarted = false;
    const quint64 stopId = stopPort.start(ignoresTerm, ProcessCallbacks {
        [&] { stopStarted = true; },
        {},
        {},
        [&](const ProcessResult &finished) { stopResult = finished; },
        {}
    });
    waitUntil([&] { return stopStarted; }, 3000, "graceful-stop fixture did not start");
    require(stopPort.requestStop(stopId, 50), "owned child graceful stop must be accepted");
    waitUntil([&] { return stopResult.has_value(); }, 5000, "graceful-stop fixture did not finish");
    require(stopResult->terminationRequested, "stop must be visible in the result");
    require(stopResult->forceKilled, "a child ignoring graceful stop must be force-killed");
    require(stopResult->signal == QStringLiteral("SIGKILL"), "forced stop must expose SIGKILL");
    require(stopPort.activeCount() == 0, "finished child must leave the owned set");

    ProcessPort cancelPort;
    ProcessSpec sleeping = fixtureSpec(python, fixture, QStringLiteral("sleep"));
    std::optional<ProcessResult> cancelResult;
    const quint64 cancelId = cancelPort.start(sleeping, ProcessCallbacks {
        {},
        {},
        {},
        [&](const ProcessResult &finished) { cancelResult = finished; },
        {}
    });
    require(cancelPort.cancel(cancelId), "cancellation race must target the launched child");
    waitUntil([&] { return cancelResult.has_value(); }, 5000, "canceled fixture did not finish");
    require(cancelResult->canceled, "cancellation must be visible in the result");
    require(cancelResult->forceKilled, "cancellation must force-kill the owned child");
    require(!cancelPort.owns(cancelId), "canceled child must be removed from ownership");

    QProcess external;
    external.setProgram(python.program);
    external.setArguments(python.prefixArguments + QStringList {fixture, QStringLiteral("sleep")});
    external.start();
    require(external.waitForStarted(3000), "external fixture did not start");
    {
        ProcessPort ownedPort;
        const quint64 ownedId = ownedPort.start(sleeping);
        waitUntil([&] { return ownedPort.running(ownedId); }, 3000, "owned fixture did not start");
    }
    require(external.state() == QProcess::Running,
            "destroying a ProcessPort must not kill a process it did not launch");
    external.kill();
    external.waitForFinished(3000);

    ProcessPort treePort;
    ProcessSpec tree = fixtureSpec(python, fixture, QStringLiteral("spawn-grandchild"));
    std::optional<ProcessResult> treeResult;
    qint64 grandchildPid = 0;
    const quint64 treeId = treePort.start(tree, ProcessCallbacks {
        {},
        [&](const QByteArray &bytes) {
            bool converted = false;
            grandchildPid = bytes.trimmed().toLongLong(&converted);
            if (!converted)
                grandchildPid = 0;
        },
        {},
        [&](const ProcessResult &finished) { treeResult = finished; },
        {}
    });
    waitUntil([&] { return treePort.running(treeId) && grandchildPid != 0; },
              3000, "grandchild fixture did not start");
    require(treePort.requestStop(treeId, 50), "owned process tree stop must be accepted");
    waitUntil([&] { return treeResult.has_value(); }, 5000,
              "grandchild parent did not finish");

    QProcess probe;
    probe.setProgram(python.program);
    probe.setArguments(python.prefixArguments
                       + QStringList {fixture, QStringLiteral("probe"),
                                      QString::number(grandchildPid)});
    probe.start();
    require(probe.waitForFinished(5000), "grandchild probe did not finish");
    require(probe.exitCode() == 0, "owned process teardown must not orphan a grandchild");

    ProcessPort destroyedDriverPort;
    ProcessSpec pendingOutput = fixtureSpec(python, fixture, QStringLiteral("pending-output"));
    int destroyedDriverStarted = 0;
    int destroyedDriverStdout = 0;
    int destroyedDriverStderr = 0;
    int destroyedDriverFinished = 0;
    bool destroyedDriver = false;
    int lateStarted = 0;
    int lateStdout = 0;
    int lateStderr = 0;
    int lateFinished = 0;
    {
        auto driver = std::make_unique<ProcessDriver>(destroyedDriverPort);
        DriverSpec localSpec;
        localSpec.process = pendingOutput;
        localSpec.mode = HlsV2Mode::Local;
        const quint64 driverId = driver->start(localSpec, ProcessCallbacks {
            [&] {
                ++destroyedDriverStarted;
                if (destroyedDriver)
                    ++lateStarted;
            },
            [&](const QByteArray &) {
                ++destroyedDriverStdout;
                if (destroyedDriver)
                    ++lateStdout;
            },
            [&](const QByteArray &) {
                ++destroyedDriverStderr;
                if (destroyedDriver)
                    ++lateStderr;
            },
            [&](const ProcessResult &) {
                ++destroyedDriverFinished;
                if (destroyedDriver)
                    ++lateFinished;
            },
            {}
        });
        waitUntil([&] { return driver->running(driverId) || destroyedDriverPort.activeCount() == 1; },
                  3000,
                  "local driver child was not registered before destruction");
        destroyedDriver = true;
        driver.reset();
    }
    waitUntil([&] { return destroyedDriverPort.activeCount() == 0; }, 5000,
              "destroyed local driver left an owned child");
    QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
    require(lateStarted == 0 && lateStdout == 0 && lateStderr == 0 && lateFinished == 0,
            "late local callbacks must not call a destroyed ProcessDriver");

    std::cout << "M00-02 PASS\n";
}

void caseM00_03()
{
    ProcessPort port;
    RemoteProcessTransport transport;
    quint16 requestedStart = 0;
    int dispatchCount = 0;
    int cancelCount = 0;
    int pollCount = 0;
    quint16 capturedPort = 0;
    QString capturedId;
    QString capturedUrl;
    QStringList capturedArguments;
    QStringList dispatchedIds;
    QStringList canceledIds;
    std::function<void(const RemoteCompletion &)> firstCompletion;

    transport.reservePort = [&](quint16 start) {
        requestedStart = start;
        return std::optional<quint16> {40123};
    };
    transport.dispatch = [&](const QString &id, const QString &, const QStringList &arguments,
                             const QProcessEnvironment &,
                             std::function<void(const RemoteCompletion &)> complete) {
        ++dispatchCount;
        capturedId = id;
        dispatchedIds.append(id);
        capturedPort = 40123;
        capturedArguments = arguments;
        if (dispatchCount == 1) {
            firstCompletion = std::move(complete);
            return;
        }
        QTimer::singleShot(25, [complete] {
            RemoteCompletion completion;
            completion.exitCode = 0;
            complete(completion);
        });
    };
    transport.poll = [&](const QString &url, std::function<void(const RemotePollResult &)> complete) {
        ++pollCount;
        capturedUrl = url;
        QTimer::singleShot(0, [complete, poll = pollCount] {
            RemotePollResult pollResult;
            pollResult.ready = poll >= 2;
            if (pollResult.ready)
                pollResult.data = QByteArray("remote-binary");
            complete(pollResult);
        });
    };
    transport.cancel = [&](const QString &id) {
        ++cancelCount;
        canceledIds.append(id);
        require(id == capturedId, "remote disconnect must cancel its own bridge id");
    };

    ProcessDriver driver(port, transport);
    DriverSpec spec;
    spec.process.program = QStringLiteral("ffmpeg");
    spec.process.arguments = {QStringLiteral("-i"), QStringLiteral("space media.mp4")};
    spec.process.environment = QProcessEnvironment::systemEnvironment();
    spec.mode = HlsV2Mode::Remote;
    spec.remotePollIntervalMs = 1;

    bool ready = false;
    QByteArray remoteBytes;
    std::optional<ProcessResult> result;
    const quint64 id = driver.start(spec, ProcessCallbacks {
        {},
        [&](const QByteArray &bytes) { remoteBytes += bytes; },
        {},
        [&](const ProcessResult &finished) { result = finished; },
        [&](quint16 portNumber, const QString &) {
            capturedPort = portNumber;
            ready = true;
        }
    });

    waitUntil([&] { return ready; }, 3000, "remote fixture never became ready");
    require(requestedStart >= 11920, "remote port selection must preserve the source starting range");
    require(dispatchCount == 1, "remote bridge must receive one dispatch");
    require(capturedPort == 40123, "remote port allocator result must be used");
    require(capturedArguments.size() == 5, "remote argv must append three listen arguments");
    require(capturedArguments[0] == QStringLiteral("-i")
                && capturedArguments[1] == QStringLiteral("space media.mp4")
                && capturedArguments[2] == QStringLiteral("-listen")
                && capturedArguments[3] == QStringLiteral("1"),
            "remote argv must preserve the original array and append listen options");
    require(capturedArguments[4].startsWith(QStringLiteral("http://127.0.0.1:40123/")),
            "remote argv must contain the selected loopback port");
    require(capturedUrl == capturedArguments[4], "polling must use the dispatched loopback URL");
    require(remoteBytes == QByteArray("remote-binary"), "remote polling bytes must remain binary");

    require(driver.disconnect(id), "remote disconnect must target the active driver");
    waitUntil([&] { return result.has_value(); }, 3000, "remote disconnect did not complete");
    require(result->canceled, "remote disconnect must surface cancellation");
    require(cancelCount == 1, "remote disconnect must issue one bridge cancellation");
    const int pollsAfterDisconnect = pollCount;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
    require(pollCount == pollsAfterDisconnect, "disconnect must stop completion polling");
    require(!driver.running(id), "disconnected remote process must not remain active");
    require(static_cast<bool>(firstCompletion),
            "remote fixture must retain a completion callback for the cancellation race");
    firstCompletion(RemoteCompletion {});

    ProcessPort fallbackPort;
    RemoteProcessTransport fallbackTransport;
    quint16 fallbackRequestedStart = 0;
    int fallbackDispatchCount = 0;
    QStringList fallbackArguments;
    bool fallbackReady = false;
    std::optional<ProcessResult> fallbackResult;
    fallbackTransport.reservePort = [&](quint16 start) {
        fallbackRequestedStart = start;
        return std::optional<quint16> {};
    };
    fallbackTransport.dispatch = [&](const QString &, const QString &, const QStringList &arguments,
                                     const QProcessEnvironment &,
                                     std::function<void(const RemoteCompletion &)> complete) {
        ++fallbackDispatchCount;
        fallbackArguments = arguments;
        complete(RemoteCompletion {});
    };
    fallbackTransport.poll = [&](const QString &,
                                 std::function<void(const RemotePollResult &)> complete) {
        RemotePollResult pollResult;
        pollResult.ready = true;
        complete(pollResult);
    };
    fallbackTransport.cancel = [](const QString &) {};

    ProcessDriver fallbackDriver(fallbackPort, fallbackTransport);
    DriverSpec fallbackSpec = spec;
    fallbackSpec.remotePollIntervalMs = 0;
    const quint64 fallbackId = fallbackDriver.start(fallbackSpec, ProcessCallbacks {
        {},
        {},
        {},
        [&](const ProcessResult &finished) { fallbackResult = finished; },
        [&](quint16, const QString &) { fallbackReady = true; }
    });
    waitUntil([&] { return fallbackReady && fallbackResult.has_value(); }, 3000,
              "port-search fallback did not complete through the remote bridge");
    require(fallbackRequestedStart >= 11920,
            "port-search fallback must preserve the source starting range");
    require(fallbackDispatchCount == 1,
            "port-search failure must still dispatch the source remote conversion");
    require(fallbackArguments.size() == 5
                && fallbackArguments[4].startsWith(
                    QStringLiteral("http://127.0.0.1:%1/").arg(fallbackRequestedStart)),
            "port-search failure must use the requested source port in the bridge URL");
    require(fallbackResult->started && fallbackResult->error == QProcess::UnknownError,
            "source port-search fallback must complete as a started remote process");
    require(!fallbackDriver.running(fallbackId),
            "completed source port-search fallback must be inactive");

    ProcessDriver completedDriver(port, transport);
    std::optional<ProcessResult> completedResult;
    QByteArray completedBytes;
    const quint64 completedId = completedDriver.start(spec, ProcessCallbacks {
        {},
        [&](const QByteArray &bytes) { completedBytes += bytes; },
        {},
        [&](const ProcessResult &finished) { completedResult = finished; },
        {}
    });
    waitUntil([&] { return completedResult.has_value(); }, 3000,
              "remote completion did not finish after polling became ready");
    require(completedResult->exitCode == 0, "successful remote completion must preserve exit code");
    require(completedResult->error == QProcess::UnknownError,
            "successful remote completion must not be reported as a crash");
    require(completedResult->stdoutData == QByteArray("remote-binary"),
            "remote completion must retain polled binary output");
    require(completedBytes == QByteArray("remote-binary"),
            "remote completion callback must receive polled binary output");
    require(!completedDriver.running(completedId), "completed remote process must be inactive");

    ProcessPort concurrentPort;
    RemoteProcessTransport concurrentTransport;
    quint16 concurrentPortNumber = 40201;
    QStringList concurrentDispatchedIds;
    QStringList concurrentCanceledIds;
    concurrentTransport.reservePort = [&](quint16) {
        return std::optional<quint16> {concurrentPortNumber++};
    };
    concurrentTransport.dispatch = [&](const QString &bridgeId, const QString &,
                                       const QStringList &, const QProcessEnvironment &,
                                       std::function<void(const RemoteCompletion &)>) {
        concurrentDispatchedIds.append(bridgeId);
    };
    concurrentTransport.poll = [](const QString &,
                                  std::function<void(const RemotePollResult &)>) {};
    concurrentTransport.cancel = [&](const QString &bridgeId) {
        concurrentCanceledIds.append(bridgeId);
    };
    ProcessDriver concurrentDriverA(concurrentPort, concurrentTransport);
    ProcessDriver concurrentDriverB(concurrentPort, concurrentTransport);
    const quint64 concurrentIdA = concurrentDriverA.start(spec);
    const quint64 concurrentIdB = concurrentDriverB.start(spec);
    require(concurrentDispatchedIds.size() == 2,
            "two concurrent drivers must each dispatch a remote bridge");
    require(concurrentDispatchedIds[0] != concurrentDispatchedIds[1],
            "remote bridge identities must be unique across driver instances");
    require(concurrentDriverA.disconnect(concurrentIdA),
            "first concurrent driver must disconnect independently");
    require(concurrentCanceledIds.size() == 1
                && concurrentCanceledIds[0] == concurrentDispatchedIds[0],
            "first concurrent driver cancellation must target only its bridge identity");
    require(concurrentDriverB.running(concurrentIdB),
            "first concurrent driver cancellation must not deactivate the second driver");

    ProcessPort callbackPort;
    RemoteProcessTransport callbackTransport;
    std::function<void(const RemotePollResult &)> callbackPoll;
    int callbackCancelCount = 0;
    int callbackOutputCount = 0;
    int callbackReadyCount = 0;
    int callbackFinishedCount = 0;
    bool callbackDisconnected = false;
    callbackTransport.reservePort = [](quint16) {
        return std::optional<quint16> {40211};
    };
    callbackTransport.dispatch = [](const QString &, const QString &, const QStringList &,
                                    const QProcessEnvironment &,
                                    std::function<void(const RemoteCompletion &)>) {};
    callbackTransport.poll = [&](const QString &,
                                 std::function<void(const RemotePollResult &)> complete) {
        callbackPoll = std::move(complete);
    };
    callbackTransport.cancel = [&](const QString &) { ++callbackCancelCount; };
    auto callbackDriver = std::make_unique<ProcessDriver>(callbackPort, callbackTransport);
    quint64 callbackId = 0;
    callbackId = callbackDriver->start(spec, ProcessCallbacks {
        {},
        [&](const QByteArray &) {
            ++callbackOutputCount;
            callbackDisconnected = callbackDriver->disconnect(callbackId);
        },
        {},
        [&](const ProcessResult &finished) {
            ++callbackFinishedCount;
            require(finished.canceled, "callback cancellation must surface a canceled result");
        },
        [&](quint16, const QString &) { ++callbackReadyCount; }
    });
    waitUntil([&] { return static_cast<bool>(callbackPoll); }, 3000,
              "callback-cancellation poll callback was not captured");
    RemotePollResult callbackPollResult;
    callbackPollResult.ready = true;
    callbackPollResult.data = QByteArray("callback-output");
    callbackPoll(callbackPollResult);
    require(callbackOutputCount == 1, "remote output callback must run before cancellation");
    require(callbackDisconnected, "remote output callback must be able to cancel its driver");
    require(callbackCancelCount == 1, "callback cancellation must issue one bridge cancel");
    require(callbackFinishedCount == 1, "callback cancellation must finish the remote child once");
    require(callbackReadyCount == 0,
            "remote-ready callback must not run after output callback cancellation");
    require(!callbackDriver->running(callbackId),
            "callback cancellation must remove the remote child before poll returns");

    ProcessPort delayedPort;
    RemoteProcessTransport delayedTransport;
    std::function<void(const RemoteCompletion &)> delayedCompletion;
    std::function<void(const RemotePollResult &)> delayedPoll;
    int delayedCancelCount = 0;
    int delayedFinished = 0;
    int delayedReady = 0;
    int delayedOutput = 0;
    delayedTransport.reservePort = [](quint16) {
        return std::optional<quint16> {40124};
    };
    delayedTransport.dispatch = [&](const QString &, const QString &, const QStringList &,
                                    const QProcessEnvironment &environment,
                                    std::function<void(const RemoteCompletion &)> complete) {
        Q_UNUSED(environment);
        delayedCompletion = std::move(complete);
    };
    delayedTransport.poll = [&](const QString &,
                                std::function<void(const RemotePollResult &)> complete) {
        delayedPoll = std::move(complete);
    };
    delayedTransport.cancel = [&](const QString &) { ++delayedCancelCount; };

    {
        auto delayedDriver = std::make_unique<ProcessDriver>(delayedPort, delayedTransport);
        DriverSpec delayedSpec = spec;
        delayedSpec.remotePollIntervalMs = 1;
        (void)delayedDriver->start(delayedSpec, ProcessCallbacks {
            {},
            [&](const QByteArray &) { ++delayedOutput; },
            {},
            [&](const ProcessResult &) { ++delayedFinished; },
            [&](quint16, const QString &) { ++delayedReady; }
        });
        waitUntil([&] { return static_cast<bool>(delayedPoll); }, 3000,
                  "delayed remote poll callback was not captured");
        delayedDriver.reset();
    }
    require(delayedCancelCount == 1, "destroyed remote driver must cancel exactly once");
    require(delayedCompletion && delayedPoll,
            "destroyed remote driver must leave deterministic delayed callbacks");
    delayedCompletion(RemoteCompletion {});
    RemotePollResult delayedResult;
    delayedResult.ready = true;
    delayedResult.data = QByteArray("late-remote-data");
    delayedPoll(delayedResult);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
    require(delayedCancelCount == 1,
            "late remote callbacks must not issue a second cancellation");
    require(delayedFinished == 0 && delayedReady == 0 && delayedOutput == 0,
            "late remote callbacks must not call destroyed driver state");
    require(delayedPort.activeCount() == 0,
            "remote driver destruction must not leave owned local children");

    std::cout << "M00-03 PASS\n";
}

void emitTrace()
{
    QTemporaryDir directory;
    require(directory.isValid(), "trace temporary directory unavailable");

    const QString executable = QCoreApplication::applicationFilePath();
    const QString unusualDirectory = QDir(directory.path()).filePath(QStringLiteral("space Δ"));
    require(QDir().mkpath(unusualDirectory), "cannot create trace executable directory");
    const QString pathCandidate = QDir(unusualDirectory).filePath(QStringLiteral("trace tool.exe"));
    require(QFile::copy(executable, pathCandidate), "cannot create trace PATH candidate");
    const QString overrideCandidate = QDir(unusualDirectory).filePath(QStringLiteral("override tool.exe"));
    require(QFile::copy(executable, overrideCandidate), "cannot create trace override candidate");

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), unusualDirectory);
    ExecutableLocator locator(environment);
    const QString missing = QDir(unusualDirectory).filePath(QStringLiteral("missing.exe"));
    const QString directoryCandidate = QDir(unusualDirectory).filePath(QStringLiteral("trace-directory"));
    require(QDir().mkpath(directoryCandidate), "cannot create trace directory candidate");
    const auto overridden = locator.locate(QStringLiteral("trace tool.exe"), {overrideCandidate});
    const auto directoryLocated = locator.locate(QStringLiteral("trace-directory"), {directoryCandidate});
    const auto pathLocated = locator.locate(QStringLiteral("trace tool.exe"), {missing});
    std::cout << "M00-01 locator.override="
              << (overridden && *overridden == overrideCandidate ? "preferred" : "mismatch") << '\n';
    std::cout << "M00-01 locator.directory="
              << (directoryLocated && *directoryLocated == directoryCandidate ? "accepted" : "rejected")
              << '\n';
    std::cout << "M00-01 locator.missing-fallback="
              << (pathLocated && *pathLocated == pathCandidate ? "path" : "missing") << '\n';

    const ScriptCommand python = pythonCommand();
    const QString fixture = writeFixture(directory);
    ProcessPort localPort;
    ProcessSpec binary = fixtureSpec(python, fixture, QStringLiteral("binary"));
    std::optional<ProcessResult> localResult;
    qsizetype stdoutCallbackBytes = 0;
    qsizetype stderrCallbackBytes = 0;
    (void)localPort.start(binary, ProcessCallbacks {
        {},
        [&](const QByteArray &bytes) { stdoutCallbackBytes += bytes.size(); },
        [&](const QByteArray &bytes) { stderrCallbackBytes += bytes.size(); },
        [&](const ProcessResult &finished) { localResult = finished; },
        {}
    });
    waitUntil([&] { return localResult.has_value(); }, 5000,
              "trace binary fixture did not complete");

    ProcessPort environmentPort;
    ProcessSpec environmentSpec = fixtureSpec(python, fixture, QStringLiteral("environment"));
    environmentSpec.environment.insert(QStringLiteral("M00_CHILD_ENV"),
                                       QStringLiteral("trace-environment"));
    std::optional<ProcessResult> environmentResult;
    (void)environmentPort.start(environmentSpec, ProcessCallbacks {
        {},
        {},
        {},
        [&](const ProcessResult &finished) { environmentResult = finished; },
        {}
    });
    waitUntil([&] { return environmentResult.has_value(); }, 5000,
              "trace environment fixture did not complete");
    std::cout << "M00-02 process="
              << (environmentResult->stdoutData == QByteArray("trace-environment")
                      && localResult->started ? "argv-environment-preserved" : "mismatch")
              << '\n';
    std::cout << "M00-02 output="
              << (stdoutCallbackBytes == 8 && stderrCallbackBytes == 5 ? "callbacks-complete" : "mismatch")
              << '\n';
    std::cout << "M00-02 exit=" << localResult->exitCode << '\n';

    ProcessPort remotePort;
    RemoteProcessTransport fallbackTransport;
    quint16 fallbackRequestedStart = 0;
    bool fallbackDispatched = false;
    bool fallbackReady = false;
    QString fallbackUrl;
    fallbackTransport.reservePort = [&](quint16 start) {
        fallbackRequestedStart = start;
        return std::optional<quint16> {};
    };
    fallbackTransport.dispatch = [&](const QString &, const QString &, const QStringList &arguments,
                                     const QProcessEnvironment &,
                                     std::function<void(const RemoteCompletion &)> complete) {
        fallbackDispatched = true;
        fallbackUrl = arguments.isEmpty() ? QString() : arguments.constLast();
        complete(RemoteCompletion {});
    };
    fallbackTransport.poll = [&](const QString &,
                                 std::function<void(const RemotePollResult &)> complete) {
        RemotePollResult result;
        result.ready = true;
        complete(result);
    };
    fallbackTransport.cancel = [](const QString &) {};
    ProcessDriver remoteDriver(remotePort, fallbackTransport);
    DriverSpec remoteSpec;
    remoteSpec.process.program = QStringLiteral("ffmpeg");
    remoteSpec.process.arguments = {QStringLiteral("-i"), QStringLiteral("trace media.mp4")};
    remoteSpec.process.environment = QProcessEnvironment::systemEnvironment();
    remoteSpec.mode = HlsV2Mode::Remote;
    remoteSpec.remotePollIntervalMs = 0;
    std::optional<ProcessResult> remoteResult;
    (void)remoteDriver.start(remoteSpec, ProcessCallbacks {
        {},
        {},
        {},
        [&](const ProcessResult &finished) { remoteResult = finished; },
        [&](quint16, const QString &) { fallbackReady = true; }
    });
    waitUntil([&] { return fallbackReady && remoteResult.has_value(); }, 3000,
              "trace remote fixture did not complete");
    const QString expectedFallbackPrefix =
        QStringLiteral("http://127.0.0.1:%1/").arg(fallbackRequestedStart);
    std::cout << "M00-03 mode="
              << (remoteResult->started ? "local-remote-preserved" : "mismatch") << '\n';
    std::cout << "M00-03 port-search="
              << (fallbackDispatched && fallbackUrl.startsWith(expectedFallbackPrefix)
                      ? "dispatch-requested-port"
                      : "failed")
              << '\n';

    ProcessPort concurrentPort;
    RemoteProcessTransport concurrentTransport;
    quint16 concurrentPortNumber = 40301;
    QStringList concurrentDispatchedIds;
    QStringList concurrentCanceledIds;
    concurrentTransport.reservePort = [&](quint16) {
        return std::optional<quint16> {concurrentPortNumber++};
    };
    concurrentTransport.dispatch = [&](const QString &bridgeId, const QString &, const QStringList &,
                                       const QProcessEnvironment &,
                                       std::function<void(const RemoteCompletion &)>) {
        concurrentDispatchedIds.append(bridgeId);
    };
    concurrentTransport.poll = [](const QString &,
                                  std::function<void(const RemotePollResult &)>) {};
    concurrentTransport.cancel = [&](const QString &bridgeId) {
        concurrentCanceledIds.append(bridgeId);
    };
    ProcessDriver concurrentDriverA(concurrentPort, concurrentTransport);
    ProcessDriver concurrentDriverB(concurrentPort, concurrentTransport);
    DriverSpec concurrentSpec = remoteSpec;
    const quint64 concurrentIdA = concurrentDriverA.start(concurrentSpec);
    const quint64 concurrentIdB = concurrentDriverB.start(concurrentSpec);
    const bool uniqueBridges = concurrentDispatchedIds.size() == 2
        && concurrentDispatchedIds[0] != concurrentDispatchedIds[1];
    const bool firstDisconnect = concurrentDriverA.disconnect(concurrentIdA);
    const bool secondStillRunning = concurrentDriverB.running(concurrentIdB);
    std::cout << "M00-03 bridge.concurrent="
              << (uniqueBridges && firstDisconnect ? "unique" : "collision") << '\n';
    std::cout << "M00-03 bridge.remaining="
              << (secondStillRunning ? "one" : "zero") << '\n';

    ProcessPort callbackPort;
    RemoteProcessTransport callbackTransport;
    std::function<void(const RemotePollResult &)> callbackPoll;
    int callbackCancelCount = 0;
    int callbackFinishedCount = 0;
    int callbackReadyCount = 0;
    bool callbackDisconnected = false;
    callbackTransport.reservePort = [](quint16) {
        return std::optional<quint16> {40311};
    };
    callbackTransport.dispatch = [](const QString &, const QString &, const QStringList &,
                                    const QProcessEnvironment &,
                                    std::function<void(const RemoteCompletion &)>) {};
    callbackTransport.poll = [&](const QString &,
                                 std::function<void(const RemotePollResult &)> complete) {
        callbackPoll = std::move(complete);
    };
    callbackTransport.cancel = [&](const QString &) { ++callbackCancelCount; };
    auto callbackDriver = std::make_unique<ProcessDriver>(callbackPort, callbackTransport);
    quint64 callbackId = 0;
    callbackId = callbackDriver->start(remoteSpec, ProcessCallbacks {
        {},
        [&](const QByteArray &) { callbackDisconnected = callbackDriver->disconnect(callbackId); },
        {},
        [&](const ProcessResult &) { ++callbackFinishedCount; },
        [&](quint16, const QString &) { ++callbackReadyCount; }
    });
    waitUntil([&] { return static_cast<bool>(callbackPoll); }, 3000,
              "trace callback poll fixture was not captured");
    RemotePollResult callbackPollResult;
    callbackPollResult.ready = true;
    callbackPollResult.data = QByteArray("trace-callback");
    callbackPoll(callbackPollResult);
    std::cout << "M00-03 callback-cancel="
              << (callbackDisconnected && callbackCancelCount == 1 && callbackFinishedCount == 1
                          && callbackReadyCount == 0
                      ? "one"
                      : "failed")
              << '\n';
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    try {
        const QString requested = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("all");
        if (requested == QStringLiteral("--trace")) {
            emitTrace();
            return 0;
        }
        if (requested == QStringLiteral("all") || requested == QStringLiteral("M00-01"))
            caseM00_01();
        if (requested == QStringLiteral("all") || requested == QStringLiteral("M00-02"))
            caseM00_02();
        if (requested == QStringLiteral("all") || requested == QStringLiteral("M00-03"))
            caseM00_03();
        if (requested != QStringLiteral("all") && requested != QStringLiteral("M00-01")
            && requested != QStringLiteral("M00-02") && requested != QStringLiteral("M00-03"))
            throw std::runtime_error("unknown M00 case");
    } catch (const std::exception &error) {
        std::cerr << "M00 FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
