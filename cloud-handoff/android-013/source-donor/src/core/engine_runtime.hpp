#ifndef NUVIO_ENGINE_ENGINE_RUNTIME_HPP
#define NUVIO_ENGINE_ENGINE_RUNTIME_HPP

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include "nuvio_engine/nuvio_engine.h"
#include "torrent/protocol_backend.hpp"

namespace nuvio::core {

class EngineRuntime {
public:
    EngineRuntime(
        std::unique_ptr<torrent::ProtocolBackend> backend,
        std::string save_path,
        std::size_t command_capacity = 256,
        std::size_t event_capacity = 1024
    );
    ~EngineRuntime();

    EngineRuntime(const EngineRuntime&) = delete;
    EngineRuntime& operator=(const EngineRuntime&) = delete;

    [[nodiscard]] nuvio_engine_status add_torrent(
        torrent::TorrentInput input,
        std::uint64_t& request_id
    );
    [[nodiscard]] nuvio_engine_status poll_event(nuvio_engine_event& event);
    [[nodiscard]] nuvio_engine_status get_file_count(
        const std::string& torrent_id,
        std::size_t& file_count
    );
    [[nodiscard]] nuvio_engine_status get_file(
        const std::string& torrent_id,
        std::size_t file_index,
        nuvio_engine_file& file
    );
    [[nodiscard]] nuvio_engine_status prepare_stream(
        std::string torrent_id,
        std::optional<std::size_t> requested_index,
        std::string filename_hint,
        std::uint64_t& request_id
    );
    [[nodiscard]] nuvio_engine_status remove_torrent(
        std::string torrent_id,
        std::uint64_t& request_id
    );
    [[nodiscard]] nuvio_engine_status stop_stream(
        std::string stream_id,
        std::uint64_t& request_id
    );
    [[nodiscard]] nuvio_engine_stats get_stats();
    /* Task-streaming-pass diagnostics; empty when the backend has none. */
    [[nodiscard]] std::string session_diagnostics();
    [[nodiscard]] nuvio_engine_status get_stream_stats(
        const std::string& stream_id,
        nuvio_engine_stream_stats& stats
    );
    [[nodiscard]] nuvio_engine_status reclaim_disk_cache(
        std::uint64_t target_bytes,
        std::uint64_t& request_id
    );

private:
    enum class CommandType {
        add_torrent,
        prepare_stream,
        stop_stream,
        reclaim_disk_cache,
        remove_torrent,
        verify_session,
    };

    struct Command {
        CommandType type;
        std::uint64_t request_id;
        torrent::TorrentInput input;
        std::string torrent_id;
        std::string stream_id;
        std::uint32_t file_index = 0;
        torrent::TorrentFileInfo file;
        std::uint64_t target_bytes = 0;
        std::shared_ptr<std::promise<std::string>> verification;
    };

    [[nodiscard]] nuvio_engine_status enqueue(Command command, std::uint64_t& request_id);
    void wake();
    void run();
    void process_command(Command command);
    void collect_backend_events();
    void refresh_stats();
    void push_event(torrent::BackendEvent event);

    std::unique_ptr<torrent::ProtocolBackend> backend_;
    std::string save_path_;
    const std::size_t command_capacity_;
    const std::size_t event_capacity_;
    std::mutex command_mutex_;
    std::condition_variable command_ready_;
    std::deque<Command> commands_;
    std::mutex event_mutex_;
    std::deque<nuvio_engine_event> events_;
    std::mutex metadata_mutex_;
    std::unordered_map<std::string, std::vector<torrent::TorrentFileInfo>> files_;
    std::mutex stats_mutex_;
    nuvio_engine_stats stats_{};
    std::unordered_map<std::string, nuvio_engine_stream_stats> stream_stats_;
    std::thread worker_;
    bool stopping_ = false;
    bool wake_pending_ = false;
    std::chrono::steady_clock::time_point next_stats_refresh_{};
    std::uint64_t next_request_id_ = 1;
    std::uint64_t next_sequence_ = 1;
    std::uint64_t dropped_events_ = 0;
};

}

#endif
