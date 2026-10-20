#include "nuvioengineapi.h"

#include <QLibrary>

namespace Colosseum::Nuvio {
namespace {

template <typename Symbol>
bool resolve(QLibrary &library, const char *name, Symbol &target) {
    target = reinterpret_cast<Symbol>(library.resolve(name));
    return target != nullptr;
}

} // namespace

EngineApiLoadResult loadEngineApi(const QString &libraryPath) {
    EngineApiLoadResult result;
    if (libraryPath.isEmpty()) {
        result.diagnostic = QStringLiteral("no nuvio engine library path configured");
        return result;
    }
    QLibrary library(libraryPath);
    if (!library.load()) {
        result.diagnostic = QStringLiteral("cannot load nuvio engine library %1: %2")
                                .arg(libraryPath, library.errorString());
        return result;
    }
    EngineApi api;
    const bool resolved =
        resolve(library, "nuvio_engine_api_version", api.api_version) &&
        resolve(library, "nuvio_engine_version_string", api.version_string) &&
        resolve(library, "nuvio_engine_status_message", api.status_message) &&
        resolve(library, "nuvio_engine_config_init_sized", api.config_init_sized) &&
        resolve(
            library,
            "nuvio_engine_torrent_request_init_sized",
            api.torrent_request_init_sized
        ) &&
        resolve(library, "nuvio_engine_event_init_sized", api.event_init_sized) &&
        resolve(
            library,
            "nuvio_engine_stream_request_init_sized",
            api.stream_request_init_sized
        ) &&
        resolve(library, "nuvio_engine_stats_init_sized", api.stats_init_sized) &&
        resolve(
            library,
            "nuvio_engine_stream_stats_init_sized",
            api.stream_stats_init_sized
        ) &&
        resolve(library, "nuvio_engine_create", api.create) &&
        resolve(library, "nuvio_engine_destroy", api.destroy) &&
        resolve(library, "nuvio_engine_add_torrent", api.add_torrent) &&
        resolve(library, "nuvio_engine_poll_event", api.poll_event) &&
        resolve(library, "nuvio_engine_get_file_count", api.get_file_count) &&
        resolve(library, "nuvio_engine_get_file", api.get_file) &&
        resolve(library, "nuvio_engine_prepare_stream", api.prepare_stream) &&
        resolve(library, "nuvio_engine_remove_torrent", api.remove_torrent) &&
        resolve(library, "nuvio_engine_stop_stream", api.stop_stream) &&
        resolve(library, "nuvio_engine_get_stats", api.get_stats) &&
        resolve(library, "nuvio_engine_get_stream_stats", api.get_stream_stats);
    if (!resolved || !api.resolves()) {
        result.diagnostic = QStringLiteral(
            "nuvio engine library %1 does not export the pinned C API"
        ).arg(libraryPath);
        return result;
    }
    if (api.api_version() != NUVIO_ENGINE_API_VERSION) {
        result.diagnostic = QStringLiteral(
            "nuvio engine library %1 reports API version %2, expected %3"
        ).arg(libraryPath).arg(api.api_version()).arg(NUVIO_ENGINE_API_VERSION);
        return result;
    }
    result.api = api;
    return result;
}

} // namespace Colosseum::Nuvio
