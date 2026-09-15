#include "server1/policy/EngineRegistry.h"
#include "server1/policy/TorrentMetadata.h"

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

struct FakeState final {
    explicit FakeState(const TorrentOpenRequest &opened)
        : request(opened), constructedOn(std::this_thread::get_id()) {}
    TorrentOpenRequest request;
    std::vector<TorrentObservation> observations;
    std::vector<TorrentAction> actions;
    std::thread::id constructedOn;
    std::size_t closeCount = 0;
    std::size_t autonomyCount = 0;
};

class FakeTransport final : public TorrentTransport {
public:
    explicit FakeTransport(std::shared_ptr<FakeState> state) : state_(std::move(state)) {}
    bool submit(const TorrentAction &action) override
    { state_->actions.push_back(action); return true; }
    std::vector<TorrentObservation> poll() override
    { std::vector<TorrentObservation> result; result.swap(state_->observations); return result; }
    TransportStatistics statistics() const override
    { TransportStatistics result; result.downloadedBytes = 77; return result; }
    void close() override { ++state_->closeCount; }
protected:
    bool applyAutonomySuppression() override
    { ++state_->autonomyCount; return true; }
private:
    std::shared_ptr<FakeState> state_;
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
    FakeFactory factory;
    const auto appThread = std::this_thread::get_id();
    EngineRegistryConfig config;
    config.cacheRoot = std::filesystem::temp_directory_path() / "server1-k11-01";
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
        {"path", Value::boolean(false)},
        {"peerSearch", Value::object({{"sources", Value::array({Value::string("dht:custom")})}})}})});
    const auto t2 = registry.create({mixed, Value::object({})});
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
    std::cout << "K11-01 registry/hooks/options PASS\n";
}

void runK1102()
{
    const auto torrent = fixture("root", {{"a.bin", 4}, {"b.bin", 4}, {"c.bin", 4}});
    FakeFactory factory;
    std::vector<EngineEvent> events;
    std::vector<EngineCreateResult> results;
    EngineRegistryConfig config;
    config.cacheRoot = std::filesystem::temp_directory_path() / "server1-k11-02";
    config.transportFactory = [&](const auto &request) { return factory.open(request); };
    config.onEvent = [&](const auto &event) { events.push_back(event); };
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
    registry.create({torrent.hash, Value::object({{"marker", Value::number(3)}})},
                    [&](auto result) { results.push_back(std::move(result)); });
    registry.dispatch();
    expect(results.size() == 2 && engine->resumeCount() == 3
               && engine->options().find("marker")->asNumber() == 3,
           "K11-02 cached reuse callback was not next-turn or options were stale");
    registry.dispatch();
    expect(results.size() == 3 && results.back().engine == engine
               && registry.constructionCount() == 1,
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
    config.cacheRoot = std::filesystem::temp_directory_path() / "server1-k11-03";
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
        else throw std::runtime_error("unknown K11 case: " + id);
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
