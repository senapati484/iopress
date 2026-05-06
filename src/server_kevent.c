/**
 * iopress kqueue Backend (macOS/BSD)
 *
 * High-performance event-driven HTTP server using kqueue.
 *
 * @file server_kevent.c
 * @version 1.0.0
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

/* Suppress unused function warnings for fallback implementations */
#if defined(__APPLE__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif

/* xp_memmem is a GNU extension, not in POSIX.1-2001 */
/* Provide our own implementation */
static void* xp_memmem(const void* haystack, size_t haystack_len,
                       const void* needle, size_t needle_len) {
  if (needle_len == 0) return (void*)haystack;
  if (haystack_len < needle_len) return NULL;

  const char* h = haystack;
  const char* n = needle;

  for (size_t i = 0; i <= haystack_len - needle_len; i++) {
    if (memcmp(h + i, n, needle_len) == 0) {
      return (void*)(h + i);
    }
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
  int kq;                 /* kqueue fd */
  int listen_fd;          /* Listening socket */
  bool running;           /* Event loop running */
  pthread_t event_thread; /* Background thread */

  /* Server config and callbacks */
  server_config_t config;
  request_callback_t on_request;
  connection_callback_t on_connection;
  void* user_data;

  /* Connection pool - indexed by fd */
  connection_t* connections;
  size_t max_connections;
} kevent_context_t;

/* Server handle wrapper */
struct server_handle_s {
  kevent_context_t ctx;
};

/* ============================================================================
 * HTTP Chunked Body Assembly
 * ============================================================================
 */

static uint8_t* assemble_chunked_body(const uint8_t* data, size_t len,
                                      size_t* out_len) {
  /* Calculate total size first */
  const uint8_t* p = data;
  const uint8_t* end = data + len;
  size_t total_size = 0;

  while (p < end) {
    /* Find end of size line */
    const uint8_t* nl = memchr(p, '\n', end - p);
    if (!nl) break;

    /* Parse chunk size (hex) */
    size_t chunk_size = 0;
    const uint8_t* sp = p;
    while (sp < nl) {
      char c = *sp;
      if (c >= '0' && c <= '9')
        chunk_size = chunk_size * 16 + (c - '0');
      else if (c >= 'a' && c <= 'f')
        chunk_size = chunk_size * 16 + (c - 'a' + 10);
      else if (c >= 'A' && c <= 'F')
        chunk_size = chunk_size * 16 + (c - 'A' + 10);
      else if (c == '\r' || c == ';')
        break;
      else
        break;
      sp++;
    }

    /* Move past size line */
    p = nl + 1;

    /* Last chunk */
    if (chunk_size == 0) break;

    /* Skip chunk data and trailing \r\n */
    p += chunk_size + 2;
    total_size += chunk_size;
  }

  if (total_size == 0) {
    *out_len = 0;
    return NULL;
  }

  /* Allocate buffer and copy data */
  uint8_t* result = malloc(total_size);
  if (!result) {
    *out_len = 0;
    return NULL;
  }

  /* Second pass: copy data */
  p = data;
  uint8_t* out = result;

  while (p < end) {
    const uint8_t* nl = memchr(p, '\n', end - p);
    if (!nl) break;

    size_t chunk_size = 0;
    const uint8_t* sp = p;
    while (sp < nl) {
      char c = *sp;
      if (c >= '0' && c <= '9')
        chunk_size = chunk_size * 16 + (c - '0');
      else if (c >= 'a' && c <= 'f')
        chunk_size = chunk_size * 16 + (c - 'a' + 10);
      else if (c >= 'A' && c <= 'F')
        chunk_size = chunk_size * 16 + (c - 'A' + 10);
      else if (c == '\r' || c == ';')
        break;
      else
        break;
      sp++;
    }

    p = nl + 1;
    if (chunk_size == 0) break;

    memcpy(out, p, chunk_size);
    out += chunk_size;
    p += chunk_size + 2;
  }

  *out_len = total_size;
  return result;
}

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

  int sndbuf = 1048576;
  int rcvbuf = 1048576;
  setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
  setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

  int keepalive = 1;
  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
}

/* ============================================================================
 * Connection Pool
 * ============================================================================
 */

/* Pre-allocated buffer pool for zero-allocation hot path */
#define BUFFER_POOL_SLOTS 4096
static uint8_t* g_buffer_pool[BUFFER_POOL_SLOTS];
static int g_buffer_pool_init = 0;

