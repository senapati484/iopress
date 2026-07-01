/**
 * iopress N-API Binding
 *
 * Bridge between JavaScript and the native HTTP server.
 * Handles thread-safe communication using napi_threadsafe_function.
 *
 * @file binding.c
 * @version 1.0.5
 */

#include <node_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "fast_router.h"
#include "parser.h"
#include "router.h"
#include "server.h"

/* ============================================================================
 * Global Context
 * ============================================================================
 */

typedef struct {
  server_handle_t server;
  napi_threadsafe_function ts_fn;
  napi_env env;
  server_config_t config;
} global_context_t;

static global_context_t g_context = {0};

/* ============================================================================
 * Request Data Structure
 * ============================================================================
 */

typedef struct {
  connection_t* conn;
  parse_result_t result;
  char* method;
  char* path;
  char* query;
  uint8_t* body;
  size_t body_len;
  /* When true, `body` is owned by a V8 external Buffer; the external
   * finalize callback will free it. request_data_cleanup must NOT free it. */
  bool body_owned_by_v8;
} request_data_t;

/* ============================================================================
 * JavaScript Callbacks
 * ============================================================================
 */

/**
 * Cleanup function for request data
 */
static void request_data_cleanup(request_data_t* data) {
  if (!data) return;
  if (data->method) free(data->method);
  if (data->path) free(data->path);
  if (data->query) free(data->query);
  /* Only free body if V8 did not take ownership via external buffer. */
  if (data->body && !data->body_owned_by_v8) free(data->body);
  free(data);
}

/**
 * Finalize callback for the request body external buffer.
 *
 * V8 calls this when the JS-side Buffer that wraps our request body is
 * garbage-collected. We free the underlying C allocation here so the
 * body does not need to be copied on the way into V8 (zero-copy path).
 */
static void request_body_external_finalize(napi_env env, void* data,
                                           void* hint) {
  (void)env;
  (void)hint;
  if (data) free(data);
}

/**
 * JS Thread Invocation
 */
static void CallJsHandler(napi_env env, napi_value js_callback, void* context,
                          void* data) {
  (void)context;
  request_data_t* req_data = (request_data_t*)data;

  if (env == NULL || js_callback == NULL) {
    request_data_cleanup(req_data);
    return;
  }

  napi_status status;
  napi_value global;
  status = napi_get_global(env, &global);
  if (status != napi_ok) goto cleanup;

  /* 1. Create Request object */
  napi_value req_obj;
  status = napi_create_object(env, &req_obj);
  if (status != napi_ok) goto cleanup;

  napi_value val;
  napi_create_string_utf8(env, req_data->method, NAPI_AUTO_LENGTH, &val);
  napi_set_named_property(env, req_obj, "method", val);

  napi_create_string_utf8(env, req_data->path, NAPI_AUTO_LENGTH, &val);
  napi_set_named_property(env, req_obj, "path", val);

  if (req_data->query) {
    napi_create_string_utf8(env, req_data->query, NAPI_AUTO_LENGTH, &val);
  } else {
    napi_get_null(env, &val);
  }
  napi_set_named_property(env, req_obj, "query", val);

  napi_create_int32(env, (int)req_data->conn->fd, &val);
  napi_set_named_property(env, req_obj, "fd", val);

  if (req_data->body_len > 0) {
    napi_value body_buf;
    /* Zero-copy: hand V8 the malloc'd body directly. The external finalize
     * callback (request_body_external_finalize) will free() it when the JS
     * Buffer is GC'd. Mark the body as V8-owned so request_data_cleanup
     * does NOT double-free it. */
    status = napi_create_external_buffer(
        env, req_data->body_len, req_data->body, request_body_external_finalize,
        NULL, &body_buf);
    if (status == napi_ok) {
      req_data->body_owned_by_v8 = true;
      napi_set_named_property(env, req_obj, "body", body_buf);
    } else {
      napi_get_null(env, &val);
      napi_set_named_property(env, req_obj, "body", val);
    }
  } else {
    napi_get_null(env, &val);
    napi_set_named_property(env, req_obj, "body", val);
  }

  /* 2. Create Response object */
  napi_value res_obj;
  status = napi_create_object(env, &res_obj);
  if (status != napi_ok) goto cleanup;

  napi_create_int32(env, (int)req_data->conn->fd, &val);
  napi_set_named_property(env, res_obj, "fd", val);

  /* 3. Call JS callback */
  napi_value args[2] = {req_obj, res_obj};
  napi_value return_val;
  status = napi_call_function(env, global, js_callback, 2, args, &return_val);

cleanup:
  request_data_cleanup(req_data);
}

