#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace server1::discovery {
struct AutonomyPolicy final {
    bool automaticPicker = false; bool automaticDht = false; bool automaticTracker = false;
    [[nodiscard]] bool externallyControlled() const noexcept
    { return !automaticPicker && !automaticDht && !automaticTracker; }
};
struct PeerSourceStats final { std::size_t numFound=0,numFoundUniq=0,numRequests=0;
    std::string url; std::uint64_t lastStartedMs=0; };
class TrackerSource final {
public:
    TrackerSource(std::string url,std::string infoHash); void run(); void pause() noexcept;
    void close() noexcept; void failNextRun() noexcept;
    [[nodiscard]] const std::string&url()const noexcept; [[nodiscard]] const std::string&infoHash()const noexcept;
    [[nodiscard]] std::size_t numRequests()const noexcept; [[nodiscard]] bool lastRunFailed()const noexcept;
    [[nodiscard]] bool closed()const noexcept;
private: std::string url_,infoHash_;std::size_t numRequests_=0;bool failNextRun_=false,lastRunFailed_=false,closed_=false;
};
class DhtSource final {
public:
    explicit DhtSource(std::string infoHash); void run(std::uint64_t);void pause(std::uint64_t);
    void advance(std::uint64_t);void close()noexcept;
    [[nodiscard]]const std::string&infoHash()const noexcept;[[nodiscard]]std::size_t numRequests()const noexcept;
    [[nodiscard]]bool waiting()const noexcept;[[nodiscard]]bool lookupActive()const noexcept;
    [[nodiscard]]bool abortPending()const noexcept;[[nodiscard]]bool closed()const noexcept;
private:std::string infoHash_;std::size_t numRequests_=0;std::uint64_t lookupAtMs_=0,abortAtMs_=0;
    bool waiting_=false,lookupActive_=false,abortPending_=false,closed_=false;
};
class PeerSearch final {
public:
    PeerSearch(std::vector<std::string> sources,std::optional<std::size_t> minimum,
               std::optional<std::size_t> maximum,std::uint64_t nowMs,
               AutonomyPolicy autonomy = {});
    void run(std::uint64_t);void pause(std::uint64_t nowMs=0)noexcept;
    void onSwarmState(std::size_t queued,bool swarmPaused,std::uint64_t nowMs);
    void tick(std::uint64_t);void emitPeer(std::size_t,std::string);void close();
    [[nodiscard]]bool isRunning()const noexcept;[[nodiscard]]bool closed()const noexcept;
    [[nodiscard]]bool intervalActive()const noexcept;[[nodiscard]]const std::vector<PeerSourceStats>&stats()const noexcept;
    [[nodiscard]]const std::vector<std::string>&peerAdds()const noexcept;
    std::vector<std::string> takePeerAdds();
    [[nodiscard]]const AutonomyPolicy&autonomyPolicy()const noexcept;
    [[nodiscard]]std::size_t trackerRequests(std::size_t sourceIndex)const;
    [[nodiscard]]std::size_t dhtRequests(std::size_t sourceIndex)const;
    [[nodiscard]]bool dhtWaiting(std::size_t sourceIndex)const;
    static std::vector<std::string> selectSources(const std::vector<std::string>&,
        const std::vector<std::string>&,const std::string&);
    static bool internalDhtEnabled()noexcept;static bool internalTrackerEnabled()noexcept;
private:
    struct Binding { bool tracker; std::size_t index; };
    std::vector<PeerSourceStats> sources_;std::vector<Binding>bindings_;
    std::vector<TrackerSource>trackers_;std::vector<DhtSource>dhts_;
    std::set<std::string>uniquePeers_;std::vector<std::string>peerAdds_;
    std::optional<std::size_t>minimum_,maximum_;AutonomyPolicy autonomy_;
    std::uint64_t lastIntervalMs_=0,lastNowMs_=0;bool running_=false,intervalActive_=true,closed_=false;
};
}
