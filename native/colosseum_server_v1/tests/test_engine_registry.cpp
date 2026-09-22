#include "server1/policy/EngineRegistry.h"
#include "server1/policy/TorrentMetadata.h"

#include <QCryptographicHash>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
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
    std::vector<std::thread::id> submitThreads;
    std::thread::id constructedOn;
    std::thread::id closedOn;
    std::size_t closeCount = 0;
    std::size_t autonomyCount = 0;
    TransportStatistics statistics{};
    mutable std::mutex mutex;
};

class FakeTransport final : public TorrentTransport {
public:
    explicit FakeTransport(std::shared_ptr<FakeState> state) : state_(std::move(state)) {}
    bool submit(const TorrentAction &action) override
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->actions.push_back(action);
        state_->submitThreads.push_back(std::this_thread::get_id());
        return true;
    }
    std::vector<TorrentObservation> poll() override
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        std::vector<TorrentObservation> result;
        result.swap(state_->observations);
        return result;
    }
    TransportStatistics statistics() const override
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        auto result = state_->statistics;
        result.downloadedBytes = 77;
        return result;
    }
    void close() override
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        ++state_->closeCount;
        state_->closedOn = std::this_thread::get_id();
    }
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
    mutable std::mutex mutex;
    std::unique_ptr<TorrentTransport> open(const TorrentOpenRequest &request)
    {
        auto state = std::make_shared<FakeState>(request);
        {
            std::lock_guard<std::mutex> lock(mutex);
            states.push_back(state);
        }
        return std::make_unique<FakeTransport>(state);
    }
    [[nodiscard]] std::size_t count() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return states.size();
    }
    [[nodiscard]] std::shared_ptr<FakeState> at(std::size_t index) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return states.at(index);
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

// A test-owned background work lane. fence() holds queued tasks so the test
// can prove that no peer or disk work runs anywhere else while it is closed.
class LaneWorker final {
public:
    LaneWorker() : thread_([this] { run(); }) {}
    ~LaneWorker()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
            open_ = true;
        }
        wake_.notify_all();
        thread_.join();
    }
    bool post(EngineContinuation task)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_) return false;
            tasks_.push_back(std::move(task));
        }
        wake_.notify_all();
        return true;
    }
    void fence()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        open_ = false;
    }
    void release()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            open_ = true;
        }
        wake_.notify_all();
    }
    bool waitIdle(std::chrono::milliseconds timeout = std::chrono::milliseconds(10000))
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return idle_.wait_for(lock, timeout, [this] {
            return (!open_ || tasks_.empty()) && !running_;
        });
    }
    [[nodiscard]] std::size_t queued() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.size();
    }
    [[nodiscard]] std::thread::id id() const { return thread_.get_id(); }

private:
    void run()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while (true) {
            wake_.wait(lock, [this] { return stop_ || (open_ && !tasks_.empty()); });
            if (tasks_.empty()) {
                if (stop_) break;
                continue;
            }
            auto task = std::move(tasks_.front());
            tasks_.pop_front();
            running_ = true;
            lock.unlock();
            if (task) task();
            task = nullptr;
            lock.lock();
            running_ = false;
            idle_.notify_all();
        }
        idle_.notify_all();
    }

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::deque<EngineContinuation> tasks_;
    bool open_ = true;
    bool stop_ = false;
    bool running_ = false;
    std::thread thread_;
};

// A test-owned app lane: callbackExecutor posts here and only drain() runs work.
struct AppQueue final {
    std::mutex mutex;
    std::deque<EngineContinuation> tasks;
    bool post(EngineContinuation task)
    {
        std::lock_guard<std::mutex> lock(mutex);
        tasks.push_back(std::move(task));
        return true;
    }
    std::size_t drain()
    {
        std::size_t total = 0;
        while (true) {
            std::deque<EngineContinuation> batch;
            {
                std::lock_guard<std::mutex> lock(mutex);
                batch.swap(tasks);
            }
            if (batch.empty()) return total;
            for (auto &task : batch) {
                ++total;
                if (task) task();
            }
        }
    }
};

void settle(EngineRegistry &registry, LaneWorker &worker, int rounds = 6)
{
    for (int round = 0; round < rounds; ++round) {
        registry.poll();
        expect(worker.waitIdle(), "work lane did not become idle");
        registry.dispatch();
    }
}

std::chrono::milliseconds elapsedSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
}

template <typename Action>
std::size_t countActions(const std::shared_ptr<FakeState> &state)
{
    std::lock_guard<std::mutex> lock(state->mutex);
    return static_cast<std::size_t>(std::count_if(state->actions.begin(), state->actions.end(),
        [](const auto &action) { return std::holds_alternative<Action>(action); }));
}

template <typename Action>
std::vector<Action> actionsOf(const std::shared_ptr<FakeState> &state, std::size_t from = 0)
{
    std::lock_guard<std::mutex> lock(state->mutex);
    std::vector<Action> result;
    for (std::size_t index = from; index < state->actions.size(); ++index)
        if (const auto *item = std::get_if<Action>(&state->actions[index])) result.push_back(*item);
    return result;
}

void observe(const std::shared_ptr<FakeState> &state, TorrentObservation observation)
{
    std::lock_guard<std::mutex> lock(state->mutex);
    state->observations.push_back(std::move(observation));
}

// Finding 1: peer and disk work must never run on the app lane. The work lane is
// fenced; any metadata install, restore scan, verify/commit or read that still
// completes during poll()/dispatch() ran on the app lane.
void regressionAppLane()
{
    const Bytes payload{'l', 'a', 'n', 'e', 'L', 'A', 'N', 'E'};
    const auto torrent = dataFixture("lane.bin", {{"lane.bin", 8}}, payload);
    LaneWorker worker;
    FakeFactory factory;
    const auto app = std::this_thread::get_id();
    std::atomic_bool factoryOnApp{false};
    std::mutex latchMutex;
    std::condition_variable latchWake;
    bool latchOpen = false;
    EngineRegistryConfig config;
    config.cacheRoot = uniqueRoot("k11-lane");
    config.workExecutor = [&](EngineContinuation task) { return worker.post(std::move(task)); };
    config.transportFactory = [&](const TorrentOpenRequest &request) {
        if (std::this_thread::get_id() == app) factoryOnApp = true;
        std::unique_lock<std::mutex> lock(latchMutex);
        latchWake.wait_for(lock, std::chrono::seconds(3), [&] { return latchOpen; });
        return factory.open(request);
    };
    std::vector<EngineCreateOutcome> outcomes;
    EngineRegistry registry(std::move(config));
    auto start = std::chrono::steady_clock::now();
    registry.create({torrent.hash, Value::object({{"peerSearch", Value::boolean(false)}})},
                    [&](auto result) { outcomes.push_back(result.outcome); });
    registry.dispatch();
    registry.poll();
    registry.dispatch();
    expect(elapsedSince(start) < std::chrono::milliseconds(1000),
           "K11-LANE slow transport open stalled create/poll/dispatch on the app lane");
    {
        std::lock_guard<std::mutex> lock(latchMutex);
        latchOpen = true;
    }
    latchWake.notify_all();
    settle(registry, worker);
    expect(!factoryOnApp && factory.count() == 1,
           "K11-LANE transport open ran on the app lane");
    const auto engine = registry.get(torrent.hash);
    expect(engine && !engine->ready(), "K11-LANE engine was not registered before metadata");
    const auto state = factory.at(0);

    worker.fence();
    ready(state, torrent);
    start = std::chrono::steady_clock::now();
    registry.poll();
    registry.dispatch();
    registry.poll();
    registry.dispatch();
    expect(elapsedSince(start) < std::chrono::milliseconds(1000),
           "K11-LANE fenced metadata install stalled the app lane");
    expect(!engine->ready() && outcomes.empty(),
           "K11-LANE metadata install/restore scan ran on the app lane while the work lane was fenced");
    worker.release();
    settle(registry, worker);
    expect(engine->ready() && outcomes == std::vector<EngineCreateOutcome>{EngineCreateOutcome::Ready},
           "K11-LANE metadata install did not complete through the work lane");

    auto reader = engine->createReader(0);
    observe(state, AvailablePiecesObservation{engine->generation(), 7, {0, 1}});
    observe(state, PeerObservation{7, false, true, 1000.0, 0.0, 0, 0});
    reader->request(8);
    settle(registry, worker);
    const auto requests = actionsOf<RequestAction>(state);
    const auto first = std::find_if(requests.begin(), requests.end(),
                                    [](const auto &item) { return item.block.piece == 0; });
    expect(first != requests.end(), "K11-LANE reader demand did not reach a RequestAction");

    worker.fence();
    observe(state, BlockObservation{first->ownership, first->peer, first->block,
                                    Bytes(payload.begin(), payload.begin() + 4), false, false});
    start = std::chrono::steady_clock::now();
    registry.poll();
    registry.dispatch();
    registry.poll();
    registry.dispatch();
    expect(elapsedSince(start) < std::chrono::milliseconds(1000),
           "K11-LANE fenced verify/commit stalled the app lane");
    expect(countActions<AdvertisePieceAction>(state) == 0 && reader->takeData().empty(),
           "K11-LANE verification/disk commit ran on the app lane while the work lane was fenced");
    worker.release();
    settle(registry, worker);
    expect(countActions<AdvertisePieceAction>(state) == 1
               && reader->takeData() == std::vector<Bytes>{Bytes(payload.begin(), payload.begin() + 4)},
           "K11-LANE commit did not complete through the work lane");

    auto second = engine->createReader(0);
    worker.fence();
    second->request(4);
    registry.poll();
    registry.dispatch();
    expect(second->takeData().empty(),
           "K11-LANE committed cache read ran on the app lane while the work lane was fenced");
    worker.release();
    settle(registry, worker);
    expect(second->takeData() == std::vector<Bytes>{Bytes(payload.begin(), payload.begin() + 4)},
           "K11-LANE cache read did not complete through the work lane");

    worker.fence();
    observe(state, UploadRequestObservation{UploadOwnership{1, engine->generation()}, 7,
                                            BlockSpan{0, 0, 0, 4}});
    registry.poll();
    registry.dispatch();
    expect(countActions<UploadResponseAction>(state) == 0,
           "K11-LANE upload disk read ran on the app lane while the work lane was fenced");
    worker.release();
    settle(registry, worker);
    const auto uploads = actionsOf<UploadResponseAction>(state);
    expect(uploads.size() == 1 && uploads[0].payload == Bytes(payload.begin(), payload.begin() + 4),
           "K11-LANE committed upload did not complete through the work lane");
    std::cout << "K11-LANE app lane never ran peer or disk work PASS\n";
}

