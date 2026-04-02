/**
 * Error Handling Implementation Patterns
 *
 * Code snippets demonstrating error handling strategies from
 * BINDING_CONTRACT.md. These are reference implementations for the 4 platform
 * backends.
 *
 * @file error_handling.c
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "server.h"

/* N-API types for reference implementation */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct napi_env__* napi_env;
typedef struct napi_value__* napi_value;
typedef struct napi_threadsafe_function__* napi_threadsafe_function;
typedef int napi_status;
#define napi_ok 0
#define napi_pending_exception 1
#define napi_tsfn_nonblocking 0
#define NAPI_AUTO_LENGTH ((size_t)-1)

/* Forward declarations for functions used before their definition */
void log_debug(const char* fmt, ...);
void log_warn(const char* fmt, ...);
void log_error(const char* fmt, ...);
server_config_t* get_server_config(connection_t* conn);
napi_threadsafe_function get_error_tsfn(void);
void queue_error_to_js(connection_t* conn, const char* code, const char* msg,
                       int status);
/* Forward declaration - actual implementation is later in file */
void notify_js_error_async(connection_t* conn, const char* code,
                           const char* message);
int on_request(connection_t* c, const parse_result_t* r, void* u);
void on_connection(connection_t* c, int e, void* u);
typedef struct napi_callback_info__* napi_callback_info;
napi_status napi_throw_error(napi_env env, const char* code, const char* msg);
napi_status napi_create_external(napi_env env, void* data,
                                 void (*finalize_cb)(napi_env env,
                                                     void* finalize_data,
                                                     void* finalize_hint),
                                 void* finalize_hint, napi_value* result);

napi_status napi_call_function(napi_env env, napi_value recv, napi_value func,
                               size_t argc, napi_value* argv,
                               napi_value* result);
napi_status napi_get_and_clear_last_exception(napi_env env, napi_value* result);
napi_status napi_get_named_property(napi_env env, napi_value object,
                                    const char* name, napi_value* result);
napi_status napi_get_value_string_utf8(napi_env env, napi_value value,
                                       char* buf, size_t bufsize, size_t* len);
napi_status napi_create_string_utf8(napi_env env, const char* str, size_t len,
                                    napi_value* result);
napi_status napi_create_object(napi_env env, napi_value* result);
napi_status napi_create_int32(napi_env env, int32_t value, napi_value* result);
napi_status napi_set_named_property(napi_env env, napi_value object,
                                    const char* name, napi_value value);
napi_status napi_call_threadsafe_function(napi_threadsafe_function func,
                                          void* data, int is_blocking);
napi_status napi_create_error(napi_env env, napi_value code, napi_value msg,
                              napi_value* result);
napi_status napi_throw(napi_env env, napi_value error);

#ifdef __cplusplus
}
#endif

/* ============================================================================
 * Section 1: Fatal Error Detection and Mapping
 * ============================================================================
 */

/**
 * Map system errors to Express-Pro error codes.
 *
 * @param sys_errno The errno value from system call
 * @return Machine-readable error code string
 */
const char* map_errno_to_code(int sys_errno) {
  switch (sys_errno) {
    case ENOMEM:
      return "ENOMEM";
    case EACCES:
      return "EACCES";
    case EADDRINUSE:
      return "EADDRINUSE";
    case EADDRNOTAVAIL:
      return "EADDRNOTAVAIL";
    case EMFILE:
      return "EMFILE";
    case ENFILE:
      return "ENFILE";
    case ECONNRESET:
      return "ECONNRESET";
    case EPIPE:
      return "EPIPE";
    case ETIMEDOUT:
      return "ETIMEDOUT";
    default:
      return "EUNKNOWN";
  }
}

/**
 * Check if an error is fatal (should throw synchronously from Listen()).
 *
 * Fatal errors prevent the server from starting and must be handled
 * before the event loop begins.
 *
 * @param code The error code
 * @return true if error is fatal
 */
bool is_fatal_error(const char* code) {
  return (strcmp(code, "ENOMEM") == 0 || strcmp(code, "EMFILE") == 0 ||
          strcmp(code, "ENFILE") == 0 || strcmp(code, "EADDRINUSE") == 0 ||
          strcmp(code, "EADDRNOTAVAIL") == 0 || strcmp(code, "EACCES") == 0 ||
          strcmp(code, "ETHREAD") == 0 || strcmp(code, "EPLATFORM") == 0);
}

