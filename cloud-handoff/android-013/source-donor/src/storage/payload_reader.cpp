#include "storage/payload_reader.hpp"

#include <algorithm>
#include <cerrno>
#include <limits>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace nuvio::storage {

PayloadReader::PayloadReader(const std::filesystem::path& path) {
#if defined(_WIN32)
    const auto handle = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (handle != INVALID_HANDLE_VALUE) {
        handle_ = handle;
    }
#else
    int flags = O_RDONLY;
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
    do {
        descriptor_ = ::open(path.c_str(), flags);
    } while (descriptor_ < 0 && errno == EINTR);
#endif
}

PayloadReader::~PayloadReader() {
    close();
}

PayloadReader::PayloadReader(PayloadReader&& other) noexcept {
    *this = std::move(other);
}

PayloadReader& PayloadReader::operator=(PayloadReader&& other) noexcept {
    if (this != &other) {
        close();
#if defined(_WIN32)
        handle_ = std::exchange(other.handle_, nullptr);
#else
        descriptor_ = std::exchange(other.descriptor_, -1);
#endif
    }
    return *this;
}

bool PayloadReader::is_open() const {
#if defined(_WIN32)
    return handle_ != nullptr;
#else
    return descriptor_ >= 0;
#endif
}

bool PayloadReader::read_exact(
    const std::uint64_t offset,
    const std::span<char> destination
) const {
    if (!is_open()) {
        return false;
    }
    std::size_t done = 0;
    while (done < destination.size()) {
        const auto position = offset + done;
        const auto remaining = destination.size() - done;
#if defined(_WIN32)
        const auto chunk = static_cast<DWORD>(std::min(
            remaining,
            static_cast<std::size_t>(std::numeric_limits<DWORD>::max())
        ));
        OVERLAPPED overlapped{};
        overlapped.Offset = static_cast<DWORD>(position & 0xffffffffULL);
        overlapped.OffsetHigh = static_cast<DWORD>(position >> 32U);
        DWORD read = 0;
        if (!ReadFile(handle_, destination.data() + done, chunk, &read, &overlapped) ||
            read == 0) {
            return false;
        }
#else
        if (position > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
            return false;
        }
        const auto read = ::pread(
            descriptor_,
            destination.data() + done,
            remaining,
            static_cast<off_t>(position)
        );
        if (read < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (read == 0) {
            return false;
        }
#endif
        done += static_cast<std::size_t>(read);
    }
    return true;
}

void PayloadReader::close() {
#if defined(_WIN32)
    if (handle_ != nullptr) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
#else
    if (descriptor_ >= 0) {
        ::close(descriptor_);
        descriptor_ = -1;
    }
#endif
}

}
