#pragma once
namespace server1::http {
// Registry is owned by mounted route contexts and outstanding responses.
// No application/global credentials or filesystem extraction are involved.
void mountArchiveRoutes(void *router);
}
