#if defined(P08_NEGATE_OWNERSHIP)
#define requestId requestId_removed
#elif defined(P08_NEGATE_GENERATION)
#define generation generation_removed
#elif defined(P08_NEGATE_EXACT_BLOCK)
#define length length_removed
#elif defined(P08_NEGATE_BLOCK_IDENTITY)
#define blockOrdinal blockOrdinal_removed
#elif defined(P08_NEGATE_CANCELLATION)
#define CancelAction CancelAction_removed
#elif defined(P08_NEGATE_OBSERVATION)
#define poll poll_removed
#elif defined(P08_NEGATE_STATISTICS)
#define statistics statistics_removed
#elif defined(P08_NEGATE_AUTONOMY)
#define configureAutonomy configureAutonomy_removed
#elif defined(P08_NEGATE_SOURCE_GENERATION)
#define EngineGeneration EngineGeneration_removed
#elif defined(P08_NEGATE_V1_INFO_HASH)
#define V1InfoHash V1InfoHash_removed
#elif defined(P08_NEGATE_OPEN_REQUEST)
#define TorrentOpenRequest TorrentOpenRequest_removed
#elif defined(P08_NEGATE_CONNECT_ACTION)
#define ConnectAction ConnectAction_removed
#elif defined(P08_NEGATE_METADATA_READY)
#define MetadataReadyObservation MetadataReadyObservation_removed
#elif defined(P08_NEGATE_SOURCE_FAILURE)
#define SourceFailureObservation SourceFailureObservation_removed
#elif defined(P08_NEGATE_AVAILABLE_PIECES)
#define AvailablePiecesObservation AvailablePiecesObservation_removed
#endif

#include "server1/ports/TorrentTransport.h"