// Finding 2: terminal completions cross the declared callback boundary exactly
// once, never on a foreign destroying thread, and queued removals are not lost.
void regressionDestruction()
{
    const auto torrent = fixture("destroy.bin", {{"destroy.bin", 8}});
    const auto app = std::this_thread::get_id();
    AppQueue appLane;
    std::vector<std::pair<EngineCreateOutcome, std::thread::id>> held;
    {
        EngineRegistryConfig config;
        config.callbackExecutor = [&](EngineContinuation task) { return appLane.post(std::move(task)); };
        config.beforeCreate = [](const auto &, EngineContinuation) {};
        auto registry = std::make_unique<EngineRegistry>(std::move(config));
        registry->create({torrent.hash, Value::object({})}, [&](auto result) {
            held.push_back({result.outcome, std::this_thread::get_id()});
        });
        std::thread destroyer([&] { registry.reset(); });
        destroyer.join();
    }
    expect(held.empty(),
           "K11-DESTROY destruction invoked an unfinished create callback on the destroying thread");
    appLane.drain();
    appLane.drain();
    expect(held.size() == 1 && held[0].first == EngineCreateOutcome::Cancelled
               && held[0].second == app,
           "K11-DESTROY held create was not cancelled exactly once on the app lane");

    FakeFactory factory;
    LaneWorker worker;
    std::vector<EngineCreateOutcome> outcomes;
    std::vector<bool> removals;
    std::vector<std::thread::id> callbackThreads;
    {
        EngineRegistryConfig config;
        config.cacheRoot = uniqueRoot("k11-destroy");
        config.workExecutor = [&](EngineContinuation task) { return worker.post(std::move(task)); };
        config.callbackExecutor = [&](EngineContinuation task) { return appLane.post(std::move(task)); };
        config.transportFactory = [&](const auto &request) { return factory.open(request); };
        auto registry = std::make_unique<EngineRegistry>(std::move(config));
        registry->create({torrent.hash, Value::object({{"peerSearch", Value::boolean(false)}})},
                         [&](auto result) {
                             outcomes.push_back(result.outcome);
                             callbackThreads.push_back(std::this_thread::get_id());
                         });
        for (int round = 0; round < 6 && !registry->exists(torrent.hash); ++round) {
            registry->dispatch();
            worker.waitIdle();
            appLane.drain();
        }
        expect(registry->exists(torrent.hash), "K11-DESTROY engine was not created");
        for (int round = 0; round < 6 && factory.count() == 0; ++round) {
            worker.waitIdle();
            appLane.drain();
        }
        expect(factory.count() == 1, "K11-DESTROY transport was not opened on the work lane");
        registry->remove(torrent.hash, [&](bool removed) {
            removals.push_back(removed);
            callbackThreads.push_back(std::this_thread::get_id());
        });
        std::thread destroyer([&] { registry.reset(); });
        destroyer.join();
    }
    expect(outcomes.empty() && removals.empty(),
           "K11-DESTROY destruction ran terminal callbacks on the destroying thread");
    for (int round = 0; round < 6; ++round) {
        worker.waitIdle();
        appLane.drain();
    }
    expect(outcomes == std::vector<EngineCreateOutcome>{EngineCreateOutcome::Removed}
               && removals == std::vector<bool>{true},
           "K11-DESTROY queued removal completions were lost or rewritten by destruction");
    expect(std::all_of(callbackThreads.begin(), callbackThreads.end(),
                       [&](const auto &id) { return id == app; }),
           "K11-DESTROY terminal callback escaped the app lane");
    expect(factory.count() == 1 && factory.at(0)->closeCount == 1,
           "K11-DESTROY transport was not closed exactly once");

    // With the work lane fenced, the close completion cannot win the race:
    // finalize itself must deliver the removal decided before destruction.
    FakeFactory fencedFactory;
    LaneWorker fencedWorker;
    std::vector<EngineCreateOutcome> fencedOutcomes;
    std::vector<bool> fencedRemovals;
    {
        EngineRegistryConfig config;
        config.cacheRoot = uniqueRoot("k11-destroy-fenced");
        config.workExecutor = [&](EngineContinuation task) { return fencedWorker.post(std::move(task)); };
        config.callbackExecutor = [&](EngineContinuation task) { return appLane.post(std::move(task)); };
        config.transportFactory = [&](const auto &request) { return fencedFactory.open(request); };
        auto registry = std::make_unique<EngineRegistry>(std::move(config));
        registry->create({torrent.hash, Value::object({{"peerSearch", Value::boolean(false)}})},
                         [&](auto result) { fencedOutcomes.push_back(result.outcome); });
        for (int round = 0; round < 6 && fencedFactory.count() == 0; ++round) {
            fencedWorker.waitIdle();
            appLane.drain();
        }
        expect(fencedFactory.count() == 1, "K11-DESTROY fenced transport was not opened");
        fencedWorker.fence();
        registry->remove(torrent.hash, [&](bool removed) { fencedRemovals.push_back(removed); });
        std::thread destroyer([&] { registry.reset(); });
        destroyer.join();
    }
    appLane.drain();
    expect(fencedOutcomes == std::vector<EngineCreateOutcome>{EngineCreateOutcome::Removed}
               && fencedRemovals == std::vector<bool>{true},
           "K11-DESTROY finalize lost or rewrote a removal decided before destruction");
    fencedWorker.release();
    fencedWorker.waitIdle();
    appLane.drain();
    expect(fencedOutcomes.size() == 1 && fencedRemovals.size() == 1
               && fencedFactory.at(0)->closeCount == 1,
           "K11-DESTROY late close completion delivered a second terminal callback");

    // Remove/close races against fenced work: each terminates once, and no
    // late work-lane result reaches the closed generation.
    const Bytes payload{'r', 'a', 'c', 'e', 'R', 'A', 'C', 'E'};
    const auto raced = dataFixture("race.bin", {{"race.bin", 8}}, payload);
    FakeFactory raceFactory;
    LaneWorker raceWorker;
    std::vector<EngineEvent> raceEvents;
    EngineRegistryConfig raceConfig;
    raceConfig.cacheRoot = uniqueRoot("k11-race");
    raceConfig.workExecutor = [&](EngineContinuation task) { return raceWorker.post(std::move(task)); };
    raceConfig.transportFactory = [&](const auto &request) { return raceFactory.open(request); };
    raceConfig.onEvent = [&](const EngineEvent &event) { raceEvents.push_back(event); };
    EngineRegistry race(std::move(raceConfig));
    const Value quiet = Value::object({{"peerSearch", Value::boolean(false)}});

    std::vector<EngineCreateOutcome> beforeOpen;
    std::vector<bool> beforeOpenRemoved;
    raceWorker.fence();
    race.create({raced.hash, quiet}, [&](auto result) { beforeOpen.push_back(result.outcome); });
    race.dispatch();
    race.remove(raced.hash, [&](bool ok) { beforeOpenRemoved.push_back(ok); });
    raceWorker.release();
    settle(race, raceWorker);
    expect(raceFactory.count() == 0
               && beforeOpen == std::vector<EngineCreateOutcome>{EngineCreateOutcome::Removed}
               && beforeOpenRemoved == std::vector<bool>{true},
           "K11-RACE remove before transport open opened a transport or completed twice");

    std::vector<EngineCreateOutcome> duringInstall;
    race.create({raced.hash, quiet}, [&](auto result) { duringInstall.push_back(result.outcome); });
    settle(race, raceWorker);
    const auto installing = raceFactory.at(0);
    raceWorker.fence();
    ready(installing, raced);
    race.poll();
    race.dispatch();
    race.remove(raced.hash);
    raceWorker.release();
    settle(race, raceWorker);
    {
        std::lock_guard<std::mutex> lock(installing->mutex);
        expect(duringInstall == std::vector<EngineCreateOutcome>{EngineCreateOutcome::Removed}
                   && installing->closeCount == 1
                   && std::none_of(raceEvents.begin(), raceEvents.end(), [](const auto &event) {
                          return event.type == EngineEventType::ScopedReady
                              || event.type == EngineEventType::Ready;
                      }),
               "K11-RACE remove during metadata install delivered a stale ready");
    }

    std::vector<EngineCreateOutcome> duringCommit;
    race.create({raced.hash, quiet}, [&](auto result) { duringCommit.push_back(result.outcome); });
    settle(race, raceWorker);
    const auto committing = raceFactory.at(1);
    ready(committing, raced);
    settle(race, raceWorker);
    const auto engine = race.get(raced.hash);
    auto reader = engine->createReader(0);
    observe(committing, AvailablePiecesObservation{engine->generation(), 5, {0, 1}});
    observe(committing, PeerObservation{5, false, true, 100.0, 0.0, 0, 0});
    reader->request(8);
    settle(race, raceWorker);
    const auto request = actionsOf<RequestAction>(committing).front();
    raceWorker.fence();
    observe(committing, BlockObservation{request.ownership, request.peer, request.block,
                                         Bytes(payload.begin(), payload.begin() + 4), false, false});
    race.poll();
    race.dispatch();
    race.remove(raced.hash);
    raceWorker.release();
    settle(race, raceWorker);
    {
        std::lock_guard<std::mutex> lock(committing->mutex);
        const auto adverts = std::count_if(committing->actions.begin(), committing->actions.end(),
            [](const auto &action) { return std::holds_alternative<AdvertisePieceAction>(action); });
        expect(duringCommit == std::vector<EngineCreateOutcome>{EngineCreateOutcome::Ready}
                   && adverts == 0 && committing->closeCount == 1 && reader->closed()
                   && reader->takeData().empty(),
               "K11-RACE remove during an in-flight commit leaked a late result");
    }
    std::cout << "K11-DESTROY exactly-once terminal delivery PASS\n";
}

