/**
 * iopress N-API Binding
 *
 * Bridge between JavaScript and the native HTTP server.
 * Handles thread-safe communication using napi_threadsafe_function.
 *
 * @file binding.c
 * @version 1.0.0
 */

#include <node_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fast_router.h"
#include "parser.h"
#include "server.h"

/* ============================================================================
 * Thread-Safe Function Context
 * ============================================================================
 */

typedef struct {
  napi_threadsafe_function tsfn;
  server_handle_t server;
  napi_env env;

  /* Current request connection (set before TSFN call) */
  connection_t* current_conn;

  /* Route handlers stored as refs */
  void* route_handlers[256];
  size_t route_count;
} binding_context_t;

static binding_context_t g_context = {0};

/* ============================================================================
 * Request Data for Thread-Safe Passing
 * ============================================================================
 */

typedef struct {
  int fd;
  char method[16];
  char path[256];
  char query[256];
  uint8_t* body;
  size_t body_len;
  int handler_index;
  /* Headers as simple key-value pairs (max 32 headers, 256 chars each) */
  char header_keys[32][64];
  char header_values[32][256];
  int header_count;
  /* Connection pointer - passed through TSFN to avoid race condition */
  connection_t* conn;
} request_data_t;

/* ============================================================================
 * Fast Path Constants
 * ============================================================================
 */

#define FAST_PATH_ENABLED 1
#define MAX_CACHED_PATHS 32
#define MAX_CACHE_RESPONSE_SIZE 1024

/* ============================================================================
 * Response Cache for Fast Path
 * ============================================================================
 */

#define MAX_CACHED_ROUTES 64

typedef struct {
  char method[8];
  char path[128];
  uint8_t response[MAX_CACHE_RESPONSE_SIZE];
  size_t response_len;
  uint16_t status_code;
  int hit_count;
  int active;
} cached_route_t;

static cached_route_t route_cache[MAX_CACHED_ROUTES];
static int cache_initialized = 0;
static int cache_hits = 0;
static int cache_misses = 0;

static void init_route_cache(void) {
  if (cache_initialized) return;

  memset(route_cache, 0, sizeof(route_cache));
  cache_initialized = 1;
  cache_hits = 0;
  cache_misses = 0;
}

static cached_route_t* find_cached_route(const char* method, const char* path) {
  if (!cache_initialized) init_route_cache();

  for (int i = 0; i < MAX_CACHED_ROUTES; i++) {
    if (!route_cache[i].active) continue;

    if (strcmp(route_cache[i].method, method) == 0 &&
        strcmp(route_cache[i].path, path) == 0) {
      route_cache[i].hit_count++;
      cache_hits++;
      return &route_cache[i];
    }
  }
  cache_misses++;
  return NULL;
}

static void add_cached_route(const char* method, const char* path,
                             const uint8_t* response, size_t response_len,
                             uint16_t status_code) {
  if (!cache_initialized) init_route_cache();

  /* Find empty slot */
  for (int i = 0; i < MAX_CACHED_ROUTES; i++) {
    if (!route_cache[i].active) {
      strncpy(route_cache[i].method, method, 7);
      strncpy(route_cache[i].path, path, 127);
      if (response_len < MAX_CACHE_RESPONSE_SIZE) {
        memcpy(route_cache[i].response, response, response_len);
      }
      route_cache[i].response_len = response_len;
      route_cache[i].status_code = status_code;
      route_cache[i].active = 1;
      route_cache[i].hit_count = 0;
      return;
    }
  }

  /* Cache full - replace least used (simple LRU) */
  int min_hits = route_cache[0].hit_count;
  int min_idx = 0;
  for (int i = 1; i < MAX_CACHED_ROUTES; i++) {
    if (route_cache[i].hit_count < min_hits) {
      min_hits = route_cache[i].hit_count;
      min_idx = i;
    }
  }

  strncpy(route_cache[min_idx].method, method, 7);
  strncpy(route_cache[min_idx].path, path, 127);
  if (response_len < MAX_CACHE_RESPONSE_SIZE) {
    memcpy(route_cache[min_idx].response, response, response_len);
  }
  route_cache[min_idx].response_len = response_len;
  route_cache[min_idx].status_code = status_code;
  route_cache[min_idx].hit_count = 0;
}

static void get_cache_stats(int* hits, int* misses) {
  if (hits) *hits = cache_hits;
  if (misses) *misses = cache_misses;
}

