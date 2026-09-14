#if defined(P08_NEGATE_OWNERSHIP)
#define requestId requestId_removed
#elif defined(P08_NEGATE_GENERATION)
#define generation generation_removed
#elif defined(P08_NEGATE_EXACT_BLOCK)
#define length length_removed
#elif defined(P08_NEGATE_CANCELLATION)
#define CancelAction CancelAction_removed
#elif defined(P08_NEGATE_OBSERVATION)
#define poll poll_removed
#elif defined(P08_NEGATE_STATISTICS)
#define statistics statistics_removed
#elif defined(P08_NEGATE_AUTONOMY)
#define configureAutonomy configureAutonomy_removed
#endif

#include "server1/ports/TorrentTransport.h"

#if defined(P08_NEGATE_OWNERSHIP)
#undef requestId
#elif defined(P08_NEGATE_GENERATION)
#undef generation
#elif defined(P08_NEGATE_EXACT_BLOCK)
#undef length
#elif defined(P08_NEGATE_CANCELLATION)
#undef CancelAction
#elif defined(P08_NEGATE_OBSERVATION)
#undef poll
#elif defined(P08_NEGATE_STATISTICS)
#undef statistics
#elif defined(P08_NEGATE_AUTONOMY)
#undef configureAutonomy
#endif

#include <cstdlib>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace server1::ports;

[[noreturn]] void fail(const std::string &message)
{
    std::cerr << message << '\n';
    std::exit(1);
}

void expect(bool condition, const std::string &message)
{
    if (!condition) {
        fail(message);
    }
}

class ContractConsumer final : public TorrentTransport {
public:
    bool submit(const TorrentAction &action) override
    {
        actions.push_back(action);
        return true;
    }

    std::vector<TorrentObservation> poll() override
    {
        std::vector<TorrentObservation> result;
        result.swap(observations);
        return result;
    }

    TransportStatistics statistics() const override { return stats; }
    void close() override { closed = true; }

    std::vector<TorrentAction> actions;
    std::vector<TorrentObservation> observations;
    TransportStatistics stats;
    bool closed = false;
    bool suppressionApplied = false;
protected:
    bool applyAutonomySuppression() override { suppressionApplied = true; return true; }
};

} // namespace

int main()
{
    static_assert(std::is_abstract_v<TorrentTransport>);
    static_assert(std::is_same_v<decltype(RequestOwnership::requestId), std::uint64_t>);
    static_assert(std::is_same_v<decltype(RequestOwnership::generation), std::uint64_t>);
    static_assert(std::is_same_v<decltype(BlockSpan::length), std::uint32_t>);
    static_assert(std::is_same_v<decltype(&TorrentTransport::poll),
                                 std::vector<TorrentObservation> (TorrentTransport::*)()>);
    static_assert(std::is_same_v<decltype(&TorrentTransport::statistics),
                                 TransportStatistics (TorrentTransport::*)() const>);
    static_assert(std::is_same_v<decltype(&TorrentTransport::configureAutonomy),
                                 bool (TorrentTransport::*)(const server1::discovery::AutonomyPolicy &)>);

    ContractConsumer transport;
    expect(!transport.configureAutonomy({true, false, false})
               && !transport.configureAutonomy({false, true, false})
               && !transport.configureAutonomy({false, false, true})
               && !transport.suppressionApplied,
           "P08-T rejects autonomous picker/tracker/DHT before adapter activation");
    expect(transport.configureAutonomy({}) && transport.suppressionApplied,
           "P08-T carries and enforces explicit external-control suppression");
    const RequestOwnership owner{41, 7, 3};
    const BlockSpan full{9, 0, kWireBlockLength};
    const BlockSpan tail{9, kWireBlockLength, 123};
    expect(isValidBlock(full) && isValidBlock(tail), "P08-T exact block and tail contract");
    expect(!isValidBlock({9, 1, kWireBlockLength}), "P08-T rejects unaligned block");
    expect(!isValidBlock({9, 0, 0}), "P08-T rejects zero block");

    const RequestAction request{owner, 12, tail};
    const CancelAction cancel{owner, 12, tail, true};
    expect(transport.submit(request) && transport.submit(cancel),
           "P08-T request and cancellation action surface");
    expect(request.ownership.requestId == 41 && request.ownership.generation == 7
               && request.block.length == 123,
           "P08-T ownership, generation and exact tail are carried together");

    BlockObservation block{owner, 12, tail, {1, 2, 3}, false, false};
    PeerObservation peer{12, false, true, 65536.0, 1024.0, 4, 99};
    FailureObservation failure{owner, 12, tail, "timeout", true};
    transport.observations = {block, peer, failure};
    const auto observed = transport.poll();
    expect(observed.size() == 3, "P08-T typed observation surface");

    transport.stats = {2, 1, 1, 4096, 128, 65536.0, 1024.0, 3};
    const auto stats = transport.statistics();
    expect(stats.connectedPeers == 2 && stats.ownedRequestsOutstanding == 1
               && stats.pickerRequestsSuppressed == 3,
           "P08-T honest statistics surface");
    transport.close();
    expect(transport.closed, "P08-T explicit close lifecycle");

    std::cout << "P08-T PASS\n";
    return 0;
}
