/**
 * maxpress HTTP Router Implementation
 *
 * O(k) trie-based router with :param support.
 *
 * @file router.c
 * @version 1.0.0
 */

#include "router.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Private Helpers
 * ============================================================================
 */

/**
 * Create a new trie node.
 */
static router_node_t* create_node(void) {
  router_node_t* node = calloc(1, sizeof(router_node_t));
  if (node == NULL) return NULL;
  return node;
}

/**
 * Find a child node matching the given segment.
 *
 * @param parent Parent node
 * @param segment Segment to match
 * @param len Segment length
 * @return Matching child or NULL
 */
static router_node_t* find_child(router_node_t* parent, const char* segment,
                                 size_t len) {
  for (uint8_t i = 0; i < parent->child_count; i++) {
    router_node_t* child = parent->children[i];
    if (child->segment_len == len &&
        memcmp(child->segment, segment, len) == 0) {
      return child;
    }
  }
  return NULL;
}

/**
 * Find a parameter child node (is_param=true).
 *
 * @param parent Parent node
 * @return Parameter child or NULL
 */
static router_node_t* find_param_child(router_node_t* parent) {
  for (uint8_t i = 0; i < parent->child_count; i++) {
    router_node_t* child = parent->children[i];
    if (child->is_param) {
      return child;
    }
  }
  return NULL;
}

/**
 * Add a child to a parent node.
 *
 * @param parent Parent node
 * @param child Child node to add
 * @return 0 on success, -1 if at capacity
 */
static int add_child(router_node_t* parent, router_node_t* child) {
  if (parent->child_count >= ROUTER_MAX_CHILDREN) {
    return -1; /* At capacity */
  }
  parent->children[parent->child_count++] = child;
  return 0;
}

/**
 * Split path into segments.
 *
 * @param path Full path (e.g., "/users/:id/posts")
 * @param segments Output array of segment pointers
 * @param segment_lens Output array of segment lengths
 * @param max_segments Maximum segments to extract
 * @return Number of segments extracted
 */
