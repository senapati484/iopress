/**
 * IOCP Backend Implementation (Windows)
 *
 * Windows backend using I/O Completion Ports for high-performance async I/O.
 *
 * @file server_iocp.c
 */

#include "server.h"

#ifdef _WIN32

/* Ensure these are defined before ANY Windows headers */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

/* Correct include order per Microsoft guidelines:
 *   winsock2.h must precede windows.h to avoid redefinition conflicts.
 *   mswsock.h depends on winsock2 types so it comes last.
 * clang-format guards prevent the alphabetical sorter from reordering these. */
#pragma warning(push)
#pragma warning(disable : 4201 4214 4115 4996 4242 4244)
// clang-format off
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <mswsock.h>
// clang-format on
#pragma warning(pop)

#include <stdio.h>
#include <stdlib.h>

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
  HANDLE accept_thread;

  server_config_t config;
  request_callback_t on_request;
  connection_callback_t on_connection;
  void* user_data;

  connection_t* connections;
  size_t max_connections;
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

  if (conn->assembled_body) {
    free(conn->assembled_body);
    conn->assembled_body = NULL;
    conn->assembled_body_len = 0;
  }

  /* Reset output buffer state but keep allocation for reuse */
  conn->out_buffer_len = 0;
  conn->out_buffer_pos = 0;
}

