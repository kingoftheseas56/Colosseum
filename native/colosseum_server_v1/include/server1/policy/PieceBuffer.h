#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace server1::policy {

using ByteBuffer = std::vector<std::uint8_t>;

class PieceBuffer final {
public:
    static constexpr std::size_t kBlockSize = 16384;
    static constexpr int kNoReservation = -1;

    explicit PieceBuffer(std::size_t length, std::uint64_t generation = 0);

    [[nodiscard]] std::size_t length() const noexcept;
    [[nodiscard]] std::size_t parts() const noexcept;
    [[nodiscard]] std::size_t missing() const noexcept;
    [[nodiscard]] std::size_t buffered() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] bool flushed() const noexcept;
    [[nodiscard]] std::size_t size(std::size_t block) const;
    [[nodiscard]] std::size_t offset(std::size_t block) const;

    int reserve();
    bool cancel(std::size_t block);
    [[nodiscard]] std::optional<ByteBuffer> get(std::size_t block);
    bool set(std::uint64_t generation, std::size_t block, ByteBuffer data);
    [[nodiscard]] std::optional<ByteBuffer> flush();

private:
    bool init();

    std::size_t length_ = 0;
    std::size_t parts_ = 0;
    std::size_t remainder_ = 0;
    std::size_t missing_ = 0;
    std::size_t buffered_ = 0;
    std::uint64_t generation_ = 0;
    std::vector<std::optional<ByteBuffer>> blocks_;
    std::vector<std::size_t> cancellations_;
    std::size_t reservations_ = 0;
    bool initialized_ = false;
    bool flushed_ = false;
};

} // namespace server1::policy