/**
 * Native request handler (called from event loop thread)
 */
static int on_request_c_handler(connection_t* conn,
                                const parse_result_t* result, void* user_data) {
  (void)user_data;

  /* Check if TS_FN is initialized */
  if (g_context.ts_fn == NULL) return 0;

  /* 1. Capture request data */
  request_data_t* req_data = calloc(1, sizeof(request_data_t));
  if (!req_data) return 0;

  req_data->conn = conn;
  req_data->result = *result;

  if (result->method) {
    req_data->method = malloc(result->method_len + 1);
    memcpy(req_data->method, result->method, result->method_len);
    req_data->method[result->method_len] = '\0';
  }

  if (result->path) {
    req_data->path = malloc(result->path_len + 1);
    memcpy(req_data->path, result->path, result->path_len);
    req_data->path[result->path_len] = '\0';
  }

  if (result->query) {
    req_data->query = malloc(result->query_len + 1);
    memcpy(req_data->query, result->query, result->query_len);
    req_data->query[result->query_len] = '\0';
  }

  if (result->body_present && result->body_length > 0) {
    const uint8_t* body_ptr = NULL;
    size_t available = 0;

    if (result->chunked && !conn->assembled_body) {
      conn->assembled_body = malloc(result->body_length);
      if (conn->assembled_body) {
        conn->assembled_body_len = (size_t)http_assemble_chunked_body(
            conn->buffer, result, conn->assembled_body, result->body_length);
      }
    }

    if (conn->assembled_body) {
      body_ptr = conn->assembled_body;
      available = conn->assembled_body_len;
    } else {
      body_ptr = conn->buffer + result->body_start;
      if (conn->buffer_len > result->body_start) {
        available = conn->buffer_len - result->body_start;
        if (result->body_length < available) {
          available = result->body_length;
        }
      }
    }

    req_data->body_len = available;
    req_data->body = malloc(available > 0 ? available : 1);
    if (available > 0) {
      memcpy(req_data->body, body_ptr, available);
    }
  }

  /* 2. Dispatch to JS thread. Use nonblocking so the C event-loop thread
   * never stalls on JS backpressure (the worst tail-latency cliff under
   * high keep-alive load). The queue is bounded at 1024 entries; if full,
   * napi_tsfn_nonblocking returns napi_queue_full and we drop the request
   * (the connection will be closed below — the client will reconnect). */
  napi_status status = napi_call_threadsafe_function(g_context.ts_fn, req_data,
                                                     napi_tsfn_nonblocking);
  if (status != napi_ok) {
    /* Queue full or thread terminating. Free our captured data and close
     * the connection so the client doesn't hang. The kqueue backend will
     * see the fd close and clean up its connection slot. */
    request_data_cleanup(req_data);
    if (conn && conn->fd >= 0) {
      close(conn->fd);
    }
  }

  return 0;
}

/* ============================================================================
 * Exported Functions
 * ============================================================================
 */

/**
 * SetServerOptions(options)
 */
static napi_value SetServerOptions(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);

  if (argc < 1) return NULL;

  napi_value options = args[0];
  napi_value val;
  bool has_prop;

  /* initialBufferSize */
  napi_has_named_property(env, options, "initialBufferSize", &has_prop);
  if (has_prop) {
    napi_get_named_property(env, options, "initialBufferSize", &val);
    int32_t size;
    napi_get_value_int32(env, val, &size);
    g_context.config.initial_buffer_size = (size_t)size;
  }

  /* maxBodySize */
  napi_has_named_property(env, options, "maxBodySize", &has_prop);
  if (has_prop) {
    napi_get_named_property(env, options, "maxBodySize", &val);
    int32_t size;
    napi_get_value_int32(env, val, &size);
    g_context.config.max_body_size = (size_t)size;
  }

  /* reusePort */
  napi_has_named_property(env, options, "reusePort", &has_prop);
  if (has_prop) {
    napi_get_named_property(env, options, "reusePort", &val);
    bool reuse;
    napi_get_value_bool(env, val, &reuse);
    g_context.config.reuse_port = reuse;
  }

  return NULL;
}

