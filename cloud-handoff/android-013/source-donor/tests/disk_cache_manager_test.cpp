#include "test_support.hpp"

#include "cache/disk_cache_manager.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>

namespace {

class DiskCacheFixture {
public:
    DiskCacheFixture() {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("nuvio-disk-cache-" + std::to_string(nonce));
        outside_ = root_.string() + "-outside";
        std::filesystem::create_directories(root_);
    }

    ~DiskCacheFixture() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
        std::filesystem::remove_all(outside_, ignored);
    }

    void put(const std::string& id, const std::string& bytes) {
        const auto directory = root_ / id;
        std::filesystem::create_directories(directory);
        std::ofstream output(directory / "payload.bin", std::ios::binary);
        output << bytes;
    }

    void put_sparse(const std::string& id, const std::uint64_t logical_size) {
        const auto directory = root_ / id;
        std::filesystem::create_directories(directory);
        std::ofstream output(directory / "payload.bin", std::ios::binary);
        output.seekp(static_cast<std::streamoff>(logical_size - 1));
        output.put('\0');
    }

    [[nodiscard]] const std::filesystem::path& root() const {
        return root_;
    }

    [[nodiscard]] const std::filesystem::path& outside() const {
        return outside_;
    }

private:
    std::filesystem::path root_;
    std::filesystem::path outside_;
};

constexpr auto first_id = "0000000000000000000000000000000000000001";
constexpr auto second_id = "0000000000000000000000000000000000000002";

}

NUVIO_TEST("disk cache evicts inactive hash trees but protects live torrents") {
    DiskCacheFixture fixture;
    fixture.put(first_id, "1234");
    fixture.put(second_id, "5678");
    nuvio::cache::DiskCacheManager cache(fixture.root(), 4);

    const auto stats = cache.enforce({second_id});

    NUVIO_EXPECT_TRUE(!std::filesystem::exists(fixture.root() / first_id));
    NUVIO_EXPECT_TRUE(std::filesystem::exists(fixture.root() / second_id));
    NUVIO_EXPECT_EQ(stats.used_bytes, std::uint64_t(4));
    NUVIO_EXPECT_EQ(stats.protected_bytes, std::uint64_t(4));
    NUVIO_EXPECT_EQ(stats.evictions, std::uint64_t(1));
    NUVIO_EXPECT_TRUE(!stats.over_budget);
}

NUVIO_TEST("disk cache reports protected bytes over budget without deleting them") {
    DiskCacheFixture fixture;
    fixture.put(first_id, "12345");
    nuvio::cache::DiskCacheManager cache(fixture.root(), 4);

    const auto stats = cache.enforce({first_id});

    NUVIO_EXPECT_TRUE(std::filesystem::exists(fixture.root() / first_id));
    NUVIO_EXPECT_EQ(stats.used_bytes, std::uint64_t(5));
    NUVIO_EXPECT_EQ(stats.protected_bytes, std::uint64_t(5));
    NUVIO_EXPECT_TRUE(stats.over_budget);
}

NUVIO_TEST("disk cache accounts sparse payload allocation instead of logical length") {
    DiskCacheFixture fixture;
    constexpr std::uint64_t logical_size = 16 * 1024 * 1024;
    fixture.put_sparse(first_id, logical_size);
    nuvio::cache::DiskCacheManager cache(fixture.root(), logical_size / 2);

    const auto stats = cache.enforce({first_id});

    NUVIO_EXPECT_TRUE(std::filesystem::exists(fixture.root() / first_id));
    if (stats.used_bytes < logical_size) {
        NUVIO_EXPECT_TRUE(stats.used_bytes < logical_size / 2);
        NUVIO_EXPECT_TRUE(!stats.over_budget);
    }
}

NUVIO_TEST("disk cache evicts the least recently touched inactive torrent") {
    DiskCacheFixture fixture;
    fixture.put(first_id, "1234");
    fixture.put(second_id, "5678");
    const auto old_time = std::filesystem::file_time_type::clock::now() -
        std::chrono::hours(1);
    std::filesystem::last_write_time(fixture.root() / first_id, old_time);
    std::filesystem::last_write_time(fixture.root() / second_id, old_time);
    nuvio::cache::DiskCacheManager cache(fixture.root(), 4);
    cache.touch(second_id);

    const auto stats = cache.enforce({});

    NUVIO_EXPECT_TRUE(!std::filesystem::exists(fixture.root() / first_id));
    NUVIO_EXPECT_TRUE(std::filesystem::exists(fixture.root() / second_id));
    NUVIO_EXPECT_EQ(stats.used_bytes, std::uint64_t(4));
    NUVIO_EXPECT_EQ(stats.evictions, std::uint64_t(1));
}

