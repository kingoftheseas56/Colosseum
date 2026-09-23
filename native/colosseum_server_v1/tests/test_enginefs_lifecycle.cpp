// K12-A: EngineFS counters, statistics, removal, keepConcurrency and
// guessed-file selection.
//
// Every scenario comes from artifacts/server1/K12/K12-A/cases/K12.json. The
// same file drives the real source in run_oracle.js. This binary either emits
// its outputs for that live comparison (--emit), or, as K12-01/02/03, compares
// them with the committed source outputs and runs native-only checks.

#include "server1/policy/EngineLifecycle.h"
#include "server1/policy/FileSelection.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace server1::policy;

namespace {

[[noreturn]] void fail(const std::string &message)
{
    std::cerr << "FAIL " << message << '\n';
    std::exit(1);
}

void expect(bool condition, const std::string &message)
{
    if (!condition)
        fail(message);
}

// ---- JSON input ----

// An order-preserving JSON reader: key order in cases and source outputs is
// significant.
class OrderedJson final {
public:
    explicit OrderedJson(std::string text) : text_(std::move(text)) {}
    Value parse()
    {
        skip();
        auto value = parseValue();
        skip();
        return value;
    }

private:
    void skip()
    {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_])))
            ++pos_;
    }
    Value parseValue()
    {
        skip();
        const char c = text_[pos_];
        if (c == '{') {
            ++pos_;
            Value::Object object;
            skip();
            if (text_[pos_] == '}') {
                ++pos_;
                return Value::object(std::move(object));
            }
            for (;;) {
                skip();
                auto key = parseString();
                skip();
                ++pos_; // ':'
                object.emplace_back(std::move(key), parseValue());
                skip();
                if (text_[pos_++] == '}')
                    return Value::object(std::move(object));
            }
        }
        if (c == '[') {
            ++pos_;
            Value::Array array;
            skip();
            if (text_[pos_] == ']') {
                ++pos_;
                return Value::array(std::move(array));
            }
            for (;;) {
                array.push_back(parseValue());
                skip();
                if (text_[pos_++] == ']')
                    return Value::array(std::move(array));
            }
        }
        if (c == '"')
            return Value::string(parseString());
        if (text_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            return Value::boolean(true);
        }
        if (text_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            return Value::boolean(false);
        }
        if (text_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return Value::null();
        }
        const auto start = pos_;
        while (pos_ < text_.size() && std::string("+-0123456789.eE").find(text_[pos_]) != std::string::npos)
            ++pos_;
        return Value::number(std::strtod(text_.substr(start, pos_ - start).c_str(), nullptr));
    }
    std::string parseString()
    {
        std::string out;
        ++pos_; // opening quote
        while (text_[pos_] != '"') {
            char c = text_[pos_++];
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            c = text_[pos_++];
            switch (c) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'u': {
                const auto code = std::stoul(text_.substr(pos_, 4), nullptr, 16);
                pos_ += 4;
                if (code < 0x80) {
                    out.push_back(static_cast<char>(code));
                } else if (code < 0x800) {
                    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                } else {
                    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
                break;
            }
            default: out.push_back(c);
            }
        }
        ++pos_;
        return out;
    }
    std::string text_;
    std::size_t pos_ = 0;
};

Value readOrderedJson(const std::string &path)
{
    std::ifstream input(path, std::ios::binary);
    expect(bool(input), "cannot read " + path);
    std::stringstream buffer;
    buffer << input.rdbuf();
    return OrderedJson(buffer.str()).parse();
}

const Value &field(const Value &object, const std::string &key)
{
    static const Value missing = Value::missing();
    const auto *found = object.find(key);
    return found ? *found : missing;
}

std::string str(const Value &value) { return value.asString(); }
double num(const Value &value) { return value.asNumber(); }

// ---- virtual clock, one-shot timers and the deferred-turn queue ----

class VirtualClock final {
public:
    class Timer final : public EngineTimer {
    public:
        Timer(VirtualClock *clock, std::uint64_t id) : clock_(clock), id_(id) {}
        void cancel() noexcept override
        {
            if (clock_)
                clock_->timers_.erase(id_);
            active_ = false;
        }
        [[nodiscard]] bool active() const noexcept override { return active_; }
        [[nodiscard]] std::uint64_t id() const noexcept override { return id_; }
        VirtualClock *clock_;
        std::uint64_t id_;
        bool active_ = true;
    };

    LifecycleTimeout timeout()
    {
        return [this](std::uint64_t delay, EngineContinuation continuation) {
            if (delay < 1 || delay > 2147483647)
                delay = 1;
            const auto id = ++seq_;
            auto timer = std::make_shared<Timer>(this, id);
            timers_[id] = {now + delay, std::move(continuation), timer};
            return timer;
        };
    }

    void defer(EngineContinuation continuation) { queue_.push_back(std::move(continuation)); }

