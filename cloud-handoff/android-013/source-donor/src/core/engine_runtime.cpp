#include "core/engine_runtime.hpp"
#include "nuvio_engine/file_selection.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <optional>
#include <utility>

namespace nuvio::core {
namespace {

template <std::size_t Size>
void copy_text(char (&destination)[Size], const std::string& source) {
    const auto length = std::min(source.size(), Size - 1);
    std::memcpy(destination, source.data(), length);
    destination[length] = '\0';
}

}

EngineRuntime::EngineRuntime(
    std::unique_ptr<torrent::ProtocolBackend> backend,
    std::string save_path,
    const std::size_t command_capacity,
    const std::size_t event_capacity
)
    : backend_(std::move(backend)),
      save_path_(std::move(save_path)),
      command_capacity_(command_capacity),
      event_capacity_(event_capacity) {
    stats_.struct_size = sizeof(nuvio_engine_stats);
    if (backend_) {
        backend_->set_wakeup([this] {
            wake();
        });
        worker_ = std::thread(&EngineRuntime::run, this);
    }
}

void EngineRuntime::wake() {
    {
        std::lock_guard lock(command_mutex_);
        wake_pending_ = true;
    }
    command_ready_.notify_one();
}

EngineRuntime::~EngineRuntime() {
    {
        std::lock_guard lock(command_mutex_);
        stopping_ = true;
    }
    command_ready_.notify_one();
    if (worker_.joinable()) {
        worker_.join();
    }
}

nuvio_engine_status EngineRuntime::add_torrent(
    torrent::TorrentInput input,
    std::uint64_t& request_id
) {
    Command command{};
    command.type = CommandType::add_torrent;
    command.input = std::move(input);
    return enqueue(std::move(command), request_id);
}

nuvio_engine_status EngineRuntime::enqueue(Command command, std::uint64_t& request_id) {
    if (!backend_) {
        return NUVIO_ENGINE_STATUS_BACKEND_UNAVAILABLE;
    }
    std::lock_guard lock(command_mutex_);
    if (stopping_) {
        return NUVIO_ENGINE_STATUS_INITIALIZATION_FAILED;
    }
    if (commands_.size() >= command_capacity_) {
        return NUVIO_ENGINE_STATUS_QUEUE_FULL;
    }
    const auto accepted_request_id = next_request_id_;
    command.request_id = accepted_request_id;
    commands_.push_back(std::move(command));
    ++next_request_id_;
    request_id = accepted_request_id;
    command_ready_.notify_one();
    return NUVIO_ENGINE_STATUS_OK;
}

nuvio_engine_status EngineRuntime::prepare_stream(
    std::string torrent_id,
    const std::optional<std::size_t> requested_index,
    std::string filename_hint,
    std::uint64_t& request_id
) {
    torrent::TorrentFileInfo selected_file;
    std::size_t selected_index = 0;
    {
        std::lock_guard lock(metadata_mutex_);
        const auto snapshot = files_.find(torrent_id);
        if (snapshot == files_.end()) {
            return NUVIO_ENGINE_STATUS_METADATA_NOT_READY;
        }
        std::vector<torrent::TorrentFile> selectable;
        selectable.reserve(snapshot->second.size());
        for (const auto& file : snapshot->second) {
            selectable.push_back({file.path, file.size});
        }
        const auto selection = torrent::select_file(
            selectable,
            requested_index,
            filename_hint
        );
        if (!selection.index.has_value()) {
            return NUVIO_ENGINE_STATUS_NOT_FOUND;
        }
        selected_index = *selection.index;
        selected_file = snapshot->second[selected_index];
    }
    Command command{};
    command.type = CommandType::prepare_stream;
    command.torrent_id = std::move(torrent_id);
    command.file_index = static_cast<std::uint32_t>(selected_index);
    command.file = std::move(selected_file);
    return enqueue(std::move(command), request_id);
}

nuvio_engine_status EngineRuntime::remove_torrent(
    std::string torrent_id,
    std::uint64_t& request_id
) {
    Command command{};
    command.type = CommandType::remove_torrent;
    command.torrent_id = std::move(torrent_id);
    return enqueue(std::move(command), request_id);
}

nuvio_engine_status EngineRuntime::stop_stream(
    std::string stream_id,
    std::uint64_t& request_id
) {
    Command command{};
    command.type = CommandType::stop_stream;
    command.stream_id = std::move(stream_id);
    return enqueue(std::move(command), request_id);
}

nuvio_engine_status EngineRuntime::get_file_count(
    const std::string& torrent_id,
    std::size_t& file_count
) {
    std::lock_guard lock(metadata_mutex_);
    const auto files = files_.find(torrent_id);
    if (files == files_.end()) {
        return NUVIO_ENGINE_STATUS_METADATA_NOT_READY;
    }
    file_count = files->second.size();
    return NUVIO_ENGINE_STATUS_OK;
}

nuvio_engine_status EngineRuntime::get_file(
    const std::string& torrent_id,
    const std::size_t file_index,
    nuvio_engine_file& file
) {
    std::lock_guard lock(metadata_mutex_);
    const auto files = files_.find(torrent_id);
    if (files == files_.end()) {
        return NUVIO_ENGINE_STATUS_METADATA_NOT_READY;
    }
    if (file_index >= files->second.size()) {
        return NUVIO_ENGINE_STATUS_OUT_OF_RANGE;
    }
    const auto& source = files->second[file_index];
    file = {};
    file.struct_size = sizeof(nuvio_engine_file);
    file.index = static_cast<std::uint32_t>(file_index);
    file.offset = source.offset;
    file.size = source.size;
    file.path_truncated = source.path.size() >= sizeof(file.path) ? 1 : 0;
    copy_text(file.path, source.path);
    return NUVIO_ENGINE_STATUS_OK;
}

nuvio_engine_status EngineRuntime::poll_event(nuvio_engine_event& event) {
    std::lock_guard lock(event_mutex_);
    if (events_.empty()) {
        return NUVIO_ENGINE_STATUS_NO_EVENT;
    }
    event = events_.front();
    events_.pop_front();
    return NUVIO_ENGINE_STATUS_OK;
}

nuvio_engine_stats EngineRuntime::get_stats() {
    std::lock_guard lock(stats_mutex_);
    return stats_;
}

nuvio_engine_status EngineRuntime::get_stream_stats(
    const std::string& stream_id,
    nuvio_engine_stream_stats& stats
) {
    std::lock_guard lock(stats_mutex_);
    const auto found = stream_stats_.find(stream_id);
    if (found == stream_stats_.end()) {
        return NUVIO_ENGINE_STATUS_NOT_FOUND;
    }
    stats = found->second;
    return NUVIO_ENGINE_STATUS_OK;
}

nuvio_engine_status EngineRuntime::reclaim_disk_cache(
    const std::uint64_t target_bytes,
    std::uint64_t& request_id
) {
    Command command{};
    command.type = CommandType::reclaim_disk_cache;
    command.target_bytes = target_bytes;
    return enqueue(std::move(command), request_id);
}

void EngineRuntime::run() {
    while (true) {
        std::optional<Command> command;
        bool shutdown_requested = false;
        {
            std::unique_lock lock(command_mutex_);
            command_ready_.wait_for(lock, std::chrono::milliseconds(25), [this] {
                return stopping_ || wake_pending_ || !commands_.empty();
            });
            wake_pending_ = false;
            if (!commands_.empty()) {
                command = std::move(commands_.front());
                commands_.pop_front();
            } else if (stopping_) {
                shutdown_requested = true;
            }
        }
        if (shutdown_requested) {
            try {
                backend_->shutdown();
            } catch (...) {
            }
            break;
        }
        if (command.has_value()) {
            process_command(std::move(*command));
        }
        try {
            collect_backend_events();
        } catch (const std::exception& error) {
            push_event({torrent::BackendEventType::torrent_error, 0, {}, error.what(), {}});
        } catch (...) {
            push_event({
                torrent::BackendEventType::torrent_error,
                0,
                {},
                "unknown protocol event error",
                {},
            });
        }
    }
}

void EngineRuntime::process_command(Command command) {
    try {
        switch (command.type) {
        case CommandType::add_torrent:
            backend_->add_torrent(command.request_id, std::move(command.input), save_path_);
            break;
        case CommandType::prepare_stream:
            backend_->prepare_file(
                command.request_id,
                command.torrent_id,
                command.file_index,
                std::move(command.file)
            );
            break;
        case CommandType::stop_stream:
            backend_->stop_stream(command.request_id, command.stream_id);
            break;
        case CommandType::reclaim_disk_cache:
            backend_->reclaim_disk_cache(command.request_id, command.target_bytes);
            break;
        case CommandType::remove_torrent:
            backend_->remove_torrent(command.request_id, command.torrent_id);
            break;
        case CommandType::verify_session:
            command.verification->set_value(backend_->session_diagnostics());
            break;
        }
    } catch (const std::exception& error) {
        push_event({
            torrent::BackendEventType::torrent_error,
            command.request_id,
            {},
            error.what(),
            {},
        });
    } catch (...) {
        push_event({
            torrent::BackendEventType::torrent_error,
            command.request_id,
            {},
            "unknown protocol backend error",
            {},
        });
    }
}

void EngineRuntime::collect_backend_events() {
    if (!backend_) {
        return;
    }
    auto events = backend_->pop_events();
    const auto now = std::chrono::steady_clock::now();
    if (events.empty() && now < next_stats_refresh_) {
        return;
    }
    next_stats_refresh_ = now + std::chrono::milliseconds(50);
    refresh_stats();
    for (auto& event : events) {
        push_event(std::move(event));
    }
}

void EngineRuntime::refresh_stats() {
    const auto backend_stats = backend_->statistics();
    nuvio_engine_stats stats{};
    stats.struct_size = sizeof(nuvio_engine_stats);
    stats.active_torrents = backend_stats.active_torrents;
    stats.active_streams = backend_stats.active_streams;
    stats.active_http_requests = backend_stats.active_http_requests;
    stats.connected_peers = backend_stats.connected_peers;
    stats.connected_seeds = backend_stats.connected_seeds;
    stats.known_peers = backend_stats.known_peers;
    stats.connect_candidates = backend_stats.connect_candidates;
    stats.interested_peers = backend_stats.interested_peers;
    stats.unchoked_peers = backend_stats.unchoked_peers;
    stats.downloading_peers = backend_stats.downloading_peers;
    stats.snubbed_peers = backend_stats.snubbed_peers;
    stats.pending_block_requests = backend_stats.pending_block_requests;
    stats.target_block_requests = backend_stats.target_block_requests;
    stats.timed_out_block_requests = backend_stats.timed_out_block_requests;
    stats.connecting_peers = backend_stats.connecting_peers;
    stats.handshaking_peers = backend_stats.handshaking_peers;
    stats.target_piece_peers = backend_stats.target_piece_peers;
    stats.target_piece_unchoked_peers = backend_stats.target_piece_unchoked_peers;
    stats.target_piece_downloading_peers = backend_stats.target_piece_downloading_peers;
    stats.off_target_downloading_peers = backend_stats.off_target_downloading_peers;
    stats.tracker_reply_events = backend_stats.tracker_reply_events;
    stats.tracker_error_events = backend_stats.tracker_error_events;
    stats.dht_reply_events = backend_stats.dht_reply_events;
    stats.pending_piece_reads = backend_stats.pending_piece_reads;
    stats.download_rate_bytes_per_second =
        backend_stats.download_rate_bytes_per_second;
    stats.upload_rate_bytes_per_second = backend_stats.upload_rate_bytes_per_second;
    stats.total_payload_download_bytes = backend_stats.total_payload_download_bytes;
    stats.total_payload_upload_bytes = backend_stats.total_payload_upload_bytes;
    stats.memory_cache_capacity_bytes = backend_stats.memory_cache_capacity_bytes;
    stats.memory_cache_used_bytes = backend_stats.memory_cache_used_bytes;
    stats.memory_cache_hits = backend_stats.memory_cache_hits;
    stats.memory_cache_misses = backend_stats.memory_cache_misses;
    stats.memory_cache_evictions = backend_stats.memory_cache_evictions;
    stats.memory_cache_entries = backend_stats.memory_cache_entries;
    stats.warm_torrents = backend_stats.warm_torrents;
    stats.quiesced_torrents = backend_stats.quiesced_torrents;
    stats.disk_cache_capacity_bytes = backend_stats.disk_cache_capacity_bytes;
    stats.disk_cache_used_bytes = backend_stats.disk_cache_used_bytes;
    stats.disk_cache_protected_bytes = backend_stats.disk_cache_protected_bytes;
    stats.disk_cache_evictions = backend_stats.disk_cache_evictions;
    stats.disk_cache_reclaimed_bytes = backend_stats.disk_cache_reclaimed_bytes;
    stats.disk_cache_over_budget = backend_stats.disk_cache_over_budget ? 1 : 0;
    stats.tracker_peers_returned = backend_stats.tracker_peers_returned;
    stats.dht_peers_returned = backend_stats.dht_peers_returned;
    stats.peer_connect_events = backend_stats.peer_connect_events;
    stats.peer_disconnect_events = backend_stats.peer_disconnect_events;
    stats.peer_disconnect_timeouts = backend_stats.peer_disconnect_timeouts;
    stats.peer_disconnect_connect_failures =
        backend_stats.peer_disconnect_connect_failures;
    stats.peer_disconnect_redundant = backend_stats.peer_disconnect_redundant;
    stats.peer_disconnect_turnover = backend_stats.peer_disconnect_turnover;
    stats.peer_disconnect_other = backend_stats.peer_disconnect_other;
    stats.torrent_finished_events = backend_stats.torrent_finished_events;
    std::lock_guard lock(stats_mutex_);
    stats_ = stats;
    stream_stats_.clear();
    for (const auto& stream : backend_stats.streams) {
        nuvio_engine_stream_stats snapshot{};
        snapshot.struct_size = sizeof(nuvio_engine_stream_stats);
        snapshot.file_index = stream.file_index;
        snapshot.file_size = stream.file_size;
        snapshot.contiguous_ready_bytes = stream.contiguous_ready_bytes;
        snapshot.verified_file_bytes = stream.verified_file_bytes;
        snapshot.delivered_bytes = stream.delivered_bytes;
        snapshot.active_demands = stream.active_demands;
        snapshot.scheduled_pieces = stream.scheduled_pieces;
        snapshot.blocking_pieces = stream.blocking_pieces;
        snapshot.primary_blocking_piece = stream.primary_blocking_piece;
        snapshot.secondary_blocking_piece = stream.secondary_blocking_piece;
        snapshot.last_ready_piece = stream.last_ready_piece;
        snapshot.primary_demand_start = stream.primary_demand_start;
        snapshot.primary_demand_end = stream.primary_demand_end;
        snapshot.secondary_demand_start = stream.secondary_demand_start;
        snapshot.secondary_demand_end = stream.secondary_demand_end;
        snapshot.schedule_revision = stream.schedule_revision;
        stream_stats_.insert_or_assign(stream.stream_id, snapshot);
    }
}

std::string EngineRuntime::session_diagnostics() {
    if (!backend_) return {};
    auto result = std::make_shared<std::promise<std::string>>();
    auto future = result->get_future();
    {
        std::lock_guard lock(command_mutex_);
        if (stopping_ || commands_.size() >= command_capacity_) return {};
        Command command{};
        command.type = CommandType::verify_session;
        command.verification = std::move(result);
        commands_.push_back(std::move(command));
    }
    command_ready_.notify_one();
    if (future.wait_for(std::chrono::seconds(4)) != std::future_status::ready) return {};
    return future.get();
}

void EngineRuntime::push_event(torrent::BackendEvent backend_event) {
    if (backend_event.type == torrent::BackendEventType::metadata_ready) {
        std::lock_guard lock(metadata_mutex_);
        files_.insert_or_assign(backend_event.torrent_id, std::move(backend_event.files));
    } else if (backend_event.type == torrent::BackendEventType::torrent_removed) {
        std::lock_guard lock(metadata_mutex_);
        files_.erase(backend_event.torrent_id);
    }
    nuvio_engine_event event{};
    event.struct_size = sizeof(nuvio_engine_event);
    switch (backend_event.type) {
    case torrent::BackendEventType::torrent_added:
        event.type = NUVIO_ENGINE_EVENT_TORRENT_ADDED;
        break;
    case torrent::BackendEventType::metadata_ready:
        event.type = NUVIO_ENGINE_EVENT_TORRENT_METADATA_READY;
        break;
    case torrent::BackendEventType::torrent_error:
        event.type = NUVIO_ENGINE_EVENT_TORRENT_ERROR;
        break;
    case torrent::BackendEventType::stream_prepared:
        event.type = NUVIO_ENGINE_EVENT_STREAM_PREPARED;
        break;
    case torrent::BackendEventType::stream_stopped:
        event.type = NUVIO_ENGINE_EVENT_STREAM_STOPPED;
        break;
    case torrent::BackendEventType::disk_cache_reclaimed:
        event.type = NUVIO_ENGINE_EVENT_DISK_CACHE_RECLAIMED;
        break;
    case torrent::BackendEventType::torrent_removed:
        event.type = NUVIO_ENGINE_EVENT_TORRENT_REMOVED;
        break;
    }
    event.sequence = next_sequence_++;
    event.request_id = backend_event.request_id;
    copy_text(event.torrent_id, backend_event.torrent_id);
    copy_text(event.message, backend_event.message);
    event.file_index = backend_event.file_index;
    event.file_size = backend_event.file_size;
    copy_text(event.stream_id, backend_event.stream_id);
    copy_text(event.stream_url, backend_event.stream_url);

    std::lock_guard lock(event_mutex_);
    if (events_.size() >= event_capacity_) {
        events_.pop_front();
        ++dropped_events_;
    }
    event.dropped_events = dropped_events_;
    events_.push_back(event);
}

}
