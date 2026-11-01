#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/extensions.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/peer_connection.hpp>
#include <libtorrent/peer_connection_handle.hpp>
#include <libtorrent/piece_block.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/version.hpp>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace lt = libtorrent;

namespace {

constexpr int kBlockSize = 16 * 1024;
constexpr int kPayloadSize = 2 * kBlockSize;

struct ProbeState {
    ProbeState(int aPort, int bPort, fs::path transcriptPath, std::string phase, bool commandEnabled, bool suppressPicker)
        : aPort(aPort)
        , bPort(bPort)
        , transcript(std::move(transcriptPath), std::ios::app)
        , phase(std::move(phase))
        , commandEnabled(commandEnabled)
        , suppressPicker(suppressPicker)
    {
    }

    void log(std::string line)
    {
        std::lock_guard<std::mutex> lock(mutex);
        transcript << "phase=" << phase << " " << line << '\n';
        transcript.flush();
    }

    int aPort;
    int bPort;
    std::ofstream transcript;
    std::string phase;
    bool commandEnabled;
    bool suppressPicker;
    std::mutex mutex;
    std::atomic<bool> commandIssued{false};
    std::atomic<bool> commandQueued{false};
    std::atomic<bool> commandIssuedAfterBothAdvertised{false};
    std::atomic<bool> exactCommandPermit{false};
    std::atomic<bool> ownedExactAwaitingSent{false};
    std::atomic<bool> exactWire{false};
    std::atomic<bool> pieceReceived{false};
    std::atomic<bool> pieceAccepted{false};
    std::atomic<int> validHandshakesA{0};
    std::atomic<int> validHandshakesB{0};
    std::atomic<bool> advertisedA{false};
    std::atomic<bool> advertisedB{false};
    std::atomic<int> wireRequests{0};
    std::atomic<int> exactA{0};
    std::atomic<int> exactB{0};
    std::atomic<int> preCommandExactB{0};
    std::atomic<int> postCommandExactB{0};
    std::atomic<int> suppressedUnowned{0};
    std::mutex commandMutex;
    std::weak_ptr<lt::peer_connection> bConnection;
};

bool isExact(lt::peer_request const& request)
{
    return request.piece == lt::piece_index_t(0)
        && request.start == 0
        && request.length == kBlockSize;
}

class ProbePeerPlugin final : public lt::peer_plugin {
public:
    ProbePeerPlugin(lt::peer_connection_handle peer, bool selected, std::shared_ptr<ProbeState> state)
        : peer_(std::move(peer)), selected_(selected), state_(std::move(state))
    {
    }

    bool on_handshake(lt::span<char const>) override
    {
        if (selected_) {
            ++state_->validHandshakesB;
            std::lock_guard<std::mutex> lock(state_->commandMutex);
            state_->bConnection = peer_.native_handle();
        } else {
            ++state_->validHandshakesA;
        }
        state_->log(std::string("HANDSHAKE_VALID peer=") + (selected_ ? "B" : "A"));
        maybeIssueCommand();
        return true;
    }

    bool on_bitfield(lt::bitfield const& pieces) override
    {
        const bool advertisesPieceZero = pieces.size() > 0 && pieces[0];
        if (advertisesPieceZero) {
            if (selected_) state_->advertisedB.store(true);
            else state_->advertisedA.store(true);
            state_->log(std::string("ADVERTISE peer=") + (selected_ ? "B" : "A") + " piece=0");
        }
        maybeIssueCommand();
        return false;
    }

    void on_connected() override
    {
        state_->log(std::string("CONNECTED peer=") + (selected_ ? "B" : "A"));
    }

