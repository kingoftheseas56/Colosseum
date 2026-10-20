#include "test_support.hpp"

#include "nuvio_engine/file_selection.hpp"

using nuvio::torrent::SelectionReason;
using nuvio::torrent::TorrentFile;
using nuvio::torrent::select_file;

namespace {

const std::vector<TorrentFile> files{
    {"Show/readme.txt", 100},
    {"Show/S01E01.mkv", 1'000},
    {"Show/S01E02.mkv", 2'000},
    {"Show/poster.jpg", 5'000},
};

}

NUVIO_TEST("exact filename hint wins over a stale requested index") {
    const auto result = select_file(files, 1, "S01E02.mkv");
    NUVIO_EXPECT_EQ(result.index, std::optional<std::size_t>(2));
    NUVIO_EXPECT_EQ(result.reason, SelectionReason::exact_filename);
}

NUVIO_TEST("canonical requested index wins when the hint does not match") {
    const auto result = select_file(files, 1, "missing.mkv");
    NUVIO_EXPECT_EQ(result.index, std::optional<std::size_t>(1));
    NUVIO_EXPECT_EQ(result.reason, SelectionReason::requested_index);
}

NUVIO_TEST("exact torrent path resolves case-insensitively") {
    const auto result = select_file(files, std::nullopt, "show/s01e01.MKV");
    NUVIO_EXPECT_EQ(result.index, std::optional<std::size_t>(1));
    NUVIO_EXPECT_EQ(result.reason, SelectionReason::exact_path);
}

NUVIO_TEST("exact basename resolves a packed episode") {
    const auto result = select_file(files, std::nullopt, "s01e02.mkv");
    NUVIO_EXPECT_EQ(result.index, std::optional<std::size_t>(2));
    NUVIO_EXPECT_EQ(result.reason, SelectionReason::exact_filename);
}

NUVIO_TEST("largest playable file beats a larger non-media file") {
    const auto result = select_file(files, 99, "");
    NUVIO_EXPECT_EQ(result.index, std::optional<std::size_t>(2));
    NUVIO_EXPECT_EQ(result.reason, SelectionReason::largest_playable_file);
}

NUVIO_TEST("empty metadata produces no selection") {
    const auto result = select_file({}, std::nullopt, "");
    NUVIO_EXPECT_TRUE(!result.index.has_value());
    NUVIO_EXPECT_EQ(result.reason, SelectionReason::no_files);
}
