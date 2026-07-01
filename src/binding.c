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

/* Platform atomic operations.
 * GCC/Clang: __atomic builtins (since GCC 4.7, Clang 3.1).
 * MSVC: _InterlockedExchangeAdd64 (available on x64, no C11 required). */
#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_InterlockedExchangeAdd64)
typedef volatile long long atomic_u64;
#define atomic_inc(p) _InterlockedExchangeAdd64(p, 1)
#define atomic_dec(p) _InterlockedExchangeAdd64(p, -1)
#define atomic_read(p) _InterlockedExchangeAdd64(p, 0)
#else
#include <stdatomic.h>
typedef _Atomic uint64_t atomic_u64;
#define atomic_inc(p) atomic_fetch_add(p, 1)
#define atomic_dec(p) atomic_fetch_sub(p, 1)
#define atomic_read(p) atomic_load(p)
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
  /* Atomic counters for graceful shutdown drain + production observability.
   * pending: requests handed to the JS thread that haven't been responded to.
   *          server_stop() waits for this to hit 0 (or timeout). */
  atomic_u64 pending_requests;
  /* Lifetime totals for observability. process.metrics() snapshots these. */
  atomic_u64 total_requests;
  atomic_u64 total_drops;  /* ts_fn queue full, request rejected */
  atomic_u64 total_errors; /* JS handler threw, not caught */
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
  /* method is the base of the contiguous method+path+query block.
   * Freeing it releases all three in one call. */
  if (data->method) free(data->method);
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

  /* Every dispatch is paired with this decrement — pairs with the
   * increment in on_request_c_handler. Covers all paths (early return
   * on null env, error during NAPI object creation, normal flow, and
   * JS-handler-throws). */
  if (env == NULL || js_callback == NULL) {
    atomic_fetch_sub(&g_context.pending_requests, 1);
    request_data_cleanup(req_data);
    return;
  }

  napi_status status;
  napi_value global;
  status = napi_get_global(env, &global);
  if (status != napi_ok) goto cleanup;

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

  /* 1b. Build headers object. Parser already extracted and lowercased
   * them into req_data->result.header_names / header_values (zero-copy,
   * points into the request buffer). Use napi_define_properties to batch
   * all property definitions in one call instead of per-header set_property. */
  {
    napi_value headers_obj;
    status = napi_create_object(env, &headers_obj);
    if (status != napi_ok) goto cleanup;
    size_t hc = req_data->result.header_count;
    if (hc > 0) {
      napi_value hnames[128];
      napi_value hvals[128];
      napi_property_descriptor hprops[128];
      for (size_t i = 0; i < hc; i++) {
        napi_create_string_utf8(env, req_data->result.header_names[i],
                                req_data->result.header_name_lens[i],
                                &hnames[i]);
        napi_create_string_utf8(env, req_data->result.header_values[i],
                                req_data->result.header_value_lens[i],
                                &hvals[i]);
        hprops[i] = (napi_property_descriptor){
            .name = hnames[i],
            .value = hvals[i],
            .attributes = napi_enumerable,
        };
      }
      napi_define_properties(env, headers_obj, hc, hprops);
    }
    napi_set_named_property(env, req_obj, "headers", headers_obj);
  }

  /* 2. Create Response object */
  napi_value res_obj;
  status = napi_create_object(env, &res_obj);
  if (status != napi_ok) goto cleanup;

  napi_value rfd;
  napi_create_int32(env, (int)req_data->conn->fd, &rfd);
  napi_set_named_property(env, res_obj, "fd", rfd);

  /* 4. Call JS callback */
  napi_value args[2] = {req_obj, res_obj};
  napi_value return_val;
  status = napi_call_function(env, global, js_callback, 2, args, &return_val);

