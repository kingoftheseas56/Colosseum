#include "server1/ports/TorrentTransport.h"

namespace server1::transport {

bool sameOwnership(const ports::RequestOwnership &left,
                   const ports::RequestOwnership &right) noexcept
{
    return left.requestId == right.requestId
        && left.generation == right.generation
        && left.selectionId == right.selectionId;
}

bool sameBlock(const ports::BlockSpan &left, const ports::BlockSpan &right) noexcept
{
    return left.piece == right.piece && left.blockOrdinal == right.blockOrdinal
        && left.offset == right.offset && left.length == right.length;
}

} // namespace server1::transport