/* ============================================================================
 * HTTP Header Parsing
 * ============================================================================
 */

/**
 * Parse headers from HTTP request buffer.
 * Returns number of headers parsed.
 */
static int parse_headers(const uint8_t* buffer, size_t len, char keys[][64],
                         char values[][256], int max_count) {
  /* Find end of request line (first \r\n) */
  const uint8_t* p = buffer;
  const uint8_t* end = buffer + len;

  /* Skip request line */
  while (p < end && *p != '\r') p++;
  if (p < end && *p == '\r') p++; /* Skip \r */
  if (p < end && *p == '\n') p++; /* Skip \n */

  int count = 0;
  while (p < end && count < max_count) {
    /* Check for end of headers (empty line) */
    if (p + 1 < end && *p == '\r' && *(p + 1) == '\n') {
      break;
    }

    /* Find colon */
    const uint8_t* colon = p;
    while (colon < end && *colon != ':' && *colon != '\r') colon++;

    if (colon >= end || *colon != ':') break;

    /* Copy key (trim whitespace) */
    size_t key_len = colon - p;
    if (key_len >= 64) key_len = 63;
    memcpy(keys[count], p, key_len);
    keys[count][key_len] = '\0';

    /* Skip colon and whitespace */
    const uint8_t* val = colon + 1;
    while (val < end && (*val == ' ' || *val == '\t')) val++;

    /* Find end of value */
    const uint8_t* val_end = val;
    while (val_end < end && *val_end != '\r') val_end++;

    /* Copy value (trim trailing whitespace) */
    size_t val_len = val_end - val;
    while (val_len > 0 &&
           (*(val + val_len - 1) == ' ' || *(val + val_len - 1) == '\t')) {
      val_len--;
    }
    if (val_len >= 256) val_len = 255;
    memcpy(values[count], val, val_len);
    values[count][val_len] = '\0';

    count++;

    /* Move to next line */
    p = val_end;
    if (p < end && *p == '\r') p++;
    if (p < end && *p == '\n') p++;
  }

  return count;
}

/* ============================================================================
 * C Request Handler (Called from server thread)
 * ============================================================================
 */

static int on_request_c_handler(connection_t* conn,
                                const parse_result_t* result, void* user_data) {
  (void)user_data;

  /* Try fast router first (C-only, no JavaScript) */
  uint8_t* fast_response = NULL;
  size_t fast_response_len = 0;

  int fast_result = fast_router_try_handle_full(
      (const char*)result->method, result->method_len,
      (const char*)result->path, result->path_len, &fast_response,
      &fast_response_len);

  if (fast_result == 0 && fast_response_len > 0) {
    write(conn->fd, fast_response, fast_response_len);
    return 0;
  }

  /* Standard path: Allocate and pass to JS */
  request_data_t* req_data = calloc(1, sizeof(request_data_t));
  if (!req_data) return -1;

  req_data->fd = conn->fd;

  /* Copy method */
  size_t method_len = result->method_len < 15 ? result->method_len : 15;
  memcpy(req_data->method, result->method, method_len);
  req_data->method[method_len] = '\0';

  /* Copy path */
  size_t path_len = result->path_len < 255 ? result->path_len : 255;
  memcpy(req_data->path, result->path, path_len);
  req_data->path[path_len] = '\0';

  /* Copy query */
  if (result->query && result->query_len > 0) {
    size_t query_len = result->query_len < 255 ? result->query_len : 255;
    memcpy(req_data->query, result->query, query_len);
    req_data->query[query_len] = '\0';
  }

  /* Parse headers from request buffer */
  req_data->header_count =
      parse_headers(conn->buffer, conn->buffer_len, req_data->header_keys,
                    req_data->header_values, 32);

  /* Copy body - use assembled body for chunked encoding if available */
  if (result->body_present && result->body_length > 0) {
    req_data->body = malloc(result->body_length);
    if (req_data->body) {
      if (conn->assembled_body) {
        /* Use pre-assembled chunked body */
        memcpy(req_data->body, conn->assembled_body, result->body_length);
      } else {
        /* Normal body from buffer */
        memcpy(req_data->body, conn->buffer + result->body_start,
               result->body_length);
      }
      req_data->body_len = result->body_length;
    }
  }

  /* Store connection in request data to avoid race condition */
  req_data->conn = conn;

  /* Pause reads for this connection until JS layer calls res.send() */
  server_pause_read(g_context.server, conn);

  /* Call thread-safe function (non-blocking) */
  napi_call_threadsafe_function(g_context.tsfn, req_data,
                                napi_tsfn_nonblocking);

  return 0;
}

