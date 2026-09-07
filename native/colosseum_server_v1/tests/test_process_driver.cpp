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
#include <optional>
#include <stdexcept>
#include <string>

namespace {

using namespace server1::media;

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
    require(!noPathLocator.locate(QStringLiteral("not-launchable"), {directoryCandidate}),
            "directories must not be reported as executable candidates");

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

    transport.reservePort = [&](quint16 start) {
        requestedStart = start;
        return std::optional<quint16> {40123};
    };
    transport.dispatch = [&](const QString &id, const QString &, const QStringList &arguments,
                             const QProcessEnvironment &,
                             std::function<void(const RemoteCompletion &)> complete) {
        ++dispatchCount;
        capturedId = id;
        capturedPort = 40123;
        capturedArguments = arguments;
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

    std::cout << "M00-03 PASS\n";
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    try {
        const QString requested = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("all");
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