cleanup:
  /* Decrement the in-flight counter. This label is hit on the success
   * path (after napi_call_function returns), on every goto cleanup in
   * the NAPI object-building blocks, and after JS errors are swallowed
   * by the chain — so the counter always balances with the increment
   * in on_request_c_handler. */
  atomic_fetch_sub(&g_context.pending_requests, 1);
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

  /* Single contiguous allocation for method + path + query. 3 fewer
   * malloc/free pairs per request (~450K/yr heap operations at 150K RPS). */
  size_t mlen = result->method ? result->method_len : 0;
  size_t plen = result->path ? result->path_len : 0;
  size_t qlen = result->query ? result->query_len : 0;
  size_t total = mlen + 1 + plen + 1 + qlen + 1;
  if (total > 0) {
    char* buf = malloc(total);
    char* p = buf;
    if (result->method) {
      memcpy(p, result->method, mlen);
      p[mlen] = '\0';
      req_data->method = p;
      p += mlen + 1;
    }
    if (result->path) {
      memcpy(p, result->path, plen);
      p[plen] = '\0';
      req_data->path = p;
      p += plen + 1;
    }
    if (result->query) {
      memcpy(p, result->query, qlen);
      p[qlen] = '\0';
      req_data->query = p;
    }
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

  /* 2. Dispatch to JS thread */
  atomic_fetch_add(&g_context.pending_requests, 1);
  atomic_fetch_add(&g_context.total_requests, 1);
  napi_status status = napi_call_threadsafe_function(g_context.ts_fn, req_data,
                                                     napi_tsfn_blocking);
  if (status != napi_ok) {
    atomic_fetch_sub(&g_context.pending_requests, 1);
    atomic_fetch_add(&g_context.total_drops, 1);
    request_data_cleanup(req_data);
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

  /* 2. Initialize Threadsafe Function */
  napi_value resource_name;
  napi_create_string_utf8(env, "iopress-callback", NAPI_AUTO_LENGTH,
                          &resource_name);

  napi_status status = napi_create_threadsafe_function(
      env, args[2], NULL, resource_name, 65536, 1, NULL, NULL, NULL,
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

    /* Read into a 256-byte stack buffer first. Most HTTP headers fit;
     * one N-API call instead of two (probe + fill). For oversized
     * headers (>255 bytes) we fall back to a second call. */
    char stack_buf[256];
    size_t vlen;
    napi_get_value_string_utf8(env, val, stack_buf, sizeof(stack_buf), &vlen);
    if (vlen < sizeof(stack_buf)) {
      header_strings[k] = malloc(vlen + 1);
      memcpy(header_strings[k], stack_buf, vlen + 1);
    } else {
      header_strings[k] = malloc(vlen + 1);
      napi_get_value_string_utf8(env, val, header_strings[k], vlen + 1, &vlen);
    }
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

  /* NOTE: argument is reserved for future graceful shutdown. See
   * comment in the body below. */
  size_t argc = 1;
  napi_value args[1];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);
  (void)args;

  if (g_context.server) {
    /* NOTE: We don't drain in-flight requests here. The reason is
     * subtle: Close() runs on the JS thread, and any drain loop
     * (usleep / sleep / poll) on the JS thread blocks the entire
     * Node event loop. The JS handlers that hold pending_requests
     * decrements run on this same thread, so they'd never get a
     * chance to finish — classic self-deadlock.
     *
     * Two correct ways to drain (not implemented in v1.0.5; tracked
     * in AUDIT.md as a future improvement):
     *   1. Stop accepting new requests in C, then signal the worker
     *      thread to join (which also joins the in-flight handlers).
     *   2. Run the drain from a C-side pthread that polls pending
     *      atomically and unblocks the JS thread via a tsfn call.
     *
     * For now, callers wanting graceful shutdown should:
     *   a) Stop accepting external traffic (LB, iptables)
     *   b) Wait for app.metrics().pending to reach 0
     *   c) Call app.close()
     */
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

/**
 * Metrics() — production observability snapshot.
 *
 * Returns a plain JS object with atomic counters. Safe to call from
 * any code path; reads are non-blocking. Counts are lifetime totals
 * since process start, except pending which is a current gauge.
 *
 *   {
 *     pending: <int>,    // requests in-flight on the JS thread
 *     total:   <int>,    // lifetime accepted requests
 *     drops:   <int>,    // requests rejected (ts_fn queue full)
 *     errors:  <int>,    // JS handler errors
 *   }
 */
static napi_value Metrics(napi_env env, napi_callback_info info) {
  (void)info;
  napi_value obj;
  napi_create_object(env, &obj);

  napi_value val;
  napi_create_int64(env, (int64_t)atomic_read(&g_context.pending_requests),
                    &val);
  napi_set_named_property(env, obj, "pending", val);
  napi_create_int64(env, (int64_t)atomic_read(&g_context.total_requests), &val);
  napi_set_named_property(env, obj, "total", val);
  napi_create_int64(env, (int64_t)atomic_read(&g_context.total_drops), &val);
  napi_set_named_property(env, obj, "drops", val);
  napi_create_int64(env, (int64_t)atomic_read(&g_context.total_errors), &val);
  napi_set_named_property(env, obj, "errors", val);

  return obj;
}

/**
 * BumpError() — atomically increment the total_errors counter.
 *
 * Called from JS when a request handler throws an uncaught error
 * (caught by the _executeChain error boundary). Cheap, no-arg.
 */
static napi_value BumpError(napi_env env, napi_callback_info info) {
  (void)env;
  (void)info;
  atomic_fetch_add(&g_context.total_errors, 1);
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

  napi_create_function(env, "Metrics", NAPI_AUTO_LENGTH, Metrics, NULL, &fn);
  napi_set_named_property(env, exports, "Metrics", fn);

  napi_create_function(env, "BumpError", NAPI_AUTO_LENGTH, BumpError, NULL,
                       &fn);
  napi_set_named_property(env, exports, "BumpError", fn);

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
