// NuvioStreamBackend — the opt-in Android torrent playback backend behind the
// existing StreamServer/Media3 seam.
//
// Ownership model (fixed architecture for the COLOSSEUM_ANDROID_NUVIO_BACKEND
// candidate):
//   * One process-owned playback engine, created lazily on explicit
//     play()/prefetch() demand — never on Activity/surface creation, and
//     warmUp() deliberately does NOT create it, so app startup cannot start
//     torrent networking.
//   * All potentially blocking engine work (create, add, prepare, poll,
//     stats, destroy) runs on an independently owned, joinable worker.
//   * Every public method may be called on the GUI thread; results are
//     delivered as signals via queued connections.
//
// Foreground/prefetch ownership discipline:
//   * FOREGROUND playback owns the active route (`m_active`). Only a play()
//     demand may retire or replace it. A prefetch never invalidates active
//     playback.
//   * The engine replaces routes for the SAME torrent, so a prefetch for a
//     different file of the playing torrent is deferred until playback moves
//     away; a prefetch of the exact playing source reuses its route; a
//     prefetch of another torrent runs in parallel and is remembered as the
//     single prefetch route, which a later play() of the same source shares
//     without re-preparing. Each owner may retire independently.
//
// Correlation discipline: every add_torrent/prepare_stream request id is
// tracked per in-flight demand chain, and completions (metadata, prepared,
// errors) are matched by request id AND torrent id (never torrent id alone),
// so an old file-0 completion can never satisfy a newer file-1 request. Each
// play()/prefetch() bumps a request generation; results delivered by an older
// generation are suppressed, and any route created by a stale or retired
// request is stopped so it cannot shadow newer playback.
//
// Teardown discipline (distinct from ordinary background transitions):
//   * Background transition retires playback on the worker. With a confirmed
//     Android download host, download routes/chains survive; otherwise all
//     routes are stopped and the engine is destroyed. The GUI never waits.
//   * Destruction invalidates the GUI delivery gate and transfers the
//     joinable worker to the process-lifetime reaper without waiting for
//     slow in-flight calls. Worker state remains alive until worker exit.
//
// Statistics are mapped honestly onto the existing QML keys: `peers`,
// `unchoked`, `downloaded`, `downloadSpeed`, `streamProgress`, `streamLen`.
// `downloaded` and `streamProgress` count hash-verified file bytes only;
// route creation is never reported as decoded playback.
#ifndef COLOSSEUM_PLAYER_NUVIOSTREAMBACKEND_H
#define COLOSSEUM_PLAYER_NUVIOSTREAMBACKEND_H

#include "nuvioengineapi.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace Colosseum::Nuvio {

class NuvioStreamBackend : public QObject {
    Q_OBJECT
public:
    struct Configuration {
        // App-private engine state directories (must already exist or be
        // creatable by the engine).
        QString dataDirectory;
        QString cacheDirectory;
        // PEM bundle of Android system TLS roots exported before engine
        // create; certificate verification is never disabled.
        QString tlsCaBundlePath;
        bool requireFixtureIsolation = false; // exact diagnostic-package only
        QHash<QString, QByteArray> fixtureMetadataSha256; // diagnostic source-switch pins
        // Bounded cache/upload budgets; upload is Disabled for this candidate
        // and the limits below are always configured explicitly rather than
        // inherited from upstream defaults.
        quint64 memoryCacheBytes = 64ull * 1024 * 1024;
        quint64 diskCacheBytes = 512ull * 1024 * 1024;
    };

    // Production entry point: resolves the pinned C API from the private
    // hardened engine library. available() reports whether the table loaded.
    explicit NuvioStreamBackend(
        const QString &engineLibraryPath,
        const Configuration &configuration,
        QObject *parent = nullptr
    );
    // Test seam: a scripted provider supplies the C API directly; no library
    // is loaded. The table must outlive this object.
    explicit NuvioStreamBackend(
        const EngineApi &injectedApi,
        const Configuration &configuration,
        QObject *parent = nullptr
    );
    ~NuvioStreamBackend() override;

    NuvioStreamBackend(const NuvioStreamBackend &) = delete;
    NuvioStreamBackend &operator=(const NuvioStreamBackend &) = delete;

    [[nodiscard]] bool available() const;
    [[nodiscard]] bool ready() const;
    [[nodiscard]] bool starting() const;
    [[nodiscard]] bool engineUnavailable() const;

    // Lazy demand entries; both bump the request generation.
    void play(const QString &infoHash, int fileIdx);
    // Task-only (nuvio-glm-local-streaming-20261020): explicit-magnet demand
    // for task-owned fixture endpoints, and an empty-session isolation check
    // that logs the engine's effective discovery diagnostics.
    void playMagnet(const QString &magnet, const QString &infoHash, int fileIdx);
    void playTorrentData(const QByteArray &data, const QString &infoHash, int fileIdx);
    void verifyIsolation(bool reuseVerifiedSession = false);
    void prefetch(const QString &infoHash, int fileIdx);
    // Speculative browsing cannot replace an explicit download's route.
    Q_INVOKABLE void warmSource(const QString &infoHash, int fileIdx);
    // The player closed: supersede in-flight foreground demand, retire the
    // foreground route and remove torrents no prefetch or demand still owns.
    void stop();

    // Deliberately does not start torrent networking.
    void warmUp();

    [[nodiscard]] QString streamUrl(const QString &infoHash, int fileIdx) const;

    void watchStats(const QString &infoHash, int fileIdx);
    void unwatchStats();

    // PlatformRuntime lifecycle gate: in the background the engine is
    // quiesced (streams stopped, routes invalidated); the next explicit
    // foreground demand reconstructs fresh engine state.
    void setForeground(bool foreground);
    // A successful Android foreground-download service grants only download
    // execution. It never grants playback or changes the real Activity state.
    Q_INVOKABLE void setBackgroundDownloadsActive(bool active);
    void stopPrefetch();

Q_SIGNALS:
    void readyChanged();
    void startingChanged();
    void engineUnavailableChanged();
    void streamReady(const QString &url, const QString &infoHash, int fileIdx);
    void fetchReady(const QString &url, const QString &infoHash, int fileIdx);
    void streamError(const QString &message);
    void streamStats(
        const QString &infoHash,
        int fileIdx,
        const QVariantMap &stats
    );

private:
    void requestPrefetch(const QString &infoHash, int fileIdx, bool download);
    struct WorkerState;
    std::shared_ptr<WorkerState> m_state;

};

} // namespace Colosseum::Nuvio

#endif // COLOSSEUM_PLAYER_NUVIOSTREAMBACKEND_H
