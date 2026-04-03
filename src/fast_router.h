/**
 * Fast Path Router - C Implementation
 *
 * Routes simple requests entirely in C without N-API crossing.
 * Only falls back to JavaScript for complex dynamic routes.
 *
 * @file fast_router.h
 * @version 2.0.0
 */

#ifndef FAST_ROUTER_H
#define FAST_ROUTER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_FAST_ROUTES 64
#define MAX_PATH_LEN 128
#define MAX_RESPONSE_LEN 4096

/* Route types */
typedef enum {
  ROUTE_TYPE_STATIC_JSON,   /* Static JSON response */
  ROUTE_TYPE_STATIC_TEXT,   /* Static text response */
  ROUTE_TYPE_DYNAMIC        /* Requires JavaScript handler */
} route_type_t;

/* Fast route entry */
typedef struct {
  char method[8];
  char path[MAX_PATH_LEN];
  route_type_t type;
  uint16_t status_code;
  char content_type[64];
  uint8_t response[MAX_RESPONSE_LEN];
  size_t response_len;
  bool active;
} fast_route_t;

/* Router state */
typedef struct {
  fast_route_t routes[MAX_FAST_ROUTES];
  int route_count;
  uint64_t fast_hits;
  uint64_t slow_falls;
} fast_router_t;

/* Initialize router */
void fast_router_init(void);

/* Register a fast route */
int fast_router_register(const char* method, const char* path,
                         route_type_t type, uint16_t status,
                         const char* content_type,
                         const uint8_t* response, size_t response_len);

/* Try to handle request in fast path */
/* Returns: 0 = handled, 1 = needs JavaScript, -1 = error */
int fast_router_try_handle(const char* method, size_t method_len,
                           const char* path, size_t path_len,
                           uint8_t** response_out, size_t* response_len_out,
                           uint16_t* status_out);

/* Get stats */
void fast_router_get_stats(uint64_t* fast_hits, uint64_t* slow_falls);

/* Pre-register common routes */
void fast_router_register_defaults(void);

#endif /* FAST_ROUTER_H */
