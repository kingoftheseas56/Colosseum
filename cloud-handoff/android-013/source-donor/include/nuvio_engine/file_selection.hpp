#ifndef NUVIO_ENGINE_FILE_SELECTION_HPP
#define NUVIO_ENGINE_FILE_SELECTION_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "nuvio_engine/export.h"

namespace nuvio::torrent {

struct TorrentFile {
    std::string path;
    std::uint64_t size;
};

enum class SelectionReason {
    requested_index,
    exact_path,
    exact_filename,
    largest_playable_file,
    largest_file,
    no_files,
};

struct FileSelection {
    std::optional<std::size_t> index;
    SelectionReason reason;
};

[[nodiscard]] NUVIO_ENGINE_CPP_API FileSelection select_file(
    const std::vector<TorrentFile>& files,
    std::optional<std::size_t> requested_index,
    std::string_view path_or_filename_hint
);

[[nodiscard]] NUVIO_ENGINE_CPP_API bool is_playable_media_path(std::string_view path);

}

#endif
