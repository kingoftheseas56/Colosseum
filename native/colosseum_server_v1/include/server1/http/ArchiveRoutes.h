#pragma once
namespace server1::http {
// Each family has a ten-active FIFO preparation queue, shared by query key.
// Registry/source lifetimes belong to mounted contexts and outstanding responses;
// response readers and cancellation remain independent.
// No application/global credentials or filesystem extraction are involved.
void mountArchiveRoutes(void *router);
}
