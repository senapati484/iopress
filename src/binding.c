/**
 * Express-Pro N-API Binding
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
} request_data_t;

/* ============================================================================
 * C Request Handler (Called from server thread)
 * ============================================================================
 */

static int on_request_c_handler(connection_t* conn,
                                const parse_result_t* result, void* user_data) {
  (void)user_data;

  /* Allocate request data for thread-safe passing */
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

  /* Copy body */
  if (result->body_present && result->body_length > 0) {
    req_data->body = malloc(result->body_length);
    if (req_data->body) {
      memcpy(req_data->body, conn->buffer + result->body_start,
             result->body_length);
      req_data->body_len = result->body_length;
    }
  }

  g_context.current_conn = conn;
  g_context.current_conn = conn;

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

  /* Build response object with fd attached */
  napi_value res_fd_val;
  napi_create_int32(env, req_data->fd, &res_fd_val);
  napi_set_named_property(env, res_obj, "fd", res_fd_val);

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

  /* Get fd from res._fd */
  napi_value fd_val;
  int32_t fd = -1;
  if (napi_get_named_property(env, this_val, "_fd", &fd_val) == napi_ok) {
    napi_get_value_int32(env, fd_val, &fd);
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

  /* Send using stored connection */
  if (g_context.server && g_context.current_conn) {
    server_send_response(g_context.server, g_context.current_conn, &resp);
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
  }

  /* Create thread-safe function */
  napi_value async_resource_name;
  napi_create_string_utf8(env, "express-pro", NAPI_AUTO_LENGTH,
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
  (void)env;

  size_t argc = 3;
  napi_value args[3];
  napi_get_cb_info(env, info, &argc, args, NULL, NULL);

  /* Get fd (not used directly, we use current_conn) */
  int32_t fd;
  napi_get_value_int32(env, args[0], &fd);

  /* Get status */
  int32_t status;
  napi_get_value_int32(env, args[1], &status);

  /* Get body */
  char body[4096];
  size_t body_len;
  napi_get_value_string_utf8(env, args[2], body, sizeof(body), &body_len);

  /* Build response */
  response_t resp = {0};
  resp.status_code = status;
  resp.body = (uint8_t*)body;
  resp.body_len = body_len;
  resp.is_last = true;

  /* Send using current connection */
  if (g_context.server && g_context.current_conn) {
    server_send_response(g_context.server, g_context.current_conn, &resp);
  }

  return NULL;
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

/* ============================================================================
 * Module Initialization
 * ============================================================================
 */

napi_value Init(napi_env env, napi_value exports) {
  napi_value fn;

  /* RegisterRoute */
  napi_create_function(env, "RegisterRoute", NAPI_AUTO_LENGTH, RegisterRoute,
                       NULL, &fn);
  napi_set_named_property(env, exports, "RegisterRoute", fn);

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

  /* Export version info */
  napi_value version;
  napi_create_string_utf8(env, "1.0.0", NAPI_AUTO_LENGTH, &version);
  napi_set_named_property(env, exports, "version", version);

  napi_value platform;
  napi_create_string_utf8(env, server_get_platform(), NAPI_AUTO_LENGTH,
                          &platform);
  napi_set_named_property(env, exports, "platform", platform);

  return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
