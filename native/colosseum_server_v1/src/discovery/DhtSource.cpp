#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace server1::discovery {

class DhtSource final {
public:
    explicit DhtSource(std::string infoHash)
        : infoHash_(std::move(infoHash))
    {
    }

    void run(std::uint64_t nowMs)
    {
        if (!closed_ && !lookupActive_ && !waiting_) {
            waiting_ = true;
            lookupAtMs_ = nowMs + 1500;
        }
    }

    void pause(std::uint64_t nowMs)
    {
        if (waiting_) {
            waiting_ = false;
        }
        if (lookupActive_) {
            lookupActive_ = false;
            abortPending_ = true;
            abortAtMs_ = nowMs + 1500;
        }
    }

    void advance(std::uint64_t nowMs)
    {
        if (closed_) {
            return;
        }
        if (waiting_ && nowMs >= lookupAtMs_) {
            waiting_ = false;
            lookupActive_ = true;
            ++numRequests_;
        }
        if (abortPending_ && nowMs >= abortAtMs_) {
            abortPending_ = false;
        }
    }

    void close() noexcept
    {
        waiting_ = false;
        lookupActive_ = false;
        abortPending_ = false;
        closed_ = true;
    }

    [[nodiscard]] const std::string &infoHash() const noexcept { return infoHash_; }
    [[nodiscard]] std::size_t numRequests() const noexcept { return numRequests_; }
    [[nodiscard]] bool waiting() const noexcept { return waiting_; }
    [[nodiscard]] bool lookupActive() const noexcept { return lookupActive_; }
    [[nodiscard]] bool abortPending() const noexcept { return abortPending_; }
    [[nodiscard]] bool closed() const noexcept { return closed_; }

private:
    std::string infoHash_;
    std::size_t numRequests_ = 0;
    std::uint64_t lookupAtMs_ = 0;
    std::uint64_t abortAtMs_ = 0;
    bool waiting_ = false;
    bool lookupActive_ = false;
    bool abortPending_ = false;
    bool closed_ = false;
};

} // namespace server1::discovery