/* Forward declarations */
napi_value res_status_wrapper(napi_env env, napi_callback_info info);
napi_value res_send_wrapper(napi_env env, napi_callback_info info);

/* ============================================================================
 * JS Callback (Called from main thread via TSFN)
 * ============================================================================
 */

static void on_request_js_callback(napi_env env, napi_value js_callback,
                                   void* context, void* data) {
  (void)context;

  request_data_t* req_data = (request_data_t*)data;

  if (!req_data) {
    return;
  }

  /* Build request object */
  napi_value req_obj, res_obj;
  napi_create_object(env, &req_obj);
  napi_create_object(env, &res_obj);

  /* req.method */
  napi_value method_val;
  napi_create_string_utf8(env, req_data->method, NAPI_AUTO_LENGTH, &method_val);
  napi_set_named_property(env, req_obj, "method", method_val);

  /* req.path */
  napi_value path_val;
  napi_create_string_utf8(env, req_data->path, NAPI_AUTO_LENGTH, &path_val);
  napi_set_named_property(env, req_obj, "path", path_val);

  /* req.query */
  napi_value query_val;
  napi_create_string_utf8(env, req_data->query, NAPI_AUTO_LENGTH, &query_val);
  napi_set_named_property(env, req_obj, "query", query_val);

  /* req.headers */
  napi_value headers_obj;
  napi_create_object(env, &headers_obj);
  for (int i = 0; i < req_data->header_count; i++) {
    napi_value header_val;
    napi_create_string_utf8(env, req_data->header_values[i], NAPI_AUTO_LENGTH,
                            &header_val);
    napi_set_named_property(env, headers_obj, req_data->header_keys[i],
                            header_val);
  }
  napi_set_named_property(env, req_obj, "headers", headers_obj);

  /* req.body */
  if (req_data->body && req_data->body_len > 0) {
    napi_value body_val;
    napi_create_buffer_copy(env, req_data->body_len, req_data->body, NULL,
                            &body_val);
    napi_set_named_property(env, req_obj, "body", body_val);
  }

  /* req.fd */
  napi_value fd_val;
  napi_create_int32(env, req_data->fd, &fd_val);
  napi_set_named_property(env, req_obj, "fd", fd_val);

  /* Build response object with fd and conn attached */
  napi_value res_fd_val;
  napi_create_int32(env, req_data->fd, &res_fd_val);
  napi_set_named_property(env, res_obj, "fd", res_fd_val);

  /* Store connection pointer as external - avoids race condition */
  napi_value res_conn_val;
  napi_create_external(env, req_data->conn, NULL, NULL, &res_conn_val);
  napi_set_named_property(env, res_obj, "_conn", res_conn_val);

  /* Build response object with methods */
  napi_value status_fn, send_fn;

  /* res.status() */
  napi_create_function(env, "status", NAPI_AUTO_LENGTH, res_status_wrapper,
                       NULL, &status_fn);
  napi_set_named_property(env, res_obj, "status", status_fn);

  /* res.send() */
  napi_create_function(env, "send", NAPI_AUTO_LENGTH, res_send_wrapper, NULL,
                       &send_fn);
  napi_set_named_property(env, res_obj, "send", send_fn);

  /* Call JS handler */

  /* Get undefined for `this` */
  napi_value undefined_val;
  napi_get_undefined(env, &undefined_val);

  napi_value argv[2] = {req_obj, res_obj};
  napi_value result;
  napi_status call_status =
      napi_call_function(env, undefined_val, js_callback, 2, argv, &result);

  if (call_status == napi_pending_exception) {
    napi_value exception;
    napi_get_and_clear_last_exception(env, &exception);
    napi_value str;
    napi_coerce_to_string(env, exception, &str);
    char err_msg[256];
    size_t err_len;
    napi_get_value_string_utf8(env, str, err_msg, sizeof(err_msg), &err_len);
  }

  /* Cleanup */
  free(req_data->body);
  free(req_data);
}

/* Response method wrappers */
napi_value res_status_wrapper(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);

  int32_t status;
  napi_get_value_int32(env, args[0], &status);

  /* Store status in res object */
  napi_value this_val;
  napi_get_cb_info(env, info, &argc, args, &this_val, NULL);
  napi_value status_val;
  napi_create_int32(env, status, &status_val);
  napi_set_named_property(env, this_val, "_status", status_val);

  return this_val;
}

