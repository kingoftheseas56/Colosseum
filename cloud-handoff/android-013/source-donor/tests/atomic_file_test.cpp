#include "test_support.hpp"

#include "storage/atomic_file.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("nuvio-engine-atomic-file-" + std::to_string(nonce));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

}

NUVIO_TEST("atomic state files replace complete contents") {
    TemporaryDirectory directory;
    const auto path = directory.path() / "nested" / "session.state";
    const std::string first = "first-state";
    const std::string second = "replacement-state";

    nuvio::storage::write_file_atomically(path, std::span<const char>(first));
    nuvio::storage::write_file_atomically(path, std::span<const char>(second));

    const auto loaded = nuvio::storage::read_bounded_file(path, 1024);
    NUVIO_EXPECT_TRUE(loaded.has_value());
    NUVIO_EXPECT_EQ(std::string(loaded->begin(), loaded->end()), second);
    NUVIO_EXPECT_TRUE(!std::filesystem::exists(path.string() + ".tmp"));
}

NUVIO_TEST("bounded state reads reject oversized input") {
    TemporaryDirectory directory;
    const auto path = directory.path() / "resume.data";
    constexpr std::array<char, 8> contents{'o', 'v', 'e', 'r', 's', 'i', 'z', 'e'};
    nuvio::storage::write_file_atomically(path, contents);

    bool rejected = false;
    try {
        static_cast<void>(nuvio::storage::read_bounded_file(path, 4));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    NUVIO_EXPECT_TRUE(rejected);
}

NUVIO_TEST("missing and removed state files are idempotent") {
    TemporaryDirectory directory;
    const auto path = directory.path() / "missing.state";
    NUVIO_EXPECT_TRUE(!nuvio::storage::read_bounded_file(path, 1024).has_value());
    nuvio::storage::remove_file_if_present(path);

    const std::string contents = "state";
    nuvio::storage::write_file_atomically(path, std::span<const char>(contents));
    nuvio::storage::remove_file_if_present(path);
    nuvio::storage::remove_file_if_present(path);
    NUVIO_EXPECT_TRUE(!std::filesystem::exists(path));
}
