#include <QCryptographicHash>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace server1::policy {

using ByteVector = std::vector<std::uint8_t>;

class MetadataExchange final {
public:
    static constexpr std::size_t kChunkSize = 16384;
    static constexpr std::size_t kMaxMetadataSize = 4 * 1024 * 1024;

    explicit MetadataExchange(std::string infoHash)
        : infoHash_(std::move(infoHash))
    {
    }

    bool advertise(std::size_t size)
    {
        if (size == 0 || size > kMaxMetadataSize)
            return false;
        size_ = size;
        chunks_.clear();
        chunks_.resize((size + kChunkSize - 1) / kChunkSize);
        metadata_.reset();
        return true;
    }

    [[nodiscard]] std::vector<std::size_t> pendingRequests() const
    {
        std::vector<std::size_t> pending;
        for (std::size_t index = 0; index < chunks_.size(); ++index) {
            if (!chunks_[index].has_value())
                pending.push_back(index);
        }
        return pending;
    }

    bool receive(int piece, ByteVector data)
    {
        if (piece < 0 || static_cast<std::size_t>(piece) >= chunks_.size()
            || metadata_.has_value())
            return false;
        const auto index = static_cast<std::size_t>(piece);
        const auto expected = index + 1 == chunks_.size() ? size_ - index * kChunkSize
                                                          : kChunkSize;
        if (data.size() != expected)
            return false;
        chunks_[index] = std::move(data);
        if (!pendingRequests().empty())
            return false;

        ByteVector joined;
        joined.reserve(size_);
        for (const auto &chunk : chunks_)
            joined.insert(joined.end(), chunk->begin(), chunk->end());
        const QByteArray bytes(reinterpret_cast<const char *>(joined.data()),
                               static_cast<qsizetype>(joined.size()));
        const auto actual = QCryptographicHash::hash(bytes, QCryptographicHash::Sha1)
                                .toHex().toStdString();
        if (actual != infoHash_) {
            ++rejections_;
            for (auto &chunk : chunks_)
                chunk.reset();
            return false;
        }
        metadata_ = std::move(joined);
        return true;
    }

    [[nodiscard]] const std::optional<ByteVector> &metadata() const noexcept
    {
        return metadata_;
    }

    [[nodiscard]] std::size_t rejections() const noexcept { return rejections_; }

private:
    std::string infoHash_;
    std::size_t size_ = 0;
    std::vector<std::optional<ByteVector>> chunks_;
    std::optional<ByteVector> metadata_;
    std::size_t rejections_ = 0;
};

} // namespace server1::policy
