#include "integration/LibtorrentBlockTransport.h"

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

std::shared_ptr<lt::torrent_info> torrentInfo(
    const std::vector<char> &payload,
    const std::filesystem::path &root)
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
                                lt::create_torrent::v1_only);
    lt::set_piece_hashes(creator, (root / "seed").string());
    const auto encoded = creator.generate_buf();
    lt::error_code error;
    auto info = std::make_shared<lt::torrent_info>(
        encoded.data(), static_cast<int>(encoded.size()), error);
    require(!error && info, "fixture torrent metadata decode failed");
    return info;
}

lt::session_params sessionParams(const int port)
{
    lt::settings_pack settings;
    settings.set_str(lt::settings_pack::listen_interfaces,
                     "127.0.0.1:" + std::to_string(port));
    settings.set_bool(lt::settings_pack::enable_dht, false);
    settings.set_bool(lt::settings_pack::enable_lsd, false);
    settings.set_bool(lt::settings_pack::enable_upnp, false);
    settings.set_bool(lt::settings_pack::enable_natpmp, false);
    settings.set_int(lt::settings_pack::unchoke_slots_limit, -1);
    return lt::session_params(settings);
}

class LoopbackFixture final
{
public:
    LoopbackFixture()
        : payload_(makePayload()),
          root_(std::filesystem::temp_directory_path()
                / ("colosseum-arc44-libtorrent-"
                   + std::to_string(std::chrono::steady_clock::now()
                                       .time_since_epoch().count()))),
          info_(torrentInfo(payload_, root_)),
          seed_(sessionParams(0)),
          client_(sessionParams(0))
    {
        std::filesystem::create_directories(root_ / "seed");
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
        const auto port = seed_.listen_port();
        clientHandle_.connect_peer({address, port});
    }

    ~LoopbackFixture() = default;

    [[nodiscard]] const std::vector<char> &payload() const { return payload_; }
    [[nodiscard]] lt::torrent_handle &clientHandle() { return clientHandle_; }
    [[nodiscard]] lt::session &seedSession() { return seed_; }

private:
    static std::vector<char> makePayload()
    {
        std::vector<char> payload(BlockSize * 3);
        for (std::size_t i = 0; i < payload.size(); ++i)
            payload[i] = static_cast<char>((i * 37 + 11) & 0xff);
        return payload;
    }

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

void testRealBlockAndCancellation()
{
    LoopbackFixture fixture;
    const auto peerIdentity = std::string("127.0.0.1:")
        + std::to_string(fixture.seedSession().listen_port());

    LibtorrentBlockTransport transport(fixture.clientHandle());
    const WireBlock first{0, 0, BlockSize};
    bool called = false;
    std::error_code callbackError;
    std::vector<std::byte> bytes;
    transport.requestBlock(peerIdentity, first,
        [&](std::error_code error, std::vector<std::byte> result) {
            called = true;
            callbackError = error;
            bytes = std::move(result);
        });

    require(waitFor([&] {
        transport.pumpResults();
        return called;
    }, 10000), "first block callback timed out");
    require(!callbackError, "first block callback failed");
    require(bytes.size() == BlockSize, "first block length mismatch");
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto payloadOffset = first.piece * BlockSize + i;
        require(bytes[i] == std::byte{
                     static_cast<unsigned char>(fixture.payload()[payloadOffset])},
                 "first block bytes mismatch");
    }

    const WireBlock cancelledBlock{1, 0, BlockSize};
    bool cancellationCallback = false;
    transport.requestBlock(peerIdentity, cancelledBlock,
        [&](std::error_code, std::vector<std::byte>) {
            cancellationCallback = true;
        });
    transport.cancelBlock(peerIdentity, cancelledBlock);
    waitFor([&] {
        transport.pumpResults();
        return cancellationCallback;
    }, 250);
    require(!cancellationCallback, "cancelled block unexpectedly completed");
}

void testPeerFailureIsReported()
{
    LoopbackFixture fixture;

    LibtorrentBlockTransport transport(fixture.clientHandle());
    const WireBlock block{2, 0, BlockSize};
    bool called = false;
    std::error_code callbackError;
    transport.requestBlock("missing-peer", block,
        [&](std::error_code error, std::vector<std::byte>) {
            called = true;
            callbackError = error;
        });
    require(waitFor([&] {
        transport.pumpResults();
        return called;
    }, 5000), "missing-peer callback timed out");
    require(bool(callbackError), "missing-peer callback did not fail");
}

} // namespace

int main()
{
    testRealBlockAndCancellation();
    testPeerFailureIsReported();
    std::puts("LIBTORRENT_BLOCK_TRANSPORT_OK");
    return 0;
}
