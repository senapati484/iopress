/**
 * iopress Server Interface
 *
 * Platform-agnostic C interface that all 4 backend implementations
 * (io_uring, kqueue, IOCP, libuv) must conform to.
 *
 * @file server.h
 * @version 1.0.0
 */

#ifndef EXPRESS_PRO_SERVER_H
#define EXPRESS_PRO_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Platform-specific includes for ssize_t */
#ifdef _WIN32
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#else
#include <sys/types.h>
#include <unistd.h>
#endif

/* ============================================================================
 * Default Configuration Constants
 * ============================================================================
 */

/** Default initial buffer size for connections (16KB for better performance) */
#define DEFAULT_INITIAL_BUFFER_SIZE 16384

/** Default maximum body size (1MB) */
#define DEFAULT_MAX_BODY_SIZE 1048576

/** Maximum HTTP header size (8KB) */
#define MAX_HEADER_SIZE 8192

/** Maximum number of concurrent connections */
#define MAX_CONNECTIONS 65535

/** HTTP parsing status codes */
#define PARSE_STATUS_DONE 0
#define PARSE_STATUS_NEED_MORE 1
#define PARSE_STATUS_ERROR 2

/* ============================================================================
 * Type Definitions
 * ============================================================================
 */

/**
 * Server configuration options.
 *
 * All size values are in bytes.
 * Timeout values are in milliseconds (0 = disabled).
 */
typedef struct {
  /** Initial buffer size per connection */
  size_t initial_buffer_size;

  /** Maximum request body size (0 = unlimited) */
  size_t max_body_size;

  /** Enable streaming mode for large bodies */
  bool streaming_enabled;

  /** Keep-alive timeout in milliseconds */
  uint32_t keep_alive_timeout;

  /** Request timeout in milliseconds */
  uint32_t request_timeout;

  /** Port number to listen on */
  uint16_t port;

  /** Bind address (NULL or empty for all interfaces) */
  const char* bind_address;

  /** Backlog size for listen() */
  int backlog;
} server_config_t;

/**
 * Connection state structure.
 *
 * Represents a single client connection and its associated
 * request/response buffering state.
 */
typedef struct {
  /** Connection file descriptor / handle */
  int fd;

  /** Read/write buffer */
  uint8_t* buffer;

  /** Buffer capacity (allocated size) */
  size_t buffer_cap;

  /** Current buffer length (data available) */
  size_t buffer_len;

  /** Read position in buffer */
  size_t buffer_pos;

  /** HTTP body remaining to read (0 = complete, SIZE_MAX = chunked/unknown) */
  size_t body_remaining;

  /** Streaming mode enabled for this connection */
  bool streaming;

  /** Connection is keep-alive */
  bool keep_alive;

  /** Request parsing complete */
  bool request_complete;

  /** Response headers sent */
  bool headers_sent;

  /** Connection has been closed */
  bool closed;

  /** Uses pre-allocated pool buffer */
  bool uses_pool_buffer;

  /** User data pointer (for JS request reference) */
  void* user_data;

  /** Platform-specific opaque handle */
  void* platform_ctx;

  /** Offset in buffer where body starts (set by parser) */
  size_t body_start;

  /** Assembled chunked body (malloc'd, freed after request) */
  uint8_t* assembled_body;
  size_t assembled_body_len;
} connection_t;

/**
 * HTTP request parsing result.
 *
 * Returned by parser when examining buffered request data.
 */
typedef struct {
  /** Parse status: DONE, NEED_MORE, or ERROR */
  int status;

  /** Number of bytes consumed from buffer */
  size_t bytes_consumed;

  /** HTTP method (GET, POST, etc.) - points into buffer */
  const char* method;

  /** Method length */
  size_t method_len;

  /** Request path - points into buffer */
  const char* path;

  /** Path length */
  size_t path_len;

  /** Query string start (NULL if no query) */
  const char* query;

  /** Query string length */
  size_t query_len;

  /** Headers section complete */
  bool headers_complete;

  /** Body present in request */
  bool body_present;

  /** Content-Length value (0 if chunked or no body) */
  size_t body_length;

  /** Offset in buffer where body starts */
  size_t body_start;

  /** Major HTTP version */
  uint8_t http_major;

  /** Minor HTTP version */
  uint8_t http_minor;

  /** Parsing error code (if status == ERROR) */
  int error_code;
} parse_result_t;

/**
 * HTTP response structure for sending data.
 */
typedef struct {
  /** HTTP status code */
  uint16_t status_code;

  /** Status message (NULL for default) */
  const char* status_message;

  /** Response headers as null-terminated string array (name, value, name,
   * value, NULL) */
  const char** headers;

  /** Number of header pairs */
  size_t header_count;

  /** Response body data */
  const uint8_t* body;

  /** Body length */
  size_t body_len;

  /** Last chunk (for streaming responses) */
  bool is_last;
} response_t;

/**
 * Server handle returned by initialization.
 */
typedef struct server_handle_s* server_handle_t;

