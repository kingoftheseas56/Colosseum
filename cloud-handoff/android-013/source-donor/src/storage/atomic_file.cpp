#include "storage/atomic_file.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace nuvio::storage {
namespace {

std::filesystem::path temporary_path(const std::filesystem::path& path) {
    auto temporary = path;
    temporary += ".tmp";
    return temporary;
}

void throw_system_error(const int error, const char* const operation) {
    throw std::system_error(error, std::system_category(), operation);
}

#if defined(_WIN32)

void write_all(const HANDLE file, std::span<const char> contents) {
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const auto remaining = contents.size() - offset;
        const auto chunk = static_cast<DWORD>(std::min(
            remaining,
            static_cast<std::size_t>(std::numeric_limits<DWORD>::max())
        ));
        DWORD written = 0;
        if (!WriteFile(file, contents.data() + offset, chunk, &written, nullptr)) {
            throw_system_error(static_cast<int>(GetLastError()), "write state file");
        }
        if (written == 0) {
            throw std::runtime_error("state file write made no progress");
        }
        offset += written;
    }
}

void commit_file(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination,
    std::span<const char> contents
) {
    const auto file = CreateFileW(
        temporary.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (file == INVALID_HANDLE_VALUE) {
        throw_system_error(static_cast<int>(GetLastError()), "create state file");
    }
    try {
        write_all(file, contents);
        if (!FlushFileBuffers(file)) {
            throw_system_error(static_cast<int>(GetLastError()), "flush state file");
        }
    } catch (...) {
        CloseHandle(file);
        throw;
    }
    if (!CloseHandle(file)) {
        throw_system_error(static_cast<int>(GetLastError()), "close state file");
    }
    if (!MoveFileExW(
            temporary.c_str(),
            destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
        )) {
        throw_system_error(static_cast<int>(GetLastError()), "replace state file");
    }
}

#else

void write_all(const int file, std::span<const char> contents) {
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const auto result = ::write(file, contents.data() + offset, contents.size() - offset);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw_system_error(errno, "write state file");
        }
        if (result == 0) {
            throw std::runtime_error("state file write made no progress");
        }
        offset += static_cast<std::size_t>(result);
    }
}

void sync_parent_directory(const std::filesystem::path& destination) {
    auto parent = destination.parent_path();
    if (parent.empty()) {
        parent = ".";
    }
    int flags = O_RDONLY;
#if defined(O_DIRECTORY)
    flags |= O_DIRECTORY;
#endif
    const auto directory = ::open(parent.c_str(), flags);
    if (directory < 0) {
        throw_system_error(errno, "open state directory");
    }
    if (::fsync(directory) != 0) {
        const auto error = errno;
        ::close(directory);
        throw_system_error(error, "flush state directory");
    }
    if (::close(directory) != 0) {
        throw_system_error(errno, "close state directory");
    }
}

void commit_file(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination,
    std::span<const char> contents
) {
    auto flags = O_WRONLY | O_CREAT | O_EXCL;
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#endif
    const auto file = ::open(temporary.c_str(), flags, S_IRUSR | S_IWUSR);
    if (file < 0) {
        throw_system_error(errno, "create state file");
    }
    try {
        write_all(file, contents);
        if (::fsync(file) != 0) {
            throw_system_error(errno, "flush state file");
        }
    } catch (...) {
        ::close(file);
        throw;
    }
    if (::close(file) != 0) {
        throw_system_error(errno, "close state file");
    }
    if (::rename(temporary.c_str(), destination.c_str()) != 0) {
        throw_system_error(errno, "replace state file");
    }
    sync_parent_directory(destination);
}

#endif

}

std::optional<std::vector<char>> read_bounded_file(
    const std::filesystem::path& path,
    const std::size_t maximum_size
) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory ||
        status.type() == std::filesystem::file_type::not_found) {
        return std::nullopt;
    }
    if (error) {
        throw std::system_error(error, "inspect state file");
    }
    if (status.type() != std::filesystem::file_type::regular) {
        throw std::runtime_error("state file is not a regular file");
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        throw std::system_error(error, "inspect state file");
    }
    if (size > maximum_size) {
        throw std::runtime_error("state file exceeds configured size limit");
    }
    if (size > static_cast<std::uintmax_t>(
            std::numeric_limits<std::streamsize>::max()
        )) {
        throw std::runtime_error("state file exceeds stream size limit");
    }
    std::vector<char> contents(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("open state file for reading failed");
    }
    if (!contents.empty()) {
        input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (input.gcount() != static_cast<std::streamsize>(contents.size())) {
            throw std::runtime_error("state file changed while being read");
        }
    }
    if (input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("state file changed while being read");
    }
    return contents;
}

void write_file_atomically(
    const std::filesystem::path& path,
    const std::span<const char> contents
) {
    if (path.empty() || path.filename().empty()) {
        throw std::invalid_argument("state file path must include a filename");
    }
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    const auto temporary = temporary_path(path);
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    try {
        commit_file(temporary, path, contents);
    } catch (...) {
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

void remove_file_if_present(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove(path, error);
    if (error && error != std::errc::no_such_file_or_directory) {
        throw std::system_error(error, "remove state file");
    }
}

}
