// StreamServer — turns a torrent (infoHash + fileIdx) into a localhost HTTP URL mpv can play.
//
// It does NOT reimplement torrent streaming: it runs Stremio's proven stream-server
// runtime (`stremio-runtime[.exe] server.js`) as a child process when no official
// Stremio Service is already answering on :11470. The runtime binds
// http://127.0.0.1:<port>/<infoHash>/<fileIdx> and we surface that URL to QML.
//
// Lifecycle: lazy — the 88 MB runtime is only spawned on the FIRST play() call, so a
// session that never watches anything never pays for it. Killed on app exit.
//
// QML contract (exposed as the context property `Stream`):
//   Stream.play(infoHash, fileIdx)      -> eventually emits streamReady(url, infoHash, fileIdx)
//   Stream.ready                         -> bool, true once the runtime's port is known
//   onStreamReady(url, infoHash, idx)    -> hand `url` to MpvItem.loadFile(url)
//   onStreamError(message)               -> show the message; playback won't start
#ifndef COLOSSEUM_STREAMSERVER_H
#define COLOSSEUM_STREAMSERVER_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>

// The Android facade shape (backend member + Android declarations) is active
// on Android, and also on a host build that defines
// COLOSSEUM_ANDROID_NUVIO_BACKEND purely to compile the facade translation
// unit in focused tests. That host shape is compile/behavior coverage only;
// desktop production never defines the macro.
#if defined(Q_OS_ANDROID) || defined(COLOSSEUM_ANDROID_NUVIO_BACKEND)
#include <functional>
#include <memory>
namespace Colosseum::Nuvio { class NuvioStreamBackend; }
#endif

class QProcess;
class QNetworkAccessManager;
class QTimer;
class QFileSystemWatcher;

class StreamServer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool starting READ starting NOTIFY startingChanged)
    Q_PROPERTY(bool engineUnavailable READ engineUnavailable NOTIFY engineUnavailableChanged)
public:
    explicit StreamServer(QObject *parent = nullptr);
    ~StreamServer() override;

#if defined(Q_OS_ANDROID) || defined(COLOSSEUM_ANDROID_NUVIO_BACKEND)
    // Android: ready/starting delegate to the opt-in Nuvio backend (see
    // streamserver_android.cpp); the desktop inline accessors below stay the
    // single source of truth for the runtime-child implementation.
    bool ready() const;
    bool starting() const;
    // PlatformRuntime lifecycle gate: the backend quiesces in the background
    // and reconstructs fresh engine state on the next explicit demand.
    void setForeground(bool foreground);
    void setBackgroundDownloadsActive(bool active);
    void stopFetchDemand();
    // The live application foreground state. QML handlers of the same
    // lifecycle signal run before the C++ setForeground() slot, so a demand
    // issued from them syncs the backend first instead of being fenced.
    void setForegroundProbe(std::function<bool()> probe);
#else
    bool ready() const { return m_port > 0; }
    bool starting() const { return m_starting; }
#endif
    bool engineUnavailable() const { return m_engineUnavailable; }

    // Start the stream (spawning the runtime if needed) and emit streamReady when the
    // torrent is registered and a playable URL exists.
    Q_INVOKABLE void play(const QString &infoHash, int fileIdx);

    // Same warm-up, but for background DOWNLOADS: emits fetchReady instead of
    // streamReady, so the kept-alive player page never hijacks a download's url
    // into mpv. (PlayerPage.qml loads EVERY streamReady it hears.)
    Q_INVOKABLE void prefetch(const QString &infoHash, int fileIdx);

    // Bring the engine up BEFORE anything is played. It used to spawn lazily on the first play(), so
    // every session paid a cold start at the worst possible moment -- the instant Play was pressed. A
    // cold server has an empty DHT and no tracker answers, so it must bootstrap, announce, handshake
    // and wait to be unchoked before one byte of video arrives; a warm one has already paid all of it.
    // That is why the SAME years-old torrent with a constant ~100 seeders loaded in a split second
    // sometimes and took minutes other times: with 100 full seeders piece availability is never the
    // constraint -- being CONNECTED is. (The seed count is a tracker scrape: it says those peers
    // exist, not that we have reached them.) Idempotent; adopt-first still wins when the official
    // Stremio Service already owns :11470. This is what the Stremio app does -- we ship its identical
    // runtime and were simply starting it later. (2026-07-30, Theatre lane)
    Q_INVOKABLE void warmUp();

    // The URL for an already-registered stream, or "" if the runtime isn't up yet.
    Q_INVOKABLE QString streamUrl(const QString &infoHash, int fileIdx) const;

    // Pre-play telemetry (Popcorn Time streamer.js parity, 2026-08-02): poll the engine's
    // /:infoHash/:fileIdx/stats.json once a second — PT's exact stats cadence — and surface
    // peers / downloaded / downloadSpeed so the loading face can narrate the wait instead of
    // sitting on a static "Starting stream...". Watch starts at streamReady, QML stops it the
    // moment playback (or an error) retires the loading face.
    Q_INVOKABLE void watchStats(const QString &infoHash, int fileIdx);
    Q_INVOKABLE void unwatchStats();

    // The player closed for good (minimize never calls this): on Android the
    // Nuvio backend retires the route and removes the torrent's session
    // demand. The desktop runtime keeps its own torrent lifetime (no-op).
    Q_INVOKABLE void stopDemand();

