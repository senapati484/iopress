/**
 * iopress io_uring Backend (Linux)
 *
 * High-performance Linux backend using io_uring for async I/O.
 *
 * @file server_uring.c
 * @version 1.1.0
 */

#include "server.h"

#ifdef USE_IO_URING

#include <errno.h>
#include <fcntl.h>
#include <liburing.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "fast_router.h"
#include "parser.h"

/* ============================================================================
 * Constants
 * ============================================================================
 */

#define QUEUE_DEPTH 1024
#define BATCH_SIZE 256
#define MAX_PIPELINE_BATCH 64
#define MAX_ACCEPT_BATCH 512

/* io_uring operation types */
typedef enum { OP_ACCEPT, OP_RECV, OP_SEND } op_type_t;

typedef struct {
  op_type_t type;
  void* ptr;
} uring_data_t;

/* ============================================================================
 * Helper Functions
 * ============================================================================
 */

static char* fast_itoa_size(size_t val, char* buf) {
  char tmp[16];
  int i = 0;
  if (val == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return buf;
  }
  while (val > 0) {
    tmp[i++] = '0' + (val % 10);
    val /= 10;
  }
  for (int j = 0; j < i; j++) {
    buf[j] = tmp[i - 1 - j];
  }
  buf[i] = '\0';
  return buf;
}

static char* fast_itoa(uint16_t val, char* buf) {
  return fast_itoa_size((size_t)val, buf);
}

static int format_http_response_fast(int status_code, const uint8_t* body,
                                     size_t body_len, uint8_t* out,
                                     size_t out_cap, size_t* out_len) {
  (void)out_cap;

  /* Fast status line - optimized for common cases */
  out[0] = 'H';
  out[1] = 'T';
  out[2] = 'T';
  out[3] = 'P';
  out[4] = '/';
  out[5] = '1';
  out[6] = '.';
  out[7] = '1';
  out[8] = ' ';
  char* p = (char*)out + 9;

  if (status_code == 200) {
    p[0] = '2';
    p[1] = '0';
    p[2] = '0';
    p[3] = ' ';
    p[4] = 'O';
    p[5] = 'K';
    p += 6;
  } else if (status_code == 404) {
    p[0] = '4';
    p[1] = '0';
    p[2] = '4';
    p[3] = ' ';
    p[4] = 'N';
    p[5] = 'o';
    p[6] = 't';
    p[7] = ' ';
    p[8] = 'F';
    p[9] = 'o';
    p[10] = 'u';
    p[11] = 'n';
    p[12] = 'd';
    p += 13;
  } else if (status_code == 400) {
    p[0] = '4';
    p[1] = '0';
    p[2] = '0';
    p[3] = ' ';
    p[4] = 'B';
    p[5] = 'a';
    p[6] = 'd';
    p[7] = ' ';
    p[8] = 'R';
    p[9] = 'e';
    p[10] = 'q';
    p[11] = 'u';
    p[12] = 'e';
    p[13] = 's';
    p[14] = 't';
    p += 15;
  } else {
    fast_itoa((uint16_t)status_code, p);
    while (*p) p++;
    *p++ = ' ';
    const char* sp = "OK";
    while (*sp) *p++ = *sp++;
  }

  /* Content-Length header */
  *p++ = '\r';
  *p++ = '\n';
  const char* cl = "Content-Length: ";
  while (*cl) *p++ = *cl++;
  fast_itoa_size(body_len, p);
  while (*p) p++;
  *p++ = '\r';
  *p++ = '\n';

  /* End headers */
  *p++ = '\r';
  *p++ = '\n';

  /* Body */
  if (body_len > 0 && body != NULL) {
    memcpy(p, body, body_len);
    p += body_len;
  }

  *out_len = p - (char*)out;
  return 0;
}

