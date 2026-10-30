#pragma once

#include <QProcess>

// Ties managed child processes (node StreamServer, ffmpeg, archive extractors,
// python helpers) to Colosseum's lifetime. On Windows each adopted child joins one
// process-wide job object with KILL_ON_JOB_CLOSE; the job handle is never closed,
// so the OS closes it when Colosseum exits - however it exits - and that kills
// every member (and anything they spawned). Colosseum itself is NOT in the job:
// apps it opens for the user and the update installer must outlive it.
// Other platforms: no-ops.
namespace Colosseum::Bootstrap {

// Adopt an already-started child. A failure is logged once and never fatal.
void adoptChildProcess(QProcess &process);

// One-line call-site hook: adopts the child when `process` emits started().
// Call it before start(); the connection is owned by the process object.
void adoptChildProcessOnStart(QProcess *process);

}