// Finding 3: the engine repeat timer is M814's 10 s rechoke interval, started
// by ontorrent, cancelled once, and inert after close.
void regressionTimer()
{
    const auto torrent = fixture("timer.bin", {{"timer.bin", 8}});
    FakeFactory factory;
    std::vector<std::uint64_t> intervals;
    EngineContinuation captured;
    auto cancels = std::make_shared<std::size_t>(0);
    std::shared_ptr<ProbeTimer> timer;
    EngineRegistryConfig config;
    config.cacheRoot = uniqueRoot("k11-timer");
    config.transportFactory = [&](const auto &request) { return factory.open(request); };
    config.repeat = [&](std::uint64_t interval, EngineContinuation callback) {
        intervals.push_back(interval);
        captured = callback;
        timer = std::make_shared<ProbeTimer>(4242, cancels, std::move(callback));
        return timer;
    };
    EngineRegistry registry(std::move(config));
    registry.create({torrent.hash, Value::object({{"peerSearch", Value::boolean(false)},
                                                  {"uploads", Value::number(1)}})});
    for (int round = 0; round < 20 && factory.count() == 0; ++round) {
        registry.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    registry.dispatch();
    expect(intervals.empty(), "K11-TIMER repeat timer started before ontorrent/metadata");
    const auto state = factory.at(0);
    ready(state, torrent);
    for (int round = 0; round < 40 && !registry.get(torrent.hash)->ready(); ++round) {
        registry.poll();
        registry.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    expect(intervals == std::vector<std::uint64_t>{10000},
           "K11-TIMER repeat interval drifted from the M814 10000 ms rechoke interval");
    const auto engine = registry.get(torrent.hash);
    observe(state, AvailablePiecesObservation{engine->generation(), 31, {0}});
    observe(state, PeerObservation{31, false, false, 10.0, 0.0, 0, 0});
    registry.poll();
    registry.dispatch();
    const auto before = countActions<ChokeAction>(state);
    timer->fire();
    for (int round = 0; round < 20 && countActions<ChokeAction>(state) == before; ++round) {
        registry.poll();
        registry.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto chokes = actionsOf<ChokeAction>(state);
    expect(chokes.size() == before + 1 && chokes.back().peer == 31 && !chokes.back().choked,
           "K11-TIMER tick did not translate the M814 rechoke unchoke");
    bool removed = false;
    registry.remove(torrent.hash, [&](bool ok) { removed = ok; });
    for (int round = 0; round < 40 && !removed; ++round) {
        registry.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    expect(removed && *cancels == 1, "K11-TIMER repeat timer was not cancelled exactly once");
    std::size_t actionsAfterClose = 0;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        actionsAfterClose = state->actions.size();
    }
    captured();
    captured();
    for (int round = 0; round < 5; ++round) {
        registry.poll();
        registry.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        expect(state->actions.size() == actionsAfterClose && *cancels == 1,
               "K11-TIMER post-close tick produced effects");
    }
    std::cout << "K11-TIMER M814 rechoke interval/cancel/post-close PASS\n";
}

// Finding 4: M612 "peer" results become generation-owned ConnectActions with
// cross-source deduplication, malformed-result rejection, min/max hysteresis,
// swarm pause/resume and stale-generation isolation.
void regressionPeerSearch()
{
    const auto torrent = fixture("peers.bin", {{"peers.bin", 8}});
    FakeFactory factory;
    LaneWorker worker;
    EngineRegistryConfig config;
    config.cacheRoot = uniqueRoot("k11-peers");
    config.workExecutor = [&](EngineContinuation task) { return worker.post(std::move(task)); };
    config.transportFactory = [&](const auto &request) { return factory.open(request); };
    std::uint64_t now = 1000;
    config.monotonicClock = [&] { return now; };
    EngineRegistry registry(std::move(config));
    const Value creation = Value::object({
        {"peerSearch", Value::object({{"min", Value::number(1)}, {"max", Value::number(2)},
                                      {"sources", Value::array({Value::string("tracker:udp://one"),
                                                                Value::string("dht:" + torrent.hash)})}})},
        {"swarmCap", Value::object({{"maxSpeed", Value::number(10)}, {"minPeers", Value::number(0)}})}});
    registry.create({torrent.hash, creation});
    settle(registry, worker);
    const auto engine = registry.get(torrent.hash);
    const auto state = factory.at(0);
    expect(engine->hasPeerSearch() && engine->peerSearchRunning(),
           "K11-PEERS PeerSearch was not constructed and running at isNew create");
    const auto stats = engine->peerSearchStats();
    expect(stats.size() == 2 && stats[0].url == "tracker:udp://one"
               && stats[1].url == "dht:" + torrent.hash,
           "K11-PEERS configured sources were not used for a non-parsed torrent");

    expect(engine->discoverPeer(0, "10.0.0.1:6881") && engine->discoverPeer(1, "10.0.0.1:6881"),
           "K11-PEERS source peer result was not accepted");
    for (const auto *malformed : {"bad", "10.0.0.2:0", "10.0.0.3:70000", "999.1.1.1:5",
                                  "10.0.0.4:5x", ":6881", "10.0.0.5:"})
        engine->discoverPeer(0, malformed);
    settle(registry, worker);
    auto connects = actionsOf<ConnectAction>(state);
    expect(connects.size() == 1 && connects[0].address == "10.0.0.1" && connects[0].port == 6881
               && connects[0].generation == engine->generation() && connects[0].peer != 0,
           "K11-PEERS valid discovered peer did not become one generation-owned ConnectAction");
    const auto discovery = engine->peerDiscovery();
    // M612 counts every source result; uniqueness spans sources. The engine, not
    // PeerSearch, rejects malformed results before they reach the transport.
    const auto counted = engine->peerSearchStats();
    expect(discovery.malformedRejected == 7 && discovery.connectsSubmitted == 1
               && discovery.duplicatesSuppressed == 0
               && counted[0].numFound == 8 && counted[0].numFoundUniq == 8
               && counted[1].numFound == 1 && counted[1].numFoundUniq == 0,
           "K11-PEERS deduplication or malformed-result rejection drifted");
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        expect(std::all_of(state->submitThreads.begin(), state->submitThreads.end(),
                           [&](const auto &id) { return id == worker.id(); }),
               "K11-PEERS blocking ConnectAction submission ran on the app lane");
    }

    for (const auto *address : {"10.0.0.2:1", "10.0.0.3:1", "10.0.0.4:1"})
        engine->discoverPeer(0, address);
    settle(registry, worker);
    connects = actionsOf<ConnectAction>(state);
    expect(connects.size() == 4 && engine->peerDiscovery().queued == 4,
           "K11-PEERS queued peer accounting drifted");
    observe(state, PeerObservation{connects[0].peer, true, false, 0.0, 0.0, 0, 0});
    settle(registry, worker);
    expect(engine->peerDiscovery().queued == 3 && !engine->peerSearchRunning(),
           "K11-PEERS queued above max did not pause search on a wire event");
    for (std::size_t index = 1; index < connects.size(); ++index)
        observe(state, PeerObservation{connects[index].peer, true, false, 0.0, 0.0, 0, 0});
    settle(registry, worker);
    expect(engine->peerDiscovery().queued == 0 && engine->peerSearchRunning(),
           "K11-PEERS queued below min did not resume search");

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->statistics.unchokedPeers = 1;
        state->statistics.downloadBytesPerSecond = 100.0;
    }
    // A state change on an existing wire is not a source "wire" event.
    observe(state, PeerObservation{connects[0].peer, false, false, 100.0, 0.0, 0, 0});
    settle(registry, worker);
    expect(actionsOf<PauseAction>(state).empty() && engine->peerSearchRunning(),
           "K11-PEERS swarm cap reacted to a non-wire peer state change");
    engine->discoverPeer(0, "10.0.0.6:1");
    settle(registry, worker);
    const auto sixth = actionsOf<ConnectAction>(state).back();
    observe(state, PeerObservation{sixth.peer, false, false, 100.0, 0.0, 0, 0});
    settle(registry, worker);
    const auto pauses = actionsOf<PauseAction>(state);
    expect(!pauses.empty() && pauses.back().paused && !engine->peerSearchRunning(),
           "K11-PEERS swarm-cap pause did not pause peer search");
    registry.create({torrent.hash, Value::object({})});
    settle(registry, worker);
    const auto resumed = actionsOf<PauseAction>(state);
    expect(!resumed.empty() && !resumed.back().paused && engine->peerSearchRunning(),
           "K11-PEERS create/resume did not resume the swarm and peer search");

    worker.fence();
    engine->discoverPeer(0, "10.0.0.9:9");
    registry.poll();
    registry.dispatch();
    const auto g1 = engine->generation();
    const auto g1Connects = actionsOf<ConnectAction>(state).size();
    registry.remove(torrent.hash);
    registry.create({torrent.hash, creation});
    registry.dispatch();
    worker.release();
    settle(registry, worker);
    const auto replacement = registry.get(torrent.hash);
    expect(replacement && replacement->generation() != g1 && factory.count() == 2,
           "K11-PEERS remove/recreate did not produce a new generation");
    expect(actionsOf<ConnectAction>(state).size() == g1Connects && !engine->peerSearchRunning()
               && !engine->discoverPeer(0, "10.0.0.10:10"),
           "K11-PEERS closed generation still connected or discovered peers");
    const auto g2 = factory.at(1);
    expect(actionsOf<ConnectAction>(g2).empty(),
           "K11-PEERS stale generation peer leaked into the replacement generation");
    expect(replacement->discoverPeer(0, "10.0.0.9:9"), "K11-PEERS replacement rejected a peer");
    settle(registry, worker);
    const auto g2Connects = actionsOf<ConnectAction>(g2);
    expect(g2Connects.size() == 1 && g2Connects[0].generation == replacement->generation(),
           "K11-PEERS replacement generation did not own its discovered peer");

    // Without a swarm cap only M612's own "wire" listener runs; hysteresis
    // must still follow wire events.
    const auto plain = fixture("plain-peers.bin", {{"plain-peers.bin", 8}});
    registry.create({plain.hash, Value::object({
        {"peerSearch", Value::object({{"min", Value::number(1)}, {"max", Value::number(1)},
                                      {"sources", Value::array({Value::string("dht:plain")})}})}})});
    settle(registry, worker);
    const auto plainEngine = registry.get(plain.hash);
    const auto plainState = factory.at(2);
    for (const auto *address : {"10.1.0.1:1", "10.1.0.2:1", "10.1.0.3:1"})
        plainEngine->discoverPeer(0, address);
    settle(registry, worker);
    const auto plainConnects = actionsOf<ConnectAction>(plainState);
    expect(plainConnects.size() == 3 && plainEngine->peerSearchRunning(),
           "K11-PEERS plain engine did not connect its discovered peers");
    observe(plainState, PeerObservation{plainConnects[0].peer, true, false, 0.0, 0.0, 0, 0});
    settle(registry, worker);
    expect(plainEngine->peerDiscovery().queued == 2 && !plainEngine->peerSearchRunning()
               && actionsOf<PauseAction>(plainState).empty(),
           "K11-PEERS wire event without a swarm cap did not apply max hysteresis");
    std::cout << "K11-PEERS discovery to ConnectAction PASS\n";
}

struct TraceLog final {
    std::mutex mutex;
    std::vector<EngineTrace> entries;
    EngineTraceCallback sink()
    {
        return [this](const EngineTrace &trace) {
            std::lock_guard<std::mutex> lock(mutex);
            entries.push_back(trace);
        };
    }
    std::vector<EngineTrace> snapshot()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return entries;
    }
};

