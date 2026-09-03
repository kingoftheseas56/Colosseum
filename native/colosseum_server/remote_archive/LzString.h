#pragma once

#include <QString>

#include <optional>

namespace Colosseum::Server::RemoteArchive {

// Stremio 4.20.17 module 77: lz-string decompressFromEncodedURIComponent.
// Only the decode direction is needed by the native /create?lz= route surface.
[[nodiscard]] std::optional<QString> decompressLzEncodedURIComponent(QString input);

} // namespace Colosseum::Server::RemoteArchive