/**
 * Create a formatted error message for system errors.
 *
 * @param code Error code
 * @param syscall System call that failed
 * @param out_buffer Output buffer
 * @param buffer_len Buffer size
 */
void format_error_message(const char* code, const char* syscall,
                          char* out_buffer, size_t buffer_len) {
  if (syscall) {
    snprintf(out_buffer, buffer_len, "%s: %s syscall failed: %s", code, syscall,
             strerror(errno));
  } else {
    snprintf(out_buffer, buffer_len, "%s: %s", code, strerror(errno));
  }
}

/* ============================================================================
 * Section 2: Per-Request Error Handling
 * ============================================================================
 */

typedef struct {
  const char* code;
  int http_status;
  const char* message;
  bool auto_respond;     /* Send HTTP response from C layer */
  bool close_connection; /* Close connection after error */
} error_mapping_t;

/**
 * Error mapping table: C errors to HTTP responses.
 *
 * This table defines how different errors are handled:
 * - auto_respond=true: Generate HTTP response in C, may not reach JS
 * - auto_respond=false: Pass to JS via next(err), let JS handle
 */
static const error_mapping_t error_mappings[] = {
    /* Parse errors - pass to JS */
    {"EPARSE", 400, "Bad Request", false, true},
    {"EHTTPVERSION", 505, "HTTP Version Not Supported", false, true},
    {"EMETHOD", 405, "Method Not Allowed", false, true},
    {"EURI", 400, "Invalid URI", false, true},
    {"EINVALIDCHUNK", 400, "Bad Request", false, true},
    {"EINVALIDLENGTH", 400, "Bad Request", false, true},
    {"EBADCHAR", 400, "Bad Request", false, true},

    /* Protocol errors - auto-respond from C */
    {"EREQUESTTIMEOUT", 408, "Request Timeout", true, true},
    {"EHEADER", 431, "Request Header Fields Too Large", true, true},
    {"ETOOBIG", 413, "Payload Too Large", true, true},

    /* Connection errors - silent handling */
    {"ECONNRESET", 0, "Connection reset by peer", false, true},
    {"EPIPE", 0, "Broken pipe", false, true},
    {"ETIMEDOUT", 504, "Gateway Timeout", true, true},

    /* Internal errors */
    {"EDOUBLERESPONSE", 0, "Response already sent", false, false},
    {"EBACKPRESSURE", 0, "Write buffer full", false, false},

    {NULL, 0, NULL, false, false}};

/**
 * Look up error mapping by code.
 *
 * @param code Error code string
 * @return Pointer to error_mapping_t or NULL if not found
 */
const error_mapping_t* get_error_mapping(const char* code) {
  for (size_t i = 0; error_mappings[i].code != NULL; i++) {
    if (strcmp(error_mappings[i].code, code) == 0) {
      return &error_mappings[i];
    }
  }
  return NULL;
}

/**
 * Send automatic HTTP error response from C layer.
 *
 * Used for errors that should be handled entirely in C (413, 431, 408, etc.)
 * to prevent memory exhaustion and DoS attacks.
 *
 * @param conn Connection handle
 * @param http_status HTTP status code
 * @param message Status message
 * @return 0 on success, -1 on failure
 */
int send_auto_http_error(connection_t* conn, int http_status,
                         const char* message) {
  const char* http_version = "HTTP/1.1";
  const char* connection = conn->keep_alive ? "keep-alive" : "close";

  /* Build minimal HTTP response */
  char response[512];
  int len = snprintf(response, sizeof(response),
                     "%s %d %s\r\n"
                     "Content-Length: 0\r\n"
                     "Connection: %s\r\n"
                     "\r\n",
                     http_version, http_status, message, connection);

  if (len < 0 || len >= (int)sizeof(response)) {
    return -1; /* Buffer overflow */
  }

  /* Write response to connection */
  ssize_t written = write(conn->fd, response, len);
  if (written < 0) {
    return -1;
  }

  return 0;
}

/**
 * Handle request error based on error type.
 *
 * This function implements the decision tree from BINDING_CONTRACT.md:
 * 1. Check if error should be auto-responded
 * 2. If auto-respond: send HTTP response, optionally notify JS
 * 3. If not auto-respond: queue error to JS via threadsafe function
 *
 * @param conn Connection handle
 * @param error_code Express-Pro error code
 * @param error_message Human-readable message
 * @return true if connection should be closed
 */
