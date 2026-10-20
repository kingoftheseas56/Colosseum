#include "nuvio_engine/nuvio_engine.h"

#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 3) {
        return 2;
    }

    nuvio_engine_config config{};
    nuvio_engine_config_init(&config);
    config.data_directory = argv[1];
    config.cache_directory = argv[2];

    nuvio_engine* engine = nullptr;
    const auto status = nuvio_engine_create(&config, &engine);
    if (status != NUVIO_ENGINE_STATUS_OK) {
        std::fprintf(stderr, "create failed: %s\n", nuvio_engine_status_message(status));
        return 1;
    }
    if (engine == nullptr || nuvio_engine_api_version() != NUVIO_ENGINE_API_VERSION) {
        nuvio_engine_destroy(engine);
        return 1;
    }

    std::printf(
        "Nuvio Engine %s (%s)\n",
        nuvio_engine_version_string(),
        nuvio_engine_protocol_backend_version()
    );
    nuvio_engine_destroy(engine);
    return 0;
}