napi_value res_send_wrapper(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_value this_val;
  napi_get_cb_info(env, info, &argc, args, &this_val, NULL);

  /* Get status */
  napi_value status_val;
  int32_t status = 200;
  if (napi_get_named_property(env, this_val, "_status", &status_val) ==
      napi_ok) {
    napi_get_value_int32(env, status_val, &status);
  }

  /* Get connection from res._conn (external) */
  connection_t* conn = NULL;
  napi_value conn_val;
  if (napi_get_named_property(env, this_val, "_conn", &conn_val) == napi_ok) {
    napi_get_value_external(env, conn_val, (void**)&conn);
  }

  /* Get body */
  char* body_str = NULL;
  size_t body_len = 0;
  if (argc > 0) {
    napi_get_value_string_utf8(env, args[0], NULL, 0, &body_len);
    body_str = malloc(body_len + 1);
    napi_get_value_string_utf8(env, args[0], body_str, body_len + 1, &body_len);
  }

  /* Send response via C */
  response_t resp = {0};
  resp.status_code = status;
  resp.body = (uint8_t*)body_str;
  resp.body_len = body_len;
  resp.is_last = true;

  /* Send using connection from response object (no race condition) */
  if (g_context.server && conn) {
    server_send_response(g_context.server, conn, &resp);
    /* Resume reads now that response is sent */
    server_resume_read(g_context.server, conn);
  }

  free(body_str);

  return this_val;
}

/* ============================================================================
 * N-API Exported Functions
 * ============================================================================
 */

napi_value RegisterRoute(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value args[3];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);

  /* Get method */
  char method[16];
  size_t method_len;
  napi_get_value_string_utf8(env, args[0], method, sizeof(method), &method_len);

  /* Get path */
  char path[256];
  size_t path_len;
  napi_get_value_string_utf8(env, args[1], path, sizeof(path), &path_len);

  /* Store handler reference */
  napi_ref handler_ref;
  napi_create_reference(env, args[2], 1, &handler_ref);

  /* Store in context */
  if (g_context.route_count < 256) {
    g_context.route_handlers[g_context.route_count++] = handler_ref;
  }

  /* Add to router (if server initialized) */
  if (g_context.server) {
    /* router_add(g_context.server->router, method, path, handler_ref); */
  }

  return NULL;
}

napi_value RegisterFastRoute(napi_env env, napi_callback_info info) {
  size_t argc = 4;
  napi_value args[4];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);

  if (argc < 4) {
    napi_throw_error(env, NULL,
                     "Expected 4 args: method, path, statusCode, response");
    return NULL;
  }

  /* Get method */
  char method[8];
  size_t method_len;
  napi_get_value_string_utf8(env, args[0], method, sizeof(method), &method_len);

  /* Get path */
  char path[128];
  size_t path_len;
  napi_get_value_string_utf8(env, args[1], path, sizeof(path), &path_len);

  /* Get status code */
  int32_t status;
  napi_get_value_int32(env, args[2], &status);

  /* Get response body */
  char response[4096];
  size_t response_len;
  napi_get_value_string_utf8(env, args[3], response, sizeof(response),
                             &response_len);

  /* Register with fast router */
  int reg_result = fast_router_register(method, path, ROUTE_TYPE_STATIC_JSON,
                                        status, "application/json",
                                        (uint8_t*)response, response_len);

  fprintf(stderr, "RegisterFastRoute: %s %s -> result=%d\n", method, path,
          reg_result);

  napi_value result;
  napi_get_undefined(env, &result);
  return result;
}

