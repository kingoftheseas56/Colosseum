// Function-table mirror of the pinned nuvio_engine C API (API version 3,
// source pin recorded in native/third_party/nuvio-engine/SOURCE_MANIFEST.md).
//
// The production Android backend resolves this table at runtime from the
// private, independently built and hardened libnuvio_engine.so. Keeping the C
// boundary behind a function table means focused tests can inject a scripted
// provider instead of reimplementing the backend algorithm against a fake.
//
// Only C POD/function boundaries cross into the engine library: no C++
// objects, exceptions or allocators are shared with it.
#ifndef COLOSSEUM_PLAYER_NUVIOENGINEAPI_H
#define COLOSSEUM_PLAYER_NUVIOENGINEAPI_H

#include "third_party/nuvio-engine/include/nuvio_engine/nuvio_engine.h"

#include <QString>

namespace Colosseum::Nuvio {

struct EngineApi {
    uint32_t (*api_version)() = nullptr;
    const char *(*version_string)() = nullptr;
    const char *(*status_message)(nuvio_engine_status status) = nullptr;
    void (*config_init_sized)(nuvio_engine_config *config, uint32_t struct_size) = nullptr;
    void (*torrent_request_init_sized)(
        nuvio_engine_torrent_request *request,
        uint32_t struct_size
    ) = nullptr;
    void (*event_init_sized)(nuvio_engine_event *event, uint32_t struct_size) = nullptr;
    void (*stream_request_init_sized)(
        nuvio_engine_stream_request *request,
        uint32_t struct_size
    ) = nullptr;
    void (*stats_init_sized)(nuvio_engine_stats *stats, uint32_t struct_size) = nullptr;
    void (*stream_stats_init_sized)(
        nuvio_engine_stream_stats *stats,
        uint32_t struct_size
    ) = nullptr;
    nuvio_engine_status (*create)(
        const nuvio_engine_config *config,
        nuvio_engine **engine
    ) = nullptr;
    void (*destroy)(nuvio_engine *engine) = nullptr;
    nuvio_engine_status (*add_torrent)(
        nuvio_engine *engine,
        const nuvio_engine_torrent_request *request,
        uint64_t *request_id
    ) = nullptr;
    nuvio_engine_status (*poll_event)(nuvio_engine *engine, nuvio_engine_event *event) = nullptr;
    nuvio_engine_status (*get_file_count)(
        nuvio_engine *engine,
        const char *torrent_id,
        size_t *file_count
    ) = nullptr;
    nuvio_engine_status (*get_file)(
        nuvio_engine *engine,
        const char *torrent_id,
        size_t file_index,
        nuvio_engine_file *file
    ) = nullptr;
    nuvio_engine_status (*prepare_stream)(
        nuvio_engine *engine,
        const nuvio_engine_stream_request *request,
        uint64_t *request_id
    ) = nullptr;
    nuvio_engine_status (*remove_torrent)(
        nuvio_engine *engine,
        const char *torrent_id,
        uint64_t *request_id
    ) = nullptr;
    nuvio_engine_status (*stop_stream)(
        nuvio_engine *engine,
        const char *stream_id,
        uint64_t *request_id
    ) = nullptr;
    nuvio_engine_status (*get_stats)(nuvio_engine *engine, nuvio_engine_stats *stats) = nullptr;
    nuvio_engine_status (*get_stream_stats)(
        nuvio_engine *engine,
        const char *stream_id,
        nuvio_engine_stream_stats *stats
    ) = nullptr;

    [[nodiscard]] bool resolves() const {
        return api_version != nullptr && config_init_sized != nullptr &&
            torrent_request_init_sized != nullptr && event_init_sized != nullptr &&
            stream_request_init_sized != nullptr && stats_init_sized != nullptr &&
            stream_stats_init_sized != nullptr && create != nullptr &&
            destroy != nullptr && add_torrent != nullptr && poll_event != nullptr &&
            get_file_count != nullptr && get_file != nullptr &&
            prepare_stream != nullptr && stop_stream != nullptr &&
            remove_torrent != nullptr &&
            get_stats != nullptr && get_stream_stats != nullptr;
    }
};

struct EngineApiLoadResult {
    EngineApi api;
    QString diagnostic;
};

// Resolves the pinned C API from the given private engine library. Never
// falls back to an unpatched artifact: the caller decides which library path
// is trustworthy, and any resolve or ABI mismatch is reported in the
// diagnostic with an empty table.
EngineApiLoadResult loadEngineApi(const QString &libraryPath);

} // namespace Colosseum::Nuvio

#endif // COLOSSEUM_PLAYER_NUVIOENGINEAPI_H
