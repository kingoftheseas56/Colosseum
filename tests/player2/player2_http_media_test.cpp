// Hermetic tests for HttpMediaSource. A scripted in-process transport stands in for the network so
// the whole suite runs deterministically with no internet dependency. Each case proves one piece of
// the task contract: capabilities, ranged seek, honest Buffering/Recovering/Ended/Failed state,
// bounded reconnect and prompt cancellation.

#include "player2/network/HttpMediaSource.h"

#include <QtCore/QByteArray>
#include <QtCore/QUrl>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace Colosseum::Player2;

namespace {

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

QByteArray countedBody(int size)
{
    QByteArray body;
    body.resize(size);
    for (int i = 0; i < size; ++i)
        body[i] = static_cast<char>(i & 0xFF);
    return body;
}

// Records every NetworkState the source publishes, in order, so tests can assert honest sequences.
class StateLog
{
public:
    void record(NetworkState state)
    {
        std::scoped_lock lock(m_mutex);
        m_states.push_back(state);
    }
    std::vector<NetworkState> snapshot() const
    {
        std::scoped_lock lock(m_mutex);
        return m_states;
    }
    bool saw(NetworkState state) const
    {
        std::scoped_lock lock(m_mutex);
        for (NetworkState value : m_states)
            if (value == state)
                return true;
        return false;
    }
    // True if `first` appears before `second` at least once.
    bool sawInOrder(NetworkState first, NetworkState second) const
    {
        std::scoped_lock lock(m_mutex);
        bool sawFirst = false;
        for (NetworkState value : m_states) {
            if (value == first)
                sawFirst = true;
            else if (value == second && sawFirst)
                return true;
        }
        return false;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<NetworkState> m_states;
};

// A deterministic transport. It serves bytes from an in-memory resource and can be told to reject
// ranges, hide its length, disconnect after N bytes, stall until released, or block until cancelled.
class ScriptedTransport final : public IHttpTransport
{
public:
    QByteArray resource;
    bool totalKnown = true;    // advertise Content-Length / Content-Range total
    bool acceptRanges = true;  // advertise Accept-Ranges: bytes
    bool honorRange = true;    // actually serve from the requested offset
    int chunkSize = 64 * 1024; // bytes returned per read()

    // Fault injection. disconnectAfterBytes < 0 disables. When alwaysDisconnect is set, every
    // connection disconnects after disconnectAfterBytes; otherwise only the first connection does.
    int disconnectAfterBytes = -1;
    bool alwaysDisconnect = false;

    // When stallAfterBytes >= 0, read() blocks once it has served that many bytes on a connection,
    // until releaseStall() is called. Used to force a read-ahead underrun.
    int stallAfterBytes = -1;

    bool blockForever = false; // read() blocks until cancel() (cancellation test)

    // Progressive-origin fault: the next N start() calls at a NON-ZERO offset are refused, the way a
    // torrent still downloading refuses a range it does not hold yet. Nothing is broken - the bytes
    // simply are not there yet - so the source must treat it as buffering, not as a dead stream.
    int refuseRangedStarts = 0;

    // Progressive-origin SILENCE - what the Stremio EngineFS sidecar measurably does (2026-07-25).
    // It does not refuse a range it does not hold yet: it accepts the connection and answers
    // NOTHING, because it stages the 206 and only emits it with the first body byte. So start()
    // BLOCKS, and the only thing that ends the wait is the bytes arriving (releaseRangedSilence())
    // or the stall timeout the SOURCE handed us running out. Modelling this as a free, instant
    // refusal is exactly why the earlier bounded-retry fix passed its tests and still killed the
    // real session: in the fake a retry cost nothing, in the field it cost 20 seconds.
    bool silenceRangedStarts = false;

    // The DOWNLOAD FRONTIER during ordinary playback, which is the same origin behaviour one step
    // later: after this many bytes on a connection the body simply stops. The origin does not close
    // anything - it holds the connection open and says nothing - so read() burns the SOURCE's stall
    // timeout and only then reports a timeout. A fake that returned -1 for free is exactly why the
    // mid-stream path looked bounded and measured 233 s in the field.
    int silentAfterBytes = -1;

    void releaseRangedSilence()
    {
        {
            std::scoped_lock lock(m_mutex);
            m_silenceReleased = true;
        }
        m_cv.notify_all();
    }

    int startCount() const { return m_startCount.load(); }

    void releaseStall()
    {
        {
            std::scoped_lock lock(m_mutex);
            m_stallReleased = true;
        }
        m_cv.notify_all();
    }

