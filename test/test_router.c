/**
 * maxpress HTTP Router Test Suite
 *
 * Standalone test binary for the trie-based router.
 *
 * Build: gcc -o test_router test_router.c ../src/router.c -I..
 * Run: ./test_router
 *
 * @file test_router.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "src/router.h"

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

/* Dummy handler pointers for testing */
void* HANDLER_GET_USERS = (void*)0x1;
void* HANDLER_GET_USER = (void*)0x2;
void* HANDLER_CREATE_USER = (void*)0x3;
void* HANDLER_GET_POSTS = (void*)0x4;
void* HANDLER_GET_POST = (void*)0x5;
void* HANDLER_UPDATE_POST = (void*)0x6;
void* HANDLER_DELETE_POST = (void*)0x7;
void* HANDLER_ROOT = (void*)0x8;

#define TEST(name) printf("  [TEST] %-50s ", name);
#define PASS()                       \
  do {                               \
    printf("\033[32mPASS\033[0m\n"); \
    tests_passed++;                  \
  } while (0)
#define FAIL(fmt, ...)                                       \
  do {                                                       \
    printf("\033[31mFAIL\033[0m: " fmt "\n", ##__VA_ARGS__); \
    tests_failed++;                                          \
  } while (0)

/* ============================================================================
 * Test Cases
 * ============================================================================
 */

void test_basic_routes(void) {
  router_t router;
  router_init(&router);

  /* Add routes */
  router_add(&router, ROUTER_METHOD_GET, "/", HANDLER_ROOT);
  router_add(&router, ROUTER_METHOD_GET, "/users", HANDLER_GET_USERS);
  router_add(&router, ROUTER_METHOD_POST, "/users", HANDLER_CREATE_USER);
  router_add(&router, ROUTER_METHOD_GET, "/users/:id", HANDLER_GET_USER);

  /* Test lookups */
  router_params_t params;
  void* handler;

  TEST("GET / matches root");
  handler = router_lookup(&router, ROUTER_METHOD_GET, "/", 1, &params);
  if (handler != HANDLER_ROOT) {
    FAIL("Expected root handler");
  } else {
    PASS();
  }

  TEST("GET /users matches");
  handler = router_lookup(&router, ROUTER_METHOD_GET, "/users", 6, &params);
  if (handler != HANDLER_GET_USERS) {
    FAIL("Expected GET_USERS handler");
  } else {
    PASS();
  }

  TEST("POST /users matches");
  handler = router_lookup(&router, ROUTER_METHOD_POST, "/users", 6, &params);
  if (handler != HANDLER_CREATE_USER) {
    FAIL("Expected CREATE_USER handler");
  } else {
    PASS();
  }

  TEST("GET /users/42 extracts param");
  handler = router_lookup(&router, ROUTER_METHOD_GET, "/users/42", 9, &params);
  if (handler != HANDLER_GET_USER) {
    FAIL("Expected GET_USER handler, got %p", handler);
  } else if (params.count != 1) {
    FAIL("Expected 1 param, got %zu", params.count);
  } else if (strncmp(params.params[0].key, "id", params.params[0].key_len) !=
             0) {
    FAIL("Expected param 'id'");
  } else if (strncmp(params.params[0].value, "42",
                     params.params[0].value_len) != 0) {
    FAIL("Expected value '42'");
  } else {
    PASS();
  }

  TEST("GET /unknown returns NULL");
  handler = router_lookup(&router, ROUTER_METHOD_GET, "/unknown", 8, &params);
  if (handler != NULL) {
    FAIL("Expected NULL for unknown route");
  } else {
    PASS();
  }

  router_destroy(&router);
}

void test_nested_params(void) {
  router_t router;
  router_init(&router);

  /* Add nested route: /users/:id/posts/:postId */
  router_add(&router, ROUTER_METHOD_GET, "/users/:id/posts/:postId",
             HANDLER_GET_POST);

  router_params_t params;
  void* handler;

  TEST("GET /users/42/posts/123 extracts both params");
  handler = router_lookup(&router, ROUTER_METHOD_GET, "/users/42/posts/123", 19,
                          &params);
  if (handler != HANDLER_GET_POST) {
    FAIL("Expected GET_POST handler");
    router_destroy(&router);
    return;
  }
  if (params.count != 2) {
    FAIL("Expected 2 params, got %zu", params.count);
    router_destroy(&router);
    return;
  }
  if (strncmp(params.params[0].key, "id", 2) != 0) {
    FAIL("Expected first param 'id'");
    router_destroy(&router);
    return;
  }
  if (strncmp(params.params[0].value, "42", 2) != 0) {
    FAIL("Expected first value '42'");
    router_destroy(&router);
    return;
  }
  if (strncmp(params.params[1].key, "postId", 6) != 0) {
    FAIL("Expected second param 'postId'");
    router_destroy(&router);
    return;
  }
  if (strncmp(params.params[1].value, "123", 3) != 0) {
    FAIL("Expected second value '123'");
    router_destroy(&router);
    return;
  }
  PASS();

  router_destroy(&router);
}

void test_method_mismatch(void) {
  router_t router;
  router_init(&router);

  /* Add GET /users/:id */
  router_add(&router, ROUTER_METHOD_GET, "/users/:id", HANDLER_GET_USER);

  router_params_t params;
  void* handler;

  TEST("POST /users/42 does NOT match GET /users/:id");
  handler = router_lookup(&router, ROUTER_METHOD_POST, "/users/42", 9, &params);
  if (handler != NULL) {
    FAIL("POST should not match GET route");
  } else {
    PASS();
  }

  router_destroy(&router);
}

void test_param_value_retrieval(void) {
  router_t router;
  router_init(&router);

  router_add(&router, ROUTER_METHOD_GET, "/users/:id/posts/:postId",
             HANDLER_GET_POST);

  router_params_t params;
  router_lookup(&router, ROUTER_METHOD_GET, "/users/abc123/posts/xyz789", 26,
                &params);

  TEST("router_param_get retrieves 'id'");
  const char* id = router_param_get(&params, "id", 2);
  if (id == NULL || strncmp(id, "abc123", 6) != 0) {
    FAIL("Failed to retrieve 'id' parameter");
  } else {
    PASS();
  }

  TEST("router_param_get retrieves 'postId'");
  const char* postId = router_param_get(&params, "postId", 6);
  if (postId == NULL || strncmp(postId, "xyz789", 6) != 0) {
    FAIL("Failed to retrieve 'postId' parameter");
  } else {
    PASS();
  }

  TEST("router_param_get returns NULL for unknown param");
  const char* unknown = router_param_get(&params, "unknown", 7);
  if (unknown != NULL) {
    FAIL("Expected NULL for unknown param");
  } else {
    PASS();
  }

  router_destroy(&router);
}

void test_multiple_methods_same_path(void) {
  router_t router;
  router_init(&router);

  /* Register multiple methods for same path */
  router_add(&router, ROUTER_METHOD_GET, "/posts/:id", HANDLER_GET_POST);
  router_add(&router, ROUTER_METHOD_PUT, "/posts/:id", HANDLER_UPDATE_POST);
  router_add(&router, ROUTER_METHOD_DELETE, "/posts/:id", HANDLER_DELETE_POST);

  router_params_t params;
  void* handler;

  TEST("GET /posts/1 returns GET handler");
  handler = router_lookup(&router, ROUTER_METHOD_GET, "/posts/1", 8, &params);
  if (handler != HANDLER_GET_POST) {
    FAIL("Wrong handler for GET");
  } else {
    PASS();
  }

  TEST("PUT /posts/1 returns PUT handler");
  handler = router_lookup(&router, ROUTER_METHOD_PUT, "/posts/1", 8, &params);
  if (handler != HANDLER_UPDATE_POST) {
    FAIL("Wrong handler for PUT");
  } else {
    PASS();
  }

  TEST("DELETE /posts/1 returns DELETE handler");
  handler =
      router_lookup(&router, ROUTER_METHOD_DELETE, "/posts/1", 8, &params);
  if (handler != HANDLER_DELETE_POST) {
    FAIL("Wrong handler for DELETE");
  } else {
    PASS();
  }

  router_destroy(&router);
}

void test_method_string_conversion(void) {
  TEST("router_method_from_string GET");
  if (router_method_from_string("GET", 3) != ROUTER_METHOD_GET) {
    FAIL("Failed to parse GET");
  } else {
    PASS();
  }

  TEST("router_method_from_string POST");
  if (router_method_from_string("POST", 4) != ROUTER_METHOD_POST) {
    FAIL("Failed to parse POST");
  } else {
    PASS();
  }

  TEST("router_method_from_string PUT");
  if (router_method_from_string("PUT", 3) != ROUTER_METHOD_PUT) {
    FAIL("Failed to parse PUT");
  } else {
    PASS();
  }

  TEST("router_method_from_string PATCH");
  if (router_method_from_string("PATCH", 5) != ROUTER_METHOD_PATCH) {
    FAIL("Failed to parse PATCH");
  } else {
    PASS();
  }

  TEST("router_method_from_string DELETE");
  if (router_method_from_string("DELETE", 6) != ROUTER_METHOD_DELETE) {
    FAIL("Failed to parse DELETE");
  } else {
    PASS();
  }

  TEST("router_method_from_string UNKNOWN");
  if (router_method_from_string("UNKNOWN", 7) != ROUTER_METHOD_UNKNOWN) {
    FAIL("Should return UNKNOWN for unknown method");
  } else {
    PASS();
  }
}

void test_deep_nesting(void) {
  router_t router;
  router_init(&router);

  /* Deep nested route: /api/v1/users/:id/projects/:projectId/tasks/:taskId */
  router_add(&router, ROUTER_METHOD_GET,
             "/api/v1/users/:id/projects/:projectId/tasks/:taskId",
             HANDLER_ROOT);

  router_params_t params;
  void* handler;

  TEST("Deep nesting with 3 params");
  handler = router_lookup(
      &router, ROUTER_METHOD_GET, "/api/v1/users/42/projects/101/tasks/999",
      strlen("/api/v1/users/42/projects/101/tasks/999"), &params);
  if (handler != HANDLER_ROOT) {
    FAIL("Handler mismatch");
    router_destroy(&router);
    return;
  }
  if (params.count != 3) {
    FAIL("Expected 3 params, got %zu", params.count);
    router_destroy(&router);
    return;
  }
  if (strncmp(params.params[0].key, "id", 2) != 0 ||
      strncmp(params.params[1].key, "projectId", 9) != 0 ||
      strncmp(params.params[2].key, "taskId", 6) != 0) {
    FAIL("Param names don't match");
    router_destroy(&router);
    return;
  }
  PASS();

  router_destroy(&router);
}

void test_performance(void) {
  router_t router;
  router_init(&router);

  /* Add many routes */
  router_add(&router, ROUTER_METHOD_GET, "/api/v1/users", HANDLER_GET_USERS);
  router_add(&router, ROUTER_METHOD_GET, "/api/v1/users/:id", HANDLER_GET_USER);
  router_add(&router, ROUTER_METHOD_GET, "/api/v1/posts", HANDLER_GET_POSTS);
  router_add(&router, ROUTER_METHOD_GET, "/api/v1/posts/:id", HANDLER_GET_POST);
  router_add(&router, ROUTER_METHOD_GET, "/api/v2/users", HANDLER_GET_USERS);
  router_add(&router, ROUTER_METHOD_GET, "/api/v2/users/:id", HANDLER_GET_USER);

  /* Time 10,000 lookups */
  router_params_t params;
  const char* path = "/api/v1/users/12345";
  size_t path_len = strlen(path);

  clock_t start = clock();
  for (int i = 0; i < 10000; i++) {
    void* handler =
        router_lookup(&router, ROUTER_METHOD_GET, path, path_len, &params);
    (void)handler; /* Suppress unused warning */
  }
  clock_t end = clock();

  double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
  double per_lookup = (elapsed / 10000) * 1000000; /* microseconds */

  TEST("10,000 lookups performance");
  printf("(%.3f μs/lookup) ", per_lookup);
  if (elapsed > 1.0) { /* Should be much faster than 1 second */
    FAIL("Too slow: %.3f seconds", elapsed);
  } else {
    PASS();
  }

  router_destroy(&router);
}

void test_static_vs_param_precedence(void) {
  router_t router;
  router_init(&router);

  /* Add both static and param routes at same level */
  router_add(&router, ROUTER_METHOD_GET, "/users/new",
             HANDLER_GET_USER); /* static */
  router_add(&router, ROUTER_METHOD_GET, "/users/:id",
             HANDLER_GET_USER); /* param */

  router_params_t params;
  void* handler;

  TEST("Static route takes precedence over param");
  handler =
      router_lookup(&router, ROUTER_METHOD_GET, "/users/new", 10, &params);
  if (handler == NULL) {
    FAIL("Should match static route");
  } else {
    /* Should match static "/users/new", not param "/users/:id" */
    if (params.count != 0) {
      FAIL("Static route should not extract params");
    } else {
      PASS();
    }
  }

  /* Now test that other values match param */
  TEST("Param route matches non-static values");
  handler =
      router_lookup(&router, ROUTER_METHOD_GET, "/users/123", 10, &params);
  if (handler == NULL) {
    FAIL("Should match param route");
  } else if (params.count != 1) {
    FAIL("Should extract 1 param");
  } else if (strncmp(params.params[0].value, "123", 3) != 0) {
    FAIL("Param value should be '123'");
  } else {
    PASS();
  }

  router_destroy(&router);
}

/* ============================================================================
 * Main
 * ============================================================================
 */

int main(void) {
  printf("\033[1mmaxpress HTTP Router Test Suite\033[0m\n");
  printf("====================================\n\n");

  test_basic_routes();
  test_nested_params();
  test_method_mismatch();
  test_param_value_retrieval();
  test_multiple_methods_same_path();
  test_method_string_conversion();
  test_deep_nesting();
  test_static_vs_param_precedence();

  printf("\n====================================\n");
  printf("\033[1mResults:\033[0m %d passed, %d failed\n", tests_passed,
         tests_failed);

  return tests_failed > 0 ? 1 : 0;
}