NUVIO_TEST("disk cache supports an immediate reclaim target below its normal capacity") {
    DiskCacheFixture fixture;
    fixture.put(first_id, "1234");
    fixture.put(second_id, "5678");
    nuvio::cache::DiskCacheManager cache(fixture.root(), 8);

    const auto stats = cache.enforce({}, 0);

    NUVIO_EXPECT_TRUE(!std::filesystem::exists(fixture.root() / first_id));
    NUVIO_EXPECT_TRUE(!std::filesystem::exists(fixture.root() / second_id));
    NUVIO_EXPECT_EQ(stats.capacity_bytes, std::uint64_t(8));
    NUVIO_EXPECT_EQ(stats.used_bytes, std::uint64_t(0));
    NUVIO_EXPECT_EQ(stats.evictions, std::uint64_t(2));
    NUVIO_EXPECT_TRUE(!stats.over_budget);
}

NUVIO_TEST("disk cache measurement reports usage without evicting") {
    DiskCacheFixture fixture;
    fixture.put(first_id, "1234");
    fixture.put(second_id, "5678");
    nuvio::cache::DiskCacheManager cache(fixture.root(), 4);

    const auto stats = cache.measure({second_id});

    NUVIO_EXPECT_TRUE(std::filesystem::exists(fixture.root() / first_id));
    NUVIO_EXPECT_TRUE(std::filesystem::exists(fixture.root() / second_id));
    NUVIO_EXPECT_EQ(stats.used_bytes, std::uint64_t(8));
    NUVIO_EXPECT_EQ(stats.protected_bytes, std::uint64_t(4));
    NUVIO_EXPECT_EQ(stats.evictions, std::uint64_t(0));
    NUVIO_EXPECT_TRUE(stats.over_budget);
}

NUVIO_TEST("disk cache measurement rescans protected torrents and reuses inactive sizes") {
    DiskCacheFixture fixture;
    fixture.put(first_id, "1234");
    fixture.put(second_id, "5678");
    nuvio::cache::DiskCacheManager cache(fixture.root(), 100);
    static_cast<void>(cache.enforce({}));
    fixture.put(first_id, "12345678");
    fixture.put(second_id, "12345678");

    const auto stats = cache.measure({second_id});

    NUVIO_EXPECT_EQ(stats.used_bytes, std::uint64_t(12));
    NUVIO_EXPECT_EQ(stats.protected_bytes, std::uint64_t(8));
}

NUVIO_TEST("disk cache measurement drops payload trees that disappeared") {
    DiskCacheFixture fixture;
    fixture.put(first_id, "1234");
    fixture.put(second_id, "5678");
    nuvio::cache::DiskCacheManager cache(fixture.root(), 100);
    NUVIO_EXPECT_EQ(cache.measure({}).used_bytes, std::uint64_t(8));
    std::filesystem::remove_all(fixture.root() / second_id);

    const auto stats = cache.measure({});

    NUVIO_EXPECT_EQ(stats.used_bytes, std::uint64_t(4));
    NUVIO_EXPECT_EQ(stats.protected_bytes, std::uint64_t(0));
}

NUVIO_TEST("disk cache scan never follows a hash-shaped symlink") {
    DiskCacheFixture fixture;
    const auto outside = fixture.outside();
    std::filesystem::create_directories(outside);
    {
        std::ofstream output(outside / "valuable.bin", std::ios::binary);
        output << "keep";
    }
    std::error_code error;
    std::filesystem::create_directory_symlink(outside, fixture.root() / first_id, error);
    if (error) {
        return;
    }
    nuvio::cache::DiskCacheManager cache(fixture.root(), 0);

    bool rejected = false;
    try {
        static_cast<void>(cache.enforce({}));
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    NUVIO_EXPECT_TRUE(rejected);
    NUVIO_EXPECT_TRUE(std::filesystem::exists(outside / "valuable.bin"));
}
