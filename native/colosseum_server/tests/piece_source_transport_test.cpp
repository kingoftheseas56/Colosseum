#include "integration/PieceSourceBlockTransport.h"

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <map>
#include <set>
#include <system_error>
#include <vector>

using namespace colosseum::server::integration;
using namespace colosseum::server::scheduler;

namespace {

class FakePieceSource final : public IPieceSource
{
public:
    bool hasPiece(std::size_t piece) const override
    {
        return ready.contains(piece);
    }

    void makeUrgent(std::size_t piece) override
    {
        urgent.push_back(piece);
    }

    Subscription waitForPiece(std::size_t piece, Ready readyCallback) override
    {
        const auto token = ++nextToken;
        waiters[token] = {piece, std::move(readyCallback)};
        return token;
    }
    void cancelWait(Subscription token) override
    {
        cancelCalls.push_back(token);
        if (!retainCancelledWaiters)
            waiters.erase(token);
    }

    std::vector<std::byte> readBlock(const WireBlock &block,
                                     std::error_code &error) const override
    {
        error.clear();
        std::vector<std::byte> bytes(block.length);
        for (std::size_t i = 0; i < bytes.size(); ++i)
            bytes[i] = std::byte((block.offset + i) & 0xff);
        return bytes;
    }

    void complete(std::size_t piece)
    {
        ready.insert(piece);
        std::vector<Subscription> done;
        for (auto &[token, waiter] : waiters) {
            if (waiter.first == piece) {
                waiter.second({});
                done.push_back(token);
            }
        }
        for (auto token : done) waiters.erase(token);
    }

    struct Waiter { std::size_t first; Ready second; };
    std::set<std::size_t> ready;
    std::vector<std::size_t> urgent;
    std::map<Subscription, Waiter> waiters;
    std::vector<Subscription> cancelCalls;
    bool retainCancelledWaiters = false;
    Subscription nextToken = 0;
};
void testWaitThenReadExactBlock()
{
    FakePieceSource source;
    PieceSourceBlockTransport transport(source);
    const WireBlock block{3, 16 * 1024, 16 * 1024};

    bool called = false;
    transport.requestBlock("peer-a", block,
        [&](std::error_code error, std::vector<std::byte> bytes) {
            assert(!error);
            assert(bytes.size() == block.length);
            assert(bytes.front() == std::byte{0x00});
            assert(bytes[1] == std::byte{0x01});
            called = true;
        });

    assert(!called);
    assert(source.urgent == std::vector<std::size_t>({3}));
    assert(source.waiters.size() == 1);
    source.complete(3);
    assert(called);
    assert(source.waiters.empty());
}

void testCompletedWaitIsRemovedBeforeTransportDestruction()
{
    FakePieceSource source;
    const WireBlock block{3, 0, 8};
    {
        PieceSourceBlockTransport transport(source);
        transport.requestBlock("peer-a", block,
            [](std::error_code error, std::vector<std::byte> bytes) {
                assert(!error && bytes.size() == 8);
            });
        source.complete(3);
    }
    assert(source.cancelCalls.empty());
}

void testCancelledWaitSuppressesLateReadyCallback()
{
    FakePieceSource source;
    source.retainCancelledWaiters = true;
    const WireBlock block{5, 0, 8};
    bool called = false;
    {
        PieceSourceBlockTransport transport(source);
        transport.requestBlock("peer-a", block,
            [&](std::error_code, std::vector<std::byte>) { called = true; });
        transport.cancelBlock("peer-a", block);
        source.complete(5);
    }
    assert(!called);
}

void testAlreadyAvailableReadsImmediately()
{
    FakePieceSource source;
    source.ready.insert(4);
    PieceSourceBlockTransport transport(source);
    const WireBlock block{4, 7, 9};
    bool called = false;
    transport.requestBlock("peer-a", block,
        [&](std::error_code error, std::vector<std::byte> bytes) {
            assert(!error && bytes.size() == 9);
            called = true;
        });
    assert(called);
    assert(source.waiters.empty());
}

} // namespace

int main()
{
    testWaitThenReadExactBlock();
    testAlreadyAvailableReadsImmediately();
    testCompletedWaitIsRemovedBeforeTransportDestruction();
    testCancelledWaitSuppressesLateReadyCallback();
    std::puts("PIECE_SOURCE_BLOCK_TRANSPORT_OK");
    return 0;
}
