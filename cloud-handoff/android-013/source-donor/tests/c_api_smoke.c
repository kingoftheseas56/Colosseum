#include "nuvio_engine/nuvio_engine.h"

int main(void) {
    nuvio_engine_config config;
    nuvio_engine* engine = NULL;
    nuvio_engine_config_init(&config);
    config.data_directory = "/tmp/nuvio-engine-c-data";
    config.cache_directory = "/tmp/nuvio-engine-c-cache";
    if (nuvio_engine_api_version() != NUVIO_ENGINE_API_VERSION) {
        return 1;
    }
    if (nuvio_engine_protocol_backend_version() == NULL) {
        return 2;
    }
    if (nuvio_engine_create(&config, &engine) != NUVIO_ENGINE_STATUS_OK) {
        return 3;
    }
    if (engine == NULL) {
        return 4;
    }
    {
        nuvio_engine_event event;
        nuvio_engine_event_init(&event);
        if (nuvio_engine_poll_event(engine, &event) != NUVIO_ENGINE_STATUS_NO_EVENT) {
            return 5;
        }
    }
    {
        nuvio_engine_file file;
        nuvio_engine_file_init(&file);
        if (file.struct_size != sizeof(nuvio_engine_file)) {
            return 6;
        }
    }
    {
        nuvio_engine_stream_request request;
        nuvio_engine_stream_request_init(&request);
        if (request.struct_size != sizeof(nuvio_engine_stream_request)) {
            return 7;
        }
        if (request.file_index != UINT32_MAX) {
            return 8;
        }
    }
    {
        nuvio_engine_stats stats;
        nuvio_engine_stats_init(&stats);
        if (stats.struct_size != sizeof(nuvio_engine_stats)) {
            return 9;
        }
        if (nuvio_engine_get_stats(engine, &stats) != NUVIO_ENGINE_STATUS_OK) {
            return 10;
        }
    }
    {
        nuvio_engine_stream_stats stats;
        nuvio_engine_stream_stats_init(&stats);
        if (stats.struct_size != sizeof(nuvio_engine_stream_stats)) {
            return 11;
        }
        if (nuvio_engine_get_stream_stats(engine, "invalid", &stats) !=
            NUVIO_ENGINE_STATUS_INVALID_ARGUMENT) {
            return 12;
        }
    }
    {
        uint64_t request_id = 99;
        if (nuvio_engine_stop_stream(engine, "invalid", &request_id) !=
            NUVIO_ENGINE_STATUS_INVALID_ARGUMENT) {
            return 13;
        }
        if (request_id != 0) {
            return 14;
        }
    }
    {
        struct legacy_event {
            uint32_t struct_size;
            nuvio_engine_event_type type;
            uint64_t sequence;
            uint64_t request_id;
            uint64_t dropped_events;
            char torrent_id[65];
            char message[256];
        };
        struct legacy_event_storage {
            struct legacy_event value;
            uint64_t canary;
        } storage;
        storage.canary = UINT64_C(0x123456789abcdef0);
        nuvio_engine_event_init_sized(
            (nuvio_engine_event*)&storage.value,
            (uint32_t)sizeof(storage.value)
        );
        if (storage.value.struct_size != sizeof(storage.value)) {
            return 13;
        }
        if (storage.canary != UINT64_C(0x123456789abcdef0)) {
            return 14;
        }
    }
    {
        struct legacy_stats {
            uint32_t struct_size;
            uint32_t active_torrents;
            uint32_t active_streams;
            uint32_t active_http_requests;
            uint32_t connected_peers;
            uint32_t connected_seeds;
            uint32_t pending_piece_reads;
            uint32_t reserved_0;
            uint64_t download_rate_bytes_per_second;
            uint64_t upload_rate_bytes_per_second;
            uint64_t total_payload_download_bytes;
            uint64_t total_payload_upload_bytes;
            uint64_t memory_cache_capacity_bytes;
            uint64_t memory_cache_used_bytes;
            uint64_t memory_cache_hits;
            uint64_t memory_cache_misses;
            uint64_t memory_cache_evictions;
            uint64_t memory_cache_entries;
        };
        struct legacy_stats_storage {
            struct legacy_stats value;
            uint64_t canary;
        } storage;
        storage.canary = UINT64_C(0x55aa55aa55aa55aa);
        nuvio_engine_stats_init_sized(
            (nuvio_engine_stats*)&storage.value,
            (uint32_t)sizeof(storage.value)
        );
        if (nuvio_engine_get_stats(
                engine,
                (nuvio_engine_stats*)&storage.value
            ) != NUVIO_ENGINE_STATUS_OK) {
            return 15;
        }
        if (storage.canary != UINT64_C(0x55aa55aa55aa55aa)) {
            return 16;
        }
    }
    nuvio_engine_destroy(engine);
    {
        struct legacy_config {
            uint32_t struct_size;
            const char* data_directory;
            const char* cache_directory;
            uint64_t memory_cache_capacity_bytes;
            uint64_t disk_cache_capacity_bytes;
            uint16_t listen_port;
            uint16_t reserved_0;
            nuvio_engine_upload_mode upload_mode;
            uint64_t upload_limit_bytes_per_second;
        };
        struct legacy_config_storage {
            struct legacy_config value;
            uint64_t canary;
        } storage;
        nuvio_engine* legacy_engine = NULL;
        storage.canary = UINT64_C(0x0fedcba987654321);
        nuvio_engine_config_init_sized(
            (nuvio_engine_config*)&storage.value,
            (uint32_t)sizeof(storage.value)
        );
        storage.value.data_directory = "/tmp/nuvio-engine-c-legacy-data";
        storage.value.cache_directory = "/tmp/nuvio-engine-c-legacy-cache";
        if (storage.canary != UINT64_C(0x0fedcba987654321)) {
            return 17;
        }
        if (nuvio_engine_create(
                (const nuvio_engine_config*)&storage.value,
                &legacy_engine
            ) != NUVIO_ENGINE_STATUS_OK) {
            return 18;
        }
        nuvio_engine_destroy(legacy_engine);
    }
    return 0;
}