    void flush()
    {
        while (!queue_.empty()) {
            auto next = std::move(queue_.front());
            queue_.pop_front();
            next();
        }
    }

    void advance(std::uint64_t milliseconds)
    {
        const auto target = now + milliseconds;
        for (;;) {
            auto next = timers_.end();
            for (auto it = timers_.begin(); it != timers_.end(); ++it)
                if (it->second.at <= target
                    && (next == timers_.end() || it->second.at < next->second.at))
                    next = it;
            if (next == timers_.end())
                break;
            auto entry = std::move(next->second);
            timers_.erase(next);
            now = entry.at;
            if (auto timer = entry.timer.lock())
                timer->active_ = false;
            entry.continuation();
            flush();
        }
        now = target;
    }

    [[nodiscard]] std::size_t pendingTimers() const { return timers_.size(); }

    std::uint64_t now = 0;

private:
    struct Entry {
        std::uint64_t at = 0;
        EngineContinuation continuation;
        std::weak_ptr<Timer> timer;
    };
    std::uint64_t seq_ = 0;
    std::map<std::uint64_t, Entry> timers_;
    std::deque<EngineContinuation> queue_;
};

// ---- controlled engines and the engine table ----

using Log = Value::Array;

void logEntry(Log &log, std::uint64_t t, const std::string &name, Value::Array args)
{
    Value::Array entry{Value::number(static_cast<double>(t)), Value::string(name)};
    for (auto &arg : args)
        entry.push_back(arg.isMissing() ? Value::null() : std::move(arg));
    log.push_back(Value::array(std::move(entry)));
}

TorrentFile toFile(const Value &tuple)
{
    const auto &a = tuple.asArray();
    TorrentFile file;
    file.path = str(a[0]);
    file.name = str(a[1]);
    file.length = static_cast<std::int64_t>(num(a[2]));
    file.offset = static_cast<std::int64_t>(num(a[3]));
    return file;
}

class FakeEngine final : public LifecycleEngine {
public:
    FakeEngine(std::string hash, const Value &spec, VirtualClock &clock, Log &log, bool &logging,
               std::uint64_t instance)
        : hash_(std::move(hash)), clock_(clock), log_(log), logging_(logging), instance_(instance)
    {
        view_.pieceLength = static_cast<std::int64_t>(num(field(spec, "pieceLength")));
        if (!field(spec, "verificationLen").isMissing())
            view_.verificationLen = static_cast<std::int64_t>(num(field(spec, "verificationLen")));
        for (const auto &file : field(spec, "files").asArray())
            view_.files.push_back(toFile(file));
        hasTorrent_ = field(spec, "torrent").kind() != Value::Kind::Boolean
            || field(spec, "torrent").asBoolean();
        buffer_ = jsTruthy(field(spec, "buffer"));
        if (!field(spec, "dest").isMissing())
            dest_ = str(field(spec, "dest"));
        if (field(spec, "have").kind() == Value::Kind::Array)
            for (const auto &piece : field(spec, "have").asArray())
                have_.push_back(static_cast<std::int64_t>(num(piece)));
    }

    [[nodiscard]] std::uint64_t instance() const noexcept override { return instance_; }
    void ready(EngineContinuation continuation) override
    {
        if (hasTorrent_)
            clock_.defer(std::move(continuation));
        else
            waiting_.push_back(std::move(continuation));
    }
    [[nodiscard]] const LifecycleTorrentView *torrent() const override
    {
        return hasTorrent_ ? &view_ : nullptr;
    }
    [[nodiscard]] bool havePiece(std::int64_t piece) const override
    {
        return std::find(have_.begin(), have_.end(), piece) != have_.end();
    }
    [[nodiscard]] bool buffer() const override { return buffer_; }
    [[nodiscard]] std::optional<std::string> storeDest(std::size_t) const override { return dest_; }
    void select(double from, double to) override
    {
        if (logging_)
            logEntry(log_, clock_.now, "select",
                     {Value::string(hash_), Value::number(from), Value::number(to), Value::boolean(false)});
    }

    void metadata()
    {
        hasTorrent_ = true;
        auto waiting = std::move(waiting_);
        waiting_.clear();
        for (auto &continuation : waiting)
            continuation();
    }
    void addPiece(std::int64_t piece) { have_.push_back(piece); }

private:
    std::string hash_;
    VirtualClock &clock_;
    Log &log_;
    bool &logging_;
    std::uint64_t instance_;
    LifecycleTorrentView view_;
    bool hasTorrent_ = true;
    bool buffer_ = false;
    std::optional<std::string> dest_;
    std::vector<std::int64_t> have_;
    std::vector<EngineContinuation> waiting_;
};

class FakeCatalog final : public EngineCatalog {
public:
    FakeCatalog(VirtualClock &clock, Log &log, bool &logging) : clock_(clock), log_(log), logging_(logging) {}