static int format_http_response(int status_code, const char** headers,
                                size_t header_count, const uint8_t* body,
                                size_t body_len, uint8_t* out, size_t out_cap,
                                size_t* out_len) {
  const char* status_text = "OK";
  switch (status_code) {
    case 200:
      status_text = "OK";
      break;
    case 201:
      status_text = "Created";
      break;
    case 400:
      status_text = "Bad Request";
      break;
    case 404:
      status_text = "Not Found";
      break;
    case 500:
      status_text = "Internal Server Error";
      break;
  }

  int len = snprintf((char*)out, out_cap, "HTTP/1.1 %d %s\r\n", status_code,
                     status_text);
  if (len < 0) return -1;
  char* p = (char*)out + len;

  for (size_t i = 0; i < header_count * 2; i += 2) {
    len = snprintf(p, out_cap - (p - (char*)out), "%s: %s\r\n", headers[i],
                   headers[i + 1]);
    if (len < 0) return -1;
    p += len;
  }

  len = snprintf(p, out_cap - (p - (char*)out), "Content-Length: %zu\r\n\r\n",
                 body_len);
  if (len < 0) return -1;
  p += len;

  if (body_len > 0 && body != NULL) {
    if ((size_t)(p - (char*)out) + body_len > out_cap) return -1;
    memcpy(p, body, body_len);
    p += body_len;
  }

  *out_len = p - (char*)out;
  return 0;
}

/* ============================================================================
 * Platform Context
 * ============================================================================
 */

typedef struct uring_context {
  struct io_uring ring;
  int listen_fd;
  bool running;
  pthread_t event_thread;

  /* Static data for common operations to avoid malloc */
  uring_data_t accept_data;

  /* Server config and callbacks */
  server_config_t config;
  request_callback_t on_request;
  connection_callback_t on_connection;
  void* user_data;

  /* Connection pool - indexed by fd */
  connection_t* connections;
  size_t max_connections;

  /* Batch submission */
  unsigned int pending_sqes;
} uring_context_t;

/* Server handle wrapper */
struct server_handle_s {
  uring_context_t ctx;
};

/* ============================================================================
 * Socket Utilities
 * ============================================================================
 */

static int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) return -1;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int set_reuseport(int fd) {
  int opt = 1;
  return setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
}

static int set_reuseaddr(int fd) {
  int opt = 1;
  return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

static int set_nodelay(int fd) {
  int opt = 1;
  return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
}

static int set_socket_options(int fd) {
  set_nodelay(fd);

  int sndbuf = 1048576;
  int rcvbuf = 1048576;
  setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
  setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

  int keepalive = 1;
  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));

  return 0;
}

/* ============================================================================
 * Connection Pool
 * ============================================================================
 */

static connection_t* get_connection(connection_t* pool, size_t pool_size,
                                    int fd) {
  if (fd < 0 || (size_t)fd >= pool_size) return NULL;
  connection_t* conn = &pool[fd];
  if (conn->fd == -1) {
    memset(conn, 0, sizeof(*conn));
    conn->fd = fd;
    conn->buffer_cap = DEFAULT_INITIAL_BUFFER_SIZE;
    conn->buffer = malloc(conn->buffer_cap);
    if (conn->buffer == NULL) return NULL;

    /* Pre-allocate SQE data objects */
    uring_data_t* recv_data = malloc(sizeof(uring_data_t));
    if (recv_data) {
      recv_data->type = OP_RECV;
      recv_data->ptr = conn;
      conn->platform_sqe_data_recv = recv_data;
    }

    uring_data_t* send_data = malloc(sizeof(uring_data_t));
    if (send_data) {
      send_data->type = OP_SEND;
      send_data->ptr = conn;
      conn->platform_sqe_data_send = send_data;
    }
  }
  return conn;
}

static void reset_connection(connection_t* conn) {
  if (conn == NULL) return;
  conn->buffer_len = 0;
  conn->buffer_pos = 0;
  conn->body_remaining = 0;
  conn->streaming = false;
  conn->keep_alive = true;
  conn->request_complete = false;
  conn->headers_sent = false;
  conn->user_data = NULL;

  /* Reset output buffer state but keep allocation for reuse */
  conn->out_buffer_len = 0;
  conn->out_buffer_pos = 0;
}

