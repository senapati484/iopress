/**
 * iopress Server Interface
 *
 * Platform-agnostic C interface that all 4 backend implementations
 * (io_uring, kqueue, IOCP, libuv) must conform to.
 *
 * @file server.h
 * @version 1.1.0
 */

#ifndef EXPRESS_PRO_SERVER_H
#define EXPRESS_PRO_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef _MSC_VER
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#endif

#define DEFAULT_INITIAL_BUFFER_SIZE 16384
#define DEFAULT_MAX_BODY_SIZE 1048576
#define MAX_CONNECTIONS 100000

typedef struct {
  uint16_t port;
  const char* bind_address;
  size_t initial_buffer_size;
  size_t max_body_size;
  int backlog;
  bool reuse_port;
} server_config_t;

typedef struct {
  int status_code;
  const uint8_t* body;
  size_t body_len;
  const char** headers;
  /* Parallel length array, 2*header_count entries, [name_len, val_len, ...].
   * May be NULL, in which case consumers fall back to strlen(headers[i]).
   * When non-NULL, avoids 3 strlen walks per header (size probe in NAPI,
   * copy in NAPI, plus the strlen in server_kevent). */
  const size_t* header_lens;
  size_t header_count;
  bool is_last;
} response_t;

/* Forward declaration for parsing result */
struct parse_result_s;
typedef struct parse_result_s parse_result_t;

typedef struct connection_s {
  int fd;
  bool closed;
  uint8_t* buffer;
  size_t buffer_cap;
  size_t buffer_len;
  size_t buffer_pos;
  size_t body_remaining;
  bool streaming;
  bool keep_alive;
  bool request_complete;
  bool headers_sent;
  void* user_data;
  void* platform_ctx;

  /** Offset in buffer where body starts (set by parser) */
  size_t body_start;

  /** Assembled chunked body (malloc'd, freed after request) */
  uint8_t* assembled_body;
  size_t assembled_body_len;

  /** Platform-specific SQE data to avoid heap allocations in hot path */
  void* platform_sqe_data_recv;
  void* platform_sqe_data_send;

  /** Hardening: True if JS layer is currently processing this connection */
  bool processing;

  /** Hardening: Last activity timestamp (seconds) for idle timeout */
  uint64_t last_active;

  /** Output buffer for partial writes */
  uint8_t* out_buffer;
  size_t out_buffer_cap;
  size_t out_buffer_len;
  size_t out_buffer_pos;

  /** Pool management */
  bool uses_pool_buffer;

  /** Guards against double RECV re-arm (set by server_resume_read,
   *  cleared by OP_RECV handler on next completion).
   *  Currently only used by the IOCP backend; ignored by kqueue/io_uring. */
  bool recv_armed;
} connection_t;

typedef int (*request_callback_t)(connection_t* conn,
                                  const parse_result_t* result,
                                  void* user_data);
typedef void (*connection_callback_t)(connection_t* conn, int status,
                                      void* user_data);

typedef struct server_handle_s* server_handle_t;

server_handle_t server_init(const server_config_t* config,
                            request_callback_t on_request,
                            connection_callback_t on_connection,
                            void* user_data);

int server_start(server_handle_t server);

int server_stop(server_handle_t server, uint32_t timeout_ms);

int server_send_response(server_handle_t server, connection_t* conn,
                         const response_t* response);

int server_write(server_handle_t server, connection_t* conn, const void* data,
                 size_t len);

/**
 * Pause read events for a connection.
 * Used when JS layer is processing a request on a persistent connection.
 */
int server_pause_read(server_handle_t server, connection_t* conn);

/**
 * Resume read events for a connection.
 * Used when JS layer has finished processing.
 */
int server_resume_read(server_handle_t server, connection_t* conn);

/**
 * Get connection by file descriptor.
 *
 * Looks up the connection structure for a given fd from the server's
 * connection pool. Returns NULL if fd is invalid or connection closed.
 */
connection_t* server_get_connection_by_fd(server_handle_t server, int fd);

/**
 * Get the platform backend string.
 *
 * @return Static backend string (e.g., \"kqueue\", \"io_uring\", \"iocp\")
 */
const char* server_get_platform(void);

/**
 * Get the backend version string.
 *
 * @return Static version string (e.g., \"1.1.0\")
 */
const char* server_get_version(void);

#ifdef __cplusplus
}
#endif

#endif /* EXPRESS_PRO_SERVER_H */