    void maybeIssueCommand()
    {
        if (!state_->commandEnabled) return;

        std::shared_ptr<lt::peer_connection> native;
        {
            std::lock_guard<std::mutex> lock(state_->commandMutex);
            if (state_->validHandshakesA.load() < 1 || state_->validHandshakesB.load() < 1
                || !state_->advertisedA.load() || !state_->advertisedB.load()
                || state_->commandIssued.exchange(true))
                return;
            native = state_->bConnection.lock();
        }

        if (!native) {
            state_->log("COMMAND target=B native_handle=null");
            return;
        }

        state_->exactCommandPermit.store(true);
        state_->log("COMMAND target=B piece=0 offset=0 length=16384 after_both_advertised=1 ownership_armed=1");
        const bool queued = native->add_request(lt::piece_block{lt::piece_index_t(0), 0});
        state_->commandQueued.store(queued);
        state_->commandIssuedAfterBothAdvertised.store(true);
        if (!queued) state_->exactCommandPermit.store(false);
        state_->log(std::string("COMMAND_QUEUE target=B queued=")
            + (queued ? "1" : "0"));
        native->send_block_requests();
    }

    bool write_request(lt::peer_request const& request) override
    {
        if (!state_->suppressPicker) return false;
        if (selected_ && isExact(request)) {
            bool permit = true;
            if (state_->exactCommandPermit.compare_exchange_strong(permit, false)) {
                state_->ownedExactAwaitingSent.store(true);
                state_->log("ALLOW_OWNED peer=B piece=0 offset=0 length=16384");
                return false;
            }
        }

        ++state_->suppressedUnowned;
        state_->log(std::string("SUPPRESS peer=") + (selected_ ? "B" : "A")
            + " piece=" + std::to_string(int(request.piece))
            + " offset=" + std::to_string(request.start)
            + " length=" + std::to_string(request.length));
        return true;
    }

    void sent_request(lt::peer_request const& request) override
    {
        ++state_->wireRequests;
        bool ownedExactB = false;
        if (isExact(request)) {
            if (selected_) {
                ++state_->exactB;
                ownedExactB = state_->ownedExactAwaitingSent.exchange(false);
                if (ownedExactB) {
                    ++state_->postCommandExactB;
                    state_->exactWire.store(true);
                } else {
                    ++state_->preCommandExactB;
                }
            } else {
                ++state_->exactA;
            }
        }
        state_->log(std::string("WIRE peer=") + (selected_ ? "B" : "A")
            + " piece=" + std::to_string(int(request.piece))
            + " offset=" + std::to_string(request.start)
            + " length=" + std::to_string(request.length)
            + " ownership=" + (ownedExactB ? "command" : "unowned"));
    }

    bool on_piece(lt::peer_request const& request, lt::span<char const>) override
    {
        if (selected_ && isExact(request)) {
            state_->pieceReceived.store(true);
            state_->log("PIECE_RECEIVED peer=B piece=0 offset=0 length=16384");
        }
        return false;
    }

private:
    lt::peer_connection_handle peer_;
    bool selected_;
    std::shared_ptr<ProbeState> state_;
};

class ProbeTorrentPlugin final : public lt::torrent_plugin {
public:
    explicit ProbeTorrentPlugin(std::shared_ptr<ProbeState> state)
        : state_(std::move(state))
    {
    }

