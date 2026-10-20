#include "torrent/protocol_backend.hpp"

namespace nuvio::torrent {

std::unique_ptr<ProtocolBackend> create_protocol_backend(const ProtocolBackendConfig&) {
    return nullptr;
}

}