std::string jsonQuote(const std::string &value)
{
    std::string result = "\"";
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') {
            result.push_back('\\');
            result.push_back(static_cast<char>(c));
        } else if (c < 0x20) {
            static constexpr char digits[] = "0123456789abcdef";
            result += "\\u00";
            result.push_back(digits[c >> 4U]);
            result.push_back(digits[c & 0x0fU]);
        } else {
            result.push_back(static_cast<char>(c));
        }
    }
    return result + "\"";
}

std::string joinJson(const std::vector<std::string> &items)
{
    std::string result = "[";
    for (std::size_t index = 0; index < items.size(); ++index)
        result += (index ? "," : "") + items[index];
    return result + "]";
}

// Raw artifacts never carry the machine-local cache root.
std::string scrubRoot(std::string text, const std::filesystem::path &root)
{
    const auto replaceAll = [&text](const std::string &from, const std::string &to) {
        if (from.empty()) return;
        for (auto at = text.find(from); at != std::string::npos;
             at = text.find(from, at + to.size()))
            text.replace(at, from.size(), to);
    };
    const auto native = root.string();
    auto escaped = native;
    for (auto at = escaped.find('\\'); at != std::string::npos; at = escaped.find('\\', at + 2))
        escaped.replace(at, 1, "\\\\");
    replaceAll(escaped, "<cache-root>");
    replaceAll(native, "<cache-root>");
    replaceAll(root.generic_string(), "<cache-root>");
    return text;
}

std::string upperHex(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

// M172 translation scenario. Every value in the returned raw log is observed
// from the running registry: events, callbacks, emitted options, engine ids,
// resume count, peer-search sources and constructions. run_oracle.js normalizes
// it with the same rules it applies to the executed source factory.
std::string runM172Scenario()
{
    const auto torrent = fixture("one.bin", {{"one.bin", 8}});
    const auto root = uniqueRoot("k11-m172");
    LaneWorker worker;
    FakeFactory factory;
    const auto app = std::this_thread::get_id();
    std::vector<EngineCreateRequest> hooks;
    std::vector<EngineContinuation> holds;
    std::vector<std::string> timeline;
    std::vector<std::string> createOptions;
    std::atomic_bool openedOnApp{false};
    EngineRegistryConfig config;
    config.cacheRoot = root;
    config.workExecutor = [&](EngineContinuation task) { return worker.post(std::move(task)); };
    config.beforeCreate = [&](const EngineCreateRequest &request, EngineContinuation continuation) {
        hooks.push_back(request);
        holds.push_back(std::move(continuation));
    };
    config.onEvent = [&](const EngineEvent &event) {
        expect(std::this_thread::get_id() == app, "K11-01 event escaped the app lane");
        switch (event.type) {
        case EngineEventType::Create: {
            const auto *marker = event.options.find("marker");
            timeline.push_back("[\"Create\","
                               + std::to_string(marker ? static_cast<int>(marker->asNumber()) : 0)
                               + "]");
            createOptions.push_back(jsonStringify(event.options));
            break;
        }
        case EngineEventType::Created: timeline.push_back("[\"Created\"]"); break;
        case EngineEventType::ScopedReady: timeline.push_back("[\"ScopedReady\"]"); break;
        case EngineEventType::Ready: timeline.push_back("[\"Ready\"]"); break;
        case EngineEventType::ScopedError: timeline.push_back("[\"ScopedError\"]"); break;
        case EngineEventType::Error: timeline.push_back("[\"Error\"]"); break;
        case EngineEventType::Destroyed: timeline.push_back("[\"Destroyed\"]"); break;
        }
    };
    config.transportFactory = [&](const TorrentOpenRequest &request) {
        if (std::this_thread::get_id() == app) openedOnApp = true;
        return factory.open(request);
    };
    EngineRegistry registry(std::move(config));
    const auto callback = [&](std::string name) {
        return [&timeline, app, name](EngineCreateResult) {
            expect(std::this_thread::get_id() == app, "K11-01 callback escaped the app lane");
            timeline.push_back("[\"callback\"," + jsonQuote(name) + "]");
        };
    };
    const auto mixed = upperHex(torrent.hash);
    const auto stream = "magnet:?xt=urn:btih:" + mixed;
    const auto t1 = registry.create({mixed, Value::object({
        {"stream", Value::string(stream)}, {"marker", Value::number(1)},
        {"path", Value::boolean(false)},
        {"peerSearch", Value::object({{"sources", Value::array({Value::string("dht:custom")})}})}})},
        callback("C1"));
    const auto t2 = registry.create({mixed, Value::object({{"marker", Value::number(2)}})},
                                    callback("C2"));
    expect(t1 && t2 && t1 != t2, "K11-01 create ids are not fresh");
    expect(hooks.size() == 2 && hooks[0].sourceKey == torrent.hash
               && hooks[0].options.find("torrent")
               && hooks[0].options.find("torrent")->asString() == stream,
           "K11-01 canonicalization/stream alias did not precede both hooks");
    std::thread foreign([&] { holds[1](); holds[1](); });
    foreign.join();
    expect(factory.count() == 0, "K11-01 held hook constructed before the next turn");
    settle(registry, worker);
    const auto engine = registry.get(mixed);
    expect(engine && engine == registry.get(torrent.hash) && factory.count() == 1 && !openedOnApp,
           "K11-01 first continuation did not open one transport on the work lane");
    std::vector<std::string> ids{engine->options().find("id")->asString()};
    holds[0]();
    holds[0]();
    settle(registry, worker);
    ids.push_back(engine->options().find("id")->asString());
    expect(registry.constructionCount() == 1 && engine->resumeCount() == 2,
           "K11-01 reordered/reentrant continuation reconstructed E1");
    expect(factory.at(0)->autonomyCount == 1, "K11-01 transport autonomy was not suppressed");
    const auto options = engine->options();
    expect(options.find("path") && options.find("path")->kind() == Value::Kind::String
               && options.find("peerSearch") && !options.find("peerSearch")->find("min"),
           "K11-01 falsy path fallback or shallow merge drifted");
    ready(factory.at(0), torrent);
    settle(registry, worker);

    const auto beforeCached = timeline.size();
    registry.create({torrent.hash, Value::object({{"marker", Value::number(3)},
                                                  {"path", Value::string("")}})},
                    callback("C3"));
    holds[2]();
    registry.dispatch();
    expect(timeline.size() == beforeCached,
           "K11-01 cached ready create delivered before the next turn");
    settle(registry, worker);
    ids.push_back(engine->options().find("id")->asString());
    const auto resumeCount = engine->resumeCount();
    const auto finalOptions = jsonStringify(engine->options());
    std::vector<std::string> peerSources;
    for (const auto &source : engine->peerSearchStats()) peerSources.push_back(jsonQuote(source.url));

    registry.remove(torrent.hash, [&](bool) { timeline.push_back("[\"remove-callback\"]"); });
    settle(registry, worker);
    {
        const auto first = factory.at(0);
        std::lock_guard<std::mutex> lock(first->mutex);
        expect(first->closeCount == 1 && first->closedOn == worker.id(),
               "K11-01 transport close did not run once on the work lane");
    }
    registry.create({torrent.hash, Value::object({{"marker", Value::number(4)}})}, callback("C4"));
    holds[3]();
    settle(registry, worker);
    const auto replacement = registry.get(torrent.hash);
    expect(replacement && replacement != engine && replacement->generation() != engine->generation(),
           "K11-01 remove/recreate did not create a new generation");
    ids.push_back(replacement->options().find("id")->asString());

    std::vector<std::string> idJson;
    for (const auto &id : ids) idJson.push_back(jsonQuote(id));
    const std::string raw = "{\"hash\":" + jsonQuote(torrent.hash)
        + ",\"stream\":" + jsonQuote(stream)
        + ",\"cacheRoot\":\"<cache-root>\""
        + ",\"constructions\":" + std::to_string(registry.constructionCount())
        + ",\"resumeCount\":" + std::to_string(resumeCount)
        + ",\"timeline\":" + joinJson(timeline)
        + ",\"createOptions\":" + joinJson(createOptions)
        + ",\"ids\":" + joinJson(idJson)
        + ",\"finalOptions\":" + finalOptions
        + ",\"peerSearchSources\":" + joinJson(peerSources) + "}";
    return scrubRoot(raw, root);
}

// M814 full-engine scenario, mirroring the controlled source harness: one
// 524289-byte file, one 1 MiB verification piece, two 512 KiB virtual pieces.
// The raw log is built only from observed transport actions, engine traces,
// reader deliveries, events and timer calls.
std::string runM814Scenario()
{
    constexpr std::size_t length = 524289;
    constexpr std::size_t verificationLength = 1048576;
    Bytes payload(length, 65);
    payload.back() = 200;
    const auto torrent = dataFixture("payload.bin", {{"payload.bin", 524289}}, payload,
                                     verificationLength);
    const auto parsed = TorrentMetadata::parse(torrent.metainfo, nullptr);
    expect(parsed.has_value(), "K11-02 M814 fixture did not parse");
    const auto root = uniqueRoot("k11-m814");
    LaneWorker worker;
    FakeFactory factory;
    TraceLog traces;
    std::vector<std::uint64_t> intervals;
    EngineContinuation timerCallback;
    auto cancels = std::make_shared<std::size_t>(0);
    std::shared_ptr<ProbeTimer> timer;
    std::vector<std::string> events;
    EngineRegistryConfig config;
    config.cacheRoot = root;
    config.workExecutor = [&](EngineContinuation task) { return worker.post(std::move(task)); };
    config.transportFactory = [&](const auto &request) { return factory.open(request); };
    config.trace = traces.sink();
    config.repeat = [&](std::uint64_t interval, EngineContinuation callback) {
        intervals.push_back(interval);
        timerCallback = callback;
        timer = std::make_shared<ProbeTimer>(814, cancels, std::move(callback));
        return timer;
    };
    config.onEvent = [&](const EngineEvent &event) {
        if (event.type == EngineEventType::Destroyed) events.push_back("\"Destroyed\"");
    };
    EngineRegistry registry(std::move(config));
    registry.create({torrent.hash, Value::object({{"peerSearch", Value::boolean(false)},
                                                  {"uploads", Value::number(1)}})});
    settle(registry, worker);
    const auto state = factory.at(0);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->statistics.unchokedPeers = 1;
    }
    ready(state, torrent);
    settle(registry, worker);
    const auto engine = registry.get(torrent.hash);
    expect(engine && engine->ready(), "K11-02 M814 engine did not become ready");
    auto reader = engine->createReader(0);
    observe(state, AvailablePiecesObservation{engine->generation(), 52, {0}});
    observe(state, PeerObservation{52, false, false, 1.0, 0.0, 0, 0});
    reader->request(length);

    std::vector<std::string> responses;
    Bytes delivered;
    std::size_t answered = 0;
    for (int round = 0; round < 200; ++round) {
        settle(registry, worker, 2);
        for (const auto &chunk : reader->takeData())
            delivered.insert(delivered.end(), chunk.begin(), chunk.end());
        const auto requests = actionsOf<RequestAction>(state);
        if (answered == requests.size() && delivered.size() == length) break;
        for (; answered < requests.size(); ++answered) {
            const auto &request = requests[answered];
            if (answered == 0) {
                observe(state, FailureObservation{request.ownership, request.peer, request.block,
                                                  "controlled retry", true});
                responses.push_back("\"fail\"");
                continue;
            }
            const auto global = static_cast<std::size_t>(request.block.piece) * verificationLength
                + request.block.offset;
            observe(state, BlockObservation{request.ownership, request.peer, request.block,
                Bytes(payload.begin() + static_cast<std::ptrdiff_t>(global),
                      payload.begin() + static_cast<std::ptrdiff_t>(global + request.block.length)),
                false, false});
            responses.push_back("\"block\"");
        }
    }
    expect(delivered == payload, "K11-02 M814 reader did not receive exact committed bytes");
    timer->fire();
    settle(registry, worker);
    observe(state, UploadRequestObservation{UploadOwnership{9, engine->generation()}, 52,
                                            BlockSpan{0, 32, 524288, 1}});
    settle(registry, worker);
    bool removed = false;
    registry.remove(torrent.hash, [&](bool ok) { removed = ok; });
    settle(registry, worker);
    expect(removed && *cancels == 1, "K11-02 M814 teardown did not cancel the timer once");
    timerCallback();
    timerCallback();
    settle(registry, worker);

    std::vector<std::string> requests;
    std::vector<std::string> actions;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        for (const auto &action : state->actions) {
            if (const auto *request = std::get_if<RequestAction>(&action)) {
                requests.push_back("[" + std::to_string(request->block.piece) + ","
                                   + std::to_string(request->block.offset) + ","
                                   + std::to_string(request->block.length) + ","
                                   + std::to_string(request->ownership.requestId) + "]");
            } else if (const auto *have = std::get_if<AdvertisePieceAction>(&action)) {
                actions.push_back("[\"have\"," + std::to_string(have->piece) + "]");
            } else if (const auto *choke = std::get_if<ChokeAction>(&action)) {
                actions.push_back(std::string("[\"") + (choke->choked ? "choke" : "unchoke")
                                  + "\"," + std::to_string(choke->peer) + "]");
            } else if (const auto *response = std::get_if<UploadResponseAction>(&action)) {
                std::vector<std::string> bytesJson;
                for (const auto byte : response->payload) bytesJson.push_back(std::to_string(byte));
                actions.push_back("[\"upload\"," + std::to_string(response->block.piece) + ","
                                  + std::to_string(response->block.offset) + ","
                                  + std::to_string(response->block.length) + ","
                                  + joinJson(bytesJson) + "]");
            } else if (const auto *abort = std::get_if<UploadAbortAction>(&action)) {
                actions.push_back("[\"upload-abort\"," + std::to_string(abort->block.piece) + "]");
            } else if (const auto *cancel = std::get_if<CancelAction>(&action)) {
                actions.push_back("[\"cancel\"," + std::to_string(cancel->block.offset) + "]");
            } else if (const auto *pause = std::get_if<PauseAction>(&action)) {
                actions.push_back(std::string("[\"") + (pause->paused ? "pause" : "resume") + "\"]");
            }
        }
        expect(state->closeCount == 1, "K11-02 M814 transport was not closed once");
    }
    std::vector<std::string> traceJson;
    for (const auto &entry : traces.snapshot()) {
        expect(entry.kind.rfind("timer", 0) == 0 || entry.thread == worker.id(),
               "K11-02 M814 work trace ran off the work lane: " + entry.kind);
        traceJson.push_back("[" + jsonQuote(entry.kind) + "," + std::to_string(entry.piece) + ","
                            + std::to_string(entry.start) + "," + std::to_string(entry.end) + ","
                            + std::to_string(entry.length) + "]");
    }
    std::vector<std::string> intervalJson;
    for (const auto interval : intervals) intervalJson.push_back(std::to_string(interval));
    const std::string raw = "{\"verificationPieceLength\":" + std::to_string(verificationLength)
        + ",\"virtualPieceLength\":" + std::to_string(parsed->geometry().virtualPieceLength())
        + ",\"length\":" + std::to_string(length)
        + ",\"requests\":" + joinJson(requests)
        + ",\"responses\":" + joinJson(responses)
        + ",\"actions\":" + joinJson(actions)
        + ",\"trace\":" + joinJson(traceJson)
        + ",\"readerBytes\":" + std::to_string(delivered.size())
        + ",\"events\":" + joinJson(events)
        + ",\"timer\":{\"intervals\":" + joinJson(intervalJson)
        + ",\"cancels\":" + std::to_string(*cancels)
        + ",\"postCloseInvocations\":2}}";
    return scrubRoot(raw, root);
}

