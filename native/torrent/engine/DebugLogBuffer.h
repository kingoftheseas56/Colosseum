#pragma once

// DebugLogBuffer — bounded in-memory ring buffer for structured debug logs.
// Replaces ad-hoc fopen()-to-hardcoded-path debug breadcrumbs flagged by the
// external AI audit (REPO_HYGIENE_FIX_TODO Phase 1.3, 2026-04-26).
//
// Design:
//   - Singleton (one buffer per process, accessed via instance()).
//   - Capacity 500 entries via std::deque (oldest entries pop_front when full).
//   - Thread-safe via QMutex (multiple subsystems write: boot/mainwindow/
//     sidecar-ipc/frame-canvas/loading-overlay/etc.).
//   - Disk persistence OFF by default. Gated behind TANKOBAN_DEBUG_LOG=1
//     env var. When enabled, flushes the ring to QStandardPaths AppDataLocation
//     /debug.log on app exit (via QCoreApplication::aboutToQuit signal — wired
//     in main.cpp). One env var, one Qt-resolved path. No hardcoded paths.
//   - Phase 3 dev-control bridge will expose a `logs` command that reads from
//     this buffer (planned per REPO_HYGIENE_FIX_TODO §6 P3.2 / P3.7).
//
// Why a bespoke buffer rather than QLoggingCategory + qDebug:
//   - Need structured query interface (recent(N), lastError()) for Phase 3.
//   - Need bounded retention (Q* output is unbounded by default).
//   - Need per-source attribution (boot vs sidecar-ipc vs frame-canvas) for
//     filtering in future Phase 3 dev-bridge commands.

#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <deque>

class DebugLogBuffer
{
public:
    enum class Level { Info, Warning, Error };

    struct Entry {
        Level level;
        QString source;
        QString message;
        qint64 timestampMs;
        QJsonObject details;  // optional structured context; default empty
    };

    static DebugLogBuffer& instance();

    void info(const QString& source, const QString& message, const QJsonObject& details = {});
    void warning(const QString& source, const QString& message, const QJsonObject& details = {});
    void error(const QString& source, const QString& message, const QJsonObject& details = {});

    // Returns most recent N entries (oldest first within the slice).
    QJsonArray recent(int limit = 100) const;

    // Returns last Error-level entry as JSON, or null QJsonObject if none.
    QJsonObject lastError() const;

    // Flush buffer to disk if TANKOBAN_DEBUG_LOG=1 is set; else no-op.
    // Wire this to QCoreApplication::aboutToQuit in main.cpp.
    void flushToDiskIfEnabled() const;

private:
    DebugLogBuffer() = default;
    DebugLogBuffer(const DebugLogBuffer&) = delete;
    DebugLogBuffer& operator=(const DebugLogBuffer&) = delete;

    void append(Level level, const QString& source, const QString& message, const QJsonObject& details);
    static QJsonObject toJson(const Entry& entry);
    static const char* levelName(Level level);

    static constexpr int kCapacity = 500;

    mutable QMutex m_mutex;
    std::deque<Entry> m_entries;
    Entry m_lastError;
    bool m_hasLastError = false;
};