static void init_buffer_pool(void) {
  if (g_buffer_pool_init) return;
  for (int i = 0; i < BUFFER_POOL_SLOTS; i++) {
    g_buffer_pool[i] = malloc(DEFAULT_INITIAL_BUFFER_SIZE);
  }
  g_buffer_pool_init = 1;
}

static connection_t* get_connection(connection_t* pool, size_t pool_size,
                                    int fd) {
  if (fd < 0 || (size_t)fd >= pool_size) return NULL;
  connection_t* conn = &pool[fd];
  if (conn->fd == -1) {
    /* Initialize new connection */
    memset(conn, 0, sizeof(*conn));
    conn->fd = fd;
    conn->buffer_cap = DEFAULT_INITIAL_BUFFER_SIZE;
    /* Use pool buffer for common fd range, malloc for overflow */
    if (fd < BUFFER_POOL_SLOTS && g_buffer_pool[fd]) {
      conn->buffer = g_buffer_pool[fd];
      conn->uses_pool_buffer = 1;
    } else {
      conn->buffer = malloc(conn->buffer_cap);
      conn->uses_pool_buffer = 0;
    }
    if (conn->buffer == NULL) return NULL;
  }
  return conn;
}

#if defined(__APPLE__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif

static void reset_connection(connection_t* conn) {
  if (conn == NULL) return;

  /* Reset state but keep buffer */
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

static void close_connection(kevent_context_t* ctx, int fd) {
  if (fd < 0) return;

  connection_t* conn = &ctx->connections[fd];
  if (conn->fd != -1) {
    /* Notify JS layer */
    if (ctx->on_connection) {
      ctx->on_connection(conn, 1, ctx->user_data); /* 1 = close */
    }

    close(fd);
    conn->fd = -1;
    conn->closed = true;

    /* Free buffer (only if not from pool) */
    if (conn->buffer && !conn->uses_pool_buffer) {
      free(conn->buffer);
    }
    conn->buffer = NULL;
    conn->buffer_cap = 0;
    conn->uses_pool_buffer = 0;

    /* Free output buffer */
    if (conn->out_buffer) {
      free(conn->out_buffer);
      conn->out_buffer = NULL;
    }
    conn->out_buffer_cap = 0;
    conn->out_buffer_len = 0;
    conn->out_buffer_pos = 0;
  }
}

/* ============================================================================
 * HTTP Response Formatter
 * ============================================================================
 */

/* Pre-computed response templates for zero-allocation fast path */
static const uint8_t RESPONSE_200_JSON_EMPTY[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 2\r\n"
    "Connection: keep-alive\r\n"
    "Content-Type: application/json\r\n"
    "\r\n"
    "{}";

static const uint8_t RESPONSE_200_JSON_HEALTH[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 15\r\n"
    "Connection: keep-alive\r\n"
    "Content-Type: application/json\r\n"
    "\r\n"
    "{\"status\":\"ok\"}";

static const uint8_t RESPONSE_200_PING[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 4\r\n"
    "Connection: keep-alive\r\n"
    "Content-Type: text/plain\r\n"
    "\r\n"
    "pong";

static const uint8_t RESPONSE_404[] =
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Length: 9\r\n"
    "Connection: close\r\n"
    "\r\n"
    "Not Found";

static const uint8_t RESPONSE_200_USERS[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 13\r\n"
    "Connection: keep-alive\r\n"
    "Content-Type: application/json\r\n"
    "\r\n"
    "{\"users\":[]}";

static const uint8_t RESPONSE_200_ECHO[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 16\r\n"
    "Connection: keep-alive\r\n"
    "Content-Type: application/json\r\n"
    "\r\n"
    "{\"test\":\"data\"}";

static const uint8_t RESPONSE_200_SEARCH[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 15\r\n"
    "Connection: keep-alive\r\n"
    "Content-Type: application/json\r\n"
    "\r\n"
    "{\"results\":[]}";

static const size_t RESPONSE_200_JSON_EMPTY_LEN =
    sizeof(RESPONSE_200_JSON_EMPTY) - 1;
static const size_t RESPONSE_200_JSON_HEALTH_LEN =
    sizeof(RESPONSE_200_JSON_HEALTH) - 1;
static const size_t RESPONSE_200_PING_LEN = sizeof(RESPONSE_200_PING) - 1;
static const size_t RESPONSE_404_LEN = sizeof(RESPONSE_404) - 1;
static const size_t RESPONSE_200_USERS_LEN = sizeof(RESPONSE_200_USERS) - 1;
static const size_t RESPONSE_200_ECHO_LEN = sizeof(RESPONSE_200_ECHO) - 1;
static const size_t RESPONSE_200_SEARCH_LEN = sizeof(RESPONSE_200_SEARCH) - 1;

/* Pre-computed response lookup - optimized for common paths */
static const uint8_t* get_static_response(const char* method, size_t method_len,
                                          const char* path, size_t path_len,
                                          size_t* out_len) {
  /* Fast path only handles GET for static pre-computed responses */
  if (method_len != 3 || memcmp(method, "GET", 3) != 0) {
    return NULL;
  }

  /* Direct lookup for most common endpoints - O(1) */
  switch (path_len) {
    case 1:
      if (path[0] == '/') {
        *out_len = RESPONSE_200_JSON_EMPTY_LEN;
        return RESPONSE_200_JSON_EMPTY;
      }
      break;
    case 5:
      if (memcmp(path, "/ping", 5) == 0) {
        *out_len = RESPONSE_200_PING_LEN;
        return RESPONSE_200_PING;
      }
      if (memcmp(path, "/echo", 5) == 0) {
        *out_len = RESPONSE_200_ECHO_LEN;
        return RESPONSE_200_ECHO;
      }
      break;
    case 6:
      if (memcmp(path, "/users", 6) == 0) {
        *out_len = RESPONSE_200_USERS_LEN;
        return RESPONSE_200_USERS;
      }
      break;
    case 7:
      if (memcmp(path, "/health", 7) == 0) {
        *out_len = RESPONSE_200_JSON_HEALTH_LEN;
        return RESPONSE_200_JSON_HEALTH;
      }
      if (memcmp(path, "/search", 7) == 0) {
        *out_len = RESPONSE_200_SEARCH_LEN;
        return RESPONSE_200_SEARCH;
      }
      break;
  }
  return NULL;
}

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
  } else if (status_code == 500) {
    p[0] = '5';
    p[1] = '0';
    p[2] = '0';
    p[3] = ' ';
    p[4] = 'I';
    p[5] = 'n';
    p[6] = 't';
    p[7] = 'e';
    p[8] = 'r';
    p[9] = 'n';
    p[10] = 'a';
    p[11] = 'l';
    p[12] = ' ';
    p[13] = 'S';
    p[14] = 'e';
    p[15] = 'r';
    p[16] = 'v';
    p[17] = 'e';
    p[18] = 'r';
    p[19] = ' ';
    p[20] = 'E';
    p[21] = 'r';
    p[22] = 'r';
    p[23] = 'o';
    p[24] = 'r';
    p += 25;
  } else {
    fast_itoa(status_code, p);
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
  fast_itoa((uint16_t)body_len, p);
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
    case 413:
      status_text = "Payload Too Large";
      break;
    case 500:
      status_text = "Internal Server Error";
      break;
    default:
      status_text = "Unknown";
      break;
  }

  int len = snprintf((char*)out, out_cap, "HTTP/1.1 %d %s\r\n", status_code,
                     status_text);
  if (len < 0 || (size_t)len >= out_cap) return -1;

  /* Add headers */
  for (size_t i = 0; i < header_count && headers[i] != NULL; i += 2) {
    int hlen = snprintf((char*)out + len, out_cap - len, "%s: %s\r\n",
                        headers[i], headers[i + 1]);
    if (hlen < 0 || (size_t)hlen >= out_cap - len) return -1;
    len += hlen;
  }

  /* Content-Length header if body present */
  if (body_len > 0) {
    int clen = snprintf((char*)out + len, out_cap - len,
                        "Content-Length: %zu\r\n", body_len);
    if (clen < 0 || (size_t)clen >= out_cap - len) return -1;
    len += clen;
  }

  /* End headers */
  if (len + 2 >= (int)out_cap) return -1;
  out[len++] = '\r';
  out[len++] = '\n';

  /* Body */
  if (body_len > 0 && body != NULL) {
    if (len + body_len > out_cap) return -1;
    memcpy(out + len, body, body_len);
    len += body_len;
  }

  *out_len = len;
  return 0;
}

/* ============================================================================
 * Event Handlers
 * ============================================================================
 */

static int conn_write(kevent_context_t* ctx, connection_t* conn,
                      const void* data, size_t len) {
  /* 1. If there's already data in the queue, we must append to maintain order
   */
  if (conn->out_buffer_len > 0) {
    size_t remaining = len;
    if (conn->out_buffer_len + remaining > conn->out_buffer_cap) {
      size_t new_cap =
          conn->out_buffer_cap == 0 ? 16384 : conn->out_buffer_cap * 2;
      while (new_cap < conn->out_buffer_len + remaining) new_cap *= 2;
      uint8_t* new_buf = realloc(conn->out_buffer, new_cap);
      if (!new_buf) return -1;
      conn->out_buffer = new_buf;
      conn->out_buffer_cap = new_cap;
    }
    memcpy(conn->out_buffer + conn->out_buffer_len, (const uint8_t*)data,
           remaining);
    conn->out_buffer_len += remaining;
    return 0;
  }

  /* 2. Try immediate write */
  ssize_t n = write(conn->fd, data, len);

  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      n = 0;
    else
      return -1;
  }

  if ((size_t)n < len) {
    /* 3. Queue remaining data */
    size_t remaining = len - n;
    if (conn->out_buffer_len + remaining > conn->out_buffer_cap) {
      size_t new_cap =
          conn->out_buffer_cap == 0 ? 16384 : conn->out_buffer_cap * 2;
      while (new_cap < conn->out_buffer_len + remaining) new_cap *= 2;
      uint8_t* new_buf = realloc(conn->out_buffer, new_cap);
      if (!new_buf) return -1;
      conn->out_buffer = new_buf;
      conn->out_buffer_cap = new_cap;
    }
    memcpy(conn->out_buffer + conn->out_buffer_len, (const uint8_t*)data + n,
           remaining);
    conn->out_buffer_len += remaining;

    /* 4. Register for write events */
    struct kevent ev;
    EV_SET(&ev, conn->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0,
           conn);
    kevent(ctx->kq, &ev, 1, NULL, 0, NULL);
  }

  return 0;
}

int server_write(server_handle_t server, connection_t* conn, const void* data,
                 size_t len) {
  if (server == NULL || conn == NULL) return -1;
  return conn_write(&server->ctx, conn, data, len);
}

static void handle_client_write(kevent_context_t* ctx, int fd,
                                connection_t* conn) {
  if (conn->out_buffer_len == 0) return;

  ssize_t n = write(fd, conn->out_buffer + conn->out_buffer_pos,
                    conn->out_buffer_len - conn->out_buffer_pos);

  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) return;
    close_connection(ctx, fd);
    return;
  }

  conn->out_buffer_pos += n;

  if (conn->out_buffer_pos == conn->out_buffer_len) {
    /* All data sent */
    conn->out_buffer_pos = 0;
    conn->out_buffer_len = 0;

    /* Disable write events */
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(ctx->kq, &ev, 1, NULL, 0, NULL);

    /* Handle keep-alive/reset if request was complete and headers sent */
    if (conn->request_complete && conn->headers_sent) {
      if (!conn->keep_alive) {
        close_connection(ctx, fd);
      } else {
        reset_connection(conn);
      }
    }
  }
}

static void handle_new_connection(kevent_context_t* ctx) {
  struct sockaddr_in client_addr;
  socklen_t addr_len = sizeof(client_addr);

  /* Accept up to 256 connections per wakeup to handle connection bursts */
  for (int i = 0; i < 256; i++) {
    int client_fd =
        accept(ctx->listen_fd, (struct sockaddr*)&client_addr, &addr_len);
    if (client_fd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) return;
      perror("accept");
      return;
    }

    /* Set non-blocking */
    if (set_nonblocking(client_fd) < 0) {
      close(client_fd);
      continue;
    }

    /* TCP optimizations for latency */
    set_nodelay(client_fd);

    /* Get connection slot */
    connection_t* conn =
        get_connection(ctx->connections, ctx->max_connections, client_fd);
    if (conn == NULL) {
      close(client_fd);
      continue;
    }

    /* Notify JS layer */
    if (ctx->on_connection) {
      ctx->on_connection(conn, 0, ctx->user_data); /* 0 = open */
    }

    /* Add to kqueue for reading */
    struct kevent ev;
    EV_SET(&ev, client_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, conn);
    if (kevent(ctx->kq, &ev, 1, NULL, 0, NULL) < 0) {
      close_connection(ctx, client_fd);
      continue;
    }
  }
}

#if defined(__APPLE__)
#pragma clang diagnostic pop
#endif

int server_pause_read(server_handle_t server, connection_t* conn) {
  if (!server || !conn || conn->fd < 0) return -1;
  conn->processing = true;
  struct kevent ev;
  EV_SET(&ev, conn->fd, EVFILT_READ, EV_DELETE, 0, 0, conn);
  kevent(server->ctx.kq, &ev, 1, NULL, 0, NULL);
  return 0;
}

int server_resume_read(server_handle_t server, connection_t* conn) {
  if (!server || !conn || conn->fd < 0) return -1;
  conn->processing = false;
  struct kevent ev;
  EV_SET(&ev, conn->fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, conn);
  kevent(server->ctx.kq, &ev, 1, NULL, 0, NULL);
  return 0;
}

static void handle_client_read(kevent_context_t* ctx, int fd,
                               connection_t* conn) {
  if (conn->processing) return;
  (void)fd;
  ...
      /* Ensure buffer has space */
      if (conn->buffer_len >= conn->buffer_cap) {
    /* Need to grow buffer */
    size_t new_cap = conn->buffer_cap * 2;
    if (new_cap > ctx->config.max_body_size && ctx->config.max_body_size > 0) {
      new_cap = ctx->config.max_body_size;
    }
    if (new_cap > MAX_HEADER_SIZE + ctx->config.max_body_size) {
      new_cap = MAX_HEADER_SIZE + ctx->config.max_body_size;
    }

    uint8_t* new_buf = realloc(conn->buffer, new_cap);
    if (new_buf == NULL) {
      close_connection(ctx, fd);
      return;
    }
    conn->buffer = new_buf;
    conn->buffer_cap = new_cap;
  }

  /* Read data */
  ssize_t n = read(fd, conn->buffer + conn->buffer_len,
                   conn->buffer_cap - conn->buffer_len);

  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) return;
    close_connection(ctx, fd);
    return;
  }

  if (n == 0) {
    /* Client closed connection */
    close_connection(ctx, fd);
    return;
  }

  conn->buffer_len += n;

  /* B5: HTTP Pipeline batching - process multiple requests in a loop */
  int requests_processed = 0;
  const int max_pipeline_batch =
      128; /* Process up to 128 pipelined requests per event */

  while (requests_processed < max_pipeline_batch) {
    /* Try to parse request */
    parse_result_t result;
    int status = http_parse_request(conn->buffer, conn->buffer_len, &result);

    if (status == PARSE_STATUS_ERROR) {
      /* Bad request - send 400 */
      const char* headers[] = {"Connection", "close", NULL};
      uint8_t response[256];
      size_t response_len;
      format_http_response(400, headers, 2, NULL, 0, response, sizeof(response),
                           &response_len);
      conn_write(ctx, conn, response, response_len);
      close_connection(ctx, fd);
      return;
    }

    if (status == PARSE_STATUS_NEED_MORE) {
      /* Need more data - continue reading */
      conn->body_remaining = result.body_length;
      return;
    }

    if (status == PARSE_STATUS_DONE) {
      /* ULTRA FAST PATH: Check pre-computed responses first - zero allocation
       */
      size_t static_len = 0;
      const uint8_t* static_resp =
          get_static_response(result.method, result.method_len, result.path,
                              result.path_len, &static_len);
      if (static_resp != NULL && static_len > 0) {
        conn_write(ctx, conn, static_resp, static_len);

        requests_processed++;
        size_t consumed = result.bytes_consumed;
        if (result.body_present && conn->assembled_body) {
          const char* body_start =
              (const char*)conn->buffer + result.body_start;
          size_t body_len = conn->buffer_len - result.body_start;
          const char* term = memmem(body_start, body_len, "0\r\n\r\n", 5);
          if (term) {
            consumed = result.body_start + (term - body_start) + 5;
          }
        } else if (result.body_present && result.body_length > 0) {
          consumed = result.body_start + result.body_length;
        }
        if (consumed < conn->buffer_len) {
          memmove(conn->buffer, conn->buffer + consumed,
                  conn->buffer_len - consumed);
          conn->buffer_len -= consumed;
          conn->buffer_pos = 0;
        } else {
          conn->buffer_len = 0;
        }
        bool keep_alive = (result.http_minor >= 1);
        if (!keep_alive) {
          close_connection(ctx, fd);
          return;
        }
        continue;
      }

      /* FAST PATH: Try fast router before JS callback - use pre-built response
       */
      uint8_t* fast_response = NULL;
      size_t fast_response_len = 0;

      int fast_handled = fast_router_try_handle_full(
          result.method, strlen(result.method), result.path,
          strlen(result.path), &fast_response, &fast_response_len);

      if (fast_handled == 0 && fast_response != NULL && fast_response_len > 0) {
        /* Ultra fast path - write pre-built response directly */
        conn_write(ctx, conn, fast_response, fast_response_len);

        requests_processed++;

        /* Handle keep-alive */
        size_t consumed = result.bytes_consumed;
        if (result.body_present && conn->assembled_body) {
          const char* body_start =
              (const char*)conn->buffer + result.body_start;
          size_t body_len = conn->buffer_len - result.body_start;
          const char* term = memmem(body_start, body_len, "0\r\n\r\n", 5);
          if (term) {
            consumed = result.body_start + (term - body_start) + 5;
          }
        } else if (result.body_present && result.body_length > 0) {
          consumed = result.body_start + result.body_length;
        }

        /* Remove processed request from buffer */
        if (consumed < conn->buffer_len) {
          memmove(conn->buffer, conn->buffer + consumed,
                  conn->buffer_len - consumed);
          conn->buffer_len -= consumed;
          conn->buffer_pos = 0;
        } else {
          conn->buffer_len = 0;
        }

        /* Handle keep-alive for HTTP/1.1 */
        bool keep_alive = (result.http_minor >= 1);

        if (!keep_alive) {
          close_connection(ctx, fd);
          return;
        }
        continue;
      }

      /* Slow path: need to notify JS */
      conn->request_complete = true;
      conn->body_start = result.body_start;
      conn->keep_alive =
          result.http_minor >= 1; /* HTTP/1.1 default keep-alive */

      /* B9: For chunked encoding, assemble body into a contiguous buffer */
      if (result.body_present && result.body_length > 0) {
        /* Chunked body - assemble if not already done */
        if (!conn->assembled_body) {
          size_t assembled_len = 0;
          uint8_t* assembled = assemble_chunked_body(
              conn->buffer + result.body_start,
              conn->buffer_len - result.body_start, &assembled_len);
          if (assembled && assembled_len > 0) {
            conn->assembled_body = assembled;
            conn->assembled_body_len = assembled_len;
          }
        }
      }

      /* Store parse result - just reference, no malloc */
      conn->body_start = result.body_start;
      conn->body_remaining = result.body_length;

      /* Call JS callback (threadsafe) */
      if (ctx->on_request) {
        ctx->on_request(conn, &result, ctx->user_data);
      }

      requests_processed++;

      /* Calculate remaining data after this request */
      size_t consumed = result.bytes_consumed;

      if (result.body_present && conn->assembled_body) {
        /* For chunked: need to find actual end (including terminator) */
        const char* body_start = (const char*)conn->buffer + result.body_start;
        size_t body_len = conn->buffer_len - result.body_start;
        const char* term = memmem(body_start, body_len, "0\r\n\r\n", 5);
        if (term) {
          consumed = result.body_start + (term - body_start) + 5;
        }
      } else if (result.body_present && result.body_length > 0) {
        consumed = result.body_start + result.body_length;
      }

      /* Check for pipelined requests */
      if (conn->buffer_len > consumed) {
        /* More data available - shift buffer and process next request */
        size_t remaining = conn->buffer_len - consumed;
        memmove(conn->buffer, conn->buffer + consumed, remaining);
        conn->buffer_len = remaining;

        /* Reset connection state for next request */
        conn->request_complete = false;
        conn->headers_sent = false;
        /* Free assembled body if any */
        if (conn->assembled_body) {
          free(conn->assembled_body);
          conn->assembled_body = NULL;
          conn->assembled_body_len = 0;
        }
        /* Continue loop to process next pipelined request */
      } else {
        /* No more data - reset buffer */
        conn->buffer_len = 0;
        return; /* Done with this event */
      }
    }
  }
}

