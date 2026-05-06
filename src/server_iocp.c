/**
 * IOCP Backend Implementation (Windows)
 *
 * Windows backend using I/O Completion Ports for high-performance async I/O.
 *
 * @file server_iocp.c
 */

#include "server.h"

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <mswsock.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "fast_router.h"
#include "parser.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

/* ============================================================================
 * Constants
 * ============================================================================
 */

#define BUFFER_SIZE 8192
#define MAX_PIPELINE_BATCH 64
#define MAX_ACCEPT_BATCH 512
#define WORKER_THREADS 4

/* Operation types for IOCP */
typedef enum { OP_ACCEPT, OP_RECV, OP_SEND } op_type_t;

/* ============================================================================
 * Types
 * ============================================================================
 */

typedef struct {
  OVERLAPPED overlapped;
  SOCKET socket;
  WSABUF wsabuf;
  char buffer[BUFFER_SIZE];
  DWORD bytes_transferred;
  DWORD flags;
  op_type_t type;
  connection_t* conn;
} iocp_overlapped_t;

typedef struct {
  HANDLE iocp;
  SOCKET listen_socket;
  bool running;
  HANDLE worker_threads[WORKER_THREADS];

  server_config_t config;
  request_callback_t on_request;
  connection_callback_t on_connection;
  void* user_data;

  connection_t* connections;
  size_t max_connections;
  CRITICAL_SECTION cs;

  /* SQE data recycling to avoid malloc in hot path */
  iocp_overlapped_t accept_ov;
} iocp_context_t;

struct server_handle_s {
  iocp_context_t ctx;
};

/* ============================================================================
 * Socket Utilities
 * ============================================================================
 */

static int set_nonblocking(SOCKET fd) {
  u_long mode = 1;
  return ioctlsocket(fd, FIONBIO, &mode);
}

static int set_reuseport(SOCKET fd) {
  int opt = 1;
  return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt,
                    sizeof(opt));
}

static int set_reuseaddr(SOCKET fd) {
  int opt = 1;
  return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt,
                    sizeof(opt));
}

static int set_nodelay(SOCKET fd) {
  int opt = 1;
  return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt,
                    sizeof(opt));
}

static int set_socket_options(SOCKET fd) {
  set_reuseaddr(fd);
  set_reuseport(fd);
  set_nodelay(fd);

  int sndbuf = 1048576;
  int rcvbuf = 1048576;
  setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const char*)&sndbuf, sizeof(sndbuf));
  setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvbuf, sizeof(rcvbuf));

  return 0;
}

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
    len = snprintf(p, (int)(out_cap - (p - (char*)out)), "%s: %s\r\n",
                   headers[i], headers[i + 1]);
    if (len < 0) return -1;
    p += len;
  }

  len = snprintf(p, (int)(out_cap - (p - (char*)out)),
                 "Content-Length: %zu\r\n\r\n", body_len);
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
 * Connection Management
 * ============================================================================
 */

