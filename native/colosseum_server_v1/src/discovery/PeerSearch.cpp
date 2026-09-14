#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace server1::discovery {

struct PeerSourceStats final {
    std::size_t numFound = 0;
    std::size_t numFoundUniq = 0;
    std::size_t numRequests = 0;
    std::string url;
    std::uint64_t lastStartedMs = 0;
};

class PeerSearch final {
public:
    PeerSearch(std::vector<std::string> sources,
               std::optional<std::size_t> minimum,
               std::optional<std::size_t> maximum,
               std::uint64_t nowMs)
        : minimum_(minimum)
        , maximum_(maximum)
    {
        for (auto &source : sources) {
            if (source.rfind("dht:", 0) == 0 || source.rfind("tracker:", 0) == 0) {
                sources_.push_back({0, 0, 0, std::move(source), 0});
            }
        }
        run(nowMs);
    }

    void run(std::uint64_t nowMs)
    {
        if (closed_) {
            return;
        }
        running_ = true;
        for (auto &source : sources_) {
            source.lastStartedMs = nowMs;
            if (source.url.rfind("tracker:", 0) == 0) {
                ++source.numRequests;
            }
        }
    }

    void pause() noexcept { running_ = false; }

    void onSwarmState(std::size_t queued, bool swarmPaused, std::uint64_t nowMs)
    {
        if (swarmPaused && running_) {
            pause();
        } else if (minimum_ && queued < *minimum_ && !running_) {
            run(nowMs);
        } else if (maximum_ && queued > *maximum_ && running_) {
            pause();
        }
    }

    void tick(std::uint64_t nowMs)
    {
        if (running_ && intervalActive_ && nowMs >= lastIntervalMs_ + 30000) {
            lastIntervalMs_ = nowMs;
            run(nowMs);
        }
    }

    void emitPeer(std::size_t sourceIndex, std::string address)
    {
        if (closed_ || sourceIndex >= sources_.size()) {
            return;
        }
        auto &source = sources_[sourceIndex];
        if (uniquePeers_.insert(address).second) {
            ++source.numFoundUniq;
        }
        ++source.numFound;
        peerAdds_.push_back(std::move(address));
    }

    void close()
    {
        pause();
        intervalActive_ = false;
        closed_ = true;
    }

    [[nodiscard]] bool isRunning() const noexcept { return running_; }
    [[nodiscard]] bool closed() const noexcept { return closed_; }
    [[nodiscard]] bool intervalActive() const noexcept { return intervalActive_; }
    [[nodiscard]] const std::vector<PeerSourceStats> &stats() const noexcept { return sources_; }
    [[nodiscard]] const std::vector<std::string> &peerAdds() const noexcept { return peerAdds_; }

    [[nodiscard]] static std::vector<std::string>
    selectSources(const std::vector<std::string> &torrentAnnounces,
                  const std::vector<std::string> &configuredSources,
                  const std::string &infoHash)
    {
        if (torrentAnnounces.empty()) {
            return configuredSources;
        }
        std::vector<std::string> result;
        result.reserve(torrentAnnounces.size() + 1);
        for (const auto &announce : torrentAnnounces) {
            result.push_back("tracker:" + announce);
        }
        result.push_back("dht:" + infoHash);
        return result;
    }

    [[nodiscard]] static bool internalDhtEnabled() noexcept { return false; }
    [[nodiscard]] static bool internalTrackerEnabled() noexcept { return false; }

private:
    std::vector<PeerSourceStats> sources_;
    std::set<std::string> uniquePeers_;
    std::vector<std::string> peerAdds_;
    std::optional<std::size_t> minimum_;
    std::optional<std::size_t> maximum_;
    std::uint64_t lastIntervalMs_ = 0;
    bool running_ = false;
    bool intervalActive_ = true;
    bool closed_ = false;
};

} // namespace server1::discovery
