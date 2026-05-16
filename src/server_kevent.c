/**
 * iopress kqueue Backend (macOS/BSD)
 *
 * High-performance event-driven HTTP server using kqueue.
 *
 * @file server_kevent.c
 * @version 1.1.0
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "fast_router.h"
#include "http2.h"
#include "parser.h"
#include "server.h"

/* Suppress unused function warnings */
#if defined(__APPLE__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif

static void* xp_memmem(const void* haystack, size_t haystack_len,
                       const void* needle, size_t needle_len) {
  if (needle_len == 0) return (void*)haystack;
  if (haystack_len < needle_len) return NULL;
  const char* h = haystack;
  const char* n = needle;
  for (size_t i = 0; i <= haystack_len - needle_len; i++) {
    if (memcmp(h + i, n, needle_len) == 0) return (void*)(h + i);
  }
  return NULL;
}

#if defined(__APPLE__)
#pragma clang diagnostic pop
#endif

/* ============================================================================
 * Platform Context
 * ============================================================================
 */

typedef struct kevent_context {
  int kq;
  int listen_fd;
  bool running;
  pthread_t event_thread;
  int worker_id;
  server_config_t config;
  request_callback_t on_request;
  connection_callback_t on_connection;
  void* user_data;
  connection_t* connections;
  size_t max_connections;
} kevent_context_t;

#define MAX_WORKERS 32
struct server_handle_s {
  kevent_context_t workers[MAX_WORKERS];
  int worker_count;
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

static void tune_socket(int fd) {
  set_nodelay(fd);
#if defined(__APPLE__)
  int nosigpipe = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &nosigpipe, sizeof(nosigpipe));
#endif
  int bufsize = 1048576;
  setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));
  setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
}

/* ============================================================================
 * Connection Pool
 * ============================================================================
 */

#define BUFFER_POOL_SLOTS 16384
static uint8_t* g_buffer_pool[BUFFER_POOL_SLOTS];
static int g_buffer_pool_init = 0;

static void init_buffer_pool(void) {
  if (g_buffer_pool_init) return;
  for (int i = 0; i < BUFFER_POOL_SLOTS; i++) {
    g_buffer_pool[i] = malloc(DEFAULT_INITIAL_BUFFER_SIZE);
  }
  g_buffer_pool_init = 1;
}

static connection_t* get_connection(kevent_context_t* ctx, int fd) {
  if (fd < 0 || (size_t)fd >= ctx->max_connections) return NULL;
  connection_t* conn = &ctx->connections[fd];
  if (conn->fd == -1) {
    memset(conn, 0, sizeof(*conn));
    conn->fd = fd;
    conn->platform_ctx = ctx;
    conn->buffer_cap = DEFAULT_INITIAL_BUFFER_SIZE;
    if (fd < BUFFER_POOL_SLOTS && g_buffer_pool[fd]) {
      conn->buffer = g_buffer_pool[fd];
      conn->uses_pool_buffer = 1;
    } else {
      conn->buffer = malloc(conn->buffer_cap);
      conn->uses_pool_buffer = 0;
    }
  }
  return conn;
}

static void reset_connection(connection_t* conn) {
  if (!conn) return;
  conn->body_remaining = 0;
  conn->streaming = false;
  conn->keep_alive = true;
  conn->request_complete = false;
  conn->headers_sent = false;
  conn->out_buffer_len = 0;
  conn->out_buffer_pos = 0;
  if (conn->assembled_body) {
    free(conn->assembled_body);
    conn->assembled_body = NULL;
    conn->assembled_body_len = 0;
  }
}

static void close_connection(kevent_context_t* ctx, int fd) {
  if (fd < 0) return;
  connection_t* conn = &ctx->connections[fd];
  if (conn->fd != -1) {
    if (ctx->on_connection) ctx->on_connection(conn, 1, ctx->user_data);
    close(fd);
    conn->fd = -1;
    if (conn->buffer && !conn->uses_pool_buffer) free(conn->buffer);
    conn->buffer = NULL;
    if (conn->out_buffer) free(conn->out_buffer);
    conn->out_buffer = NULL;
    conn->out_buffer_cap = 0;
    if (conn->assembled_body) {
      free(conn->assembled_body);
      conn->assembled_body = NULL;
    }
  }
}

/* ============================================================================
 * I/O Handlers
 * ============================================================================
 */