napi_value Listen(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value args[3];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);

  /* Get port */
  int32_t port = 3000;
  if (argc > 0) {
    napi_get_value_int32(env, args[0], &port);
  }

  /* Parse options */
  server_config_t config = {0};
  config.port = port;
  config.initial_buffer_size = DEFAULT_INITIAL_BUFFER_SIZE;
  config.max_body_size = DEFAULT_MAX_BODY_SIZE;
  config.reuse_port = false;

  if (argc > 1) {
    napi_value options = args[1];
    napi_value val;

    if (napi_get_named_property(env, options, "initialBufferSize", &val) ==
        napi_ok) {
      int32_t v;
      napi_get_value_int32(env, val, &v);
      config.initial_buffer_size = v;
    }

    if (napi_get_named_property(env, options, "maxBodySize", &val) == napi_ok) {
      int32_t v;
      napi_get_value_int32(env, val, &v);
      config.max_body_size = v;
    }

    if (napi_get_named_property(env, options, "reusePort", &val) == napi_ok) {
      bool v;
      napi_get_value_bool(env, val, &v);
      config.reuse_port = v;
    }
  }

  /* Create thread-safe function */
  napi_value async_resource_name;
  napi_create_string_utf8(env, "iopress", NAPI_AUTO_LENGTH,
                          &async_resource_name);

  napi_threadsafe_function tsfn;
  napi_create_threadsafe_function(env, args[2] /* callback */, NULL,
                                  async_resource_name, 0, 1, NULL, NULL, NULL,
                                  on_request_js_callback, &tsfn);

  g_context.tsfn = tsfn;
  g_context.env = env;

  /* Initialize server */
  g_context.server = server_init(&config, on_request_c_handler, NULL, NULL);
  if (!g_context.server) {
    napi_throw_error(env, NULL, "Failed to initialize server");
    return NULL;
  }

  /* Start server */
  if (server_start(g_context.server) != 0) {
    napi_throw_error(env, NULL, "Failed to start server");
    return NULL;
  }

  /* Return server info */
  napi_value result;
  napi_create_object(env, &result);

  napi_value port_val;
  napi_create_int32(env, port, &port_val);
  napi_set_named_property(env, result, "port", port_val);

  return result;
}

napi_value SendResponse(napi_env env, napi_callback_info info) {
  size_t argc = 5;
  napi_value args[5];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);

  /* Get fd */
  int32_t fd;
  napi_get_value_int32(env, args[0], &fd);

  /* Get status */
  int32_t status;
  napi_get_value_int32(env, args[1], &status);

  /* Get body */
  uint8_t* body_ptr = NULL;
  size_t body_len = 0;

  if (argc > 2 && args[2] != NULL) {
    napi_valuetype body_type;
    napi_typeof(env, args[2], &body_type);

    if (body_type == napi_string) {
      napi_get_value_string_utf8(env, args[2], NULL, 0, &body_len);
      body_ptr = malloc(body_len + 1);
      if (body_ptr) {
        napi_get_value_string_utf8(env, args[2], (char*)body_ptr, body_len + 1,
                                   &body_len);
      }
    } else {
      bool is_buffer;
      napi_is_buffer(env, args[2], &is_buffer);
      if (is_buffer) {
        void* data;
        napi_get_buffer_info(env, args[2], &data, &body_len);
        body_ptr = malloc(body_len);
        if (body_ptr) {
          memcpy(body_ptr, data, body_len);
        }
      }
    }
  }

  /* Get headers (object with key-value pairs) */
  char headers_buf[4096];
  size_t headers_len = 0;
  if (argc > 3 && args[3] != NULL) {
    napi_value headers_obj = args[3];
    napi_value keys;
    napi_get_property_names(env, headers_obj, &keys);

    uint32_t key_count;
    napi_get_array_length(env, keys, &key_count);

    for (uint32_t i = 0;
         i < key_count && headers_len < sizeof(headers_buf) - 256; i++) {
      napi_value key_val;
      napi_get_element(env, keys, i, &key_val);

      char key[64], value[256];
      size_t key_len, value_len;
      napi_get_value_string_utf8(env, key_val, key, sizeof(key), &key_len);

      napi_value val;
      napi_get_named_property(env, headers_obj, key, &val);
      napi_get_value_string_utf8(env, val, value, sizeof(value), &value_len);

      int written =
          snprintf(headers_buf + headers_len, sizeof(headers_buf) - headers_len,
                   "%s: %s\r\n", key, value);
      if (written > 0) {
        headers_len += written;
      }
    }
  }

  /* Send using connection looked up by fd (avoids race condition) */
  if (g_context.server && fd >= 0) {
    /* Look up connection by fd in the server's connection pool */
    connection_t* conn = server_get_connection_by_fd(g_context.server, fd);

    if (conn) {
      /* Build HTTP response manually since server_send_response doesn't support
       * custom headers well */
      int conn_fd = conn->fd;

      char response[32768];
      int len = snprintf(
          response, sizeof(response),
          "HTTP/1.1 %d %s\r\n"
          "%s" /* Custom headers */
          "Content-Length: %zu\r\n"
          "\r\n",
          status, status < 400 ? "OK" : (status < 500 ? "Not Found" : "Error"),
          headers_len > 0 ? headers_buf : "", body_len);

      /* Send headers */
      server_write(g_context.server, conn, response, len);

      /* Send body */
      if (body_ptr && body_len > 0) {
        server_write(g_context.server, conn, body_ptr, body_len);
      }

      /* Close if not keep-alive, otherwise reset for next request */
      if (!conn->keep_alive) {
        close(conn_fd);
        conn->fd = -1;
      } else {
        /* Reset state if all data sent immediately, otherwise
         * handle_client_write will do it */
        if (conn->out_buffer_len == 0) {
          /* Reset buffer and state for next request on keep-alive connection */
          conn->buffer_len = 0;
          conn->buffer_pos = 0;
          conn->request_complete = false;
          conn->headers_sent = false;

          /* Free assembled body if any */
          if (conn->assembled_body) {
            free(conn->assembled_body);
            conn->assembled_body = NULL;
            conn->assembled_body_len = 0;
          }
        }
      }
    }
  }

  if (body_ptr) free(body_ptr);

  napi_value result;
  napi_get_undefined(env, &result);
  return result;
}