    void add(const std::string &hash, const Value &spec, std::size_t selections = 0)
    {
        order_.push_back(hash);
        entries_[hash] = {std::make_unique<FakeEngine>(hash, spec, clock_, log_, logging_, ++instances_),
                          selections};
    }
    FakeEngine *fake(const std::string &hash)
    {
        const auto found = entries_.find(hash);
        return found == entries_.end() ? nullptr : found->second.engine.get();
    }
    void hold(const std::string &hash) { held_[hash].hold = true; }
    void release(const std::string &hash)
    {
        auto callbacks = std::move(held_[hash].callbacks);
        held_.erase(hash);
        for (auto &callback : callbacks)
            complete(hash, std::move(callback));
    }
    void releaseAll()
    {
        std::vector<std::string> keys;
        for (const auto &[key, held] : held_)
            keys.push_back(key);
        for (const auto &key : keys)
            release(key);
    }

    [[nodiscard]] std::vector<std::string> list() const override { return order_; }
    [[nodiscard]] bool exists(const std::string &hash) const override { return entries_.count(hash) != 0; }
    [[nodiscard]] std::size_t selectionCount(const std::string &hash) const override
    {
        const auto found = entries_.find(hash);
        return found == entries_.end() ? 0 : found->second.selections;
    }
    void destroy(const std::string &hash, std::function<void()> done) override
    {
        if (logging_)
            logEntry(log_, clock_.now, "destroy", {Value::string(hash)});
        const auto held = held_.find(hash);
        if (held != held_.end() && held->second.hold) {
            held->second.callbacks.push_back(std::move(done));
            return;
        }
        clock_.defer([this, hash, done = std::move(done)]() mutable { complete(hash, std::move(done)); });
    }
    [[nodiscard]] LifecycleEngine *engine(const std::string &hash) override { return fake(hash); }

private:
    void complete(const std::string &hash, std::function<void()> done)
    {
        entries_.erase(hash);
        order_.erase(std::remove(order_.begin(), order_.end(), hash), order_.end());
        if (done)
            done();
    }
    struct Entry {
        std::unique_ptr<FakeEngine> engine;
        std::size_t selections = 0;
    };
    struct Held {
        bool hold = false;
        std::vector<std::function<void()>> callbacks;
    };
    VirtualClock &clock_;
    Log &log_;
    bool &logging_;
    std::vector<std::string> order_;
    std::map<std::string, Entry> entries_;
    std::map<std::string, Held> held_;
    std::uint64_t instances_ = 0;
};

// ---- scenario runners (mirror run_oracle.js) ----

struct Cases {
    Value root;
    std::string hash(const std::string &key) const
    {
        if (key == "Aupper") {
            auto text = str(field(field(root, "hashes"), "A"));
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
                return static_cast<char>(std::toupper(c));
            });
            return text;
        }
        const auto &found = field(field(root, "hashes"), key);
        return found.isMissing() ? key : str(found);
    }
};

Value runCounter(const Value &scenario)
{
    VirtualClock clock;
    Log log;
    EngineCounter counter(
        [](const std::string &hash, std::size_t idx) { return hash + ":" + jsNumberToString(static_cast<double>(idx)); },
        [&](const std::string &hash, std::size_t idx) {
            logEntry(log, clock.now, "positive", {Value::string(hash), Value::number(static_cast<double>(idx))});
        },
        [&](const std::string &hash, std::size_t idx) {
            logEntry(log, clock.now, "zero", {Value::string(hash), Value::number(static_cast<double>(idx))});
        },
        [] { return std::uint64_t{1000}; }, clock.timeout());
    for (const auto &op : field(scenario, "ops").asArray()) {
        const auto &a = op.asArray();
        const auto name = str(a[0]);
        if (name == "advance")
            clock.advance(static_cast<std::uint64_t>(num(a[1])));
        else if (name == "inc")
            counter.increment(str(a[1]), static_cast<std::size_t>(num(a[2])));
        else
            counter.decrement(str(a[1]), static_cast<std::size_t>(num(a[2])));
    }
    return Value::array(std::move(log));
}

