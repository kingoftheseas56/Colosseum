#ifndef NUVIO_ENGINE_PROTOCOL_BACKEND_HPP
#define NUVIO_ENGINE_PROTOCOL_BACKEND_HPP

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "nuvio_engine/nuvio_engine.h"

namespace nuvio::torrent {

struct ProtocolBackendConfig {
    nuvio_engine_upload_mode upload_mode;
    std::uint64_t upload_limit_bytes_per_second;
    std::string state_directory;
    std::string download_directory;
    std::uint16_t listen_port;
    std::uint64_t memory_cache_capacity_bytes;
    std::uint64_t disk_cache_capacity_bytes;
    std::uint32_t stream_inactivity_timeout_milliseconds;
    std::uint32_t warm_torrent_timeout_milliseconds;
    nuvio_engine_torrent_profile torrent_profile;
    std::string tls_ca_bundle_path;
};

enum class TorrentInputType {
    magnet,
    torrent_data,
};

struct TorrentInput {
    TorrentInputType type;
    std::string magnet_uri;
    std::vector<char> torrent_data;
};

struct TorrentFileInfo {
    std::string path;
    std::uint64_t offset;
    std::uint64_t size;
};

enum class BackendEventType {
    torrent_added,
    metadata_ready,
    torrent_error,
    stream_prepared,
    stream_stopped,
    disk_cache_reclaimed,
    torrent_removed,
};

struct BackendEvent {
    BackendEventType type;
    std::uint64_t request_id;
    std::string torrent_id;
    std::string message;
    std::vector<TorrentFileInfo> files{};
    std::uint32_t file_index = std::numeric_limits<std::uint32_t>::max();
    std::uint64_t file_size = 0;
    std::string stream_id{};
    std::string stream_url{};
};

struct BackendStats {
    std::uint32_t active_torrents = 0;
    std::uint32_t active_streams = 0;
    std::uint32_t active_http_requests = 0;
    std::uint32_t connected_peers = 0;
    std::uint32_t connected_seeds = 0;
    std::uint32_t known_peers = 0;
    std::uint32_t connect_candidates = 0;
    std::uint32_t interested_peers = 0;
    std::uint32_t unchoked_peers = 0;
    std::uint32_t downloading_peers = 0;
    std::uint32_t snubbed_peers = 0;
    std::uint32_t pending_block_requests = 0;
    std::uint32_t target_block_requests = 0;
    std::uint32_t timed_out_block_requests = 0;
    std::uint32_t pending_piece_reads = 0;
    std::uint32_t connecting_peers = 0;
    std::uint32_t handshaking_peers = 0;
    std::uint32_t target_piece_peers = 0;
    std::uint32_t target_piece_unchoked_peers = 0;
    std::uint32_t target_piece_downloading_peers = 0;
    std::uint32_t off_target_downloading_peers = 0;
    std::uint32_t tracker_reply_events = 0;
    std::uint32_t tracker_error_events = 0;
    std::uint32_t dht_reply_events = 0;
    std::uint64_t download_rate_bytes_per_second = 0;
    std::uint64_t upload_rate_bytes_per_second = 0;
    std::uint64_t total_payload_download_bytes = 0;
    std::uint64_t total_payload_upload_bytes = 0;
    std::uint64_t memory_cache_capacity_bytes = 0;
    std::uint64_t memory_cache_used_bytes = 0;
    std::uint64_t memory_cache_hits = 0;
    std::uint64_t memory_cache_misses = 0;
    std::uint64_t memory_cache_evictions = 0;
    std::uint64_t memory_cache_entries = 0;
    std::uint32_t warm_torrents = 0;
    std::uint32_t quiesced_torrents = 0;
    std::uint64_t disk_cache_capacity_bytes = 0;
    std::uint64_t disk_cache_used_bytes = 0;
    std::uint64_t disk_cache_protected_bytes = 0;
    std::uint64_t disk_cache_evictions = 0;
    std::uint64_t disk_cache_reclaimed_bytes = 0;
    std::uint64_t tracker_peers_returned = 0;
    std::uint64_t dht_peers_returned = 0;
    std::uint64_t peer_connect_events = 0;
    std::uint64_t peer_disconnect_events = 0;
    std::uint64_t peer_disconnect_timeouts = 0;
    std::uint64_t peer_disconnect_connect_failures = 0;
    std::uint64_t peer_disconnect_redundant = 0;
    std::uint64_t peer_disconnect_turnover = 0;
    std::uint64_t peer_disconnect_other = 0;
    std::uint64_t torrent_finished_events = 0;
    bool disk_cache_over_budget = false;
    struct Stream {
        std::string stream_id;
        std::uint32_t file_index = 0;
        std::uint64_t file_size = 0;
        std::uint64_t contiguous_ready_bytes = 0;
        std::uint64_t verified_file_bytes = 0;
        std::uint64_t delivered_bytes = 0;
        std::uint32_t active_demands = 0;
        std::uint32_t scheduled_pieces = 0;
        std::uint32_t blocking_pieces = 0;
        std::uint32_t primary_blocking_piece = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t secondary_blocking_piece = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t last_ready_piece = std::numeric_limits<std::uint32_t>::max();
        std::uint64_t primary_demand_start = 0;
        std::uint64_t primary_demand_end = 0;
        std::uint64_t secondary_demand_start = 0;
        std::uint64_t secondary_demand_end = 0;
        std::uint64_t schedule_revision = 0;
    };
    std::vector<Stream> streams{};
};

class ProtocolBackend {
public:
    virtual ~ProtocolBackend() = default;
    virtual void add_torrent(
        std::uint64_t request_id,
        TorrentInput input,
        const std::string& save_path
    ) = 0;
    virtual void prepare_file(
        std::uint64_t request_id,
        const std::string& torrent_id,
        std::uint32_t file_index,
        TorrentFileInfo file
    ) = 0;
    virtual void remove_torrent(
        std::uint64_t request_id,
        const std::string& torrent_id
    ) = 0;
    virtual void stop_stream(
        std::uint64_t request_id,
        const std::string& stream_id
    ) = 0;
    virtual void reclaim_disk_cache(
        std::uint64_t request_id,
        std::uint64_t target_bytes
    ) = 0;
    virtual void shutdown() = 0;
    virtual void set_wakeup(std::function<void()>) {
    }
    [[nodiscard]] virtual std::vector<BackendEvent> pop_events() = 0;
    [[nodiscard]] virtual BackendStats statistics() = 0;
    /* Task-streaming-pass diagnostics: effective session settings of the
     * live session. Default: unavailable (empty string). */
    [[nodiscard]] virtual std::string session_diagnostics() {
        return {};
    }
};

[[nodiscard]] std::unique_ptr<ProtocolBackend> create_protocol_backend(
    const ProtocolBackendConfig& config
);

}

#endif
