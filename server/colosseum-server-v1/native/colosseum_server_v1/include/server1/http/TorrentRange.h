#pragma once
#include <cstddef>
#include <optional>
#include <string_view>
namespace server1::http {
struct TorrentRange {std::size_t start;std::size_t end;};
// M172 takes [0] of M176's uncombined range list. Invalid and unsatisfiable
// ranges therefore mean a full response, not 416. The unit is not validated.
std::optional<TorrentRange> firstTorrentRange(std::size_t length,std::string_view header);
}
