/**
 * expressmax HTTP Parser Test Suite
 *
 * Standalone test binary for the HTTP/1.1 request parser.
 *
 * Build: gcc -o test_parser test_parser.c ../src/parser.c -I..
 * Run: ./test_parser
 * Valgrind: valgrind --leak-check=full ./test_parser
 *
 * @file test_parser.c
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/server.h"

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

/* ============================================================================
 * Test Utilities
 * ============================================================================
 */

#define TEST(name) printf("  [TEST] %-40s ", name);
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

/**
 * Helper to run parser and check basic expectations.
 */
void run_parse_test(const char* name, const uint8_t* data, size_t len,
                    int expected_status, const char* expected_method,
                    const char* expected_path, size_t expected_body_start) {
  TEST(name);

  parse_result_t result;
  int status = http_parse_request(data, len, &result);

  if (status != expected_status) {
    FAIL("Expected status %d, got %d", expected_status, status);
    return;
  }

  if (expected_method &&
      (strncmp(result.method, expected_method, result.method_len) != 0 ||
       result.method_len != strlen(expected_method))) {
    char actual[32] = {0};
    memcpy(actual, result.method,
           result.method_len > 31 ? 31 : result.method_len);
    FAIL("Expected method '%s', got '%s'", expected_method, actual);
    return;
  }

  if (expected_path &&
      (strncmp(result.path, expected_path, result.path_len) != 0 ||
       result.path_len != strlen(expected_path))) {
    char actual[256] = {0};
    memcpy(actual, result.path, result.path_len > 255 ? 255 : result.path_len);
    FAIL("Expected path '%s', got '%s'", expected_path, actual);
    return;
  }

  if (expected_body_start != (size_t)-1 &&
      result.body_start != expected_body_start) {
    FAIL("Expected body_start %zu, got %zu", expected_body_start,
         result.body_start);
    return;
  }

  PASS();
}

/* ============================================================================
 * Test Cases
 * ============================================================================
 */

void test_basic_get(void) {
  const uint8_t request[] = "GET /health HTTP/1.1\r\n\r\n";

  run_parse_test("Simple GET /health", request, sizeof(request) - 1,
                 PARSE_STATUS_DONE, "GET", "/health", 24);
}

void test_get_with_query(void) {
  const uint8_t request[] = "GET /users?id=123&expand=true HTTP/1.1\r\n\r\n";

  parse_result_t result;
  int status = http_parse_request(request, sizeof(request) - 1, &result);

  TEST("GET with query string");
  if (status != PARSE_STATUS_DONE) {
    FAIL("Expected DONE, got %d", status);
    return;
  }
  if (strncmp(result.path, "/users", result.path_len) != 0) {
    FAIL("Expected path '/users'");
    return;
  }
  if (result.query == NULL ||
      strncmp(result.query, "id=123&expand=true", result.query_len) != 0) {
    FAIL("Query string not parsed correctly");
    return;
  }
  PASS();
}

void test_get_with_headers(void) {
  const uint8_t request[] =
      "GET /api/data HTTP/1.1\r\n"
      "Host: example.com\r\n"
      "Accept: application/json\r\n"
      "\r\n";

  run_parse_test("GET with headers", request, sizeof(request) - 1,
                 PARSE_STATUS_DONE, "GET", "/api/data", 71);
}

void test_post_with_body(void) {
  const uint8_t request[] =
      "POST /echo HTTP/1.1\r\n"
      "Content-Length: 13\r\n"
      "\r\n"
      "Hello, World!";

  parse_result_t result;
  int status = http_parse_request(request, sizeof(request) - 1, &result);

  TEST("POST with Content-Length: 13");
  if (status != PARSE_STATUS_DONE) {
    FAIL("Expected DONE, got %d", status);
    return;
  }
  if (!result.body_present) {
    FAIL("Body should be present");
    return;
  }
  if (result.body_length != 13) {
    FAIL("Expected body_length 13, got %zu", result.body_length);
    return;
  }
  if (result.body_start == 0) {
    FAIL("body_start should be set");
    return;
  }
  /* Verify body points to correct location */
  const char* body = (const char*)request + result.body_start;
  if (memcmp(body, "Hello, World!", 13) != 0) {
    FAIL("Body content mismatch");
    return;
  }
  PASS();
}

