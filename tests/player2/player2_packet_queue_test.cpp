// Hermetic tests for PacketQueue — the bounded, thread-safe per-stream packet FIFO that lets the
// demuxer read ahead of playback. No media or network: packets are synthesized with av_new_packet,
// so the whole suite is deterministic. Each case proves one clause of the read-ahead contract:
// FIFO + ownership, bounded back-pressure, starvation wait/wake, drain-then-end-of-stream, flush,
// oversized-accept, and prompt cancel (so worker threads always join).

#include "player2/core/PacketQueue.h"

extern "C" {
#include <libavcodec/packet.h>
}

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

using namespace Colosseum::Player2;

namespace {

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

// A self-describing packet: `size` bytes with byte[0] = tag, for identity checks after a round-trip.
AVPacket *makePacket(int size, unsigned char tag)
{
    AVPacket *packet = av_packet_alloc();
    if (!packet || av_new_packet(packet, size) < 0)
        throw std::runtime_error("failed to allocate a test packet");
    if (size > 0)
        packet->data[0] = tag;
    return packet;
}

// Push a fresh packet and free the (now-blank) caller shell — mirrors the demux loop, which reuses
// one packet: av_read_frame fills it, push() moves out of it, the loop unrefs and reuses. On a
// rejected push the packet is left intact, so av_packet_free still reclaims it cleanly.
bool pushPacket(PacketQueue &queue, int size, unsigned char tag, qint64 ptsUs, quint64 generation)
{
    AVPacket *packet = makePacket(size, tag);
    const bool ok = queue.push(packet, ptsUs, generation);
    av_packet_free(&packet);
    return ok;
}

// -----------------------------------------------------------------------------------------------

void testFifoOrderOwnershipAndGeneration()
{
    PacketQueue queue(PacketQueue::Bounds{/*maxBufferedUs*/ 0, /*maxBytes*/ 0, /*maxPackets*/ 8});

    AVPacket *first = makePacket(4, 0x11);
    AVPacket *second = makePacket(6, 0x22);
    require(queue.push(first, /*ptsUs*/ 1'000, /*generation*/ 5), "first push accepted");
    require(first->size == 0 && first->data == nullptr,
            "push must take ownership (move-ref), leaving the caller's packet blank");
    require(queue.push(second, /*ptsUs*/ 2'000, /*generation*/ 5), "second push accepted");
    require(queue.bufferedPackets() == 2, "two packets buffered");
    require(queue.bufferedBytes() == 10, "buffered bytes is the sum of payload sizes");

    AVPacket *out = av_packet_alloc();
    quint64 gen = 0;
    require(queue.pop(out, &gen) == PacketQueue::PopResult::Packet, "pop returns the first packet");
    require(gen == 5, "generation carried through the queue");
    require(out->size == 4 && out->data[0] == 0x11, "FIFO: the first packet comes out first");
    av_packet_unref(out);

    require(queue.pop(out, &gen) == PacketQueue::PopResult::Packet, "pop returns the second packet");
    require(out->size == 6 && out->data[0] == 0x22, "FIFO: the second packet comes out second");
    require(queue.bufferedPackets() == 0, "queue empty after draining both");

    av_packet_free(&first);
    av_packet_free(&second);
    av_packet_free(&out);
}

void testPushBlocksWhenFullUntilPop()
{
    PacketQueue queue(PacketQueue::Bounds{/*maxBufferedUs*/ 0, /*maxBytes*/ 0, /*maxPackets*/ 2});
    require(pushPacket(queue, 4, 1, 1'000, 1), "push 1 into a queue of 2");
    require(pushPacket(queue, 4, 2, 2'000, 1), "push 2 fills the queue");
    require(queue.bufferedPackets() == 2, "the queue is full at its 2-packet bound");

    std::atomic<bool> pushReturned{false};
    std::atomic<bool> pushOk{false};
    std::thread producer([&] {
        pushOk = pushPacket(queue, 4, 3, 3'000, 1); // must block: the queue is full
        pushReturned = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // Observe under the full queue, then ALWAYS release + join before asserting, so a failure never
    // destroys a still-joinable thread (which would std::terminate and hide the message).
    const bool blockedWhileFull = !pushReturned.load();
    const int depthWhileBlocked = queue.bufferedPackets();

    AVPacket *out = av_packet_alloc();
    quint64 gen = 0;
    const PacketQueue::PopResult firstPop = queue.pop(out, &gen); // frees a slot for the producer
    av_packet_unref(out);
    producer.join();

    require(blockedWhileFull, "push must block while the queue is full");
    require(depthWhileBlocked == 2, "a blocked push adds nothing to the buffer");
    require(firstPop == PacketQueue::PopResult::Packet, "pop frees a slot");
    require(pushOk.load(), "the blocked push completes once a slot frees");
    require(queue.bufferedPackets() == 2, "the third packet took the freed slot");

    require(queue.pop(out, &gen) == PacketQueue::PopResult::Packet, "drain the remaining two (1)");
    av_packet_unref(out);
    require(queue.pop(out, &gen) == PacketQueue::PopResult::Packet, "drain the remaining two (2)");
    av_packet_free(&out);
}

void testPopBlocksWhenEmptyUntilPush()
{
    PacketQueue queue(PacketQueue::Bounds{0, 0, 8});
    std::atomic<bool> popReturned{false};
    std::atomic<int> result{-1};
    AVPacket *out = av_packet_alloc();
    quint64 gen = 0;
    std::thread consumer([&] {
        result = static_cast<int>(queue.pop(out, &gen)); // must block: the queue is empty
        popReturned = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const bool blockedWhileEmpty = !popReturned.load();

    // Always wake + join before asserting (terminate-safe on failure).
    const bool pushWoke = pushPacket(queue, 4, 7, 1'000, 3);
    consumer.join();

    require(blockedWhileEmpty, "pop must block while the queue is empty");
    require(pushWoke, "a push into a non-full queue succeeds");
    require(result.load() == static_cast<int>(PacketQueue::PopResult::Packet),
            "the woken pop returned a packet");
    require(gen == 3, "the pushed generation reached the consumer");
    require(out->size == 4 && out->data[0] == 7, "the pushed packet reached the consumer");
    av_packet_free(&out);
}

void testEmptyQueueAcceptsOversizedPacket()
{
    // A packet larger than every bound must still be accepted into an empty queue, or a pipeline
    // whose first packet exceeds the bound would deadlock forever.
    PacketQueue queue(PacketQueue::Bounds{/*maxBufferedUs*/ 0, /*maxBytes*/ 8, /*maxPackets*/ 0});
    require(pushPacket(queue, 100, 9, 1'000, 1),
            "an oversized packet is accepted into an empty queue without blocking");
    require(queue.bufferedPackets() == 1, "the oversized packet is buffered");
    AVPacket *out = av_packet_alloc();
    quint64 gen = 0;
    require(queue.pop(out, &gen) == PacketQueue::PopResult::Packet, "the oversized packet pops back");
    av_packet_free(&out);
}

// Poll `flag` up to `budgetMs`; return true as soon as it is set. Used to bound "must not block"
// assertions so a missing wake fails cleanly instead of hanging the whole suite.
bool waitForFlag(const std::atomic<bool> &flag, int budgetMs)
{
    for (int elapsed = 0; elapsed < budgetMs; elapsed += 5) {
        if (flag.load())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return flag.load();
}

void testFlushClearsBufferedAndResetsEndOfStream()
{
    PacketQueue queue(PacketQueue::Bounds{0, 0, 8});
    require(pushPacket(queue, 4, 1, 1'000, 1), "push 1");
    require(pushPacket(queue, 4, 2, 2'000, 1), "push 2");
    require(pushPacket(queue, 4, 3, 3'000, 1), "push 3");
    queue.setEndOfStream();

    queue.flush();
    require(queue.bufferedPackets() == 0, "flush drops all buffered packets");
    require(queue.bufferedBytes() == 0, "flush resets the byte accounting");

    // Flush also cleared the end-of-stream flag, so the queue is reusable: a fresh push/pop returns
    // the new packet, never a stale EndOfStream from before the seek.
    require(pushPacket(queue, 5, 8, 9'000, 2), "push after flush is accepted");
    AVPacket *out = av_packet_alloc();
    quint64 gen = 0;
    require(queue.pop(out, &gen) == PacketQueue::PopResult::Packet,
            "pop after flush returns the new packet, not a stale end-of-stream");
    require(gen == 2 && out->data[0] == 8, "the post-flush packet is the new one");
    av_packet_free(&out);
}

void testCancelUnblocksWaitersAndRejects()
{
    // Heap queues (shared_ptr) so a thread still stuck on a missing wake (RED) never touches freed
    // memory after the test bails.
    auto queue = std::make_shared<PacketQueue>(PacketQueue::Bounds{0, 0, /*maxPackets*/ 1});

    // (a) A blocked pop on an empty queue must wake with Cancelled.
    auto popWoke = std::make_shared<std::atomic<bool>>(false);
    auto popResult = std::make_shared<std::atomic<int>>(-1);
    AVPacket *out = av_packet_alloc();
    std::thread popper([queue, popWoke, popResult, out] {
        quint64 g = 0;
        *popResult = static_cast<int>(queue->pop(out, &g));
        *popWoke = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    queue->cancel();
    const bool popAwoke = waitForFlag(*popWoke, 2'000);
    if (popAwoke)
        popper.join();
    else
        popper.detach();
    require(popAwoke, "cancel must wake a blocked pop promptly");
    require(popResult->load() == static_cast<int>(PacketQueue::PopResult::Cancelled),
            "a cancelled pop returns Cancelled");
    require(!pushPacket(*queue, 4, 1, 1'000, 1), "push after cancel is rejected");
    if (popAwoke)
        av_packet_free(&out);

    // (b) A blocked push on a full queue must wake and return false.
    auto full = std::make_shared<PacketQueue>(PacketQueue::Bounds{0, 0, /*maxPackets*/ 1});
    require(pushPacket(*full, 4, 1, 1'000, 1), "fill the 1-slot queue");
    auto pushWoke = std::make_shared<std::atomic<bool>>(false);
    auto pushOk = std::make_shared<std::atomic<int>>(-1);
    std::thread producer([full, pushWoke, pushOk] {
        AVPacket *p = makePacket(4, 2);
        const bool ok = full->push(p, 2'000, 1); // blocks: the queue is full
        av_packet_free(&p);
        *pushOk = ok ? 1 : 0;
        *pushWoke = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    full->cancel();
    const bool pushAwoke = waitForFlag(*pushWoke, 2'000);
    if (pushAwoke)
        producer.join();
    else
        producer.detach();
    require(pushAwoke, "cancel must wake a blocked push promptly");
    require(pushOk->load() == 0, "a push cancelled while blocked returns false");
}

void testEndOfStreamDrainsThenSignals()
{
    auto queue = std::make_shared<PacketQueue>(PacketQueue::Bounds{0, 0, 8});
    require(pushPacket(*queue, 4, 1, 1'000, 1), "push 1");
    require(pushPacket(*queue, 4, 2, 2'000, 1), "push 2");
    queue->setEndOfStream();

    AVPacket *out = av_packet_alloc();
    quint64 gen = 0;
    require(queue->pop(out, &gen) == PacketQueue::PopResult::Packet, "buffered packets drain first (1)");
    av_packet_unref(out);
    require(queue->pop(out, &gen) == PacketQueue::PopResult::Packet, "buffered packets drain first (2)");
    av_packet_unref(out);

    // Now empty with end-of-stream set: pop must report EndOfStream WITHOUT blocking. Guard it on a
    // thread so a missing signal (RED) fails cleanly via the cancel escape instead of hanging.
    auto done = std::make_shared<std::atomic<bool>>(false);
    auto result = std::make_shared<std::atomic<int>>(-1);
    std::thread ender([queue, out, result, done] {
        quint64 g = 0;
        *result = static_cast<int>(queue->pop(out, &g));
        *done = true;
    });
    const bool finished = waitForFlag(*done, 2'000);
    if (!finished)
        queue->cancel(); // escape a RED hang; cancel() is proven by the prior test
    ender.join();
    require(finished, "end-of-stream must not block pop once the buffer is drained");
    require(result->load() == static_cast<int>(PacketQueue::PopResult::EndOfStream),
            "pop reports EndOfStream when drained and ended");

    // End-of-stream is idempotent: repeated pops keep reporting it, never hang.
    quint64 g = 0;
    require(queue->pop(out, &g) == PacketQueue::PopResult::EndOfStream, "end-of-stream is idempotent");
    av_packet_free(&out);
}

// Push a keyframe-tagged packet through the ownership-safe helper (frees on the caller's side).
bool pushKf(PacketQueue &queue, unsigned char tag, qint64 ptsUs, quint64 generation, bool keyframe)
{
    AVPacket *packet = makePacket(4, tag);
    const bool ok = queue.push(packet, ptsUs, generation, keyframe);
    av_packet_free(&packet);
    return ok;
}

// The video queue's policy: a full push NEVER blocks — it drops the oldest whole GOP so the new
// front is a keyframe, then admits. Single-threaded here: a blocking push would HANG this test, so a
// clean completion IS the proof the demux thread can never be stalled by a full video queue.
void testDropOldestWhenFullNeverBlocksAndKeepsNewest()
{
    PacketQueue queue(PacketQueue::Bounds{/*maxBufferedUs*/ 0, /*maxBytes*/ 0, /*maxPackets*/ 4,
                                          /*dropOldestWhenFull*/ true});
    require(pushKf(queue, 0xA0, 0, 1, /*keyframe*/ true), "K0 admitted");
    require(pushKf(queue, 0xB1, 1, 1, false), "P1 admitted");
    require(pushKf(queue, 0xB2, 2, 1, false), "P2 admitted");
    require(pushKf(queue, 0xA3, 3, 1, true), "K3 admitted — queue full at the 4-packet bound");
    require(queue.bufferedPackets() == 4, "queue is full");

    // Over-push: must not block; drops the leading GOP (K0,P1,P2) so the new front is keyframe K3.
    require(pushKf(queue, 0xB4, 4, 1, false), "over-push admitted WITHOUT blocking");
    require(queue.bufferedPackets() <= 4, "buffered stays within the bound after the drop");

    AVPacket *out = av_packet_alloc();
    quint64 gen = 0;
    bool discontinuity = false;
    require(queue.pop(out, &gen, &discontinuity) == PacketQueue::PopResult::Packet, "pop after drop");
    require(out->data[0] == 0xA3,
            "the oldest GOP was dropped; the retained keyframe pops first (a slow video skips ahead)");
    require(discontinuity, "the post-drop pop flags a discontinuity so the decoder flushes cleanly");
    av_packet_unref(out);
    av_packet_free(&out);
}

// The discontinuity flag is one-shot: it fires on the first pop after a drop, then clears.
void testDropDiscontinuityReportedOncePerDrop()
{
    PacketQueue queue(PacketQueue::Bounds{0, 0, /*maxPackets*/ 2, /*dropOldestWhenFull*/ true});
    require(pushKf(queue, 0xA0, 0, 1, true), "K0");
    require(pushKf(queue, 0xA1, 1, 1, true), "K1 — full");
    require(pushKf(queue, 0xA2, 2, 1, true), "K2 over-push drops the oldest");

    AVPacket *out = av_packet_alloc();
    quint64 gen = 0;
    bool discontinuity = false;
    require(queue.pop(out, &gen, &discontinuity) == PacketQueue::PopResult::Packet, "pop 1");
    require(discontinuity, "first pop after a drop flags a discontinuity");
    av_packet_unref(out);

    require(pushKf(queue, 0xA3, 3, 1, true), "K3 fits without overflow");
    discontinuity = true;
    require(queue.pop(out, &gen, &discontinuity) == PacketQueue::PopResult::Packet, "pop 2");
    require(!discontinuity, "a normal pop after the discontinuity was consumed does not re-flag");
    av_packet_unref(out);
    av_packet_free(&out);
}

} // namespace

int main()
{
    try {
        testFifoOrderOwnershipAndGeneration();
        testPushBlocksWhenFullUntilPop();
        testPopBlocksWhenEmptyUntilPush();
        testEmptyQueueAcceptsOversizedPacket();
        testFlushClearsBufferedAndResetsEndOfStream();
        testCancelUnblocksWaitersAndRejects();
        testEndOfStreamDrainsThenSignals();
        testDropOldestWhenFullNeverBlocksAndKeepsNewest();
        testDropDiscontinuityReportedOncePerDrop();
    } catch (const std::exception &error) {
        std::cerr << "player2_packet_queue_test: FAIL " << error.what() << '\n';
        return 1;
    }
    std::cout << "player2_packet_queue_test: PASS\n";
    return 0;
}
