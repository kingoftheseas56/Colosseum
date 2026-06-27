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

class QProcess;
class QNetworkAccessManager;

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

    // The URL for an already-registered stream, or "" if the runtime isn't up yet.
    Q_INVOKABLE QString streamUrl(const QString &infoHash, int fileIdx) const;

Q_SIGNALS:
    void readyChanged();
    void startingChanged();
    void streamReady(const QString &url, const QString &infoHash, int fileIdx);
    void streamError(const QString &message);

private:
    struct Pending {
        QString infoHash;
        int fileIdx;
    };

    void ensureStarted();
    QString findRuntimeDir() const;       // first dir that contains stremio-runtime.exe
    void onStdout();                      // scrape the "EngineFS server started at …:<port>" line
    void flushPending();
    void registerThenReady(const QString &infoHash, int fileIdx);  // POST /create, then emit URL

    QProcess *m_proc = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    int m_port = -1;
    bool m_starting = false;
    QString m_stdoutBuf;
    QList<Pending> m_pending;
};

#endif // COLOSSEUM_STREAMSERVER_H
