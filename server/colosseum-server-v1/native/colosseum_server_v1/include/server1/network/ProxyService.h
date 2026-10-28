#pragma once
namespace server1::network {
// Register the source M803 proxy. Router owns the network service; requests and
// callbacks run on the HTTP application's Qt event-loop thread.
int registerProxyRoutes(void *router);
} // namespace server1::network
