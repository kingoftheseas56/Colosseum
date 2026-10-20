#include "nuvio_engine/file_selection.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace nuvio::torrent {
namespace {

constexpr std::array<std::string_view, 19> playable_extensions{
    ".aac", ".avi", ".flac", ".flv", ".m3u8", ".m4v", ".mkv",
    ".mov", ".mp3", ".mp4", ".mpeg", ".mpg", ".ogg", ".ts",
    ".vp8", ".wav", ".webm", ".wmv", ".wma",
};

std::string lowercase(std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

std::string_view basename(const std::string_view path) {
    const auto separator = path.find_last_of("/\\");
    return separator == std::string_view::npos ? path : path.substr(separator + 1);
}

std::optional<std::size_t> largest_file(
    const std::vector<TorrentFile>& files,
    const bool playable_only
) {
    std::optional<std::size_t> selected;
    for (std::size_t index = 0; index < files.size(); ++index) {
        if (playable_only && !is_playable_media_path(files[index].path)) {
            continue;
        }
        if (!selected.has_value() || files[index].size > files[*selected].size) {
            selected = index;
        }
    }
    return selected;
}

}

bool is_playable_media_path(const std::string_view path) {
    const auto normalized = lowercase(path);
    return std::ranges::any_of(playable_extensions, [&](const std::string_view extension) {
        return normalized.ends_with(extension);
    });
}

FileSelection select_file(
    const std::vector<TorrentFile>& files,
    const std::optional<std::size_t> requested_index,
    const std::string_view path_or_filename_hint
) {
    if (files.empty()) {
        return {std::nullopt, SelectionReason::no_files};
    }
    if (!path_or_filename_hint.empty()) {
        const auto normalized_hint = lowercase(path_or_filename_hint);
        for (std::size_t index = 0; index < files.size(); ++index) {
            if (lowercase(files[index].path) == normalized_hint) {
                return {index, SelectionReason::exact_path};
            }
        }
        for (std::size_t index = 0; index < files.size(); ++index) {
            if (lowercase(basename(files[index].path)) == normalized_hint) {
                return {index, SelectionReason::exact_filename};
            }
        }
    }

    if (requested_index.has_value() && *requested_index < files.size()) {
        return {requested_index, SelectionReason::requested_index};
    }

    if (const auto playable = largest_file(files, true); playable.has_value()) {
        return {playable, SelectionReason::largest_playable_file};
    }
    return {largest_file(files, false), SelectionReason::largest_file};
}

}