/**
 * Listen(port, config, on_request)
 */
static napi_value Listen(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value args[3];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);

  if (argc < 3) {
    napi_throw_error(env, NULL, "Wrong number of arguments");
    return NULL;
  }

  /* 1. Parse port */
  int32_t port;
  napi_get_value_int32(env, args[0], &port);

  /* 2. Initialize Threadsafe Function
   *
   * Args: js_callback, NULL, resource_name, queue_size (1024 — bounded
   *       for predictable memory; with nonblocking semantics we never
   *       want the queue to grow without limit), max_threads (1), ... */
  napi_value resource_name;
  napi_create_string_utf8(env, "iopress-callback", NAPI_AUTO_LENGTH,
                          &resource_name);

  napi_status status = napi_create_threadsafe_function(
      env, args[2], NULL, resource_name, 1024, 1, NULL, NULL, NULL,
      CallJsHandler, &g_context.ts_fn);

  if (status != napi_ok) {
    napi_throw_error(env, NULL, "Failed to create threadsafe function");
    return NULL;
  }

  /* 3. Start Server */
  g_context.config.port = (uint16_t)port;
  if (!g_context.config.bind_address) g_context.config.bind_address = "0.0.0.0";
  if (g_context.config.backlog == 0) g_context.config.backlog = 4096;

  g_context.server =
      server_init(&g_context.config, on_request_c_handler, NULL, NULL);
  if (!g_context.server) {
    napi_throw_error(env, NULL, "Failed to initialize server");
    return NULL;
  }

  if (server_start(g_context.server) != 0) {
    napi_throw_error(env, NULL, "Failed to start server thread");
    return NULL;
  }

  /* Return server info object */
  napi_value result;
  napi_create_object(env, &result);

  napi_value val;
  napi_create_int32(env, port, &val);
  napi_set_named_property(env, result, "port", val);

  return result;
}

/**
 * SendResponse(fd, status_code, body, headers)
 */
static napi_value SendResponse(napi_env env, napi_callback_info info) {
  size_t argc = 4;
  napi_value args[4];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);

  int32_t fd;
  napi_get_value_int32(env, args[0], &fd);

  int32_t status_code;
  napi_get_value_int32(env, args[1], &status_code);

  /* Get connection */
  connection_t* conn = server_get_connection_by_fd(g_context.server, fd);
  if (!conn) return NULL;

  response_t res = {0};
  res.status_code = (uint16_t)status_code;

  /* 1. Parse body (args[2]) */
  bool is_buffer;
  napi_is_buffer(env, args[2], &is_buffer);

  uint8_t* body_ptr = NULL;
  size_t body_len = 0;

  if (is_buffer) {
    napi_get_buffer_info(env, args[2], (void**)&body_ptr, &body_len);
    res.body = body_ptr;
    res.body_len = body_len;
  } else {
    /* String body */
    napi_valuetype type;
    napi_typeof(env, args[2], &type);
    if (type == napi_string) {
      napi_get_value_string_utf8(env, args[2], NULL, 0, &body_len);
      body_ptr = malloc(body_len + 1);
      napi_get_value_string_utf8(env, args[2], (char*)body_ptr, body_len + 1,
                                 &body_len);
      res.body = body_ptr;
      res.body_len = body_len;
    }
  }

  /* 2. Parse headers flat array (args[3])
   *
   * Format: [name0, value0, name1, value1, ...] — built by the JS layer
   * in Response.send(). Replaces the old object-walk that did one
   * napi_get_all_property_names + N napi_get_property calls per response.
   * The array path is one napi_get_array_length + 2*n napi_get_element
   * calls — no V8 object walk, no hidden-class walk. The flat array also
   * means the names and values land in the same order on both sides, so
   * we don't need to re-pair them in C. */
  uint32_t header_count = 0;
  napi_get_array_length(env, args[3], &header_count);

  const char* headers[256];
  char* header_strings[256];
  size_t header_lens[256];
  uint32_t k = 0;

  for (uint32_t i = 0; i < header_count && k < 254; i++) {
    napi_value val;
    napi_get_element(env, args[3], i, &val);

    size_t vlen;
    napi_get_value_string_utf8(env, val, NULL, 0, &vlen);
    header_strings[k] = malloc(vlen + 1);
    napi_get_value_string_utf8(env, val, header_strings[k], vlen + 1, &vlen);
    headers[k] = header_strings[k];
    header_lens[k] = vlen;
    k++;
  }

  res.headers = headers;
  res.header_lens = header_lens;
  res.header_count = k / 2;

  server_send_response(g_context.server, conn, &res);

  /* Cleanup header strings */
  for (uint32_t i = 0; i < k; i++) {
    free(header_strings[i]);
  }

  if (!is_buffer && body_ptr) free(body_ptr);

  return NULL;
}