// M172 binds the swarm-cap updater and PeerSearch once, from the creating call
// (isNew). A reuse overrides the effective options and resumes the swarm but
// does not rebind the updater.
void swarmCapCreationBinding()
{
    const auto torrent = fixture("cap.bin", {{"cap.bin", 8}});
    FakeFactory factory;
    LaneWorker worker;
    EngineRegistryConfig config;
    config.cacheRoot = uniqueRoot("k11-cap");
    config.workExecutor = [&](EngineContinuation task) { return worker.post(std::move(task)); };
    config.transportFactory = [&](const auto &request) { return factory.open(request); };
    EngineRegistry registry(std::move(config));
    registry.create({torrent.hash, Value::object({
        {"peerSearch", Value::boolean(false)},
        {"swarmCap", Value::object({{"maxSpeed", Value::number(10)},
                                    {"minPeers", Value::number(0)}})}})});
    settle(registry, worker);
    const auto engine = registry.get(torrent.hash);
    const auto state = factory.at(0);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->statistics.unchokedPeers = 2;
        state->statistics.downloadBytesPerSecond = 100.0;
    }
    observe(state, PeerObservation{61, false, false, 100.0, 0.0, 0, 0});
    settle(registry, worker);
    auto pauses = actionsOf<PauseAction>(state);
    expect(pauses.size() == 1 && pauses[0].paused && pauses[0].generation == engine->generation(),
           "K11-01 wire event did not apply the creation swarm cap");
    registry.create({torrent.hash, Value::object({
        {"marker", Value::number(7)},
        {"swarmCap", Value::object({{"maxSpeed", Value::number(1000)},
                                    {"minPeers", Value::number(0)}})}})});
    settle(registry, worker);
    pauses = actionsOf<PauseAction>(state);
    expect(pauses.size() == 2 && !pauses[1].paused
               && engine->options().find("marker")->asNumber() == 7,
           "K11-01 reuse did not resume the swarm or override effective options");
    observe(state, PeerObservation{62, false, false, 100.0, 0.0, 0, 0});
    settle(registry, worker);
    pauses = actionsOf<PauseAction>(state);
    expect(pauses.size() == 3 && pauses[2].paused,
           "K11-01 reuse options rebound the creation-time swarm-cap updater");
}

