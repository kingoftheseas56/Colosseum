#include "nuvio_engine/nuvio_engine.h"
#include "core/engine_runtime.hpp"
#include "torrent/protocol_backend.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>

#if defined(NUVIO_ENGINE_HAS_LIBTORRENT)
const char* libtorrent_version_string();
#endif

struct nuvio_engine {
    std::string data_directory;
    std::string cache_directory;
    std::uint64_t memory_cache_capacity_bytes;
    std::uint64_t disk_cache_capacity_bytes;
    std::uint16_t listen_port;
    nuvio_engine_upload_mode upload_mode;
    std::uint64_t upload_limit_bytes_per_second;
    std::uint32_t stream_inactivity_timeout_milliseconds;
    std::uint32_t warm_torrent_timeout_milliseconds;
    nuvio_engine_torrent_profile torrent_profile;
    std::unique_ptr<nuvio::core::EngineRuntime> runtime;
};

namespace {

constexpr std::uint64_t default_memory_cache_capacity = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t default_disk_cache_capacity = 2ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t default_stream_inactivity_timeout_milliseconds = 30'000;
constexpr std::uint32_t default_warm_torrent_timeout_milliseconds = 60'000;
constexpr nuvio_engine_torrent_profile default_torrent_profile =
    NUVIO_ENGINE_TORRENT_PROFILE_BALANCED;
constexpr std::size_t config_v1_size =
    offsetof(nuvio_engine_config, upload_limit_bytes_per_second) + sizeof(std::uint64_t);
constexpr std::size_t config_v2_size =
    offsetof(nuvio_engine_config, reserved_1) + sizeof(std::uint32_t);
constexpr std::size_t config_v3_size =
    offsetof(nuvio_engine_config, reserved_2) + sizeof(std::uint32_t);
constexpr std::size_t config_v4_size =
    offsetof(nuvio_engine_config, tls_ca_bundle_path) + sizeof(const char*);
constexpr std::size_t config_v5_size =
    offsetof(nuvio_engine_config, reserved_3) + sizeof(std::uint32_t);
constexpr std::size_t torrent_request_v1_size =
    offsetof(nuvio_engine_torrent_request, magnet_uri) + sizeof(const char*);
constexpr std::size_t torrent_request_v2_size =
    offsetof(nuvio_engine_torrent_request, torrent_data_size) + sizeof(std::size_t);
constexpr std::size_t stream_request_v1_size =
    offsetof(nuvio_engine_stream_request, filename_hint) + sizeof(const char*);
constexpr std::size_t event_v1_size =
    offsetof(nuvio_engine_event, message) + sizeof(nuvio_engine_event{}.message);
constexpr std::size_t file_v1_size =
    offsetof(nuvio_engine_file, path) + sizeof(nuvio_engine_file{}.path);
constexpr std::size_t stats_v1_size =
    offsetof(nuvio_engine_stats, memory_cache_entries) + sizeof(std::uint64_t);
constexpr std::size_t stream_stats_v1_size =
    offsetof(nuvio_engine_stream_stats, delivered_bytes) + sizeof(std::uint64_t);

template <typename Structure>
bool initialize_structure(Structure* const value, const std::uint32_t struct_size) {
    if (value == nullptr) {
        return false;
    }
    std::memset(value, 0, struct_size);
    if (struct_size < sizeof(std::uint32_t)) {
        return false;
    }
    value->struct_size = struct_size;
    return true;
}

bool valid_upload_configuration(const nuvio_engine_config& config) {
    switch (config.upload_mode) {
    case NUVIO_ENGINE_UPLOAD_DISABLED:
    case NUVIO_ENGINE_UPLOAD_UNLIMITED:
        return config.upload_limit_bytes_per_second == 0;
    case NUVIO_ENGINE_UPLOAD_LIMITED:
        return config.upload_limit_bytes_per_second > 0;
    }
    return false;
}

bool valid_torrent_profile(const nuvio_engine_torrent_profile profile) {
    switch (profile) {
    case NUVIO_ENGINE_TORRENT_PROFILE_SOFT:
    case NUVIO_ENGINE_TORRENT_PROFILE_BALANCED:
    case NUVIO_ENGINE_TORRENT_PROFILE_FAST:
        return true;
    }
    return false;
}

std::size_t bounded_string_length(const char* const text, const std::size_t maximum) {
    std::size_t length = 0;
    while (length <= maximum && text[length] != '\0') {
        ++length;
    }
    return length;
}

std::optional<std::string> normalize_torrent_id(const char* const torrent_id) {
    if (torrent_id == nullptr) {
        return std::nullopt;
    }
    const auto length = bounded_string_length(torrent_id, 64);
    if (length != 40 && length != 64) {
        return std::nullopt;
    }
    std::string normalized(torrent_id, length);
    for (auto& character : normalized) {
        const auto value = static_cast<unsigned char>(character);
        if (!std::isxdigit(value)) {
            return std::nullopt;
        }
        character = static_cast<char>(std::tolower(value));
    }
    return normalized;
}

std::optional<std::string> normalize_stream_id(const char* const stream_id) {
    if (stream_id == nullptr) {
        return std::nullopt;
    }
    const auto length = bounded_string_length(stream_id, 64);
    if (length != 64) {
        return std::nullopt;
    }
    std::string normalized(stream_id, length);
    for (auto& character : normalized) {
        const auto value = static_cast<unsigned char>(character);
        if (!std::isxdigit(value)) {
            return std::nullopt;
        }
        character = static_cast<char>(std::tolower(value));
    }
    return normalized;
}

}