static void close_connection(uring_context_t* ctx, int fd) {
  if (fd < 0) return;

  connection_t* conn = &ctx->connections[fd];
  if (conn->fd != -1) {
    if (ctx->on_connection) {
      ctx->on_connection(conn, 1, ctx->user_data);
    }

    close(fd);
    conn->fd = -1;
    conn->closed = true;

    if (conn->buffer) {
      free(conn->buffer);
      conn->buffer = NULL;
    }
    conn->buffer_cap = 0;

    /* Cleanup platform_sqe_data */
    if (conn->platform_sqe_data_recv) {
      free(conn->platform_sqe_data_recv);
      conn->platform_sqe_data_recv = NULL;
    }
    if (conn->platform_sqe_data_send) {
      free(conn->platform_sqe_data_send);
      conn->platform_sqe_data_send = NULL;
    }

    /* Free output buffer */
    if (conn->out_buffer) {
      free(conn->out_buffer);
      conn->out_buffer = NULL;
    }
    conn->out_buffer_cap = 0;
    conn->out_buffer_len = 0;
    conn->out_buffer_pos = 0;

    /* Cleanup platform_ctx if it's a uring_data_t */
    if (conn->platform_ctx) {
      free(conn->platform_ctx);
      conn->platform_ctx = NULL;
    }
  }
}

/* ============================================================================
 * Submission Helpers
 * ============================================================================
 */

static void submit_pending(uring_context_t* ctx) {
  if (ctx->pending_sqes > 0) {
    io_uring_submit(&ctx->ring);
    ctx->pending_sqes = 0;
  }
}

static void arm_send(uring_context_t* ctx, connection_t* conn);

static int conn_write(uring_context_t* ctx, connection_t* conn,
                      const void* data, size_t len) {
  if (conn == NULL || data == NULL || len == 0) return -1;

  /* Append to output buffer */
  if (conn->out_buffer_len + len > conn->out_buffer_cap) {
    size_t new_cap =
        conn->out_buffer_cap == 0 ? 16384 : conn->out_buffer_cap * 2;
    while (new_cap < conn->out_buffer_len + len) new_cap *= 2;
    uint8_t* new_buf = realloc(conn->out_buffer, new_cap);
    if (!new_buf) return -1;
    conn->out_buffer = new_buf;
    conn->out_buffer_cap = new_cap;
  }

  memcpy(conn->out_buffer + conn->out_buffer_len, data, len);
  conn->out_buffer_len += len;

  /* If this is the only data, arm the send */
  if (conn->out_buffer_len == len) {
    arm_send(ctx, conn);
  }

  return 0;
}

static void arm_accept(uring_context_t* ctx) {
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ctx->ring);
  if (!sqe) {
    submit_pending(ctx);
    sqe = io_uring_get_sqe(&ctx->ring);
    if (!sqe) return;
  }

  ctx->accept_data.type = OP_ACCEPT;
  ctx->accept_data.ptr = NULL;

  io_uring_prep_accept(sqe, ctx->listen_fd, NULL, NULL, 0);
  io_uring_sqe_set_data(sqe, &ctx->accept_data);
  ctx->pending_sqes++;

  if (ctx->pending_sqes >= BATCH_SIZE) {
    submit_pending(ctx);
  }
}

static void arm_recv(uring_context_t* ctx, connection_t* conn) {
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ctx->ring);
  if (!sqe) {
    submit_pending(ctx);
    sqe = io_uring_get_sqe(&ctx->ring);
    if (!sqe) return;
  }

  size_t space = conn->buffer_cap - conn->buffer_len;
  if (space == 0) {
    size_t new_cap = conn->buffer_cap * 2;
    if (new_cap > ctx->config.max_body_size && ctx->config.max_body_size > 0) {
      new_cap = ctx->config.max_body_size;
    }
    uint8_t* new_buf = realloc(conn->buffer, new_cap);
    if (!new_buf) return;
    conn->buffer = new_buf;
    conn->buffer_cap = new_cap;
    space = conn->buffer_cap - conn->buffer_len;
  }

  uring_data_t* data = (uring_data_t*)conn->platform_sqe_data_recv;
  if (!data) return;

  io_uring_prep_recv(sqe, conn->fd, conn->buffer + conn->buffer_len, space, 0);
  io_uring_sqe_set_data(sqe, data);
  ctx->pending_sqes++;

  if (ctx->pending_sqes >= BATCH_SIZE) {
    submit_pending(ctx);
  }
}