Value runLifecycle(const Cases &cases, const Value &scenario)
{
    VirtualClock clock;
    Log log;
    bool logging = false;
    FakeCatalog catalog(clock, log, logging);
    for (const auto &engine : field(scenario, "engines").asArray())
        catalog.add(cases.hash(str(field(engine, "key"))), engine);
    EngineLifecycleConfig config;
    config.setTimeout = clock.timeout();
    config.onEvent = [&](const LifecycleEvent &event) {
        if (logging)
            logEntry(log, clock.now, event.name, event.args);
    };
    EngineLifecycle lifecycle(catalog, config);
    logging = true;
    std::map<std::string, LifecycleStream> streams;
    for (const auto &stepValue : field(scenario, "steps").asArray()) {
        const auto &step = stepValue.asArray();
        const auto op = str(step[0]);
        if (op == "open") {
            streams[str(step[3])] = lifecycle.openStream(cases.hash(str(step[1])),
                                                         static_cast<std::size_t>(num(step[2])));
        } else if (op == "finish") {
            streams[str(step[1])].finish();
        } else if (op == "close") {
            streams[str(step[1])].close();
        } else if (op == "advance") {
            clock.advance(static_cast<std::uint64_t>(num(step[1])));
        } else if (op == "verify") {
            const auto hash = cases.hash(str(step[1]));
            if (auto *engine = catalog.fake(hash))
                engine->addPiece(static_cast<std::int64_t>(num(step[2])));
            lifecycle.verify(hash, static_cast<std::int64_t>(num(step[2])));
        } else if (op == "remove" || op == "removeHeld") {
            const auto hash = cases.hash(str(step[1]));
            if (op == "removeHeld")
                catalog.hold(hash);
            lifecycle.remove(hash, [&log, &clock, hash]() {
                logEntry(log, clock.now, "remove-done", {Value::string(hash)});
            });
        } else if (op == "removeAll") {
            lifecycle.removeAll();
            logEntry(log, clock.now, "response", {Value::number(200), Value::string("{}")});
        } else if (op == "release") {
            catalog.releaseAll();
        } else if (op == "metadata") {
            catalog.fake(cases.hash(str(step[1])))->metadata();
        } else if (op == "setStreamTimeout") {
            lifecycle.setStreamTimeoutMs(static_cast<std::uint64_t>(num(step[1])));
        } else {
            fail("unknown lifecycle step " + op);
        }
        clock.flush();
    }
    return Value::array(std::move(log));
}

Value runKeep(const Cases &cases, const Value &scenario)
{
    VirtualClock clock;
    Log log;
    bool logging = false;
    FakeCatalog catalog(clock, log, logging);
    const auto engineSpec = Value::object({{"pieceLength", Value::number(64)},
                                           {"files", Value::array({})}});
    for (const auto &engine : field(scenario, "engines").asArray())
        catalog.add(cases.hash(str(field(engine, "key"))), engineSpec,
                    static_cast<std::size_t>(num(field(engine, "selections"))));
    EngineLifecycleConfig config;
    config.setTimeout = clock.timeout();
    config.onEvent = [&](const LifecycleEvent &event) {
        if (logging)
            logEntry(log, clock.now, event.name, event.args);
    };
    EngineLifecycle lifecycle(catalog, config);
    logging = true;
    std::vector<std::string> order;
    if (field(scenario, "destroyOrder").kind() == Value::Kind::Array) {
        for (const auto &key : field(scenario, "destroyOrder").asArray())
            order.push_back(cases.hash(str(key)));
        for (const auto &hash : catalog.list())
            catalog.hold(hash);
    }
    for (const auto &callValue : field(scenario, "calls").asArray()) {
        const auto &call = callValue.asArray();
        const auto key = cases.hash(str(call[0]));
        const auto concurrency = call[1];
        lifecycle.keepConcurrency(key, concurrency, [&log, &clock, key, concurrency]() {
            logEntry(log, clock.now, "resolved", {Value::string(key), concurrency});
        });
        clock.flush();
    }
    for (const auto &hash : order) {
        catalog.release(hash);
        clock.flush();
    }
    Value::Array remaining;
    for (const auto &hash : catalog.list())
        remaining.push_back(Value::string(hash));
    logEntry(log, clock.now, "remaining", remaining);
    return Value::array(std::move(log));
}

// util._extend(origin, add): keys of add copied last-first.
Value utilExtend(const Value &origin, const Value &add)
{
    Value::Object object = origin.asObject();
    if (add.kind() == Value::Kind::Object) {
        const auto &entries = add.asObject();
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            auto found = std::find_if(object.begin(), object.end(),
                                      [&](const auto &e) { return e.first == it->first; });
            if (found != object.end())
                found->second = it->second;
            else
                object.push_back(*it);
        }
    }
    return Value::object(std::move(object));
}

Value setKey(const Value &object, const std::string &key, Value value)
{
    auto entries = object.asObject();
    auto found = std::find_if(entries.begin(), entries.end(), [&](const auto &e) { return e.first == key; });
    if (found != entries.end())
        found->second = std::move(value);
    else
        entries.emplace_back(key, std::move(value));
    return Value::object(std::move(entries));
}