std::uint32_t nuvio_engine_api_version() {
    return NUVIO_ENGINE_API_VERSION;
}

const char* nuvio_engine_version_string() {
    return "0.1.5";
}

const char* nuvio_engine_protocol_backend_version() {
#if defined(NUVIO_ENGINE_HAS_LIBTORRENT)
    return libtorrent_version_string();
#else
    return "unavailable";
#endif
}

const char* nuvio_engine_status_message(const nuvio_engine_status status) {
    switch (status) {
    case NUVIO_ENGINE_STATUS_OK:
        return "ok";
    case NUVIO_ENGINE_STATUS_INVALID_ARGUMENT:
        return "invalid argument";
    case NUVIO_ENGINE_STATUS_INCOMPATIBLE_ABI:
        return "incompatible ABI";
    case NUVIO_ENGINE_STATUS_ALLOCATION_FAILED:
        return "allocation failed";
    case NUVIO_ENGINE_STATUS_INITIALIZATION_FAILED:
        return "initialization failed";
    case NUVIO_ENGINE_STATUS_BACKEND_UNAVAILABLE:
        return "protocol backend unavailable";
    case NUVIO_ENGINE_STATUS_QUEUE_FULL:
        return "command queue full";
    case NUVIO_ENGINE_STATUS_NO_EVENT:
        return "no event available";
    case NUVIO_ENGINE_STATUS_NOT_FOUND:
        return "torrent not found";
    case NUVIO_ENGINE_STATUS_METADATA_NOT_READY:
        return "torrent metadata not ready";
    case NUVIO_ENGINE_STATUS_OUT_OF_RANGE:
        return "index out of range";
    }
    return "unknown status";
}

void nuvio_engine_config_init_sized(
    nuvio_engine_config* const config,
    const std::uint32_t struct_size
) {
    if (!initialize_structure(config, struct_size)) {
        return;
    }
    if (struct_size < config_v1_size) {
        return;
    }
    config->memory_cache_capacity_bytes = default_memory_cache_capacity;
    config->disk_cache_capacity_bytes = default_disk_cache_capacity;
    config->upload_mode = NUVIO_ENGINE_UPLOAD_UNLIMITED;
    if (struct_size >= config_v2_size) {
        config->stream_inactivity_timeout_milliseconds =
            default_stream_inactivity_timeout_milliseconds;
    }
    if (struct_size >= config_v3_size) {
        config->warm_torrent_timeout_milliseconds =
            default_warm_torrent_timeout_milliseconds;
    }
    if (struct_size >= config_v5_size) {
        config->torrent_profile = default_torrent_profile;
    }
}

