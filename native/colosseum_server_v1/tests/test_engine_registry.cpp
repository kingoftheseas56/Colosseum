#include "server1/policy/EngineRegistry.h"
#include "server1/policy/TorrentMetadata.h"

#include <QCryptographicHash>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace server1::policy;
using namespace server1::ports;
using Bytes = std::vector<std::uint8_t>;

void expect(bool condition, const std::string &message)
{
    if (!condition) throw std::runtime_error(message);
}

std::string bbytes(const std::string &value)
{ return std::to_string(value.size()) + ":" + value; }
std::string bint(std::int64_t value)
{ return "i" + std::to_string(value) + "e"; }
std::string blist(const std::vector<std::string> &values)
{
    std::string result = "l";
    for (const auto &value : values) result += value;
    return result + "e";
}
std::string bdict(const std::vector<std::pair<std::string, std::string>> &values)
{
    std::string result = "d";
    for (const auto &[key, value] : values) result += bbytes(key) + value;
    return result + "e";
}
Bytes bytes(const std::string &value) { return {value.begin(), value.end()}; }
std::filesystem::path uniqueRoot(const std::string &caseId)
{
    return std::filesystem::temp_directory_path()
        / ("server1-" + caseId + "-"
           + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

struct TorrentFixture final {
    Bytes info;
    Bytes metainfo;
    std::string hash;
    V1InfoHash hashBytes{};
};

TorrentFixture fixture(std::string name,
                       std::vector<std::pair<std::string, std::int64_t>> files)
{
    std::int64_t total = 0;
    for (const auto &[path, length] : files) {
        static_cast<void>(path);
        total += length;
    }
    std::string info;
    const auto pieceHashes = bbytes(std::string(
        static_cast<std::size_t>((total + 3) / 4) * 20, 'p'));
    if (files.size() == 1) {
        info = bdict({{"length", bint(total)}, {"name", bbytes(name)},
                      {"piece length", bint(4)}, {"pieces", pieceHashes}});
    } else {
        std::vector<std::string> encodedFiles;
        for (const auto &[path, length] : files)
            encodedFiles.push_back(bdict({{"length", bint(length)},
                                           {"path", blist({bbytes(path)})}}));
        info = bdict({{"files", blist(encodedFiles)}, {"name", bbytes(name)},
                      {"piece length", bint(4)}, {"pieces", pieceHashes}});
    }
    const auto metainfo = bytes(bdict({{"info", info}}));
    std::string error;
    const auto parsed = TorrentMetadata::parse(metainfo, &error);
    expect(parsed.has_value(), "fixture metadata failed: " + error);
    TorrentFixture result{bytes(info), metainfo, parsed->infoHash(), {}};
    std::copy(parsed->infoHashBytes().begin(), parsed->infoHashBytes().end(),
              result.hashBytes.begin());
    return result;
}

TorrentFixture dataFixture(std::string name,
                           std::vector<std::pair<std::string, std::int64_t>> files,
                           const Bytes &payload,
                           std::size_t pieceLength = 4)
{
    std::string hashes;
    for (std::size_t offset = 0; offset < payload.size(); offset += pieceLength) {
        const auto length = std::min(pieceLength, payload.size() - offset);
        const QByteArray piece(reinterpret_cast<const char *>(payload.data() + offset),
                               static_cast<qsizetype>(length));
        const auto digest = QCryptographicHash::hash(piece, QCryptographicHash::Sha1);
        hashes.append(digest.constData(), static_cast<std::size_t>(digest.size()));
    }
    std::string info;
    if (files.size() == 1) {
        info = bdict({{"length", bint(static_cast<std::int64_t>(payload.size()))},
                      {"name", bbytes(name)},
                      {"piece length", bint(static_cast<std::int64_t>(pieceLength))},
                      {"pieces", bbytes(hashes)}});
    } else {
        std::vector<std::string> encodedFiles;
        for (const auto &[path, length] : files)
            encodedFiles.push_back(bdict({{"length", bint(length)},
                                           {"path", blist({bbytes(path)})}}));
        info = bdict({{"files", blist(encodedFiles)}, {"name", bbytes(name)},
                      {"piece length", bint(static_cast<std::int64_t>(pieceLength))},
                      {"pieces", bbytes(hashes)}});
    }
    const auto metainfo = bytes(bdict({{"info", info}}));
    std::string error;
    const auto parsed = TorrentMetadata::parse(metainfo, &error);
    expect(parsed.has_value(), "data fixture metadata failed: " + error);
    TorrentFixture result{bytes(info), metainfo, parsed->infoHash(), {}};
    std::copy(parsed->infoHashBytes().begin(), parsed->infoHashBytes().end(),
              result.hashBytes.begin());
    return result;
}

struct FakeState final {
    explicit FakeState(const TorrentOpenRequest &opened)
        : request(opened), constructedOn(std::this_thread::get_id()) {}
    TorrentOpenRequest request;
    std::vector<TorrentObservation> observations;
    std::vector<TorrentAction> actions;
    std::thread::id constructedOn;
    std::size_t closeCount = 0;
    std::size_t autonomyCount = 0;
    TransportStatistics statistics{};
};

class FakeTransport final : public TorrentTransport {
public:
    explicit FakeTransport(std::shared_ptr<FakeState> state) : state_(std::move(state)) {}
    bool submit(const TorrentAction &action) override
    { state_->actions.push_back(action); return true; }
    std::vector<TorrentObservation> poll() override
    { std::vector<TorrentObservation> result; result.swap(state_->observations); return result; }
    TransportStatistics statistics() const override
    { auto result = state_->statistics; result.downloadedBytes = 77; return result; }
    void close() override { ++state_->closeCount; }
protected:
    bool applyAutonomySuppression() override
    { ++state_->autonomyCount; return true; }
private:
    std::shared_ptr<FakeState> state_;
};

class ProbeTimer final : public EngineTimer {
public:
    ProbeTimer(std::uint64_t timerId, std::shared_ptr<std::size_t> cancels,
               EngineContinuation callback = {})
        : id_(timerId), cancels_(std::move(cancels)), callback_(std::move(callback)) {}
    void cancel() noexcept override
    {
        if (active_) { active_ = false; ++*cancels_; }
    }
    bool active() const noexcept override { return active_; }
    std::uint64_t id() const noexcept override { return id_; }
    void fire() { if (active_ && callback_) callback_(); }
private:
    std::uint64_t id_;
    std::shared_ptr<std::size_t> cancels_;
    EngineContinuation callback_;
    bool active_ = true;
};

struct FakeFactory final {
    std::vector<std::shared_ptr<FakeState>> states;
    std::unique_ptr<TorrentTransport> open(const TorrentOpenRequest &request)
    {
        auto state = std::make_shared<FakeState>(request);
        states.push_back(state);
        return std::make_unique<FakeTransport>(state);
    }
};

void ready(const std::shared_ptr<FakeState> &state, const TorrentFixture &torrent)
{
    state->observations.push_back(MetadataReadyObservation{
        state->request.generation, torrent.hashBytes, torrent.info,
        {"udp://tracker"}, {}});
}

std::size_t countEvents(const std::vector<EngineEvent> &events, EngineEventType type)
{
    return static_cast<std::size_t>(std::count_if(events.begin(), events.end(),
        [type](const auto &event) { return event.type == type; }));
}

void runK1101()
{
    const auto torrent = fixture("one.bin", {{"one.bin", 8}});
    std::vector<EngineCreateRequest> hooks;
    std::vector<EngineContinuation> continuations;
    std::vector<EngineEvent> events;
    std::vector<std::string> candidateCallbacks;
    FakeFactory factory;
    const auto appThread = std::this_thread::get_id();
    EngineRegistryConfig config;
    config.cacheRoot = uniqueRoot("k11-01");
    config.beforeCreate = [&](const auto &request, auto continuation) {
        hooks.push_back(request); continuations.push_back(std::move(continuation));
    };
    config.onEvent = [&](const EngineEvent &event) {
        expect(std::this_thread::get_id() == appThread,
               "K11-01 event escaped dispatcher thread");
        events.push_back(event);
    };
    config.transportFactory = [&](const TorrentOpenRequest &request) {
        expect(std::this_thread::get_id() == appThread,
               "K11-01 construction escaped dispatcher thread");
        return factory.open(request);
    };
    EngineRegistry registry(std::move(config));
    std::string mixed = torrent.hash;
    std::transform(mixed.begin(), mixed.end(), mixed.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    const auto t1 = registry.create({mixed, Value::object({
        {"stream", Value::string("magnet:?xt=urn:btih:" + mixed)},
        {"marker", Value::number(1)},
        {"path", Value::boolean(false)},
        {"peerSearch", Value::object({{"sources", Value::array({Value::string("dht:custom")})}})}})},
        [&](auto) { candidateCallbacks.push_back("C1"); });
    const auto t2 = registry.create({mixed, Value::object({{"marker", Value::number(2)}})},
                                    [&](auto) { candidateCallbacks.push_back("C2"); });
    expect(t1 && t2 && t1 != t2, "K11-01 create ids are not fresh");
    expect(hooks.size() == 2 && hooks[0].sourceKey == torrent.hash
               && hooks[0].options.find("torrent"),
           "K11-01 canonicalization/stream alias did not precede both hooks");
    std::thread worker([&] { continuations[1](); continuations[1](); });
    worker.join();
    expect(factory.states.empty(), "K11-01 held hook constructed off dispatcher");
    registry.dispatch();
    continuations[0]();
    registry.dispatch();
    expect(factory.states.size() == 1 && registry.constructionCount() == 1,
           "K11-01 reordered/reentrant continuation reconstructed E1");
    const auto engine = registry.get(mixed);
    expect(engine == registry.get(torrent.hash) && engine->resumeCount() == 2,
           "K11-01 canonical reuse did not resume E1 twice");
    expect(countEvents(events, EngineEventType::Create) == 2
               && countEvents(events, EngineEventType::Created) == 1,
           "K11-01 create/created cardinality drifted");
    std::vector<std::string> ids;
    for (const auto &event : events) if (event.type == EngineEventType::Create)
        ids.push_back(event.options.find("id")->asString());
    expect(ids.size() == 2 && ids[0] != ids[1], "K11-01 effective ids were reused");
    const auto options = engine->options();
    expect(options.find("path") && options.find("path")->kind() == Value::Kind::String,
           "K11-01 falsy path did not fall back");
    expect(options.find("peerSearch") && !options.find("peerSearch")->find("min"),
           "K11-01 merge was deep instead of shallow");
    expect(factory.states[0]->autonomyCount == 1,
           "K11-01 transport autonomy was not suppressed");
    ready(factory.states[0], torrent);
    registry.poll(); registry.dispatch();
    expect(candidateCallbacks == std::vector<std::string>({"C2", "C1"})
               && countEvents(events, EngineEventType::ScopedReady) == 2
               && countEvents(events, EngineEventType::Ready) == 2,
           "K11-01 oracle-normalized ready callback ordering drifted");
    std::cout << "K11-CANDIDATE-TRACE {\"constructions\":1,\"events\":[\"Create\","
                 "\"Created\",\"Create\",\"ScopedReady\",\"Ready\","
                 "\"ScopedReady\",\"Ready\"],\"createMarkers\":[2,1],"
                 "\"callbacks\":[\"C2\",\"C1\"],\"resumeCount\":2}\n";

    FakeFactory hardenedFactory;
    std::vector<EngineCreateResult> hardenedResults;
    EngineRegistryConfig hardened;
    hardened.cacheRoot = uniqueRoot("k11-01-hardened");
    hardened.workExecutor = [](EngineContinuation task) { task(); return true; };
    hardened.callbackExecutor = [](EngineContinuation task) { task(); return true; };
    hardened.beforeCreate = [](const auto &, EngineContinuation continuation) {
        continuation();
        throw std::runtime_error("throw after continuation");
    };
    hardened.transportFactory = [&](const auto &request) { return hardenedFactory.open(request); };
    EngineRegistry hardenedRegistry(std::move(hardened));
    hardenedRegistry.create({torrent.hash, Value::object({})}, [&](auto result) {
        hardenedResults.push_back(std::move(result));
    });
    expect(hardenedFactory.states.empty() && hardenedResults.empty(),
           "K11-01 inline executor escaped the deferred boundary");
    hardenedRegistry.dispatch();
    expect(hardenedFactory.states.empty() && hardenedRegistry.constructionCount() == 0
               && hardenedResults.size() == 1
               && hardenedResults[0].outcome == EngineCreateOutcome::SourceError
               && hardenedResults[0].error == "throw after continuation",
           "K11-01 continuation-then-throw constructed or did not fail exactly once");

    FakeFactory timerFactory;
    auto timerCancels = std::make_shared<std::size_t>(0);
    std::shared_ptr<ProbeTimer> probeTimer;
    EngineRegistryConfig timed;
    timed.repeat = [timerCancels, &probeTimer](std::uint64_t, EngineContinuation callback) {
        probeTimer = std::make_shared<ProbeTimer>(9001, timerCancels, std::move(callback));
        return probeTimer;
    };
    timed.transportFactory = [&](const auto &request) { return timerFactory.open(request); };
    EngineRegistry timedRegistry(std::move(timed));
    timedRegistry.create({torrent.hash, Value::object({
        {"swarmCap", Value::object({{"maxSpeed", Value::number(10)},
                                    {"minPeers", Value::number(0)}})}})});
    timedRegistry.dispatch();
    expect(timedRegistry.get(torrent.hash)->timerOwner() == 9001,
           "K11-01 real timer injection was not observable");
    ready(timerFactory.states[0], torrent);
    timedRegistry.poll(); timedRegistry.dispatch();
    timerFactory.states[0]->statistics.unchokedPeers = 2;
    timerFactory.states[0]->statistics.downloadBytesPerSecond = 100;
    probeTimer->fire(); timedRegistry.dispatch();
    expect(!timerFactory.states[0]->actions.empty()
               && std::holds_alternative<PauseAction>(timerFactory.states[0]->actions.back())
               && std::get<PauseAction>(timerFactory.states[0]->actions.back()).paused,
           "K11-01 timer tick did not apply accepted swarm cap");
    timedRegistry.create({torrent.hash, Value::object({
        {"swarmCap", Value::object({{"maxSpeed", Value::number(1000)},
                                    {"minPeers", Value::number(0)}})}})});
    timedRegistry.dispatch();
    probeTimer->fire(); timedRegistry.dispatch();
    expect(!timerFactory.states[0]->actions.empty()
               && std::holds_alternative<PauseAction>(timerFactory.states[0]->actions.back())
               && !std::get<PauseAction>(timerFactory.states[0]->actions.back()).paused,
           "K11-01 reuse option snapshot did not resume swarm cap");
    timedRegistry.remove(torrent.hash); timedRegistry.dispatch();
    expect(*timerCancels == 1, "K11-01 owned repeat timer was not cancelled exactly once");
    std::cout << "K11-01 registry/hooks/options PASS\n";
}

void runK1102(bool emitTrace = false)
{
    const Bytes causalBytes{'A','B','C','D','E','F','G','H','I','J','K','L'};
    const auto torrent = dataFixture("root", {{"a.bin", 4}, {"b.bin", 4}, {"c.bin", 4}},
                                     causalBytes);
    FakeFactory factory;
    std::vector<EngineEvent> events;
    std::vector<EngineCreateResult> results;
    EngineRegistryConfig config;
    config.cacheRoot = uniqueRoot("k11-02");
    config.transportFactory = [&](const auto &request) { return factory.open(request); };
    config.onEvent = [&](const auto &event) { events.push_back(event); };
    auto timerCancels = std::make_shared<std::size_t>(0);
    std::uint64_t nextTimerId = 100;
    config.repeat = [&](std::uint64_t, EngineContinuation callback) {
        return std::make_shared<ProbeTimer>(++nextTimerId, timerCancels,
                                            std::move(callback));
    };
    EngineRegistry registry(std::move(config));
    registry.create({torrent.hash, Value::object({})},
                    [&](auto result) { results.push_back(std::move(result)); });
    registry.create({torrent.hash, Value::object({{"marker", Value::number(2)}})},
                    [&](auto result) { results.push_back(std::move(result)); });
    registry.dispatch();
    expect(factory.states.size() == 1 && results.empty(),
           "K11-02 two premetadata creates did not share one nonblocking construction");
    ready(factory.states[0], torrent);
    registry.poll();
    expect(results.empty(), "K11-02 poll bypassed dispatcher");
    registry.dispatch();
    expect(results.size() == 2 && results[0].engine == results[1].engine
               && countEvents(events, EngineEventType::ScopedReady) == 2
               && countEvents(events, EngineEventType::Ready) == 2,
           "K11-02 callback/ready-pair fanout drifted");
    auto engine = results[0].engine;
    expect(engine->fileCount() == 3 && engine->hasPersistentStore()
               && engine->hasPeerSearch(),
           "K11-02 metadata components were not initialized once");
    auto readerA = engine->createReader(0);
    auto readerB = engine->createReader(1);
    expect(readerA != readerB && engine->readerCount() == 2
               && engine->selectionCount() == 2,
           "K11-02 two readers did not share E1 scheduler");

    factory.states[0]->observations.push_back(AvailablePiecesObservation{
        engine->generation(), 41, {0, 1, 2}});
    factory.states[0]->observations.push_back(PeerObservation{
        41, false, true, 1000.0, 0.0, 0, 0});
    const BlockSpan uploadSpan{0, 0, 0, 4};
    factory.states[0]->observations.push_back(UploadRequestObservation{
        UploadOwnership{1, engine->generation()}, 41, uploadSpan});
    registry.poll();
    expect(std::any_of(factory.states[0]->actions.begin(), factory.states[0]->actions.end(),
                       [](const auto &action) {
        return std::holds_alternative<UploadAbortAction>(action);
    }), "K11-02 precommit upload was not aborted");
    readerA->request(4);
    const auto request = std::find_if(factory.states[0]->actions.begin(),
                                      factory.states[0]->actions.end(), [](const auto &action) {
        return std::holds_alternative<RequestAction>(action);
    });
    expect(request != factory.states[0]->actions.end(),
           "K11-02 FileReader demand did not reach K10 RequestAction");
    const auto requested = std::get<RequestAction>(*request);
    expect(requested.block.piece == 0 && requested.block.offset == 0
               && requested.block.length == 4,
           "K11-02 scheduler request lost exact virtual/block coordinates: piece="
               + std::to_string(requested.block.piece) + " offset="
               + std::to_string(requested.block.offset) + " length="
               + std::to_string(requested.block.length));
    factory.states[0]->observations.push_back(BlockObservation{
        RequestOwnership{requested.ownership.requestId, engine->generation() + 1,
                         requested.ownership.selectionId},
        requested.peer, requested.block,
        Bytes(causalBytes.begin(), causalBytes.begin() + 4), false, false});
    registry.poll();
    expect(readerA->takeData().empty(),
           "K11-02 stale-generation block mutated active ownership");
    factory.states[0]->observations.push_back(BlockObservation{
        requested.ownership, requested.peer, requested.block,
        Bytes(causalBytes.begin(), causalBytes.begin() + 4), false, false});
    registry.poll();
    const auto chunks = readerA->takeData();
    expect(chunks == std::vector<Bytes>{Bytes(causalBytes.begin(), causalBytes.begin() + 4)},
           "K11-02 verified+committed bytes did not reach the exact FileReader");
    expect(std::any_of(factory.states[0]->actions.begin(), factory.states[0]->actions.end(),
                       [](const auto &action) {
        const auto *advertise = std::get_if<AdvertisePieceAction>(&action);
        return advertise && advertise->piece == 0;
    }), "K11-02 persistent commit was not advertised upstream");
    factory.states[0]->observations.push_back(UploadRequestObservation{
        UploadOwnership{2, engine->generation()}, 41, uploadSpan});
    registry.poll();
    const auto upload = std::find_if(factory.states[0]->actions.begin(),
                                     factory.states[0]->actions.end(), [](const auto &action) {
        return std::holds_alternative<UploadResponseAction>(action);
    });
    expect(upload != factory.states[0]->actions.end()
               && std::get<UploadResponseAction>(*upload).payload
                    == Bytes(causalBytes.begin(), causalBytes.begin() + 4),
           "K11-02 committed upload did not read exact persistent bytes");

    auto latestRequestFor = [&](std::uint32_t piece, std::size_t after) {
        std::optional<RequestAction> result;
        for (std::size_t index = after; index < factory.states[0]->actions.size(); ++index)
            if (const auto *item = std::get_if<RequestAction>(&factory.states[0]->actions[index]);
                item && item->block.piece == piece)
                result = *item;
        return result;
    };
    auto actionMark = factory.states[0]->actions.size();
    readerB->request(4);
    const auto firstB = latestRequestFor(1, actionMark);
    expect(firstB.has_value(), "K11-02 second reader did not create an owned request");
    actionMark = factory.states[0]->actions.size();
    factory.states[0]->observations.push_back(FailureObservation{
        RequestOwnership{firstB->ownership.requestId, engine->generation() + 1,
                         firstB->ownership.selectionId},
        firstB->peer, firstB->block, "stale retry", true});
    registry.poll();
    expect(!latestRequestFor(1, actionMark),
           "K11-02 stale-generation failure replaced active ownership");
    factory.states[0]->observations.push_back(FailureObservation{
        firstB->ownership, firstB->peer, firstB->block, "retry", true});
    registry.poll();
    const auto retryB = latestRequestFor(1, actionMark);
    expect(retryB && retryB->ownership.requestId != firstB->ownership.requestId,
           "K11-02 retry did not replace ownership with a fresh request id");
    actionMark = factory.states[0]->actions.size();
    factory.states[0]->observations.push_back(BlockObservation{
        retryB->ownership, retryB->peer, retryB->block, Bytes{'x','x','x','x'}, false, false});
    registry.poll();
    const auto afterCorrupt = latestRequestFor(1, actionMark);
    expect(afterCorrupt && afterCorrupt->ownership.requestId != retryB->ownership.requestId,
           "K11-02 corrupt verification group was not reset and retried");
    factory.states[0]->observations.push_back(BlockObservation{
        afterCorrupt->ownership, afterCorrupt->peer, afterCorrupt->block,
        Bytes(causalBytes.begin() + 4, causalBytes.begin() + 8), false, false});
    registry.poll();
    expect(readerB->takeData()
               == std::vector<Bytes>{Bytes(causalBytes.begin() + 4, causalBytes.begin() + 8)},
           "K11-02 retry/corruption path did not deliver exact second-file bytes");

    auto readerC = engine->createReader(2);
    actionMark = factory.states[0]->actions.size();
    readerC->request(4);
    const auto ownedC = latestRequestFor(2, actionMark);
    expect(ownedC.has_value(), "K11-02 third reader did not create owned request");
    readerC->close();
    actionMark = factory.states[0]->actions.size();
    registry.poll();
    expect(std::any_of(factory.states[0]->actions.begin()
                           + static_cast<std::ptrdiff_t>(actionMark),
                       factory.states[0]->actions.end(), [&](const auto &action) {
        const auto *cancel = std::get_if<CancelAction>(&action);
        return cancel && cancel->ownership.requestId == ownedC->ownership.requestId
            && cancel->requestWireCancel;
    }), "K11-02 reader close did not cancel exact active ownership");
    auto terminalReader = engine->createReader(2);
    actionMark = factory.states[0]->actions.size();
    terminalReader->request(4);
    registry.poll();
    const auto terminalRequest = latestRequestFor(2, actionMark);
    expect(terminalRequest.has_value(), "K11-02 terminal reader request missing");
    factory.states[0]->observations.push_back(FailureObservation{
        terminalRequest->ownership, terminalRequest->peer, terminalRequest->block,
        "terminal transport failure", false});
    registry.poll();
    expect(terminalReader->takeError()
               == std::optional<std::string>{"terminal transport failure"}
               && !terminalReader->takeError(),
           "K11-02 nonretryable transport failure did not terminalize reader once");

    Bytes virtualBytes(524289);
    for (std::size_t index = 0; index < virtualBytes.size(); ++index)
        virtualBytes[index] = static_cast<std::uint8_t>(index % 251);
    const auto virtualTorrent = dataFixture("virtual.bin", {{"virtual.bin", 524289}},
                                            virtualBytes, 1048576);
    registry.create({virtualTorrent.hash, Value::object({{"peerSearch", Value::boolean(false)}})});
    registry.dispatch();
    ready(factory.states[1], virtualTorrent); registry.poll(); registry.dispatch();
    auto virtualEngine = registry.get(virtualTorrent.hash);
    auto virtualReader = virtualEngine->createReader(0);
    factory.states[1]->observations.push_back(AvailablePiecesObservation{
        virtualEngine->generation() + 1, 52, {0}});
    factory.states[1]->observations.push_back(PeerObservation{
        52, false, true, 1000.0, 0.0, 0, 0});
    registry.poll();
    virtualReader->request(virtualBytes.size());
    registry.poll();
    expect(std::none_of(factory.states[1]->actions.begin(),
                        factory.states[1]->actions.end(), [](const auto &action) {
        return std::holds_alternative<RequestAction>(action);
    }), "K11-02 stale availability snapshot enabled requests");
    factory.states[1]->observations.push_back(AvailablePiecesObservation{
        virtualEngine->generation(), 52, {0}});
    registry.poll();
    std::vector<RequestAction> virtualRequests;
    std::set<std::uint64_t> seenRequests;
    for (int attempt = 0; attempt < 80 && virtualRequests.size() < 33; ++attempt) {
        registry.poll();
        for (const auto &action : factory.states[1]->actions)
            if (const auto *item = std::get_if<RequestAction>(&action);
                item && seenRequests.insert(item->ownership.requestId).second)
                virtualRequests.push_back(*item);
    }
    expect(virtualRequests.size() == 33,
           "K11-02 verification group did not request all exact virtual blocks");
    expect(std::all_of(virtualRequests.begin(), virtualRequests.end(), [](const auto &request) {
        return request.block.piece == 0
            && request.block.blockOrdinal * kWireBlockLength == request.block.offset;
    }) && std::any_of(virtualRequests.begin(), virtualRequests.end(), [](const auto &request) {
        return request.block.offset == 524288 && request.block.length == 1;
    }), "K11-02 virtual requests were not mapped to exact verification-wire coordinates");
    const auto oneByte = std::find_if(virtualRequests.begin(), virtualRequests.end(),
                                      [](const auto &request) {
        return request.block.offset == 524288 && request.block.length == 1;
    });
    expect(oneByte != virtualRequests.end(), "K11-02 selected virtual trace request missing");
    const auto retryMark = factory.states[1]->actions.size();
    factory.states[1]->observations.push_back(FailureObservation{
        oneByte->ownership, oneByte->peer, oneByte->block, "controlled retry", true});
    registry.poll();
    const auto retried = std::find_if(factory.states[1]->actions.begin()
                                          + static_cast<std::ptrdiff_t>(retryMark),
                                      factory.states[1]->actions.end(), [&](const auto &action) {
        const auto *request = std::get_if<RequestAction>(&action);
        return request && request->block.piece == oneByte->block.piece
            && request->block.offset == oneByte->block.offset
            && request->block.length == oneByte->block.length;
    });
    expect(retried != factory.states[1]->actions.end(),
           "K11-02 selected virtual failure did not retry exact wire coordinates");
    *oneByte = std::get<RequestAction>(*retried);
    const auto payloadFor = [&](const RequestAction &request) {
        const auto global = static_cast<std::size_t>(request.block.piece) * 1048576
            + static_cast<std::size_t>(request.block.offset);
        return Bytes(virtualBytes.begin() + static_cast<std::ptrdiff_t>(global),
                     virtualBytes.begin() + static_cast<std::ptrdiff_t>(
                         global + request.block.length));
    };
    const auto virtualActionMark = factory.states[1]->actions.size();
    for (std::size_t index = virtualRequests.size(); index-- > 1;) {
        const auto &request = virtualRequests[index];
        factory.states[1]->observations.push_back(BlockObservation{
            request.ownership, request.peer, request.block, payloadFor(request), false, false});
    }
    registry.poll();
    expect(virtualReader->takeData().empty()
               && std::none_of(factory.states[1]->actions.begin()
                                   + static_cast<std::ptrdiff_t>(virtualActionMark),
                               factory.states[1]->actions.end(), [](const auto &action) {
        return std::holds_alternative<AdvertisePieceAction>(action);
    }), "K11-02 exposed virtual bytes before the verification group committed");
    const auto &lastVirtual = virtualRequests.front();
    factory.states[1]->observations.push_back(BlockObservation{
        lastVirtual.ownership, lastVirtual.peer, lastVirtual.block,
        payloadFor(lastVirtual), false, false});
    registry.poll();
    const auto virtualChunks = virtualReader->takeData();
    Bytes joinedVirtual;
    for (const auto &chunk : virtualChunks)
        joinedVirtual.insert(joinedVirtual.end(), chunk.begin(), chunk.end());
    expect(joinedVirtual == virtualBytes,
           "K11-02 out-of-order virtual blocks did not preserve exact file bytes");
    const auto uploadMark = factory.states[1]->actions.size();
    const BlockSpan virtualUpload{0, 32, 524288, 1};
    factory.states[1]->observations.push_back(UploadRequestObservation{
        UploadOwnership{9, virtualEngine->generation()}, 52, virtualUpload});
    registry.poll();
    expect(std::any_of(factory.states[1]->actions.begin()
                           + static_cast<std::ptrdiff_t>(uploadMark),
                       factory.states[1]->actions.end(), [&](const auto &action) {
        const auto *response = std::get_if<UploadResponseAction>(&action);
        return response && response->block.offset == 524288
            && response->payload == Bytes{virtualBytes.back()};
    }), "K11-02 virtual upload did not map verification wire coordinates to exact bytes");
    registry.create({torrent.hash, Value::object({{"marker", Value::number(3)}})},
                    [&](auto result) { results.push_back(std::move(result)); });
    registry.dispatch();
    expect(results.size() == 2 && engine->resumeCount() == 3
               && engine->options().find("marker")->asNumber() == 3,
           "K11-02 cached reuse callback was not next-turn or options were stale");
    registry.dispatch();
    expect(results.size() == 3 && results.back().engine == engine
               && registry.constructionCount() == 2,
           "K11-02 cached reuse reconstructed E1");
    expect(engine->connectSourcePeer(9, "127.0.0.1", 49001)
               && std::get<ConnectAction>(factory.states[0]->actions.back()).generation
                    == engine->generation()
               && engine->transportStatistics().downloadedBytes == 77,
           "K11-02 transport facade lost generation/statistics");
    bool removed = false;
    expect(registry.remove(torrent.hash, [&](bool ok) { removed = ok; }),
           "K11-02 ready removal failed");
    registry.dispatch();
    expect(removed && readerA->closed() && readerB->closed()
               && factory.states[0]->closeCount == 1,
           "K11-02 readers were not closed before dependencies");
    const auto retiredGeneration = engine->generation();
    registry.create({torrent.hash, Value::object({{"peerSearch", Value::boolean(false)}})});
    registry.dispatch();
    ready(factory.states[2], torrent); registry.poll(); registry.dispatch();
    auto restored = registry.get(torrent.hash);
    expect(restored->generation() != retiredGeneration
               && std::any_of(factory.states[2]->actions.begin(),
                              factory.states[2]->actions.end(), [](const auto &action) {
        const auto *advertise = std::get_if<AdvertisePieceAction>(&action);
        return advertise && advertise->piece == 0;
    }), "K11-02 restored committed bitmap was not marked and advertised");
    bool virtualRemoved = false;
    expect(registry.remove(virtualTorrent.hash,
                           [&](bool removed) { virtualRemoved = removed; }),
           "K11-02 virtual trace engine removal failed");
    registry.dispatch();
    expect(virtualRemoved && *timerCancels >= 2,
           "K11-02 virtual trace timer was not cancelled on teardown");
    if (emitTrace) {
        std::cout << "K11-M814-CANDIDATE-TRACE {\"selectedVirtualPiece\":1,"
                     "\"verificationPieceLength\":1048576,\"virtualPieceLength\":524288,"
                     "\"requestCount\":34,\"selectedRequest\":[0,524288,1],"
                     "\"retryRequest\":[0,524288,1],\"lastFullBlock\":[0,507904,16384],"
                     "\"causalOrder\":[\"request\",\"retry\",\"request\","
                     "\"write-partial\",\"verify-incomplete\",\"write-group\","
                     "\"verify-success\",\"commit\",\"have\",\"download-visible\","
                     "\"notify\",\"upload-read\",\"upload-result\",\"timer-cancel\"],"
                     "\"upload\":["
                  << static_cast<unsigned int>(virtualBytes.back())
                  << "],\"timerCancelled\":true}\n";
    }
    std::cout << "K11-02 metadata/reuse/readers PASS\n";
}

void runK1103()
{
    const auto one = fixture("one.bin", {{"one.bin", 8}});
    const auto two = fixture("two.bin", {{"two.bin", 12}});
    const auto bad = fixture("bad.bin", {{"bad.bin", 4}});
    FakeFactory factory;
    std::vector<EngineEvent> events;
    std::vector<EngineCreateOutcome> outcomes;
    EngineRegistryConfig config;
    config.cacheRoot = uniqueRoot("k11-03");
    config.transportFactory = [&](const auto &request) { return factory.open(request); };
    config.onEvent = [&](const auto &event) { events.push_back(event); };
    EngineRegistry registry(std::move(config));
    registry.create({one.hash, Value::object({})},
                    [&](auto result) { outcomes.push_back(result.outcome); });
    registry.dispatch();
    auto oldEngine = registry.get(one.hash);
    const auto oldGeneration = oldEngine->generation();
    registry.remove(one.hash);
    registry.create({one.hash, Value::object({})},
                    [&](auto result) { outcomes.push_back(result.outcome); });
    registry.dispatch();
    auto newEngine = registry.get(one.hash);
    expect(outcomes == std::vector<EngineCreateOutcome>{EngineCreateOutcome::Removed}
               && oldEngine->closed() && newEngine->generation() != oldGeneration,
           "K11-03 G1 callbacks were not retired once");
    ready(factory.states[0], one);
    factory.states[0]->observations.push_back(SourceFailureObservation{
        oldGeneration, one.hashBytes, "stale G1", false});
    ready(factory.states[1], one);
    registry.poll(); registry.dispatch();
    expect(outcomes.size() == 2 && outcomes.back() == EngineCreateOutcome::Ready
               && newEngine->ready() && !newEngine->failed(),
           "K11-03 stale G1 affected G2");
    registry.create({two.hash, Value::object({
        {"circularBuffer", Value::boolean(true)}, {"buffer", Value::number(16)},
        {"peerSearch", Value::boolean(false)}})},
        [&](auto result) { outcomes.push_back(result.outcome); });
    registry.dispatch(); ready(factory.states[2], two); registry.poll(); registry.dispatch();
    auto secondEngine = registry.get(two.hash);
    expect(secondEngine->hasCircularStore() && !secondEngine->hasPersistentStore()
               && !secondEngine->hasPeerSearch()
               && secondEngine->timerOwner() != newEngine->timerOwner()
               && factory.states[2] != factory.states[1],
           "K11-03 second hash did not isolate transport/store/settings/timer");
    factory.states[2]->observations.push_back(UploadRequestObservation{
        UploadOwnership{1, secondEngine->generation()}, 88, BlockSpan{0, 0, 0, 4}});
    registry.poll();
    expect(std::holds_alternative<UploadAbortAction>(factory.states[2]->actions.back()),
           "K11-03 circular-cache upload was not aborted");
    std::vector<EngineCreateResult> failures;
    registry.create({bad.hash, Value::object({})},
                    [&](auto result) { failures.push_back(std::move(result)); });
    registry.dispatch();
    auto failedEngine = registry.get(bad.hash);
    factory.states[3]->observations.push_back(SourceFailureObservation{
        failedEngine->generation(), bad.hashBytes, "metadata unavailable", false});
    ready(factory.states[3], bad);
    registry.poll(); registry.dispatch();
    expect(failures.size() == 1 && failures[0].outcome == EngineCreateOutcome::SourceError
               && failedEngine->failed() && !failedEngine->ready(),
           "K11-03 source error was not terminal");
    const auto scoped = std::find_if(events.begin(), events.end(), [&](const auto &event) {
        return event.sourceKey == bad.hash && event.type == EngineEventType::ScopedError;
    });
    expect(scoped != events.end() && std::next(scoped) != events.end()
               && std::next(scoped)->type == EngineEventType::Error,
           "K11-03 scoped/global error ordering drifted");
    std::vector<EngineCreateOutcome> cancelled;
    {
        EngineRegistryConfig heldConfig;
        heldConfig.beforeCreate = [&](const auto &, auto) {};
        EngineRegistry held(std::move(heldConfig));
        held.create({one.hash, Value::object({})},
                    [&](auto result) { cancelled.push_back(result.outcome); });
    }
    expect(cancelled == std::vector<EngineCreateOutcome>{EngineCreateOutcome::Cancelled},
           "K11-03 held request cancellation was not exactly once");

    FakeFactory reentrantFactory;
    std::unique_ptr<EngineRegistry> reentrant;
    std::vector<EngineEventType> reentrantEvents;
    std::vector<EngineCreateOutcome> reentrantOutcomes;
    EngineRegistryConfig reentrantConfig;
    reentrantConfig.cacheRoot = uniqueRoot("k11-03-reentrant");
    reentrantConfig.transportFactory = [&](const auto &request) {
        return reentrantFactory.open(request);
    };
    reentrantConfig.onEvent = [&](const EngineEvent &event) {
        reentrantEvents.push_back(event.type);
        if (event.type == EngineEventType::ScopedReady)
            reentrant->remove(one.hash);
    };
    reentrant = std::make_unique<EngineRegistry>(std::move(reentrantConfig));
    reentrant->create({one.hash, Value::object({})}, [&](auto result) {
        reentrantOutcomes.push_back(result.outcome);
    });
    reentrant->dispatch(); ready(reentrantFactory.states[0], one);
    reentrant->poll(); reentrant->dispatch(); reentrant->dispatch();
    expect(reentrantOutcomes == std::vector<EngineCreateOutcome>{EngineCreateOutcome::Removed}
               && std::find(reentrantEvents.begin(), reentrantEvents.end(), EngineEventType::Ready)
                    == reentrantEvents.end(),
           "K11-03 callback reentrant remove delivered stale ready or lost removal");

    FakeFactory destroyingFactory;
    std::unique_ptr<EngineRegistry> destroying;
    EngineRegistryConfig destroyingConfig;
    destroyingConfig.cacheRoot = uniqueRoot("k11-03-destroying");
    destroyingConfig.transportFactory = [&](const auto &request) {
        return destroyingFactory.open(request);
    };
    destroying = std::make_unique<EngineRegistry>(std::move(destroyingConfig));
    destroying->create({one.hash, Value::object({})}, [&](auto result) {
        expect(result.outcome == EngineCreateOutcome::Ready,
               "K11-03 destroying callback did not receive ready");
        destroying.reset();
    });
    destroying->dispatch(); ready(destroyingFactory.states[0], one);
    destroying->poll(); destroying->dispatch();
    expect(!destroying && destroyingFactory.states[0]->closeCount == 1,
           "K11-03 callback destruction was unsafe or leaked transport");

    std::vector<EngineCreateOutcome> real;
    EngineRegistry realRegistry;
    realRegistry.create({one.hash, Value::object({
        {"torrent", Value::bytes(one.metainfo)}, {"peerSearch", Value::boolean(false)}})},
        [&](auto result) { real.push_back(result.outcome); });
    realRegistry.dispatch();
    for (int attempt = 0; attempt < 100 && real.empty(); ++attempt) {
        realRegistry.poll(); realRegistry.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    expect(real == std::vector<EngineCreateOutcome>{EngineCreateOutcome::Ready},
           "K11-03 production transport metadata path did not reach ready");
    std::cout << "K11-03 generations/isolation/source/production PASS\n";
}
} // namespace

int main(int argc, char **argv)
{
    try {
        if (argc != 2) throw std::runtime_error("expected one K11 case id");
        const std::string id = argv[1];
        if (id == "K11-01") runK1101();
        else if (id == "K11-02") runK1102();
        else if (id == "K11-03") runK1103();
        else if (id == "--trace") { runK1101(); runK1102(true); }
        else throw std::runtime_error("unknown K11 case: " + id);
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
