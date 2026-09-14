#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace server1::policy {

struct PeerState final {
    std::string id;
    bool peerChoking = true;
    bool amChoking = true;
    bool amInterested = false;
    bool isSeeder = false;
    std::uint64_t downloadSpeed = 0;
    std::uint64_t uploadSpeed = 0;
    std::uint64_t salt = 0;
    std::optional<std::uint64_t> chokeDeadlineMs;
};

struct SwarmAction final {
    std::string peerId;
    bool choke = true;
};

class SwarmPolicy final {
public:
    SwarmPolicy(std::size_t capacity, std::size_t uploads)
        : capacity_(capacity)
        , uploads_(uploads)
    {
    }

    void addPeer(PeerState peer) { peers_[peer.id] = std::move(peer); }

    [[nodiscard]] const PeerState &peer(const std::string &id) const
    {
        const auto found = peers_.find(id);
        if (found == peers_.end())
            throw std::out_of_range("peer id");
        return found->second;
    }

    void onChoke(const std::string &id, std::uint64_t nowMs)
    {
        auto &state = peers_.at(id);
        state.peerChoking = true;
        state.chokeDeadlineMs = nowMs + 5000;
    }

    void onUnchoke(const std::string &id)
    {
        auto &state = peers_.at(id);
        state.peerChoking = false;
        state.chokeDeadlineMs.reset();
    }

    [[nodiscard]] bool shouldDropChoked(const std::string &id, std::size_t queued,
                                         std::size_t capacity, std::size_t connected,
                                         std::uint64_t nowMs) const
    {
        const auto &state = peer(id);
        const auto open = capacity > connected ? capacity - connected : 0;
        return state.chokeDeadlineMs.has_value() && nowMs >= *state.chokeDeadlineMs
            && state.amInterested && queued > 2 * open;
    }

    [[nodiscard]] bool rechokeDue(std::uint64_t nowMs) const noexcept
    {
        return nowMs >= lastRechokeMs_ + 10000;
    }

    std::vector<SwarmAction> rechoke(std::uint64_t nowMs, std::size_t uploadSlots)
    {
        lastRechokeMs_ = nowMs;
        std::vector<PeerState *> ranked;
        std::vector<SwarmAction> actions;
        for (auto &[id, state] : peers_) {
            if (state.isSeeder) {
                if (!state.amChoking) {
                    state.amChoking = true;
                    actions.push_back({id, true});
                }
            } else {
                ranked.push_back(&state);
            }
        }
        std::sort(ranked.begin(), ranked.end(), [](const PeerState *left, const PeerState *right) {
            if (left->downloadSpeed != right->downloadSpeed)
                return left->downloadSpeed > right->downloadSpeed;
            if (left->uploadSpeed != right->uploadSpeed)
                return left->uploadSpeed > right->uploadSpeed;
            if (left->amChoking != right->amChoking)
                return !left->amChoking;
            return left->salt < right->salt;
        });

        std::size_t index = 0;
        std::size_t unchokedInterested = 0;
        for (; index < ranked.size() && unchokedInterested < uploadSlots; ++index) {
            auto &state = *ranked[index];
            if (state.amChoking) {
                state.amChoking = false;
                actions.push_back({state.id, false});
            }
            if (state.amInterested)
                ++unchokedInterested;
        }

        if (uploadSlots != 0 && index < ranked.size()) {
            auto optimistic = std::min_element(ranked.begin() + static_cast<std::ptrdiff_t>(index),
                                               ranked.end(),
                [](const PeerState *left, const PeerState *right) {
                    if (left->amInterested != right->amInterested)
                        return left->amInterested > right->amInterested;
                    return left->salt < right->salt;
                });
            if (optimistic != ranked.end() && (*optimistic)->amInterested
                && (*optimistic)->amChoking) {
                (*optimistic)->amChoking = false;
                actions.push_back({(*optimistic)->id, false});
            }
        }

        for (auto *state : ranked) {
            const bool selected = std::any_of(actions.begin(), actions.end(),
                [&](const SwarmAction &action) {
                    return action.peerId == state->id && !action.choke;
                });
            if (!selected && !state->amChoking) {
                state->amChoking = true;
                actions.push_back({state->id, true});
            }
        }
        return actions;
    }

private:
    std::size_t capacity_ = 0;
    std::size_t uploads_ = 0;
    std::uint64_t lastRechokeMs_ = 0;
    std::map<std::string, PeerState> peers_;
};

class EngineSwarmRegistry final {
public:
    void start(std::string infoHash, std::uint64_t generation)
    {
        entries_[std::move(infoHash)] = Entry{generation, {}, 0};
    }

    bool stop(const std::string &infoHash) { return entries_.erase(infoHash) != 0; }
    [[nodiscard]] bool live(const std::string &infoHash) const
    {
        return entries_.find(infoHash) != entries_.end();
    }
    void recordDiscovery(const std::string &infoHash, std::string source)
    {
        entries_.at(infoHash).discovery.push_back(std::move(source));
    }
    void scheduleTimer(const std::string &infoHash, std::uint64_t deadline)
    {
        entries_.at(infoHash).timer = deadline;
    }
    [[nodiscard]] std::vector<std::string> discovery(const std::string &infoHash) const
    {
        const auto found = entries_.find(infoHash);
        return found == entries_.end() ? std::vector<std::string>{}
                                      : found->second.discovery;
    }
    [[nodiscard]] std::uint64_t timer(const std::string &infoHash) const
    {
        const auto found = entries_.find(infoHash);
        return found == entries_.end() ? 0 : found->second.timer;
    }

private:
    struct Entry final {
        std::uint64_t generation = 0;
        std::vector<std::string> discovery;
        std::uint64_t timer = 0;
    };
    std::map<std::string, Entry> entries_;
};

} // namespace server1::policy