    bool start(const QUrl &, const RequestHeaders &, qint64 byteOffset, HttpResponse *response,
               QString *error) override
    {
        std::unique_lock lock(m_mutex);
        // A new connection rearms the transport: a real client is reusable after a per-operation
        // cancel(); only HttpMediaSource's own flag is the permanent shutdown.
        m_cancelled = false;
        ++m_startCount;
        if (byteOffset > 0 && refuseRangedStarts > 0) {
            --refuseRangedStarts;
            if (error)
                *error = QStringLiteral("range not available yet");
            return false;
        }
        if (byteOffset > 0 && silenceRangedStarts && !m_silenceReleased) {
            const bool arrived =
                m_cv.wait_for(lock, std::chrono::milliseconds(m_stallTimeoutMs),
                              [this] { return m_silenceReleased || m_cancelled; });
            if (m_cancelled) {
                if (error)
                    *error = QStringLiteral("cancelled");
                return false;
            }
            if (!arrived) {
                if (error)
                    *error = QStringLiteral("timed out reading headers");
                return false;
            }
        }
        m_served = 0;
        m_connectionHadDisconnect = false;
        const qint64 effectiveOffset = honorRange ? byteOffset : 0;
        m_serveOffset = effectiveOffset;
        response->statusCode = (honorRange && byteOffset > 0) ? 206 : 200;
        response->partial = honorRange && byteOffset > 0;
        response->acceptRanges = acceptRanges;
        response->rangeStart = effectiveOffset;
        response->totalBytes = totalKnown ? static_cast<qint64>(resource.size()) : -1;
        return true;
    }

    int read(char *dst, int maxLen) override
    {
        std::unique_lock lock(m_mutex);
        if (blockForever) {
            m_cv.wait(lock, [this] { return m_cancelled; });
            return -1; // interrupted
        }
        if (stallAfterBytes >= 0 && m_served >= stallAfterBytes && !m_stallReleased) {
            m_cv.wait(lock, [this] { return m_stallReleased || m_cancelled; });
        }
        if (silentAfterBytes >= 0 && m_served >= silentAfterBytes) {
            m_cv.wait_for(lock, std::chrono::milliseconds(m_stallTimeoutMs),
                          [this] { return m_cancelled; });
            return -1; // the socket's patience ran out, exactly as a real silent origin does
        }
        if (m_cancelled)
            return -1;
        const bool disconnectThisConnection = alwaysDisconnect || m_startCount == 1;
        if (disconnectAfterBytes >= 0 && disconnectThisConnection && !m_connectionHadDisconnect &&
            m_served >= disconnectAfterBytes) {
            m_connectionHadDisconnect = true;
            return -1; // mid-stream failure the source should try to recover
        }
        if (m_serveOffset >= resource.size())
            return 0; // clean EOF
        const int available = static_cast<int>(resource.size() - m_serveOffset);
        const int count = std::min({maxLen, chunkSize, available});
        std::memcpy(dst, resource.constData() + m_serveOffset, static_cast<size_t>(count));
        m_serveOffset += count;
        m_served += count;
        return count;
    }

    void close() override {}

    void cancel() override
    {
        {
            std::scoped_lock lock(m_mutex);
            m_cancelled = true;
        }
        m_cv.notify_all();
    }