Q_SIGNALS:
    void readyChanged();
    void startingChanged();
    void engineUnavailableChanged();
    void streamReady(const QString &url, const QString &infoHash, int fileIdx);
    void fetchReady(const QString &url, const QString &infoHash, int fileIdx);
    void streamError(const QString &message);
    // keys: peers, unchoked, downloaded, downloadSpeed, streamProgress, streamLen
    void streamStats(const QString &infoHash, int fileIdx, const QVariantMap &stats);
    // Task-only (nuvio-glm-local-streaming-20261020): raised when a task
    // trigger file requests a fixture play; QML opens the real player route.
    void fixturePlayRequested(const QString &infoHash, int fileIdx, const QString &title);

private:
#if defined(Q_OS_ANDROID) || defined(COLOSSEUM_ANDROID_NUVIO_BACKEND)
#ifdef COLOSSEUM_ANDROID_NUVIO_BACKEND
    std::unique_ptr<Colosseum::Nuvio::NuvioStreamBackend> m_backend;
    // Task-only fixture trigger plumbing (task-owned file in app storage).
    QByteArray m_pendingFixtureTorrentData;
    QString m_pendingFixtureInfoHash;
    bool m_diagnosticTask = false;
    QFileSystemWatcher *m_triggerWatcher = nullptr;
    QTimer *m_triggerDebounce = nullptr;
    void handleTriggerFile();
    QByteArray loadPinnedFixture(const QString &infoHash, int fileIdx) const;
#endif
    std::function<bool()> m_foregroundProbe;
    bool m_engineUnavailable = false;
    // Reflects runtime engine-unavailability (library resolve failure at
    // construction, engine create failure later) into the facade property
    // and its change notification.
    void setEngineUnavailable(bool unavailable);
#else
    struct Pending {
        QString infoHash;
        int fileIdx;
        bool fetch = false;   // true -> answer with fetchReady (download), not streamReady
    };


    void ensureStarted();                 // adopt a running official server, else launch our own
    void launchChild();                   // spawn the platform Stremio runtime + server.js ourselves
    void setEngineUnavailable(bool unavailable);
    void markEngineUnavailable(const QString &message);
    QString findRuntimeDir() const;       // first dir containing this platform's runtime + server.js
    void onStdout();                      // scrape the "EngineFS server started at …:<port>" line
    void flushPending();
    void registerThenReady(const QString &infoHash, int fileIdx, bool fetch);  // POST /create, then emit URL
    void recoverDeadServer(int failedPort); // retire the matching unreachable server and retry once
    void pollStats();                     // one stats.json GET; single-flight behind m_statsInflight
    void pushTunedSettings();             // raise the runtime's swarm caps, THEN flush pending streams

    QProcess *m_proc = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    int m_port = -1;
    bool m_starting = false;
    bool m_engineUnavailable = false;
    QString m_stdoutBuf;
    QList<Pending> m_pending;
    QTimer *m_statsTimer = nullptr;
    QString m_statsHash;
    int m_statsIdx = -1;
    bool m_statsInflight = false;
    int m_consecutiveHealthRestarts = 0;
#endif // Q_OS_ANDROID
};

#endif // COLOSSEUM_STREAMSERVER_H