    std::shared_ptr<lt::peer_plugin> new_connection(lt::peer_connection_handle const& peer) override
    {
        const bool selected = peer.remote().port() == state_->bPort;
        const bool expectedA = peer.remote().port() == state_->aPort;
        state_->log(std::string("CONNECT peer=") + (selected ? "B" : (expectedA ? "A" : "UNKNOWN"))
            + " endpoint=" + peer.remote().address().to_string()
            + ":" + std::to_string(peer.remote().port()));
        if (!selected && !expectedA) return {};
        return std::make_shared<ProbePeerPlugin>(peer, selected, state_);
    }

private:
    std::shared_ptr<ProbeState> state_;
};

std::shared_ptr<lt::torrent_info> createMetadata(fs::path const& control)
{
    fs::create_directories(control);
    const fs::path payload = control / "P08A-exact-block.bin";
    std::ofstream output(payload, std::ios::binary | std::ios::trunc);
    const std::string bytes(kPayloadSize, 'P');
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();

    lt::file_storage storage;
    storage.add_file("P08A-exact-block.bin", kPayloadSize);
    lt::create_torrent creator(storage, kBlockSize);
    lt::set_piece_hashes(creator, control.string());
    const lt::entry generated = creator.generate();
    std::vector<char> encoded;
    lt::bencode(std::back_inserter(encoded), generated);
    std::ofstream torrent(control / "probe.torrent", std::ios::binary | std::ios::trunc);
    torrent.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    torrent.close();
    return std::make_shared<lt::torrent_info>(encoded.data(), static_cast<int>(encoded.size()));
}

std::shared_ptr<lt::torrent_info> loadMetadata(fs::path const& control)
{
    std::ifstream input(control / "probe.torrent", std::ios::binary);
    std::vector<char> encoded((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    lt::error_code error;
    auto info = std::make_shared<lt::torrent_info>(encoded.data(), static_cast<int>(encoded.size()), error);
    if (error) throw std::runtime_error("torrent metadata decode failed: " + error.message());
    return info;
}

void writePrepare(fs::path const& control)
{
    auto info = createMetadata(control);
    std::ofstream hash(control / "info_hash.txt", std::ios::trunc);
    // This is a v1 torrent. libtorrent's BitTorrent handshake carries the
    // v1 info hash, even though info_hashes().get_best() prefers v2 here.
    hash << info->info_hashes().v1 << '\n';
    std::ofstream identity(control / "dependency_identity.txt", std::ios::trunc);
    identity << "header_declared_version=2.0.11.0\n"
             << "header_declared_revision=6e1587799\n"
             << "linked_runtime_version=" << lt::version() << '\n'
             << "v1_info_hash=" << info->info_hashes().v1 << '\n'
             << "v2_info_hash=" << info->info_hashes().v2 << '\n'
             << "best_info_hash=" << info->info_hashes().get_best() << '\n'
             << "archive_sha256=a2d67f24710303750aaf068d1945380d95434e4791ad611b68b0b3b055d89a30\n";
}

struct PhaseResult {
    bool commandQueued;
    bool commandIssuedAfterBothAdvertised;
    bool exactWire;
    bool pieceReceived;
    bool pieceAccepted;
    int validHandshakesA;
    int validHandshakesB;
    bool advertisedA;
    bool advertisedB;
    int wireRequests;
    int exactA;
    int exactB;
    int preCommandExactB;
    int postCommandExactB;
    int suppressedUnowned;
    bool destroyedWithOutstanding;
    bool postDestroyProcessAlive;
};

bool fileContains(fs::path const& path, std::string const& token)
{
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        if (line.find(token) != std::string::npos) return true;
    }
    return false;
}

PhaseResult runPhase(fs::path const& control, std::string phase, int aPort, int bPort,
    bool commandEnabled, bool suppressPicker, fs::path releaseFile)
{
    const auto state = std::make_shared<ProbeState>(
        aPort, bPort, control / ("lifecycle-" + phase + ".transcript"), phase, commandEnabled, suppressPicker);
    bool destroyedWithOutstanding = false;
    bool postDestroyProcessAlive = false;
    {
        lt::settings_pack settings;
        settings.set_str(lt::settings_pack::listen_interfaces, "127.0.0.1:49100");
        settings.set_bool(lt::settings_pack::enable_dht, false);
        settings.set_bool(lt::settings_pack::enable_lsd, false);
        settings.set_bool(lt::settings_pack::enable_upnp, false);
        settings.set_bool(lt::settings_pack::enable_natpmp, false);
        settings.set_bool(lt::settings_pack::allow_multiple_connections_per_ip, true);
        settings.set_int(lt::settings_pack::out_enc_policy, lt::settings_pack::pe_disabled);
        settings.set_int(lt::settings_pack::in_enc_policy, lt::settings_pack::pe_disabled);

        lt::session session(settings);
        session.add_extension([state](lt::torrent_handle const&, lt::client_data_t) {
            return std::make_shared<ProbeTorrentPlugin>(state);
        });

        lt::add_torrent_params params;
        params.ti = loadMetadata(control);
        const fs::path savePath = control / ("download-" + phase);
        fs::create_directories(savePath);
        params.save_path = savePath.string();
        state->log("SAVE_PATH " + savePath.string());
        lt::error_code error;
        const lt::torrent_handle handle = session.add_torrent(std::move(params), error);
        if (error) throw std::runtime_error("add_torrent failed: " + error.message());

        for (int i = 0; i < 200; ++i) {
            const auto status = handle.status();
            if (status.has_metadata && status.state != lt::torrent_status::checking_files)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        // Piece 1 is the ordinary-picker control block. Piece 0 remains free
        // for the exact command sent to selected peer B.
        handle.set_piece_deadline(lt::piece_index_t(1), 0);
        state->log(std::string("CONNECT_REQUEST peer=A port=") + std::to_string(aPort));
        handle.connect_peer(lt::tcp::endpoint(lt::make_address("127.0.0.1"), static_cast<std::uint16_t>(aPort)));
        state->log(std::string("CONNECT_REQUEST peer=B port=") + std::to_string(bPort));
        handle.connect_peer(lt::tcp::endpoint(lt::make_address("127.0.0.1"), static_cast<std::uint16_t>(bPort)));

        if (!commandEnabled) {
            for (int i = 0; i < 600 && state->wireRequests.load() == 0; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else if (releaseFile.empty()) {
            for (int i = 0; i < 600 && !state->pieceAccepted.load(); ++i) {
                const auto status = handle.status();
                if (status.num_pieces == 1 && status.pieces.size() >= 1 && status.pieces[lt::piece_index_t(0)]) {
                    state->pieceAccepted.store(true);
                    state->log("PIECE_ACCEPTED num_pieces=1");
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } else {
            for (int i = 0; i < 400 && !state->exactWire.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            destroyedWithOutstanding = state->exactWire.load() && !state->pieceReceived.load();
            state->log(std::string("SESSION_DESTROY_PENDING outstanding=")
                + (destroyedWithOutstanding ? "1" : "0"));
        }
    }

    if (!releaseFile.empty()) {
        std::ofstream(releaseFile, std::ios::trunc).close();
        const fs::path wireDir = releaseFile.parent_path();
        const fs::path wireA = wireDir / "lifecycle-peer-A-wire.log";
        const fs::path wireB = wireDir / "lifecycle-peer-B-wire.log";
        bool latePeerObserved = false;
        bool bothPeersDisconnected = false;
        for (int i = 0; i < 400; ++i) {
            latePeerObserved = fileContains(wireB, "LATE_PIECE_SENT_AFTER_RELEASE")
                || fileContains(wireB, "LATE_PIECE_DROPPED_AFTER_RELEASE");
            bothPeersDisconnected = fileContains(wireA, "CLIENT_DISCONNECTED")
                && fileContains(wireB, "CLIENT_DISCONNECTED");
            if (latePeerObserved && bothPeersDisconnected) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        postDestroyProcessAlive = latePeerObserved && bothPeersDisconnected;
        state->log(std::string("SESSION_DESTROYED release_marker_created=1 late_peer_observed=")
            + (latePeerObserved ? "1" : "0")
            + " both_peers_disconnected=" + (bothPeersDisconnected ? "1" : "0")
            + " process_alive=" + (postDestroyProcessAlive ? "1" : "0"));
    }

    return {
        state->commandQueued.load(),
        state->commandIssuedAfterBothAdvertised.load(),
        state->exactWire.load(),
        state->pieceReceived.load(),
        state->pieceAccepted.load(),
        state->validHandshakesA.load(),
        state->validHandshakesB.load(),
        state->advertisedA.load(),
        state->advertisedB.load(),
        state->wireRequests.load(),
        state->exactA.load(),
        state->exactB.load(),
        state->preCommandExactB.load(),
        state->postCommandExactB.load(),
        state->suppressedUnowned.load(),
        destroyedWithOutstanding,
        postDestroyProcessAlive
    };
}

struct WireCounts {
    int requests = 0;
    int exact = 0;
    int lateAfterRelease = 0;
    int releaseObserved = 0;
    int disconnected = 0;
    int handshakes = 0;
    int advertisements = 0;
};

WireCounts readWire(fs::path const& path)
{
    WireCounts counts;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        if (line.find("REQUEST ") != std::string::npos) ++counts.requests;
        if (line.find("REQUEST piece=0 start=0 length=16384") != std::string::npos) ++counts.exact;
        if (line.find("HANDSHAKE remote=") != std::string::npos
            && line.find("info_hash_match=1") != std::string::npos)
            ++counts.handshakes;
        if (line.find("BITFIELD_SENT pieces=0,1") != std::string::npos) ++counts.advertisements;
        if (line.find("LATE_PIECE_SENT_AFTER_RELEASE") != std::string::npos
            || line.find("LATE_PIECE_DROPPED_AFTER_RELEASE") != std::string::npos)
            ++counts.lateAfterRelease;
        if (line.find("RELEASE_OBSERVED") != std::string::npos) ++counts.releaseObserved;
        if (line.find("CLIENT_DISCONNECTED") != std::string::npos) ++counts.disconnected;
    }
    return counts;
}

void writeResult(fs::path const& control, PhaseResult const& normal, PhaseResult const& controlled,
    PhaseResult const& lifecycle, WireCounts normalA, WireCounts normalB,
    WireCounts controlledA, WireCounts controlledB, WireCounts lifecycleA, WireCounts lifecycleB,
    std::string const& probeExecutableSha256)
{
    const int controlledUnowned = controlledA.requests + controlledB.requests - controlledB.exact;
    const int lifecycleReplayed = std::max(0, lifecycleB.requests - 1) + lifecycleA.requests;
    const int controlledPeerRequests = controlledA.requests + controlledB.requests;
    const int lifecyclePeerRequests = lifecycleA.requests + lifecycleB.requests;
    const bool controlledCountsMatch = controlled.wireRequests == controlledPeerRequests;
    const bool lifecycleCountsMatch = lifecycle.wireRequests == lifecyclePeerRequests;
    const bool pickerAvailable = normalA.requests + normalB.requests > 0;
    const bool bothControlledPeersReady = controlled.validHandshakesA > 0 && controlled.validHandshakesB > 0
        && controlled.advertisedA && controlled.advertisedB
        && controlledA.handshakes > 0 && controlledB.handshakes > 0
        && controlledA.advertisements > 0 && controlledB.advertisements > 0;
    const bool case01 = bothControlledPeersReady && controlled.commandIssuedAfterBothAdvertised
        && controlled.commandQueued && controlled.pieceReceived && controlled.pieceAccepted
        && controlled.preCommandExactB == 0 && controlled.postCommandExactB == 1
        && controlled.wireRequests == 1 && controlledPeerRequests == 1 && controlledCountsMatch
        && controlledB.exact == 1 && controlledA.exact == 0;
    const bool case02 = pickerAvailable && controlled.pieceReceived && controlled.pieceAccepted
        && controlled.suppressedUnowned > 0 && controlledUnowned == 0;
    const bool noOrphaned = lifecycleA.handshakes == lifecycleA.disconnected
        && lifecycleB.handshakes == lifecycleB.disconnected
        && lifecycleA.handshakes > 0 && lifecycleB.handshakes > 0;
    const bool case03 = lifecycle.destroyedWithOutstanding && lifecycle.postDestroyProcessAlive
        && lifecycle.commandIssuedAfterBothAdvertised && lifecycle.advertisedA && lifecycle.advertisedB
        && lifecycle.preCommandExactB == 0 && lifecycle.postCommandExactB == 1
        && lifecycle.wireRequests == 1 && lifecyclePeerRequests == 1 && lifecycleCountsMatch
        && lifecycleB.exact == 1 && lifecycleA.requests == 0 && lifecycleReplayed == 0
        && lifecycleB.lateAfterRelease > 0 && lifecycleB.releaseObserved > 0
        && noOrphaned;
    std::ofstream result(control / "result.json", std::ios::trunc);
    result << "{\n"
           << "  \"schema\": \"colosseum-server1-p08a-feasibility/v2\",\n"
           << "  \"dependency\": {\"header_path\": \"C:/tools/libtorrent-2.0-msvc/include/libtorrent/version.hpp\", \"header_sha256\": \"674fe75760cca96c5b1c9ca162501e222944025d5b43af3964a30d15d33edf07\", \"header_declared_version\": \"2.0.11.0\", \"header_declared_revision\": \"6e1587799\", \"linked_runtime_version\": \"" << lt::version() << "\", \"archive_path\": \"C:/tools/libtorrent-2.0-msvc/lib/torrent-rasterbar.lib\", \"archive_bytes\": 320838716, \"archive_sha256\": \"a2d67f24710303750aaf068d1945380d95434e4791ad611b68b0b3b055d89a30\", \"probe_executable_sha256\": \"" << probeExecutableSha256 << "\"},\n"
           << "  \"source_tuples\": {\"M814\": {\"lines\": \"72439-72764\", \"sha256\": \"05eba72a8229b9705b0e657af5a4223fb88809f1b90325614cb33a5ecefb005e\"}, \"M851\": {\"lines\": \"74856-74884\", \"sha256\": \"2d42fa6b493e0786b631c7e6a49b5a235283cf49ab3a617e31ee5402ad4ff041\"}, \"oracle_sha256\": \"405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f\"},\n"
           << "  \"source_identity\": {\"source\": \"artifacts/server1/P08A/probe-src/exact_block_probe.cpp\", \"public_api_attempt\": \"torrent_handle::set_piece_deadline(piece)\", \"internal_control\": \"peer_connection::add_request(piece_block) + send_block_requests()\", \"session_option\": \"settings_pack::allow_multiple_connections_per_ip=true (probe-local)\"},\n"
           << "  \"control_phase\": {\"state\": \"" << (pickerAvailable ? "PASS" : "FAIL") << "\", \"wire_requests\": " << normalA.requests + normalB.requests << "},\n"
           << "  \"case_01\": {\"state\": \"" << (case01 ? "PASS" : "FAIL") << "\", \"commanded_response_received_and_accepted\": " << ((controlled.pieceReceived && controlled.pieceAccepted) ? "true" : "false") << ", \"command_issued_after_both_peers_advertised\": " << (controlled.commandIssuedAfterBothAdvertised ? "true" : "false") << ", \"peer_a_valid_handshakes\": " << controlledA.handshakes << ", \"peer_b_valid_handshakes\": " << controlledB.handshakes << ", \"peer_a_piece_advertisements\": " << controlledA.advertisements << ", \"peer_b_piece_advertisements\": " << controlledB.advertisements << ", \"plugin_sent_requests\": " << controlled.wireRequests << ", \"peer_observed_requests\": " << controlledPeerRequests << ", \"plugin_peer_request_counts_match\": " << (controlledCountsMatch ? "true" : "false") << ", \"plugin_pre_command_exact_b_requests\": " << controlled.preCommandExactB << ", \"plugin_post_command_exact_b_requests\": " << controlled.postCommandExactB << ", \"peer_b_exact_requests\": " << controlledB.exact << ", \"peer_a_exact_requests\": " << controlledA.exact << "},\n"
           << "  \"case_02\": {\"state\": \"" << (case02 ? "PASS" : "FAIL") << "\", \"ordinary_picker_available\": " << (pickerAvailable ? "true" : "false") << ", \"control_phase_wire_requests\": " << normalA.requests + normalB.requests << ", \"controlled_suppressed_picker_attempts\": " << controlled.suppressedUnowned << ", \"controlled_phase_wire_requests\": " << controlledA.requests + controlledB.requests << ", \"unowned_wire_requests\": " << controlledUnowned << "},\n"
           << "  \"case_03\": {\"state\": \"" << (case03 ? "PASS" : "FAIL") << "\", \"destroyed_with_outstanding\": " << (lifecycle.destroyedWithOutstanding ? "true" : "false") << ", \"post_destroy_process_alive\": " << (lifecycle.postDestroyProcessAlive ? "true" : "false") << ", \"late_peer_event_after_destroy\": " << ((lifecycleB.lateAfterRelease > 0 && lifecycleB.releaseObserved > 0) ? "true" : "false") << ", \"replayed_or_second_requests\": " << lifecycleReplayed << ", \"plugin_sent_requests\": " << lifecycle.wireRequests << ", \"peer_observed_requests\": " << lifecyclePeerRequests << ", \"plugin_peer_request_counts_match\": " << (lifecycleCountsMatch ? "true" : "false") << ", \"plugin_pre_command_exact_b_requests\": " << lifecycle.preCommandExactB << ", \"plugin_post_command_exact_b_requests\": " << lifecycle.postCommandExactB << ", \"peer_a_valid_handshakes\": " << lifecycleA.handshakes << ", \"peer_b_valid_handshakes\": " << lifecycleB.handshakes << ", \"connected_peer_handshakes\": " << lifecycleA.handshakes + lifecycleB.handshakes << ", \"client_disconnects\": " << lifecycleA.disconnected + lifecycleB.disconnected << ", \"no_orphaned_client_connections\": " << (noOrphaned ? "true" : "false") << "},\n"
           << "  \"seam_classification\": \"version-specific internal header access\",\n"
           << "  \"interface_statement\": \"No production interface changed; this is a disposable feasibility probe.\",\n"
           << "  \"wiring_request\": \"No production wiring requested; P08A remains a feasibility result.\"\n"
           << "}\n";
}

int run(fs::path const& control, int controlA, int controlB, fs::path const& normalLogA,
    fs::path const& normalLogB, int controlledA, int controlledB, fs::path const& controlledLogA,
    fs::path const& controlledLogB, int lifecycleA, int lifecycleB, fs::path const& lifecycleLogA,
    fs::path const& lifecycleLogB, fs::path const& releaseFile, std::string const& probeExecutableSha256)
{
    const auto normal = runPhase(control, "normal-picker", controlA, controlB, false, false, {});
    const auto controlled = runPhase(control, "controlled", controlledA, controlledB, true, true, {});
    const auto lifecycle = runPhase(control, "lifecycle", lifecycleA, lifecycleB, true, true, releaseFile);
    const auto normalWireA = readWire(normalLogA);
    const auto normalWireB = readWire(normalLogB);
    const auto controlledWireA = readWire(controlledLogA);
    const auto controlledWireB = readWire(controlledLogB);
    const auto lifecycleWireA = readWire(lifecycleLogA);
    const auto lifecycleWireB = readWire(lifecycleLogB);
    writeResult(control, normal, controlled, lifecycle, normalWireA, normalWireB,
        controlledWireA, controlledWireB, lifecycleWireA, lifecycleWireB, probeExecutableSha256);
    std::cout << "P08A probe complete; linked_runtime=" << lt::version()
              << "; normal_picker_requests=" << normalWireA.requests + normalWireB.requests
              << "; controlled_B_exact=" << controlledWireB.exact
              << "; controlled_unowned=" << controlledWireA.requests + controlledWireB.requests - controlledWireB.exact
              << "; lifecycle_replayed=" << std::max(0, lifecycleWireB.requests - 1) + lifecycleWireA.requests << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc == 3 && std::string(argv[1]) == "--prepare") {
            writePrepare(argv[2]);
            return 0;
        }
        if (argc == 17 && std::string(argv[1]) == "--run") {
            return run(argv[2], std::stoi(argv[3]), std::stoi(argv[4]), argv[5], argv[6],
                std::stoi(argv[7]), std::stoi(argv[8]), argv[9], argv[10], std::stoi(argv[11]),
                std::stoi(argv[12]), argv[13], argv[14], argv[15], argv[16]);
        }
        std::cerr << "usage: exact_block_probe --prepare CONTROL_DIR\n"
                  << "   or: exact_block_probe --run CONTROL_DIR CONTROL_A CONTROL_B CONTROL_LOG_A CONTROL_LOG_B"
                     " CONTROLLED_A CONTROLLED_B CONTROLLED_LOG_A CONTROLLED_LOG_B"
                     " LIFECYCLE_A LIFECYCLE_B LIFECYCLE_LOG_A LIFECYCLE_LOG_B RELEASE_FILE PROBE_EXE_SHA256\n";
        return 2;
    } catch (std::exception const& error) {
        std::cerr << "probe failure: " << error.what() << '\n';
        return 1;
    }
}