napi_value SetServerOptions(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);

  /* Parse options */
  /* TODO: Store in context */

  return NULL;
}

napi_value StreamData(napi_env env, napi_callback_info info) {
  /* TODO: Implement streaming */
  return NULL;
}

/**
 * Close the server
 */
napi_value Close(napi_env env, napi_callback_info info) {
  (void)info;

  if (g_context.server) {
    server_stop(g_context.server, 0);
    g_context.server = NULL;
  }

  /* Release the threadsafe function */
  if (g_context.tsfn) {
    napi_release_threadsafe_function(g_context.tsfn, napi_tsfn_abort);
    g_context.tsfn = NULL;
  }

  napi_value result;
  napi_get_boolean(env, true, &result);
  return result;
}

/* ============================================================================
 * Module Initialization
 * ============================================================================
 */

napi_value Init(napi_env env, napi_value exports) {
  napi_value fn;

  /* Initialize fast router with default routes */
  fast_router_register_defaults();

  /* RegisterRoute */
  napi_create_function(env, "RegisterRoute", NAPI_AUTO_LENGTH, RegisterRoute,
                       NULL, &fn);
  napi_set_named_property(env, exports, "RegisterRoute", fn);

  /* RegisterFastRoute */
  napi_create_function(env, "RegisterFastRoute", NAPI_AUTO_LENGTH,
                       RegisterFastRoute, NULL, &fn);
  napi_set_named_property(env, exports, "RegisterFastRoute", fn);

  /* Listen */
  napi_create_function(env, "Listen", NAPI_AUTO_LENGTH, Listen, NULL, &fn);
  napi_set_named_property(env, exports, "Listen", fn);

  /* SendResponse */
  napi_create_function(env, "SendResponse", NAPI_AUTO_LENGTH, SendResponse,
                       NULL, &fn);
  napi_set_named_property(env, exports, "SendResponse", fn);

  /* SetServerOptions */
  napi_create_function(env, "SetServerOptions", NAPI_AUTO_LENGTH,
                       SetServerOptions, NULL, &fn);
  napi_set_named_property(env, exports, "SetServerOptions", fn);

  /* StreamData */
  napi_create_function(env, "StreamData", NAPI_AUTO_LENGTH, StreamData, NULL,
                       &fn);
  napi_set_named_property(env, exports, "StreamData", fn);

  /* Close */
  napi_create_function(env, "Close", NAPI_AUTO_LENGTH, Close, NULL, &fn);
  napi_set_named_property(env, exports, "Close", fn);

  /* Export version info */
  napi_value version;
  napi_create_string_utf8(env, "1.0.0", NAPI_AUTO_LENGTH, &version);
  napi_set_named_property(env, exports, "version", version);

  napi_value platform;
  napi_create_string_utf8(env, server_get_platform(), NAPI_AUTO_LENGTH,
                          &platform);
  napi_set_named_property(env, exports, "platform", platform);

  /* Export backend (same as platform) */
  napi_value backend;
  napi_create_string_utf8(env, server_get_platform(), NAPI_AUTO_LENGTH,
                          &backend);
  napi_set_named_property(env, exports, "backend", backend);

  return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
