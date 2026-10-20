#include "test_support.hpp"

#include "security/random_bytes.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

NUVIO_TEST("stream tokens use independent system-random lowercase hex") {
    const auto first = nuvio::security::random_hex_token(32);
    const auto second = nuvio::security::random_hex_token(32);
    NUVIO_EXPECT_EQ(first.size(), std::size_t(64));
    NUVIO_EXPECT_EQ(second.size(), std::size_t(64));
    NUVIO_EXPECT_TRUE(first != second);
    NUVIO_EXPECT_TRUE(std::ranges::all_of(first, [](const unsigned char character) {
        return std::isdigit(character) != 0 ||
            (character >= static_cast<unsigned char>('a') &&
             character <= static_cast<unsigned char>('f'));
    }));
}

NUVIO_TEST("stream token generation rejects empty requests") {
    bool rejected = false;
    try {
        static_cast<void>(nuvio::security::random_hex_token(0));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    NUVIO_EXPECT_TRUE(rejected);
}