static void close_connection(iocp_context_t* ctx, SOCKET fd) {
  if (fd == INVALID_SOCKET) return;

  connection_t* conn = &ctx->connections[fd];
  if (conn->fd != INVALID_SOCKET) {
    /* CRITICAL: set fd to INVALID_SOCKET BEFORE closesocket().
     * If we close the socket first, the kernel may reuse the freed
     * handle via accept() on another thread before we update conn->fd.
     * A concurrent OP_SEND cancellation that reads conn->fd (still the
     * old value) would close the NEW connection. */
    conn->fd = INVALID_SOCKET;
    conn->closed = true;

    if (ctx->on_connection) {
      ctx->on_connection(conn, 1, ctx->user_data);
    }

    closesocket(fd);

    if (conn->buffer) {
      free(conn->buffer);
      conn->buffer = NULL;
    }
    conn->buffer_cap = 0;

    if (conn->assembled_body) {
      free(conn->assembled_body);
      conn->assembled_body = NULL;
      conn->assembled_body_len = 0;
    }

    /* Free output buffer */
    if (conn->out_buffer) {
      free(conn->out_buffer);
      conn->out_buffer = NULL;
    }
    conn->out_buffer_cap = 0;
    conn->out_buffer_len = 0;
    conn->out_buffer_pos = 0;

    /* NOTE: platform_sqe_data_recv is NOT freed here — a WSARecv may still
     * be pending on the IOCP. closesocket() above cancels all pending I/O;
     * the cancelled completion will arrive on the worker thread, which
     * detects conn->fd==INVALID_SOCKET and frees the overlapped there.
     * platform_sqe_data_send is always dynamically allocated per send and
     * freed in the OP_SEND handler — no cleanup needed here either. */
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
        /* Dynamic overlapped types must be freed here.
         * OP_RECV uses pre-allocated per-connection overlapped, but on
         * cancellation (i.e. close_connection ran from above) the
         * conn-side pointer was already nulled in the connection cleanup
         * — still safe to free the overlapped itself. */
        if (ov->type == OP_RECV && ov->conn) {
          ov->conn->platform_sqe_data_recv = NULL;
        }
        if (ov->type == OP_ACCEPT || ov->type == OP_SEND ||
            ov->type == OP_RECV) {
          free(ov);
        }
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
              next_ov->wsabuf.buf = conn->buffer;
              next_ov->wsabuf.len = (ULONG)conn->buffer_cap;

              DWORD received = 0;
              DWORD flags = 0;
              WSARecv(client, &next_ov->wsabuf, 1, &received, &flags,
                      &next_ov->overlapped, NULL);
            }
          } else {
            closesocket(client);
          }
        }
        /* Per-accept overlapped was heap-allocated; must free */
        free(ov);
        break;
      }

      case OP_RECV: {
        connection_t* conn = (connection_t*)key;
        if (bytes_transferred == 0) {
          if (conn->fd == (int)INVALID_SOCKET) {
            /* Connection was already closed externally (e.g. server_stop).
             * Clean up the in-flight recv overlapped. */
            if (conn->platform_sqe_data_recv) {
              free(conn->platform_sqe_data_recv);
              conn->platform_sqe_data_recv = NULL;
            }
          } else {
            close_connection(ctx, (SOCKET)conn->fd);
            /* close_connection no longer frees platform_sqe_data_recv
             * (may still be in-flight from a prior WSARecv). Now that
             * the cancelled completion has arrived, free it. */
            if (conn->platform_sqe_data_recv) {
              free(conn->platform_sqe_data_recv);
              conn->platform_sqe_data_recv = NULL;
            }
          }
          continue;
        }

        /* Clear the recv_armed flag — a new RECV completion arrived.
         * This allows the OP_RECV handler to re-arm RECV below when
         * not processing (fast path / NEED_MORE), while preventing a
         * double re-arm when server_resume_read already handled it
         * (slow path). */
        conn->recv_armed = false;

        /* Accumulate data across multiple reads.
         * WSARecv writes at conn->buffer + conn->buffer_len (set during
         * the previous RECV re-arm). For the first read buffer_len is 0,
         * so data lands at offset 0. For NEED_MORE continuations, data
         * lands after the previously accumulated bytes. */
        conn->buffer_len += bytes_transferred;
        /* Grow buffer if needed for the next read. We check capacity
         * after updating buffer_len so the next RECV re-arm below has
         * room for at least one more chunk. */
        size_t slack = conn->buffer_cap - conn->buffer_len;
        if (slack < DEFAULT_INITIAL_BUFFER_SIZE) {
          /* 1.5x growth capped at 64KB. Above 64KB most requests have
           * already finished or we should be streaming; 1.5x gives
           * ~25% average slack vs 2x's ~50%. The while-loop guarantees
           * the new cap is at least large enough for the next read. */
          size_t new_cap = conn->buffer_cap == 0
                               ? DEFAULT_INITIAL_BUFFER_SIZE
                               : conn->buffer_cap + conn->buffer_cap / 2;
          if (new_cap > 65536) new_cap = 65536;
          while (new_cap < conn->buffer_len + DEFAULT_INITIAL_BUFFER_SIZE)
            new_cap += new_cap / 2;
          uint8_t* new_buf = realloc(conn->buffer, new_cap);
          if (!new_buf) {
            close_connection(ctx, (SOCKET)conn->fd);
            continue;
          }
          conn->buffer = new_buf;
          conn->buffer_cap = new_cap;
        }

        parse_result_t result;
        int status = http_parse_request((const uint8_t*)conn->buffer,
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
            /* Set processing=true to prevent buffer overwrite while
             * JS reads header pointers that reference conn->buffer.
             * server_send_response will call server_resume_read to
             * re-arm RECV when JS is done. */
            conn->processing = true;
            ctx->on_request(conn, &result, ctx->user_data);
          }

          /* Reset buffer for the next request (done/error handled) */
          conn->buffer_len = 0;
        } else if (status == PARSE_STATUS_ERROR) {
          conn->buffer_len = 0;
          const char* headers[] = {"Connection", "close", NULL};
          uint8_t response[256];
          size_t response_len;
          format_http_response(400, headers, 2, NULL, 0, response,
                               sizeof(response), &response_len);
          server_write(NULL, conn, response, response_len);
          close_connection(ctx, (SOCKET)conn->fd);
          continue;
        }
        /* PARSE_STATUS_NEED_MORE: keep buffer_len as-is for accumulation. */

        /* Re-arm RECV only if:
         *   1. JS is not processing the request (processing=false), AND
         *   2. server_resume_read has not already re-armed RECV
         * (recv_armed=false).
         *
         * When processing=true, server_resume_read (called from
         * server_send_response) will re-arm after the JS handler completes
         * and sets recv_armed=true, preventing a double re-arm here.
         * For the fast path (no JS) and NEED_MORE, we re-arm here since
         * recv_armed was cleared at the start of this handler. */
        if (!conn->processing && !conn->recv_armed) {
          memset(&ov->overlapped, 0, sizeof(OVERLAPPED));
          ov->wsabuf.buf = conn->buffer + conn->buffer_len;
          ov->wsabuf.len = (ULONG)(conn->buffer_cap - conn->buffer_len);

          DWORD received = 0;
          DWORD flags = 0;
          if (WSARecv((SOCKET)conn->fd, &ov->wsabuf, 1, &received, &flags,
                      &ov->overlapped, NULL) == SOCKET_ERROR &&
              WSAGetLastError() != WSA_IO_PENDING) {
            close_connection(ctx, (SOCKET)conn->fd);
          }
        }
        break;
      }

      case OP_SEND: {
        /* Send completion - cleanup dynamic SEND object */
        connection_t* conn = (connection_t*)key;
        if (bytes_transferred > 0) {
          conn->out_buffer_pos += bytes_transferred;
          if (conn->out_buffer_pos < conn->out_buffer_len) {
            /* Queue the next chunk of the buffer */
            iocp_overlapped_t* next_ov = malloc(sizeof(iocp_overlapped_t));
            if (next_ov) {
              memset(next_ov, 0, sizeof(iocp_overlapped_t));
              next_ov->type = OP_SEND;
              next_ov->conn = conn;

              size_t to_send =
                  (conn->out_buffer_len - conn->out_buffer_pos) > BUFFER_SIZE
                      ? BUFFER_SIZE
                      : (conn->out_buffer_len - conn->out_buffer_pos);
              memcpy(next_ov->buffer, conn->out_buffer + conn->out_buffer_pos,
                     to_send);
              next_ov->wsabuf.buf = next_ov->buffer;
              next_ov->wsabuf.len = (ULONG)to_send;

              DWORD sent = 0;
              if (WSASend((SOCKET)conn->fd, &next_ov->wsabuf, 1, &sent, 0,
                          &next_ov->overlapped, NULL) == SOCKET_ERROR) {
                if (WSAGetLastError() != WSA_IO_PENDING) {
                  free(next_ov);
                  close_connection(ctx, (SOCKET)conn->fd);
                }
              }
            } else {
              close_connection(ctx, (SOCKET)conn->fd);
            }
          } else {
            /* Completed sending all buffered data.
             * Do NOT reset connection state here — OP_RECV may already be
             * processing the next pipelined request. OP_RECV lazily cleans
             * stale state before parsing the next request. */
            conn->out_buffer_pos = 0;
            conn->out_buffer_len = 0;
            if (!conn->keep_alive && conn->headers_sent) {
              close_connection(ctx, (SOCKET)conn->fd);
            }
          }
        } else {
          /* Error or connection closed */
          close_connection(ctx, (SOCKET)conn->fd);
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

static DWORD WINAPI accept_thread_func(LPVOID param) {
  iocp_context_t* ctx = (iocp_context_t*)param;
  while (ctx->running) {
    SOCKET client = accept(ctx->listen_socket, NULL, NULL);
    if (client != INVALID_SOCKET) {
      /* Allocate per-accept overlapped to avoid race with worker thread.
       * The worker's OP_ACCEPT handler frees it after processing. */
      iocp_overlapped_t* ov = malloc(sizeof(iocp_overlapped_t));
      if (ov) {
        memset(ov, 0, sizeof(iocp_overlapped_t));
        ov->type = OP_ACCEPT;
        ov->socket = client;
        PostQueuedCompletionStatus(ctx->iocp, 0, 0, &ov->overlapped);
      } else {
        closesocket(client);
      }
    }
  }
  return 0;
}

int server_start(server_handle_t server) {
  iocp_context_t* ctx = &server->ctx;

  CreateIoCompletionPort((HANDLE)ctx->listen_socket, ctx->iocp, 0, 0);

  ctx->running = true;

  for (int i = 0; i < WORKER_THREADS; i++) {
    ctx->worker_threads[i] = CreateThread(NULL, 0, worker_thread, ctx, 0, NULL);
  }

  /* Spawn accept loop in a dedicated background thread to prevent blocking V8
   */
  ctx->accept_thread = CreateThread(NULL, 0, accept_thread_func, ctx, 0, NULL);

  return 0;
}

int server_stop(server_handle_t server, uint32_t timeout_ms) {
  (void)timeout_ms;

  iocp_context_t* ctx = &server->ctx;
  ctx->running = false;

  /* First close the listen socket so that the blocking accept() call in
   * accept_thread exits.
   * NOTE: INVALID_SOCKET on Windows is ~(SOCKET)0 (non-zero), so the
   * equality check is required — truthiness alone does not work. */
  if (ctx->listen_socket != INVALID_SOCKET) {
    closesocket(ctx->listen_socket);
    ctx->listen_socket = INVALID_SOCKET;
  }

  /* Wait for and close accept thread */
  if (ctx->accept_thread) {
    WaitForSingleObject(ctx->accept_thread, INFINITE);
    CloseHandle(ctx->accept_thread);
    ctx->accept_thread = NULL;
  }

  /* Signal and wait for worker threads.
   * Post ALL wakeups FIRST, then wait for each thread in a second loop.
   * PostQueuedCompletionStatus wakes ANY thread from GQCS, so a sequential
   * post-and-wait-per-thread loop would deadlock — thread T might consume
   * the wakeup intended for thread S, while S stays blocked on GQCS. */
  for (int i = 0; i < WORKER_THREADS; i++) {
    if (ctx->worker_threads[i]) {
      PostQueuedCompletionStatus(ctx->iocp, 0, 0, NULL);
    }
  }
  for (int i = 0; i < WORKER_THREADS; i++) {
    if (ctx->worker_threads[i]) {
      WaitForSingleObject(ctx->worker_threads[i], INFINITE);
      CloseHandle(ctx->worker_threads[i]);
    }
  }

  if (ctx->iocp) {
    CloseHandle(ctx->iocp);
  }

  if (ctx->connections) {
    for (size_t i = 0; i < ctx->max_connections; i++) {
      if (ctx->connections[i].fd != (int)INVALID_SOCKET) {
        close_connection(ctx, (SOCKET)ctx->connections[i].fd);
      }
    }
    free(ctx->connections);
  }

  WSACleanup();

  return 0;
}

int server_send_response(server_handle_t server, connection_t* conn,
                         const response_t* response) {
  (void)server;
  if (!conn || !response) return -1;

  /* 1. Format status line + headers + Content-Length into stack buffer */
  int status_code = response->status_code;
  char head[4096];
  size_t hlen = 0;

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

  int len = snprintf(head + hlen, sizeof(head) - hlen, "HTTP/1.1 %d %s\r\n",
                     status_code, status_text);
  if (len < 0) return -1;
  hlen += (size_t)len;

  if (response->headers) {
    size_t count = response->header_count;
    if (count > 64) count = 64;
    for (size_t i = 0; i < count * 2 && response->headers[i]; i += 2) {
      len = snprintf(head + hlen, sizeof(head) - hlen, "%s: %s\r\n",
                     response->headers[i], response->headers[i + 1]);
      if (len < 0) return -1;
      hlen += (size_t)len;
    }
  }

  len = snprintf(head + hlen, sizeof(head) - hlen,
                 "Content-Length: %zu\r\n\r\n", response->body_len);
  if (len < 0) return -1;
  hlen += (size_t)len;

  /* 2. Try single WSASend: copy headers + body into one send buffer.
   * Saves 1 malloc + 1 memcpy + 1 send-queue traversal per response vs
   * the previous headers-then-body double server_write path. */
  iocp_overlapped_t* ov = (iocp_overlapped_t*)malloc(sizeof(iocp_overlapped_t));
  if (!ov) goto fallback;
  memset(ov, 0, sizeof(iocp_overlapped_t));
  ov->type = OP_SEND;
  ov->conn = conn;

  size_t total = hlen;
  if (total > BUFFER_SIZE) {
    free(ov);
    goto fallback;
  }
  memcpy(ov->buffer, head, total);

  if (response->body && response->body_len > 0 &&
      total + response->body_len <= BUFFER_SIZE) {
    memcpy(ov->buffer + total, response->body, response->body_len);
    total += response->body_len;
  }

  ov->wsabuf.buf = ov->buffer;
  ov->wsabuf.len = (ULONG)total;

  DWORD sent = 0;
  if (WSASend((SOCKET)conn->fd, &ov->wsabuf, 1, &sent, 0, &ov->overlapped,
              NULL) == SOCKET_ERROR &&
      WSAGetLastError() != WSA_IO_PENDING) {
    free(ov);
    return -1;
  }

  conn->headers_sent = true;

  /* If body didn't fit in single send, send remainder separately */
  if (response->body && response->body_len > 0 &&
      total < hlen + response->body_len) {
    size_t already = total - hlen;
    server_write(server, conn, response->body + already,
                 response->body_len - already);
  }

  goto done;

fallback:
  if (server_write(server, conn, head, hlen) < 0) return -1;
  if (response->body && response->body_len > 0)
    if (server_write(server, conn, response->body, response->body_len) < 0)
      return -1;
  conn->headers_sent = true;

done:
  /* Re-arm RECV for pipelining (deferred by setting processing=true
   * in the OP_RECV handler). Safe even if data is still buffered for send
   * — recv uses conn->buffer, send uses conn->out_buffer.
   * Sets recv_armed=true to prevent a double re-arm in the OP_RECV
   * handler's fallthrough code. */
  if (conn->keep_alive && conn->fd != (int)INVALID_SOCKET && !conn->closed) {
    server_resume_read(server, conn);
  }
  return 0;
}

int server_write(server_handle_t server, connection_t* conn, const void* data,
                 size_t len) {
  if (conn == NULL || data == NULL || len == 0) return -1;

  /* 1. Append to output buffer */
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

  /* 2. Arm send if this is the only data */
  if (conn->out_buffer_len == len) {
    iocp_overlapped_t* ov = malloc(sizeof(iocp_overlapped_t));
    if (!ov) return -1;

    memset(ov, 0, sizeof(iocp_overlapped_t));
    ov->type = OP_SEND;
    ov->conn = conn;

    size_t to_send = (conn->out_buffer_len - conn->out_buffer_pos) > BUFFER_SIZE
                         ? BUFFER_SIZE
                         : (conn->out_buffer_len - conn->out_buffer_pos);
    memcpy(ov->buffer, conn->out_buffer + conn->out_buffer_pos, to_send);
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
  }

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

int server_pause_read(server_handle_t server, connection_t* conn) {
  if (!server || !conn || conn->fd == (int)INVALID_SOCKET) return -1;
  conn->processing = true;
  return 0;
}

int server_resume_read(server_handle_t server, connection_t* conn) {
  if (!server || !conn || conn->fd == (int)INVALID_SOCKET) return -1;
  conn->processing = false;
  /* Re-arm RECV immediately when resuming.
   * Use conn->buffer + conn->buffer_len as the read target so we don't
   * overwrite any NEED_MORE data accumulated from a prior partial read. */
  iocp_overlapped_t* ov = (iocp_overlapped_t*)conn->platform_sqe_data_recv;
  if (ov) {
    memset(&ov->overlapped, 0, sizeof(OVERLAPPED));
    ov->wsabuf.buf = conn->buffer + conn->buffer_len;
    ov->wsabuf.len = (ULONG)(conn->buffer_cap - conn->buffer_len);
    DWORD received = 0;
    DWORD flags = 0;
    if (WSARecv((SOCKET)conn->fd, &ov->wsabuf, 1, &received, &flags,
                &ov->overlapped, NULL) == SOCKET_ERROR &&
        WSAGetLastError() != WSA_IO_PENDING) {
      close_connection((iocp_context_t*)server, (SOCKET)conn->fd);
      return -1;
    }
    conn->recv_armed = true;
  }
  return 0;
}

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

const char* server_get_version(void) { return "1.0.5"; }

#endif
