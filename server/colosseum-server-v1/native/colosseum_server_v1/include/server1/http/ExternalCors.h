#pragma once
namespace server1::http {
// M564 external-router filter, distinct from unconditional root-router CORS.
// Install before external service routes. Fixed expressions intentionally
// retain JavaScript's unescaped dots and unanchored substring matching.
bool installExternalCors(void *router);
} // namespace server1::http
