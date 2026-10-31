// Build against the exact Android libtorrent archive/defines used by Nuvio.
// Runs on the session thread with a real loopback peer and requests in flight.
// "retired" deterministically reproduces picker retirement before upload mode;
// "active" verifies cancellation still releases blocks from a live picker.
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/peer_connection.hpp>
#include <libtorrent/piece_picker.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent.hpp>
#include <libtorrent/torrent_info.hpp>
#include <boost/asio/post.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <sys/stat.h>

namespace lt = libtorrent;

int main(int argc, char** argv) try {
    if (argc != 5 || (std::string(argv[1]) != "retired" && std::string(argv[1]) != "active"))
        throw std::runtime_error("usage: probe active|retired fixture.torrent own-save-dir loopback-peer-port");
    bool const retire = std::string(argv[1]) == "retired";
    int const port = std::stoi(argv[4]);
    if (port < 1024 || port > 65535) throw std::runtime_error("invalid loopback port");
    mkdir(argv[3], 0700);
    lt::settings_pack settings;
    settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
    settings.set_str(lt::settings_pack::dht_bootstrap_nodes, "");
    settings.set_bool(lt::settings_pack::enable_dht, false);
    settings.set_bool(lt::settings_pack::enable_lsd, false);
    settings.set_bool(lt::settings_pack::enable_upnp, false);
    settings.set_bool(lt::settings_pack::enable_natpmp, false);
    settings.set_bool(lt::settings_pack::enable_outgoing_utp, false);
    settings.set_bool(lt::settings_pack::close_redundant_connections, false);
    settings.set_int(lt::settings_pack::max_pex_peers, 0);
    settings.set_int(lt::settings_pack::alert_mask, 0);
    lt::session session(settings);
    lt::add_torrent_params params;
    params.ti = std::make_shared<lt::torrent_info>(argv[2]);
    params.save_path = argv[3];
    params.flags &= ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed);
    params.flags |= lt::torrent_flags::override_trackers | lt::torrent_flags::override_web_seeds;
    auto handle = session.add_torrent(params);
    handle.connect_peer({lt::make_address("127.0.0.1"), static_cast<unsigned short>(port)});

    for (int attempt = 0; attempt < 120; ++attempt) {
        auto done = std::make_shared<std::promise<int>>();
        auto result = done->get_future();
        boost::asio::post(session.get_context(), [handle, done, retire] {
            try {
                auto torrent = handle.native_handle();
                if (!torrent || !torrent->has_picker()) { done->set_value(0); return; }
                for (auto* peer : *torrent) {
                    // Sent requests must really exist before exercising cancellation.
                    if (peer->download_queue().empty()) continue;
                    lt::piece_block queued;
                    bool added = false;
                    for (int i = 0; i < torrent->torrent_file().num_pieces() && !added; ++i) {
                        queued = lt::piece_block(lt::piece_index_t(i), 0);
                        auto same = [&](lt::pending_block const& p) { return p.block == queued; };
                        if (torrent->have_piece(queued.piece_index)
                                || std::any_of(peer->request_queue().begin(), peer->request_queue().end(), same)
                                || std::any_of(peer->download_queue().begin(), peer->download_queue().end(), same))
                            continue;
                        added = peer->add_request(queued, lt::peer_connection::time_critical);
                    }
                    if (!added) throw std::runtime_error("could not queue an unsent request");
                    auto const sent = peer->download_queue().size();
                    auto const unsent = peer->request_queue().size();
                    if (retire) {
                        // Exercise the real completed-piece retirement operation without
                        // relying on nondeterministic duplicate-request/disk timing.
                        torrent->picker().we_have_all();
                        torrent->maybe_done_flushing();
                        if (torrent->has_picker()) throw std::runtime_error("picker did not retire");
                    }
                    std::printf("{\"phase\":\"before-cancel\",\"retired\":%s,\"sent\":%zu,\"unsent\":%zu}\n",
                                retire ? "true" : "false", sent, unsent);
                    std::fflush(stdout);
                    torrent->set_upload_mode(true);
                    if (!peer->request_queue().empty() || !peer->download_queue().empty())
                        throw std::runtime_error("request queues were not cancelled");
                    if (!retire && torrent->picker().is_requested(queued))
                        throw std::runtime_error("live picker still owns cancelled block");
                    std::printf("{\"phase\":\"after-cancel\",\"passed\":true,\"retired\":%s}\n",
                                retire ? "true" : "false");
                    std::fflush(stdout);
                    done->set_value(1);
                    return;
                }
                done->set_value(0);
            } catch (...) { done->set_exception(std::current_exception()); }
        });
        if (result.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
            throw std::runtime_error("session thread did not respond");
        if (result.get() == 1) return 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    throw std::runtime_error("no real in-flight requests; test is unverified");
} catch (std::exception const& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
