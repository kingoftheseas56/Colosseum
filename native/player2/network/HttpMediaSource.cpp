#include "HttpMediaSource.h"

#include <QtCore/QElapsedTimer>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>

namespace Colosseum::Player2 {
namespace {

// FFmpeg's AVSEEK_SIZE / AVSEEK_FORCE values, defined locally so this file needs no FFmpeg headers
// (the AVIO glue that supplies these lives in DemuxSession, which owns the FFmpeg dependency).
constexpr int kAvseekSize = 0x10000;
constexpr int kAvseekForce = 0x20000;
constexpr int kTerminal = -1; // negative sentinel returned to the demuxer on cancel / terminal error

int defaultReconnectDelayMs(int attempt)
{
    // Capped exponential backoff, mirroring the current mpv player's reconnect_delay_max=10s.
    const int capped = std::min(attempt, 6);
    return std::min(10'000, 250 * (1 << capped));
}

} // namespace

HttpMediaSource::HttpMediaSource(std::unique_ptr<IHttpTransport> transport, PlaybackRequest request,
                                 HttpSourcePolicy policy)
    : m_transport(std::move(transport)), m_request(std::move(request)), m_policy(std::move(policy))
{
    if (!m_policy.reconnectDelayMs)
        m_policy.reconnectDelayMs = &defaultReconnectDelayMs;
}

HttpMediaSource::~HttpMediaSource()
{
    cancel();
    if (m_fetch.joinable())
        m_fetch.join();
}

bool HttpMediaSource::isHttpUrl(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    return scheme == QStringLiteral("http") || scheme == QStringLiteral("https");
}

int HttpMediaSource::reconnectDelayFor(int attempt) const
{
    return m_policy.reconnectDelayMs ? m_policy.reconnectDelayMs(attempt) : 0;
}

void HttpMediaSource::setStateCallback(std::function<void(NetworkState)> callback)
{
    m_stateCallback = std::move(callback);
}

void HttpMediaSource::setState(NetworkState next)
{
    const NetworkState previous = m_state.exchange(next, std::memory_order_acq_rel);
    if (previous != next && m_stateCallback)
        m_stateCallback(next);
}

bool HttpMediaSource::open(QString *error)
{
    setState(NetworkState::Connecting);
    // The fetch thread owns the transport for its whole life, so it makes the first connection too;
    // open() blocks here until the first response resolves. This keeps a real single-threaded socket
    // transport free of any cross-thread affinity hazard.
    m_fetch = std::thread(&HttpMediaSource::fetchLoop, this);
    std::unique_lock lock(m_mutex);
    m_openReady.wait(lock, [this] { return m_openResolved; });
    if (!m_openOk && error)
        *error = m_openError;
    return m_openOk;
}

SourceCapabilities HttpMediaSource::capabilities() const
{
    std::scoped_lock lock(m_mutex);
    return m_capabilities;
}

NetworkState HttpMediaSource::state() const noexcept
{
    return m_state.load(std::memory_order_acquire);
}

qint64 HttpMediaSource::bufferedBytes() const noexcept
{
    std::scoped_lock lock(m_mutex);
    return m_ringBytes;
}

int HttpMediaSource::reconnectCount() const noexcept
{
    return m_reconnectCount.load(std::memory_order_acquire);
}

qint64 HttpMediaSource::position() const noexcept
{
    std::scoped_lock lock(m_mutex);
    return m_readPosition;
}

qint64 HttpMediaSource::knownSize() const
{
    return m_capabilities.knownDuration ? m_capabilities.totalBytes : -1;
}

void HttpMediaSource::cancel()
{
    // Set the flag under m_mutex so it is ordered against read()/fetchLoop predicate evaluation;
    // this closes the lost-wakeup window with the lock-free notifies that follow.
    {
        std::scoped_lock lock(m_mutex);
        m_cancelled.store(true, std::memory_order_release);
    }
    if (m_transport)
        m_transport->cancel();
    m_dataReady.notify_all();
    m_spaceReady.notify_all();
    m_openReady.notify_all();
}

int HttpMediaSource::read(char *dst, int maxLen)
{
    if (maxLen <= 0)
        return 0;
    std::unique_lock lock(m_mutex);
    for (;;) {
        if (m_cancelled.load(std::memory_order_acquire))
            return kTerminal;
        if (m_state.load(std::memory_order_acquire) == NetworkState::Failed)
            return kTerminal;

        if (m_ringBytes > 0) {
            int copied = 0;
            while (copied < maxLen && m_ringBytes > 0) {
                QByteArray &front = m_ring.front();
                const int avail = front.size() - m_frontOffset;
                const int take = std::min(avail, maxLen - copied);
                std::memcpy(dst + copied, front.constData() + m_frontOffset,
                            static_cast<size_t>(take));
                copied += take;
                m_frontOffset += take;
                m_ringBytes -= take;
                if (m_frontOffset >= front.size()) {
                    m_ring.pop_front();
                    m_frontOffset = 0;
                }
            }
            m_readPosition += copied;
            // Leave Buffering once a cushion has rebuilt (or the body ended) — cache-pause parity.
            if (m_state.load(std::memory_order_acquire) == NetworkState::Buffering &&
                (m_ringBytes >= m_policy.resumeBytes || m_eof))
                setState(NetworkState::Streaming);
            m_spaceReady.notify_all();
            return copied;
        }

        if (m_eof) {
            setState(NetworkState::Ended);
            return 0;
        }

        // The ring is empty and the body has not ended: a genuine underrun. Only report Buffering
        // once we have actually streamed, so the initial pre-roll wait is not mislabelled.
        if (m_hasStreamed && m_state.load(std::memory_order_acquire) == NetworkState::Streaming)
            setState(NetworkState::Buffering);
        // Predicate-based wait: cancel() sets m_cancelled under m_mutex, so a cancel that arrives
        // between the checks above and this wait is never lost (no hang on close/switch).
        m_dataReady.wait(lock, [this] {
            return m_cancelled.load(std::memory_order_acquire) ||
                   m_state.load(std::memory_order_acquire) == NetworkState::Failed ||
                   m_ringBytes > 0 || m_eof;
        });
    }
}

qint64 HttpMediaSource::seek(qint64 offset, int whence)
{
    std::unique_lock lock(m_mutex);
    const int mode = whence & ~kAvseekForce;
    if (mode == kAvseekSize)
        return knownSize();
    if (!m_capabilities.seekable)
        return kTerminal;

    qint64 target = -1;
    switch (mode) {
    case SEEK_SET:
        target = offset;
        break;
    case SEEK_CUR:
        target = m_readPosition + offset;
        break;
    case SEEK_END: {
        const qint64 size = knownSize();
        if (size < 0)
            return kTerminal;
        target = size + offset;
        break;
    }
    default:
        return kTerminal;
    }
    if (target < 0)
        return kTerminal;
    const qint64 size = knownSize();
    if (size >= 0 && target > size)
        target = size;

    // Reposition: drop the read-ahead, point the fetch thread at the new offset and wake it. The
    // fetch loop reissues a ranged request; a blocked transport read is interrupted below.
    m_ring.clear();
    m_ringBytes = 0;
    m_frontOffset = 0;
    m_eof = false;
    m_readPosition = target;
    m_fetchPosition = target;
    m_seekRequest = target;
    lock.unlock();
    m_transport->cancel(); // unblock any in-flight transport.read so the fetch loop sees the seek
    m_spaceReady.notify_all();
    m_dataReady.notify_all();
    return target;
}

void HttpMediaSource::fetchLoop()
{
    // The transport does not get to decide how patient we are with a slow origin; we do.
    m_transport->setStallTimeoutMs(m_policy.stallTimeoutMs);

    // First connection: resolve open() with the discovered capabilities (or a failure).
    {
        qint64 startOffset = 0;
        {
            std::scoped_lock lock(m_mutex);
            startOffset = m_fetchPosition;
        }
        HttpResponse response;
        QString error;
        if (!m_transport->start(m_request.source, m_request.headers, startOffset, &response,
                                &error)) {
            std::scoped_lock lock(m_mutex);
            m_openOk = false;
            m_openError = error;
            m_openResolved = true;
            setState(NetworkState::Failed);
            m_openReady.notify_all();
            return;
        }
        SourceCapabilities caps;
        caps.rangeSupported = response.acceptRanges || response.partial;
        caps.knownDuration = response.totalBytes >= 0;
        caps.totalBytes = response.totalBytes;
        caps.seekable = caps.rangeSupported;
        caps.live = m_request.live || (!caps.knownDuration && !caps.rangeSupported);
        std::scoped_lock lock(m_mutex);
        m_capabilities = caps;
        m_fetchPosition = response.rangeStart;
        m_readPosition = response.rangeStart;
        m_openOk = true;
        m_openResolved = true;
        m_openReady.notify_all();
    }

    QByteArray chunk;
    chunk.resize(64 * 1024);
    for (;;) {
        qint64 seekTarget = -1;
        {
            std::unique_lock lock(m_mutex);
            m_spaceReady.wait(lock, [this] {
                return m_cancelled.load(std::memory_order_acquire) || m_seekRequest >= 0 ||
                       (!m_eof && m_ringBytes < m_policy.highWaterBytes);
            });
            if (m_cancelled.load(std::memory_order_acquire))
                return;
            if (m_seekRequest >= 0) {
                seekTarget = m_seekRequest;
                m_seekRequest = -1;
            } else if (m_eof) {
                continue; // parked at EOF; only a seek or cancel makes progress
            }
        }

        if (seekTarget >= 0) {
            qint64 size = -1;
            {
                std::scoped_lock lock(m_mutex);
                size = knownSize();
            }
            // Seeking to or past the end (FFmpeg probes mkv by seeking to EOF) must not request an
            // out-of-range byte offset — a conformant server answers 416. Park at EOF instead.
            if (size >= 0 && seekTarget >= size) {
                std::scoped_lock lock(m_mutex);
                m_eof = true;
                m_dataReady.notify_all();
                continue;
            }
            m_transport->close();
            HttpResponse response;
            QString error;
            // A progressive origin (a torrent still downloading) does not hold the seek target YET.
            // Measured 2026-07-25 against the real Stremio EngineFS sidecar and a fixture that
            // copies it: such an origin does not refuse and does not misalign - it ACCEPTS the
            // connection and says NOTHING until the pieces land (server.js:18267, which stages a
            // 206 at the exact offset and only emits it with the first body byte). So the wait is
            // Buffering from the first instant, and the bound has to be WALL CLOCK: an attempt
            // count times a socket timeout is what turned one legitimate stall into 8 x 20s of
            // frozen Seeking followed by a killed session.
            //
            // Spend the budget on ONE held-open connection rather than a run of teardowns. Each
            // teardown destroys the read stream the origin had already prioritised for us, so
            // retrying makes the wait longer, not shorter. Retries remain only for an origin that
            // refuses or drops us quickly - which is cheap, and bounded by maxSeekOpenAttempts.
            setState(NetworkState::Buffering);
            QElapsedTimer seekBudget;
            seekBudget.start();
            bool seekOpened = false;
            bool seekSuperseded = false;
            for (int attempt = 1; ; ++attempt) {
                const qint64 remainingMs =
                    m_policy.seekStallBudgetMs - seekBudget.elapsed();
                if (remainingMs <= 0) {
                    setState(NetworkState::Failed);
                    std::scoped_lock lock(m_mutex);
                    m_dataReady.notify_all();
                    return;
                }
                // Wait out whatever is left of the budget on this one attempt; cancel() and a newer
                // seek both interrupt it, so patience never costs responsiveness.
                m_transport->setStallTimeoutMs(static_cast<int>(
                    std::min<qint64>(remainingMs, std::numeric_limits<int>::max())));
                const bool opened = m_transport->start(m_request.source, m_request.headers,
                                                       seekTarget, &response, &error);
                m_transport->setStallTimeoutMs(m_policy.stallTimeoutMs);
                if (opened) {
                    seekOpened = true;
                    break;
                }
                if (m_cancelled.load(std::memory_order_acquire))
                    return;
                {
                    // A newer seek supersedes this one and must not burn its budget.
                    std::scoped_lock lock(m_mutex);
                    if (m_seekRequest >= 0) {
                        seekSuperseded = true;
                        break;
                    }
                }
                if (attempt >= m_policy.maxSeekOpenAttempts ||
                    seekBudget.elapsed() >= m_policy.seekStallBudgetMs) {
                    setState(NetworkState::Failed);
                    std::scoped_lock lock(m_mutex);
                    m_dataReady.notify_all();
                    return;
                }
                const int delayMs = reconnectDelayFor(attempt);
                if (delayMs > 0) {
                    std::unique_lock lock(m_mutex);
                    m_spaceReady.wait_for(lock, std::chrono::milliseconds(delayMs), [this] {
                        return m_cancelled.load(std::memory_order_acquire) || m_seekRequest >= 0;
                    });
                }
                if (m_cancelled.load(std::memory_order_acquire))
                    return;
                m_transport->close();
            }
            if (!seekOpened) {
                if (seekSuperseded)
                    continue; // handle the newer seek at the top of the loop
                return;
            }
            // A server that ignored the range (served 200 from 0) would splice misaligned bytes at
            // the seek target; fail instead of corrupting the stream.
            if (response.rangeStart != seekTarget) {
                setState(NetworkState::Failed);
                std::scoped_lock lock(m_mutex);
                m_dataReady.notify_all();
                return;
            }
            continue;
        }

        const int n = m_transport->read(chunk.data(), chunk.size());

        if (m_cancelled.load(std::memory_order_acquire))
            return;

        // Evaluate the seek-check and the append under one lock. If a seek landed while this chunk
        // was in flight it cleared the ring; appending here would splice stale bytes past the seek,
        // so drop the chunk and let the top handle the seek.
        {
            std::scoped_lock lock(m_mutex);
            if (m_seekRequest >= 0)
                continue;
            if (n > 0) {
                m_ring.emplace_back(chunk.constData(), n);
                m_ringBytes += n;
                m_fetchPosition += n;
                m_hasStreamed = true;
                if (m_state.load(std::memory_order_acquire) != NetworkState::Streaming)
                    setState(NetworkState::Streaming);
                m_dataReady.notify_all();
                continue;
            }
            if (n == 0) {
                m_eof = true;
                m_dataReady.notify_all();
                continue; // park at the top until a seek or cancel
            }
        }

        // n < 0: a mid-stream disconnect. Reconnect with bounded attempts from the last byte, but a
        // seek that arrived alongside the disconnect takes precedence and must not burn an attempt.
        {
            std::scoped_lock lock(m_mutex);
            if (m_seekRequest >= 0)
                continue;
        }
        if (m_reconnectCount.load(std::memory_order_acquire) >= m_policy.maxReconnectAttempts) {
            setState(NetworkState::Failed);
            std::scoped_lock lock(m_mutex);
            m_dataReady.notify_all();
            return;
        }
        const int attempt = m_reconnectCount.fetch_add(1, std::memory_order_acq_rel) + 1;
        setState(NetworkState::Recovering);
        const int delayMs = reconnectDelayFor(attempt);
        if (delayMs > 0) {
            std::unique_lock lock(m_mutex);
            m_spaceReady.wait_for(lock, std::chrono::milliseconds(delayMs), [this] {
                return m_cancelled.load(std::memory_order_acquire) || m_seekRequest >= 0;
            });
        }
        if (m_cancelled.load(std::memory_order_acquire))
            return;
        m_transport->close();
        qint64 resumeAt = 0;
        {
            std::scoped_lock lock(m_mutex);
            if (m_seekRequest >= 0)
                continue; // a seek arrived during the backoff; handle it at the top
            resumeAt = m_fetchPosition;
        }
        HttpResponse response;
        QString error;
        if (!m_transport->start(m_request.source, m_request.headers, resumeAt, &response, &error)) {
            // A failed reconnect start counts against the bound; loop to retry or fail.
            continue;
        }
        // If the reconnect did not resume from the requested byte (server ignored the range, or the
        // source is not seekable), resuming would corrupt the stream — fail cleanly instead.
        if (response.rangeStart != resumeAt) {
            setState(NetworkState::Failed);
            std::scoped_lock lock(m_mutex);
            m_dataReady.notify_all();
            return;
        }
        // Reconnected; the next read() serves data and restores Streaming.
    }
}

} // namespace Colosseum::Player2
