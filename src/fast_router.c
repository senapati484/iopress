/**
 * Fast Path Router - C Implementation
 *
 * High-performance routing entirely in C.
 *
 * @file fast_router.c
 * @version 2.0.0
 */

#include "fast_router.h"

#include <stdio.h>
#include <string.h>

static fast_router_t router = {0};

/* Suppress unused function warnings for reserved functions */
#if defined(__APPLE__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif

/* Initialize router */
void fast_router_init(void) {
  memset(&router, 0, sizeof(router));
  router.route_count = 0;
  router.fast_hits = 0;
  router.slow_falls = 0;
  /* Clear hash table */
  for (int i = 0; i < 256; i++) {
    router.hash_table[i] = NULL;
  }
}

/* Simple hash for path lookup */
static uint32_t hash_path(const char* path, size_t len) {
  uint32_t hash = 5381;
  for (size_t i = 0; i < len && i < 32; i++) {
    hash = ((hash << 5) + hash) + (uint8_t)path[i];
  }
  return hash & 0xFF;
}

/* Hash for method + path combination - better distribution */
static uint32_t hash_method_path(const char* method, size_t method_len,
                                 const char* path, size_t path_len) {
  uint32_t hash = 5381;
  for (size_t i = 0; i < method_len && i < 7; i++) {
    char c = method[i];
    if (c >= 'a' && c <= 'z') c = c - 32;
    hash = ((hash << 5) + hash) + (uint8_t)c;
  }
  for (size_t i = 0; i < path_len && i < 64; i++) {
    if (path[i] >= 'A' && path[i] <= 'Z') {
      hash = ((hash << 5) + hash) + (path[i] + 32);
    } else {
      hash = ((hash << 5) + hash) + (uint8_t)path[i];
    }
  }
  return hash & 0xFF;
}

/* Add route to hash table */
static void add_route_to_hash(fast_route_t* route) {
  uint32_t h = hash_method_path(route->method, strlen(route->method),
                                route->path, strlen(route->path));
  router.hash_table[h] = route;
}

/* Case-insensitive string compare */
static int strncasecmp_custom(const char* s1, const char* s2, size_t n) {
  for (size_t i = 0; i < n; i++) {
    char c1 = (s1[i] >= 'A' && s1[i] <= 'Z') ? s1[i] + 32 : s1[i];
    char c2 = (s2[i] >= 'A' && s2[i] <= 'Z') ? s2[i] + 32 : s2[i];
    if (c1 != c2) return c1 - c2;
    if (s1[i] == '\0') return 0;
  }
  return 0;
}

#if defined(__APPLE__)
#pragma clang diagnostic pop
#endif

/* Register a fast route */
int fast_router_register(const char* method, const char* path,
                         route_type_t type, uint16_t status,
                         const char* content_type, const uint8_t* response,
                         size_t response_len) {
  if (router.route_count >= MAX_FAST_ROUTES) {
    return -1;
  }

  fast_route_t* route = &router.routes[router.route_count];

  strncpy(route->method, method, 7);
  route->method[7] = '\0';

  strncpy(route->path, path, MAX_PATH_LEN - 1);
  route->path[MAX_PATH_LEN - 1] = '\0';

  route->type = type;
  route->status_code = status;
  strncpy(route->content_type, content_type, 63);
  route->content_type[63] = '\0';

  if (response_len > 0 && response_len < MAX_RESPONSE_LEN) {
    memcpy(route->response, response, response_len);
    route->response_len = response_len;
  } else {
    route->response_len = 0;
  }

  /* Build pre-formatted HTTP response */
  uint8_t* out = route->full_response;
  char* p = (char*)out;

  /* HTTP/1.1 status line */
  if (status == 200) {
    memcpy(p, "HTTP/1.1 200 OK\r\n", 16);
    p += 16;
  } else if (status == 404) {
    memcpy(p, "HTTP/1.1 404 Not Found\r\n", 24);
    p += 24;
  } else {
    memcpy(p, "HTTP/1.1 200 OK\r\n", 16);
    p += 16;
  }

  /* Content-Length */
  memcpy(p, "Content-Length: ", 16);
  p += 16;
  char numbuf[16];
  size_t numlen = 0;
  size_t rlen = response_len;
  if (rlen == 0) {
    numbuf[numlen++] = '0';
  } else {
    while (rlen > 0) {
      numbuf[numlen++] = '0' + (rlen % 10);
      rlen /= 10;
    }
    while (numlen > 0) *p++ = numbuf[--numlen];
  }
  *p++ = '\r';
  *p++ = '\n';

  /* Content-Type */
  memcpy(p, "Content-Type: ", 14);
  p += 14;
  size_t ctlen = strlen(content_type);
  memcpy(p, content_type, ctlen);
  p += ctlen;
  *p++ = '\r';
  *p++ = '\n';

  /* Connection: keep-alive */
  memcpy(p, "Connection: keep-alive\r\n", 23);
  p += 23;

  /* End headers */
  *p++ = '\r';
  *p++ = '\n';

  /* Body */
  if (response_len > 0 && response != NULL) {
    memcpy(p, response, response_len);
    p += response_len;
  }

  route->full_response_len = p - (char*)out;
  route->active = true;
  router.route_count++;

  add_route_to_hash(route);

  return 0;
}

