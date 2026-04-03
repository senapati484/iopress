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

/* Initialize router */
void fast_router_init(void) {
  memset(&router, 0, sizeof(router));
  router.route_count = 0;
  router.fast_hits = 0;
  router.slow_falls = 0;
}

/* Simple hash for path lookup */
static uint32_t hash_path(const char* path, size_t len) {
  uint32_t hash = 5381;
  for (size_t i = 0; i < len && i < 32; i++) {
    hash = ((hash << 5) + hash) + path[i];
  }
  return hash;
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

/* Register a fast route */
int fast_router_register(const char* method, const char* path,
                         route_type_t type, uint16_t status,
                         const char* content_type, const uint8_t* response,
                         size_t response_len) {
  if (router.route_count >= MAX_FAST_ROUTES) {
    return -1;
  }

  fast_route_t* route = &router.routes[router.route_count];

  /* Store method (uppercase) */
  strncpy(route->method, method, 7);
  route->method[7] = '\0';

  /* Store path */
  strncpy(route->path, path, MAX_PATH_LEN - 1);
  route->path[MAX_PATH_LEN - 1] = '\0';

  /* Store response */
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

  route->active = true;
  router.route_count++;

  return 0;
}

/* Optimized direct match for most common paths (B8: O(1) health check) */
static inline int match_exact(const char* path, size_t path_len,
                              const char* target, size_t target_len) {
  return (path_len == target_len) && (memcmp(path, target, path_len) == 0);
}

/* Try to handle request in fast path */
int fast_router_try_handle(const char* method, size_t method_len,
                           const char* path, size_t path_len,
                           uint8_t** response_out, size_t* response_len_out,
                           uint16_t* status_out) {
  /* Fast path - only handle exact matches on known paths */
  if (method_len < 1 || method_len > 7) {
    router.slow_falls++;
    return 1; /* Needs JavaScript */
  }

  /* Case-insensitive method first char check for speed */
  char method_upper = method[0] >= 'a' ? method[0] - 32 : method[0];

  /* Direct O(1) matches for most common paths */
  if (path_len == 1 && path[0] == '/') {
    /* Match GET / */
    for (int i = 0; i < router.route_count; i++) {
      fast_route_t* route = &router.routes[i];
      if (!route->active) continue;
      if (route->path[0] != '/' || route->path[1] != '\0') continue;

      char route_method =
          route->method[0] >= 'a' ? route->method[0] - 32 : route->method[0];
      if (route_method != method_upper) continue;

      *response_out = route->response;
      *response_len_out = route->response_len;
      *status_out = route->status_code;
      router.fast_hits++;
      return 0;
    }
  }

  /* Match /health */
  if (path_len == 7 && memcmp(path, "/health", 7) == 0) {
    for (int i = 0; i < router.route_count; i++) {
      fast_route_t* route = &router.routes[i];
      if (!route->active) continue;
      if (route->path[0] != '/' || strcmp(route->path + 1, "health") != 0)
        continue;

      char route_method =
          route->method[0] >= 'a' ? route->method[0] - 32 : route->method[0];
      if (route_method != method_upper) continue;

      *response_out = route->response;
      *response_len_out = route->response_len;
      *status_out = route->status_code;
      router.fast_hits++;
      return 0;
    }
  }

  /* Hash-based lookup for remaining routes */
  for (int i = 0; i < router.route_count; i++) {
    fast_route_t* route = &router.routes[i];
    if (!route->active) continue;

    char route_method =
        route->method[0] >= 'a' ? route->method[0] - 32 : route->method[0];
    if (route_method != method_upper) continue;

    size_t route_path_len = strlen(route->path);
    if (route_path_len != path_len) continue;

    if (memcmp(route->path, path, path_len) == 0) {
      *response_out = route->response;
      *response_len_out = route->response_len;
      *status_out = route->status_code;
      router.fast_hits++;
      return 0; /* Handled in C */
    }
  }

  router.slow_falls++;
  return 1; /* Needs JavaScript */
}

/* Get stats */
void fast_router_get_stats(uint64_t* fast_hits, uint64_t* slow_falls) {
  if (fast_hits) *fast_hits = router.fast_hits;
  if (slow_falls) *slow_falls = router.slow_falls;
}

/* Pre-register common routes - DISABLED to avoid conflicts with JS routes
 * Users can register fast routes explicitly via the binding */
void fast_router_register_defaults(void) {
  fast_router_init();

  /* Fast router is currently disabled by default
   * To enable fast routes, register them explicitly:
   * fast_router_register("GET", "/static-path", ...)
   */
}
