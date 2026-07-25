#pragma once

#include "player2/core/Player2Types.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QUrl>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace Colosseum::Player2 {

// What the source knows about the origin, decided from the first response plus the request flags.
// This is the typed answer to "seekable / range-supported / known-duration / live" the task needs.
struct SourceCapabilities
{
    bool seekable = false;       // byte-range seek is possible (server served 206 / Accept-Ranges)
    bool rangeSupported = false; // the server honoured an explicit Range request
    bool knownDuration = false;  // the total byte length is known (so a duration can be derived)
    bool live = false;           // caller marked it live, or length is unknown and unseekable
    qint64 totalBytes = -1;      // total resource length, or -1 when unknown
};

// Honest transport state. It maps one-to-one onto the player's Buffering / Recovering / Error
// truth so no layer has to guess: the source is the only thing that knows the network reality.
enum class NetworkState
{
    Idle,       // constructed, not opened
    Connecting, // opening the first response (maps to Opening at the session)
    Streaming,  // read-ahead healthy, feeding the demuxer (maps to Playing)
    Buffering,  // read-ahead underran; the demuxer is starved (mpv cache-pause parity)
    Recovering, // a mid-stream disconnect; reconnecting with bounded attempts
    Ended,      // the body reached its end
    Failed,     // terminal: reconnect attempts exhausted or an unrecoverable error
};

// Response headers the source needs from a transport open. Everything else is intentionally hidden
// behind the transport so no credential ever reaches a log or a diagnostic snapshot.
struct HttpResponse
{
    int statusCode = 0;
    qint64 rangeStart = 0;     // first byte actually served (non-zero only on a 206 Content-Range)
    qint64 totalBytes = -1;    // total resource length if known, else -1
    bool partial = false;      // 206 Partial Content
    bool acceptRanges = false; // Accept-Ranges: bytes advertised on the response
};

// A blocking HTTP transport seam. The real implementation talks to the network; tests inject a
// deterministic fake. The source drives every call from its own fetch thread (start/read/close)
// and can wake a blocked call from any thread with cancel().
class IHttpTransport
{
public:
    virtual ~IHttpTransport() = default;

    // Open a GET for [byteOffset, end) with the request headers applied. Blocks until the response
    // headers arrive or cancel() is called. Returns false and fills error on a connect/HTTP error.
    virtual bool start(const QUrl &url, const RequestHeaders &headers, qint64 byteOffset,
                       HttpResponse *response, QString *error) = 0;
    // Read up to maxLen body bytes into dst. Returns >0 bytes read, 0 at a clean EOF, or -1 on a
    // mid-stream failure that a reconnect may recover. Blocks until data / EOF / failure / cancel.
    virtual int read(char *dst, int maxLen) = 0;
    virtual void close() = 0;
    // Unblock any in-flight start()/read() so it returns promptly. Idempotent and thread-safe.
    virtual void cancel() = 0;
    // How long a CONNECTED but silent origin may say nothing before start()/read() calls it a
    // failure. The SOURCE owns this number, not the socket: how patient to be with a progressive
    // origin is a policy decision. A genuinely dropped connection does not depend on it at all —
    // the socket goes Unconnected and the wait ends immediately — so this only ever governs an
    // origin that is alive and still fetching. Default no-op: fakes need nothing.
    virtual void setStallTimeoutMs(int /*milliseconds*/) {}
};

// Bounds and reconnect policy. Injectable so tests exercise buffering and reconnect without real
// wall-clock delays (reconnectDelayMs returns 0 in tests). Defaults mirror the current mpv player:
// a bounded read-ahead and a capped reconnect backoff (reconnect_delay_max=10s).
struct HttpSourcePolicy
{
    int highWaterBytes = 8 * 1024 * 1024;  // stop fetching above this (backpressure; one cache)
    int lowWaterBytes = 64 * 1024;         // resume fetching below this
    int resumeBytes = 512 * 1024;          // leave Buffering once the ring refills to here
    int maxReconnectAttempts = 5;          // bounded reconnect before a terminal Failed
    // Bounded retries for a SEEK whose target the origin does not hold yet (a torrent still
    // downloading). Separate budget from mid-stream reconnects: nothing is broken, the bytes just
    // have not arrived, so this is deliberately more patient.
    int maxSeekOpenAttempts = 8;
    // How long a connected-but-silent origin may stall a normal read before it counts as a
    // mid-stream failure. Handed to the transport at open().
    int stallTimeoutMs = 20'000;
    // Total wall-clock patience for a seek target the origin does not hold yet. This is the bound
    // that matters, because the Stremio sidecar answers such a range with SILENCE, not a refusal:
    // an attempt COUNT multiplied by a socket timeout is not a budget anyone can reason about.
    int seekStallBudgetMs = 90'000;
    std::function<int(int)> reconnectDelayMs; // delay before attempt N; nullptr => capped backoff
};

