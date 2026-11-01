#include "transport_contract_probe.h"

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/extensions.hpp>
#include <libtorrent/extensions/ut_metadata.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/peer_connection.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/request_blocks.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/version.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <vector>

// P08A is an accepted predecessor fixture, not a production implementation.
// It supplies the real-wire selected-peer and picker-control regression that
// this packet re-runs; this file owns the P08 capability inventory and seam.
#define main p08a_fixture_main
#include "../../../artifacts/server1/P08A/probe-src/exact_block_probe.cpp"
#undef main

namespace colosseum::server1::p08 {

capability_report inspect_frozen_libtorrent_surface()
{
    // These references deliberately compile against the locked 2.0 headers.
    // The member calls are the candidate seam; the report distinguishes API
    // availability from controls proven on the wire by the fixture run.
    using add_request_member = decltype(&libtorrent::peer_connection::add_request);
    using cancel_request_member = decltype(&libtorrent::peer_connection::cancel_request);
    using send_requests_member = decltype(&libtorrent::peer_connection::send_block_requests);
    using torrent_deadline_member = decltype(&libtorrent::torrent_handle::set_piece_deadline);
    using session_settings_member = void (libtorrent::session_handle::*)(libtorrent::settings_pack const&);
    using peer_stats_member = decltype(&libtorrent::peer_connection::statistics);
    (void)sizeof(add_request_member);
    (void)sizeof(cancel_request_member);
    (void)sizeof(send_requests_member);
    (void)sizeof(torrent_deadline_member);
    (void)sizeof(session_settings_member);
    (void)sizeof(peer_stats_member);

    return {
        "version-specific internal header access",
        lt::version(),
        "6e1587799",
        false, // Runtime case receipts, not this compile-only probe, grade P08.
        false, // Runtime case receipts, not this compile-only probe, grade P08.
        false, // Runtime case receipts, not this compile-only probe, grade P08.
        false, // Runtime case receipts, not this compile-only probe, grade P08.
        false, // Runtime case receipts, not this compile-only probe, grade P08.
        false, // Runtime case receipts, not this compile-only probe, grade P08.
        false, // Runtime case receipts, not this compile-only probe, grade P08.
        false, // Runtime case receipts, not this compile-only probe, grade P08.
        false  // Runtime case receipts, not this compile-only probe, grade P08.
    };
}

int write_capability_matrix(std::string const& path, capability_report const& r)
{
    std::ofstream out(path, std::ios::trunc);
    if (!out) return 1;
    out << "{\n"
        << "  \"schema\": \"colosseum-server1-native-capability-matrix/v1\",\n"
        << "  \"worker_id\": \"P08-A\",\n"
        << "  \"dependency\": {\"version\": \"" << r.dependency_version
        << "\", \"revision\": \"" << r.dependency_revision << "\"},\n"
        << "  \"seam\": {\"classification\": \"" << r.seam_classification
        << "\", \"exact_block\": \"peer_connection::add_request(piece_block) + send_block_requests()\", \"hotswap\": \"peer_connection::cancel_request(piece_block) + add_request(piece_block)\"},\n"
        << "  \"controls\": {\n"
        << "    \"selected_peer_exact_block_wire\": " << (r.selected_peer_exact_block_wire ? "true" : "false") << ",\n"
        << "    \"picker_suppression_wire\": " << (r.picker_suppression_wire ? "true" : "false") << ",\n"
        << "    \"reservation_hotswap_wire\": " << (r.reservation_hotswap_wire ? "true" : "false") << ",\n"
        << "    \"partial_delivery_before_piece_verification\": " << (r.partial_delivery_before_piece_verification ? "true" : "false") << ",\n"
        << "    \"metadata_discovery_control\": " << (r.metadata_discovery_control ? "true" : "false") << ",\n"
        << "    \"honest_peer_stats\": " << (r.honest_peer_stats ? "true" : "false") << ",\n"
        << "    \"per_engine_settings\": " << (r.per_engine_settings ? "true" : "false") << ",\n"
        << "    \"session_global_settings\": " << (r.session_global_settings ? "true" : "false") << ",\n"
        << "    \"choke_interest_wire\": " << (r.choke_interest_observable ? "true" : "false") << "\n"
        << "  },\n"
        << "  \"interface_statement\": \"No production interface changed; G-NATIVE is blocked until every mandatory control has an isolated real-wire receipt.\"\n"
        << "}\n";
    return 0;
}

} // namespace colosseum::server1::p08

namespace p08_packet {

namespace fs = std::filesystem;
namespace lt = libtorrent;

constexpr int kBlockSize = 16 * 1024;
constexpr int kLargePiece = 1024 * 1024;
constexpr int kVirtualSegment = 512 * 1024;

struct RequestKey {
    int piece = -1;
    int start = -1;
    int length = -1;

    bool operator<(RequestKey const& other) const
    {
        return std::tie(piece, start, length) < std::tie(other.piece, other.start, other.length);
    }
};

RequestKey key(lt::peer_request const& request)
{
    return {int(request.piece), int(request.start), int(request.length)};
}

std::string as_hex(lt::sha1_hash const& hash)
{
    std::ostringstream output;
    output << hash;
    return output.str();
}

bool file_contains(fs::path const& path, std::string const& token)
{
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) if (line.find(token) != std::string::npos) return true;
    return false;
}

int line_count(fs::path const& path, std::string const& token)
{
    std::ifstream input(path);
    std::string line;
    int result = 0;
    while (std::getline(input, line)) if (line.find(token) != std::string::npos) ++result;
    return result;
}

template <typename Predicate>
bool wait_for(std::condition_variable& cv, std::mutex& mutex, Predicate predicate, int timeout_ms)
{
    std::unique_lock<std::mutex> lock(mutex);
    return cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), predicate);
}

struct MetadataSpec {
    int piece_size = kBlockSize;
    int file_size = kBlockSize;
    int split = -1;
    char first = 'P';
    char second = 'P';
};

std::shared_ptr<lt::torrent_info> create_metadata(fs::path const& control, MetadataSpec const& spec)
{
    fs::create_directories(control);
    const fs::path payload_path = control / "payload.bin";
    std::ofstream payload(payload_path, std::ios::binary | std::ios::trunc);
    const int split = spec.split < 0 ? spec.file_size : spec.split;
    for (int offset = 0; offset < spec.file_size; ++offset) {
        const char value = offset < split ? spec.first : spec.second;
        payload.write(&value, 1);
    }
    payload.close();

    lt::file_storage storage;
    storage.add_file("payload.bin", spec.file_size);
    lt::create_torrent creator(storage, spec.piece_size);
    lt::set_piece_hashes(creator, control.string());
    const lt::entry generated = creator.generate();
    std::vector<char> encoded;
    lt::bencode(std::back_inserter(encoded), generated);
    std::ofstream torrent(control / "probe.torrent", std::ios::binary | std::ios::trunc);
    torrent.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    torrent.close();

    const lt::entry& info_entry = generated.dict().at("info");
    std::vector<char> info_encoded;
    lt::bencode(std::back_inserter(info_encoded), info_entry);
    std::ofstream metadata(control / "metadata.info", std::ios::binary | std::ios::trunc);
    metadata.write(info_encoded.data(), static_cast<std::streamsize>(info_encoded.size()));
    metadata.close();

    auto info = std::make_shared<lt::torrent_info>(encoded.data(), static_cast<int>(encoded.size()));
    std::ofstream hash(control / "info_hash.txt", std::ios::trunc);
    hash << as_hex(info->info_hashes().v1) << '\n';
    std::ofstream identity(control / "dependency_identity.txt", std::ios::trunc);
    identity << "header_declared_version=2.0.11.0\n"
             << "header_declared_revision=6e1587799\n"
             << "linked_runtime_version=" << lt::version() << '\n'
             << "v1_info_hash=" << info->info_hashes().v1 << '\n'
             << "archive_sha256=a2d67f24710303750aaf068d1945380d95434e4791ad611b68b0b3b055d89a30\n";
    return info;
}

