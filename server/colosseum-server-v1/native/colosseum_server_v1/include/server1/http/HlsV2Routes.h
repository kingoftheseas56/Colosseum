#pragma once
#include <QString>
#include <functional>
namespace server1::media {
class HlsV2Service;
}
namespace server1::http {
// Both routes use one registry; baseUrlLocal is the source torrent URL origin.
// Service must remain alive while routes are installed. Route contexts hold a
// weak QObject reference.
bool installHlsV2Routes(void *router, media::HlsV2Service *service,
                        QString baseUrlLocal);
bool installHlsV2Routes(void *router, media::HlsV2Service *service,
                        std::function<QString()> baseUrlLocal);
} // namespace server1::http