#if defined(P08_NEGATE_OWNERSHIP)
#undef requestId
#elif defined(P08_NEGATE_GENERATION)
#undef generation
#elif defined(P08_NEGATE_EXACT_BLOCK)
#undef length
#elif defined(P08_NEGATE_BLOCK_IDENTITY)
#undef blockOrdinal
#elif defined(P08_NEGATE_CANCELLATION)
#undef CancelAction
#elif defined(P08_NEGATE_OBSERVATION)
#undef poll
#elif defined(P08_NEGATE_STATISTICS)
#undef statistics
#elif defined(P08_NEGATE_AUTONOMY)
#undef configureAutonomy
#elif defined(P08_NEGATE_SOURCE_GENERATION)
#undef EngineGeneration
#elif defined(P08_NEGATE_V1_INFO_HASH)
#undef V1InfoHash
#elif defined(P08_NEGATE_OPEN_REQUEST)
#undef TorrentOpenRequest
#elif defined(P08_NEGATE_CONNECT_ACTION)
#undef ConnectAction
#elif defined(P08_NEGATE_METADATA_READY)
#undef MetadataReadyObservation
#elif defined(P08_NEGATE_SOURCE_FAILURE)
#undef SourceFailureObservation
#elif defined(P08_NEGATE_AVAILABLE_PIECES)
#undef AvailablePiecesObservation
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
    static_assert(std::is_same_v<decltype(BlockSpan::blockOrdinal), std::uint32_t>);
    static_assert(std::is_same_v<decltype(&TorrentTransport::poll),
                                 std::vector<TorrentObservation> (TorrentTransport::*)()>);
    static_assert(std::is_same_v<decltype(&TorrentTransport::statistics),
                                 TransportStatistics (TorrentTransport::*)() const>);
    static_assert(std::is_same_v<decltype(&TorrentTransport::configureAutonomy),
                                 bool (TorrentTransport::*)(const server1::discovery::AutonomyPolicy &)>);
    static_assert(std::is_same_v<EngineGeneration, std::uint64_t>);
    static_assert(std::is_same_v<V1InfoHash, std::array<std::uint8_t, 20>>);
    static_assert(!std::is_default_constructible_v<TorrentOpenRequest>);
    static_assert(std::is_same_v<decltype(TorrentOpenRequest::generation), EngineGeneration>);
    static_assert(std::is_same_v<decltype(TorrentOpenRequest::infoHash), V1InfoHash>);
    static_assert(std::is_same_v<decltype(TorrentOpenRequest::source), TorrentSource>);
    static_assert(std::is_same_v<decltype(TorrentOpenRequest::savePath), std::string>);
    static_assert(std::is_same_v<decltype(&openTorrentTransport),
                                 std::unique_ptr<TorrentTransport> (*)(const TorrentOpenRequest &) noexcept>);
    static_assert(std::is_same_v<decltype(AvailablePiecesObservation::generation), EngineGeneration>);
    static_assert(std::is_same_v<decltype(AvailablePiecesObservation::peer), PeerHandle>);
    static_assert(std::is_same_v<decltype(AvailablePiecesObservation::pieces),
                                 std::vector<std::uint32_t>>);

    ContractConsumer transport;
    expect(!transport.configureAutonomy({true, false, false})
               && !transport.configureAutonomy({false, true, false})
               && !transport.configureAutonomy({false, false, true})
               && !transport.suppressionApplied,
           "P08-T rejects autonomous picker/tracker/DHT before adapter activation");
    expect(transport.configureAutonomy({}) && transport.suppressionApplied,
           "P08-T carries and enforces explicit external-control suppression");
    const RequestOwnership owner{41, 7, 3};
    const BlockSpan full{9, 0, 0, kWireBlockLength};
    const BlockSpan tail{9, 1, kWireBlockLength, 123};
    expect(isValidBlock(full) && isValidBlock(tail), "P08-T exact block and tail contract");
    expect(!isValidBlock({9, 0, 1, kWireBlockLength}), "P08-T rejects unaligned block");
    expect(!isValidBlock({9, 2, kWireBlockLength, 123}),
           "P08-T rejects block ordinal and offset disagreement");
    expect(!isValidBlock({9, 0, 0, 0}), "P08-T rejects zero block");

    const RequestAction request{owner, 12, tail};
    const CancelAction cancel{owner, 12, tail, true};
    const ConnectAction connect{7, 12, "127.0.0.1", 49080};
    expect(transport.submit(request) && transport.submit(cancel) && transport.submit(connect),
           "P08-T request, cancellation, and public connection action surface");
    expect(request.ownership.requestId == 41 && request.ownership.generation == 7
               && request.block.blockOrdinal == 1 && request.block.length == 123,
           "P08-T ownership, generation, block ordinal and exact tail are carried together");

    const server1::policy::SchedulerAction schedulerRequest{
        server1::policy::SchedulerActionType::Request,
        {41, 7, 3, 12, 9, 1, kWireBlockLength, 123}, false};
    const auto convertedRequest = toTorrentAction(schedulerRequest);
    expect(convertedRequest && std::holds_alternative<RequestAction>(*convertedRequest),
           "P08-T consumes the K04 request action contract");
    const auto &convertedRequestValue = std::get<RequestAction>(*convertedRequest);
    expect(convertedRequestValue.ownership.requestId == 41
               && convertedRequestValue.ownership.generation == 7
               && convertedRequestValue.ownership.selectionId == 3
               && convertedRequestValue.peer == 12
               && convertedRequestValue.block.piece == 9
               && convertedRequestValue.block.blockOrdinal == 1
               && convertedRequestValue.block.offset == kWireBlockLength
               && convertedRequestValue.block.length == 123,
           "P08-T conversion preserves K04 ownership, peer, and exact block span");
    auto inconsistentBlock = schedulerRequest;
    inconsistentBlock.request.block = 0;
    expect(!toTorrentAction(inconsistentBlock),
           "P08-T conversion rejects K04 block ordinal and offset disagreement");
    auto schedulerCancel = schedulerRequest;
    schedulerCancel.type = server1::policy::SchedulerActionType::Cancel;
    schedulerCancel.requestWireCancel = true;
    const auto convertedCancel = toTorrentAction(schedulerCancel);
    expect(convertedCancel && std::holds_alternative<CancelAction>(*convertedCancel)
               && std::get<CancelAction>(*convertedCancel).requestWireCancel
               && std::get<CancelAction>(*convertedCancel).ownership.requestId == 41
               && std::get<CancelAction>(*convertedCancel).ownership.generation == 7
               && std::get<CancelAction>(*convertedCancel).ownership.selectionId == 3
               && std::get<CancelAction>(*convertedCancel).peer == 12
               && std::get<CancelAction>(*convertedCancel).block.piece == 9
               && std::get<CancelAction>(*convertedCancel).block.blockOrdinal == 1
               && std::get<CancelAction>(*convertedCancel).block.offset == kWireBlockLength
               && std::get<CancelAction>(*convertedCancel).block.length == 123,
           "P08-T preserves K04 block identity on explicit wire cancellation");

    BlockObservation block{owner, 12, tail, {1, 2, 3}, false, false};
    PeerObservation peer{12, false, true, 65536.0, 1024.0, 4, 99};
    FailureObservation failure{owner, 12, tail, "timeout", true};
    const V1InfoHash infoHash{0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23,
                              0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67};
    MetadataReadyObservation metadata{7, infoHash,
                                      {1, 2, 3}, {"http://tracker.invalid/announce"},
                                      {"http://seed.invalid/file"}};
    SourceFailureObservation sourceFailure{7, infoHash, "invalid source", false};
    AvailablePiecesObservation available{7, 12, {1, 4, 9}};
    transport.observations = {block, peer, failure, metadata, sourceFailure, available};
    const auto observed = transport.poll();
    expect(observed.size() == 6
               && std::get<MetadataReadyObservation>(observed[3]).generation == 7
               && std::get<MetadataReadyObservation>(observed[3]).infoSection.size() == 3
               && std::get<SourceFailureObservation>(observed[4]).generation == 7
               && std::get<AvailablePiecesObservation>(observed[5]).generation == 7
               && std::get<AvailablePiecesObservation>(observed[5]).peer == 12
               && std::get<AvailablePiecesObservation>(observed[5]).pieces
                    == std::vector<std::uint32_t>({1, 4, 9}),
           "P08-T typed source and full peer-availability observations carry generation");

    const TorrentOpenRequest bare{7, infoHash, InfoHashSource{}, "download"};
    const TorrentOpenRequest magnet{8, infoHash,
                                    MagnetSource{"magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567"},
                                    "download"};
    const TorrentOpenRequest cached{9, infoHash, MetainfoSource{{1, 2, 3}}, "download"};
    expect(std::holds_alternative<InfoHashSource>(bare.source)
               && std::holds_alternative<MagnetSource>(magnet.source)
               && std::holds_alternative<MetainfoSource>(cached.source),
           "P08-T source variant makes bare hash, magnet, and metainfo mutually exclusive");

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
