#include <cstddef>
#include <string>
#include <utility>

namespace server1::discovery {

class TrackerSource final {
public:
    TrackerSource(std::string url, std::string infoHash)
        : url_(std::move(url))
        , infoHash_(std::move(infoHash))
    {
    }

    void run()
    {
        if (!closed_) {
            ++numRequests_;
            lastRunFailed_ = failNextRun_;
            failNextRun_ = false;
        }
    }

    void pause() noexcept {}
    void close() noexcept { closed_ = true; }
    void failNextRun() noexcept { failNextRun_ = true; }

    [[nodiscard]] const std::string &url() const noexcept { return url_; }
    [[nodiscard]] const std::string &infoHash() const noexcept { return infoHash_; }
    [[nodiscard]] std::size_t numRequests() const noexcept { return numRequests_; }
    [[nodiscard]] bool lastRunFailed() const noexcept { return lastRunFailed_; }
    [[nodiscard]] bool closed() const noexcept { return closed_; }

private:
    std::string url_;
    std::string infoHash_;
    std::size_t numRequests_ = 0;
    bool failNextRun_ = false;
    bool lastRunFailed_ = false;
    bool closed_ = false;
};

} // namespace server1::discovery
