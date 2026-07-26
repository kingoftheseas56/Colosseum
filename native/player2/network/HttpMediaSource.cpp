#include "HttpMediaSource.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QtGlobal>

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
// A read that gave up so its caller can service a pending command. Negative like kTerminal (FFmpeg's
// AVIO contract has no third answer), but NOT a failure: the caller asks consumeReadInterrupt().
constexpr int kInterrupted = -2;

int defaultReconnectDelayMs(int attempt)
{
    // Capped exponential backoff, mirroring the current mpv player's reconnect_delay_max=10s.
    const int capped = std::min(attempt, 6);
    return std::min(10'000, 250 * (1 << capped));
}

int clampToIntMs(qint64 milliseconds)
{
    return static_cast<int>(std::min<qint64>(milliseconds, std::numeric_limits<int>::max()));
}

// The two stall causes, in the viewer's words. They are the transport's own account of what went
// wrong, carried up so the layer above never has to describe a network stall as a decode failure.
QString seekStallMessage(int budgetMs)
{
    return QStringLiteral("Network stalled: that part of the stream has not downloaded yet "
                          "(waited %1s)").arg(budgetMs / 1000);
}

QString frontierStallMessage(int budgetMs)
{
    return QStringLiteral("Network stalled: the stream stopped sending data (waited %1s)")
        .arg(budgetMs / 1000);
}

// A timeline for this layer, off unless COLOSSEUM_PLAYER2_NET_TRACE is set. It exists because the
// bound on this path has now been diagnosed twice from arithmetic alone; the next diagnosis should
// read a stamped run instead. One env check, one fprintf, zero cost when unset.
bool netTraceEnabled()
{
    static const bool enabled = qEnvironmentVariableIsSet("COLOSSEUM_PLAYER2_NET_TRACE");
    return enabled;
}

void netTrace(const QString &line)
{
    if (!netTraceEnabled())
        return;
    static QElapsedTimer since = [] { QElapsedTimer t; t.start(); return t; }();
    std::fprintf(stderr, "player2.net t=%lldms %s\n", static_cast<long long>(since.elapsed()),
                 line.toUtf8().constData());
    std::fflush(stderr);
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

void HttpMediaSource::setInterruptPredicate(std::function<bool()> predicate)
{
    std::scoped_lock lock(m_mutex);
    m_interruptPredicate = std::move(predicate);
}

void HttpMediaSource::wakeRead()
{
    // A pure wake: it changes no state, it only makes a parked read re-evaluate its predicate.
    // Taking the mutex first is what makes it reliable, exactly as cancel() does: the caller sets its
    // pending flag BEFORE calling here, so a read that is mid-way through evaluating the predicate
    // either has not looked yet (and will see the flag) or is already waiting (and gets this notify).
    // Without the lock, a notify landing in that window would be lost and the park would hold.
    { std::scoped_lock lock(m_mutex); }
    m_dataReady.notify_all();
}

bool HttpMediaSource::consumeReadInterrupt() noexcept
{
    return m_readWasInterrupted.exchange(false, std::memory_order_acq_rel);
}

void HttpMediaSource::setState(NetworkState next)
{
    const NetworkState previous = m_state.exchange(next, std::memory_order_acq_rel);
    if (previous != next && m_stateCallback)
        m_stateCallback(next);
}

void HttpMediaSource::setFailed(const QString &reason)
{
    {
        std::scoped_lock lock(m_mutex);
        if (m_terminalError.isEmpty())
            m_terminalError = reason;
    }
    netTrace(QStringLiteral("FAILED %1").arg(reason));
    setState(NetworkState::Failed);
}

QString HttpMediaSource::terminalError() const
{
    std::scoped_lock lock(m_mutex);
    return m_terminalError;
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

        // Nothing to give AND the owner has a command waiting: hand the thread back so it can run
        // it. Checked HERE, below the ring, so data always wins — if bytes are available we serve
        // them and the caller services its command on its next turn anyway. This is the only exit
        // from read() that is not terminal; consumeReadInterrupt() is how the caller tells them
        // apart. The predicate must be false again before the next read, or the read would abort
        // itself forever; the demux clears its pending flag when it services the command.
        if (m_interruptPredicate && m_interruptPredicate()) {
            m_readWasInterrupted.store(true, std::memory_order_release);
            netTrace(QStringLiteral("READ INTERRUPTED for a pending command at pos=%1")
                         .arg(m_readPosition));
            return kInterrupted;
        }

        // The ring is empty and the body has not ended: a genuine underrun. Only report Buffering
        // once we have actually streamed, so the initial pre-roll wait is not mislabelled.
        if (m_hasStreamed && m_state.load(std::memory_order_acquire) == NetworkState::Streaming)
            setState(NetworkState::Buffering);
        // Predicate-based wait: cancel() sets m_cancelled under m_mutex, so a cancel that arrives
        // between the checks above and this wait is never lost (no hang on close/switch). The
        // interrupt predicate rides the same wait, woken by wakeRead().
        m_dataReady.wait(lock, [this] {
            return m_cancelled.load(std::memory_order_acquire) ||
                   m_state.load(std::memory_order_acquire) == NetworkState::Failed ||
                   m_ringBytes > 0 || m_eof ||
                   (m_interruptPredicate && m_interruptPredicate());
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
            m_terminalError = QStringLiteral("Could not open the stream: %1").arg(error);
            m_openResolved = true;
            setState(NetworkState::Failed); // m_mutex is held here; setFailed would deadlock
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

    // ---- One mid-stream STALL EPISODE (fetch-thread state) ----------------------------------
    // A progressive origin does not only withhold a SEEK target; ordinary playback walks into the
    // same download frontier and the origin goes silent there too. That path had no wall-clock
    // bound at all — only an attempt count — and measured 233 s against a 90 s promise
    // (fixture 2026-07-25: five reconnects, every one at the frontier byte, 20 s silent open plus
    // 20 s silent read plus backoff each). So the episode is armed at the first mid-stream failure
    // and bounded by the SAME stallBudgetMs the seek path uses; the first byte that arrives
    // disarms it, because the stall is then genuinely over.
    QElapsedTimer stallEpisode;
    bool stalled = false;
    // Bytes this connection has delivered since the last reconnect. maxReconnectAttempts is a budget
    // for a FLAPPING origin, so it may only be cleared by recovery that is actually demonstrated —
    // a whole read-ahead refilled. Clearing it on a mere successful start() would re-arm the budget
    // every cycle for an origin that accepts and instantly drops, and the flap would never end.
    qint64 deliveredSinceReconnect = 0;

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
            netTrace(QStringLiteral("SEEK budget armed target=%1 budget=%2ms")
                         .arg(seekTarget)
                         .arg(m_policy.stallBudgetMs));
            QElapsedTimer seekBudget;
            seekBudget.start();
            bool seekOpened = false;
            bool seekSuperseded = false;
            for (int attempt = 1; ; ++attempt) {
                const qint64 remainingMs = m_policy.stallBudgetMs - seekBudget.elapsed();
                if (remainingMs <= 0) {
                    setFailed(seekStallMessage(m_policy.stallBudgetMs));
                    std::scoped_lock lock(m_mutex);
                    m_dataReady.notify_all();
                    return;
                }
                // Wait out whatever is left of the budget on this one attempt; cancel() and a newer
                // seek both interrupt it, so patience never costs responsiveness.
                m_transport->setStallTimeoutMs(clampToIntMs(remainingMs));
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
                    seekBudget.elapsed() >= m_policy.stallBudgetMs) {
                    setFailed(seekStallMessage(m_policy.stallBudgetMs));
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
                setFailed(QStringLiteral("Network error: the server ignored the seek byte range"));
                std::scoped_lock lock(m_mutex);
                m_dataReady.notify_all();
                return;
            }
            // The seek landed on a live connection: this is a fresh stream, so any earlier stall
            // episode is over and the reconnect budget starts clean at the new offset.
            stalled = false;
            deliveredSinceReconnect = 0;
            netTrace(QStringLiteral("SEEK opened target=%1 after %2ms")
                         .arg(seekTarget)
                         .arg(seekBudget.elapsed()));
            continue;
        }

        // While a stall episode is open the transport's patience is whatever is LEFT of the budget,
        // never a fresh stallTimeoutMs. That is what stops two independent 20 s waits per attempt
        // from stacking past the ceiling.
        if (stalled) {
            const qint64 remainingMs = m_policy.stallBudgetMs - stallEpisode.elapsed();
            if (remainingMs <= 0) {
                setFailed(frontierStallMessage(m_policy.stallBudgetMs));
                std::scoped_lock lock(m_mutex);
                m_dataReady.notify_all();
                return;
            }
            m_transport->setStallTimeoutMs(clampToIntMs(remainingMs));
        }
        const int n = m_transport->read(chunk.data(), chunk.size());
        if (stalled)
            m_transport->setStallTimeoutMs(m_policy.stallTimeoutMs);

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
                // Bytes arrived: the stall is over, so the wall-clock episode ends here. The
                // reconnect COUNT is a separate, stricter question and is answered below.
                stalled = false;
                deliveredSinceReconnect += n;
                if (m_reconnectCount.load(std::memory_order_acquire) > 0 &&
                    deliveredSinceReconnect >= m_policy.highWaterBytes) {
                    // A whole read-ahead refilled on this connection: the incident is genuinely
                    // over, so the next one gets the full budget instead of inheriting a spent one.
                    netTrace(QStringLiteral("RECOVERED after %1 bytes; reconnect budget reset")
                                 .arg(deliveredSinceReconnect));
                    m_reconnectCount.store(0, std::memory_order_release);
                }
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

        // n < 0: the stream stopped delivering — a dropped connection, or the download frontier of a
        // progressive origin going silent. A seek that arrived alongside it takes precedence and
        // must not burn an attempt.
        {
            std::scoped_lock lock(m_mutex);
            if (m_seekRequest >= 0)
                continue;
        }
        // Arm the wall-clock episode on the FIRST failure. Everything below spends what remains of
        // it; two attempt budgets (count and clock) both apply and the tighter one wins.
        if (!stalled) {
            stallEpisode.start();
            stalled = true;
            netTrace(QStringLiteral("STALL armed budget=%1ms").arg(m_policy.stallBudgetMs));
        }
        qint64 remainingMs = m_policy.stallBudgetMs - stallEpisode.elapsed();
        if (remainingMs <= 0) {
            setFailed(frontierStallMessage(m_policy.stallBudgetMs));
            std::scoped_lock lock(m_mutex);
            m_dataReady.notify_all();
            return;
        }
        if (m_reconnectCount.load(std::memory_order_acquire) >= m_policy.maxReconnectAttempts) {
            setFailed(QStringLiteral("Network stream lost: reconnecting failed %1 times")
                          .arg(m_policy.maxReconnectAttempts));
            std::scoped_lock lock(m_mutex);
            m_dataReady.notify_all();
            return;
        }
        const int attempt = m_reconnectCount.fetch_add(1, std::memory_order_acq_rel) + 1;
        deliveredSinceReconnect = 0;
        setState(NetworkState::Recovering);
        // The backoff is part of the episode, not extra to it.
        const int delayMs = static_cast<int>(
            std::min<qint64>(reconnectDelayFor(attempt), remainingMs));
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
        remainingMs = m_policy.stallBudgetMs - stallEpisode.elapsed();
        if (remainingMs <= 0) {
            setFailed(frontierStallMessage(m_policy.stallBudgetMs));
            std::scoped_lock lock(m_mutex);
            m_dataReady.notify_all();
            return;
        }
        HttpResponse response;
        QString error;
        netTrace(QStringLiteral("RECONNECT attempt=%1 offset=%2 patience=%3ms")
                     .arg(attempt).arg(resumeAt).arg(remainingMs));
        // Same doctrine as the seek path: spend what is left of the budget WAITING on this one
        // connection rather than re-dialling. A silent origin is still fetching for us; a genuinely
        // dead one refuses or drops immediately and costs nothing.
        m_transport->setStallTimeoutMs(clampToIntMs(remainingMs));
        const bool reconnected =
            m_transport->start(m_request.source, m_request.headers, resumeAt, &response, &error);
        m_transport->setStallTimeoutMs(m_policy.stallTimeoutMs);
        if (!reconnected) {
            // A failed reconnect start counts against the bound; loop to retry or fail.
            netTrace(QStringLiteral("RECONNECT attempt=%1 failed after %2ms: %3")
                         .arg(attempt).arg(stallEpisode.elapsed()).arg(error));
            continue;
        }
        // If the reconnect did not resume from the requested byte (server ignored the range, or the
        // source is not seekable), resuming would corrupt the stream — fail cleanly instead.
        if (response.rangeStart != resumeAt) {
            setFailed(QStringLiteral("Network error: the server ignored the resume byte range"));
            std::scoped_lock lock(m_mutex);
            m_dataReady.notify_all();
            return;
        }
        // Reconnected; the next read() serves data and restores Streaming.
    }
}

} // namespace Colosseum::Player2
