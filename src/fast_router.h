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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_FAST_ROUTES 256
#define MAX_PATH_LEN 128
#define MAX_RESPONSE_LEN 4096
#define MAX_FULL_RESPONSE 8192

/* Route types */
typedef enum {
  ROUTE_TYPE_STATIC_JSON,
  ROUTE_TYPE_STATIC_TEXT,
  ROUTE_TYPE_STATIC_FILE,
  ROUTE_TYPE_DYNAMIC
} route_type_t;

/* Fast route entry */
typedef struct fast_route_s {
  char method[8];
  char path[MAX_PATH_LEN];
  route_type_t type;
  uint16_t status_code;
  char content_type[64];
  uint8_t response[MAX_RESPONSE_LEN];
  size_t response_len;
  uint8_t full_response[MAX_FULL_RESPONSE];
  size_t full_response_len;
  char file_path[256];
  bool active;
  struct fast_route_s* next;
} fast_route_t;

/* Router state */
typedef struct {
  fast_route_t routes[MAX_FAST_ROUTES];
  int route_count;
  uint64_t fast_hits;
  uint64_t slow_falls;
  fast_route_t* hash_table[256];
} fast_router_t;

void fast_router_init(void);

int fast_router_register(const char* method, const char* path,
                         route_type_t type, uint16_t status,
                         const char* content_type, const uint8_t* response,
                         size_t response_len);

int fast_router_try_handle(const char* method, size_t method_len,
                           const char* path, size_t path_len,
                           uint8_t** response_out, size_t* response_len_out,
                           uint16_t* status_out);

int fast_router_try_handle_full(const char* method, size_t method_len,
                                const char* path, size_t path_len,
                                uint8_t** response_out,
                                size_t* response_len_out);

const char* fast_router_get_file_path(const char* method, size_t method_len,
                                      const char* path, size_t path_len);

void fast_router_get_stats(uint64_t* fast_hits, uint64_t* slow_falls);

void fast_router_register_defaults(void);

#endif /* FAST_ROUTER_H */