// An HTTP(S) byte source for FFmpeg. It exists so that WE, not FFmpeg's internal http protocol,
// own buffering truth, ranged seeks, reconnect policy and header redaction. FFmpeg consumes it
// through a custom AVIOContext (read + seek callbacks). "QML paints; C++ decides" — this is the
// C++ that decides transport, cache and threading.
//
// Thread ownership (documented per the task):
//   * fetch thread   — owns the IHttpTransport, fills the bounded ring, drives reconnect.
//   * demux thread   — calls read()/seek() (the AVIO callbacks); drains the ring, blocks on
//                      underrun (publishing Buffering) with a cancel-aware wait.
//   * any thread     — cancel() wakes both promptly (cancellation within the 2s bound).
// There is exactly one buffer (the ring); the transport does not keep a second cache.
class HttpMediaSource
{
public:
    HttpMediaSource(std::unique_ptr<IHttpTransport> transport, PlaybackRequest request,
                    HttpSourcePolicy policy = {});
    ~HttpMediaSource();

    HttpMediaSource(const HttpMediaSource &) = delete;
    HttpMediaSource &operator=(const HttpMediaSource &) = delete;

    // Open the first response and start the fetch thread. Returns false + error if the origin is
    // unreachable. capabilities() is valid once this returns true.
    bool open(QString *error);
    SourceCapabilities capabilities() const;

    // AVIO read: drains the ring. Blocks on underrun (publishing Buffering) until data arrives,
    // EOF, or cancel. Returns bytes read, 0 at EOF, or a negative code on cancel / terminal error.
    int read(char *dst, int maxLen);
    // AVIO seek. whence is SEEK_SET / SEEK_CUR / SEEK_END or AVSEEK_SIZE. Returns the new absolute
    // position, or a negative value when the source is not seekable / the size is unknown.
    qint64 seek(qint64 offset, int whence);
    // Wake and stop everything; a blocked read() returns promptly.
    void cancel();

    NetworkState state() const noexcept;
    // Invoked on the fetch/demux thread whenever the network state changes; the caller marshals to
    // the GUI thread. Set before open().
    void setStateCallback(std::function<void(NetworkState)> callback);

    // Thread-safe diagnostic snapshots.
    qint64 bufferedBytes() const noexcept;
    int reconnectCount() const noexcept;
    qint64 position() const noexcept;

    static bool isHttpUrl(const QUrl &url);

private:
    void fetchLoop();
    void setState(NetworkState next);
    int reconnectDelayFor(int attempt) const;
    qint64 knownSize() const; // total bytes if known, else -1 (call with m_mutex held)

    std::unique_ptr<IHttpTransport> m_transport;
    PlaybackRequest m_request;
    HttpSourcePolicy m_policy;

    SourceCapabilities m_capabilities;
    std::function<void(NetworkState)> m_stateCallback;

    mutable std::mutex m_mutex;
    std::condition_variable m_dataReady;    // demux read waits here on underrun
    std::condition_variable m_spaceReady;   // fetch thread waits here on backpressure
    std::condition_variable m_openReady;    // open() waits here for the first response
    bool m_openResolved = false;            // the fetch thread has attempted the first connection
    bool m_openOk = false;                  // the first connection succeeded
    QString m_openError;                    // failure detail for open()
    std::deque<QByteArray> m_ring;          // buffered body chunks, in order
    qint64 m_ringBytes = 0;                 // sum of chunk sizes still unread
    int m_frontOffset = 0;                  // bytes already consumed from m_ring.front()

    qint64 m_readPosition = 0;              // absolute byte offset the demuxer has consumed to
    qint64 m_fetchPosition = 0;             // absolute byte offset the fetch thread will request
    qint64 m_seekRequest = -1;              // a pending absolute seek target for the fetch thread
    bool m_eof = false;                     // the fetch thread saw a clean end of body
    bool m_hasStreamed = false;             // true once any body byte has been buffered

    std::atomic<NetworkState> m_state{NetworkState::Idle};
    std::atomic_bool m_cancelled{false};
    std::atomic<int> m_reconnectCount{0};

    std::thread m_fetch;
};

} // namespace Colosseum::Player2