// M172 createEngine options: getDefaults(ih) extended by the body, then path
// and the spoofed id.
Value engineOptions(const Cases &cases, const std::string &hash, const Value &body)
{
    const auto defaults = Value::object({
        {"peerSearch", Value::object({{"min", Value::number(40)}, {"max", Value::number(200)},
                                      {"sources", Value::array({Value::string("dht:" + hash)})}})},
        {"dht", Value::boolean(false)},
        {"tracker", Value::boolean(false)},
    });
    auto options = utilExtend(defaults, body);
    const auto *path = options.find("path");
    options = setKey(options, "path", path && jsTruthy(*path)
                                          ? *path
                                          : Value::string(str(field(cases.root, "cachePathPrefix")) + hash));
    return setKey(options, "id", field(cases.root, "spoofedPeerId"));
}

const Value &filesOf(const Cases &cases, const Value &list)
{
    return list.kind() == Value::Kind::String ? field(cases.root, "createFiles") : list;
}

Value fileObjects(const Cases &cases, const Value &list)
{
    Value::Array files;
    for (const auto &tuple : filesOf(cases, list).asArray()) {
        const auto file = toFile(tuple);
        files.push_back(Value::object({{"path", Value::string(file.path)},
                                       {"name", Value::string(file.name)},
                                       {"length", Value::number(static_cast<double>(file.length))},
                                       {"offset", Value::number(static_cast<double>(file.offset))}}));
    }
    return Value::array(std::move(files));
}

struct StatsEngine {
    Value state;
    Value options;
};

EngineStatsSnapshot snapshotOf(const Cases &cases, const std::string &hash, const StatsEngine &engine,
                               const EngineLifecycle *lifecycle)
{
    const auto &state = engine.state;
    EngineStatsSnapshot s;
    s.infoHash = hash;
    const auto &torrent = field(state, "torrent");
    s.hasTorrent = torrent.kind() == Value::Kind::Object;
    if (s.hasTorrent) {
        s.torrentName = field(torrent, "name");
        s.pieceLength = static_cast<std::int64_t>(num(field(torrent, "pieceLength")));
        Value::Array files;
        std::size_t index = 0;
        for (const auto &tuple : filesOf(cases, field(torrent, "files")).asArray()) {
            const auto file = toFile(tuple);
            s.fileGeometry.push_back(file);
            Value::Object object{{"path", Value::string(file.path)},
                                 {"name", Value::string(file.name)},
                                 {"length", Value::number(static_cast<double>(file.length))},
                                 {"offset", Value::number(static_cast<double>(file.offset))}};
            if (lifecycle && lifecycle->cacheEventsEmitted(hash, index))
                object.emplace_back("__cacheEvents", Value::boolean(true));
            files.push_back(Value::object(std::move(object)));
            ++index;
        }
        s.torrentFiles = Value::array(std::move(files));
    }
    for (const auto &piece : field(state, "have").asArray()) {
        const auto index = static_cast<std::size_t>(num(piece));
        if (s.have.size() <= index)
            s.have.resize(index + 1, false);
        s.have[index] = true;
    }
    for (const auto &wire : field(state, "wires").asArray()) {
        EngineWireRecord record;
        record.peerChoking = field(wire, "peerChoking").asBoolean();
        record.requests = static_cast<std::size_t>(num(field(wire, "requests")));
        record.address = field(wire, "address");
        record.amInterested = field(wire, "amInterested").asBoolean();
        record.isSeeder = field(wire, "isSeeder").asBoolean();
        record.downSpeed = num(field(wire, "downSpeed"));
        record.upSpeed = num(field(wire, "upSpeed"));
        s.wires.push_back(record);
    }
    s.queued = field(state, "queued");
    s.uniquePeers = static_cast<std::size_t>(num(field(state, "unique")));
    s.connectionTries = field(state, "tries");
    s.swarmPaused = field(state, "paused").asBoolean();
    s.swarmConnections = static_cast<std::size_t>(num(field(state, "connections")));
    s.swarmSize = field(state, "size");
    s.selections = field(state, "selections");
    s.downloaded = field(state, "downloaded");
    s.uploaded = field(state, "uploaded");
    s.swarmDownloadSpeed = num(field(state, "downloadSpeed"));
    s.swarmUploadSpeed = num(field(state, "uploadSpeed"));
    const auto &peerSearch = field(state, "peerSearch");
    s.hasPeerSearch = !peerSearch.isMissing();
    if (s.hasPeerSearch) {
        s.peerSearchStats = field(peerSearch, "stats");
        s.peerSearchRunning = field(peerSearch, "running").asBoolean();
    }
    s.options = engine.options;
    return s;
}

Value lifecycleSpecOf(const Cases &cases, const Value &state)
{
    const auto &torrent = field(state, "torrent");
    if (torrent.kind() != Value::Kind::Object)
        return Value::object({{"torrent", Value::boolean(false)}, {"pieceLength", Value::number(1)},
                              {"files", Value::array({})}});
    return Value::object({{"pieceLength", field(torrent, "pieceLength")},
                          {"files", filesOf(cases, field(torrent, "files"))},
                          {"have", field(state, "have")}});
}