void nuvio_engine_torrent_request_init_sized(
    nuvio_engine_torrent_request* const request,
    const std::uint32_t struct_size
) {
    initialize_structure(request, struct_size);
}

void nuvio_engine_event_init_sized(
    nuvio_engine_event* const event,
    const std::uint32_t struct_size
) {
    if (!initialize_structure(event, struct_size)) {
        return;
    }
    constexpr auto file_index_size =
        offsetof(nuvio_engine_event, file_index) + sizeof(std::uint32_t);
    if (struct_size >= file_index_size) {
        event->file_index = std::numeric_limits<std::uint32_t>::max();
    }
}

void nuvio_engine_file_init_sized(
    nuvio_engine_file* const file,
    const std::uint32_t struct_size
) {
    initialize_structure(file, struct_size);
}

void nuvio_engine_stream_request_init_sized(
    nuvio_engine_stream_request* const request,
    const std::uint32_t struct_size
) {
    if (!initialize_structure(request, struct_size)) {
        return;
    }
    constexpr auto file_index_size =
        offsetof(nuvio_engine_stream_request, file_index) + sizeof(std::uint32_t);
    if (struct_size >= file_index_size) {
        request->file_index = std::numeric_limits<std::uint32_t>::max();
    }
}

void nuvio_engine_stats_init_sized(
    nuvio_engine_stats* const stats,
    const std::uint32_t struct_size
) {
    initialize_structure(stats, struct_size);
}

void nuvio_engine_stream_stats_init_sized(
    nuvio_engine_stream_stats* const stats,
    const std::uint32_t struct_size
) {
    initialize_structure(stats, struct_size);
}

std::size_t nuvio_engine_session_diagnostics(
    const nuvio_engine* const engine,
    char* const out,
    const std::size_t out_size
) {
    if (engine == nullptr || out == nullptr || out_size == 0) {
        return 0;
    }
    const std::string diagnostics = engine->runtime->session_diagnostics();
    const std::size_t written = std::min(out_size - 1, diagnostics.size());
    std::memcpy(out, diagnostics.data(), written);
    out[written] = '\0';
    return written;
}