static void arm_send(uring_context_t* ctx, connection_t* conn) {
  if (conn->out_buffer_len == 0) return;

  struct io_uring_sqe* sqe = io_uring_get_sqe(&ctx->ring);
  if (!sqe) {
    submit_pending(ctx);
    sqe = io_uring_get_sqe(&ctx->ring);
    if (!sqe) return;
  }

  uring_data_t* data = (uring_data_t*)conn->platform_sqe_data_send;
  if (!data) return;

  io_uring_prep_send(sqe, conn->fd, conn->out_buffer + conn->out_buffer_pos,
                     conn->out_buffer_len - conn->out_buffer_pos, 0);
  io_uring_sqe_set_data(sqe, data);
  ctx->pending_sqes++;

  submit_pending(ctx);
}

/* ============================================================================
 * Event Handlers
 * ============================================================================
 */

static void handle_new_connection(uring_context_t* ctx) {
  int accepted = 0;

  while (accepted < MAX_ACCEPT_BATCH) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    int client_fd =
        accept(ctx->listen_fd, (struct sockaddr*)&client_addr, &addr_len);
    if (client_fd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;
      break;
    }

    if (set_nonblocking(client_fd) < 0) {
      close(client_fd);
      continue;
    }

    set_socket_options(client_fd);

    connection_t* conn =
        get_connection(ctx->connections, ctx->max_connections, client_fd);
    if (conn == NULL) {
      close(client_fd);
      continue;
    }

    if (ctx->on_connection) {
      ctx->on_connection(conn, 0, ctx->user_data);
    }

    arm_recv(ctx, conn);
    accepted++;
  }

  arm_accept(ctx);
}

static void handle_send_completion(uring_context_t* ctx, connection_t* conn,
                                   int res) {
  if (res <= 0) {
    close_connection(ctx, conn->fd);
    return;
  }

  conn->out_buffer_pos += res;

  if (conn->out_buffer_pos == conn->out_buffer_len) {
    /* All data sent */
    conn->out_buffer_pos = 0;
    conn->out_buffer_len = 0;

    /* Handle keep-alive/reset */
    if (conn->request_complete && conn->headers_sent) {
      if (!conn->keep_alive) {
        close_connection(ctx, conn->fd);
      } else {
        reset_connection(conn);
        arm_recv(ctx, conn);
      }
    }
  } else {
    /* Partial send, arm another send */
    arm_send(ctx, conn);
  }
}

int server_pause_read(server_handle_t server, connection_t* conn) {
  if (!server || !conn || conn->fd < 0) return -1;
  conn->processing = true;
  return 0;
}

int server_resume_read(server_handle_t server, connection_t* conn) {
  if (!server || !conn || conn->fd < 0) return -1;
  conn->processing = false;
  /* Re-arm RECV immediately when resuming */
  arm_recv(&server->ctx, conn);
  return 0;
}

