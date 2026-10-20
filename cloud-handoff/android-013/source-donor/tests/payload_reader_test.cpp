#include "test_support.hpp"

#include "storage/payload_reader.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("nuvio-engine-payload-reader-" + std::to_string(nonce));
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

std::filesystem::path write_payload(
    const std::filesystem::path& directory,
    const std::string& contents
) {
    const auto path = directory / "payload.bin";
    std::ofstream output(path, std::ios::binary);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return path;
}

}

NUVIO_TEST("payload reader returns exact ranges at arbitrary offsets") {
    TemporaryDirectory directory;
    const auto path = write_payload(directory.path(), "0123456789abcdef");
    const nuvio::storage::PayloadReader reader(path);
    NUVIO_EXPECT_TRUE(reader.is_open());

    std::vector<char> head(4);
    NUVIO_EXPECT_TRUE(reader.read_exact(0, head));
    NUVIO_EXPECT_EQ(std::string(head.begin(), head.end()), std::string("0123"));

    std::vector<char> middle(6);
    NUVIO_EXPECT_TRUE(reader.read_exact(9, middle));
    NUVIO_EXPECT_EQ(std::string(middle.begin(), middle.end()), std::string("9abcde"));
}

NUVIO_TEST("payload reader rejects reads past the written end") {
    TemporaryDirectory directory;
    const auto path = write_payload(directory.path(), "short");
    const nuvio::storage::PayloadReader reader(path);

    std::vector<char> beyond(8);
    NUVIO_EXPECT_TRUE(!reader.read_exact(2, beyond));
    NUVIO_EXPECT_TRUE(!reader.read_exact(64, beyond));
}

NUVIO_TEST("payload reader observes bytes appended by another writer") {
    TemporaryDirectory directory;
    const auto path = write_payload(directory.path(), "abc");
    const nuvio::storage::PayloadReader reader(path);
    {
        std::ofstream append(path, std::ios::binary | std::ios::app);
        append << "def";
    }

    std::vector<char> tail(3);
    NUVIO_EXPECT_TRUE(reader.read_exact(3, tail));
    NUVIO_EXPECT_EQ(std::string(tail.begin(), tail.end()), std::string("def"));
}

NUVIO_TEST("payload reader reports a missing file without throwing") {
    TemporaryDirectory directory;
    const nuvio::storage::PayloadReader reader(directory.path() / "missing.bin");
    NUVIO_EXPECT_TRUE(!reader.is_open());

    std::vector<char> bytes(1);
    NUVIO_EXPECT_TRUE(!reader.read_exact(0, bytes));
}