bool handle_request_error(connection_t* conn, const char* error_code,
                          const char* error_message) {
  const error_mapping_t* mapping = get_error_mapping(error_code);

  if (mapping == NULL) {
    /* Unknown error - treat as 500 and close */
    send_auto_http_error(conn, 500, "Internal Server Error");
    return true;
  }

  /* Log error (at appropriate level) */
  if (mapping->http_status >= 400 && mapping->http_status < 500) {
    /* Client errors - log at debug level */
    log_debug("Request error: %s - %s (fd=%d)", error_code, error_message,
              conn->fd);
  } else {
    /* Server/connection errors - log at warning level */
    log_warn("Request error: %s - %s (fd=%d)", error_code, error_message,
             conn->fd);
  }

  /* Auto-respond from C if configured */
  if (mapping->auto_respond && mapping->http_status > 0) {
    send_auto_http_error(conn, mapping->http_status, mapping->message);

    /* Optionally notify JS of auto-responded error (fire-and-forget) */
    if (strcmp(error_code, "ETOOBIG") == 0 ||
        strcmp(error_code, "EHEADER") == 0) {
      /* These errors might be logged/analyzed by JS */
      notify_js_error_async(conn, error_code, error_message);
    }
  } else if (!mapping->auto_respond) {
    /* Pass error to JS middleware chain via next(err) */
    queue_error_to_js(conn, error_code, error_message, mapping->http_status);
  }

  return mapping->close_connection;
}

/* ============================================================================
 * Section 3: max_body_size Enforcement (413 Auto-Response)
 * ============================================================================
 */

/**
 * Check if adding more body data would exceed max_body_size.
 *
 * This function is called incrementally as body data arrives.
 * Returns immediately with auto-response if limit exceeded.
 *
 * @param conn Connection handle
 * @param new_data_len Length of new data being added
 * @return 0 if OK, -1 if limit exceeded (413 sent, connection closing)
 */
int check_body_size_limit(connection_t* conn, size_t new_data_len) {
  server_config_t* config = get_server_config(conn);

  /* 0 means unlimited */
  if (config->max_body_size == 0) {
    return 0;
  }

  /* Calculate what body size would be after adding new data */
  /* Note: In real implementation, body_start indicates where body begins in
   * buffer */
  size_t current_body =
      conn->buffer_len; /* Simplified - actual would subtract header size */
  size_t projected_size = current_body + new_data_len;

  if (projected_size > config->max_body_size) {
    /* Body would exceed limit - send 413 and abort */
    log_warn("Body size %zu exceeds limit %zu (fd=%d)", projected_size,
             config->max_body_size, conn->fd);

    send_auto_http_error(conn, 413, "Payload Too Large");

    /* Mark connection for closing */
    conn->closed = true;

    /* Notify JS (fire-and-forget) */
    notify_js_error_async(conn, "ETOOBIG",
                          "Request body exceeds max_body_size");

    return -1; /* Signal caller to stop reading */
  }

  return 0;
}

/**
 * Read body data with size enforcement.
 *
 * Wrapper around read() that enforces max_body_size and sends
 * 413 response if exceeded.
 *
 * @param conn Connection handle
 * @param buf Buffer to read into
 * @param buf_size Buffer size
 * @return Bytes read, 0 if EOF, -1 if error (413 sent if limit exceeded)
 */
ssize_t read_body_chunk(connection_t* conn, uint8_t* buf, size_t buf_size) {
  /* Check if we can read this much without exceeding limit */
  if (check_body_size_limit(conn, buf_size) != 0) {
    return -1; /* 413 already sent */
  }

  /* Actually read the data */
  ssize_t bytes_read = read(conn->fd, buf, buf_size);

  if (bytes_read < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return 0; /* Would block - not an error */
    }

    /* Connection error */
    const char* code = map_errno_to_code(errno);
    if (strcmp(code, "ECONNRESET") == 0 || strcmp(code, "EPIPE") == 0) {
      /* Silent close for common connection errors */
      conn->closed = true;
      return -1;
    }

    return -1;
  }

  if (bytes_read == 0) {
    /* EOF - client closed connection */
    conn->closed = true;
    return 0;
  }

  return bytes_read;
}

/* ============================================================================
 * Section 4: JS Handler Exception Safety (N-API Wrapper)
 * ============================================================================
 */