    // The source owns the patience; record it so a silent ranged start times out exactly the way a
    // real socket would.
    void setStallTimeoutMs(int milliseconds) override
    {
        std::scoped_lock lock(m_mutex);
        m_stallTimeoutMs = milliseconds;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<int> m_startCount{0};
    qint64 m_serveOffset = 0;
    int m_served = 0;
    bool m_connectionHadDisconnect = false;
    bool m_stallReleased = false;
    bool m_silenceReleased = false;
    bool m_cancelled = false;
    int m_stallTimeoutMs = 20'000;
};

HttpSourcePolicy fastPolicy()
{
    HttpSourcePolicy policy;
    policy.highWaterBytes = 256 * 1024;
    policy.lowWaterBytes = 4 * 1024;
    policy.resumeBytes = 1; // any byte resumes; keeps the buffering test crisp
    policy.maxReconnectAttempts = 3;
    policy.reconnectDelayMs = [](int) { return 0; }; // no real delay in tests
    return policy;
}

PlaybackRequest streamRequest(bool live = false)
{
    PlaybackRequest request;
    request.source = QUrl(QStringLiteral("http://localhost/media.bin"));
    request.stream = true;
    request.live = live;
    return request;
}

// Drain the source on a background thread (the demux/AVIO role) into `out`, until EOF or error.
QByteArray drainAll(HttpMediaSource &source, int *terminalCode = nullptr)
{
    QByteArray out;
    char buffer[32 * 1024];
    for (;;) {
        const int n = source.read(buffer, sizeof(buffer));
        if (n > 0) {
            out.append(buffer, n);
            continue;
        }
        if (terminalCode)
            *terminalCode = n;
        break; // 0 = EOF, <0 = error/cancel
    }
    return out;
}

// -----------------------------------------------------------------------------------------------

void testRangeSuccessAndSeek()
{
    const QByteArray body = countedBody(400 * 1024);
    auto transport = std::make_unique<ScriptedTransport>();
    transport->resource = body;
    ScriptedTransport *raw = transport.get();
    (void)raw;

    HttpMediaSource source(std::move(transport), streamRequest(), fastPolicy());
    QString error;
    require(source.open(&error), "range-success open failed: " + error.toStdString());

    const SourceCapabilities caps = source.capabilities();
    require(caps.rangeSupported, "range server should report rangeSupported");
    require(caps.seekable, "range server should be seekable");
    require(caps.knownDuration, "known length should report knownDuration");
    require(!caps.live, "a seekable known-length source is not live");
    require(caps.totalBytes == body.size(), "totalBytes should equal the resource size");

    const QByteArray whole = drainAll(source);
    require(whole == body, "sequential read did not return the exact body");

    // Seek to the middle and read the tail; the bytes must match the resource from that offset.
    const qint64 target = 128 * 1024;
    const qint64 landed = source.seek(target, SEEK_SET);
    require(landed == target, "seek did not land at the requested offset");
    const QByteArray tail = drainAll(source);
    require(tail == body.mid(static_cast<int>(target)), "post-seek read did not match the tail");

    // AVSEEK_SIZE returns the known total.
    require(source.seek(0, 0x10000 /*AVSEEK_SIZE*/) == body.size(), "AVSEEK_SIZE should give size");
}

void testRangeRejectionMakesUnseekable()
{
    const QByteArray body = countedBody(200 * 1024);
    auto transport = std::make_unique<ScriptedTransport>();
    transport->resource = body;
    transport->acceptRanges = false;
    transport->honorRange = false;
    transport->totalKnown = true;

    HttpMediaSource source(std::move(transport), streamRequest(), fastPolicy());
    QString error;
    require(source.open(&error), "no-range open failed: " + error.toStdString());

    const SourceCapabilities caps = source.capabilities();
    require(!caps.rangeSupported, "server without ranges must report rangeSupported=false");
    require(!caps.seekable, "server without ranges must be unseekable");

    const QByteArray whole = drainAll(source);
    require(whole == body, "no-range read did not return the exact body");

    require(source.seek(1024, SEEK_SET) < 0, "seek on an unseekable source must fail");
}

void testSlowChunksReportBuffering()
{
    const QByteArray body = countedBody(300 * 1024);
    auto transport = std::make_unique<ScriptedTransport>();
    transport->resource = body;
    transport->chunkSize = 64 * 1024;
    transport->stallAfterBytes = 64 * 1024; // serve one chunk, then stall until released
    ScriptedTransport *raw = transport.get();

    StateLog log;
    HttpMediaSource source(std::move(transport), streamRequest(), fastPolicy());
    source.setStateCallback([&log](NetworkState state) { log.record(state); });
    QString error;
    require(source.open(&error), "slow-chunk open failed: " + error.toStdString());

    std::atomic<bool> done{false};
    QByteArray out;
    std::thread consumer([&] {
        out = drainAll(source);
        done = true;
    });

    // Give the consumer time to drain the first chunk and starve on the stall.
    for (int i = 0; i < 200 && !log.saw(NetworkState::Buffering); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    require(log.saw(NetworkState::Buffering), "an underrun must report Buffering");

    raw->releaseStall();
    consumer.join();

    require(out == body, "slow-chunk read did not return the exact body");
    require(log.sawInOrder(NetworkState::Buffering, NetworkState::Streaming),
            "the source must recover from Buffering back to Streaming");
    require(log.saw(NetworkState::Ended), "a fully drained body must report Ended");
}

void testDisconnectThenReconnect()
{
    const QByteArray body = countedBody(300 * 1024);
    auto transport = std::make_unique<ScriptedTransport>();
    transport->resource = body;
    transport->chunkSize = 64 * 1024;
    transport->disconnectAfterBytes = 128 * 1024; // first connection dies after 128 KiB
    transport->alwaysDisconnect = false;          // reconnection succeeds

    StateLog log;
    HttpMediaSource source(std::move(transport), streamRequest(), fastPolicy());
    source.setStateCallback([&log](NetworkState state) { log.record(state); });
    QString error;
    require(source.open(&error), "reconnect open failed: " + error.toStdString());

    const QByteArray whole = drainAll(source);
    require(whole == body, "reconnected read must reassemble the exact body");
    require(log.saw(NetworkState::Recovering), "a mid-stream disconnect must report Recovering");
    require(source.reconnectCount() == 1, "exactly one reconnect should have occurred");
    require(log.sawInOrder(NetworkState::Recovering, NetworkState::Streaming),
            "the source must return to Streaming after recovery");
}

void testReconnectExhaustionFails()
{
    const QByteArray body = countedBody(300 * 1024);
    auto transport = std::make_unique<ScriptedTransport>();
    transport->resource = body;
    transport->chunkSize = 64 * 1024;
    transport->disconnectAfterBytes = 64 * 1024;
    transport->alwaysDisconnect = true; // every connection dies -> attempts exhaust

    StateLog log;
    HttpSourcePolicy policy = fastPolicy();
    policy.maxReconnectAttempts = 2;
    HttpMediaSource source(std::move(transport), streamRequest(), policy);
    source.setStateCallback([&log](NetworkState state) { log.record(state); });
    QString error;
    require(source.open(&error), "exhaustion open failed: " + error.toStdString());

    int terminal = 0;
    drainAll(source, &terminal);
    require(terminal < 0, "an exhausted reconnect must return a terminal error to the demuxer");
    require(log.saw(NetworkState::Failed), "exhausted reconnect must report Failed");
    require(source.reconnectCount() == 2, "reconnect attempts should be bounded to the policy");
}

// Seeking into bytes a progressive origin does not hold YET is buffering, not a broken stream. The
// source must retry the same target with backoff and recover. This is the defect Hemanth hit on the
// first real Player 2 playback: "if i seek forward or backward the video player closes" - the seek
// re-open was given exactly one attempt and went terminal on the first refusal, while a mid-stream
// disconnect had always had bounded retries.
void testSeekIntoNotYetAvailableBytesBuffersThenRecovers()
{
    const QByteArray body = countedBody(300 * 1024);
    auto transport = std::make_unique<ScriptedTransport>();
    transport->resource = body;
    transport->chunkSize = 64 * 1024;
    transport->refuseRangedStarts = 3; // the first three attempts at the seek target are refused

    StateLog log;
    HttpMediaSource source(std::move(transport), streamRequest(), fastPolicy());
    source.setStateCallback([&log](NetworkState state) { log.record(state); });
    QString error;
    require(source.open(&error), "progressive-seek open failed: " + error.toStdString());

    require(source.seek(128 * 1024, SEEK_SET) == 128 * 1024, "seek to a known offset must report it");

    QByteArray tail;
    tail.resize(64 * 1024);
    const int n = source.read(tail.data(), tail.size());
    require(n > 0, "a seek into not-yet-available bytes must eventually deliver data, not fail");
    require(!log.saw(NetworkState::Failed),
            "a temporarily unavailable range must NOT be terminal - that closes the player");
    require(tail.left(n) == body.mid(128 * 1024, n), "recovered bytes must come from the seek target");
}

// The tolerance is bounded, not infinite: an origin that never serves the target still fails cleanly
// rather than buffering forever.
void testSeekRefusalsEventuallyFail()
{
    const QByteArray body = countedBody(300 * 1024);
    auto transport = std::make_unique<ScriptedTransport>();
    transport->resource = body;
    transport->chunkSize = 64 * 1024;
    transport->refuseRangedStarts = 1000; // never serves the seek target

    StateLog log;
    HttpSourcePolicy policy = fastPolicy();
    policy.maxSeekOpenAttempts = 3;
    HttpMediaSource source(std::move(transport), streamRequest(), policy);
    source.setStateCallback([&log](NetworkState state) { log.record(state); });
    QString error;
    require(source.open(&error), "bounded-seek open failed: " + error.toStdString());

    source.seek(128 * 1024, SEEK_SET);
    int terminal = 0;
    drainAll(source, &terminal);
    require(terminal < 0, "an unservable seek target must end in a terminal error");
    require(log.saw(NetworkState::Failed), "exhausted seek attempts must report Failed");
}

// The regression Hemanth actually hit: "if i seek forward or backward the video player closes".
// Measured against the real sidecar 2026-07-25 - a seek past the download frontier produced
//   errorText=[Network stream failed (NetworkFailed)] finalState=8
// after 196 s frozen in Seeking and exactly 8 re-open attempts at the same byte offset. The origin
// never refused any of them; it accepted every connection and stayed silent because the bytes had
// not downloaded yet, and each 20 s socket timeout was counted as an attempt against an 8-attempt
// budget. The bound has to be wall clock, and it has to be spent WAITING, not re-dialling.
void testSeekIntoSilentProgressiveOriginWaitsThenLands()
{
    const QByteArray body = countedBody(300 * 1024);
    auto transport = std::make_unique<ScriptedTransport>();
    transport->resource = body;
    transport->chunkSize = 64 * 1024;
    transport->silenceRangedStarts = true; // accepts the connection, then says nothing
    ScriptedTransport *scripted = transport.get();

    StateLog log;
    HttpSourcePolicy policy = fastPolicy();
    policy.stallTimeoutMs = 50;       // a short socket-level patience, as in the field
    policy.maxSeekOpenAttempts = 8;   // 8 x 50 ms = 400 ms of attempts...
    policy.stallBudgetMs = 5'000; // ...but 5 s of real budget, which is what must govern
    HttpMediaSource source(std::move(transport), streamRequest(), policy);
    source.setStateCallback([&log](NetworkState state) { log.record(state); });
    QString error;
    require(source.open(&error), "silent-origin open failed: " + error.toStdString());

    // The pieces land well after the attempt budget would have run out, but well inside the wall
    // clock budget - the exact gap the field failure fell into.
    std::thread arrival([scripted] {
        std::this_thread::sleep_for(std::chrono::milliseconds(1'500));
        scripted->releaseRangedSilence();
    });

    require(source.seek(128 * 1024, SEEK_SET) == 128 * 1024, "seek must report its target");
    QByteArray tail;
    tail.resize(64 * 1024);
    const int n = source.read(tail.data(), tail.size());
    arrival.join();

    require(n > 0, "a seek into bytes that arrive late must deliver them, not kill the session");
    require(!log.saw(NetworkState::Failed),
            "an origin that is still fetching has not failed - that is what closed the player");
    require(log.saw(NetworkState::Buffering),
            "the wait must be published as Buffering, not left as silent dead air");
    require(tail.left(n) == body.mid(128 * 1024, n), "delivered bytes must come from the seek target");
}

// The patience stays bounded: an origin that goes silent forever still fails cleanly and promptly
// once the wall-clock budget is spent - it must never buffer forever.
void testSilentSeekStillFailsWhenTheBudgetIsSpent()
{
    const QByteArray body = countedBody(300 * 1024);
    auto transport = std::make_unique<ScriptedTransport>();
    transport->resource = body;
    transport->chunkSize = 64 * 1024;
    transport->silenceRangedStarts = true; // never releases: the bytes never arrive

    StateLog log;
    HttpSourcePolicy policy = fastPolicy();
    policy.stallTimeoutMs = 50;
    policy.maxSeekOpenAttempts = 8;
    policy.stallBudgetMs = 400;
    HttpMediaSource source(std::move(transport), streamRequest(), policy);
    source.setStateCallback([&log](NetworkState state) { log.record(state); });
    QString error;
    require(source.open(&error), "bounded silent-seek open failed: " + error.toStdString());

    const auto started = std::chrono::steady_clock::now();
    source.seek(128 * 1024, SEEK_SET);
    int terminal = 0;
    drainAll(source, &terminal);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    require(terminal < 0, "a target that never arrives must end in a terminal error");
    require(log.saw(NetworkState::Failed), "a spent seek budget must report Failed");
    require(elapsed < std::chrono::seconds(5),
            "the seek budget must bound the wait - buffering forever is not an option");
}

// The bound that was NOT holding. Measured 2026-07-25 against the window fixture with the bytes set
// never to arrive: the seek never even reached the source, because ordinary playback had already
// walked into the download frontier and the DEMUX thread was parked inside read(). The fixture's own
// log named the path — five accepted connections, every one at the frontier byte
//   player2_http_fixture_server: window start=12582912 served=0 of 49106150 then STALLED (held open)
// and never one at the seek target — and the arithmetic matched to the second: 20 s (the first
// silent read) + 5 x (backoff + 20 s silent open + 20 s silent read) = 235 s against a 90 s promise.
// The attempt count was the ONLY bound on that path, and an attempt cost two independent socket
// timeouts. The ceiling has to be wall clock, and the transport's patience inside an episode has to
// be what remains of it.
void testMidStreamStallAtTheFrontierIsBoundedByWallClock()
{
    const QByteArray body = countedBody(300 * 1024);
    auto transport = std::make_unique<ScriptedTransport>();
    transport->resource = body;
    transport->chunkSize = 64 * 1024;
    transport->silentAfterBytes = 64 * 1024; // playback reaches the frontier after one chunk
    transport->silenceRangedStarts = true;   // and every reconnect there is met with silence too

    StateLog log;
    HttpSourcePolicy policy = fastPolicy();
    policy.stallTimeoutMs = 300;       // the socket-level patience
    policy.maxReconnectAttempts = 5;   // 5 attempts x (300 ms open + 300 ms read) = 3 s of attempts
    policy.stallBudgetMs = 500;        // ...against a 500 ms ceiling, which is what must govern
    HttpMediaSource source(std::move(transport), streamRequest(), policy);
    source.setStateCallback([&log](NetworkState state) { log.record(state); });
    QString error;
    require(source.open(&error), "frontier-stall open failed: " + error.toStdString());

    const auto started = std::chrono::steady_clock::now();
    int terminal = 0;
    drainAll(source, &terminal);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    require(terminal < 0, "a frontier that never opens must end in a terminal error");
    require(log.saw(NetworkState::Failed), "a spent stall budget must report Failed");
    // The first silent read (300 ms, outside the budget) plus the 500 ms budget is ~800 ms. The old
    // attempt-only bound was ~3.3 s here, which is what this margin catches.
    require(elapsed < std::chrono::milliseconds(2'000),
            "the mid-stream stall must be bounded by the wall clock, not attempts x socket timeout");
    require(!source.terminalError().isEmpty(),
            "a terminal network stall must stamp its own cause, or the layer above reports a "
            "decode failure for a network problem");
}

// The reconnect COUNT is a budget for a flapping origin, not a session lifetime allowance. A stream
// that drops, recovers and streams cleanly for a whole read-ahead has demonstrably recovered, so the
// next incident must get the full budget instead of inheriting a spent one. Without this, the new
// wall-clock ceiling would still strand a real torrent, which walks into a fresh frontier repeatedly.
void testReconnectBudgetResetsAfterDemonstratedRecovery()
{
    const QByteArray body = countedBody(2 * 1024 * 1024);
    auto transport = std::make_unique<ScriptedTransport>();
    transport->resource = body;
    transport->chunkSize = 64 * 1024;
    transport->disconnectAfterBytes = 512 * 1024; // each connection streams 512 KiB, then drops
    transport->alwaysDisconnect = true;           // four incidents are needed to reach the end

    StateLog log;
    HttpSourcePolicy policy = fastPolicy(); // highWaterBytes = 256 KiB: the recovery threshold
    policy.maxReconnectAttempts = 2;        // fewer than the four incidents the body needs
    HttpMediaSource source(std::move(transport), streamRequest(), policy);
    source.setStateCallback([&log](NetworkState state) { log.record(state); });
    QString error;
    require(source.open(&error), "recovery-reset open failed: " + error.toStdString());

    int terminal = 1;
    const QByteArray whole = drainAll(source, &terminal);
    require(!log.saw(NetworkState::Failed),
            "a stream that recovers fully between drops must not exhaust a lifetime budget");
    require(terminal == 0, "the body must reach a clean EOF, not a terminal error");
    require(whole == body, "a recovered stream must reassemble the exact body");
}

void testUnknownLengthIsLive()
{
    const QByteArray body = countedBody(100 * 1024);
    auto transport = std::make_unique<ScriptedTransport>();
    transport->resource = body;
    transport->totalKnown = false;   // no Content-Length
    transport->acceptRanges = false; // and no ranges
    transport->honorRange = false;

    HttpMediaSource source(std::move(transport), streamRequest(), fastPolicy());
    QString error;
    require(source.open(&error), "unknown-length open failed: " + error.toStdString());

    const SourceCapabilities caps = source.capabilities();
    require(!caps.knownDuration, "no Content-Length must report knownDuration=false");
    require(!caps.seekable, "an unknown-length no-range source is unseekable");
    require(caps.live, "an unknown-length unseekable source is live");
    require(source.seek(0, 0x10000 /*AVSEEK_SIZE*/) < 0, "AVSEEK_SIZE on unknown size must fail");

    const QByteArray whole = drainAll(source);
    require(whole == body, "unknown-length read did not return the exact body");
}

void testSeekToEndParksWithoutOutOfRangeRequest()
{
    // Regression: seeking to/past EOF (FFmpeg probes mkv this way) must not issue a Range that a
    // real server answers with 416; the source parks at EOF and read() returns a clean 0.
    const QByteArray body = countedBody(200 * 1024);
    auto transport = std::make_unique<ScriptedTransport>();
    transport->resource = body;
    ScriptedTransport *raw = transport.get();

    HttpMediaSource source(std::move(transport), streamRequest(), fastPolicy());
    QString error;
    require(source.open(&error), "seek-end open failed: " + error.toStdString());

    const int startsBefore = raw->startCount();
    require(source.seek(body.size(), SEEK_SET) == body.size(), "seek to EOF should land at size");
    int terminal = 1;
    const QByteArray afterEof = drainAll(source, &terminal);
    require(afterEof.isEmpty(), "reading at EOF must return nothing");
    require(terminal == 0, "seek to EOF must report a clean EOF, not an error");
    require(source.state() != NetworkState::Failed, "seek to EOF must not fail the source");
    require(raw->startCount() == startsBefore, "seek to EOF must not open a new connection");

    // A normal seek back into range still works afterwards.
    require(source.seek(1024, SEEK_SET) == 1024, "seek back into range should succeed");
    require(drainAll(source) == body.mid(1024), "post-EOF seek read must match the tail");
}

void testReconnectFromWrongOffsetFails()
{
    // Regression: if a reconnect does not resume from the requested byte (server ignored the range),
    // resuming would splice misaligned bytes; the source must fail cleanly instead.
    const QByteArray body = countedBody(300 * 1024);
    auto transport = std::make_unique<ScriptedTransport>();
    transport->resource = body;
    transport->chunkSize = 64 * 1024;
    transport->acceptRanges = false;
    transport->honorRange = false;             // reconnect will serve from byte 0, not the resume point
    transport->disconnectAfterBytes = 64 * 1024;
    transport->alwaysDisconnect = false;

    StateLog log;
    HttpMediaSource source(std::move(transport), streamRequest(), fastPolicy());
    source.setStateCallback([&log](NetworkState state) { log.record(state); });
    QString error;
    require(source.open(&error), "wrong-offset open failed: " + error.toStdString());

    int terminal = 0;
    drainAll(source, &terminal);
    require(terminal < 0, "a misaligned reconnect must return a terminal error");
    require(log.saw(NetworkState::Failed), "a misaligned reconnect must report Failed");
    require(source.reconnectCount() == 1, "the source should stop after the misaligned reconnect");
}

void testCancelDuringActiveReadNeverHangs()
{
    // Regression for the lost-wakeup race: cancel arriving anywhere around read()'s underrun wait
    // (not just after a guaranteed 50 ms block) must always return promptly. A regression hangs the
    // consumer join, which the harness timeout catches.
    for (int iteration = 0; iteration < 200; ++iteration) {
        auto transport = std::make_unique<ScriptedTransport>();
        transport->resource = countedBody(256 * 1024);
        transport->blockForever = true; // never delivers, so read() must block then wake on cancel
        HttpMediaSource source(std::move(transport), streamRequest(), fastPolicy());
        QString error;
        require(source.open(&error), "stress open failed: " + error.toStdString());

        std::atomic<int> terminal{1};
        std::thread consumer([&] {
            char buffer[4096];
            terminal = source.read(buffer, sizeof(buffer));
        });
        source.cancel(); // no sleep: race the reader's path into wait()
        consumer.join();
        require(terminal < 0, "a cancelled read must return a negative terminal code");
    }
}

void testCancellationIsPrompt()
{
    auto transport = std::make_unique<ScriptedTransport>();
    transport->resource = countedBody(1024 * 1024);
    transport->blockForever = true; // the transport never delivers

    HttpMediaSource source(std::move(transport), streamRequest(), fastPolicy());
    QString error;
    require(source.open(&error), "cancellation open failed: " + error.toStdString());

    std::atomic<int> terminal{123};
    std::atomic<bool> finished{false};
    const auto begin = std::chrono::steady_clock::now();
    std::thread consumer([&] {
        char buffer[4096];
        terminal = source.read(buffer, sizeof(buffer)); // blocks: no data will ever arrive
        finished = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    source.cancel();
    consumer.join();
    const auto elapsed = std::chrono::steady_clock::now() - begin;

    require(finished, "read did not return after cancel");
    require(terminal < 0, "a cancelled read must return a negative terminal code");
    require(elapsed < std::chrono::seconds(2), "cancellation must return within two seconds");
}

void testParkedReadWakesForAPendingCommand()
{
    // The T2d defect, at the layer that owns it. When the origin goes silent at the download
    // frontier the demux thread parks HERE, inside read()'s underrun wait, and nothing in that wait
    // knew that the viewer had asked for something. So a seek sat queued to a command loop nobody
    // was running until the source went terminal ~110 s later: press seek, nothing happens, it dies.
    // A parked read must be abortable by a pending command WITHOUT being terminal - the source is
    // healthy, it is just holding nothing right now.
    const QByteArray body = countedBody(300 * 1024);
    auto transport = std::make_unique<ScriptedTransport>();
    transport->resource = body;
    transport->chunkSize = 64 * 1024;
    transport->stallAfterBytes = 64 * 1024; // serve one chunk, then go silent: the frontier
    ScriptedTransport *raw = transport.get();

    std::atomic<bool> commandPending{false};
    HttpMediaSource source(std::move(transport), streamRequest(), fastPolicy());
    source.setInterruptPredicate([&commandPending] { return commandPending.load(); });
    QString error;
    require(source.open(&error), "frontier open failed: " + error.toStdString());

    // Drain everything the origin served, so the next read has nothing to give and must park.
    char buffer[16 * 1024];
    int drained = 0;
    while (drained < 64 * 1024) {
        const int n = source.read(buffer, sizeof(buffer));
        require(n > 0, "the served prefix should read without blocking");
        drained += n;
    }

    // With no command pending the parked read stays parked (this is the wait that paces playback).
    std::atomic<int> code{123};
    std::atomic<bool> returned{false};
    const auto begin = std::chrono::steady_clock::now();
    std::thread demux([&] {
        char parkedBuffer[16 * 1024];
        code = source.read(parkedBuffer, sizeof(parkedBuffer));
        returned = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    require(!returned, "a parked read must not return while no command is pending");

    // A wake with NOTHING pending must not abandon the park either — the predicate is the truth,
    // the notify is only a prompt to re-read it.
    source.wakeRead();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    require(!returned, "a wake with no command pending must leave the read parked");

    // The viewer presses seek: the owner marks a command pending and wakes the read.
    commandPending = true;
    source.wakeRead();
    demux.join();
    const auto elapsed = std::chrono::steady_clock::now() - begin;

    require(code < 0, "an interrupted read must return a negative code");
    // Sub-second is the requirement (cross-model review 2026-07-26): the whole point is that the
    // press is answered now, not eventually. Two seconds was slack enough to pass a broken fix.
    require(elapsed < std::chrono::milliseconds(1000),
            "a pending command must abort the park in under a second");
    require(source.consumeReadInterrupt(),
            "the source must report that the read ended on a command, not on a failure");
    require(!source.consumeReadInterrupt(), "the interrupt report is one-shot");
    require(source.state() != NetworkState::Failed,
            "an interrupted read is NOT terminal - the source must stay usable");
    require(source.terminalError().isEmpty(), "an interrupted read must not stamp a failure cause");

    // And the source still works: the owner serviced its command, cleared the flag, and reads on.
    commandPending = false;
    raw->releaseStall();
    const QByteArray rest = drainAll(source);
    require(rest == body.mid(64 * 1024),
            "the source must resume delivering the body after a command interrupt");
}

} // namespace

int main()
{
    try {
        testRangeSuccessAndSeek();
        testRangeRejectionMakesUnseekable();
        testSlowChunksReportBuffering();
        testDisconnectThenReconnect();
        testReconnectExhaustionFails();
        testSeekIntoNotYetAvailableBytesBuffersThenRecovers();
        testSeekRefusalsEventuallyFail();
        testSeekIntoSilentProgressiveOriginWaitsThenLands();
        testSilentSeekStillFailsWhenTheBudgetIsSpent();
        testReconnectBudgetResetsAfterDemonstratedRecovery();
        testMidStreamStallAtTheFrontierIsBoundedByWallClock();
        testUnknownLengthIsLive();
        testSeekToEndParksWithoutOutOfRangeRequest();
        testReconnectFromWrongOffsetFails();
        testCancelDuringActiveReadNeverHangs();
        testCancellationIsPrompt();
        testParkedReadWakesForAPendingCommand();
    } catch (const std::exception &error) {
        std::cerr << "player2_http_media_test: FAIL " << error.what() << '\n';
        return 1;
    }
    std::cout << "player2_http_media_test: PASS\n";
    return 0;
}
