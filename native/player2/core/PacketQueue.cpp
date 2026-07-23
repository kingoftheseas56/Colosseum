#include "PacketQueue.h"

#include <chrono>

extern "C" {
#include <libavcodec/packet.h>
}

namespace Colosseum::Player2 {

PacketQueue::PacketQueue(Bounds bounds) : m_bounds(bounds) {}

PacketQueue::~PacketQueue()
{
    for (Entry &entry : m_entries)
        av_packet_free(&entry.packet);
}

qint64 PacketQueue::bufferedUsLocked() const
{
    if (m_entries.empty())
        return 0;
    const qint64 span = m_entries.back().ptsUs - m_entries.front().ptsUs;
    return span > 0 ? span : 0;
}

bool PacketQueue::isFullLocked() const
{
    // An empty queue is never full: a single oversized packet must always be accepted, else the
    // pipeline could deadlock (producer blocked on a bound no single packet can satisfy).
    if (m_entries.empty())
        return false;
    if (m_bounds.maxPackets > 0 && static_cast<int>(m_entries.size()) >= m_bounds.maxPackets)
        return true;
    if (m_bounds.maxBytes > 0 && m_bufferedBytes >= m_bounds.maxBytes)
        return true;
    if (m_bounds.maxBufferedUs > 0 && bufferedUsLocked() >= m_bounds.maxBufferedUs)
        return true;
    return false;
}

void PacketQueue::dropOldestBacklogLocked()
{
    // Remove whole leading GOPs: stop only once there is room AND the new front is a clean decode
    // restart point. The consumer observes one discontinuity before decoding that new front.
    while (!m_entries.empty() && (isFullLocked() || !m_entries.front().keyframe)) {
        Entry &front = m_entries.front();
        m_bufferedBytes -= front.packet->size;
        av_packet_free(&front.packet);
        m_entries.pop_front();
        m_discontinuityPending = true;
    }
}

bool PacketQueue::push(AVPacket *packet, qint64 ptsUs, quint64 generation, bool keyframe)
{
    std::unique_lock lock(m_mutex);
    if (m_bounds.dropOldestWhenFull) {
        if (m_cancelled)
            return false;
        // Explicit legacy recovery primitive: its caller has already established distress, so drop
        // whole leading GOPs rather than block. Normal worker-fed video uses pushInterruptible()
        // with a per-admission clock/lateness decision instead.
        if (isFullLocked())
            dropOldestBacklogLocked();
    } else {
        // Back-pressure: block while the buffer is full so the demuxer cannot run unbounded ahead.
        m_notFull.wait(lock, [this] { return m_cancelled || !isFullLocked(); });
        if (m_cancelled)
            return false;
    }
    AVPacket *owned = av_packet_alloc();
    av_packet_move_ref(owned, packet);
    m_bufferedBytes += owned->size;
    m_entries.push_back(Entry{owned, ptsUs, generation, keyframe});
    m_notEmpty.notify_one();
    return true;
}

PacketQueue::Admit PacketQueue::pushInterruptible(AVPacket *packet, qint64 ptsUs,
                                                  quint64 generation, bool keyframe,
                                                  bool dropOldestWhenFull,
                                                  int recheckAfterMs)
{
    std::unique_lock lock(m_mutex);
    if (dropOldestWhenFull && m_bounds.dropOldestWhenFull) {
        // Commands/cancel win over recovery so the packet remains intact for command processing.
        if (m_cancelled)
            return Admit::Cancelled;
        if (m_interruptRequested) {
            m_interruptRequested = false;
            return Admit::Interrupted;
        }
        if (isFullLocked())
            dropOldestBacklogLocked();
    } else {
        // Block while full, but wake for cancel or a command interrupt so the demux command loop is
        // never trapped. An empty queue is never full (one oversized packet is always accepted).
        const auto ready = [this] {
            return m_cancelled || m_interruptRequested || !isFullLocked();
        };
        if (recheckAfterMs > 0) {
            if (!m_notFull.wait_for(lock, std::chrono::milliseconds(recheckAfterMs), ready))
                return Admit::Interrupted; // packet intact: caller re-evaluates clock/lateness
        } else {
            m_notFull.wait(lock, ready);
        }
    }
    if (m_cancelled)
        return Admit::Cancelled;
    if (m_interruptRequested) {
        m_interruptRequested = false; // one-shot: observed here, the caller services + retries
        return Admit::Interrupted;    // packet left intact (not moved)
    }
    AVPacket *owned = av_packet_alloc();
    av_packet_move_ref(owned, packet);
    m_bufferedBytes += owned->size;
    m_entries.push_back(Entry{owned, ptsUs, generation, keyframe});
    m_notEmpty.notify_one();
    return Admit::Accepted;
}

void PacketQueue::interrupt()
{
    {
        std::scoped_lock lock(m_mutex);
        m_interruptRequested = true;
    }
    m_notFull.notify_all();
}

PacketQueue::PopResult PacketQueue::pop(AVPacket *out, quint64 *generation, bool *discontinuity)
{
    std::unique_lock lock(m_mutex);
    // Wait for supply, an end-of-stream signal, or cancellation.
    m_notEmpty.wait(lock,
                    [this] { return m_cancelled || !m_entries.empty() || m_endOfStream; });
    if (m_cancelled)
        return PopResult::Cancelled;
    if (m_entries.empty())
        return PopResult::EndOfStream; // woken by setEndOfStream with nothing left to drain
    // Report and consume a pending drop-oldest discontinuity: this packet (a keyframe) immediately
    // follows discarded backlog, so the consumer must flush its decoder before decoding it.
    const bool wasDiscontinuous = m_discontinuityPending;
    m_discontinuityPending = false;
    Entry entry = m_entries.front();
    m_entries.pop_front();
    m_bufferedBytes -= entry.packet->size;
    av_packet_move_ref(out, entry.packet);
    av_packet_free(&entry.packet);
    if (generation)
        *generation = entry.generation;
    if (discontinuity)
        *discontinuity = wasDiscontinuous;
    m_notFull.notify_one();
    return PopResult::Packet;
}

void PacketQueue::setEndOfStream()
{
    {
        std::scoped_lock lock(m_mutex);
        m_endOfStream = true;
    }
    // Wake a consumer parked on an empty queue so it observes the end instead of waiting forever.
    m_notEmpty.notify_all();
}

void PacketQueue::flush()
{
    {
        std::scoped_lock lock(m_mutex);
        for (Entry &entry : m_entries)
            av_packet_free(&entry.packet);
        m_entries.clear();
        m_bufferedBytes = 0;
        m_endOfStream = false; // a seek reopens the stream; the old end no longer applies
        m_discontinuityPending = false; // dropped backlog is gone; the consumer flushes for the seek
    }
    // Space is free now (producers may proceed); consumers re-check and wait fresh for new supply.
    m_notFull.notify_all();
    m_notEmpty.notify_all();
}

void PacketQueue::cancel()
{
    {
        std::scoped_lock lock(m_mutex);
        m_cancelled = true;
    }
    // Unblock every current and future waiter so the demux/decode threads can exit and join.
    m_notFull.notify_all();
    m_notEmpty.notify_all();
}

int PacketQueue::bufferedPackets() const
{
    std::scoped_lock lock(m_mutex);
    return static_cast<int>(m_entries.size());
}

qint64 PacketQueue::bufferedUs() const
{
    std::scoped_lock lock(m_mutex);
    return bufferedUsLocked();
}

qint64 PacketQueue::bufferedBytes() const
{
    std::scoped_lock lock(m_mutex);
    return m_bufferedBytes;
}

std::optional<qint64> PacketQueue::oldestPtsUs() const
{
    std::scoped_lock lock(m_mutex);
    if (m_entries.empty())
        return std::nullopt;
    return m_entries.front().ptsUs;
}

} // namespace Colosseum::Player2