static void handle_recv_completion(uring_context_t* ctx, connection_t* conn,
                                   int bytes_read) {
  if (conn->processing) return;
  if (bytes_read <= 0) {
    close_connection(ctx, conn->fd);
    return;
  }
  ... conn->buffer_len += bytes_read;

  parse_result_t result;
  int status = http_parse_request(conn->buffer, conn->buffer_len, &result);

  if (status == PARSE_STATUS_ERROR) {
    const char* headers[] = {"Connection", "close", NULL};
    uint8_t response[256];
    size_t response_len;
    format_http_response(400, headers, 2, NULL, 0, response, sizeof(response),
                         &response_len);
    conn_write(ctx, conn, response, response_len);
    close_connection(ctx, conn->fd);
    return;
  }

  if (status == PARSE_STATUS_NEED_MORE) {
    arm_recv(ctx, conn);
    return;
  }

  if (status == PARSE_STATUS_DONE) {
    conn->request_complete = true;
    conn->keep_alive = result.http_minor >= 1;

    /* ULTRA FAST PATH: Check dynamic fast router first */
    uint8_t* fast_response = NULL;
    size_t fast_response_len = 0;

    int fast_handled = fast_router_try_handle_full(
        result.method, result.method_len, result.path, result.path_len,
        &fast_response, &fast_response_len);

    if (fast_handled == 0 && fast_response != NULL && fast_response_len > 0) {
      conn->headers_sent = true;
      conn_write(ctx, conn, fast_response, fast_response_len);
      return;
    }

    if (ctx->on_request) {
      ctx->on_request(conn, &result, ctx->user_data);
    }
  }
}

/* ============================================================================
 * Event Loop Thread
 * ============================================================================
 */

static void* event_loop_thread(void* arg) {
  uring_context_t* ctx = (uring_context_t*)arg;
  struct io_uring_cqe* cqe;

  arm_accept(ctx);

  while (ctx->running) {
    submit_pending(ctx);

    int ret = io_uring_wait_cqe(&ctx->ring, &cqe);
    if (ret < 0) {
      if (ret == -EINTR) continue;
      break;
    }

    uring_data_t* data = (uring_data_t*)io_uring_cqe_get_data(cqe);
    int res = cqe->res;

    if (data) {
      switch (data->type) {
        case OP_ACCEPT:
          handle_new_connection(ctx);
          break;
        case OP_RECV:
          handle_recv_completion(ctx, (connection_t*)data->ptr, res);
          break;
        case OP_SEND:
          handle_send_completion(ctx, (connection_t*)data->ptr, res);
          break;
      }
    }

    io_uring_cqe_seen(&ctx->ring, cqe);
  }

  return NULL;
}

/* ============================================================================
 * Server Interface Implementation
 * ============================================================================
 */

server_handle_t server_init(const server_config_t* config,
                            request_callback_t on_request,
                            connection_callback_t on_connection,
                            void* user_data) {
  server_handle_t server = calloc(1, sizeof(*server));
  if (server == NULL) return NULL;

  /* Initialize fast router */
  fast_router_init();
  fast_router_register_defaults();

  uring_context_t* ctx = &server->ctx;

  ctx->config = *config;
  if (ctx->config.initial_buffer_size == 0) {
    ctx->config.initial_buffer_size = DEFAULT_INITIAL_BUFFER_SIZE;
  }
  if (ctx->config.max_body_size == 0) {
    ctx->config.max_body_size = DEFAULT_MAX_BODY_SIZE;
  }
  if (ctx->config.port == 0) {
    ctx->config.port = 8080;
  }
  if (ctx->config.backlog == 0) {
    ctx->config.backlog = 511;
  }

  ctx->on_request = on_request;
  ctx->on_connection = on_connection;
  ctx->user_data = user_data;
  ctx->max_connections = MAX_CONNECTIONS;

  ctx->connections = calloc(ctx->max_connections, sizeof(connection_t));
  if (ctx->connections == NULL) {
    free(server);
    return NULL;
  }

  for (size_t i = 0; i < ctx->max_connections; i++) {
    ctx->connections[i].fd = -1;
  }

  ctx->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (ctx->listen_fd < 0) {
    free(ctx->connections);
    free(server);
    return NULL;
  }

  set_reuseaddr(ctx->listen_fd);
  set_reuseport(ctx->listen_fd);
  set_nonblocking(ctx->listen_fd);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(ctx->config.port);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(ctx->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(ctx->listen_fd);
    free(ctx->connections);
    free(server);
    return NULL;
  }

  if (listen(ctx->listen_fd, ctx->config.backlog) < 0) {
    close(ctx->listen_fd);
    free(ctx->connections);
    free(server);
    return NULL;
  }

  if (io_uring_queue_init(QUEUE_DEPTH, &ctx->ring, 0) < 0) {
    close(ctx->listen_fd);
    free(ctx->connections);
    free(server);
    return NULL;
  }

  return server;
}

