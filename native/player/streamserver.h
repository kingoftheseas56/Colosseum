// StreamServer — turns a torrent (infoHash + fileIdx) into a localhost HTTP URL mpv can play.
//
// It does NOT reimplement torrent streaming: it runs Tankoban 2's proven Stremio
// stream-server (`stremio-runtime.exe server.js`) as a child process, the same way TB2
// itself does. The runtime binds http://127.0.0.1:<port>/<infoHash>/<fileIdx> and we
// surface that URL to QML.
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

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>

class QProcess;
class QNetworkAccessManager;
class QTimer;

class StreamServer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool starting READ starting NOTIFY startingChanged)
public:
    explicit StreamServer(QObject *parent = nullptr);
    ~StreamServer() override;

    bool ready() const { return m_port > 0; }
    bool starting() const { return m_starting; }

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

Q_SIGNALS:
    void readyChanged();
    void startingChanged();
    void streamReady(const QString &url, const QString &infoHash, int fileIdx);
    void fetchReady(const QString &url, const QString &infoHash, int fileIdx);
    void streamError(const QString &message);
    // keys: peers, unchoked, downloaded, downloadSpeed, streamProgress, streamLen
    void streamStats(const QString &infoHash, int fileIdx, const QVariantMap &stats);

private:
    struct Pending {
        QString infoHash;
        int fileIdx;
        bool fetch = false;   // true -> answer with fetchReady (download), not streamReady
    };

    void ensureStarted();                 // adopt a running official server, else launch our own
    void launchChild();                   // spawn stremio-runtime.exe server.js ourselves
    QString findRuntimeDir() const;       // first dir that contains stremio-runtime.exe
    void onStdout();                      // scrape the "EngineFS server started at …:<port>" line
    void flushPending();
    void registerThenReady(const QString &infoHash, int fileIdx, bool fetch);  // POST /create, then emit URL
    void pollStats();                     // one stats.json GET; single-flight behind m_statsInflight
    void pushTunedSettings();             // raise the runtime's swarm caps, THEN flush pending streams

    QProcess *m_proc = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    int m_port = -1;
    bool m_starting = false;
    QString m_stdoutBuf;
    QList<Pending> m_pending;
    QTimer *m_statsTimer = nullptr;
    QString m_statsHash;
    int m_statsIdx = -1;
    bool m_statsInflight = false;
};

#endif // COLOSSEUM_STREAMSERVER_H