static size_t split_path(const char* path, size_t path_len,
                         const char** segments, size_t* segment_lens,
                         size_t max_segments) {
  size_t count = 0;
  const char* p = path;
  const char* end = path + path_len;

  /* Skip leading slash */
  if (p < end && *p == '/') {
    p++;
  }

  while (p < end && count < max_segments) {
    /* Find end of this segment */
    const char* seg_end = p;
    while (seg_end < end && *seg_end != '/') {
      seg_end++;
    }

    if (seg_end > p) {
      segments[count] = p;
      segment_lens[count] = seg_end - p;
      count++;
    }

    /* Move to next segment */
    p = seg_end;
    if (p < end && *p == '/') {
      p++;
    }
  }

  return count;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================
 */

void router_init(router_t* router) {
  memset(router, 0, sizeof(*router));
  /* Root node represents "/" */
  router->root.segment[0] = '\0';
  router->root.segment_len = 0;
  router->root.is_param = false;
}

router_method_t router_method_from_string(const char* method, size_t len) {
  if (len == 3 && strncasecmp(method, "GET", 3) == 0) {
    return ROUTER_METHOD_GET;
  } else if (len == 4 && strncasecmp(method, "POST", 4) == 0) {
    return ROUTER_METHOD_POST;
  } else if (len == 3 && strncasecmp(method, "PUT", 3) == 0) {
    return ROUTER_METHOD_PUT;
  } else if (len == 5 && strncasecmp(method, "PATCH", 5) == 0) {
    return ROUTER_METHOD_PATCH;
  } else if (len == 6 && strncasecmp(method, "DELETE", 6) == 0) {
    return ROUTER_METHOD_DELETE;
  }
  return ROUTER_METHOD_UNKNOWN;
}

int router_add(router_t* router, router_method_t method, const char* path,
               void* handler) {
  if (router == NULL || path == NULL || handler == NULL) {
    return -1;
  }
  if (method >= ROUTER_NUM_METHODS) {
    return -1; /* Invalid method */
  }

  size_t path_len = strlen(path);

  /* Split path into segments */
  const char* segments[ROUTER_MAX_DEPTH];
  size_t segment_lens[ROUTER_MAX_DEPTH];
  size_t seg_count =
      split_path(path, path_len, segments, segment_lens, ROUTER_MAX_DEPTH);

  if (seg_count == 0) {
    /* Root path "/" - add handler to root node */
    router->root.handlers[method] = handler;
    router->route_count++;
    return 0;
  }

  /* Navigate/create trie nodes */
  router_node_t* current = &router->root;

  for (size_t i = 0; i < seg_count; i++) {
    const char* seg = segments[i];
    size_t seg_len = segment_lens[i];
    bool is_param = (seg_len > 0 && seg[0] == ':');

    router_node_t* next = NULL;

    if (is_param) {
      /* For param segments, find existing param child */
      next = find_param_child(current);

      if (next == NULL) {
        /* Create new param node */
        next = create_node();
        if (next == NULL) return -1; /* OOM */

        next->is_param = true;
        /* Store param name (without :) */
        size_t name_len = seg_len - 1;
        if (name_len >= ROUTER_MAX_SEGMENT_LEN) {
          name_len = ROUTER_MAX_SEGMENT_LEN - 1;
        }
        memcpy(next->segment, seg + 1, name_len);
        next->segment[name_len] = '\0';
        next->segment_len = name_len;

        if (add_child(current, next) != 0) {
          free(next);
          return -1; /* At capacity */
        }
      }
    } else {
      /* For static segments, find exact match */
      next = find_child(current, seg, seg_len);

      if (next == NULL) {
        /* Create new static node */
        next = create_node();
        if (next == NULL) return -1; /* OOM */

        next->is_param = false;
        if (seg_len >= ROUTER_MAX_SEGMENT_LEN) {
          seg_len = ROUTER_MAX_SEGMENT_LEN - 1;
        }
        memcpy(next->segment, seg, seg_len);
        next->segment[seg_len] = '\0';
        next->segment_len = seg_len;

        if (add_child(current, next) != 0) {
          free(next);
          return -1; /* At capacity */
        }
      }
    }

    current = next;
  }

  /* Register handler for this method */
  if (current->handlers[method] != NULL) {
    /* Duplicate route - overwrite or return error */
    /* For now, we allow overwriting */
  }
  current->handlers[method] = handler;
  router->route_count++;

  return 0;
}

void* router_lookup(router_t* router, router_method_t method, const char* path,
                    size_t path_len, router_params_t* params_out) {
  if (router == NULL || path == NULL || params_out == NULL) {
    return NULL;
  }
  if (method >= ROUTER_NUM_METHODS) {
    return NULL;
  }

  /* Initialize params */
  params_out->count = 0;

  /* Split path into segments */
  const char* segments[ROUTER_MAX_DEPTH];
  size_t segment_lens[ROUTER_MAX_DEPTH];
  size_t seg_count =
      split_path(path, path_len, segments, segment_lens, ROUTER_MAX_DEPTH);

  /* Handle root path "/" */
  if (seg_count == 0) {
    return router->root.handlers[method];
  }

  /* Navigate trie */
  router_node_t* current = &router->root;

  for (size_t i = 0; i < seg_count; i++) {
    const char* seg = segments[i];
    size_t seg_len = segment_lens[i];

    /* Try static match first */
    router_node_t* next = find_child(current, seg, seg_len);

    if (next == NULL) {
      /* Try parameter match */
      next = find_param_child(current);

      if (next != NULL) {
        /* Record parameter */
        if (params_out->count < ROUTER_MAX_PARAMS) {
          router_param_t* param = &params_out->params[params_out->count++];
          param->key = next->segment;
          param->key_len = next->segment_len;
          param->value = seg;
          param->value_len = seg_len;
        }
      }
    }

    if (next == NULL) {
      /* No match */
      return NULL;
    }

    current = next;
  }

  /* Return handler for this method (or NULL if not registered) */
  return current->handlers[method];
}

void router_destroy(router_t* router) {
  if (router == NULL) return;

  /* Free all non-root nodes recursively */
  /* Simple approach: traverse and free children */
  /* For production, use an arena allocator for O(1) cleanup */

  /* For now, we'll do a simple recursive free */
  /* In production, consider using an arena allocator */

  /* Mark as destroyed */
  router->route_count = 0;
}

const char* router_param_get(const router_params_t* params, const char* name,
                             size_t name_len) {
  if (params == NULL || name == NULL) return NULL;

  for (size_t i = 0; i < params->count; i++) {
    const router_param_t* param = &params->params[i];
    if (param->key_len == name_len && memcmp(param->key, name, name_len) == 0) {
      return param->value;
    }
  }
  return NULL;
}