void test_post_partial_body(void) {
  const uint8_t request[] =
      "POST /upload HTTP/1.1\r\n"
      "Content-Length: 1000000\r\n"
      "\r\n"
      "Partial data...";

  parse_result_t result;
  int status = http_parse_request(request, sizeof(request) - 1, &result);

  TEST("POST with large Content-Length, partial body");
  if (status != PARSE_STATUS_NEED_MORE) {
    FAIL("Expected NEED_MORE, got %d", status);
    return;
  }
  if (result.body_length != 1000000) {
    FAIL("Expected body_length 1000000, got %zu", result.body_length);
    return;
  }
  PASS();
}

void test_need_more_data(void) {
  const uint8_t partial[] = "GET /test HTTP/1.1\r\nHo";

  parse_result_t result;
  int status = http_parse_request(partial, sizeof(partial) - 1, &result);

  TEST("Incomplete headers");
  if (status != PARSE_STATUS_NEED_MORE) {
    FAIL("Expected NEED_MORE, got %d", status);
    return;
  }
  PASS();
}

void test_need_more_body(void) {
  const uint8_t request[] =
      "POST /create HTTP/1.1\r\n"
      "Content-Length: 100\r\n"
      "\r\n"
      "short"; /* Only 5 bytes, need 100 */

  parse_result_t result;
  int status = http_parse_request(request, sizeof(request) - 1, &result);

  TEST("Incomplete body (NEED_MORE)");
  if (status != PARSE_STATUS_NEED_MORE) {
    FAIL("Expected NEED_MORE, got %d", status);
    return;
  }
  if (result.body_length != 100) {
    FAIL("Expected body_length 100, got %zu", result.body_length);
    return;
  }
  PASS();
}

void test_empty_body_post(void) {
  const uint8_t request[] =
      "POST /empty HTTP/1.1\r\n"
      "Content-Length: 0\r\n"
      "\r\n";

  parse_result_t result;
  int status = http_parse_request(request, sizeof(request) - 1, &result);

  TEST("POST with Content-Length: 0");
  if (status != PARSE_STATUS_DONE) {
    FAIL("Expected DONE, got %d", status);
    return;
  }
  if (result.body_length != 0) {
    FAIL("Expected body_length 0, got %zu", result.body_length);
    return;
  }
  PASS();
}

void test_various_methods(void) {
  const char* methods[] = {"GET",   "POST", "PUT",    "DELETE",
                           "PATCH", "HEAD", "OPTIONS"};

  for (int i = 0; i < 7; i++) {
    char request[64];
    snprintf(request, sizeof(request), "%s /resource HTTP/1.1\r\n\r\n",
             methods[i]);

    parse_result_t result;
    int status =
        http_parse_request((const uint8_t*)request, strlen(request), &result);

    char test_name[64];
    snprintf(test_name, sizeof(test_name), "Method: %s", methods[i]);
    TEST(test_name);

    if (status != PARSE_STATUS_DONE) {
      FAIL("Expected DONE, got %d", status);
      continue;
    }
    if (strncmp(result.method, methods[i], result.method_len) != 0) {
      FAIL("Method mismatch");
      continue;
    }
    PASS();
  }
}

void test_content_length_case_insensitive(void) {
  const uint8_t request[] =
      "POST /test HTTP/1.1\r\n"
      "content-length: 5\r\n"
      "\r\n"
      "hello";

  parse_result_t result;
  int status = http_parse_request(request, sizeof(request) - 1, &result);

  TEST("Case-insensitive Content-Length");
  if (status != PARSE_STATUS_DONE) {
    FAIL("Expected DONE, got %d", status);
    return;
  }
  if (result.body_length != 5) {
    FAIL("Expected body_length 5, got %zu", result.body_length);
    return;
  }
  PASS();
}

void test_long_uri(void) {
  /* Create a request with a very long URI path */
  char request[9000];
  strcpy(request, "GET /");
  for (int i = 5; i < 8200; i++) {
    request[i] = 'a';
  }
  strcpy(request + 8200, " HTTP/1.1\r\n\r\n");

  parse_result_t result;
  int status =
      http_parse_request((const uint8_t*)request, strlen(request), &result);

  TEST("Very long URI path");
  if (status == PARSE_STATUS_ERROR && result.error_code != 0) {
    /* This is expected to fail due to URI length limit */
    PASS();
  } else if (status == PARSE_STATUS_DONE && result.path_len >= 8000) {
    /* Or it could succeed if we support long URIs */
    PASS();
  } else {
    FAIL("Unexpected result for long URI");
  }
}

