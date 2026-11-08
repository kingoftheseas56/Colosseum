#pragma once

#include <QtGlobal>

namespace NuvioWorkerProtocol {
inline constexpr qsizetype maxScriptBytes = 4 * 1024 * 1024;
inline constexpr qsizetype maxOutputBytes = 512 * 1024;
inline constexpr int maxStreams = 64;
inline constexpr int pluginDeadlineMs = 20000;
inline constexpr int processDeadlineMs = 22000;
}