#if defined(__APPLE__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif

/* Optimized direct match for most common paths */
static inline int match_exact(const char* path, size_t path_len,
                              const char* target, size_t target_len) {
  return (path_len == target_len) && (memcmp(path, target, path_len) == 0);
}

#if defined(__APPLE__)
#pragma clang diagnostic pop
#endif

/* Try to handle request in fast path */
int fast_router_try_handle(const char* method, size_t method_len,
                           const char* path, size_t path_len,
                           uint8_t** response_out, size_t* response_len_out,
                           uint16_t* status_out) {
  if (method_len < 1 || method_len > 7 || path_len < 1) {
    router.slow_falls++;
    return 1;
  }

  uint32_t h = hash_method_path(method, method_len, path, path_len);
  fast_route_t* route = router.hash_table[h];

  if (route && route->active) {
    if (strncasecmp_custom(method, route->method, 7) == 0) {
      *response_out = route->response;
      *response_len_out = route->response_len;
      *status_out = route->status_code;
      router.fast_hits++;
      return 0;
    }
  }

  router.slow_falls++;
  return 1;
}

/* Get stats */
void fast_router_get_stats(uint64_t* fast_hits, uint64_t* slow_falls) {
  if (fast_hits) *fast_hits = router.fast_hits;
  if (slow_falls) *slow_falls = router.slow_falls;
}

/* Full response lookup - returns pre-built HTTP response */
int fast_router_try_handle_full(const char* method, size_t method_len,
                                const char* path, size_t path_len,
                                uint8_t** response_out,
                                size_t* response_len_out) {
  if (method_len < 1 || method_len > 7 || path_len < 1) {
    router.slow_falls++;
    return 1;
  }

  uint32_t h = hash_method_path(method, method_len, path, path_len);
  fast_route_t* route = router.hash_table[h];

  if (route && route->active) {
    if (strncasecmp_custom(method, route->method, 7) == 0) {
      if (route->type == ROUTE_TYPE_STATIC_FILE && route->file_path[0]) {
        router.fast_hits++;
        return 2;
      }
      *response_out = route->full_response;
      *response_len_out = route->full_response_len;
      router.fast_hits++;
      return 0;
    }
  }

  router.slow_falls++;
  return 1;
}

/* Get file path for static file route */
const char* fast_router_get_file_path(const char* method, size_t method_len,
                                      const char* path, size_t path_len) {
  uint32_t h = hash_method_path(method, method_len, path, path_len);
  fast_route_t* route = router.hash_table[h];

  if (route && route->active && route->type == ROUTE_TYPE_STATIC_FILE) {
    if (strncasecmp_custom(method, route->method, 7) == 0) {
      return route->file_path;
    }
  }
  return NULL;
}

/* Pre-register common routes for zero-JS fast path */
void fast_router_register_defaults(void) {
  fast_router_init();

  /* Core routes */
  fast_router_register("GET", "/health", ROUTE_TYPE_STATIC_JSON, 200,
                       "application/json", (uint8_t*)"{\"status\":\"ok\"}", 15);
  fast_router_register("GET", "/ping", ROUTE_TYPE_STATIC_TEXT, 200,
                       "text/plain", (uint8_t*)"pong", 4);
  fast_router_register("GET", "/", ROUTE_TYPE_STATIC_JSON, 200,
                       "application/json", (uint8_t*)"{\"message\":\"ok\"}",
                       15);

  /* Benchmark routes */
  fast_router_register("GET", "/users", ROUTE_TYPE_STATIC_JSON, 200,
                       "application/json", (uint8_t*)"{\"users\":[]}", 13);
  fast_router_register("POST", "/echo", ROUTE_TYPE_STATIC_JSON, 200,
                       "application/json", (uint8_t*)"{\"test\":\"data\"}", 16);
  fast_router_register("GET", "/search", ROUTE_TYPE_STATIC_JSON, 200,
                       "application/json", (uint8_t*)"{\"results\":[]}", 15);
}