Value runStats(const Cases &cases, const Value &scenario)
{
    VirtualClock clock;
    Log ignored;
    bool logging = false;
    FakeCatalog catalog(clock, ignored, logging);
    std::vector<std::string> order;
    std::map<std::string, StatsEngine> engines;
    const auto create = [&](const std::string &hash, const Value &state, const Value &body) {
        if (!engines.count(hash)) {
            order.push_back(hash);
            catalog.add(hash, lifecycleSpecOf(cases, state));
            engines[hash].state = state;
        }
        engines[hash].options = engineOptions(cases, hash, body);
    };
    for (const auto &engine : field(scenario, "engines").asArray())
        create(cases.hash(str(field(engine, "key"))),
               field(field(cases.root, "statStates"), str(field(engine, "state"))), Value::object({}));
    EngineLifecycleConfig config;
    config.setTimeout = clock.timeout();
    EngineLifecycle lifecycle(catalog, config);
    if (field(scenario, "open").kind() == Value::Kind::Array)
        for (const auto &open : field(scenario, "open").asArray()) {
            const auto &a = open.asArray();
            (void)lifecycle.openStream(cases.hash(str(a[0])), static_cast<std::size_t>(num(a[1])));
            clock.flush();
        }
    Value::Array responses;
    for (const auto &requestValue : field(scenario, "requests").asArray()) {
        const auto &request = requestValue.asArray();
        const auto method = str(request[0]);
        const auto url = str(request[1]);
        std::string path = url.substr(0, url.find('?'));
        std::vector<std::string> parts;
        for (std::size_t start = 1; start <= path.size();) {
            const auto slash = path.find('/', start);
            parts.push_back(path.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
            if (slash == std::string::npos)
                break;
            start = slash + 1;
        }
        for (auto &part : parts)
            if (part == "A" || part == "B" || part == "C" || part == "Aupper")
                part = cases.hash(part);
        Value response;
        const auto ok = [](std::string body) {
            return Value::object({{"status", Value::number(200)}, {"body", Value::string(std::move(body))}});
        };
        const auto lookup = [&](const std::string &key) -> std::optional<EngineStatsSnapshot> {
            const auto found = engines.find(key); // engines[req.params.infoHash]: exact case
            if (found == engines.end())
                return std::nullopt;
            return snapshotOf(cases, key, found->second, &lifecycle);
        };
        if (parts.size() == 2 && parts[1] == "stats.json") {
            const auto snapshot = lookup(parts[0]);
            response = ok(serializeEngineStatistics(snapshot ? &*snapshot : nullptr));
        } else if (parts.size() == 3 && parts[2] == "stats.json") {
            const auto snapshot = lookup(parts[0]);
            const auto idx = Value::string(parts[1]);
            response = ok(serializeEngineStatistics(snapshot ? &*snapshot : nullptr, &idx));
        } else if (parts.size() == 1 && parts[0] == "stats.json") {
            std::vector<EngineStatsSnapshot> snapshots;
            for (const auto &hash : order)
                snapshots.push_back(snapshotOf(cases, hash, engines[hash], &lifecycle));
            std::vector<std::pair<std::string, const EngineStatsSnapshot *>> all;
            for (std::size_t i = 0; i < order.size(); ++i)
                all.emplace_back(order[i], &snapshots[i]);
            const auto sys = field(cases.root, "sys");
            response = ok(serializeAllEngineStatistics(all, url.find("sys=1") != std::string::npos ? &sys : nullptr));
        } else if (method == "POST" && parts.size() == 2 && parts[1] == "create") {
            std::string hash = parts[0];
            std::transform(hash.begin(), hash.end(), hash.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            const auto body = request.size() > 2 ? request[2] : Value::object({});
            create(hash, field(field(cases.root, "statStates"), "create"), body);
            const auto snapshot = snapshotOf(cases, hash, engines[hash], &lifecycle);
            const auto selection = createRouteSelection(body, snapshot.torrentFiles);
            if (selection.error) {
                response = Value::object({{"throws", Value::string(*selection.error)}});
            } else {
                Value::Object extras;
                if (!selection.guessedFileIdx.isMissing())
                    extras.emplace_back("guessedFileIdx", selection.guessedFileIdx);
                response = ok(serializeEngineStatistics(&snapshot, nullptr, extras));
            }
        } else {
            fail("unhandled request " + url);
        }
        responses.push_back(Value::array({Value::string(method), Value::string(url), response}));
        clock.flush();
    }
    return Value::array(std::move(responses));
}

Value videoResult(const std::string &name, const Value &options)
{
    const auto meta = parseVideoName(name, options);
    if (meta.isMissing())
        return Value::object({{"throws", Value::string("TypeError")}});
    return Value::object({{"meta", meta}});
}

std::map<std::string, std::string> runAll(const Cases &cases)
{
    std::map<std::string, std::string> out;
    const auto &root = cases.root;
    for (const auto &scenario : field(root, "counter").asArray())
        out[str(field(scenario, "id"))] = jsJsonStringify(runCounter(scenario));
    for (const auto &scenario : field(root, "lifecycle").asArray())
        out[str(field(scenario, "id"))] = jsJsonStringify(runLifecycle(cases, scenario));
    for (const auto &scenario : field(root, "keep").asArray())
        out[str(field(scenario, "id"))] = jsJsonStringify(runKeep(cases, scenario));
    for (const auto &scenario : field(root, "stats").asArray())
        out[str(field(scenario, "id"))] = jsJsonStringify(runStats(cases, scenario));
    for (const auto &scenario : field(root, "guess").asArray())
        out[str(field(scenario, "id"))] = jsJsonStringify(
            Value::number(guessFileIdx(fileObjects(cases, field(scenario, "files")), field(scenario, "seriesInfo"))));
    Value::Array names;
    for (const auto &name : field(root, "videoNames").asArray())
        names.push_back(Value::array({name, videoResult(str(name), Value::object({}))}));
    out["V-names"] = jsJsonStringify(Value::array(std::move(names)));
    Value::Array withOptions;
    for (const auto &entry : field(root, "videoNameOptions").asArray()) {
        const auto &a = entry.asArray();
        withOptions.push_back(Value::array({a[0], a[1], videoResult(str(a[0]), a[1])}));
    }
    out["V-options"] = jsJsonStringify(Value::array(std::move(withOptions)));
    Value::Array text;
    for (const auto &number : field(root, "numbers").asArray())
        text.push_back(Value::string(jsNumberToString(num(number))));
    out["N-numbers"] = jsJsonStringify(Value::array({field(root, "numbers"), Value::array(std::move(text))}));
    return out;
}

void writeOutputs(const std::map<std::string, std::string> &outputs, const std::string &path)
{
    Value::Object object;
    for (const auto &[id, text] : outputs)
        object.emplace_back(id, Value::string(text));
    std::ofstream output(path, std::ios::binary);
    output << jsJsonStringify(Value::object(std::move(object))) << '\n';
    expect(bool(output), "cannot write " + path);
}

// ---- comparison with the committed source outputs ----

void compareGroup(const std::map<std::string, std::string> &native, const Value &source,
                  const std::vector<std::string> &prefixes, const std::string &label)
{
    std::size_t compared = 0;
    for (const auto &[id, expected] : source.asObject()) {
        const bool wanted = std::any_of(prefixes.begin(), prefixes.end(), [&](const std::string &prefix) {
            return id.rfind(prefix, 0) == 0;
        });
        if (!wanted)
            continue;
        const auto found = native.find(id);
        expect(found != native.end(), label + " native output missing for " + id);
        if (found->second != expected.asString()) {
            std::cerr << "source: " << expected.asString().substr(0, 600) << '\n'
                      << "native: " << found->second.substr(0, 600) << '\n';
            fail(label + " differs from the source for " + id);
        }
        ++compared;
    }
    expect(compared > 0, label + " compared nothing");
    std::cout << label << " source/native exact " << compared << '\n';
}

// ---- native-only checks ----

void checkTeardown(const Cases &cases)
{
    VirtualClock clock;
    Log log;
    bool logging = true;
    FakeCatalog catalog(clock, log, logging);
    const auto file = [](const char *name, double offset) {
        return Value::array({Value::string(name), Value::string(name), Value::number(100), Value::number(offset)});
    };
    catalog.add(cases.hash("A"), Value::object({{"pieceLength", Value::number(64)},
                                                {"files", Value::array({file("f0", 0), file("f1", 100)})}}));
    std::size_t events = 0;
    LifecycleStream open;
    {
        EngineLifecycleConfig config;
        config.setTimeout = clock.timeout();
        config.onEvent = [&](const LifecycleEvent &) { ++events; };
        EngineLifecycle lifecycle(catalog, config);
        auto first = lifecycle.openStream(cases.hash("A"), 0);
        open = lifecycle.openStream(cases.hash("A"), 1);
        clock.flush();
        expect(lifecycle.pendingCacheTrackers() == 2, "K12-01 cache listeners not registered");
        first.close();
        expect(clock.pendingTimers() == 1, "K12-01 closing one file must arm only its stream timer");
        expect(lifecycle.streamSlot(cases.hash("A"), 0).timerPending
                   && lifecycle.engineSlot(cases.hash("A")).count == 1,
               "K12-01 counters after one close");
    }
    const auto before = events;
    expect(clock.pendingTimers() == 0, "K12-01 teardown left owned timers armed");
    open.close();
    clock.advance(500000);
    expect(events == before, "K12-01 events after teardown");
    std::cout << "K12-01 TEARDOWN PASS timers=0 events_after=0\n";
}

void checkUnknownEngineOpen(const Cases &cases)
{
    VirtualClock clock;
    Log log;
    bool logging = true;
    FakeCatalog catalog(clock, log, logging);
    std::vector<std::string> names;
    EngineLifecycleConfig config;
    config.setTimeout = clock.timeout();
    config.onEvent = [&](const LifecycleEvent &event) { names.push_back(event.name); };
    EngineLifecycle lifecycle(catalog, config);
    auto stream = lifecycle.openStream(cases.hash("B"), 0);
    stream.close();
    clock.advance(500000);
    expect(names == std::vector<std::string>{"stream-open"}, "K12-01 unknown engine must stop after stream-open");
    expect(!lifecycle.streamSlot(cases.hash("B"), 0).present, "K12-01 unknown engine touched the counters");
    std::cout << "K12-01 UNKNOWN-ENGINE PASS events=stream-open counters=0\n";
}

void checkRegistryCatalog()
{
    EngineRegistry registry(EngineRegistryConfig{});
    RegistryEngineCatalog catalog(registry);
    const std::string a(40, 'a');
    const std::string b(40, 'b');
    catalog.observe({EngineEventType::Created, b, 1, Value::missing(), {}});
    catalog.observe({EngineEventType::Created, a, 2, Value::missing(), {}});
    // Nothing is in the registry, so the observed order filters to empty and
    // no engine surface is offered.
    expect(catalog.list().empty(), "K12-01 catalog lists engines the registry does not hold");
    expect(!catalog.exists(a) && catalog.selectionCount(a) == 0, "K12-01 catalog invented an engine");
    expect(catalog.engine(a) == nullptr, "K12-01 registry catalog must not offer a lifecycle surface");
    bool done = false;
    catalog.destroy(a, [&] { done = true; });
    registry.dispatch();
    expect(done, "K12-01 destroy of an unknown engine must complete");
    std::cout << "K12-01 REGISTRY-CATALOG PASS\n";
}

void checkSnapshotFieldsIgnored()
{
    EngineStatsSnapshot s;
    s.infoHash = "x";
    s.swarmDownloadSpeed = 7.5;
    s.swarmUploadSpeed = 99;
    const auto json = serializeEngineStatistics(&s);
    expect(json.find("\"downloadSpeed\":7.5,\"uploadSpeed\":7.5") != std::string::npos,
           "K12-02 uploadSpeed must carry downloadSpeed as the source assigns it");
    std::cout << "K12-02 UPLOAD-SPEED-QUIRK PASS\n";
}

void checkRegex()
{
    expect(!JsRegExp::compile("a", "gg"), "K12-03 duplicate flag accepted");
    expect(!JsRegExp::compile("a", "q"), "K12-03 unknown flag accepted");
    expect(!JsRegExp::compile("(", ""), "K12-03 invalid pattern accepted");
    const auto sticky = JsRegExp::compile("show", "y");
    expect(sticky && sticky->test("show.mkv") && !sticky->test("my show.mkv"), "K12-03 sticky anchoring");
    const auto dotAll = JsRegExp::compile("a.b", "s");
    expect(dotAll && dotAll->test("a\nb"), "K12-03 dotAll");
    const auto icase = JsRegExp::compile("S01E02", "i");
    expect(icase && icase->test("show.s01e02.mkv"), "K12-03 icase");
    std::cout << "K12-03 REGEX PASS\n";
}

int runCase(const std::string &which, const Cases &cases, const std::string &sourcePath)
{
    const auto native = runAll(cases);
    const auto source = readOrderedJson(sourcePath);
    if (which == "K12-01") {
        compareGroup(native, source, {"M662-", "L"}, "K12-01");
        checkTeardown(cases);
        checkUnknownEngineOpen(cases);
        checkRegistryCatalog();
    } else if (which == "K12-02") {
        compareGroup(native, source, {"S", "N-"}, "K12-02");
        checkSnapshotFieldsIgnored();
    } else if (which == "K12-03") {
        compareGroup(native, source, {"G", "V-", "K0"}, "K12-03");
        checkRegex();
    } else {
        fail("unknown case " + which);
    }
    std::cout << which << " PASS\n";
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc == 4 && std::string(argv[1]) == "--emit") {
        Cases cases{readOrderedJson(argv[2])};
        writeOutputs(runAll(cases), argv[3]);
        std::cout << "K12 EMIT " << argv[3] << '\n';
        return 0;
    }
    if (argc == 4) {
        Cases cases{readOrderedJson(argv[2])};
        return runCase(argv[1], cases, argv[3]);
    }
    std::cerr << "usage: " << argv[0] << " --emit <cases> <out> | K12-0N <cases> <source-outputs>\n";
    return 2;
}