static int conn_write(kevent_context_t* ctx, connection_t* conn,
                      const void* data, size_t len) {
  if (conn->out_buffer_len > 0) {
    size_t rem = len;
    if (conn->out_buffer_len + rem > conn->out_buffer_cap) {
      size_t new_cap =
          conn->out_buffer_cap == 0 ? 16384 : conn->out_buffer_cap * 2;
      while (new_cap < conn->out_buffer_len + rem) new_cap *= 2;
      uint8_t* b = realloc(conn->out_buffer, new_cap);
      if (!b) return -1;
      conn->out_buffer = b;
      conn->out_buffer_cap = new_cap;
    }
    memcpy(conn->out_buffer + conn->out_buffer_len, data, rem);
    conn->out_buffer_len += rem;
    return 0;
  }
  ssize_t n = write(conn->fd, data, len);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      n = 0;
    else
      return -1;
  }
  if ((size_t)n < len) {
    size_t rem = len - n;
    if (conn->out_buffer_len + rem > conn->out_buffer_cap) {
      size_t new_cap =
          conn->out_buffer_cap == 0 ? 16384 : conn->out_buffer_cap * 2;
      while (new_cap < conn->out_buffer_len + rem) new_cap *= 2;
      uint8_t* b = realloc(conn->out_buffer, new_cap);
      if (!b) return -1;
      conn->out_buffer = b;
      conn->out_buffer_cap = new_cap;
    }
    memcpy(conn->out_buffer + conn->out_buffer_len, (const uint8_t*)data + n,
           rem);
    conn->out_buffer_len += rem;
    struct kevent ev;
    EV_SET(&ev, conn->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0,
           conn);
    kevent(ctx->kq, &ev, 1, NULL, 0, NULL);
  }
  return 0;
}

static int conn_write_buffered(kevent_context_t* ctx, connection_t* conn,
                               const void* data, size_t len) {
  size_t rem = len;
  if (conn->out_buffer_len + rem > conn->out_buffer_cap) {
    size_t new_cap =
        conn->out_buffer_cap == 0 ? 16384 : conn->out_buffer_cap * 2;
    while (new_cap < conn->out_buffer_len + rem) new_cap *= 2;
    uint8_t* b = realloc(conn->out_buffer, new_cap);
    if (!b) return -1;
    conn->out_buffer = b;
    conn->out_buffer_cap = new_cap;
  }
  memcpy(conn->out_buffer + conn->out_buffer_len, data, rem);
  conn->out_buffer_len += rem;
  return 0;
}

static void handle_client_write(kevent_context_t* ctx, int fd,
                                connection_t* conn) {
  if (conn->out_buffer_len == 0) return;
  ssize_t n = write(fd, conn->out_buffer + conn->out_buffer_pos,
                    conn->out_buffer_len - conn->out_buffer_pos);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      struct kevent ev;
      EV_SET(&ev, fd, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, conn);
      kevent(ctx->kq, &ev, 1, NULL, 0, NULL);
      return;
    }
    close_connection(ctx, fd);
    return;
  }
  conn->out_buffer_pos += n;
  if (conn->out_buffer_pos == conn->out_buffer_len) {
    conn->out_buffer_pos = 0;
    conn->out_buffer_len = 0;
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(ctx->kq, &ev, 1, NULL, 0, NULL);
    if (conn->request_complete && conn->headers_sent) {
      if (!conn->keep_alive)
        close_connection(ctx, fd);
      else
        reset_connection(conn);
    }
  } else {
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, conn);
    kevent(ctx->kq, &ev, 1, NULL, 0, NULL);
  }
}