nuvio_engine_status nuvio_engine_create(
    const nuvio_engine_config* const config,
    nuvio_engine** const engine
) {
    if (engine == nullptr) {
        return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    *engine = nullptr;
    if (config == nullptr) {
        return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    if (config->struct_size < config_v1_size) {
        return NUVIO_ENGINE_STATUS_INCOMPATIBLE_ABI;
    }
    if (config->data_directory == nullptr || config->cache_directory == nullptr) {
        return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    if (config->data_directory[0] == '\0' || config->cache_directory[0] == '\0') {
        return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    if (!valid_upload_configuration(*config)) {
        return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    const auto torrent_profile = config->struct_size >= config_v5_size
        ? config->torrent_profile
        : default_torrent_profile;
    if (!valid_torrent_profile(torrent_profile)) {
        return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
    }

    try {
        const auto stream_inactivity_timeout = config->struct_size >= config_v2_size
            ? config->stream_inactivity_timeout_milliseconds
            : default_stream_inactivity_timeout_milliseconds;
        const auto warm_torrent_timeout = config->struct_size >= config_v3_size
            ? config->warm_torrent_timeout_milliseconds
            : default_warm_torrent_timeout_milliseconds;
        std::string tls_ca_bundle_path;
        if (config->struct_size >= config_v4_size && config->tls_ca_bundle_path != nullptr) {
            constexpr std::size_t maximum_path_length = 16 * 1024;
            const auto length = bounded_string_length(
                config->tls_ca_bundle_path,
                maximum_path_length
            );
            if (length == 0 || length > maximum_path_length) {
                return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
            }
            tls_ca_bundle_path.assign(config->tls_ca_bundle_path, length);
        }
        *engine = new nuvio_engine{
            config->data_directory,
            config->cache_directory,
            config->memory_cache_capacity_bytes,
            config->disk_cache_capacity_bytes,
            config->listen_port,
            config->upload_mode,
            config->upload_limit_bytes_per_second,
            stream_inactivity_timeout,
            warm_torrent_timeout,
            torrent_profile,
            std::make_unique<nuvio::core::EngineRuntime>(
                nuvio::torrent::create_protocol_backend({
                    config->upload_mode,
                    config->upload_limit_bytes_per_second,
                    config->data_directory,
                    config->cache_directory,
                    config->listen_port,
                    config->memory_cache_capacity_bytes,
                    config->disk_cache_capacity_bytes,
                    stream_inactivity_timeout,
                    warm_torrent_timeout,
                    torrent_profile,
                    std::move(tls_ca_bundle_path),
                }),
                config->cache_directory
            ),
        };
    } catch (const std::bad_alloc&) {
        return NUVIO_ENGINE_STATUS_ALLOCATION_FAILED;
    } catch (...) {
        return NUVIO_ENGINE_STATUS_INITIALIZATION_FAILED;
    }
    return NUVIO_ENGINE_STATUS_OK;
}

void nuvio_engine_destroy(nuvio_engine* const engine) {
    delete engine;
}

nuvio_engine_status nuvio_engine_add_torrent(
    nuvio_engine* const engine,
    const nuvio_engine_torrent_request* const request,
    std::uint64_t* const request_id
) {
    if (engine == nullptr || request == nullptr || request_id == nullptr) {
        return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    *request_id = 0;
    if (request->struct_size < torrent_request_v1_size) {
        return NUVIO_ENGINE_STATUS_INCOMPATIBLE_ABI;
    }
    try {
        nuvio::torrent::TorrentInput input{};
        const auto has_v2_fields = request->struct_size >= torrent_request_v2_size;
        const auto source_type = has_v2_fields
            ? request->source_type
            : static_cast<nuvio_engine_torrent_source_type>(
                  NUVIO_ENGINE_TORRENT_SOURCE_MAGNET
              );
        if (source_type == NUVIO_ENGINE_TORRENT_SOURCE_MAGNET) {
            if (request->magnet_uri == nullptr) {
                return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
            }
            constexpr std::size_t maximum_magnet_length = 16 * 1024;
            const auto length = bounded_string_length(
                request->magnet_uri,
                maximum_magnet_length
            );
            if (length == 0 || length > maximum_magnet_length) {
                return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
            }
            input.type = nuvio::torrent::TorrentInputType::magnet;
            input.magnet_uri.assign(request->magnet_uri, length);
        } else if (source_type == NUVIO_ENGINE_TORRENT_SOURCE_DATA) {
            constexpr std::size_t maximum_torrent_size = 4 * 1024 * 1024;
            if (!has_v2_fields || request->torrent_data == nullptr ||
                request->torrent_data_size == 0 ||
                request->torrent_data_size > maximum_torrent_size) {
                return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
            }
            input.type = nuvio::torrent::TorrentInputType::torrent_data;
            const auto* begin = reinterpret_cast<const char*>(request->torrent_data);
            input.torrent_data.assign(begin, begin + request->torrent_data_size);
        } else {
            return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
        }
        return engine->runtime->add_torrent(std::move(input), *request_id);
    } catch (const std::bad_alloc&) {
        return NUVIO_ENGINE_STATUS_ALLOCATION_FAILED;
    } catch (...) {
        return NUVIO_ENGINE_STATUS_INITIALIZATION_FAILED;
    }
}

nuvio_engine_status nuvio_engine_get_file_count(
    nuvio_engine* const engine,
    const char* const torrent_id,
    std::size_t* const file_count
) {
    if (engine == nullptr || file_count == nullptr) {
        return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    *file_count = 0;
    try {
        const auto normalized = normalize_torrent_id(torrent_id);
        if (!normalized.has_value()) {
            return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
        }
        return engine->runtime->get_file_count(*normalized, *file_count);
    } catch (const std::bad_alloc&) {
        return NUVIO_ENGINE_STATUS_ALLOCATION_FAILED;
    } catch (...) {
        return NUVIO_ENGINE_STATUS_INITIALIZATION_FAILED;
    }
}

nuvio_engine_status nuvio_engine_get_file(
    nuvio_engine* const engine,
    const char* const torrent_id,
    const std::size_t file_index,
    nuvio_engine_file* const file
) {
    if (engine == nullptr || file == nullptr) {
        return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    const auto caller_size = static_cast<std::size_t>(file->struct_size);
    if (caller_size < file_v1_size) {
        return NUVIO_ENGINE_STATUS_INCOMPATIBLE_ABI;
    }
    try {
        const auto normalized = normalize_torrent_id(torrent_id);
        if (!normalized.has_value()) {
            return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
        }
        nuvio_engine_file available_file{};
        const auto status = engine->runtime->get_file(
            *normalized,
            file_index,
            available_file
        );
        if (status != NUVIO_ENGINE_STATUS_OK) {
            return status;
        }
        std::memcpy(file, &available_file, std::min(caller_size, sizeof(available_file)));
        return NUVIO_ENGINE_STATUS_OK;
    } catch (const std::bad_alloc&) {
        return NUVIO_ENGINE_STATUS_ALLOCATION_FAILED;
    } catch (...) {
        return NUVIO_ENGINE_STATUS_INITIALIZATION_FAILED;
    }
}

nuvio_engine_status nuvio_engine_prepare_stream(
    nuvio_engine* const engine,
    const nuvio_engine_stream_request* const request,
    std::uint64_t* const request_id
) {
    if (engine == nullptr || request == nullptr || request_id == nullptr) {
        return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    *request_id = 0;
    if (request->struct_size < stream_request_v1_size) {
        return NUVIO_ENGINE_STATUS_INCOMPATIBLE_ABI;
    }
    try {
        const auto normalized = normalize_torrent_id(request->torrent_id);
        if (!normalized.has_value()) {
            return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
        }
        std::string filename_hint;
        if (request->filename_hint != nullptr) {
            constexpr std::size_t maximum_hint_length = 4 * 1024;
            const auto length = bounded_string_length(
                request->filename_hint,
                maximum_hint_length
            );
            if (length > maximum_hint_length) {
                return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
            }
            filename_hint.assign(request->filename_hint, length);
        }
        const auto requested_index = request->file_index ==
                std::numeric_limits<std::uint32_t>::max()
            ? std::optional<std::size_t>{}
            : std::optional<std::size_t>{request->file_index};
        return engine->runtime->prepare_stream(
            *normalized,
            requested_index,
            std::move(filename_hint),
            *request_id
        );
    } catch (const std::bad_alloc&) {
        return NUVIO_ENGINE_STATUS_ALLOCATION_FAILED;
    } catch (...) {
        return NUVIO_ENGINE_STATUS_INITIALIZATION_FAILED;
    }
}

nuvio_engine_status nuvio_engine_remove_torrent(
    nuvio_engine* const engine,
    const char* const torrent_id,
    std::uint64_t* const request_id
) {
    if (engine == nullptr || request_id == nullptr) {
        return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    *request_id = 0;
    try {
        const auto normalized = normalize_torrent_id(torrent_id);
        if (!normalized.has_value()) {
            return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
        }
        return engine->runtime->remove_torrent(*normalized, *request_id);
    } catch (const std::bad_alloc&) {
        return NUVIO_ENGINE_STATUS_ALLOCATION_FAILED;
    } catch (...) {
        return NUVIO_ENGINE_STATUS_INITIALIZATION_FAILED;
    }
}

nuvio_engine_status nuvio_engine_poll_event(
    nuvio_engine* const engine,
    nuvio_engine_event* const event
) {
    if (engine == nullptr || event == nullptr) {
        return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    const auto caller_size = static_cast<std::size_t>(event->struct_size);
    if (caller_size < event_v1_size) {
        return NUVIO_ENGINE_STATUS_INCOMPATIBLE_ABI;
    }
    nuvio_engine_event available_event{};
    const auto status = engine->runtime->poll_event(available_event);
    if (status != NUVIO_ENGINE_STATUS_OK) {
        return status;
    }
    std::memcpy(event, &available_event, std::min(caller_size, sizeof(available_event)));
    return NUVIO_ENGINE_STATUS_OK;
}

nuvio_engine_status nuvio_engine_stop_stream(
    nuvio_engine* const engine,
    const char* const stream_id,
    std::uint64_t* const request_id
) {
    if (engine == nullptr || request_id == nullptr) {
        return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    *request_id = 0;
    try {
        const auto normalized = normalize_stream_id(stream_id);
        if (!normalized.has_value()) {
            return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
        }
        return engine->runtime->stop_stream(*normalized, *request_id);
    } catch (const std::bad_alloc&) {
        return NUVIO_ENGINE_STATUS_ALLOCATION_FAILED;
    } catch (...) {
        return NUVIO_ENGINE_STATUS_INITIALIZATION_FAILED;
    }
}

nuvio_engine_status nuvio_engine_get_stats(
    nuvio_engine* const engine,
    nuvio_engine_stats* const stats
) {
    if (engine == nullptr || stats == nullptr) {
        return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    const auto caller_size = static_cast<std::size_t>(stats->struct_size);
    if (caller_size < stats_v1_size) {
        return NUVIO_ENGINE_STATUS_INCOMPATIBLE_ABI;
    }
    try {
        const auto snapshot = engine->runtime->get_stats();
        std::memcpy(stats, &snapshot, std::min(caller_size, sizeof(snapshot)));
        return NUVIO_ENGINE_STATUS_OK;
    } catch (...) {
        return NUVIO_ENGINE_STATUS_INITIALIZATION_FAILED;
    }
}

nuvio_engine_status nuvio_engine_get_stream_stats(
    nuvio_engine* const engine,
    const char* const stream_id,
    nuvio_engine_stream_stats* const stats
) {
    if (engine == nullptr || stats == nullptr) {
        return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    const auto caller_size = static_cast<std::size_t>(stats->struct_size);
    if (caller_size < stream_stats_v1_size) {
        return NUVIO_ENGINE_STATUS_INCOMPATIBLE_ABI;
    }
    try {
        const auto normalized = normalize_stream_id(stream_id);
        if (!normalized.has_value()) {
            return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
        }
        nuvio_engine_stream_stats snapshot{};
        const auto status = engine->runtime->get_stream_stats(*normalized, snapshot);
        if (status != NUVIO_ENGINE_STATUS_OK) {
            return status;
        }
        std::memcpy(stats, &snapshot, std::min(caller_size, sizeof(snapshot)));
        return NUVIO_ENGINE_STATUS_OK;
    } catch (...) {
        return NUVIO_ENGINE_STATUS_INITIALIZATION_FAILED;
    }
}

nuvio_engine_status nuvio_engine_reclaim_disk_cache(
    nuvio_engine* const engine,
    const std::uint64_t target_bytes,
    std::uint64_t* const request_id
) {
    if (engine == nullptr || request_id == nullptr) {
        return NUVIO_ENGINE_STATUS_INVALID_ARGUMENT;
    }
    *request_id = 0;
    try {
        return engine->runtime->reclaim_disk_cache(target_bytes, *request_id);
    } catch (const std::bad_alloc&) {
        return NUVIO_ENGINE_STATUS_ALLOCATION_FAILED;
    } catch (...) {
        return NUVIO_ENGINE_STATUS_INITIALIZATION_FAILED;
    }
}