/**
 * Call JavaScript handler with exception catching.
 *
 * This function wraps napi_call_function with proper exception handling
 * to ensure that any JS exception is caught and converted to a 500 response,
 * never crashing the Node process.
 *
 * @param env N-API environment
 * @param js_callback The JS function to call
 * @param argc Argument count
 * @param argv Arguments
 * @param conn Connection handle (for sending 500 if needed)
 * @return 0 on success, -1 if exception was thrown (500 sent)
 */
int call_js_handler_safe(napi_env env, napi_value js_callback, size_t argc,
                         napi_value* argv, connection_t* conn) {
  napi_value result;
  napi_status status;

  /* Call the JavaScript function */
  status = napi_call_function(env, NULL, js_callback, argc, argv, &result);

  if (status == napi_pending_exception) {
    /* EXCEPTION CAUGHT - get and clear it */
    napi_value exception;
    napi_get_and_clear_last_exception(env, &exception);

    /* Log the exception details */
    napi_value message_val;
    char message[256];
    if (napi_get_named_property(env, exception, "message", &message_val) ==
        napi_ok) {
      size_t msg_len;
      napi_get_value_string_utf8(env, message_val, message, sizeof(message),
                                 &msg_len);
      log_error("JS handler threw exception (fd=%d): %s", conn->fd, message);
    } else {
      log_error("JS handler threw exception (fd=%d): [unknown]", conn->fd);
    }

    /* IMPORTANT: Send 500 if response not already sent */
    if (!conn->headers_sent) {
      send_auto_http_error(conn, 500, "Internal Server Error");
      conn->headers_sent = true;
    }

    /* Close connection on exception (don't risk corrupted state) */
    conn->closed = true;

    return -1; /* Signal error to caller */
  }

  if (status != napi_ok) {
    /* Other N-API error */
    log_error("N-API call failed with status %d", (int)status);

    if (!conn->headers_sent) {
      send_auto_http_error(conn, 500, "Internal Server Error");
      conn->headers_sent = true;
    }

    conn->closed = true;
    return -1;
  }

  /* Success - handler completed normally */
  return 0;
}

/* ============================================================================
 * Section 5: Async Error Notification to JS
 * ============================================================================
 */

/**
 * Error info structure for async notification.
 */
typedef struct {
  char code[32];
  char message[256];
  int http_status;
  uint64_t request_id;
} async_error_t;

/**
 * Threadsafe function callback for delivering errors to JS.
 *
 * This is called from the main thread when an async error is ready
 * to be delivered to JavaScript.
 */
void on_error_ready(napi_env env, napi_value js_callback, void* context,
                    void* data) {
  async_error_t* err = (async_error_t*)data;

  /* Create error object */
  napi_value error_obj;
  napi_create_object(env, &error_obj);

  /* Set properties */
  napi_value code_val, msg_val, status_val;
  napi_create_string_utf8(env, err->code, NAPI_AUTO_LENGTH, &code_val);
  napi_create_string_utf8(env, err->message, NAPI_AUTO_LENGTH, &msg_val);
  napi_create_int32(env, err->http_status, &status_val);

  napi_set_named_property(env, error_obj, "code", code_val);
  napi_set_named_property(env, error_obj, "message", msg_val);
  napi_set_named_property(env, error_obj, "statusCode", status_val);

  /* Call JS error handler */
  napi_value argv[1] = {error_obj};
  napi_value result;

  /* Don't check result - fire and forget */
  napi_call_function(env, NULL, js_callback, 1, argv, &result);

  /* Free error data */
  free(err);
}

/**
 * Queue an error to be delivered to JS asynchronously.
 *
 * Used for non-fatal errors that should be logged/analyzed by JS
 * but don't require immediate handling (like 413 auto-responses).
 *
 * @param conn Connection handle
 * @param code Error code
 * @param message Error message
 */
void notify_js_error_async(connection_t* conn, const char* code,
                           const char* message) {
  async_error_t* err = malloc(sizeof(async_error_t));
  if (err == NULL) return; /* Silent fail - can't allocate */

  strncpy(err->code, code, sizeof(err->code) - 1);
  err->code[sizeof(err->code) - 1] = '\0';

  strncpy(err->message, message, sizeof(err->message) - 1);
  err->message[sizeof(err->message) - 1] = '\0';

  err->http_status = 0; /* Already responded */
  err->request_id = (uint64_t)(uintptr_t)conn->user_data;

  /* Queue to threadsafe function (non-blocking) */
  napi_threadsafe_function tsfn = get_error_tsfn();
  napi_call_threadsafe_function(tsfn, err, napi_tsfn_nonblocking);
}

