#include "integration/LibtorrentBlockTransport.h"
#include "integration/TorrentPieceSource.h"
#include "scheduler/FileStream.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/download_priority.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_info.hpp>

using namespace colosseum::server::integration;
using namespace colosseum::server::scheduler;

namespace {

constexpr std::size_t BlockSize = 16 * 1024;

[[noreturn]] void fail(const char *message)
{
    std::fprintf(stderr, "FAIL:%s\n", message);
    std::abort();
}

void require(const bool condition, const char *message)
{
    if (!condition)
        fail(message);
}

std::vector<char> makePayload()
{
    std::vector<char> payload(BlockSize * 3 + 5);
    for (std::size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<char>((i * 53 + 17) & 0xff);
    return payload;
}

std::shared_ptr<lt::torrent_info> makeTorrent(
    const std::vector<char> &payload,
    const std::filesystem::path &root,
    const lt::create_flags_t flags = lt::create_torrent::v1_only)
{
    std::filesystem::create_directories(root / "seed");
    std::ofstream file(root / "seed" / "payload.bin",
                       std::ios::binary | std::ios::trunc);
    file.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    require(file.good(), "fixture payload write failed");
    file.close();
    require(file.good(), "fixture payload close failed");

    lt::file_storage files;
    files.add_file("payload.bin", static_cast<std::int64_t>(payload.size()));
    lt::create_torrent creator(files, static_cast<int>(BlockSize),
                                flags);
    lt::set_piece_hashes(creator, (root / "seed").string());
    const auto encoded = creator.generate_buf();
    lt::error_code error;
    auto info = std::make_shared<lt::torrent_info>(
        encoded.data(), static_cast<int>(encoded.size()), error);
    require(!error && info, "fixture torrent metadata decode failed");
    return info;
}

lt::session_params sessionParams()
{
    lt::settings_pack settings;
    settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
    settings.set_bool(lt::settings_pack::enable_dht, false);
    settings.set_bool(lt::settings_pack::enable_lsd, false);
    settings.set_bool(lt::settings_pack::enable_upnp, false);
    settings.set_bool(lt::settings_pack::enable_natpmp, false);
    settings.set_int(lt::settings_pack::unchoke_slots_limit, -1);
    settings.set_bool(lt::settings_pack::close_redundant_connections, false);
    return lt::session_params(settings);
}

class Fixture final
{
public:
    explicit Fixture(const lt::create_flags_t flags = lt::create_torrent::v1_only)
        : payload_(makePayload()),
          root_(std::filesystem::temp_directory_path()
                / ("colosseum-arc44-piece-source-"
                   + std::to_string(std::chrono::steady_clock::now()
                                       .time_since_epoch().count()))),
          info_(makeTorrent(payload_, root_, flags)),
          seed_(sessionParams()),
          client_(sessionParams())
    {
        if (flags == lt::create_torrent::v2_only)
            require(info_->v2() && !info_->v1(),
                    "v2-only fixture metadata must be v2-only");
        std::filesystem::create_directories(root_ / "client");
        lt::error_code error;
        lt::add_torrent_params seedParams;
        seedParams.ti = info_;
        seedParams.save_path = (root_ / "seed").string();
        seedParams.flags &= ~lt::torrent_flags::paused;
        seedHandle_ = seed_.add_torrent(std::move(seedParams), error);
        require(!error && seedHandle_.is_valid(), "seed torrent add failed");

        lt::add_torrent_params clientParams;
        clientParams.ti = info_;
        clientParams.save_path = (root_ / "client").string();
        clientParams.flags |= lt::torrent_flags::paused;
        clientHandle_ = client_.add_torrent(std::move(clientParams), error);
        require(!error && clientHandle_.is_valid(), "client torrent add failed");

        const auto address = lt::make_address("127.0.0.1", error);
        require(!error, "loopback address construction failed");
        clientHandle_.connect_peer({address, seed_.listen_port()});
    }

    [[nodiscard]] const std::vector<char> &payload() const { return payload_; }
    [[nodiscard]] lt::torrent_handle &clientHandle() { return clientHandle_; }
    [[nodiscard]] lt::session &seedSession() { return seed_; }

    void onlyClientPiece(const int piece)
    {
        const auto info = clientHandle_.torrent_file();
        require(static_cast<bool>(info), "client torrent metadata missing");
        for (int index = 0; index < info->num_pieces(); ++index)
            clientHandle_.piece_priority(lt::piece_index_t{index},
                                         lt::dont_download);
        clientHandle_.piece_priority(lt::piece_index_t{piece},
                                     lt::top_priority);
    }

private:
    std::vector<char> payload_;
    std::filesystem::path root_;
    std::shared_ptr<lt::torrent_info> info_;
    lt::session seed_;
    lt::session client_;
    lt::torrent_handle seedHandle_;
    lt::torrent_handle clientHandle_;
};

template <typename Predicate>
bool waitFor(Predicate predicate, const int timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

void testRealPieceReadAndFinalShortPiece()
{
    Fixture fixture;
    const auto peer = std::string("127.0.0.1:")
        + std::to_string(fixture.seedSession().listen_port());
    LibtorrentBlockTransport transport(fixture.clientHandle());
    TorrentPieceSource source(fixture.clientHandle());

    bool cancelledWaitCalled = false;
    const auto cancelledWait = source.waitForPiece(0,
        [&](std::error_code) { cancelledWaitCalled = true; });
    require(cancelledWait != 0 && !cancelledWaitCalled,
            "missing piece must register a source wait");
    source.cancelWait(cancelledWait);
    source.notifyPieceFinished(0);
    require(!cancelledWaitCalled,
            "cancelled source wait must not receive a completion callback");

    fixture.onlyClientPiece(0);
    bool firstCalled = false;
    transport.requestBlock(peer, WireBlock{0, 0, BlockSize},
        [&](std::error_code error, std::vector<std::byte> bytes) {
            require(!error && bytes.size() == BlockSize,
                    "first real piece request failed");
            firstCalled = true;
        });
    require(waitFor([&] {
        transport.pumpResults();
        return firstCalled && fixture.clientHandle().have_piece(lt::piece_index_t{0});
    }, 10000), "first piece did not become verified");

    bool ready = false;
    const auto token = source.waitForPiece(0,
        [&](std::error_code error) {
            require(!error, "piece-ready notification failed");
            ready = true;
        });
    require(token == 0 && ready, "available piece must notify immediately");

    std::error_code error;
    const auto first = source.readBlock(WireBlock{0, 0, BlockSize}, error);
    require(!error && first.size() == BlockSize,
            "source must read an exact verified block");
    for (std::size_t i = 0; i < first.size(); ++i)
        require(first[i] == std::byte{
                     static_cast<unsigned char>(fixture.payload()[i])},
                "source block bytes mismatch");

    bool tailCalled = false;
    fixture.onlyClientPiece(3);
    transport.requestBlock(peer, WireBlock{3, 0, 5},
        [&](std::error_code tailError, std::vector<std::byte> bytes) {
            require(!tailError && bytes.size() == 5,
                    "final short piece request failed");
            tailCalled = true;
        });
    require(waitFor([&] {
        transport.pumpResults();
        return tailCalled && fixture.clientHandle().have_piece(lt::piece_index_t{3});
    }, 10000), "final short piece did not become verified");
    const auto tail = source.readBlock(WireBlock{3, 0, 5}, error);
    require(!error && tail.size() == 5, "final short piece read mismatch");
    for (std::size_t i = 0; i < tail.size(); ++i)
        require(tail[i] == std::byte{
                     static_cast<unsigned char>(fixture.payload()[3 * BlockSize + i])},
                "final short piece bytes mismatch");
}

void testFileStreamDestroyCancelsTorrentWait()
{
    Fixture fixture;
    TorrentPieceSource source(fixture.clientHandle());
    SchedulerSpine scheduler({BlockSize});
    scheduler.markPieceAvailable(0);
    FileStream stream(scheduler, source, FileSpan{0, BlockSize}, BlockSize);
    stream.start();
    require(stream.inFlight() == 1, "FileStream must register the real source read");
    stream.destroy();
    require(stream.destroyed() && !scheduler.isPieceLocked(0),
            "destroy must release the real source piece lock");
}

void testV2OnlyPieceRead()
{
    Fixture fixture(lt::create_torrent::v2_only);
    const auto peer = std::string("127.0.0.1:")
        + std::to_string(fixture.seedSession().listen_port());
    LibtorrentBlockTransport transport(fixture.clientHandle());
    TorrentPieceSource source(fixture.clientHandle());
    fixture.onlyClientPiece(0);

    bool received = false;
    transport.requestBlock(peer, WireBlock{0, 0, BlockSize},
        [&](std::error_code error, std::vector<std::byte> bytes) {
            require(!error && bytes.size() == BlockSize,
                    "v2-only piece request failed");
            received = true;
        });
    require(waitFor([&] {
        transport.pumpResults();
        return received && fixture.clientHandle().have_piece(lt::piece_index_t{0});
    }, 10000), "v2-only piece did not become verified");

    std::error_code error;
    std::vector<std::byte> bytes;
    bool read = false;
    source.readPiece(0, [&](std::error_code readError,
                            std::vector<std::byte> readBytes) {
        error = readError;
        bytes = std::move(readBytes);
        read = true;
    });
    require(read && !error && bytes.size() == BlockSize,
            "v2-only source must read an exact verified piece");
    for (std::size_t i = 0; i < bytes.size(); ++i)
        require(bytes[i] == std::byte{
                     static_cast<unsigned char>(fixture.payload()[i])},
                "v2-only source block bytes mismatch");
}

} // namespace

int main()
{
    testRealPieceReadAndFinalShortPiece();
    testFileStreamDestroyCancelsTorrentWait();
    testV2OnlyPieceRead();
    std::puts("TORRENT_PIECE_SOURCE_OK");
    return 0;
}
