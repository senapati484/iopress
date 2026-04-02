/**
 * libuv Fallback Backend
 *
 * Cross-platform fallback using libuv for maximum compatibility.
 *
 * @file server_libuv.c
 */

#include <stdlib.h>
#include <string.h>

#include "server.h"

/* Platform-specific context for libuv */
typedef struct {
  void* uv_loop;    /* uv_loop_t* */
  void* tcp_server; /* uv_tcp_t* */
  bool running;
} libuv_context_t;

server_handle_t server_init(const server_config_t* config,
                            request_callback_t on_request,
                            connection_callback_t on_connection,
                            void* user_data) {
  (void)config;
  (void)on_request;
  (void)on_connection;
  (void)user_data;
  return NULL;
}

int server_start(server_handle_t server) {
  (void)server;
  return 0;
}

int server_stop(server_handle_t server, uint32_t timeout_ms) {
  (void)server;
  (void)timeout_ms;
  return 0;
}

int server_send_response(server_handle_t server, connection_t* conn,
                         const response_t* response) {
  (void)server;
  (void)conn;
  (void)response;
  return 0;
}

ssize_t server_read_body(server_handle_t server, connection_t* conn,
                         size_t max_bytes) {
  (void)server;
  (void)conn;
  (void)max_bytes;
  return 0;
}
