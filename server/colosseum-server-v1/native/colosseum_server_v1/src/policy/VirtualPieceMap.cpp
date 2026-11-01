#include "server1/policy/TorrentMetadata.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace server1::policy {
namespace {

std::size_t pieceCount(std::uint64_t totalLength, std::uint64_t pieceLength)
{
    if (totalLength == 0)
        return 0;
    const std::uint64_t count = (totalLength - 1) / pieceLength + 1;
    if (count > std::numeric_limits<std::size_t>::max())
        throw std::overflow_error("piece count exceeds native size");
    return static_cast<std::size_t>(count);
}

} // namespace

VirtualPieceMap::VirtualPieceMap(std::uint64_t totalLength,
                                 std::uint64_t verificationPieceLength,
                                 std::uint64_t virtualPieceLength,
                                 std::uint64_t wireBlockLength,
                                 bool virtualized,
                                 std::vector<VerificationPieceCoordinate> verificationPieces,
                                 std::vector<VirtualPieceCoordinate> virtualPieces,
                                 std::vector<WireBlockCoordinate> wireBlocks)
    : totalLength_(totalLength)
    , verificationPieceLength_(verificationPieceLength)
    , virtualPieceLength_(virtualPieceLength)
    , wireBlockLength_(wireBlockLength)
    , virtualized_(virtualized)
    , verificationPieces_(std::move(verificationPieces))
    , virtualPieces_(std::move(virtualPieces))
    , wireBlocks_(std::move(wireBlocks))
{
}

VirtualPieceMap VirtualPieceMap::create(std::uint64_t totalLength,
                                        std::uint64_t verificationPieceLength,
                                        std::uint64_t wireBlockLength)
{
    if (verificationPieceLength == 0)
        throw std::invalid_argument("verification piece length must be positive");
    if (wireBlockLength == 0)
        throw std::invalid_argument("wire block length must be positive");

    const bool virtualized = verificationPieceLength > kVirtualPieceLength
                             && verificationPieceLength % kVirtualPieceLength == 0;
    const std::uint64_t virtualPieceLength = virtualized ? kVirtualPieceLength
                                                         : verificationPieceLength;

    const std::size_t verificationCount = pieceCount(totalLength, verificationPieceLength);
    std::vector<VerificationPieceCoordinate> verificationPieces;
    verificationPieces.reserve(verificationCount);
    for (std::size_t index = 0; index < verificationCount; ++index) {
        const std::uint64_t offset = static_cast<std::uint64_t>(index) * verificationPieceLength;
        verificationPieces.push_back({VerificationPieceIndex {index},
                                       offset,
                                       std::min(verificationPieceLength, totalLength - offset)});
    }

    const std::size_t virtualCount = pieceCount(totalLength, virtualPieceLength);
    std::vector<VirtualPieceCoordinate> virtualPieces;
    virtualPieces.reserve(virtualCount);
    std::vector<WireBlockCoordinate> wireBlocks;
    for (std::size_t index = 0; index < virtualCount; ++index) {
        const std::uint64_t offset = static_cast<std::uint64_t>(index) * virtualPieceLength;
        const std::uint64_t length = std::min(virtualPieceLength, totalLength - offset);
        const VerificationPieceIndex verificationPiece {
            static_cast<std::size_t>(offset / verificationPieceLength)};
        const std::uint64_t verificationOffset = offset % verificationPieceLength;
        virtualPieces.push_back({VirtualPieceIndex {index},
                                 verificationPiece,
                                 offset,
                                 verificationOffset,
                                 length});

        for (std::uint64_t blockOffset = 0; blockOffset < length;) {
            const std::uint64_t blockLength = std::min(wireBlockLength, length - blockOffset);
            wireBlocks.push_back({VirtualPieceIndex {index}, blockOffset, blockLength});
            blockOffset += blockLength;
        }
    }

    return VirtualPieceMap(totalLength,
                           verificationPieceLength,
                           virtualPieceLength,
                           wireBlockLength,
                           virtualized,
                           std::move(verificationPieces),
                           std::move(virtualPieces),
                           std::move(wireBlocks));
}

bool VirtualPieceMap::isVirtualized() const noexcept
{
    return virtualized_;
}

std::uint64_t VirtualPieceMap::totalLength() const noexcept
{
    return totalLength_;
}

std::uint64_t VirtualPieceMap::verificationPieceLength() const noexcept
{
    return verificationPieceLength_;
}

std::uint64_t VirtualPieceMap::virtualPieceLength() const noexcept
{
    return virtualPieceLength_;
}

std::uint64_t VirtualPieceMap::wireBlockLength() const noexcept
{
    return wireBlockLength_;
}

const std::vector<VerificationPieceCoordinate> &VirtualPieceMap::verificationPieces() const noexcept
{
    return verificationPieces_;
}

const std::vector<VirtualPieceCoordinate> &VirtualPieceMap::virtualPieces() const noexcept
{
    return virtualPieces_;
}

const std::vector<WireBlockCoordinate> &VirtualPieceMap::wireBlocks() const noexcept
{
    return wireBlocks_;
}

} // namespace server1::policy
