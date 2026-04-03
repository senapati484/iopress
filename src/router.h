/**
 * iopress HTTP Router
 *
 * O(k) trie-based router with :param support and zero heap allocation during
 * lookup.
 *
 * @file router.h
 * @version 1.0.0
 */

#ifndef EXPRESS_PRO_ROUTER_H
#define EXPRESS_PRO_ROUTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Constants
 * ============================================================================
 */

/** Maximum segment length in path (e.g., "users" in "/users/:id") */
#define ROUTER_MAX_SEGMENT_LEN 64

/** Maximum children per trie node */
#define ROUTER_MAX_CHILDREN 8

/** Maximum number of HTTP methods supported */
#define ROUTER_NUM_METHODS 5

/** Maximum depth of route path (number of / segments) */
#define ROUTER_MAX_DEPTH 16

/** Maximum number of route parameters */
#define ROUTER_MAX_PARAMS 8

/* ============================================================================
 * HTTP Methods
 * ============================================================================
 */

typedef enum {
  ROUTER_METHOD_GET = 0,
  ROUTER_METHOD_POST = 1,
  ROUTER_METHOD_PUT = 2,
  ROUTER_METHOD_PATCH = 3,
  ROUTER_METHOD_DELETE = 4,
  ROUTER_METHOD_UNKNOWN = 255
} router_method_t;

/* ============================================================================
 * Route Parameter
 * ============================================================================
 */

/**
 * Route parameter extracted during lookup.
 */
typedef struct {
  /** Parameter name (e.g., "id" from ":id") */
  const char* key;

  /** Parameter value length */
  size_t key_len;

  /** Parameter value from actual path */
  const char* value;

  /** Parameter value length */
  size_t value_len;
} router_param_t;

/**
 * Parameter array populated during lookup.
 */
typedef struct {
  /** Number of parameters extracted */
  size_t count;

  /** Parameter storage (on stack - no heap allocation) */
  router_param_t params[ROUTER_MAX_PARAMS];
} router_params_t;

/* ============================================================================
 * Trie Node
 * ============================================================================
 */

/**
 * Router trie node structure.
 *
 * Each node represents a path segment. Static segments like "users" are
 * stored directly. Parameter segments like ":id" have is_param=true and
 * the parameter name is stored in segment.
 */
typedef struct router_node_s {
  /** Path segment (e.g., "users" or "id" for ":id") */
  char segment[ROUTER_MAX_SEGMENT_LEN];

  /** Segment length */
  size_t segment_len;

  /** True if this is a parameter segment (:param) */
  bool is_param;

  /** Children nodes (static segments only, params match anything) */
  struct router_node_s* children[ROUTER_MAX_CHILDREN];

  /** Number of valid children */
  uint8_t child_count;

  /** Handler functions per method - NULL if not registered */
  void* handlers[ROUTER_NUM_METHODS];
} router_node_t;

/* ============================================================================
 * Router
 * ============================================================================
 */

/**
 * Router instance containing the trie root.
 */
typedef struct {
  /** Root node (represents "/") */
  router_node_t root;

  /** Number of registered routes */
  size_t route_count;

  /** Arena allocator for nodes (optional, for bulk free) */
  void* arena;
} router_t;

/* ============================================================================
 * Public API
 * ============================================================================
 */

/**
 * Initialize a router instance.
 *
 * @param router Router instance to initialize
 */
void router_init(router_t* router);

/**
 * Convert HTTP method string to enum.
 *
 * @param method HTTP method string (e.g., "GET", "POST")
 * @param len Length of method string
 * @return Method enum value, or ROUTER_METHOD_UNKNOWN
 */
router_method_t router_method_from_string(const char* method, size_t len);

/**
 * Add a route to the router.
 *
 * Parses the path, creates trie nodes as needed, and registers the handler
 * for the specified method.
 *
 * @param router Router instance
 * @param method HTTP method enum
 * @param path Route path (e.g., "/users/:id/posts")
 * @param handler Handler function pointer
 * @return 0 on success, -1 on error (duplicate route, invalid path)
 */
int router_add(router_t* router, router_method_t method, const char* path,
               void* handler);

/**
 * Look up a route in the router.
 *
 * Traverses the trie matching path segments. Populates params_out with
 * extracted parameters. Zero heap allocation - all work on stack.
 *
 * @param router Router instance
 * @param method HTTP method enum
 * @param path Request path to match
 * @param path_len Length of path
 * @param params_out Output parameter array (caller provides stack space)
 * @return Handler function pointer if match found, NULL otherwise
 */
void* router_lookup(router_t* router, router_method_t method, const char* path,
                    size_t path_len, router_params_t* params_out);

/**
 * Destroy router and free all allocated memory.
 *
 * @param router Router instance
 */
void router_destroy(router_t* router);

/* ============================================================================
 * Utility Functions
 * ============================================================================
 */

/**
 * Get parameter value by name from extracted parameters.
 *
 * @param params Parameter array from lookup
 * @param name Parameter name
 * @param name_len Name length
 * @return Pointer to value string, or NULL if not found
 */
const char* router_param_get(const router_params_t* params, const char* name,
                             size_t name_len);

#ifdef __cplusplus
}
#endif

#endif /* EXPRESS_PRO_ROUTER_H */