static connection_t* get_connection(connection_t* pool, size_t pool_size,
                                    SOCKET fd) {
  if (fd == INVALID_SOCKET || (size_t)fd >= pool_size) return NULL;
  connection_t* conn = &pool[fd];
  if (conn->fd == INVALID_SOCKET) {
    memset(conn, 0, sizeof(*conn));
    conn->fd = (int)fd;
    conn->buffer_cap = DEFAULT_INITIAL_BUFFER_SIZE;
    conn->buffer = malloc(conn->buffer_cap);
    if (conn->buffer == NULL) return NULL;

    /* Pre-allocate overlapped objects to avoid malloc in hot path */
    iocp_overlapped_t* recv_ov = malloc(sizeof(iocp_overlapped_t));
    if (recv_ov) {
      memset(recv_ov, 0, sizeof(iocp_overlapped_t));
      recv_ov->type = OP_RECV;
      recv_ov->conn = conn;
      recv_ov->socket = fd;
      conn->platform_sqe_data_recv = recv_ov;
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

static void close_connection(iocp_context_t* ctx, SOCKET fd) {
  if (fd == INVALID_SOCKET) return;

  connection_t* conn = &ctx->connections[fd];
  if (conn->fd != INVALID_SOCKET) {
    if (ctx->on_connection) {
      ctx->on_connection(conn, 1, ctx->user_data);
    }

    closesocket(fd);
    conn->fd = INVALID_SOCKET;
    conn->closed = true;

    if (conn->buffer) {
      free(conn->buffer);
      conn->buffer = NULL;
    }
    conn->buffer_cap = 0;

    /* Free output buffer */
    if (conn->out_buffer) {
      free(conn->out_buffer);
      conn->out_buffer = NULL;
    }
    conn->out_buffer_cap = 0;
    conn->out_buffer_len = 0;
    conn->out_buffer_pos = 0;

    /* Cleanup platform_sqe_data */
    if (conn->platform_sqe_data_recv) {
      free(conn->platform_sqe_data_recv);
      conn->platform_sqe_data_recv = NULL;
    }
    if (conn->platform_sqe_data_send) {
      /* SEND objects are usually dynamically allocated per write in IOCP
       * because multiple sends can be outstanding. We cleanup any
       * remaining one if it was somehow cached. */
      free(conn->platform_sqe_data_send);
      conn->platform_sqe_data_send = NULL;
    }
  }
}

/* ============================================================================
 * Worker Thread
 * ============================================================================
 */

static DWORD WINAPI worker_thread(LPVOID param) {
  iocp_context_t* ctx = (iocp_context_t*)param;
  OVERLAPPED* overlap = NULL;
  DWORD bytes_transferred = 0;
  ULONG_PTR key = 0;

  while (ctx->running) {
    BOOL ret = GetQueuedCompletionStatus(ctx->iocp, &bytes_transferred, &key,
                                         &overlap, INFINITE);

    if (!ret) {
      if (overlap) {
        iocp_overlapped_t* ov = (iocp_overlapped_t*)overlap;
        if (ov->conn && ov->conn->fd != INVALID_SOCKET) {
          close_connection(ctx, (SOCKET)ov->conn->fd);
        }
        if (ov->type == OP_SEND) free(ov);
      }
      continue;
    }

    if (overlap == NULL) continue;
    iocp_overlapped_t* ov = (iocp_overlapped_t*)overlap;

    switch (ov->type) {
      case OP_ACCEPT: {
        SOCKET client = (SOCKET)ov->socket;
        if (client != INVALID_SOCKET) {
          set_socket_options(client);
          set_nonblocking(client);

          connection_t* conn =
              get_connection(ctx->connections, ctx->max_connections, client);
          if (conn) {
            conn->fd = (int)client;
            if (ctx->on_connection) {
              ctx->on_connection(conn, 0, ctx->user_data);
            }

            CreateIoCompletionPort((HANDLE)client, ctx->iocp, (ULONG_PTR)conn,
                                   0);

            /* Use pre-allocated RECV object */
            iocp_overlapped_t* next_ov =
                (iocp_overlapped_t*)conn->platform_sqe_data_recv;
            if (next_ov) {
              memset(&next_ov->overlapped, 0, sizeof(OVERLAPPED));
              next_ov->wsabuf.buf = next_ov->buffer;
              next_ov->wsabuf.len = BUFFER_SIZE;

              DWORD received = 0;
              DWORD flags = 0;
              WSARecv(client, &next_ov->wsabuf, 1, &received, &flags,
                      &next_ov->overlapped, NULL);
            }
          } else {
            closesocket(client);
          }
        }
        /* ACCEPT object is static in context, don't free */
        break;
      }

      case OP_RECV: {
        connection_t* conn = (connection_t*)key;
        if (bytes_transferred == 0) {
          close_connection(ctx, (SOCKET)conn->fd);
          continue;
        }

        conn->buffer_len = bytes_transferred;

        parse_result_t result;
        int status = http_parse_request((const uint8_t*)ov->buffer,
                                        conn->buffer_len, &result);

        if (status == PARSE_STATUS_DONE) {
          conn->request_complete = true;
          conn->keep_alive = result.http_minor >= 1;

          /* ULTRA FAST PATH: Check dynamic fast router first */
          uint8_t* fast_response = NULL;
          size_t fast_response_len = 0;

          int fast_handled = fast_router_try_handle_full(
              result.method, result.method_len, result.path, result.path_len,
              &fast_response, &fast_response_len);

          if (fast_handled == 0 && fast_response != NULL &&
              fast_response_len > 0) {
            conn->headers_sent = true;
            server_write(NULL, conn, fast_response, fast_response_len);
          } else if (ctx->on_request) {
            ctx->on_request(conn, &result, ctx->user_data);
          }
        } else if (status == PARSE_STATUS_ERROR) {
          const char* headers[] = {"Connection", "close", NULL};
          uint8_t response[256];
          size_t response_len;
          format_http_response(400, headers, 2, NULL, 0, response,
                               sizeof(response), &response_len);
          server_write(NULL, conn, response, response_len);
          close_connection(ctx, (SOCKET)conn->fd);
          continue;
        }

        /* Re-arm RECV using the same object */
        memset(&ov->overlapped, 0, sizeof(OVERLAPPED));
        ov->wsabuf.buf = ov->buffer;
        ov->wsabuf.len = BUFFER_SIZE;

        DWORD received = 0;
        DWORD flags = 0;
        WSARecv((SOCKET)conn->fd, &ov->wsabuf, 1, &received, &flags,
                &ov->overlapped, NULL);
        break;
      }

      case OP_SEND: {
        /* Send completion - cleanup dynamic SEND object */
        connection_t* conn = (connection_t*)key;
        if (bytes_transferred > 0) {
          /* Handle completion/reset if needed */
          if (conn->request_complete && conn->headers_sent &&
              !conn->keep_alive) {
            close_connection(ctx, (SOCKET)conn->fd);
          }
        }
        free(ov);
        break;
      }
    }
  }

  return 0;
}

/* ============================================================================
 * Server Interface Implementation
 * ============================================================================
 */

server_handle_t server_init(const server_config_t* config,
                            request_callback_t on_request,
                            connection_callback_t on_connection,
                            void* user_data) {
  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    return NULL;
  }

  server_handle_t server = calloc(1, sizeof(*server));
  if (!server) {
    WSACleanup();
    return NULL;
  }

  /* Initialize fast router */
  fast_router_init();
  fast_router_register_defaults();

  iocp_context_t* ctx = &server->ctx;

  ctx->config = *config;
  ctx->on_request = on_request;
  ctx->on_connection = on_connection;
  ctx->user_data = user_data;
  ctx->max_connections = MAX_CONNECTIONS;
  ctx->running = false;

  ctx->connections = calloc(ctx->max_connections, sizeof(connection_t));
  if (!ctx->connections) {
    free(server);
    WSACleanup();
    return NULL;
  }

  for (size_t i = 0; i < ctx->max_connections; i++) {
    ctx->connections[i].fd = (int)INVALID_SOCKET;
  }

  InitializeCriticalSection(&ctx->cs);

  ctx->listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (ctx->listen_socket == INVALID_SOCKET) {
    free(ctx->connections);
    free(server);
    WSACleanup();
    return NULL;
  }

  set_socket_options(ctx->listen_socket);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(config->port);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(ctx->listen_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    closesocket(ctx->listen_socket);
    free(ctx->connections);
    free(server);
    WSACleanup();
    return NULL;
  }

  listen(ctx->listen_socket, 511);

  ctx->iocp =
      CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, WORKER_THREADS);
  if (!ctx->iocp) {
    closesocket(ctx->listen_socket);
    free(ctx->connections);
    free(server);
    WSACleanup();
    return NULL;
  }

  /* Init static accept data */
  memset(&ctx->accept_ov, 0, sizeof(iocp_overlapped_t));
  ctx->accept_ov.type = OP_ACCEPT;

  return server;
}

int server_start(server_handle_t server) {
  iocp_context_t* ctx = &server->ctx;

  CreateIoCompletionPort((HANDLE)ctx->listen_socket, ctx->iocp, 0, 0);

  ctx->running = true;

  for (int i = 0; i < WORKER_THREADS; i++) {
    ctx->worker_threads[i] = CreateThread(NULL, 0, worker_thread, ctx, 0, NULL);
  }

  while (ctx->running) {
    SOCKET client = accept(ctx->listen_socket, NULL, NULL);
    if (client != INVALID_SOCKET) {
      iocp_overlapped_t* ov = &ctx->accept_ov;
      ov->socket = client;
      PostQueuedCompletionStatus(ctx->iocp, 0, 0, &ov->overlapped);
    }
    Sleep(1);
  }

  return 0;
}

int server_stop(server_handle_t server, uint32_t timeout_ms) {
  (void)timeout_ms;

  iocp_context_t* ctx = &server->ctx;
  ctx->running = false;

  for (int i = 0; i < WORKER_THREADS; i++) {
    if (ctx->worker_threads[i]) {
      PostQueuedCompletionStatus(ctx->iocp, 0, 0, NULL);
      WaitForSingleObject(ctx->worker_threads[i], INFINITE);
      CloseHandle(ctx->worker_threads[i]);
    }
  }

  if (ctx->iocp) {
    CloseHandle(ctx->iocp);
  }

  if (ctx->listen_socket) {
    closesocket(ctx->listen_socket);
  }

  if (ctx->connections) {
    for (size_t i = 0; i < ctx->max_connections; i++) {
      if (ctx->connections[i].fd != (int)INVALID_SOCKET) {
        close_connection(ctx, (SOCKET)ctx->connections[i].fd);
      }
    }
    free(ctx->connections);
  }

  DeleteCriticalSection(&ctx->cs);
  WSACleanup();

  return 0;
}

int server_send_response(server_handle_t server, connection_t* conn,
                         const response_t* response) {
  if (conn == NULL || response == NULL) return -1;

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
  if (conn == NULL || data == NULL || len == 0) return -1;

  iocp_overlapped_t* ov = malloc(sizeof(iocp_overlapped_t));
  if (!ov) return -1;

  memset(ov, 0, sizeof(iocp_overlapped_t));
  ov->type = OP_SEND;
  ov->conn = conn;

  size_t to_send = len > BUFFER_SIZE ? BUFFER_SIZE : len;
  memcpy(ov->buffer, data, to_send);
  ov->wsabuf.buf = ov->buffer;
  ov->wsabuf.len = (ULONG)to_send;

  DWORD sent = 0;
  if (WSASend((SOCKET)conn->fd, &ov->wsabuf, 1, &sent, 0, &ov->overlapped,
              NULL) == SOCKET_ERROR) {
    if (WSAGetLastError() != WSA_IO_PENDING) {
      free(ov);
      return -1;
    }
  }

  /* If there's more data, we should handle it.
   * For now, this implementation is simplified.
   * In a real high-perf scenario, we'd queue the remainder. */

  return 0;
}

ssize_t server_read_body(server_handle_t server, connection_t* conn,
                         size_t max_bytes) {
  (void)server;
  if (conn == NULL || conn->fd == (int)INVALID_SOCKET) return -1;

  /* Simplified sync read for body if needed outside of async loop */
  char* buf = (char*)conn->buffer + conn->buffer_len;
  size_t space = conn->buffer_cap - conn->buffer_len;
  if (max_bytes > 0 && max_bytes < space) space = max_bytes;

  int n = recv((SOCKET)conn->fd, buf, (int)space, 0);
  if (n > 0) conn->buffer_len += n;
  return n;
}

connection_t* server_get_connection_by_fd(server_handle_t server, int fd) {
  if (server == NULL || fd == (int)INVALID_SOCKET || fd < 0 ||
      (size_t)fd >= server->ctx.max_connections) {
    return NULL;
  }
  return get_connection(server->ctx.connections, server->ctx.max_connections,
                        (SOCKET)fd);
}

const char* server_get_platform(void) { return "iocp"; }

const char* server_get_version(void) { return "1.1.0"; }

#else

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
  return 0;
}

int server_stop(server_handle_t server, uint32_t timeout_ms) {
  (void)server;
  (void)timeout_ms;
  return 0;
}

int server_send_response(server_handle_t server, connection_t* conn,
                         const response_t* response) {
  (void)server;
  (void)conn;
  (void)response;
  return 0;
}

ssize_t server_read_body(server_handle_t server, connection_t* conn,
                         size_t max_bytes) {
  (void)server;
  (void)conn;
  (void)max_bytes;
  return 0;
}

connection_t* server_get_connection_by_fd(server_handle_t server, int fd) {
  (void)server;
  (void)fd;
  return NULL;
}

const char* server_get_platform(void) { return "iocp"; }

const char* server_get_version(void) { return "1.1.0"; }

#endif
