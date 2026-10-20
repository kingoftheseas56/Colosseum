#ifndef NUVIO_ENGINE_ATOMIC_FILE_HPP
#define NUVIO_ENGINE_ATOMIC_FILE_HPP

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace nuvio::storage {

[[nodiscard]] std::optional<std::vector<char>> read_bounded_file(
    const std::filesystem::path& path,
    std::size_t maximum_size
);

void write_file_atomically(
    const std::filesystem::path& path,
    std::span<const char> contents
);

void remove_file_if_present(const std::filesystem::path& path);

}

#endif
