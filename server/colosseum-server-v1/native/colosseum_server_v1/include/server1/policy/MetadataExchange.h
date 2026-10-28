#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace server1::policy {
using ByteVector = std::vector<std::uint8_t>;
class MetadataExchange final {
public:
    static constexpr std::size_t kChunkSize = 16384;
    static constexpr std::size_t kMaxMetadataSize = 4 * 1024 * 1024;
    explicit MetadataExchange(std::string infoHash);
    bool advertise(std::size_t size);
    [[nodiscard]] std::vector<std::size_t> pendingRequests() const;
    bool receive(int piece, ByteVector data);
    [[nodiscard]] const std::optional<ByteVector> &metadata() const noexcept;
    [[nodiscard]] std::size_t rejections() const noexcept;
private:
    std::string infoHash_;
    std::size_t size_ = 0;
    std::vector<std::optional<ByteVector>> chunks_;
    std::optional<ByteVector> metadata_;
    std::size_t rejections_ = 0;
};
}
