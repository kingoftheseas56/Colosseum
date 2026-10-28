#include "server1/policy/MetadataExchange.h"
#include <QCryptographicHash>
#include <utility>

namespace server1::policy {
MetadataExchange::MetadataExchange(std::string infoHash) : infoHash_(std::move(infoHash)) {}
bool MetadataExchange::advertise(std::size_t size)
{
    if (size == 0 || size > kMaxMetadataSize) return false;
    size_ = size; chunks_.assign((size + kChunkSize - 1) / kChunkSize, std::nullopt);
    metadata_.reset(); return true;
}
std::vector<std::size_t> MetadataExchange::pendingRequests() const
{
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < chunks_.size(); ++i) if (!chunks_[i]) result.push_back(i);
    return result;
}
bool MetadataExchange::receive(int piece, ByteVector data)
{
    if (piece < 0 || static_cast<std::size_t>(piece) >= chunks_.size() || metadata_) return false;
    const auto index = static_cast<std::size_t>(piece);
    const auto expected = index + 1 == chunks_.size() ? size_ - index * kChunkSize : kChunkSize;
    if (data.size() != expected) return false;
    chunks_[index] = std::move(data);
    if (!pendingRequests().empty()) return false;
    ByteVector joined; joined.reserve(size_);
    for (const auto &chunk : chunks_) joined.insert(joined.end(), chunk->begin(), chunk->end());
    const QByteArray input(reinterpret_cast<const char *>(joined.data()), static_cast<qsizetype>(joined.size()));
    if (QCryptographicHash::hash(input, QCryptographicHash::Sha1).toHex().toStdString() != infoHash_) {
        ++rejections_; for (auto &chunk : chunks_) chunk.reset(); return false;
    }
    metadata_ = std::move(joined); return true;
}
const std::optional<ByteVector> &MetadataExchange::metadata() const noexcept { return metadata_; }
std::size_t MetadataExchange::rejections() const noexcept { return rejections_; }
}