/**
 * Callback function type for incoming requests.
 *
 * Called by backend when a complete HTTP request has been received.
 *
 * @param conn Connection handle
 * @param result Parse result containing request metadata
 * @param user_data Opaque pointer passed to server_init
 * @return 0 on success, non-zero to close connection
 */
typedef int (*request_callback_t)(connection_t* conn,
                                  const parse_result_t* result,
                                  void* user_data);

/**
 * Callback function type for connection events.
 *
 * Called by backend on connection open/close/error.
 *
 * @param conn Connection handle
 * @param event Event type: 0=open, 1=close, 2=error
 * @param user_data Opaque pointer passed to server_init
 */
typedef void (*connection_callback_t)(connection_t* conn, int event,
                                      void* user_data);

/* ============================================================================
 * Backend Interface Functions (All 4 platforms must implement)
 * ============================================================================
 */

/**
 * Initialize the server with the given configuration.
 *
 * Creates server socket, sets up event loop infrastructure, and prepares
 * for accepting connections. Does not start listening yet.
 *
 * @param config Server configuration
 * @param on_request Callback for incoming requests
 * @param on_connection Callback for connection lifecycle events (may be NULL)
 * @param user_data Opaque pointer passed to callbacks
 * @return Server handle or NULL on error
 */
server_handle_t server_init(const server_config_t* config,
                            request_callback_t on_request,
                            connection_callback_t on_connection,
                            void* user_data);

/**
 * Start the server event loop.
 *
 * Begins accepting connections and processing requests. This function
 * blocks until server_stop() is called (in synchronous mode) or
 * returns immediately (in async mode).
 *
 * @param server Server handle from server_init
 * @return 0 on success, non-zero on error
 */
int server_start(server_handle_t server);

/**
 * Stop the server gracefully.
 *
 * Signals the event loop to stop, waits for pending operations,
 * closes all connections, and releases resources.
 *
 * @param server Server handle
 * @param timeout_ms Timeout for graceful shutdown (0 = wait forever)
 * @return 0 on success, non-zero on timeout or error
 */
int server_stop(server_handle_t server, uint32_t timeout_ms);

/**
 * Send a response on the given connection.
 *
 * Queues response data for transmission. May be called multiple
 * times for streaming responses (set response->is_last on final call).
 *
 * @param server Server handle
 * @param conn Connection handle
 * @param response Response data to send
 * @return 0 on success, non-zero if connection closed or backpressure
 */
int server_send_response(server_handle_t server, connection_t* conn,
                         const response_t* response);

/**
 * Get connection by file descriptor.
 *
 * Looks up the connection structure for a given fd from the server's
 * connection pool. Returns NULL if fd is invalid or connection closed.
 *
 * @param server Server handle
 * @param fd File descriptor to look up
 * @return Connection pointer or NULL if not found/closed
 */
connection_t* server_get_connection_by_fd(server_handle_t server, int fd);

/**
 * Read more body data from a streaming request.
 *
 * Called when streaming is enabled and more body data is needed.
 * The backend should read from the connection and append to conn->buffer.
 *
 * @param server Server handle
 * @param conn Connection handle
 * @param max_bytes Maximum bytes to read (0 = as much as available)
 * @return Number of bytes read, 0 if complete, -1 on error
 */
ssize_t server_read_body(server_handle_t server, connection_t* conn,
                         size_t max_bytes);

/* ============================================================================
 * Utility Functions (May be implemented in common code)
 * ============================================================================
 */

/**
 * Parse an HTTP request from buffer.
 *
 * Common parser implementation used by all backends.
 *
 * @param buffer Raw request data
 * @param len Buffer length
 * @param result Output parse result
 * @return Parse status code
 */
int http_parse_request(const uint8_t* buffer, size_t len,
                       parse_result_t* result);

/**
 * Append new body data and check if complete.
 *
 * This function is called when more data arrives for a request
 * that returned PARSE_STATUS_NEED_MORE. It updates the connection
 * state without re-parsing headers.
 *
 * @param conn Connection state (updated in place)
 * @param new_data Pointer to new data received
 * @param new_len Length of new data
 * @param result Updated parse result
 * @return Parse status code
 */
int parse_append_body(connection_t* conn, const uint8_t* new_data,
                      size_t new_len, parse_result_t* result);

/**
 * Format HTTP response status line and headers.
 *
 * Common formatter for response headers.
 *
 * @param response Response structure
 * * @param out_buffer Output buffer
 * @param out_len Output buffer size, updated with bytes written
 * @return 0 on success, -1 if buffer too small
 */
int http_format_headers(const response_t* response, uint8_t* out_buffer,
                        size_t* out_len);

/**
 * Get the platform name string.
 *
 * @return Static string: "io_uring", "kqueue", "iocp", or "libuv"
 */
const char* server_get_platform(void);

/**
 * Get the backend version string.
 *
 * @return Static version string (e.g., "1.0.0")
 */
const char* server_get_version(void);

#ifdef __cplusplus
}
#endif

#endif /* EXPRESS_PRO_SERVER_H */
