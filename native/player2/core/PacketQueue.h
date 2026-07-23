#pragma once

#include <QtCore/QtTypes>

#include <condition_variable>
#include <deque>
#include <mutex>

struct AVPacket;

namespace Colosseum::Player2 {

// A bounded, thread-safe FIFO of demuxed packets for ONE stream — the seam that lets demuxing run
// ahead of playback. The demux thread pushes packets as fast as it can read them; a decode thread
// pops them at its own pace. Bounds keep the read-ahead finite: push() blocks while the buffered
// span is full (back-pressure on the demuxer), pop() blocks while empty (the decoder waits for
// supply). A seek flushes buffered packets; cancel() permanently unblocks every waiter so the
// worker threads can join. This is the primitive the audio read-ahead engine is built on: the
// audio decode thread pops far enough ahead of the clock to keep loudnorm's ~3s lookahead fed.
class PacketQueue
{
public:
    // Read-ahead ceiling. A zero field means "do not bound on this axis"; at least one axis should
    // be set or the queue grows without limit. maxBufferedUs is the pts span of buffered packets —
    // the knob that decides how far ahead of playback the demuxer is allowed to run.
    struct Bounds
    {
        qint64 maxBufferedUs = 0; // buffered pts span cap (the read-ahead / loudnorm-lookahead knob)
        qint64 maxBytes = 0;      // total packet payload bytes cap
        int maxPackets = 0;       // packet count cap
        // When true, a full push does NOT block: it drops the OLDEST buffered backlog (whole leading
        // GOPs, so the new front is a keyframe) to make room, then admits. This is the video queue's
        // policy — the demux thread decodes audio inline, so it must never block on a full video
        // queue or audio starves and the whole pipeline deadlocks. A slow video skips ahead instead.
        bool dropOldestWhenFull = false;
    };

    enum class PopResult
    {
        Packet,      // `out` holds a packet and `generation` its tag
        EndOfStream, // the buffer is drained and the stream has ended
        Cancelled    // the queue was cancelled; the calling thread should exit
    };

    explicit PacketQueue(Bounds bounds);
    ~PacketQueue();

    PacketQueue(const PacketQueue &) = delete;
    PacketQueue &operator=(const PacketQueue &) = delete;

    // Take ownership of `packet` (moved via av_packet_move_ref) tagged with `generation`, `ptsUs`
    // and whether it is a `keyframe`. Blocks while the queue is full (unless the queue is empty — a
    // single oversized packet is always accepted so the pipeline can never deadlock), EXCEPT when
    // Bounds::dropOldestWhenFull is set, in which case it drops the oldest backlog and never blocks.
    // Returns false only if the queue is cancelled while blocked, leaving `packet` for the caller.
    bool push(AVPacket *packet, qint64 ptsUs, quint64 generation, bool keyframe = false);

    // Move the front packet into `out` (the caller unrefs) and report its generation. If
    // `discontinuity` is non-null, it is set true when a drop-oldest admission discarded backlog
    // before this packet — the consumer must flush its decoder before decoding it. Blocks while the
    // queue is empty until a push, setEndOfStream(), flush(), or cancel().
    PopResult pop(AVPacket *out, quint64 *generation, bool *discontinuity = nullptr);

    // Mark that no more packets will arrive. Once the buffer drains, pop() returns EndOfStream.
    // A blocked consumer is woken so it observes the end promptly.
    void setEndOfStream();

    // Drop every buffered packet and clear the end-of-stream flag (a seek discards stale supply).
    // Wakes a blocked producer (space freed); a blocked consumer re-checks and waits fresh.
    void flush();

    // Permanent shutdown: unblock all current and future waiters so worker threads exit.
    void cancel();

    int bufferedPackets() const;
    qint64 bufferedUs() const;
    qint64 bufferedBytes() const;

private:
    struct Entry
    {
        AVPacket *packet = nullptr;
        qint64 ptsUs = 0;
        quint64 generation = 0;
        bool keyframe = false;
    };

    bool isFullLocked() const;
    qint64 bufferedUsLocked() const;

    const Bounds m_bounds;
    mutable std::mutex m_mutex;
    std::condition_variable m_notEmpty;
    std::condition_variable m_notFull;
    std::deque<Entry> m_entries;
    qint64 m_bufferedBytes = 0;
    bool m_endOfStream = false;
    bool m_cancelled = false;
    bool m_discontinuityPending = false; // a drop-oldest admission discarded backlog; next pop flags it
};

} // namespace Colosseum::Player2