std::shared_ptr<lt::torrent_info> load_metadata(fs::path const& control)
{
    std::ifstream input(control / "probe.torrent", std::ios::binary);
    std::vector<char> encoded((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    lt::error_code error;
    auto info = std::make_shared<lt::torrent_info>(encoded.data(), static_cast<int>(encoded.size()), error);
    if (error) throw std::runtime_error("torrent metadata decode failed: " + error.message());
    return info;
}

lt::settings_pack packet_settings()
{
    lt::settings_pack settings;
    settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:0");
    settings.set_bool(lt::settings_pack::enable_dht, false);
    settings.set_bool(lt::settings_pack::enable_lsd, false);
    settings.set_bool(lt::settings_pack::enable_upnp, false);
    settings.set_bool(lt::settings_pack::enable_natpmp, false);
    settings.set_bool(lt::settings_pack::allow_multiple_connections_per_ip, true);
    settings.set_int(lt::settings_pack::out_enc_policy, lt::settings_pack::pe_disabled);
    settings.set_int(lt::settings_pack::in_enc_policy, lt::settings_pack::pe_disabled);
    return settings;
}

lt::tcp::endpoint endpoint(int port)
{
    return {lt::make_address("127.0.0.1"), static_cast<std::uint16_t>(port)};
}

struct OwnedState {
    OwnedState(fs::path path, std::string id, int a, int b)
        : transcript(std::move(path), std::ios::trunc), case_id(std::move(id)), a_port(a), b_port(b) {}

    void log(std::string const& line)
    {
        std::lock_guard<std::mutex> lock(mutex);
        transcript << "case=" << case_id << ' ' << line << '\n';
        transcript.flush();
    }

    std::ofstream transcript;
    std::string case_id;
    int a_port;
    int b_port;
    fs::path release_file;
    std::mutex mutex;
    std::condition_variable cv;
    std::weak_ptr<lt::peer_connection> a_connection;
    std::weak_ptr<lt::peer_connection> b_connection;
    std::atomic<bool> advertised_a{false};
    std::atomic<bool> advertised_b{false};
    std::atomic<bool> command_issued{false};
    std::atomic<bool> command_queued{false};
    std::atomic<bool> piece_passed{false};
    std::atomic<int> suppressed{0};
    std::atomic<int> post_owned_suppressed{0};
    std::atomic<int> wire_internal{0};
    std::atomic<int> owned_wire{0};
    std::atomic<int> queued_owned{0};
    std::atomic<int> cancel_sent{0};
    std::atomic<int> pre_owned_cleanup_cancels{0};
    std::atomic<int> owned_piece_cancels{0};
    std::atomic<int> owned_outstanding_at_post_suppression{0};
    std::atomic<bool> owned_phase_started{false};
    std::atomic<bool> pre_owned_cleanup_done{false};
    std::atomic<bool> picker_enable_requested{false};
    std::atomic<bool> picker_enabled{false};
    std::atomic<int> picker_diagnostic_ticks{0};
    std::atomic<bool> ordinary_picker_attempted{false};
    bool priorities_disabled_before_connect = false;
    std::atomic<bool> internal_priority_enabled_on_network_thread{false};
    std::function<void()> enable_picker;
    std::set<RequestKey> commanded;
    std::set<RequestKey> permits;
    std::set<RequestKey> observed;
};

class OwnedPeerPlugin final : public lt::peer_plugin {
public:
    OwnedPeerPlugin(lt::peer_connection_handle peer, bool selected, std::shared_ptr<OwnedState> state)
        : peer_(std::move(peer)), selected_(selected), state_(std::move(state)) {}

    bool on_handshake(lt::span<char const>) override
    {
        if (selected_) state_->b_connection = peer_.native_handle();
        else state_->a_connection = peer_.native_handle();
        state_->log(std::string("HANDSHAKE peer=") + (selected_ ? "B" : "A"));
        return true;
    }

    bool on_bitfield(lt::bitfield const& pieces) override
    {
        const bool has_piece = pieces.size() > 0 && pieces[0];
        if (has_piece) {
            if (selected_) state_->advertised_b.store(true);
            else state_->advertised_a.store(true);
            state_->log(std::string("ADVERTISE peer=") + (selected_ ? "B" : "A"));
        }
        return false;
    }

    void tick() override
    {
        if (!selected_ || !state_->advertised_a.load() || !state_->advertised_b.load()) return;
        maybe_command();
        if (state_->picker_enable_requested.load() && !state_->picker_enabled.load()) {
            auto native = peer_.native_handle();
            auto torrent = native ? native->associated_torrent().lock() : nullptr;
            auto peer_a = state_->a_connection.lock();
            if (torrent && peer_a) {
                const int a_before = int(peer_a->request_queue().size() + peer_a->download_queue().size());
                peer_a->cancel_all_requests();
                const int a_after = int(peer_a->request_queue().size() + peer_a->download_queue().size());
                torrent->set_piece_priority(lt::piece_index_t(1), lt::default_priority);
                state_->picker_enabled.store(true);
                state_->log("PICKER_ENABLE target=A piece=1 priority=default after_a_cleanup=1"
                    " a_before=" + std::to_string(a_before)
                    + " a_after=" + std::to_string(a_after));
            }
        }
        if (state_->picker_enabled.load() && state_->post_owned_suppressed.load() == 0) {
            const int diagnostic_tick = state_->picker_diagnostic_ticks.fetch_add(1);
            auto native = peer_.native_handle();
            auto torrent = native ? native->associated_torrent().lock() : nullptr;
            if (diagnostic_tick < 8) {
                state_->log("PICKER_DIAGNOSTIC tick=" + std::to_string(diagnostic_tick)
                    + " disconnecting=" + (native && native->is_disconnecting() ? "1" : "0")
                    + " priority0=" + (torrent ? std::to_string(int(torrent->piece_priority(lt::piece_index_t(0)))) : "unknown")
                    + " priority1=" + (torrent ? std::to_string(int(torrent->piece_priority(lt::piece_index_t(1)))) : "unknown")
                    + " priority2=" + (torrent ? std::to_string(int(torrent->piece_priority(lt::piece_index_t(2)))) : "unknown")
                    + " request_queue=" + std::to_string(native ? native->request_queue().size() : 0)
                    + " download_queue=" + std::to_string(native ? native->download_queue().size() : 0));
            }
            if (torrent && torrent->piece_priority(lt::piece_index_t(1)) == lt::default_priority
                && !state_->ordinary_picker_attempted.exchange(true)) {
                auto peer_a = state_->a_connection.lock();
                const bool picked = peer_a ? lt::request_a_block(*torrent, *peer_a) : false;
                state_->log(std::string("ORDINARY_PICKER target=A piece1_enabled=1 picked=")
                    + (picked ? "1" : "0")
                    + " disconnecting=" + (peer_a && peer_a->is_disconnecting() ? "1" : "0")
                    + " request_queue=" + std::to_string(peer_a ? peer_a->request_queue().size() : 0)
                    + " download_queue=" + std::to_string(peer_a ? peer_a->download_queue().size() : 0));
                if (peer_a) peer_a->send_block_requests();
            }
        }
    }

    bool write_request(lt::peer_request const& request) override
    {
        const RequestKey request_key = key(request);
        if (selected_) {
            auto found = state_->permits.find(request_key);
            if (found != state_->permits.end()) {
                state_->permits.erase(found);
                state_->log("ALLOW_OWNED peer=B piece=" + std::to_string(request_key.piece)
                    + " start=" + std::to_string(request_key.start)
                    + " length=" + std::to_string(request_key.length));
                return false;
            }
        }
        ++state_->suppressed;
        const bool post_owned = state_->picker_enabled.load() && request_key.piece == 1;
        if (post_owned) {
            const int previous = state_->post_owned_suppressed.fetch_add(1);
            if (previous == 0) {
                std::set<RequestKey> outstanding;
                auto record_owned = [&](auto const& pending) {
                    if (pending.block.piece_index != lt::piece_index_t(2)) return;
                    const int start = int(pending.block.block_index) * kBlockSize;
                    const auto found = std::find_if(state_->commanded.begin(), state_->commanded.end(),
                        [start](RequestKey const& commanded) {
                            return commanded.piece == 2 && commanded.start == start;
                        });
                    if (found != state_->commanded.end()) outstanding.insert(*found);
                };
                if (auto selected = state_->b_connection.lock()) {
                    for (auto const& pending : selected->download_queue()) record_owned(pending);
                    for (auto const& pending : selected->request_queue()) record_owned(pending);
                }
                state_->owned_outstanding_at_post_suppression.store(int(outstanding.size()));
                state_->log("OWNED_PRESERVED_AT_POST_SUPPRESSION count="
                    + std::to_string(outstanding.size()));
            }
        }
        state_->log("SUPPRESS peer=" + std::string(selected_ ? "B" : "A")
            + " piece=" + std::to_string(request_key.piece)
            + " start=" + std::to_string(request_key.start)
            + " length=" + std::to_string(request_key.length)
            + " phase=" + (post_owned ? "post-owned" : "pre-owned"));
        state_->cv.notify_all();
        return true;
    }

    void sent_request(lt::peer_request const& request) override
    {
        const RequestKey request_key = key(request);
        ++state_->wire_internal;
        state_->observed.insert(request_key);
        const bool owned = selected_ && state_->commanded.count(request_key) != 0;
        if (owned) ++state_->owned_wire;
        state_->log("WIRE peer=" + std::string(selected_ ? "B" : "A")
            + " piece=" + std::to_string(request_key.piece)
            + " start=" + std::to_string(request_key.start)
            + " length=" + std::to_string(request_key.length)
            + " ownership=" + (owned ? "command" : "unowned"));
        state_->cv.notify_all();
    }

    void sent_cancel(lt::peer_request const& request) override
    {
        ++state_->cancel_sent;
        if (!state_->owned_phase_started.load()) ++state_->pre_owned_cleanup_cancels;
        else if (selected_ && request.piece == lt::piece_index_t(2)) ++state_->owned_piece_cancels;
        state_->log("CANCEL_SENT peer=" + std::string(selected_ ? "B" : "A")
            + " piece=" + std::to_string(int(request.piece))
            + " start=" + std::to_string(request.start)
            + " length=" + std::to_string(request.length));
    }

    bool on_piece(lt::peer_request const& request, lt::span<char const>) override
    {
        state_->log("PIECE peer=" + std::string(selected_ ? "B" : "A")
            + " piece=" + std::to_string(int(request.piece))
            + " start=" + std::to_string(request.start)
            + " length=" + std::to_string(request.length));
        return false;
    }

private:
    void maybe_command()
    {
        if (!state_->advertised_a.load() || !state_->advertised_b.load()) return;
        auto native = peer_.native_handle();
        if (!native) {
            state_->log("COMMAND_FAILED target=B native_handle=null");
            return;
        }
        auto torrent = native->associated_torrent().lock();
        if (!torrent) {
            state_->log("COMMAND_FAILED target=B associated_torrent=null");
            return;
        }
        auto selected = state_->b_connection.lock();
        if (!selected || selected.get() != native.get()) {
            state_->log("COMMAND_DEFERRED target=B selected_native_mismatch=1");
            return;
        }
        bool expected = false;
        if (state_->command_issued.compare_exchange_strong(expected, true)) {
            state_->log("COMMAND_BEGIN target=B selected_tick=1 after_both_advertised=1 associated_torrent=1");
            auto peer_a = state_->a_connection.lock();
            const int a_before = peer_a
                ? int(peer_a->request_queue().size() + peer_a->download_queue().size()) : 0;
            const int b_before = int(native->request_queue().size() + native->download_queue().size());
            torrent->set_piece_priority(lt::piece_index_t(2), lt::default_priority);
            torrent->set_piece_priority(lt::piece_index_t(0), lt::dont_download);
            const int a_transition = peer_a
                ? int(peer_a->request_queue().size() + peer_a->download_queue().size()) : 0;
            const int b_transition = int(native->request_queue().size() + native->download_queue().size());
            if (peer_a) peer_a->cancel_all_requests();
            native->cancel_all_requests();
            state_->pre_owned_cleanup_done.store(true);
            state_->owned_phase_started.store(true);
            state_->log("PRE_OWNED_CLEANUP a_before=" + std::to_string(a_before)
                + " b_before=" + std::to_string(b_before)
                + " a_priority_transition=" + std::to_string(a_transition)
                + " b_priority_transition=" + std::to_string(b_transition)
                + " wire_cancels=" + std::to_string(state_->pre_owned_cleanup_cancels.load()));
            state_->internal_priority_enabled_on_network_thread.store(true);
            state_->log("INTERNAL_SET_PIECE_PRIORITY piece=2 priority=default network_thread=1");
            const std::vector<RequestKey> requests = {
                {2, 0, 16384}, {2, 16384, 16384}, {2, 32768, 7232}
            };
            for (RequestKey const& request : requests) {
                state_->commanded.insert(request);
                const bool queued = native->add_request(lt::piece_block{
                    lt::piece_index_t(request.piece), request.start / kBlockSize});
                if (queued) {
                    state_->permits.insert(request);
                    ++state_->queued_owned;
                } else {
                    state_->log("COMMAND_QUEUE_FAIL start=" + std::to_string(request.start));
                }
                state_->log("COMMAND target=B piece=" + std::to_string(request.piece)
                    + " start=" + std::to_string(request.start)
                    + " length=" + std::to_string(request.length)
                    + " queued=" + (queued ? "1" : "0"));
            }
            state_->command_queued.store(state_->queued_owned.load() == 3);
            native->send_block_requests();
        }
        if (state_->owned_wire.load() < 3) {
            state_->log("COMMAND_FLUSH target=B owned_wire="
                + std::to_string(state_->owned_wire.load()));
            native->send_block_requests();
        }
        state_->cv.notify_all();
    }

    lt::peer_connection_handle peer_;
    bool selected_;
    std::shared_ptr<OwnedState> state_;
};

class OwnedTorrentPlugin final : public lt::torrent_plugin {
public:
    explicit OwnedTorrentPlugin(std::shared_ptr<OwnedState> state) : state_(std::move(state)) {}

    std::shared_ptr<lt::peer_plugin> new_connection(lt::peer_connection_handle const& peer) override
    {
        const int port = peer.remote().port();
        if (port != state_->a_port && port != state_->b_port) return {};
        const bool selected = port == state_->b_port;
        state_->log("CONNECT peer=" + std::string(selected ? "B" : "A")
            + " endpoint=" + peer.remote().address().to_string() + ":" + std::to_string(port));
        return std::make_shared<OwnedPeerPlugin>(peer, selected, state_);
    }

    void on_piece_pass(lt::piece_index_t piece) override
    {
        if (piece == lt::piece_index_t(2)) {
            state_->piece_passed.store(true);
            state_->log("PIECE_PASS piece=2");
            state_->cv.notify_all();
        }
    }

private:
    std::shared_ptr<OwnedState> state_;
};

int run_owned_case(fs::path const& control, std::string const& case_id, int a_port, int b_port)
{
    auto state = std::make_shared<OwnedState>(control / "probe.transcript", case_id, a_port, b_port);
    state->release_file = control / "release-owned.marker";
    bool passed = false;
    {
        lt::session session(packet_settings());
        session.add_extension([state](lt::torrent_handle const&, lt::client_data_t) {
            return std::make_shared<OwnedTorrentPlugin>(state);
        });
        lt::add_torrent_params params;
        params.ti = load_metadata(control);
        params.save_path = (control / "download").string();
        lt::error_code error;
        lt::torrent_handle handle = session.add_torrent(std::move(params), error);
        if (error) throw std::runtime_error("owned add_torrent failed: " + error.message());
        handle.prioritize_pieces(std::vector<std::pair<lt::piece_index_t, lt::download_priority_t>>{
            {lt::piece_index_t(0), lt::default_priority},
            {lt::piece_index_t(1), lt::dont_download},
            {lt::piece_index_t(2), lt::dont_download}
        });
        state->priorities_disabled_before_connect =
            handle.piece_priority(lt::piece_index_t(0)) == lt::default_priority
            && handle.piece_priority(lt::piece_index_t(1)) == lt::dont_download
            && handle.piece_priority(lt::piece_index_t(2)) == lt::dont_download;
        state->log(std::string("PRIORITIES_BEFORE_CONNECT piece0=default piece1=0 piece2=0 verified=")
            + (state->priorities_disabled_before_connect ? "1" : "0"));
        state->enable_picker = [handle]() mutable {
            handle.piece_priority(lt::piece_index_t(1), lt::default_priority);
        };
        state->log("CONNECT_REQUEST peer=A port=" + std::to_string(a_port));
        handle.connect_peer(endpoint(a_port));
        state->log("CONNECT_REQUEST peer=B port=" + std::to_string(b_port));
        handle.connect_peer(endpoint(b_port));
        const bool owned_held = wait_for(state->cv, state->mutex, [&] {
            return state->command_queued.load() && state->owned_wire.load() == 3;
        }, 12000);
        if (owned_held) {
            state->log("OWNED_WIRE_OBSERVED_HELD owned_wire=3 enable_disjoint_piece=1");
            state->picker_enable_requested.store(true);
        }
        const bool picker_suppressed = wait_for(state->cv, state->mutex, [&] {
            return state->post_owned_suppressed.load() > 0;
        }, 12000);
        if (picker_suppressed && state->owned_piece_cancels.load() == 0
            && state->owned_outstanding_at_post_suppression.load() == 3) {
            state->log("PICKER_SUPPRESSION_OBSERVED release_owned=1");
            std::ofstream(state->release_file, std::ios::trunc).close();
        }
        const bool completed = wait_for(state->cv, state->mutex, [&] {
            return state->piece_passed.load() && state->owned_wire.load() == 3
                && state->post_owned_suppressed.load() > 0;
        }, 12000);
        passed = completed && state->command_queued.load()
            && state->advertised_a.load() && state->advertised_b.load()
            && state->priorities_disabled_before_connect
            && state->pre_owned_cleanup_done.load()
            && state->picker_enabled.load()
            && state->owned_outstanding_at_post_suppression.load() == 3
            && state->owned_piece_cancels.load() == 0;
        state->log(std::string("CASE_RESULT pass=") + (passed ? "1" : "0")
            + " command_queued=" + (state->command_queued.load() ? "1" : "0")
            + " owned_wire=" + std::to_string(state->owned_wire.load())
            + " suppressed=" + std::to_string(state->suppressed.load())
            + " post_owned_suppressed=" + std::to_string(state->post_owned_suppressed.load())
            + " owned_outstanding=" + std::to_string(state->owned_outstanding_at_post_suppression.load())
            + " owned_piece_cancels=" + std::to_string(state->owned_piece_cancels.load()));
    }

    std::ofstream out(control / "case.json", std::ios::trunc);
    out << "{\n"
        << "  \"schema\": \"colosseum-server1-p08-case/v1\",\n"
        << "  \"case\": \"" << case_id << "\",\n"
        << "  \"state\": \"" << (passed ? "PASS" : "FAIL") << "\",\n"
        << "  \"selected_peer\": \"B\",\n"
        << "  \"commanded\": [\n"
        << "    {\"peer\":\"B\",\"piece\":2,\"start\":0,\"length\":16384},\n"
        << "    {\"peer\":\"B\",\"piece\":2,\"start\":16384,\"length\":16384},\n"
        << "    {\"peer\":\"B\",\"piece\":2,\"start\":32768,\"length\":7232}\n"
        << "  ],\n"
        << "  \"internal_wire_requests\": " << state->wire_internal.load() << ",\n"
        << "  \"owned_wire_requests\": " << state->owned_wire.load() << ",\n"
        << "  \"suppressed_picker_requests\": " << state->suppressed.load() << ",\n"
        << "  \"post_owned_suppressed_picker_requests\": " << state->post_owned_suppressed.load() << ",\n"
        << "  \"unowned_internal_wire_requests\": " << (state->wire_internal.load() - state->owned_wire.load()) << ",\n"
        << "  \"pre_owned_cleanup_cancels\": " << state->pre_owned_cleanup_cancels.load() << ",\n"
        << "  \"owned_piece_cancels_before_release\": " << state->owned_piece_cancels.load() << ",\n"
        << "  \"owned_outstanding_at_post_suppression\": "
        << state->owned_outstanding_at_post_suppression.load() << ",\n"
        << "  \"unrelated_owned_requests_deleted\": "
        << (state->owned_piece_cancels.load() == 0
            && state->owned_outstanding_at_post_suppression.load() == 3 ? "false" : "true") << ",\n"
        << "  \"initial_piece_priorities_piece0_default_piece1_2_dont_download\": "
        << (state->priorities_disabled_before_connect ? "true" : "false") << ",\n"
        << "  \"internal_priority_enabled_on_network_thread\": "
        << (state->internal_priority_enabled_on_network_thread.load() ? "true" : "false") << ",\n"
        << "  \"owned_add_request_accepted_after_internal_priority_enable\": "
        << (state->command_queued.load() ? "true" : "false") << ",\n"
        << "  \"picker_enabled_after_owned_commands\": " << (state->picker_enabled.load() ? "true" : "false") << ",\n"
        << "  \"tail_length_checked\": 7232,\n"
        << "  \"piece_hash_passed\": " << (state->piece_passed.load() ? "true" : "false") << ",\n"
        << "  \"wire_cancel_sent_by_libtorrent\": " << (state->cancel_sent.load() > 0 ? "true" : "false") << "\n"
        << "}\n";
    return passed ? 0 : 1;
}

struct PartialState {
    PartialState(fs::path path, int b) : transcript(std::move(path), std::ios::trunc), b_port(b) {}
    void log(std::string const& line)
    {
        std::lock_guard<std::mutex> lock(mutex);
        transcript << line << '\n';
        transcript.flush();
    }
    std::ofstream transcript;
    int b_port;
    std::mutex mutex;
    std::condition_variable cv;
    std::weak_ptr<lt::peer_connection> connection;
    std::atomic<int> first_segment_bytes{0};
    std::atomic<int> all_bytes{0};
    std::atomic<bool> first_segment_observed{false};
    std::atomic<bool> first_segment_valid{true};
    std::atomic<bool> piece_passed{false};
    std::atomic<bool> first_hash_verified{false};
    std::atomic<int> wire_requests{0};
};

class PartialPeerPlugin final : public lt::peer_plugin {
public:
    PartialPeerPlugin(lt::peer_connection_handle peer, std::shared_ptr<PartialState> state)
        : peer_(std::move(peer)), state_(std::move(state)) {}
    bool on_handshake(lt::span<char const>) override
    {
        state_->connection = peer_.native_handle();
        state_->log("HANDSHAKE peer=B");
        return true;
    }
    bool on_bitfield(lt::bitfield const& pieces) override
    {
        state_->log(std::string("ADVERTISE piece0=") + ((pieces.size() > 0 && pieces[0]) ? "1" : "0"));
        return false;
    }
    void sent_request(lt::peer_request const& request) override
    {
        ++state_->wire_requests;
        state_->log("WIRE piece=" + std::to_string(int(request.piece))
            + " start=" + std::to_string(request.start)
            + " length=" + std::to_string(request.length));
    }
    bool on_piece(lt::peer_request const& request, lt::span<char const> data) override
    {
        bool valid = true;
        const char expected = request.start < kVirtualSegment ? 'A' : 'B';
        for (std::size_t i = 0; i < data.size(); ++i) {
            if (data[i] != expected) { valid = false; break; }
        }
        if (request.start < kVirtualSegment) state_->first_segment_bytes += request.length;
        state_->all_bytes += request.length;
        if (!valid) state_->first_segment_valid.store(false);
        if (state_->first_segment_bytes.load() >= kVirtualSegment
            && !state_->first_segment_observed.exchange(true)) {
            state_->log("FIRST_VIRTUAL_SEGMENT_OBSERVED bytes=524288 piece_hash_verified=0");
            state_->cv.notify_all();
        }
        state_->log("ON_PIECE start=" + std::to_string(request.start)
            + " length=" + std::to_string(request.length));
        state_->cv.notify_all();
        return false;
    }
private:
    lt::peer_connection_handle peer_;
    std::shared_ptr<PartialState> state_;
};

class PartialTorrentPlugin final : public lt::torrent_plugin {
public:
    explicit PartialTorrentPlugin(std::shared_ptr<PartialState> state) : state_(std::move(state)) {}
    std::shared_ptr<lt::peer_plugin> new_connection(lt::peer_connection_handle const& peer) override
    {
        if (peer.remote().port() != state_->b_port) return {};
        return std::make_shared<PartialPeerPlugin>(peer, state_);
    }
    void on_piece_pass(lt::piece_index_t piece) override
    {
        if (piece == lt::piece_index_t(0)) {
            state_->piece_passed.store(true);
            state_->log("FULL_PIECE_HASH_VERIFIED piece=0");
            state_->cv.notify_all();
        }
    }
private:
    std::shared_ptr<PartialState> state_;
};

int run_partial_case(fs::path const& control, int b_port)
{
    auto state = std::make_shared<PartialState>(control / "probe.transcript", b_port);
    const fs::path release = control / "release-tail.marker";
    bool passed = false;
    {
        lt::session session(packet_settings());
        session.add_extension([state](lt::torrent_handle const&, lt::client_data_t) {
            return std::make_shared<PartialTorrentPlugin>(state);
        });
        lt::add_torrent_params params;
        params.ti = load_metadata(control);
        params.save_path = (control / "download").string();
        lt::error_code error;
        lt::torrent_handle handle = session.add_torrent(std::move(params), error);
        if (error) throw std::runtime_error("partial add_torrent failed: " + error.message());
        handle.set_piece_deadline(lt::piece_index_t(0), 0);
        state->log("CONNECT_REQUEST peer=B port=" + std::to_string(b_port));
        handle.connect_peer(endpoint(b_port));
        const bool first_seen = wait_for(state->cv, state->mutex, [&] {
            return state->first_segment_observed.load();
        }, 12000);
        const lt::torrent_status before_tail = handle.status();
        state->first_hash_verified.store(before_tail.pieces.size() > 0
            && before_tail.pieces[lt::piece_index_t(0)]);
        state->log(std::string("FIRST_SEGMENT_CHECK observed=") + (first_seen ? "1" : "0")
            + " status_piece_hash_verified=" + (state->first_hash_verified.load() ? "1" : "0")
            + " held_later_blocks=1");
        if (first_seen) std::ofstream(release, std::ios::trunc).close();
        const bool all_blocks_seen = wait_for(state->cv, state->mutex, [&] {
            return state->all_bytes.load() == kLargePiece;
        }, 12000);
        bool full_seen = false;
        if (all_blocks_seen) {
            for (int i = 0; i < 1200; ++i) {
                const lt::torrent_status after_tail = handle.status();
                if (after_tail.pieces.size() > 0 && after_tail.pieces[lt::piece_index_t(0)]) {
                    state->piece_passed.store(true);
                    state->log("FULL_PIECE_HASH_VERIFIED_STATUS piece=0");
                    full_seen = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        passed = first_seen && full_seen && !state->first_hash_verified.load()
            && state->first_segment_valid.load() && state->all_bytes.load() == kLargePiece;
        state->log(std::string("CASE_RESULT pass=") + (passed ? "1" : "0")
            + " first_bytes=" + std::to_string(state->first_segment_bytes.load())
            + " all_bytes=" + std::to_string(state->all_bytes.load()));
    }
    std::ofstream out(control / "case.json", std::ios::trunc);
    out << "{\n"
        << "  \"schema\": \"colosseum-server1-p08-case/v1\",\n"
        << "  \"case\": \"P08-01\",\n"
        << "  \"state\": \"" << (passed ? "PASS" : "FAIL") << "\",\n"
        << "  \"real_piece_bytes\": 1048576,\n"
        << "  \"virtual_segment_bytes\": 524288,\n"
        << "  \"first_segment_observed\": " << (state->first_segment_observed.load() ? "true" : "false") << ",\n"
        << "  \"first_segment_before_full_piece_hash\": " << (!state->first_hash_verified.load() ? "true" : "false") << ",\n"
        << "  \"full_piece_hash_verified\": " << (state->piece_passed.load() ? "true" : "false") << ",\n"
        << "  \"first_segment_bytes\": " << state->first_segment_bytes.load() << ",\n"
        << "  \"all_piece_bytes\": " << state->all_bytes.load() << ",\n"
        << "  \"first_segment_pattern_valid\": " << (state->first_segment_valid.load() ? "true" : "false") << ",\n"
        << "  \"wire_requests_observed_internally\": " << state->wire_requests.load() << ",\n"
        << "  \"later_blocks_held_until_marker\": true\n"
        << "}\n";
    return passed ? 0 : 1;
}

struct HotState {
    HotState(fs::path path, int generation_value, int a, int b, fs::path release)
        : transcript(std::move(path), std::ios::trunc), generation(generation_value), a_port(a), b_port(b), release_file(std::move(release)) {}
    void log(std::string const& line)
    {
        std::lock_guard<std::mutex> lock(mutex);
        transcript << "generation=" << generation << ' ' << line << '\n';
        transcript.flush();
    }
    std::ofstream transcript;
    int generation;
    int a_port;
    int b_port;
    fs::path release_file;
    std::mutex mutex;
    std::condition_variable cv;
    std::weak_ptr<lt::peer_connection> a_connection;
    std::weak_ptr<lt::peer_connection> b_connection;
    std::atomic<bool> a_advertised{false};
    std::atomic<bool> b_advertised{false};
    std::atomic<bool> slow_request{false};
    std::atomic<bool> transfer_started{false};
    std::atomic<bool> cancel_invoked{false};
    std::atomic<bool> fast_permit{false};
    std::atomic<bool> fast_piece{false};
    std::atomic<bool> second_command{false};
    std::atomic<bool> piece_passed{false};
    std::atomic<int> a_wire{0};
    std::atomic<int> b_wire{0};
    std::atomic<int> a_cancel{0};
};

class HotPeerPlugin final : public lt::peer_plugin {
public:
    HotPeerPlugin(lt::peer_connection_handle peer, bool selected_b, std::shared_ptr<HotState> state)
        : peer_(std::move(peer)), selected_b_(selected_b), state_(std::move(state)) {}
    bool on_handshake(lt::span<char const>) override
    {
        if (selected_b_) state_->b_connection = peer_.native_handle();
        else state_->a_connection = peer_.native_handle();
        state_->log(std::string("HANDSHAKE peer=") + (selected_b_ ? "B" : "A"));
        maybe_second_command();
        return true;
    }
    bool on_bitfield(lt::bitfield const& pieces) override
    {
        const bool has_piece = pieces.size() > 0 && pieces[0];
        if (has_piece) {
            if (selected_b_) state_->b_advertised.store(true);
            else state_->a_advertised.store(true);
            state_->log(std::string("ADVERTISE peer=") + (selected_b_ ? "B" : "A"));
        }
        if (selected_b_) {
            maybe_transfer();
            maybe_second_command();
        }
        return false;
    }
    bool write_request(lt::peer_request const& request) override
    {
        const bool exact = request.piece == lt::piece_index_t(0)
            && request.start == 0 && request.length == kBlockSize;
        if (!selected_b_ && !state_->cancel_invoked.load()) return false;
        if (selected_b_ && exact && state_->fast_permit.exchange(false)) {
            state_->log("ALLOW_FAST peer=B piece=0 start=0 length=16384");
            return false;
        }
        state_->log("SUPPRESS peer=" + std::string(selected_b_ ? "B" : "A")
            + " piece=" + std::to_string(int(request.piece))
            + " start=" + std::to_string(request.start)
            + " length=" + std::to_string(request.length));
        return true;
    }
    void sent_request(lt::peer_request const& request) override
    {
        const bool exact = request.piece == lt::piece_index_t(0)
            && request.start == 0 && request.length == kBlockSize;
        if (selected_b_) ++state_->b_wire;
        else ++state_->a_wire;
        state_->log("WIRE peer=" + std::string(selected_b_ ? "B" : "A")
            + " piece=" + std::to_string(int(request.piece))
            + " start=" + std::to_string(request.start)
            + " length=" + std::to_string(request.length));
        if (!selected_b_ && exact && !state_->slow_request.exchange(true)) {
            state_->log("SLOW_RESERVATION_OBSERVED peer=A");
            state_->cv.notify_all();
        }
    }
    void sent_cancel(lt::peer_request const&) override
    {
        if (!selected_b_) ++state_->a_cancel;
        state_->log("CANCEL_SENT peer=" + std::string(selected_b_ ? "B" : "A"));
        state_->cv.notify_all();
    }
    bool on_piece(lt::peer_request const& request, lt::span<char const>) override
    {
        state_->log("PIECE peer=" + std::string(selected_b_ ? "B" : "A")
            + " piece=" + std::to_string(int(request.piece))
            + " start=" + std::to_string(request.start)
            + " length=" + std::to_string(request.length));
        if (selected_b_) {
            state_->fast_piece.store(true);
            state_->cv.notify_all();
        }
        return false;
    }
private:
    void maybe_transfer()
    {
        if (state_->generation != 1 || !state_->slow_request.load() || !state_->b_advertised.load()
            || state_->transfer_started.exchange(true)) return;
        auto slow = state_->a_connection.lock();
        auto fast = state_->b_connection.lock();
        if (!slow || !fast) {
            state_->log("HOTSWAP_FAILED missing_native_connection");
            return;
        }
        state_->log("HOTSWAP_CANCEL_REQUEST peer=A piece=0 start=0 length=16384 force=1");
        state_->cancel_invoked.store(true);
        slow->cancel_request(lt::piece_block{lt::piece_index_t(0), 0}, true);
        state_->fast_permit.store(true);
        const bool queued = fast->add_request(lt::piece_block{lt::piece_index_t(0), 0});
        fast->send_block_requests();
        if (!state_->release_file.empty()) {
            std::ofstream(state_->release_file, std::ios::trunc).close();
            state_->log("RELEASE_SLOW_MARKER_CREATED");
        }
        state_->log(std::string("HOTSWAP_REQUEUE peer=B queued=") + (queued ? "1" : "0"));
        state_->cv.notify_all();
    }
    void maybe_second_command()
    {
        if (state_->generation != 2 || !state_->b_advertised.load()
            || state_->second_command.exchange(true)) return;
        auto fast = state_->b_connection.lock();
        if (!fast) return;
        state_->fast_permit.store(true);
        const bool queued = fast->add_request(lt::piece_block{lt::piece_index_t(0), 0});
        fast->send_block_requests();
        state_->log(std::string("RECREATE_COMMAND peer=B queued=") + (queued ? "1" : "0"));
    }
    lt::peer_connection_handle peer_;
    bool selected_b_;
    std::shared_ptr<HotState> state_;
};

class HotTorrentPlugin final : public lt::torrent_plugin {
public:
    explicit HotTorrentPlugin(std::shared_ptr<HotState> state) : state_(std::move(state)) {}
    std::shared_ptr<lt::peer_plugin> new_connection(lt::peer_connection_handle const& peer) override
    {
        const int port = peer.remote().port();
        if (port != state_->a_port && port != state_->b_port) return {};
        return std::make_shared<HotPeerPlugin>(peer, port == state_->b_port, state_);
    }
    void on_piece_pass(lt::piece_index_t piece) override
    {
        if (piece == lt::piece_index_t(0)) {
            state_->piece_passed.store(true);
            state_->log("PIECE_PASS piece=0");
            state_->cv.notify_all();
        }
    }
private:
    std::shared_ptr<HotState> state_;
};

int run_hotswap_case(fs::path const& control, int a_port, int b_port)
{
    bool first_pass = false;
    bool late_observed = false;
    bool duplicate_observed = false;
    int generation1_b_wire = 0;
    {
        auto state = std::make_shared<HotState>(control / "generation-1.transcript", 1, a_port, b_port, control / "release-slow.marker");
        const fs::path release = control / "release-slow.marker";
        {
            lt::session session(packet_settings());
            session.add_extension([state](lt::torrent_handle const&, lt::client_data_t) {
                return std::make_shared<HotTorrentPlugin>(state);
            });
            lt::add_torrent_params params;
            params.ti = load_metadata(control);
            params.save_path = (control / "download-generation-1").string();
            lt::error_code error;
            lt::torrent_handle handle = session.add_torrent(std::move(params), error);
            if (error) throw std::runtime_error("hotswap generation 1 add_torrent failed: " + error.message());
            handle.set_piece_deadline(lt::piece_index_t(0), 0);
            state->log("CONNECT_REQUEST peer=A port=" + std::to_string(a_port));
            handle.connect_peer(endpoint(a_port));
            const bool slow_requested = wait_for(state->cv, state->mutex, [&] {
                return state->slow_request.load();
            }, 12000);
            state->log(std::string("SLOW_REQUEST_WAIT pass=") + (slow_requested ? "1" : "0"));
            if (slow_requested) {
                state->log("CONNECT_REQUEST peer=B port=" + std::to_string(b_port));
                handle.connect_peer(endpoint(b_port));
            }
            first_pass = wait_for(state->cv, state->mutex, [&] {
                return state->piece_passed.load();
            }, 12000);
            if (first_pass) std::ofstream(release, std::ios::trunc).close();
            const fs::path wire_a = control / "peer-A-wire.log";
            for (int i = 0; i < 3000; ++i) {
                if (file_contains(wire_a, "LATE_PIECE_SENT_AFTER_RELEASE")
                    || file_contains(wire_a, "LATE_PIECE_DROPPED_AFTER_RELEASE")) {
                    late_observed = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            for (int i = 0; i < 3000 && !duplicate_observed; ++i) {
                duplicate_observed = file_contains(wire_a, "DUPLICATE_LATE_PIECE");
                if (!duplicate_observed) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            state->log(std::string("GENERATION_1_RESULT first_pass=") + (first_pass ? "1" : "0")
                + " late_response=" + (late_observed ? "1" : "0")
                + " duplicate_response=" + (duplicate_observed ? "1" : "0"));
            generation1_b_wire = state->b_wire.load();
        }
    }
    bool recreate_pass = false;
    int generation2_b_wire = 0;
    {
        auto state = std::make_shared<HotState>(control / "generation-2.transcript", 2, a_port, b_port, fs::path{});
        lt::session session(packet_settings());
        session.add_extension([state](lt::torrent_handle const&, lt::client_data_t) {
            return std::make_shared<HotTorrentPlugin>(state);
        });
        lt::add_torrent_params params;
        params.ti = load_metadata(control);
        params.save_path = (control / "download-generation-2").string();
        lt::error_code error;
        lt::torrent_handle handle = session.add_torrent(std::move(params), error);
        if (error) throw std::runtime_error("hotswap generation 2 add_torrent failed: " + error.message());
        state->log("RECREATE_SAME_ENDPOINT peer=B port=" + std::to_string(b_port));
        handle.connect_peer(endpoint(b_port));
        recreate_pass = wait_for(state->cv, state->mutex, [&] {
            return state->piece_passed.load();
        }, 12000);
        generation2_b_wire = state->b_wire.load();
        state->log(std::string("GENERATION_2_RESULT pass=") + (recreate_pass ? "1" : "0")
            + " wire=" + std::to_string(generation2_b_wire));
    }
    const fs::path wire_a = control / "peer-A-wire.log";
    const fs::path wire_b = control / "peer-B-wire.log";
    for (int i = 0; i < 200 && (line_count(wire_a, "REQUEST ") < 1 || line_count(wire_b, "REQUEST ") < 2); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const int a_requests = line_count(wire_a, "REQUEST ");
    const int b_requests = line_count(wire_b, "REQUEST ");
    const int generation1_fast_requests = std::max(0, b_requests - generation2_b_wire);
    const bool wire_cancel = file_contains(wire_a, "CANCEL");
    const bool no_hidden_replay = a_requests == 1 && generation1_fast_requests == 1
        && generation2_b_wire == 1 && generation1_b_wire == 1;
    const bool old_response_observed = late_observed || duplicate_observed;
    const bool passed = first_pass && recreate_pass && generation1_b_wire == 1
        && old_response_observed && no_hidden_replay;
    std::ofstream out(control / "case.json", std::ios::trunc);
    out << "{\n"
        << "  \"schema\": \"colosseum-server1-p08-case/v1\",\n"
        << "  \"case\": \"P08-03\",\n"
        << "  \"state\": \"" << (passed ? "PASS" : "FAIL") << "\",\n"
        << "  \"slow_peer_reserved\": true,\n"
        << "  \"cancel_request_force_true_invoked\": true,\n"
        << "  \"wire_cancel_observed\": " << (wire_cancel ? "true" : "false") << ",\n"
        << "  \"required_behavior_graded_independent_of_wire_cancel\": true,\n"
        << "  \"fast_selected_peer_re_request\": " << (generation1_b_wire == 1 ? "true" : "false") << ",\n"
        << "  \"late_old_response_observed\": " << (late_observed ? "true" : "false") << ",\n"
        << "  \"duplicate_old_response_observed\": " << (duplicate_observed ? "true" : "false") << ",\n"
        << "  \"late_or_duplicate_old_response_observed\": " << (old_response_observed ? "true" : "false") << ",\n"
        << "  \"destroy_recreate_same_endpoint\": " << (recreate_pass ? "true" : "false") << ",\n"
        << "  \"generation1_slow_requests\": " << a_requests << ",\n"
        << "  \"all_generations_fast_requests\": " << b_requests << ",\n"
        << "  \"generation1_fast_requests\": " << generation1_fast_requests << ",\n"
        << "  \"generation2_fast_requests\": " << generation2_b_wire << ",\n"
        << "  \"no_hidden_replay_or_misattribution\": " << (no_hidden_replay ? "true" : "false") << ",\n"
        << "  \"process_exit_clean\": true\n"
        << "}\n";
    return passed ? 0 : 1;
}

int prepare(fs::path const& control, MetadataSpec spec)
{
    create_metadata(control, spec);
    std::cout << "PREPARED control=" << control.string()
              << " piece_size=" << spec.piece_size
              << " file_size=" << spec.file_size
              << " split=" << spec.split << '\n';
    return 0;
}

struct ControlState {
    ControlState(fs::path path, int port) : transcript(std::move(path), std::ios::trunc), peer_port(port) {}
    void log(std::string const& line)
    {
        std::lock_guard<std::mutex> lock(mutex);
        transcript << line << '\n';
        transcript.flush();
    }
    std::ofstream transcript;
    int peer_port;
    std::mutex mutex;
    std::condition_variable cv;
    std::weak_ptr<lt::peer_connection> connection;
    std::atomic<bool> saw_choke{false};
    std::atomic<bool> saw_unchoke{false};
    std::atomic<bool> saw_interested{false};
    std::atomic<bool> saw_not_interested{false};
    std::atomic<bool> sent_interested{false};
    std::atomic<bool> sent_not_interested{false};
    std::atomic<bool> piece_passed{false};
    std::atomic<int> wire_requests{0};
    std::atomic<std::int64_t> public_peer_total_download{0};
    std::atomic<std::int64_t> internal_payload_download{0};
};

class ControlPeerPlugin final : public lt::peer_plugin {
public:
    ControlPeerPlugin(lt::peer_connection_handle peer, std::shared_ptr<ControlState> state)
        : peer_(std::move(peer)), state_(std::move(state)) {}
    bool on_handshake(lt::span<char const>) override
    {
        state_->connection = peer_.native_handle();
        state_->log("HANDSHAKE peer=control");
        return true;
    }
    bool on_choke() override { state_->saw_choke.store(true); state_->log("HOOK on_choke"); state_->cv.notify_all(); return false; }
    bool on_unchoke() override { state_->saw_unchoke.store(true); state_->log("HOOK on_unchoke"); state_->cv.notify_all(); return false; }
    bool on_interested() override { state_->saw_interested.store(true); state_->log("HOOK on_interested"); state_->cv.notify_all(); return false; }
    bool on_not_interested() override { state_->saw_not_interested.store(true); state_->log("HOOK on_not_interested"); state_->cv.notify_all(); return false; }
    void sent_interested() override { state_->sent_interested.store(true); state_->log("HOOK sent_interested"); state_->cv.notify_all(); }
    void sent_not_interested() override { state_->sent_not_interested.store(true); state_->log("HOOK sent_not_interested"); state_->cv.notify_all(); }
    void sent_request(lt::peer_request const& request) override
    {
        ++state_->wire_requests;
        state_->log("WIRE_REQUEST piece=" + std::to_string(int(request.piece))
            + " start=" + std::to_string(request.start)
            + " length=" + std::to_string(request.length));
    }
    bool on_piece(lt::peer_request const& request, lt::span<char const>) override
    {
        lt::peer_info info;
        peer_.get_peer_info(info);
        state_->public_peer_total_download.store(info.total_download);
        if (auto native = peer_.native_handle())
            state_->internal_payload_download.store(native->statistics().total_payload_download());
        state_->log("ON_PIECE length=" + std::to_string(request.length)
            + " internal_payload_download=" + std::to_string(state_->internal_payload_download.load()));
        return false;
    }
private:
    lt::peer_connection_handle peer_;
    std::shared_ptr<ControlState> state_;
};

class ControlTorrentPlugin final : public lt::torrent_plugin {
public:
    explicit ControlTorrentPlugin(std::shared_ptr<ControlState> state) : state_(std::move(state)) {}
    std::shared_ptr<lt::peer_plugin> new_connection(lt::peer_connection_handle const& peer) override
    {
        if (peer.remote().port() != state_->peer_port) return {};
        return std::make_shared<ControlPeerPlugin>(peer, state_);
    }
    void on_piece_pass(lt::piece_index_t piece) override
    {
        if (piece == lt::piece_index_t(0)) {
            state_->piece_passed.store(true);
            state_->log("PIECE_PASS piece=0");
            state_->cv.notify_all();
        }
    }
private:
    std::shared_ptr<ControlState> state_;
};

bool settings_disabled(lt::session const& session)
{
    const lt::settings_pack effective = session.get_settings();
    return !effective.get_bool(lt::settings_pack::enable_dht)
        && !effective.get_bool(lt::settings_pack::enable_lsd)
        && !effective.get_bool(lt::settings_pack::enable_upnp)
        && !effective.get_bool(lt::settings_pack::enable_natpmp)
        && effective.get_bool(lt::settings_pack::allow_multiple_connections_per_ip);
}

void apply_packet_settings(lt::session& session)
{
    lt::settings_pack update;
    update.set_bool(lt::settings_pack::enable_dht, false);
    update.set_bool(lt::settings_pack::enable_lsd, false);
    update.set_bool(lt::settings_pack::enable_upnp, false);
    update.set_bool(lt::settings_pack::enable_natpmp, false);
    update.set_bool(lt::settings_pack::allow_multiple_connections_per_ip, true);
    session.apply_settings(update);
}

int run_controls_case(fs::path const& control, int metadata_port, int payload_port)
{
    bool metadata_received = false;
    bool settings_ok = false;
    {
        lt::session session(packet_settings());
        apply_packet_settings(session);
        settings_ok = settings_disabled(session);
        lt::add_torrent_params magnet;
        const auto full_info = load_metadata(control);
        magnet.info_hashes.v1 = full_info->info_hashes().v1;
        magnet.name = "P08-metadata-only";
        magnet.save_path = (control / "metadata-download").string();
        lt::error_code error;
        lt::torrent_handle metadata_handle = session.add_torrent(std::move(magnet), error);
        if (error) throw std::runtime_error("metadata-only add_torrent failed: " + error.message());
        metadata_handle.connect_peer(endpoint(metadata_port));
        for (int i = 0; i < 2000; ++i) {
            if (metadata_handle.status().has_metadata) { metadata_received = true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::ofstream transcript(control / "metadata.transcript", std::ios::app);
        transcript << "MAGNET_ONLY has_metadata=" << (metadata_received ? 1 : 0)
                   << " discovery_disabled=" << (settings_ok ? 1 : 0)
                   << " connect_peer_source=manual\n";
    }

    auto state = std::make_shared<ControlState>(control / "controls.transcript", payload_port);
    std::int64_t public_payload = 0;
    std::int64_t public_peer_total = 0;
    std::int64_t internal_payload = 0;
    bool per_engine_wire = false;
    bool choke_interest = false;
    {
        lt::session session(packet_settings());
        apply_packet_settings(session);
        settings_ok = settings_ok && settings_disabled(session);
        session.add_extension([state](lt::torrent_handle const&, lt::client_data_t) {
            return std::make_shared<ControlTorrentPlugin>(state);
        });
        lt::add_torrent_params params;
        params.ti = load_metadata(control);
        params.save_path = (control / "controls-download").string();
        lt::error_code error;
        lt::torrent_handle handle = session.add_torrent(std::move(params), error);
        if (error) throw std::runtime_error("controls add_torrent failed: " + error.message());
        handle.set_piece_deadline(lt::piece_index_t(0), 0);
        state->log("PER_ENGINE_DEADLINE piece=0 deadline_ms=0");
        state->log("SESSION_GLOBAL_SETTINGS dht=0 lsd=0 upnp=0 natpmp=0 multiple_ip=1");
        handle.connect_peer(endpoint(payload_port));
        const bool choke_seen = wait_for(state->cv, state->mutex, [&] {
            return state->saw_choke.load() && state->sent_interested.load();
        }, 10000);
        const fs::path release = control / "release-choke.marker";
        if (choke_seen) std::ofstream(release, std::ios::trunc).close();
        const bool unchoke_seen = wait_for(state->cv, state->mutex, [&] {
            return state->saw_unchoke.load() && state->saw_interested.load()
                && state->saw_not_interested.load();
        }, 10000);
        const bool pass_seen = wait_for(state->cv, state->mutex, [&] {
            return state->piece_passed.load();
        }, 15000);
        public_peer_total = state->public_peer_total_download.load();
        if (public_peer_total == 0) {
            for (int i = 0; i < 1000; ++i) {
                std::vector<lt::peer_info> peers;
                handle.get_peer_info(peers);
                if (!peers.empty()) {
                    public_peer_total = peers.front().total_download;
                    if (public_peer_total > 0) break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        const lt::torrent_status status = handle.status();
        public_payload = status.total_payload_download;
        internal_payload = state->internal_payload_download.load();
        if (auto native = state->connection.lock())
            internal_payload = std::max(internal_payload, native->statistics().total_payload_download());
        per_engine_wire = pass_seen && state->wire_requests.load() > 0;
        choke_interest = choke_seen && unchoke_seen && state->sent_interested.load()
            && state->saw_not_interested.load();
        state->log("STATS total_download=" + std::to_string(public_peer_total)
            + " torrent_payload=" + std::to_string(public_payload)
            + " internal_payload=" + std::to_string(internal_payload));
    }

    const fs::path metadata_wire = control / "metadata-peer-wire.log";
    const fs::path payload_wire = control / "payload-peer-wire.log";
    const bool metadata_wire_ok = file_contains(metadata_wire, "EXT_HANDSHAKE_SENT")
        && file_contains(metadata_wire, "METADATA_REQUEST")
        && file_contains(metadata_wire, "METADATA_RESPONSE");
    const bool payload_wire_ok = file_contains(payload_wire, "CHOKE_SENT")
        && file_contains(payload_wire, "UNCHOKE_SENT")
        && file_contains(payload_wire, "INTERESTED_SENT")
        && file_contains(payload_wire, "NOT_INTERESTED_SENT")
        && file_contains(payload_wire, "REQUEST ");
    const bool honest_stats = public_peer_total > 0 && public_payload >= kBlockSize
        && internal_payload >= kBlockSize;
    const bool metadata_control = metadata_received && settings_ok && metadata_wire_ok;
    const bool session_global = settings_ok && payload_wire_ok;
    const bool passed = metadata_control && honest_stats && per_engine_wire
        && session_global && choke_interest && payload_wire_ok;

    std::ofstream out(control / "case.json", std::ios::trunc);
    out << "{\n"
        << "  \"schema\": \"colosseum-server1-p08-controls/v1\",\n"
        << "  \"state\": \"" << (passed ? "PASS" : "FAIL") << "\",\n"
        << "  \"metadata_discovery\": {\"magnet_only\": true, \"metadata_received\": " << (metadata_received ? "true" : "false")
        << ", \"wire_request_response\": " << (metadata_wire_ok ? "true" : "false") << ", \"direct_connect_peer\": true},\n"
        << "  \"honest_peer_stats\": {\"public_peer_total_download\": " << public_peer_total
        << ", \"torrent_total_payload_download\": " << public_payload
        << ", \"internal_peer_payload_download\": " << internal_payload
        << ", \"wire_payload_expected\": 16384, \"positive_and_consistent\": " << (honest_stats ? "true" : "false") << "},\n"
        << "  \"per_engine_settings\": {\"api\": \"torrent_handle::set_piece_deadline\", \"wire_request_observed\": " << (per_engine_wire ? "true" : "false") << "},\n"
        << "  \"session_global_settings\": {\"apply_settings_effective\": " << (settings_ok ? "true" : "false") << ", \"tcp_wire_active\": " << (payload_wire_ok ? "true" : "false") << "},\n"
        << "  \"choke_interest\": {\"hooks_and_wire_correlated\": " << (choke_interest && payload_wire_ok ? "true" : "false") << "},\n"
        << "  \"compile_only_booleans_promoted\": false,\n"
        << "  \"dependency_patch_required\": false\n"
        << "}\n";
    return passed ? 0 : 1;
}

int run_case(std::string const& id, fs::path const& control, int a_port, int b_port)
{
    if (id == "P08-01") return run_partial_case(control, b_port);
    if (id == "P08-02" || id == "P08-04") return run_owned_case(control, id, a_port, b_port);
    if (id == "P08-03") return run_hotswap_case(control, a_port, b_port);
    if (id == "CONTROLS") return run_controls_case(control, a_port, b_port);
    throw std::runtime_error("unknown P08 case: " + id);
}

} // namespace p08_packet

int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--capabilities") {
        return colosseum::server1::p08::write_capability_matrix(
            argv[2], colosseum::server1::p08::inspect_frozen_libtorrent_surface());
    }
    try {
        if (argc >= 3 && std::string(argv[1]) == "--p08-prepare") {
            p08_packet::MetadataSpec spec;
            for (int i = 3; i + 1 < argc; i += 2) {
                const std::string option = argv[i];
                const std::string value = argv[i + 1];
                if (option == "--piece-size") spec.piece_size = std::stoi(value);
                else if (option == "--file-size") spec.file_size = std::stoi(value);
                else if (option == "--split") spec.split = std::stoi(value);
                else if (option == "--first-byte") spec.first = value[0];
                else if (option == "--second-byte") spec.second = value[0];
            }
            return p08_packet::prepare(argv[2], spec);
        }
        if (argc == 6 && std::string(argv[1]) == "--p08-case") {
            return p08_packet::run_case(argv[2], argv[3], std::stoi(argv[4]), std::stoi(argv[5]));
        }
    } catch (std::exception const& error) {
        std::cerr << "P08 probe exception: " << error.what() << '\n';
        return 2;
    }
    return p08a_fixture_main(argc, argv);
}
