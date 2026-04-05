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

#define MAX_CONNECTIONS 65535
#define BUFFER_SIZE 8192
#define MAX_PIPELINE_BATCH 64
#define MAX_ACCEPT_BATCH 512
#define WORKER_THREADS 4

/* ============================================================================
 * Types
 * ============================================================================
 */

typedef struct {
  SOCKET socket;
  WSABUF wsabuf;
  char buffer[BUFFER_SIZE];
  DWORD bytes_received;
  DWORD flags;
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
 * Pre-computed Responses
 * ============================================================================
 */

static const char RESPONSE_200_JSON[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 2\r\n"
    "Connection: keep-alive\r\n"
    "Content-Type: application/json\r\n"
    "\r\n"
    "{}";

static const char RESPONSE_200_PING[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Length: 4\r\n"
    "Connection: keep-alive\r\n"
    "Content-Type: text/plain\r\n"
    "\r\n"
    "pong";

static const char RESPONSE_404[] =
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Length: 9\r\n"
    "Connection: close\r\n"
    "\r\n"
    "Not Found";

static const char* get_static_response(const char* path, size_t path_len,
                                       size_t* out_len) {
  switch (path_len) {
    case 1:
      if (path[0] == '/') {
        *out_len = sizeof(RESPONSE_200_JSON) - 1;
        return RESPONSE_200_JSON;
      }
      break;
    case 5:
      if (memcmp(path, "/ping", 5) == 0) {
        *out_len = sizeof(RESPONSE_200_PING) - 1;
        return RESPONSE_200_PING;
      }
      break;
    case 7:
      if (memcmp(path, "/health", 7) == 0) {
        *out_len = sizeof(RESPONSE_200_JSON) - 1;
        return RESPONSE_200_JSON;
      }
      break;
  }
  return NULL;
}

/* ============================================================================
 * Connection Management
 * ============================================================================
 */

static connection_t* get_connection(connection_t* pool, size_t pool_size,
                                    SOCKET fd) {
  if (fd < 0 || (size_t)fd >= pool_size) return NULL;
  return &pool[fd];
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
        iocp_overlapped_t* ov =
            CONTAINING_RECORD(overlap, iocp_overlapped_t, wsabuf);
        if (ov->conn) {
          closesocket(ov->conn->fd);
          ov->conn->fd = -1;
        }
      }
      continue;
    }

    if (key == 0) {
      SOCKET client = (SOCKET)overlap;
      if (client != INVALID_SOCKET) {
        set_socket_options(client);
        set_nonblocking(client);

        connection_t* conn =
            get_connection(ctx->connections, ctx->max_connections, client);
        if (conn) {
          conn->fd = client;
          if (ctx->on_connection) {
            ctx->on_connection(conn, 0, ctx->user_data);
          }

          CreateIoCompletionPort((HANDLE)client, ctx->iocp, (ULONG_PTR)conn, 0);

          iocp_overlapped_t* ov = malloc(sizeof(iocp_overlapped_t));
          if (ov) {
            memset(ov, 0, sizeof(iocp_overlapped_t));
            ov->conn = conn;
            ov->wsabuf.buf = ov->buffer;
            ov->wsabuf.len = BUFFER_SIZE;
            ov->flags = 0;

            DWORD received = 0;
            WSARecv(client, &ov->wsabuf, 1, NULL, &ov->flags, &ov->wsabuf,
                    NULL);
          }
        } else {
          closesocket(client);
        }
      }
    } else if (overlap) {
      connection_t* conn = (connection_t*)key;
      iocp_overlapped_t* ov =
          CONTAINING_RECORD(overlap, iocp_overlapped_t, wsabuf);

      if (bytes_transferred == 0) {
        closesocket(conn->fd);
        conn->fd = -1;
        free(ov);
        continue;
      }

      conn->buffer_len = bytes_transferred;

      parse_result_t result;
      int status = http_parse_request((const uint8_t*)ov->buffer,
                                      conn->buffer_len, &result);

      if (status == PARSE_STATUS_DONE) {
        size_t static_len = 0;
        const char* static_resp =
            get_static_response(result.path, result.path_len, &static_len);
        if (static_resp) {
          send(conn->fd, static_resp, static_len, 0);
        } else if (ctx->on_request) {
          ctx->on_request(conn, &result, ctx->user_data);
        }
      } else if (status == PARSE_STATUS_ERROR) {
        send(conn->fd, RESPONSE_404, sizeof(RESPONSE_404) - 1, 0);
        closesocket(conn->fd);
        conn->fd = -1;
        free(ov);
        continue;
      }

      memset(ov, 0, sizeof(iocp_overlapped_t));
      ov->conn = conn;
      ov->wsabuf.buf = ov->buffer;
      ov->wsabuf.len = BUFFER_SIZE;

      WSARecv(conn->fd, &ov->wsabuf, 1, NULL, &ov->flags, &ov->wsabuf, NULL);
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
      PostQueuedCompletionStatus(ctx->iocp, 0, 0, (LPOVERLAPPED)client);
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
      if (ctx->connections[i].fd >= 0) {
        closesocket(ctx->connections[i].fd);
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
  if (fd < 0) return NULL;
  return get_connection(server->ctx.connections, server->ctx.max_connections,
                        fd);
}

const char* server_get_platform(void) { return "iocp"; }

const char* server_get_version(void) { return "1.0.0"; }

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

const char* server_get_version(void) { return "1.0.0"; }

#endif