int server_start(server_handle_t server) {
  if (server == NULL) return -1;

  uring_context_t* ctx = &server->ctx;
  ctx->running = true;

  if (pthread_create(&ctx->event_thread, NULL, event_loop_thread, ctx) != 0) {
    return -1;
  }

  return 0;
}

int server_stop(server_handle_t server, uint32_t timeout_ms) {
  (void)timeout_ms;
  if (server == NULL) return -1;

  uring_context_t* ctx = &server->ctx;
  ctx->running = false;

  io_uring_queue_exit(&ctx->ring);

  pthread_join(ctx->event_thread, NULL);

  for (size_t i = 0; i < ctx->max_connections; i++) {
    if (ctx->connections[i].fd >= 0) {
      close_connection(ctx, ctx->connections[i].fd);
    }
  }

  close(ctx->listen_fd);
  free(ctx->connections);

  return 0;
}

int server_send_response(server_handle_t server, connection_t* conn,
                         const response_t* response) {
  if (server == NULL || conn == NULL || response == NULL) return -1;
  if (conn->fd < 0) return -1;

  uint8_t buf[16384];
  size_t len;

  /* Use fast path for common case (no custom headers) */
  if (response->header_count == 0) {
    if (format_http_response_fast(response->status_code, response->body,
                                  response->body_len, buf, sizeof(buf),
                                  &len) < 0) {
      return -1;
    }
  } else {
    /* Robust header formatting */
    const char* headers[128];
    size_t count = response->header_count;
    if (count > 64) count = 64;

    for (size_t i = 0; i < count * 2; i++) {
      headers[i] = response->headers[i];
    }

    if (format_http_response(response->status_code, headers, count,
                             response->body, response->body_len, buf,
                             sizeof(buf), &len) < 0) {
      return -1;
    }
  }

  conn->headers_sent = true;
  return server_write(server, conn, buf, len);
}

int server_write(server_handle_t server, connection_t* conn, const void* data,
                 size_t len) {
  if (server == NULL || conn == NULL || data == NULL || len == 0) return -1;
  return conn_write(&server->ctx, conn, data, len);
}

ssize_t server_read_body(server_handle_t server, connection_t* conn,
                         size_t max_bytes) {
  if (server == NULL || conn == NULL) return -1;
  if (conn->fd < 0) return -1;

  size_t to_read = max_bytes;
  if (to_read == 0) to_read = conn->buffer_cap - conn->buffer_len;

  ssize_t n = read(conn->fd, conn->buffer + conn->buffer_len, to_read);
  if (n > 0) {
    conn->buffer_len += n;
  }

  return n;
}

connection_t* server_get_connection_by_fd(server_handle_t server, int fd) {
  if (server == NULL || fd < 0 || (size_t)fd >= server->ctx.max_connections) {
    return NULL;
  }
  connection_t* conn = &server->ctx.connections[fd];
  if (conn->fd != fd) return NULL;
  return conn;
}

const char* server_get_platform(void) { return "io_uring"; }

const char* server_get_version(void) { return "1.1.0"; }

#else

/* Stub when io_uring not available */
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
  return -1;
}
int server_stop(server_handle_t server, uint32_t timeout_ms) {
  (void)server;
  (void)timeout_ms;
  return -1;
}
int server_send_response(server_handle_t server, connection_t* conn,
                         const response_t* response) {
  (void)server;
  (void)conn;
  (void)response;
  return -1;
}
ssize_t server_read_body(server_handle_t server, connection_t* conn,
                         size_t max_bytes) {
  (void)server;
  (void)conn;
  (void)max_bytes;
  return -1;
}
const char* server_get_platform(void) { return "stub"; }
const char* server_get_version(void) { return "1.1.0"; }

#endif
