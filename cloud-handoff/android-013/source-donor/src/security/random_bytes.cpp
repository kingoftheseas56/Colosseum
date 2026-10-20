#include "security/random_bytes.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace nuvio::security {
namespace {

void fill_random(std::vector<std::uint8_t>& bytes) {
#if defined(_WIN32)
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto chunk = static_cast<ULONG>(std::min(
            remaining,
            static_cast<std::size_t>(std::numeric_limits<ULONG>::max())
        ));
        const auto status = BCryptGenRandom(
            nullptr,
            bytes.data() + offset,
            chunk,
            BCRYPT_USE_SYSTEM_PREFERRED_RNG
        );
        if (!BCRYPT_SUCCESS(status)) {
            throw std::runtime_error("system random generator failed");
        }
        offset += chunk;
    }
#else
    auto flags = O_RDONLY;
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
#if defined(O_NOFOLLOW)
    flags |= O_NOFOLLOW;
#endif
    const auto random_file = open("/dev/urandom", flags);
    if (random_file < 0) {
        throw std::system_error(errno, std::system_category(), "open system random source");
    }
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = read(random_file, bytes.data() + offset, bytes.size() - offset);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            const auto error = errno;
            close(random_file);
            throw std::system_error(error, std::system_category(), "read system random source");
        }
        if (count == 0) {
            close(random_file);
            throw std::runtime_error("system random source ended unexpectedly");
        }
        offset += static_cast<std::size_t>(count);
    }
    if (close(random_file) != 0) {
        throw std::system_error(errno, std::system_category(), "close system random source");
    }
#endif
}

}

std::string random_hex_token(const std::size_t byte_count) {
    if (byte_count == 0 || byte_count > 1024) {
        throw std::invalid_argument("random token size is out of range");
    }
    std::vector<std::uint8_t> bytes(byte_count);
    fill_random(bytes);
    constexpr std::array<char, 16> alphabet{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    };
    std::string token;
    token.resize(byte_count * 2);
    for (std::size_t index = 0; index < byte_count; ++index) {
        token[index * 2] = alphabet[bytes[index] >> 4U];
        token[index * 2 + 1] = alphabet[bytes[index] & 0x0fU];
    }
    return token;
}

}