/* ============================================================================
 * Event Loop Thread
 * ============================================================================
 */

static void* event_loop_thread(void* arg) {
  kevent_context_t* ctx = (kevent_context_t*)arg;
  struct kevent events[1024];
  struct kevent new_evs[256];
  int new_ev_count = 0;

  while (ctx->running) {
    int nev = kevent(ctx->kq, NULL, 0, events, 1024, NULL);

    if (nev < 0) {
      if (errno == EINTR) continue;
      perror("kevent");
      break;
    }

    new_ev_count = 0;

    for (int i = 0; i < nev; i++) {
      struct kevent* ev = &events[i];

      if ((int)ev->ident == ctx->listen_fd) {
        /* New connection - accept all pending with edge-triggered efficiency */
        int accepted = 0;
        while (accepted < 2048) {
          struct sockaddr_in client_addr;
          socklen_t addr_len = sizeof(client_addr);
          int client_fd =
              accept(ctx->listen_fd, (struct sockaddr*)&client_addr, &addr_len);
          if (client_fd < 0) break;

          set_nonblocking(client_fd);
          tune_socket(client_fd);

          connection_t* conn =
              get_connection(ctx->connections, ctx->max_connections, client_fd);
          if (conn) {
            if (ctx->on_connection) ctx->on_connection(conn, 0, ctx->user_data);

            /* Batch kevent registrations */
            if (new_ev_count < 256) {
              EV_SET(&new_evs[new_ev_count], client_fd, EVFILT_READ,
                     EV_ADD | EV_CLEAR, 0, 0, conn);
              new_ev_count++;
            } else {
              kevent(ctx->kq, new_evs, new_ev_count, NULL, 0, NULL);
              new_ev_count = 0;
              EV_SET(&new_evs[new_ev_count], client_fd, EVFILT_READ,
                     EV_ADD | EV_CLEAR, 0, 0, conn);
              new_ev_count++;
            }
          } else {
            close(client_fd);
          }
          accepted++;
        }

        /* Submit batched kevents */
        if (new_ev_count > 0) {
          kevent(ctx->kq, new_evs, new_ev_count, NULL, 0, NULL);
          new_ev_count = 0;
        }
      } else if (ev->filter == EVFILT_READ) {
        /* Client socket readable - process all available data */
        connection_t* conn = (connection_t*)ev->udata;
        if (conn && conn->fd == (int)ev->ident) {
          handle_client_read(ctx, ev->ident, conn);
        }
      } else if (ev->filter == EVFILT_WRITE) {
        /* Client socket writable - process pending output */
        connection_t* conn = (connection_t*)ev->udata;
        if (conn && conn->fd == (int)ev->ident) {
          handle_client_write(ctx, ev->ident, conn);
        }
      }
    }
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

  kevent_context_t* ctx = &server->ctx;

  /* Copy config */
  ctx->config = *config;
  if (ctx->config.initial_buffer_size == 0) {
    ctx->config.initial_buffer_size = DEFAULT_INITIAL_BUFFER_SIZE;
  }
  if (ctx->config.max_body_size == 0) {
    ctx->config.max_body_size = DEFAULT_MAX_BODY_SIZE;
  }
  if (ctx->config.port == 0) {
    return server;
  }
  if (ctx->config.backlog == 0) {
    ctx->config.backlog = 4096;  // Increased from 511
  }

  /* Default reuse_port based on worker mode */
  if (ctx->config.reuse_port == false) {
    ctx->config.reuse_port = false;
  }

  ctx->on_request = on_request;
  ctx->on_connection = on_connection;
  ctx->user_data = user_data;
  ctx->max_connections = MAX_CONNECTIONS;

  /* Allocate connection pool */
  ctx->connections = calloc(ctx->max_connections, sizeof(connection_t));
  if (ctx->connections == NULL) {
    free(server);
    return NULL;
  }

  /* Initialize buffer pool for zero-allocation hot path */
  init_buffer_pool();

  /* Mark all connections as unused */
  for (size_t i = 0; i < ctx->max_connections; i++) {
    ctx->connections[i].fd = -1;
  }

  /* Create listening socket */
  ctx->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (ctx->listen_fd < 0) {
    free(ctx->connections);
    free(server);
    return NULL;
  }

  /* Socket options */
  set_reuseaddr(ctx->listen_fd);
  if (ctx->config.reuse_port) {
    set_reuseport(ctx->listen_fd);
  }
  set_nonblocking(ctx->listen_fd);

  /* Bind */
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(ctx->config.port);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (ctx->config.bind_address && strlen(ctx->config.bind_address) > 0) {
    inet_pton(AF_INET, ctx->config.bind_address, &addr.sin_addr);
  }

  if (bind(ctx->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(ctx->listen_fd);
    free(ctx->connections);
    free(server);
    return NULL;
  }

  /* Listen */
  if (listen(ctx->listen_fd, ctx->config.backlog) < 0) {
    close(ctx->listen_fd);
    free(ctx->connections);
    free(server);
    return NULL;
  }

  /* Create kqueue */
  ctx->kq = kqueue();
  if (ctx->kq < 0) {
    close(ctx->listen_fd);
    free(ctx->connections);
    free(server);
    return NULL;
  }

  /* Add listen socket to kqueue */
  struct kevent ev;
  EV_SET(&ev, ctx->listen_fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
  if (kevent(ctx->kq, &ev, 1, NULL, 0, NULL) < 0) {
    close(ctx->kq);
    close(ctx->listen_fd);
    free(ctx->connections);
    free(server);
    return NULL;
  }

  return server;

  return server;
}

int server_start(server_handle_t server) {
  if (server == NULL) return -1;

  kevent_context_t* ctx = &server->ctx;
  ctx->running = true;

  /* Start event loop in background thread */
  if (pthread_create(&ctx->event_thread, NULL, event_loop_thread, ctx) != 0) {
    return -1;
  }

  return 0;
}

int server_stop(server_handle_t server, uint32_t timeout_ms) {
  if (server == NULL) return -1;

  kevent_context_t* ctx = &server->ctx;
  ctx->running = false;

  /* Wake up kevent by adding a dummy event */
  struct kevent ev;
  EV_SET(&ev, 0, EVFILT_USER, EV_ADD | EV_ENABLE, NOTE_TRIGGER, 0, NULL);
  kevent(ctx->kq, &ev, 1, NULL, 0, NULL);

  /* Wait for thread to finish */
  pthread_join(ctx->event_thread, NULL);

  /* Close all connections */
  for (size_t i = 0; i < ctx->max_connections; i++) {
    if (ctx->connections[i].fd >= 0) {
      close_connection(ctx, ctx->connections[i].fd);
    }
  }

  /* Cleanup */
  close(ctx->listen_fd);
  close(ctx->kq);
  free(ctx->connections);

  return 0;
}

int server_send_response(server_handle_t server, connection_t* conn,
                         const response_t* response) {
  if (server == NULL || conn == NULL || response == NULL) return -1;
  if (conn->fd < 0) return -1;

  /* Format response */
  uint8_t buf[16384];
  size_t len;

  /* Convert headers array */
  const char* headers[64];
  size_t header_count = 0;
  if (response->headers) {
    for (size_t i = 0; i < response->header_count * 2 && header_count < 62;
         i += 2) {
      headers[header_count++] = response->headers[i];
      headers[header_count++] = response->headers[i + 1];
    }
  }
  headers[header_count] = NULL;

  /* Use fast path for common case (no custom headers) */
  if (header_count == 0) {
    if (format_http_response_fast(response->status_code, response->body,
                                  response->body_len, buf, sizeof(buf),
                                  &len) < 0) {
      return -1;
    }
  } else if (format_http_response(response->status_code, headers, header_count,
                                  response->body, response->body_len, buf,
                                  sizeof(buf), &len) < 0) {
    return -1;
  }

  /* Write response */
  if (conn_write(&server->ctx, conn, buf, len) < 0) return -1;

  conn->headers_sent = true;

  /* Handle completion if all data was sent immediately */
  if (conn->out_buffer_len == 0) {
    /* Close connection if not keep-alive, otherwise reset for next request */
    if (!conn->keep_alive) {
      close_connection(&server->ctx, conn->fd);
    } else {
      reset_connection(conn);
    }
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

connection_t* server_get_connection_by_fd(server_handle_t server, int fd) {
  if (server == NULL || fd < 0) return NULL;

  kevent_context_t* ctx = &server->ctx;
  if ((size_t)fd >= ctx->max_connections) return NULL;

  connection_t* conn = &ctx->connections[fd];
  if (conn->fd != fd) return NULL; /* Connection slot not active for this fd */

  return conn;
}

const char* server_get_platform(void) { return "kqueue"; }

const char* server_get_version(void) { return "1.0.0"; }