/**
 * RegisterFastRoute(method, path, status, body)
 */
static napi_value RegisterFastRoute(napi_env env, napi_callback_info info) {
  size_t argc = 4;
  napi_value args[4];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);

  char method[16];
  size_t method_len;
  napi_get_value_string_utf8(env, args[0], method, sizeof(method), &method_len);

  char path[1024];
  size_t path_len;
  napi_get_value_string_utf8(env, args[1], path, sizeof(path), &path_len);

  int32_t status;
  napi_get_value_int32(env, args[2], &status);

  /* Parse body - can be string or buffer */
  uint8_t* body_ptr = NULL;
  size_t body_len = 0;
  bool is_buffer;
  napi_is_buffer(env, args[3], &is_buffer);

  if (is_buffer) {
    napi_get_buffer_info(env, args[3], (void**)&body_ptr, &body_len);
  } else {
    napi_get_value_string_utf8(env, args[3], NULL, 0, &body_len);
    body_ptr = malloc(body_len + 1);
    napi_get_value_string_utf8(env, args[3], (char*)body_ptr, body_len + 1,
                               &body_len);
  }

  fast_router_register(method, path, ROUTE_TYPE_STATIC_JSON, (uint16_t)status,
                       "application/json", body_ptr, body_len);

  if (!is_buffer) free(body_ptr);

  return NULL;
}

/**
 * UnregisterFastRoute(method, path)
 */
static napi_value UnregisterFastRoute(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value args[2];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);

  char method[16];
  size_t method_len;
  napi_get_value_string_utf8(env, args[0], method, sizeof(method), &method_len);

  char path[1024];
  size_t path_len;
  napi_get_value_string_utf8(env, args[1], path, sizeof(path), &path_len);

  fast_router_unregister(method, path);

  return NULL;
}

/**
 * Close()
 */
static napi_value Close(napi_env env, napi_callback_info info) {
  (void)info;

  if (g_context.server) {
    server_stop(g_context.server, 0);
    g_context.server = NULL;
  }

  /* Release the threadsafe function */
  if (g_context.ts_fn) {
    napi_release_threadsafe_function(g_context.ts_fn, napi_tsfn_release);
    g_context.ts_fn = NULL;
  }

  return NULL;
}

/* ============================================================================
 * Module Initialization
 * ============================================================================
 */

napi_value Init(napi_env env, napi_value exports) {
  napi_value fn;

  napi_create_function(env, "Listen", NAPI_AUTO_LENGTH, Listen, NULL, &fn);
  napi_set_named_property(env, exports, "Listen", fn);

  napi_create_function(env, "SetServerOptions", NAPI_AUTO_LENGTH,
                       SetServerOptions, NULL, &fn);
  napi_set_named_property(env, exports, "SetServerOptions", fn);

  napi_create_function(env, "SendResponse", NAPI_AUTO_LENGTH, SendResponse,
                       NULL, &fn);
  napi_set_named_property(env, exports, "SendResponse", fn);

  napi_create_function(env, "RegisterFastRoute", NAPI_AUTO_LENGTH,
                       RegisterFastRoute, NULL, &fn);
  napi_set_named_property(env, exports, "RegisterFastRoute", fn);

  napi_create_function(env, "UnregisterFastRoute", NAPI_AUTO_LENGTH,
                       UnregisterFastRoute, NULL, &fn);
  napi_set_named_property(env, exports, "UnregisterFastRoute", fn);

  napi_create_function(env, "Close", NAPI_AUTO_LENGTH, Close, NULL, &fn);
  napi_set_named_property(env, exports, "Close", fn);

  /* Export version info */
  napi_value version;
  napi_create_string_utf8(env, "1.0.5", NAPI_AUTO_LENGTH, &version);
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