static void handle_client_read(kevent_context_t* ctx, int fd,
                               connection_t* conn) {
  if (conn->processing) return;

  while (1) {
    if (conn->buffer_len >= conn->buffer_cap) {
      size_t nc = conn->buffer_cap * 2;
      uint8_t* b = realloc(conn->buffer, nc);
      if (!b) {
        close_connection(ctx, fd);
        return;
      }
      conn->buffer = b;
      conn->buffer_cap = nc;
    }
    ssize_t n = read(fd, conn->buffer + conn->buffer_len,
                     conn->buffer_cap - conn->buffer_len);
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;
      close_connection(ctx, fd);
      return;
    }
    if (n == 0) {
      close_connection(ctx, fd);
      return;
    }
    conn->buffer_len += n;
    if ((size_t)n < conn->buffer_cap - (conn->buffer_len - n)) break;
  }

  size_t offset = 0;
  int processed = 0;
  while (processed < 256) {
    if (offset >= conn->buffer_len) break;
    parse_result_t res;
    int status = http_parse_request(conn->buffer + offset,
                                    conn->buffer_len - offset, &res);
    if (status == PARSE_STATUS_ERROR) {
      close_connection(ctx, fd);
      return;
    }
    if (status == PARSE_STATUS_NEED_MORE) break;
    if (status == PARSE_STATUS_DONE) {
      uint8_t* fr = NULL;
      size_t frl = 0;
      if (fast_router_try_handle_full(res.method, res.method_len, res.path,
                                      res.path_len, &fr, &frl) == 0) {
        conn_write_buffered(ctx, conn, fr, frl);
        processed++;
        offset += res.bytes_consumed;
        if (res.http_minor < 1) {
          handle_client_write(ctx, fd, conn);
          close_connection(ctx, fd);
          return;
        }
        continue;
      }
      if (conn->out_buffer_len > 0) handle_client_write(ctx, fd, conn);

      conn->keep_alive = res.http_minor >= 1;
      if (ctx->on_request) ctx->on_request(conn, &res, ctx->user_data);
      processed++;
      offset += res.bytes_consumed;
      conn->request_complete = false;
      conn->headers_sent = false;
      if (conn->processing) break;
    }
  }
  if (conn->out_buffer_len > 0) handle_client_write(ctx, fd, conn);
  if (offset > 0) {
    if (offset < conn->buffer_len) {
      memmove(conn->buffer, conn->buffer + offset, conn->buffer_len - offset);
      conn->buffer_len -= offset;
    } else {
      conn->buffer_len = 0;
    }
  }
}

static void* event_loop_thread(void* arg) {
  kevent_context_t* ctx = (kevent_context_t*)arg;
  struct kevent events[1024];
  while (ctx->running) {
    int nev = kevent(ctx->kq, NULL, 0, events, 1024, NULL);
    if (nev < 0) {
      if (errno == EINTR) continue;
      break;
    }
    for (int i = 0; i < nev; i++) {
      struct kevent* ev = &events[i];
      if ((int)ev->ident == ctx->listen_fd) {
        while (1) {
          struct sockaddr_in addr;
          socklen_t al = sizeof(addr);
          int cfd = accept(ctx->listen_fd, (struct sockaddr*)&addr, &al);
          if (cfd < 0) break;
          set_nonblocking(cfd);
          tune_socket(cfd);
          connection_t* conn = get_connection(ctx, cfd);
          if (conn) {
            if (ctx->on_connection) ctx->on_connection(conn, 0, ctx->user_data);
            struct kevent sev;
            EV_SET(&sev, cfd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0,
                   conn);
            kevent(ctx->kq, &sev, 1, NULL, 0, NULL);
          } else
            close(cfd);
        }
      } else if (ev->filter == EVFILT_READ) {
        connection_t* conn = (connection_t*)ev->udata;
        if (conn && conn->fd == (int)ev->ident)
          handle_client_read(ctx, ev->ident, conn);
      } else if (ev->filter == EVFILT_WRITE) {
        connection_t* conn = (connection_t*)ev->udata;
        if (conn && conn->fd == (int)ev->ident)
          handle_client_write(ctx, ev->ident, conn);
      }
    }
  }
  return NULL;
}

/* ============================================================================
 * Public API
 * ============================================================================
 */

