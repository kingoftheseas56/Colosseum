// Proves the app-lifetime tie for managed child processes (bootstrap/ChildProcessJob).
//
// The harness re-launches itself as a "parent" that spawns a long-lived "sleeper"
// grandchild, optionally adopts it, prints the grandchild PID and then ends itself
// with TerminateProcess - the way Colosseum's fast exit will. The outer run asserts the
// adopted grandchild dies with the parent while an un-adopted one survives (so the test
// shows the mechanism, not a coincidence). Windows-only; elsewhere it is a no-op pass.

#include "bootstrap/ChildProcessJob.h"

#include <QCoreApplication>
#include <QProcess>
#include <QStringList>

#include <cstdio>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#ifdef Q_OS_WIN

static int fails = 0;
#define CHECK(c, l) do { if (!(c)) { ++fails; std::printf("FAIL: %s\n", l); } } while (false)

namespace {

// Long-lived stand-in for a managed child (node, ffmpeg, ...). Self-expires so a
// failed run never leaks it for long.
int runSleeper()
{
    Sleep(60 * 1000);
    return 0;
}

// Spawns the sleeper, adopts it per `mode`, prints "PID <n>", then dies abruptly.
int runParent(const QString &mode)
{
    QProcess grandchild;
    grandchild.setStandardInputFile(QProcess::nullDevice());
    grandchild.setStandardOutputFile(QProcess::nullDevice());
    grandchild.setStandardErrorFile(QProcess::nullDevice());
    if (mode == QLatin1String("adopt"))
        Colosseum::Bootstrap::adoptChildProcessOnStart(&grandchild);
    grandchild.start(QCoreApplication::applicationFilePath(), {QStringLiteral("sleeper")});
    if (!grandchild.waitForStarted(10000))
        return 2;
    if (mode == QLatin1String("adopt-direct"))
        Colosseum::Bootstrap::adoptChildProcess(grandchild);

    std::printf("PID %lld\n", static_cast<long long>(grandchild.processId()));
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 0);   // abrupt: no destructors, no cleanup
    return 3;                                   // not reached
}

// Runs one parent in `mode`; returns the grandchild PID, or 0 on failure.
DWORD spawnParentAndGetGrandchildPid(const QString &mode)
{
    QProcess parent;
    parent.setProcessChannelMode(QProcess::SeparateChannels);
    parent.start(QCoreApplication::applicationFilePath(), {QStringLiteral("parent"), mode});
    if (!parent.waitForStarted(10000))
        return 0;
    QByteArray out;
    while (!out.contains('\n') && parent.waitForReadyRead(10000))
        out += parent.readAllStandardOutput();
    if (!parent.waitForFinished(10000)) {
        parent.kill();
        return 0;
    }
    const QList<QByteArray> words = out.simplified().split(' ');
    if (words.size() < 2 || words.at(0) != "PID")
        return 0;
    return static_cast<DWORD>(words.at(1).toULongLong());
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() >= 2 && args.at(1) == QLatin1String("sleeper"))
        return runSleeper();
    if (args.size() >= 3 && args.at(1) == QLatin1String("parent"))
        return runParent(args.at(2));

    const struct { const char *mode; bool expectDeath; const char *label; } cases[] = {
        {"adopt",        true,  "adopted (on start) grandchild dies when its parent is terminated"},
        {"adopt-direct", true,  "adopted (direct call) grandchild dies when its parent is terminated"},
        {"none",         false, "un-adopted grandchild survives its terminated parent (control)"},
    };
    for (const auto &c : cases) {
        const DWORD pid = spawnParentAndGetGrandchildPid(QString::fromLatin1(c.mode));
        CHECK(pid != 0, "parent reported a grandchild PID");
        if (pid == 0)
            continue;
        HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
        // An un-openable PID means the process is already gone.
        const bool gone = !h || WaitForSingleObject(h, 2000) == WAIT_OBJECT_0;
        CHECK(gone == c.expectDeath, c.label);
        if (h) {
            if (!gone)
                TerminateProcess(h, 0);     // clean up the control's survivor
            CloseHandle(h);
        }
    }

    std::printf(fails ? "FAILS: %d\n" : "child_process_job_harness: ALL PASS\n", fails);
    return fails;
}

#else

int main(int, char **)
{
    std::printf("child_process_job_harness: SKIPPED (Windows only)\n");
    return 0;
}

#endif
