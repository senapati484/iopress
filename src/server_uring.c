/**
 * expressmax io_uring Backend (Linux)
 *
 * High-performance Linux backend using io_uring for async I/O.
 *
 * @file server_uring.c
 * @version 1.0.0
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

#include "parser.h"

/* ============================================================================
 * Constants
 * ============================================================================
 */

#define QUEUE_DEPTH 256
#define BATCH_SIZE 32

/* ============================================================================
 * Platform Context
 * ============================================================================
 */

typedef struct uring_context {
  struct io_uring ring;
  int listen_fd;
  bool running;
  pthread_t event_thread;

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
      conn->buffer_cap = 0;
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

static void arm_accept(uring_context_t* ctx) {
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ctx->ring);
  if (!sqe) {
    submit_pending(ctx);
    sqe = io_uring_get_sqe(&ctx->ring);
    if (!sqe) return;
  }

  io_uring_prep_accept(sqe, ctx->listen_fd, NULL, NULL, 0);
  io_uring_sqe_set_data(sqe, NULL);
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

  io_uring_prep_recv(sqe, conn->fd, conn->buffer + conn->buffer_len, space, 0);
  io_uring_sqe_set_data(sqe, conn);
  ctx->pending_sqes++;

  if (ctx->pending_sqes >= BATCH_SIZE) {
    submit_pending(ctx);
  }
}

/* ============================================================================
 * Event Handlers
 * ============================================================================
 */

static void handle_new_connection(uring_context_t* ctx) {
  struct sockaddr_in client_addr;
  socklen_t addr_len = sizeof(client_addr);

  int client_fd =
      accept(ctx->listen_fd, (struct sockaddr*)&client_addr, &addr_len);
  if (client_fd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) return;
    return;
  }

  if (set_nonblocking(client_fd) < 0) {
    close(client_fd);
    return;
  }

  set_nodelay(client_fd);

  connection_t* conn =
      get_connection(ctx->connections, ctx->max_connections, client_fd);
  if (conn == NULL) {
    close(client_fd);
    return;
  }

  if (ctx->on_connection) {
    ctx->on_connection(conn, 0, ctx->user_data);
  }

  arm_recv(ctx, conn);
  arm_accept(ctx);
}

static void handle_recv_completion(uring_context_t* ctx, connection_t* conn,
                                   int bytes_read) {
  if (bytes_read <= 0) {
    close_connection(ctx, conn->fd);
    return;
  }

  conn->buffer_len += bytes_read;

  parse_result_t result;
  int status = http_parse_request(conn->buffer, conn->buffer_len, &result);

  if (status == PARSE_STATUS_ERROR) {
    const char* response =
        "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: "
        "close\r\n\r\n";
    write(conn->fd, response, strlen(response));
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

    void* user_data = io_uring_cqe_get_data(cqe);
    int res = cqe->res;

    if (user_data == NULL) {
      handle_new_connection(ctx);
    } else {
      connection_t* conn = (connection_t*)user_data;
      handle_recv_completion(ctx, conn, res);
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

  char buf[4096];
  int len = snprintf(buf, sizeof(buf),
                     "HTTP/1.1 %d OK\r\nContent-Length: %zu\r\n\r\n",
                     response->status_code, response->body_len);

  if (response->body && response->body_len > 0) {
    memcpy(buf + len, response->body,
           response->body_len > sizeof(buf) - len - 1 ? sizeof(buf) - len - 1
                                                      : response->body_len);
    len += response->body_len > sizeof(buf) - len - 1 ? sizeof(buf) - len - 1
                                                      : response->body_len;
  }

  write(conn->fd, buf, len);
  conn->headers_sent = true;

  if (!conn->keep_alive || !response->is_last) {
    close_connection(&server->ctx, conn->fd);
  }

  return 0;
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

const char* server_get_platform(void) { return "io_uring"; }

const char* server_get_version(void) { return "1.0.0"; }

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
const char* server_get_version(void) { return "1.0.0"; }

#endif