server_handle_t server_init(const server_config_t* config,
                            request_callback_t on_request,
                            connection_callback_t on_connection,
                            void* user_data) {
  int wc = (int)sysconf(_SC_NPROCESSORS_ONLN);
  if (wc > MAX_WORKERS) wc = MAX_WORKERS;
  if (wc < 1) wc = 1;
  server_handle_t s = calloc(1, sizeof(*s));
  if (!s) return NULL;
  s->worker_count = wc;
  init_buffer_pool();
  connection_t* conns = calloc(MAX_CONNECTIONS, sizeof(connection_t));
  for (size_t i = 0; i < MAX_CONNECTIONS; i++) conns[i].fd = -1;
  for (int i = 0; i < wc; i++) {
    kevent_context_t* ctx = &s->workers[i];
    ctx->worker_id = i;
    ctx->config = *config;
    if (ctx->config.backlog == 0) ctx->config.backlog = 4096;
    ctx->on_request = on_request;
    ctx->on_connection = on_connection;
    ctx->user_data = user_data;
    ctx->connections = conns;
    ctx->max_connections = MAX_CONNECTIONS;
    ctx->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    set_reuseaddr(ctx->listen_fd);
    set_reuseport(ctx->listen_fd);
    set_nonblocking(ctx->listen_fd);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ctx->config.port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (ctx->config.bind_address)
      inet_pton(AF_INET, ctx->config.bind_address, &addr.sin_addr);
    bind(ctx->listen_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(ctx->listen_fd, ctx->config.backlog);
    ctx->kq = kqueue();
    struct kevent ev;
    EV_SET(&ev, ctx->listen_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
    kevent(ctx->kq, &ev, 1, NULL, 0, NULL);
  }
  return s;
}

int server_start(server_handle_t s) {
  for (int i = 0; i < s->worker_count; i++) {
    s->workers[i].running = true;
    pthread_create(&s->workers[i].event_thread, NULL, event_loop_thread,
                   &s->workers[i]);
  }
  return 0;
}

int server_stop(server_handle_t s, uint32_t timeout_ms) {
  for (int i = 0; i < s->worker_count; i++) {
    s->workers[i].running = false;
    struct kevent ev;
    EV_SET(&ev, 0, EVFILT_USER, EV_ADD | EV_ENABLE, NOTE_TRIGGER, 0, NULL);
    kevent(s->workers[i].kq, &ev, 1, NULL, 0, NULL);
    pthread_join(s->workers[i].event_thread, NULL);
    close(s->workers[i].listen_fd);
    close(s->workers[i].kq);
  }
  free(s->workers[0].connections);
  free(s);
  return 0;
}

int server_send_response(server_handle_t s, connection_t* conn,
                         const response_t* res) {
  kevent_context_t* ctx = (kevent_context_t*)conn->platform_ctx;
  if (!ctx) ctx = &s->workers[0];

  char head[4096];
  size_t hlen = 0;

  /* 1. Format status line */
  hlen += snprintf(head + hlen, sizeof(head) - hlen, "HTTP/1.1 %d OK\r\n",
                   res->status_code);

  /* 2. Format headers */
  if (res->headers) {
    for (size_t i = 0; i < res->header_count * 2 && res->headers[i]; i += 2) {
      hlen += snprintf(head + hlen, sizeof(head) - hlen, "%s: %s\r\n",
                       res->headers[i], res->headers[i + 1]);
    }
  }

  /* 3. Format Content-Length */
  hlen += snprintf(head + hlen, sizeof(head) - hlen,
                   "Content-Length: %zu\r\n\r\n", res->body_len);

  /* 4. Write headers */
  if (conn_write(ctx, conn, head, hlen) < 0) return -1;

  /* 5. Write body */
  if (res->body && res->body_len > 0) {
    if (conn_write(ctx, conn, res->body, res->body_len) < 0) return -1;
  }

  conn->headers_sent = true;
  if (conn->out_buffer_len == 0) {
    if (!conn->keep_alive)
      close_connection(ctx, conn->fd);
    else
      reset_connection(conn);
  }
  return 0;
}

int server_write(server_handle_t s, connection_t* conn, const void* data,
                 size_t len) {
  kevent_context_t* ctx = (kevent_context_t*)conn->platform_ctx;
  if (!ctx) ctx = &s->workers[0];
  return conn_write(ctx, conn, data, len);
}

int server_pause_read(server_handle_t s, connection_t* conn) {
  conn->processing = true;
  struct kevent ev;
  EV_SET(&ev, conn->fd, EVFILT_READ, EV_DELETE, 0, 0, conn);
  kevent_context_t* ctx = (kevent_context_t*)conn->platform_ctx;
  if (!ctx) ctx = &s->workers[0];
  return kevent(ctx->kq, &ev, 1, NULL, 0, NULL);
}

int server_resume_read(server_handle_t s, connection_t* conn) {
  conn->processing = false;
  struct kevent ev;
  EV_SET(&ev, conn->fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, conn);
  kevent_context_t* ctx = (kevent_context_t*)conn->platform_ctx;
  if (!ctx) ctx = &s->workers[0];
  return kevent(ctx->kq, &ev, 1, NULL, 0, NULL);
}

connection_t* server_get_connection_by_fd(server_handle_t s, int fd) {
  if (fd < 0 || (size_t)fd >= MAX_CONNECTIONS) return NULL;
  connection_t* conn = &s->workers[0].connections[fd];
  return conn->fd == fd ? conn : NULL;
}

const char* server_get_platform(void) { return "kqueue"; }
const char* server_get_version(void) { return "1.1.0"; }
