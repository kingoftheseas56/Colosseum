// AppLog.h — the always-on rolling log.
//
// WHY THIS EXISTS (2026-08-05): Hemanth hit a Downloads "Cancel" that did
// nothing and said nothing, killed the app, and asked for the log. There was
// none. Colosseum installed no message handler, and Colosseum.bat — the
// launcher he actually double-clicks — runs the exe with stderr unredirected,
// so every qInfo/qWarning in a normal launch was discarded as it was produced.
// A bug he can reproduce but never evidence is a bug we debug blind, so the
// log is now unconditional: no dev launcher, no env var, no opt-in.
//
// Contract:
//   - appends to <AppDataLocation>/logs/colosseum.log
//   - flushes EVERY line — the case this was built for is a hard kill, and a
//     buffered tail is exactly the part that would be missing
//   - rotates at 5 MB, keeping colosseum.1..3.log (oldest dropped)
//   - forwards to the previously-installed handler, so dev.bat's console and
//     QT_FORCE_STDERR_LOGGING behave exactly as before
//   - mutex-guarded: Qt logs from the network/decode threads, not just the GUI
//
// install() must be called AFTER applicationName/organizationName are set (and
// after the COLOSSEUM_APPDATA_TAG override), because AppDataLocation resolves
// from them — a dltest run then writes its own isolated log, never the real one.
#pragma once

#include <QString>

namespace AppLog {

// Idempotent. Safe to call once from main() after the app identity is set.
void install();

// <AppDataLocation>/logs — created on install.
QString logDir();

// The live log file's absolute path (empty before install()).
QString currentLogPath();

}  // namespace AppLog
