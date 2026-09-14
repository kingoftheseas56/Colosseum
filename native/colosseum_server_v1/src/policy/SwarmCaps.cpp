#include <cstddef>
#include <optional>
#include <vector>

namespace server1::policy {

struct BufferSelection final {
    std::size_t from = 0;
    std::size_t offset = 0;
    std::size_t readFrom = 0;
    std::size_t selectTo = 0;
};

struct SwarmCapOptions final {
    std::optional<double> maxSpeed;
    std::optional<double> maxBuffer;
    std::size_t minPeers = 0;
};

class SwarmCaps final {
public:
    [[nodiscard]] static double bufferFullness(const std::vector<BufferSelection> &selections)
    {
        double sum = 0.0;
        std::size_t count = 0;
        for (const auto &selection : selections) {
            if (selection.readFrom > 0 && selection.selectTo > 0) {
                const double bufferPieces = static_cast<double>(selection.selectTo)
                    - static_cast<double>(selection.readFrom);
                const double progress = (static_cast<double>(selection.from)
                                         + static_cast<double>(selection.offset)
                                         - static_cast<double>(selection.readFrom))
                    / bufferPieces;
                sum += progress;
                ++count;
            }
        }
        return count == 0 ? 0.0 : sum / static_cast<double>(count);
    }

    [[nodiscard]] static bool shouldPause(std::size_t unchokedPeers,
                                          double aggregateBytesPerSecond,
                                          const std::vector<BufferSelection> &selections,
                                          const SwarmCapOptions &options)
    {
        bool primaryCondition = true;
        if (options.maxSpeed && *options.maxSpeed != 0.0) {
            primaryCondition = aggregateBytesPerSecond > *options.maxSpeed;
        }
        if (options.maxBuffer && *options.maxBuffer != 0.0) {
            primaryCondition = bufferFullness(selections) > *options.maxBuffer;
        }
        return primaryCondition && unchokedPeers > options.minPeers;
    }
};

} // namespace server1::policy
