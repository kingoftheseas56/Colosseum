#pragma once

#include <cstddef>

// The packet owns this C ABI until the Server 1.0 integration seam publishes the
// equivalent shared header.  Handles are opaque and remain owned by the caller
// unless a response stream is transferred to the connection by dispatch.

inline constexpr std::size_t SERVER1_HTTP_UNKNOWN_CONTENT_LENGTH = static_cast<std::size_t>(-1);

inline constexpr std::ptrdiff_t SERVER1_HTTP_WOULD_BLOCK = -2;

extern "C" {

using server1_http_stream_read_fn = std::ptrdiff_t (*)(void *context, void *buffer,
                                                        std::size_t capacity);
using server1_http_stream_destroy_fn = void (*)(void *context);
using server1_http_context_destroy_fn = void (*)(void *context);
using server1_http_next_fn = int (*)(void *nextContext);
using server1_http_route_handler = int (*)(const void *request, void *response,
                                            server1_http_next_fn next, void *nextContext,
                                            void *context);

void *server1_http_parser_create(std::size_t maxBodyBytes);
void server1_http_parser_destroy(void *parser);
int server1_http_parser_feed(void *parser, const char *data, std::size_t size);
int server1_http_parser_error_status(void *parser);
const char *server1_http_parser_error(void *parser);
std::size_t server1_http_parser_consumed(void *parser);
const char *server1_http_parser_remaining(void *parser);
std::size_t server1_http_parser_remaining_size(void *parser);
const char *server1_http_parser_method(void *parser);
const char *server1_http_parser_target(void *parser);
const char *server1_http_parser_path(void *parser);
const char *server1_http_parser_body(void *parser);
std::size_t server1_http_parser_body_size(void *parser);
std::size_t server1_http_parser_header_entry_count(void *parser);
const char *server1_http_parser_header_entry_name(void *parser, std::size_t index);
const char *server1_http_parser_header_entry_value(void *parser, std::size_t index);
int server1_http_parser_body_kind(void *parser);
bool server1_http_parser_keep_alive(void *parser);
std::size_t server1_http_parser_header_count(void *parser, const char *name);
const char *server1_http_parser_header_value_at(void *parser, const char *name,
                                                 std::size_t index);
std::size_t server1_http_parser_query_entry_count(void *parser);
const char *server1_http_parser_query_key_at(void *parser, std::size_t index);
const char *server1_http_parser_query_value_at(void *parser, std::size_t index);
std::size_t server1_http_parser_form_entry_count(void *parser);
const char *server1_http_parser_form_key_at(void *parser, std::size_t index);
const char *server1_http_parser_form_value_at(void *parser, std::size_t index);

void *server1_http_router_create();
void server1_http_router_destroy(void *router);
int server1_http_router_add_static(void *router, int external, int prefix, const char *method,
                                    const char *pattern, int status, const char *body);
int server1_http_router_add_handler(void *router, int external, int prefix, const char *method,
                                    const char *pattern, server1_http_route_handler handler,
                                    void *context, server1_http_context_destroy_fn destroy);
int server1_http_router_add_middleware(void *router, int external, const char *pattern,
                                       server1_http_route_handler handler, void *context,
                                       server1_http_context_destroy_fn destroy);
const char *server1_http_router_error(void *router);
void *server1_http_router_dispatch(void *router, const char *method, const char *target,
                                   const char *body);

// Copies a completed parser request, including ordered duplicate headers and
// exact body length, before entering the synchronous handler chain.
void *server1_http_router_dispatch_parsed(void *router, void *parser);
std::size_t server1_http_request_body_size(const void *request);
std::size_t server1_http_request_header_count(const void *request, const char *name);
const char *server1_http_request_header_value_at(const void *request, const char *name, std::size_t index);
const char *server1_http_request_method(const void *request);
const char *server1_http_request_target(const void *request);
const char *server1_http_request_path(const void *request);
const char *server1_http_request_body(const void *request);
const char *server1_http_request_param(const void *request, const char *name);
const char *server1_http_request_query_value(const void *request, const char *name);

// Transfers one lifetime context on success. The nonthrowing callback runs once
// when the response is retired (socket drained or aborted), not at reader EOF.
// Active producer reads retain stream context and lifetime through callback
// unwind, retiring stream ownership before response lifetime callbacks.
// Body/stream replacement does not retire it. Failure retains caller ownership.
int server1_http_response_set_lifetime(void *response, server1_http_context_destroy_fn destroy,
                                      void *context);
void server1_http_response_destroy(void *response);
int server1_http_response_status(void *response);
const char *server1_http_response_body(void *response);
std::size_t server1_http_response_body_size(void *response);
const char *server1_http_response_header(void *response, const char *name);
std::size_t server1_http_response_header_count(void *response);
const char *server1_http_response_header_name_at(void *response, std::size_t index);
const char *server1_http_response_header_value_at(void *response, std::size_t index);
int server1_http_response_close(void *response);
int server1_http_response_set_status(void *response, int status);
int server1_http_response_set_body(void *response, const void *data, std::size_t size);
int server1_http_response_set_header(void *response, const char *name, const char *value);
int server1_http_response_set_close(void *response, int closeAfter);
int server1_http_response_set_stream(void *response, server1_http_stream_read_fn read,
                                     server1_http_stream_destroy_fn destroy, void *context,
                                     std::size_t contentLength);
// Opt-in async stream. All wake operations use the serialized app lane.
// Legacy negative reads remain errors. Wake handles are caller-owned weak
// references; notify after response retirement returns 0. Destroy each once.
int server1_http_response_set_async_stream(void *response, server1_http_stream_read_fn read,
    server1_http_stream_destroy_fn destroy, void *context, std::size_t contentLength);
int server1_http_response_stream_can_wait(void *response);
void *server1_http_response_stream_wake_handle(void *response);
int server1_http_stream_wake_notify(void *handle);
void server1_http_stream_wake_destroy(void *handle);
using server1_http_wake_fn = void (*)(void *context);
// One connection subscription per stream; context transfers only on success.
// Callback and destructor must not throw.
int server1_http_response_bind_wake(void *response, server1_http_wake_fn callback,
    void *context, server1_http_context_destroy_fn destroy);
// Deferred metadata response, serialized app lane only. Handle is caller-owned,
// weak, follows response moves, and is invalidated before lifetime teardown.
// Do not retain the borrowed response pointer across event-loop turns.
void *server1_http_response_defer(void *response);
void *server1_http_deferred_response(void *handle);
int server1_http_deferred_complete(void *handle);
void server1_http_deferred_destroy(void *handle);
int server1_http_response_is_deferred(void *response);
// Internal connection subscription, ownership transfers on success.
int server1_http_response_bind_deferred(void *response, server1_http_wake_fn callback,
    void *context, server1_http_context_destroy_fn destroy);
int server1_http_response_has_stream(void *response);
std::size_t server1_http_response_stream_content_length(void *response);
std::ptrdiff_t server1_http_response_stream_read(void *response, void *buffer,
                                                 std::size_t capacity);

void *server1_http_server_create(void *router, std::size_t maxQueuedBytes);
// Invalidates the handle and stops I/O immediately. QObject storage is released through deleteLater; the
// owning Qt event loop must drain deferred deletes before it is destroyed.
void server1_http_server_destroy(void *server);
int server1_http_server_listen(void *server, unsigned short port);
void server1_http_server_stop(void *server);
unsigned short server1_http_server_port(void *server);
const char *server1_http_server_error(void *server);
void server1_http_server_set_drain_paused(void *server, int paused);
std::size_t server1_http_server_queued_bytes(void *server);
std::size_t server1_http_server_max_queued_bytes(void *server);
std::size_t server1_http_server_active_connections(void *server);
// Socket-buffered plus deferred pipeline bytes, excluding the active parser.
std::size_t server1_http_server_input_backlog_bytes(void *server);

}
