#include "integration/SchedulerTransportBridge.h"
#include "scheduler/SchedulerSpine.h"

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <system_error>
#include <vector>

using namespace colosseum::server::integration;
using namespace colosseum::server::scheduler;

namespace {

class FakeBlockTransport final : public IBlockTransport
{
public:
    struct Pending {
        std::string peer;
        WireBlock block;
        Completion completion;
        bool cancelled = false;
    };

    void requestBlock(const std::string &peerHint, const WireBlock &block,
                      Completion completion) override
    {
        pending.push_back({peerHint, block, std::move(completion), false});
    }
    void cancelBlock(const std::string &peerHint, const WireBlock &block) override
    {
        for (auto &item : pending) {
            if (item.peer == peerHint && item.block.piece == block.piece
                && item.block.offset == block.offset && item.block.length == block.length) {
                item.cancelled = true;
            }
        }
    }

    void succeed(std::size_t index, std::byte fill)
    {
        assert(index < pending.size());
        auto completion = pending[index].completion;
        std::vector<std::byte> data(pending[index].block.length, fill);
        completion({}, std::move(data));
    }

    void fail(std::size_t index)
    {
        assert(index < pending.size());
        pending[index].completion(std::make_error_code(std::errc::io_error), {});
    }

    std::vector<Pending> pending;
};

PeerState peer(std::string id)
{
    PeerState result;
    result.id = std::move(id);
    result.peerChoking = false;
    result.downloaded = 1;
    result.downloadSpeed = 256 * 1024.0;
    result.peerPieces = {true};
    return result;
}
void testDispatchAndComplete()
{
    SchedulerSpine scheduler({32 * 1024});
    scheduler.select(0, 0, 10);

    FakeBlockTransport transport;
    SchedulerTransportBridge bridge(scheduler, transport);
    bridge.upsertPeer(peer("peer-a"));

    std::optional<CompletedPiece> completed;
    bridge.setCompletedObserver([&](CompletedPiece piece) {
        completed = std::move(piece);
    });

    bridge.pump();
    assert(transport.pending.size() == 2);
    assert(transport.pending[0].block.piece == 0);
    assert(transport.pending[0].block.offset == 0);
    assert(transport.pending[0].block.length == PieceBuffer::BlockSize);
    assert(transport.pending[1].block.offset == PieceBuffer::BlockSize);

    transport.succeed(1, std::byte{0x22});
    assert(!completed.has_value());
    transport.succeed(0, std::byte{0x11});
    assert(completed.has_value());
    assert(completed->piece == 0);
    assert(completed->bytes.size() == 32 * 1024);
    assert(completed->bytes.front() == std::byte{0x11});
    assert(completed->bytes.back() == std::byte{0x22});
}
void testFailureReleasesReservation()
{
    SchedulerSpine scheduler({PieceBuffer::BlockSize});
    scheduler.select(0, 0, 10);

    FakeBlockTransport transport;
    SchedulerTransportBridge bridge(scheduler, transport);
    bridge.upsertPeer(peer("peer-a"));

    bridge.pump();
    assert(transport.pending.size() == 1);
    transport.fail(0);

    bridge.pump();
    assert(transport.pending.size() == 2);
    assert(transport.pending[1].block.piece == 0);
    assert(transport.pending[1].block.offset == 0);
}

void testHotswapRetiresTransportWithoutCancellationCallback()
{
    SchedulerSpine scheduler({32 * 1024});
    scheduler.select(0, 0, 10);

    FakeBlockTransport transport;
    SchedulerTransportBridge bridge(scheduler, transport);

    auto slow = peer("slow");
    slow.downloadSpeed = 8 * 1024.0;
    bridge.upsertPeer(slow);
    bridge.pump();
    assert(scheduler.peer("slow")->activeRequestCount() == 2);

    bridge.upsertPeer(peer("fast"));
    bridge.pump();

    assert(scheduler.peer("slow")->activeRequestCount() == 0);
    assert(transport.pending.size() == 4);
    assert(transport.pending[0].cancelled);
    assert(transport.pending[1].cancelled);
}

void testSelectionNotificationMayCreateAnotherSelection()
{
    SchedulerSpine scheduler({PieceBuffer::BlockSize, PieceBuffer::BlockSize});
    bool notified = false;
    bool added = false;
    scheduler.select(0, 1, 10, [&] {
        notified = true;
        if (!added) {
            added = true;
            scheduler.select(1, 1, 9);
        }
    });

    scheduler.markPieceAvailable(0);
    assert(notified);
    assert(added);
}

} // namespace

int main()
{
    testDispatchAndComplete();
    testFailureReleasesReservation();
    testHotswapRetiresTransportWithoutCancellationCallback();
    testSelectionNotificationMayCreateAnotherSelection();
    std::puts("SCHEDULER_TRANSPORT_BRIDGE_OK");
    return 0;
}