void runK1101()
{
    static_cast<void>(runM172Scenario());
    const auto one = fixture("one.bin", {{"one.bin", 8}});

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
    hardenedRegistry.create({one.hash, Value::object({})}, [&](auto result) {
        hardenedResults.push_back(std::move(result));
    });
    expect(hardenedFactory.count() == 0 && hardenedResults.empty(),
           "K11-01 inline executor escaped the deferred boundary");
    hardenedRegistry.dispatch();
    expect(hardenedFactory.count() == 0 && hardenedRegistry.constructionCount() == 0
               && hardenedResults.size() == 1
               && hardenedResults[0].outcome == EngineCreateOutcome::SourceError
               && hardenedResults[0].error == "throw after continuation",
           "K11-01 continuation-then-throw constructed or did not fail exactly once");

    // An inline work executor is refused; peer work moves to the registry worker.
    FakeFactory inlineFactory;
    const auto app = std::this_thread::get_id();
    std::atomic_bool inlineOnApp{false};
    EngineRegistryConfig inlineWork;
    inlineWork.cacheRoot = uniqueRoot("k11-01-inline");
    inlineWork.workExecutor = [](EngineContinuation task) { task(); return true; };
    inlineWork.transportFactory = [&](const auto &request) {
        if (std::this_thread::get_id() == app) inlineOnApp = true;
        return inlineFactory.open(request);
    };
    EngineRegistry inlineRegistry(std::move(inlineWork));
    inlineRegistry.create({one.hash, Value::object({{"peerSearch", Value::boolean(false)}})});
    for (int round = 0; round < 500 && inlineFactory.count() == 0; ++round) {
        inlineRegistry.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    expect(inlineFactory.count() == 1 && !inlineOnApp,
           "K11-01 inline work executor ran transport open on the app lane");

    swarmCapCreationBinding();
    regressionDestruction();
    std::cout << "K11-01 registry/hooks/options PASS\n";
}

// Only durably committed persistent pieces are advertised or uploaded. The K06
// store can read staged bytes, so a staged-but-uncommitted virtual piece is the
// case that proves the guard.
void regressionCommittedOnly()
{
    constexpr std::size_t length = 524289;
    Bytes payload(length, 65);
    payload.back() = 200;
    const auto torrent = dataFixture("staged.bin", {{"staged.bin", 524289}}, payload, 1048576);
    LaneWorker worker;
    FakeFactory factory;
    EngineRegistryConfig config;
    config.cacheRoot = uniqueRoot("k11-staged");
    config.workExecutor = [&](EngineContinuation task) { return worker.post(std::move(task)); };
    config.transportFactory = [&](const auto &request) { return factory.open(request); };
    EngineRegistry registry(std::move(config));
    registry.create({torrent.hash, Value::object({{"peerSearch", Value::boolean(false)}})});
    settle(registry, worker);
    const auto state = factory.at(0);
    ready(state, torrent);
    settle(registry, worker);
    const auto engine = registry.get(torrent.hash);
    auto reader = engine->createReader(0);
    observe(state, AvailablePiecesObservation{engine->generation(), 71, {0}});
    observe(state, PeerObservation{71, false, true, 1.0, 0.0, 0, 0});
    reader->request(length);
    settle(registry, worker);
    auto requests = actionsOf<RequestAction>(state);
    expect(requests.size() == 1 && requests[0].block.offset == 524288,
           "K11-STAGED fresh wire did not request the last virtual piece first");
    observe(state, BlockObservation{requests[0].ownership, requests[0].peer, requests[0].block,
                                    Bytes{200}, false, false});
    settle(registry, worker);
    FileReadOptions tail;
    tail.start = 524288;
    auto tailReader = engine->createReader(0, tail);
    tailReader->request(1);
    settle(registry, worker);
    expect(tailReader->takeData().empty() && !tailReader->takeError(),
           "K11-STAGED a reader inside the staged piece read or failed before commit");
    const BlockSpan stagedBlock{0, 32, 524288, 1};
    observe(state, UploadRequestObservation{UploadOwnership{1, engine->generation()}, 71,
                                            stagedBlock});
    settle(registry, worker);
    expect(countActions<UploadResponseAction>(state) == 0
               && countActions<UploadAbortAction>(state) == 1
               && countActions<AdvertisePieceAction>(state) == 0 && reader->takeData().empty(),
           "K11-STAGED staged but uncommitted bytes were advertised, uploaded or read");
    requests = actionsOf<RequestAction>(state);
    for (std::size_t index = 1; index < requests.size(); ++index) {
        const auto &request = requests[index];
        observe(state, BlockObservation{request.ownership, request.peer, request.block,
            Bytes(payload.begin() + request.block.offset,
                  payload.begin() + request.block.offset + request.block.length), false, false});
    }
    settle(registry, worker);
    observe(state, UploadRequestObservation{UploadOwnership{2, engine->generation()}, 71,
                                            stagedBlock});
    settle(registry, worker);
    const auto uploads = actionsOf<UploadResponseAction>(state);
    expect(countActions<AdvertisePieceAction>(state) == 1 && uploads.size() == 1
               && uploads[0].payload == Bytes{200}
               && tailReader->takeData() == std::vector<Bytes>{Bytes{200}},
           "K11-STAGED committed group was not advertised, uploaded and read exactly");
    std::cout << "K11-STAGED committed-only advertise/upload PASS\n";
}

// A real verification piece is advertised or served only when every virtual
// component is durably committed. K06 restores virtual pieces one by one, so a
// truncated backing file can keep virtual piece 0 of a group and drop piece 1.
void regressionPartialRestore()
{
    constexpr std::size_t length = 524289;
    constexpr std::size_t virtualLength = 524288;
    Bytes payload(length, 65);
    payload.back() = 200;
    const auto torrent = dataFixture("partial.bin", {{"partial.bin", 524289}}, payload, 1048576);
    const auto root = uniqueRoot("k11-partial");
    LaneWorker worker;
    FakeFactory factory;
    EngineRegistryConfig config;
    config.cacheRoot = root;
    config.workExecutor = [&](EngineContinuation task) { return worker.post(std::move(task)); };
    config.transportFactory = [&](const auto &request) { return factory.open(request); };
    EngineRegistry registry(std::move(config));
    const Value quiet = Value::object({{"peerSearch", Value::boolean(false)}});
    const auto answerAll = [&](const std::shared_ptr<FakeState> &state, std::size_t &answered) {
        const auto requests = actionsOf<RequestAction>(state);
        for (; answered < requests.size(); ++answered) {
            const auto &request = requests[answered];
            observe(state, BlockObservation{request.ownership, request.peer, request.block,
                Bytes(payload.begin() + request.block.offset,
                      payload.begin() + request.block.offset + request.block.length),
                false, false});
        }
    };

    registry.create({torrent.hash, quiet});
    settle(registry, worker);
    const auto first = factory.at(0);
    ready(first, torrent);
    settle(registry, worker);
    auto engine = registry.get(torrent.hash);
    auto reader = engine->createReader(0);
    observe(first, AvailablePiecesObservation{engine->generation(), 81, {0}});
    observe(first, PeerObservation{81, false, true, 1.0, 0.0, 0, 0});
    reader->request(length);
    std::size_t answered = 0;
    Bytes delivered;
    for (int round = 0; round < 40 && delivered.size() < length; ++round) {
        settle(registry, worker, 2);
        answerAll(first, answered);
        for (const auto &chunk : reader->takeData())
            delivered.insert(delivered.end(), chunk.begin(), chunk.end());
    }
    expect(delivered == payload && countActions<AdvertisePieceAction>(first) == 1,
           "K11-PARTIAL initial group was not committed and advertised once");
    registry.remove(torrent.hash);
    settle(registry, worker);

    // Keep virtual piece 0 and invalidate virtual piece 1 of real piece 0.
    const auto backing = root / torrent.hash / "0";
    expect(std::filesystem::file_size(backing) == length,
           "K11-PARTIAL backing file was not written");
    std::filesystem::resize_file(backing, virtualLength);

    registry.create({torrent.hash, quiet});
    settle(registry, worker);
    const auto second = factory.at(1);
    ready(second, torrent);
    settle(registry, worker);
    engine = registry.get(torrent.hash);
    expect(engine && engine->ready(), "K11-PARTIAL recreated engine did not become ready");
    expect(countActions<AdvertisePieceAction>(second) == 0,
           "K11-PARTIAL advertised a real piece whose virtual component was not restored");

    FileReadOptions head;
    head.end = virtualLength - 1;
    auto headReader = engine->createReader(0, head);
    headReader->request(virtualLength);
    settle(registry, worker);
    Bytes headBytes;
    for (const auto &chunk : headReader->takeData())
        headBytes.insert(headBytes.end(), chunk.begin(), chunk.end());
    expect(headBytes == Bytes(payload.begin(), payload.begin() + virtualLength),
           "K11-PARTIAL restored virtual piece 0 was not individually readable");

    observe(second, AvailablePiecesObservation{engine->generation(), 82, {0}});
    observe(second, PeerObservation{82, false, true, 1.0, 0.0, 0, 0});
    observe(second, UploadRequestObservation{UploadOwnership{1, engine->generation()}, 82,
                                             BlockSpan{0, 0, 0, 16384}});
    settle(registry, worker);
    expect(countActions<UploadResponseAction>(second) == 0
               && countActions<UploadAbortAction>(second) == 1,
           "K11-PARTIAL served bytes of an incomplete real piece");

    FileReadOptions tail;
    tail.start = virtualLength;
    auto tailReader = engine->createReader(0, tail);
    tailReader->request(1);
    std::size_t reAnswered = 0;
    Bytes tailBytes;
    for (int round = 0; round < 40 && tailBytes.empty(); ++round) {
        settle(registry, worker, 2);
        answerAll(second, reAnswered);
        for (const auto &chunk : tailReader->takeData())
            tailBytes.insert(tailBytes.end(), chunk.begin(), chunk.end());
    }
    const auto requests = actionsOf<RequestAction>(second);
    expect(!requests.empty() && std::all_of(requests.begin(), requests.end(), [](const auto &item) {
               return item.block.offset == 524288 && item.block.length == 1;
           }),
           "K11-PARTIAL re-download requested more than the missing virtual component");
    expect(tailBytes == Bytes{200} && countActions<AdvertisePieceAction>(second) == 1,
           "K11-PARTIAL recommitted group did not become readable and advertised exactly once");
    observe(second, UploadRequestObservation{UploadOwnership{2, engine->generation()}, 82,
                                             BlockSpan{0, 0, 0, 16384}});
    observe(second, UploadRequestObservation{UploadOwnership{3, engine->generation()}, 82,
                                             BlockSpan{0, 32, 524288, 1}});
    settle(registry, worker);
    const auto uploads = actionsOf<UploadResponseAction>(second);
    expect(uploads.size() == 2 && uploads[0].payload == Bytes(16384, 65)
               && uploads[1].payload == Bytes{200}
               && countActions<AdvertisePieceAction>(second) == 1,
           "K11-PARTIAL recommitted real piece was not served exactly");

    // A surviving component whose durable bytes are corrupt fails the group
    // hash when restaged: the whole real piece is reset and fetched again.
    registry.remove(torrent.hash);
    settle(registry, worker);
    {
        std::fstream corrupt(backing, std::ios::binary | std::ios::in | std::ios::out);
        corrupt.seekp(0);
        corrupt.put('X');
    }
    std::filesystem::resize_file(backing, virtualLength);
    registry.create({torrent.hash, quiet});
    settle(registry, worker);
    const auto third = factory.at(2);
    ready(third, torrent);
    settle(registry, worker);
    engine = registry.get(torrent.hash);
    expect(countActions<AdvertisePieceAction>(third) == 0,
           "K11-PARTIAL advertised a partially restored real piece after corruption");
    observe(third, AvailablePiecesObservation{engine->generation(), 83, {0}});
    observe(third, PeerObservation{83, false, true, 1.0, 0.0, 0, 0});
    auto corruptTail = engine->createReader(0, tail);
    corruptTail->request(1);
    std::size_t thirdAnswered = 0;
    Bytes corruptTailBytes;
    for (int round = 0; round < 60 && corruptTailBytes.empty(); ++round) {
        settle(registry, worker, 2);
        answerAll(third, thirdAnswered);
        for (const auto &chunk : corruptTail->takeData())
            corruptTailBytes.insert(corruptTailBytes.end(), chunk.begin(), chunk.end());
    }
    const auto refetched = actionsOf<RequestAction>(third);
    expect(std::any_of(refetched.begin(), refetched.end(), [](const auto &item) {
               return item.block.offset == 0;
           }) && corruptTailBytes == Bytes{200}
               && countActions<AdvertisePieceAction>(third) == 1,
           "K11-PARTIAL corrupt survivor was not reset, refetched and advertised once: requests="
               + std::to_string(refetched.size()) + " tail=" + std::to_string(corruptTailBytes.size())
               + " have=" + std::to_string(countActions<AdvertisePieceAction>(third))
               + " error=" + corruptTail->takeError().value_or("none"));
    observe(third, UploadRequestObservation{UploadOwnership{1, engine->generation()}, 83,
                                            BlockSpan{0, 0, 0, 16384}});
    settle(registry, worker);
    const auto repaired = actionsOf<UploadResponseAction>(third);
    expect(repaired.size() == 1 && repaired[0].payload == Bytes(16384, 65),
           "K11-PARTIAL served corrupt survivor bytes after the group was repaired");
    std::cout << "K11-PARTIAL group-complete advertise/upload PASS\n";
}

void runK1102()
{
    const Bytes causalBytes{'A','B','C','D','E','F','G','H','I','J','K','L'};
    const auto torrent = dataFixture("root", {{"a.bin", 4}, {"b.bin", 4}, {"c.bin", 4}},
                                     causalBytes);
    FakeFactory factory;
    LaneWorker worker;
    std::vector<EngineEvent> events;
    std::vector<EngineCreateResult> results;
    EngineRegistryConfig config;
    config.cacheRoot = uniqueRoot("k11-02");
    config.workExecutor = [&](EngineContinuation task) { return worker.post(std::move(task)); };
    config.transportFactory = [&](const auto &request) { return factory.open(request); };
    config.onEvent = [&](const auto &event) { events.push_back(event); };
    auto timerCancels = std::make_shared<std::size_t>(0);
    std::uint64_t nextTimerId = 100;
    config.repeat = [&](std::uint64_t, EngineContinuation callback) {
        return std::make_shared<ProbeTimer>(++nextTimerId, timerCancels, std::move(callback));
    };
    EngineRegistry registry(std::move(config));
    registry.create({torrent.hash, Value::object({})},
                    [&](auto result) { results.push_back(std::move(result)); });
    registry.create({torrent.hash, Value::object({{"marker", Value::number(2)}})},
                    [&](auto result) { results.push_back(std::move(result)); });
    settle(registry, worker);
    expect(factory.count() == 1 && results.empty()
               && countEvents(events, EngineEventType::Created) == 1
               && countEvents(events, EngineEventType::ScopedReady) == 0,
           "K11-02 two premetadata creates did not share one nonblocking construction");
    const auto state = factory.at(0);
    ready(state, torrent);
    registry.poll();
    expect(results.empty(), "K11-02 poll bypassed dispatcher");
    settle(registry, worker);
    const auto created = std::find_if(events.begin(), events.end(), [](const auto &event) {
        return event.type == EngineEventType::Created;
    });
    const auto firstReady = std::find_if(events.begin(), events.end(), [](const auto &event) {
        return event.type == EngineEventType::ScopedReady;
    });
    expect(results.size() == 2 && results[0].engine == results[1].engine
               && countEvents(events, EngineEventType::ScopedReady) == 2
               && countEvents(events, EngineEventType::Ready) == 2 && created < firstReady,
           "K11-02 created-before-ready or callback/ready-pair fanout drifted");
    auto engine = results[0].engine;
    expect(engine->fileCount() == 3 && engine->hasPersistentStore()
               && engine->hasPeerSearch(),
           "K11-02 metadata components were not initialized once");
    auto readerA = engine->createReader(0);
    auto readerB = engine->createReader(1);
    expect(readerA != readerB && engine->readerCount() == 2
               && engine->selectionCount() == 2,
           "K11-02 two readers did not share E1 scheduler");

    observe(state, AvailablePiecesObservation{engine->generation(), 41, {0, 1, 2}});
    observe(state, PeerObservation{41, false, true, 1000.0, 0.0, 0, 0});
    const BlockSpan uploadSpan{0, 0, 0, 4};
    observe(state, UploadRequestObservation{UploadOwnership{1, engine->generation()}, 41,
                                            uploadSpan});
    settle(registry, worker);
    expect(countActions<UploadAbortAction>(state) == 1,
           "K11-02 precommit upload was not aborted");
    readerA->request(4);
    settle(registry, worker);
    const auto requestedList = actionsOf<RequestAction>(state);
    expect(!requestedList.empty(), "K11-02 FileReader demand did not reach K10 RequestAction");
    const auto requested = requestedList.front();
    expect(requested.block.piece == 0 && requested.block.offset == 0
               && requested.block.length == 4,
           "K11-02 scheduler request lost exact virtual/block coordinates: piece="
               + std::to_string(requested.block.piece) + " offset="
               + std::to_string(requested.block.offset) + " length="
               + std::to_string(requested.block.length));
    observe(state, BlockObservation{
        RequestOwnership{requested.ownership.requestId, engine->generation() + 1,
                         requested.ownership.selectionId},
        requested.peer, requested.block,
        Bytes(causalBytes.begin(), causalBytes.begin() + 4), false, false});
    settle(registry, worker);
    expect(readerA->takeData().empty(),
           "K11-02 stale-generation block mutated active ownership");
    observe(state, BlockObservation{requested.ownership, requested.peer, requested.block,
                                    Bytes(causalBytes.begin(), causalBytes.begin() + 4),
                                    false, false});
    settle(registry, worker);
    expect(readerA->takeData()
               == std::vector<Bytes>{Bytes(causalBytes.begin(), causalBytes.begin() + 4)},
           "K11-02 verified+committed bytes did not reach the exact FileReader");
    const auto advertised = actionsOf<AdvertisePieceAction>(state);
    expect(advertised.size() == 1 && advertised[0].piece == 0,
           "K11-02 persistent commit was not advertised upstream exactly once");
    observe(state, UploadRequestObservation{UploadOwnership{2, engine->generation()}, 41,
                                            uploadSpan});
    settle(registry, worker);
    const auto uploads = actionsOf<UploadResponseAction>(state);
    expect(uploads.size() == 1
               && uploads[0].payload == Bytes(causalBytes.begin(), causalBytes.begin() + 4),
           "K11-02 committed upload did not read exact persistent bytes");

    auto latestRequestFor = [&](std::uint32_t piece, std::size_t after) {
        std::optional<RequestAction> result;
        std::lock_guard<std::mutex> lock(state->mutex);
        for (std::size_t index = after; index < state->actions.size(); ++index)
            if (const auto *item = std::get_if<RequestAction>(&state->actions[index]);
                item && item->block.piece == piece)
                result = *item;
        return result;
    };
    auto mark = [&] {
        std::lock_guard<std::mutex> lock(state->mutex);
        return state->actions.size();
    };
    auto actionMark = mark();
    readerB->request(4);
    settle(registry, worker);
    const auto firstB = latestRequestFor(1, actionMark);
    expect(firstB.has_value(), "K11-02 second reader did not create an owned request");
    actionMark = mark();
    observe(state, FailureObservation{
        RequestOwnership{firstB->ownership.requestId, engine->generation() + 1,
                         firstB->ownership.selectionId},
        firstB->peer, firstB->block, "stale retry", true});
    settle(registry, worker);
    expect(!latestRequestFor(1, actionMark),
           "K11-02 stale-generation failure replaced active ownership");
    observe(state, FailureObservation{firstB->ownership, firstB->peer, firstB->block,
                                      "retry", true});
    settle(registry, worker);
    const auto retryB = latestRequestFor(1, actionMark);
    expect(retryB && retryB->ownership.requestId != firstB->ownership.requestId,
           "K11-02 retry did not replace ownership with a fresh request id");
    actionMark = mark();
    observe(state, BlockObservation{retryB->ownership, retryB->peer, retryB->block,
                                    Bytes{'x','x','x','x'}, false, false});
    settle(registry, worker);
    const auto afterCorrupt = latestRequestFor(1, actionMark);
    expect(afterCorrupt && afterCorrupt->ownership.requestId != retryB->ownership.requestId
               && readerB->takeData().empty() && countActions<AdvertisePieceAction>(state) == 1,
           "K11-02 corrupt verification group was not reset and retried before visibility");
    observe(state, BlockObservation{afterCorrupt->ownership, afterCorrupt->peer,
                                    afterCorrupt->block,
                                    Bytes(causalBytes.begin() + 4, causalBytes.begin() + 8),
                                    false, false});
    settle(registry, worker);
    expect(readerB->takeData()
               == std::vector<Bytes>{Bytes(causalBytes.begin() + 4, causalBytes.begin() + 8)},
           "K11-02 retry/corruption path did not deliver exact second-file bytes");

    auto readerC = engine->createReader(2);
    actionMark = mark();
    readerC->request(4);
    settle(registry, worker);
    const auto ownedC = latestRequestFor(2, actionMark);
    expect(ownedC.has_value(), "K11-02 third reader did not create owned request");
    readerC->close();
    actionMark = mark();
    settle(registry, worker);
    const auto cancels = actionsOf<CancelAction>(state, actionMark);
    expect(std::any_of(cancels.begin(), cancels.end(), [&](const auto &cancel) {
        return cancel.ownership.requestId == ownedC->ownership.requestId
            && cancel.requestWireCancel;
    }), "K11-02 reader close did not cancel exact active ownership");
    auto terminalReader = engine->createReader(2);
    actionMark = mark();
    terminalReader->request(4);
    settle(registry, worker);
    const auto terminalRequest = latestRequestFor(2, actionMark);
    expect(terminalRequest.has_value(), "K11-02 terminal reader request missing");
    observe(state, FailureObservation{terminalRequest->ownership, terminalRequest->peer,
                                      terminalRequest->block, "terminal transport failure",
                                      false});
    settle(registry, worker);
    expect(terminalReader->takeError()
               == std::optional<std::string>{"terminal transport failure"}
               && !terminalReader->takeError(),
           "K11-02 nonretryable transport failure did not terminalize reader once");

    registry.create({torrent.hash, Value::object({{"marker", Value::number(3)}})},
                    [&](auto result) { results.push_back(std::move(result)); });
    registry.dispatch();
    expect(results.size() == 2 && engine->resumeCount() == 3
               && engine->options().find("marker")->asNumber() == 3,
           "K11-02 cached reuse callback was not next-turn or options were stale");
    registry.dispatch();
    expect(results.size() == 3 && results.back().engine == engine
               && results.back().outcome == EngineCreateOutcome::Ready
               && registry.constructionCount() == 1,
           "K11-02 cached reuse reconstructed E1");
    expect(engine->connectSourcePeer(9, "127.0.0.1", 49001),
           "K11-02 source peer connect was not admitted");
    settle(registry, worker);
    const auto connects = actionsOf<ConnectAction>(state);
    expect(connects.size() == 1 && connects[0].generation == engine->generation()
               && connects[0].peer == 9 && engine->transportStatistics().downloadedBytes == 77,
           "K11-02 transport facade lost generation/statistics");
    bool removed = false;
    expect(registry.remove(torrent.hash, [&](bool ok) { removed = ok; }),
           "K11-02 ready removal failed");
    expect(readerA->closed() && readerB->closed(),
           "K11-02 readers were not closed before dependencies");
    settle(registry, worker);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        expect(removed && state->closeCount == 1 && *timerCancels == 1,
               "K11-02 removal did not close transport and timer exactly once");
    }
    const auto retiredGeneration = engine->generation();
    registry.create({torrent.hash, Value::object({{"peerSearch", Value::boolean(false)}})});
    settle(registry, worker);
    const auto second = factory.at(1);
    ready(second, torrent);
    settle(registry, worker);
    const auto restored = registry.get(torrent.hash);
    const auto restoredHave = actionsOf<AdvertisePieceAction>(second);
    expect(restored && restored->ready() && restored->generation() != retiredGeneration
               && std::any_of(restoredHave.begin(), restoredHave.end(),
                              [](const auto &have) { return have.piece == 0; }),
           "K11-02 restored committed bitmap was not marked and advertised");
    auto restoredReader = restored->createReader(0);
    restoredReader->request(4);
    settle(registry, worker);
    expect(restoredReader->takeData()
               == std::vector<Bytes>{Bytes(causalBytes.begin(), causalBytes.begin() + 4)},
           "K11-02 restored committed bytes were not readable");

    static_cast<void>(runM814Scenario());
    regressionAppLane();
    regressionCommittedOnly();
    regressionPartialRestore();
    std::cout << "K11-02 metadata/reuse/readers PASS\n";
}

