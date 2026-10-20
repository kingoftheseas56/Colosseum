#ifndef NUVIO_ENGINE_RANDOM_BYTES_HPP
#define NUVIO_ENGINE_RANDOM_BYTES_HPP

#include <cstddef>
#include <string>

namespace nuvio::security {

[[nodiscard]] std::string random_hex_token(std::size_t byte_count);

}

#endif
