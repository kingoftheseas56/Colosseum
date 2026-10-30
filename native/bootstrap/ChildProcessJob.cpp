#include "ChildProcessJob.h"

#include <QDebug>
#include <QObject>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#endif

namespace Colosseum::Bootstrap {

#ifdef Q_OS_WIN
namespace {

std::atomic_flag s_warned = ATOMIC_FLAG_INIT;

void warnOnce(const char *what, DWORD error)
{
    if (!s_warned.test_and_set())
        qWarning("[childjob] %s failed (error %lu); managed children will not be tied to the app lifetime",
                 what, static_cast<unsigned long>(error));
}

// Created once, on first use, and kept open for the whole process lifetime on purpose.
HANDLE childJob()
{
    static const HANDLE job = [] {
        HANDLE h = CreateJobObjectW(nullptr, nullptr);
        if (!h) {
            warnOnce("CreateJobObject", GetLastError());
            return HANDLE(nullptr);
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(h, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
            warnOnce("SetInformationJobObject", GetLastError());
            CloseHandle(h);
            return HANDLE(nullptr);
        }
        return h;
    }();
    return job;
}

} // namespace
#endif

void adoptChildProcess(QProcess &process)
{
#ifdef Q_OS_WIN
    const qint64 pid = process.processId();
    if (pid <= 0)
        return;
    const HANDLE job = childJob();
    if (!job)
        return;
    const HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (!child) {
        warnOnce("OpenProcess", GetLastError());
        return;
    }
    if (!AssignProcessToJobObject(job, child))
        warnOnce("AssignProcessToJobObject", GetLastError());
    CloseHandle(child);
#else
    Q_UNUSED(process);
#endif
}

void adoptChildProcessOnStart(QProcess *process)
{
#ifdef Q_OS_WIN
    if (!process)
        return;
    QObject::connect(process, &QProcess::started, process, [process] { adoptChildProcess(*process); });
#else
    Q_UNUSED(process);
#endif
}

}