template <typename Predicate>
void pumpUntil(EngineRegistry &registry, Predicate predicate, int rounds = 2000)
{
    for (int round = 0; round < rounds && !predicate(); ++round) {
        registry.poll();
        registry.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void runK1103()
{
    const auto one = fixture("one.bin", {{"one.bin", 8}});
    const auto two = fixture("two.bin", {{"two.bin", 12}});
    const auto bad = fixture("bad.bin", {{"bad.bin", 4}});
    FakeFactory factory;
    LaneWorker worker;
    std::vector<EngineEvent> events;
    std::vector<EngineCreateOutcome> outcomes;
    EngineRegistryConfig config;
    config.cacheRoot = uniqueRoot("k11-03");
    config.workExecutor = [&](EngineContinuation task) { return worker.post(std::move(task)); };
    config.transportFactory = [&](const auto &request) { return factory.open(request); };
    config.onEvent = [&](const auto &event) { events.push_back(event); };
    EngineRegistry registry(std::move(config));
    registry.create({one.hash, Value::object({})},
                    [&](auto result) { outcomes.push_back(result.outcome); });
    settle(registry, worker);
    auto oldEngine = registry.get(one.hash);
    const auto oldGeneration = oldEngine->generation();
    registry.remove(one.hash);
    registry.create({one.hash, Value::object({})},
                    [&](auto result) { outcomes.push_back(result.outcome); });
    settle(registry, worker);
    auto newEngine = registry.get(one.hash);
    expect(outcomes == std::vector<EngineCreateOutcome>{EngineCreateOutcome::Removed}
               && oldEngine->closed() && newEngine->generation() != oldGeneration,
           "K11-03 G1 callbacks were not retired once");
    ready(factory.at(0), one);
    observe(factory.at(0), SourceFailureObservation{oldGeneration, one.hashBytes, "stale G1", false});
    ready(factory.at(1), one);
    settle(registry, worker);
    expect(outcomes.size() == 2 && outcomes.back() == EngineCreateOutcome::Ready
               && newEngine->ready() && !newEngine->failed(),
           "K11-03 stale G1 affected G2");
    registry.create({two.hash, Value::object({
        {"circularBuffer", Value::boolean(true)}, {"buffer", Value::number(16)},
        {"peerSearch", Value::boolean(false)}})},
        [&](auto result) { outcomes.push_back(result.outcome); });
    settle(registry, worker);
    ready(factory.at(2), two);
    settle(registry, worker);
    auto secondEngine = registry.get(two.hash);
    auto shared = newEngine->createReader(0);
    expect(secondEngine->hasCircularStore() && !secondEngine->hasPersistentStore()
               && !secondEngine->hasPeerSearch() && newEngine->hasPeerSearch()
               && secondEngine->timerOwner() != 0
               && secondEngine->timerOwner() != newEngine->timerOwner()
               && newEngine->selectionCount() == 1 && secondEngine->selectionCount() == 0
               && factory.at(2) != factory.at(1),
           "K11-03 second hash did not isolate transport/store/settings/timer/selections");
    observe(factory.at(2), UploadRequestObservation{UploadOwnership{1, secondEngine->generation()},
                                                    88, BlockSpan{0, 0, 0, 4}});
    settle(registry, worker);
    expect(countActions<UploadAbortAction>(factory.at(2)) == 1
               && countActions<UploadResponseAction>(factory.at(2)) == 0,
           "K11-03 circular-cache upload was not aborted");
    shared->close();
    std::vector<EngineCreateResult> failures;
    registry.create({bad.hash, Value::object({})},
                    [&](auto result) { failures.push_back(std::move(result)); });
    settle(registry, worker);
    auto failedEngine = registry.get(bad.hash);
    observe(factory.at(3), SourceFailureObservation{
        failedEngine->generation(), bad.hashBytes, "metadata unavailable", false});
    ready(factory.at(3), bad);
    settle(registry, worker);
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

    // Preserved uncommitted retained-reader regression; only the work-lane
    // settle calls are new.
    FakeFactory retainedFactory;
    LaneWorker retainedWorker;
    EngineRegistryConfig retainedConfig;
    retainedConfig.cacheRoot = uniqueRoot("k11-03-retained-reader");
    retainedConfig.workExecutor = [&](EngineContinuation task) {
        return retainedWorker.post(std::move(task));
    };
    retainedConfig.transportFactory = [&](const auto &request) {
        return retainedFactory.open(request);
    };
    EngineRegistry retainedRegistry(std::move(retainedConfig));
    retainedRegistry.create({one.hash, Value::object({})});
    settle(retainedRegistry, retainedWorker);
    ready(retainedFactory.states[0], one);
    settle(retainedRegistry, retainedWorker);
    auto retainedEngine = retainedRegistry.get(one.hash);
    auto retainedReader = retainedEngine->createReader(0);
    expect(retainedReader->hasActiveSelection(),
           "K11-03 retained reader did not begin with an owned selection");
    retainedRegistry.remove(one.hash);
    settle(retainedRegistry, retainedWorker);
    expect(retainedReader->closed() && !retainedReader->hasActiveSelection(),
           "K11-03 retained reader reached scheduler ownership after engine close");
    retainedReader->request(4);
    expect(retainedReader->takeData().empty(),
           "K11-03 retained reader produced data after engine close");
    retainedReader->close();

    FakeFactory reentrantFactory;
    LaneWorker reentrantWorker;
    std::unique_ptr<EngineRegistry> reentrant;
    std::vector<EngineEventType> reentrantEvents;
    std::vector<EngineCreateOutcome> reentrantOutcomes;
    EngineRegistryConfig reentrantConfig;
    reentrantConfig.cacheRoot = uniqueRoot("k11-03-reentrant");
    reentrantConfig.workExecutor = [&](EngineContinuation task) {
        return reentrantWorker.post(std::move(task));
    };
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
    settle(*reentrant, reentrantWorker);
    ready(reentrantFactory.at(0), one);
    settle(*reentrant, reentrantWorker);
    expect(reentrantOutcomes == std::vector<EngineCreateOutcome>{EngineCreateOutcome::Removed}
               && std::find(reentrantEvents.begin(), reentrantEvents.end(), EngineEventType::Ready)
                    == reentrantEvents.end(),
           "K11-03 callback reentrant remove delivered stale ready or lost removal");

    FakeFactory destroyingFactory;
    LaneWorker destroyingWorker;
    std::unique_ptr<EngineRegistry> destroying;
    std::vector<EngineCreateOutcome> destroyingOutcomes;
    EngineRegistryConfig destroyingConfig;
    destroyingConfig.cacheRoot = uniqueRoot("k11-03-destroying");
    destroyingConfig.workExecutor = [&](EngineContinuation task) {
        return destroyingWorker.post(std::move(task));
    };
    destroyingConfig.transportFactory = [&](const auto &request) {
        return destroyingFactory.open(request);
    };
    destroying = std::make_unique<EngineRegistry>(std::move(destroyingConfig));
    destroying->create({one.hash, Value::object({})}, [&](auto result) {
        destroyingOutcomes.push_back(result.outcome);
        destroying.reset();
    });
    destroying->create({one.hash, Value::object({{"marker", Value::number(2)}})},
                       [&](auto result) { destroyingOutcomes.push_back(result.outcome); });
    settle(*destroying, destroyingWorker);
    ready(destroyingFactory.at(0), one);
    for (int round = 0; round < 10 && destroying; ++round) {
        destroying->poll();
        destroyingWorker.waitIdle();
        if (destroying) destroying->dispatch();
    }
    destroyingWorker.waitIdle();
    {
        const auto closedState = destroyingFactory.at(0);
        std::lock_guard<std::mutex> lock(closedState->mutex);
        expect(!destroying && closedState->closeCount == 1
                   && destroyingOutcomes == std::vector<EngineCreateOutcome>{
                          EngineCreateOutcome::Ready, EngineCreateOutcome::Cancelled},
               "K11-03 callback destruction was unsafe, leaked transport or lost a completion");
    }

    regressionTimer();
    regressionPeerSearch();

    LaneWorker realWorker;
    std::vector<EngineCreateOutcome> real;
    {
        EngineRegistryConfig realConfig;
        realConfig.cacheRoot = uniqueRoot("k11-03-real");
        realConfig.workExecutor = [&](EngineContinuation task) {
            return realWorker.post(std::move(task));
        };
        EngineRegistry realRegistry(std::move(realConfig));
        realRegistry.create({one.hash, Value::object({
            {"torrent", Value::bytes(one.metainfo)}, {"peerSearch", Value::boolean(false)}})},
            [&](auto result) { real.push_back(result.outcome); });
        pumpUntil(realRegistry, [&] { return !real.empty(); }, 5000);
        expect(real == std::vector<EngineCreateOutcome>{EngineCreateOutcome::Ready},
               "K11-03 production transport metadata path did not reach ready");
    }
    realWorker.waitIdle(std::chrono::milliseconds(30000));
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
        else if (id == "--trace") {
            std::cout << "K11-M172-NATIVE-RAW " << runM172Scenario() << '\n';
            std::cout << "K11-M814-NATIVE-RAW " << runM814Scenario() << '\n';
        }
        else if (id == "R1") regressionAppLane();
        else if (id == "R2") regressionDestruction();
        else if (id == "R3") regressionTimer();
        else if (id == "R4") regressionPeerSearch();
        else if (id == "R5") regressionCommittedOnly();
        else if (id == "R6") regressionPartialRestore();
        else throw std::runtime_error("unknown K11 case: " + id);
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