void test_http_version(void) {
  const uint8_t req10[] = "GET / HTTP/1.0\r\n\r\n";
  const uint8_t req11[] = "GET / HTTP/1.1\r\n\r\n";

  parse_result_t result;

  TEST("HTTP/1.0 version");
  http_parse_request(req10, sizeof(req10) - 1, &result);
  if (result.http_major != 1 || result.http_minor != 0) {
    FAIL("Expected HTTP/1.0, got %d.%d", result.http_major, result.http_minor);
    return;
  }
  PASS();

  TEST("HTTP/1.1 version");
  http_parse_request(req11, sizeof(req11) - 1, &result);
  if (result.http_major != 1 || result.http_minor != 1) {
    FAIL("Expected HTTP/1.1, got %d.%d", result.http_major, result.http_minor);
    return;
  }
  PASS();
}

void test_bare_lf(void) {
  const uint8_t request[] = "GET /test HTTP/1.1\n\n";

  parse_result_t result;
  int status = http_parse_request(request, sizeof(request) - 1, &result);

  TEST("Bare LF line endings");
  if (status != PARSE_STATUS_DONE) {
    FAIL("Expected DONE, got %d", status);
    return;
  }
  if (strncmp(result.method, "GET", result.method_len) != 0) {
    FAIL("Method parsing failed");
    return;
  }
  PASS();
}

void test_append_body(void) {
  /* Simulate incremental body arrival */
  connection_t conn;
  memset(&conn, 0, sizeof(conn));

  /* First call: headers received, body partially */
  const uint8_t request1[] =
      "POST /upload HTTP/1.1\r\n"
      "Content-Length: 20\r\n"
      "\r\n"
      "First part...";

  /* Allocate buffer and copy data */
  conn.buffer = malloc(4096);
  memcpy(conn.buffer, request1, sizeof(request1) - 1);
  conn.buffer_len = sizeof(request1) - 1;
  conn.buffer_cap = 4096;

  parse_result_t result1;
  int status1 = http_parse_request(conn.buffer, conn.buffer_len, &result1);

  TEST("Initial partial body");
  if (status1 != PARSE_STATUS_NEED_MORE) {
    FAIL("Expected NEED_MORE, got %d", status1);
    free(conn.buffer);
    return;
  }
  if (result1.body_length != 20) {
    FAIL("Expected body_length 20, got %zu", result1.body_length);
    free(conn.buffer);
    return;
  }

  /* Set up connection state for append */
  conn.body_remaining = result1.body_length;
  conn.buffer_pos = result1.body_start;
  conn.request_complete = false;

  PASS();

  /* Second call: append more data */
  const uint8_t more_data[] = "...rest of body.";
  memcpy(conn.buffer + conn.buffer_len, more_data, sizeof(more_data) - 1);
  conn.buffer_len += sizeof(more_data) - 1;

  parse_result_t result2;
  int status2 = parse_append_body(&conn, conn.buffer + conn.buffer_pos,
                                  sizeof(more_data) - 1, &result2);

  TEST("Append body completion");
  if (status2 != PARSE_STATUS_DONE) {
    FAIL("Expected DONE, got %d", status2);
    free(conn.buffer);
    return;
  }
  if (result2.body_length != 20) {
    FAIL("Expected final body_length 20, got %zu", result2.body_length);
    free(conn.buffer);
    return;
  }
  if (conn.request_complete != true) {
    FAIL("Expected request_complete to be set");
    free(conn.buffer);
    return;
  }

  PASS();
  free(conn.buffer);
}

/* ============================================================================
 * Main
 * ============================================================================
 */

int main(void) {
  printf("\033[1mexpressmax HTTP Parser Test Suite\033[0m\n");
  printf("====================================\n\n");

  test_basic_get();
  test_get_with_query();
  test_get_with_headers();
  test_post_with_body();
  test_post_partial_body();
  test_need_more_data();
  test_need_more_body();
  test_empty_body_post();
  test_various_methods();
  test_content_length_case_insensitive();
  test_long_uri();
  test_http_version();
  test_bare_lf();
  test_append_body();

  printf("\n====================================\n");
  printf("\033[1mResults:\033[0m %d passed, %d failed\n", tests_passed,
         tests_failed);

  return tests_failed > 0 ? 1 : 0;
}