/* ============================================================================
 * Section 6: Startup Error Handling Example
 * ============================================================================
 */

/**
 * Example: Listen() with comprehensive error handling.
 *
 * This demonstrates how to handle all startup errors and ensure
 * no crash occurs during server initialization.
 */
napi_value example_listen_implementation(napi_env env,
                                         napi_callback_info info) {
  server_config_t config = {0};

  /* Parse arguments from JS... */
  /* (omitted for brevity) */

  /* Initialize server with error handling */
  errno = 0;
  server_handle_t handle =
      server_init(&config, on_request, on_connection, NULL);

  if (handle == NULL) {
    const char* code = map_errno_to_code(errno);
    char message[256];

    if (is_fatal_error(code)) {
      /* FATAL: OOM, thread creation failure, etc. */
      format_error_message(code, "server_init", message, sizeof(message));

      napi_throw_error(env, code, message);
      return NULL;
    }

    /* Non-fatal init error */
    format_error_message(code, "server_init", message, sizeof(message));
    napi_throw_error(env, code, message);
    return NULL;
  }

  /* Start server - this is where EADDRINUSE, EACCES happen */
  errno = 0;
  if (server_start(handle) != 0) {
    const char* code = map_errno_to_code(errno);
    char message[256];

    /* All startup errors from server_start are fatal */
    format_error_message(code, "bind", message, sizeof(message));

    /* Cleanup before throwing */
    server_stop(handle, 0);

    napi_value error, msg_val;
    napi_create_string_utf8(env, message, NAPI_AUTO_LENGTH, &msg_val);
    napi_create_error(env, NULL, msg_val, &error);

    /* Add code and syscall properties */
    napi_value code_val, syscall_val;
    napi_create_string_utf8(env, code, NAPI_AUTO_LENGTH, &code_val);
    napi_create_string_utf8(env, "bind", NAPI_AUTO_LENGTH, &syscall_val);
    napi_set_named_property(env, error, "code", code_val);
    napi_set_named_property(env, error, "syscall", syscall_val);

    napi_throw(env, error);
    return NULL;
  }

  /* Success - return server handle */
  napi_value result;
  napi_create_external(env, handle, NULL, NULL, &result);
  return result;
}

/* ============================================================================
 * Stub implementations for compilation
 * ============================================================================
 */

/* Extend connection_t with body_start for this example */
#define conn_body_start(conn)                                             \
  ((conn)->body_remaining) /* Placeholder - real implementation would use \
                              proper field */

void log_debug(const char* fmt, ...) { (void)fmt; }
void log_warn(const char* fmt, ...) { (void)fmt; }
void log_error(const char* fmt, ...) { (void)fmt; }
server_config_t* get_server_config(connection_t* conn) {
  (void)conn;
  return NULL;
}
napi_threadsafe_function get_error_tsfn(void) { return NULL; }
napi_status napi_throw_error(napi_env env, const char* code, const char* msg) {
  (void)env;
  (void)code;
  (void)msg;
  return napi_ok;
}
napi_status napi_create_external(napi_env env, void* data,
                                 void (*finalize_cb)(napi_env env,
                                                     void* finalize_data,
                                                     void* finalize_hint),
                                 void* finalize_hint, napi_value* result) {
  (void)env;
  (void)data;
  (void)finalize_cb;
  (void)finalize_hint;
  (void)result;
  return napi_ok;
}
void queue_error_to_js(connection_t* conn, const char* code, const char* msg,
                       int status) {
  (void)conn;
  (void)code;
  (void)msg;
  (void)status;
}
ssize_t write(int fd, const void* buf, size_t count) {
  (void)fd;
  (void)buf;
  (void)count;
  return -1;
}
ssize_t read(int fd, void* buf, size_t count) {
  (void)fd;
  (void)buf;
  (void)count;
  return -1;
}
int on_request(connection_t* c, const parse_result_t* r, void* u) {
  (void)c;
  (void)r;
  (void)u;
  return 0;
}
void on_connection(connection_t* c, int e, void* u) {
  (void)c;
  (void)e;
  (void)u;
}
