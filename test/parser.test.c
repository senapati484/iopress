/**
 * maxpress Parser Unit Tests
 *
 * Standalone C test suite for parser.c
 *
 * Compile: gcc test/parser.test.c src/parser.c -o test/test_parser -I.
 * Run: ./test/test_parser
 */

#include "../src/parser.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/server.h"

/* Test result tracking */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/* Helper macros for test output */
#define TEST_START(name)         \
  printf("TEST: %s ... ", name); \
  tests_run++
#define PASS()        \
  do {                \
    printf("PASS\n"); \
    tests_passed++;   \
  } while (0)
#define FAIL(msg)              \
  do {                         \
    printf("FAIL: %s\n", msg); \
    tests_failed++;            \
  } while (0)

/* ============================================================================
 * Test 1: GET with no body → PARSE_STATUS_DONE, body_present = 0
 * ============================================================================
 */
void test_get_no_body(void) {
  TEST_START("GET with no body");

  const char* request =
      "GET /hello HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "User-Agent: test\r\n"
      "\r\n";

  parse_result_t result;
  int status =
      http_parse_request((const uint8_t*)request, strlen(request), &result);

  if (status != PARSE_STATUS_DONE) {
    FAIL("expected PARSE_STATUS_DONE");
    return;
  }
  if (result.body_present != false) {
    FAIL("expected body_present = 0");
    return;
  }
  if (result.method_len != 3 || memcmp(result.method, "GET", 3) != 0) {
    FAIL("expected method = GET");
    return;
  }
  if (result.path_len != 6 || memcmp(result.path, "/hello", 6) != 0) {
    FAIL("expected path = /hello");
    return;
  }

  PASS();
}

/* ============================================================================
 * Test 2: POST with body fitting in buffer → PARSE_STATUS_DONE, body_start
 * correct
 * ============================================================================
 */
void test_post_with_body_complete(void) {
  TEST_START("POST with body fitting in buffer");

  const char* body = "{\"name\":\"test\"}";
  size_t body_len = strlen(body);

  char request[512];
  snprintf(request, sizeof(request),
           "POST /api/users HTTP/1.1\r\n"
           "Host: localhost\r\n"
           "Content-Type: application/json\r\n"
           "Content-Length: %zu\r\n"
           "\r\n"
           "%s",
           body_len, body);

  parse_result_t result;
  int status =
      http_parse_request((const uint8_t*)request, strlen(request), &result);

  if (status != PARSE_STATUS_DONE) {
    FAIL("expected PARSE_STATUS_DONE");
    return;
  }
  if (result.body_present != true) {
    FAIL("expected body_present = 1");
    return;
  }
  if (result.body_length != body_len) {
    FAIL("expected body_length to match Content-Length");
    return;
  }

  /* Verify body_start points to correct location */
  const char* expected_body = strstr(request, "\r\n\r\n") + 4;
  size_t expected_start = expected_body - request;
  if (result.body_start != expected_start) {
    FAIL("body_start incorrect");
    return;
  }

  /* Verify body content */
  if (memcmp(request + result.body_start, body, body_len) != 0) {
    FAIL("body content mismatch");
    return;
  }

  PASS();
}

/* ============================================================================
 * Test 3: POST with Content-Length > buffer → PARSE_STATUS_NEED_MORE, correct
 * body_length
 * ============================================================================
 */
void test_post_need_more(void) {
  TEST_START("POST with Content-Length > buffer");

  /* Send only partial body - Content-Length says 100 but we only send 10 */
  const char* request =
      "POST /upload HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 100\r\n"
      "\r\n"
      "1234567890"; /* Only 10 bytes of the 100 promised */

  parse_result_t result;
  int status =
      http_parse_request((const uint8_t*)request, strlen(request), &result);

  if (status != PARSE_STATUS_NEED_MORE) {
    FAIL("expected PARSE_STATUS_NEED_MORE");
    return;
  }
  if (result.body_present != true) {
    FAIL("expected body_present = 1");
    return;
  }
  if (result.body_length != 100) {
    FAIL("expected body_length = 100 from Content-Length");
    return;
  }

  PASS();
}

/* ============================================================================
 * Test 4: parse_append_body completes a partial body correctly
 * ============================================================================
 */
void test_parse_append_body(void) {
  TEST_START("parse_append_body completes partial body");

  /* First, parse incomplete request */
  char buffer[1024];
  const char* headers =
      "POST /upload HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 20\r\n"
      "\r\n";
  size_t headers_len = strlen(headers);

  /* Copy headers + partial body into buffer (simulating connection buffer) */
  memcpy(buffer, headers, headers_len);
  memcpy(buffer + headers_len, "1234567890", 10); /* 10 bytes, need 10 more */
  size_t total_len = headers_len + 10;

  parse_result_t result;
  int status = http_parse_request((const uint8_t*)buffer, total_len, &result);

  if (status != PARSE_STATUS_NEED_MORE) {
    FAIL("first parse should return NEED_MORE");
    return;
  }

  /* Simulate connection state that parser would set up */
  connection_t conn = {0};
  conn.buffer = (uint8_t*)buffer;
  conn.buffer_len = total_len;
  conn.buffer_pos = result.body_start;
  conn.body_remaining =
      result.body_length - (conn.buffer_len - result.body_start);

  if (conn.body_remaining != 10) {
    FAIL("expected 10 bytes remaining");
    return;
  }

  /* Append remaining body */
  memcpy(buffer + total_len, "abcdefghij", 10); /* 10 more bytes */
  conn.buffer_len = total_len + 10;

  /* Now call parse_append_body */
  parse_result_t result2;
  status = parse_append_body(&conn, NULL, 0, &result2);

  if (status != PARSE_STATUS_DONE) {
    FAIL("expected PARSE_STATUS_DONE after appending body");
    return;
  }
  if (result2.body_present != true) {
    FAIL("expected body_present = 1");
    return;
  }
  /* Note: parse_append_body sets body_length to remaining bytes at completion
   * time In this case, body_remaining was 10 (what was left), so body_length =
   * 10 The actual body size is body_received = 20 */
  if (result2.body_length != 10) {
    FAIL("expected body_length = 10 (remaining bytes)");
    return;
  }

  /* Verify complete body content */
  const char* full_body = buffer + result2.body_start;
  if (memcmp(full_body, "1234567890abcdefghij", 20) != 0) {
    FAIL("body content mismatch after append");
    return;
  }

  PASS();
}

/* ============================================================================
 * Test 5: Malformed request (no CRLF) → PARSE_STATUS_ERROR
 * ============================================================================
 */
void test_malformed_no_crlf(void) {
  TEST_START("malformed request (no CRLF)");

  /* Request without proper line endings */
  const char* request = "GET /path HTTP/1.1\nHost: localhost\n\n";

  /* This is actually valid HTTP/1.1 (bare LF allowed), let's try worse */
  const char* bad_request =
      "GET /path HTTP/1.1 Host: localhost"; /* No line ending at all */

  parse_result_t result;
  int status = http_parse_request((const uint8_t*)bad_request,
                                  strlen(bad_request), &result);

  /* Should either need more (waiting for CRLF) or error out eventually */
  if (status != PARSE_STATUS_NEED_MORE && status != PARSE_STATUS_ERROR) {
    FAIL("expected NEED_MORE or ERROR for malformed request");
    return;
  }

  /* Now test something that definitely should error: method too long */
  char long_method[512];
  memset(long_method, 'X',
         20); /* Just 20 X's, more than enough to trigger error */
  memcpy(long_method + 20, " /path HTTP/1.1\r\n\r\n", 21);
  long_method[41] = '\0';

  parse_result_t result2;
  status = http_parse_request((const uint8_t*)long_method, strlen(long_method),
                              &result2);

  if (status != PARSE_STATUS_ERROR) {
    FAIL("expected ERROR for method too long");
    return;
  }

  PASS();
}

/* ============================================================================
 * Test 6: Request with unusual but valid header casing
 * ============================================================================
 */
void test_header_casing(void) {
  TEST_START("header casing variations");

  /* Test lowercase content-length */
  const char* request1 =
      "POST /test HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "content-length: 12\r\n"
      "\r\n"
      "Hello World!";

  parse_result_t result1;
  int status1 =
      http_parse_request((const uint8_t*)request1, strlen(request1), &result1);

  if (status1 != PARSE_STATUS_DONE) {
    FAIL("lowercase content-length should parse");
    return;
  }
  if (result1.body_length != 12) {
    FAIL("lowercase content-length not parsed");
    return;
  }

  /* Test mixed case Content-Length */
  const char* request2 =
      "POST /test HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "CoNtEnT-lEnGtH: 5\r\n"
      "\r\n"
      "12345";

  parse_result_t result2;
  int status2 =
      http_parse_request((const uint8_t*)request2, strlen(request2), &result2);

  if (status2 != PARSE_STATUS_DONE) {
    FAIL("mixed case CoNtEnT-lEnGtH should parse");
    return;
  }
  if (result2.body_length != 5) {
    FAIL("mixed case Content-Length not parsed");
    return;
  }

  /* Test with whitespace variations */
  const char* request3 =
      "POST /test HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "CONTENT-LENGTH:   8   \r\n"
      "\r\n"
      "abcdefgh";

  parse_result_t result3;
  int status3 =
      http_parse_request((const uint8_t*)request3, strlen(request3), &result3);

  if (status3 != PARSE_STATUS_DONE) {
    FAIL("uppercase CONTENT-LENGTH with whitespace should parse");
    return;
  }
  if (result3.body_length != 8) {
    FAIL("uppercase CONTENT-LENGTH not parsed");
    return;
  }

  PASS();
}

/* ============================================================================
 * Additional Tests
 * ============================================================================
 */
void test_query_string_parsing(void) {
  TEST_START("query string parsing");

  const char* request =
      "GET /search?q=test&limit=10 HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "\r\n";

  parse_result_t result;
  int status =
      http_parse_request((const uint8_t*)request, strlen(request), &result);

  if (status != PARSE_STATUS_DONE) {
    FAIL("expected PARSE_STATUS_DONE");
    return;
  }
  if (result.path_len != 7 || memcmp(result.path, "/search", 7) != 0) {
    FAIL("path should be /search without query");
    return;
  }
  if (result.query_len != 15 ||
      memcmp(result.query, "q=test&limit=10", 15) != 0) {
    FAIL("query string not parsed correctly");
    return;
  }

  PASS();
}

void test_http_version_parsing(void) {
  TEST_START("HTTP version parsing");

  const char* request =
      "GET / HTTP/1.0\r\n"
      "\r\n";

  parse_result_t result;
  int status =
      http_parse_request((const uint8_t*)request, strlen(request), &result);

  if (status != PARSE_STATUS_DONE) {
    FAIL("expected PARSE_STATUS_DONE");
    return;
  }
  if (result.http_major != 1 || result.http_minor != 0) {
    FAIL("HTTP/1.0 not parsed correctly");
    return;
  }

  PASS();
}

void test_empty_path(void) {
  TEST_START("empty path (root)");

  const char* request =
      "GET / HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "\r\n";

  parse_result_t result;
  int status =
      http_parse_request((const uint8_t*)request, strlen(request), &result);

  if (status != PARSE_STATUS_DONE) {
    FAIL("expected PARSE_STATUS_DONE");
    return;
  }
  if (result.path_len != 1 || memcmp(result.path, "/", 1) != 0) {
    FAIL("expected path = /");
    return;
  }

  PASS();
}

void test_zero_content_length(void) {
  TEST_START("zero Content-Length");

  const char* request =
      "POST /test HTTP/1.1\r\n"
      "Host: localhost\r\n"
      "Content-Length: 0\r\n"
      "\r\n";

  parse_result_t result;
  int status =
      http_parse_request((const uint8_t*)request, strlen(request), &result);

  if (status != PARSE_STATUS_DONE) {
    FAIL("expected PARSE_STATUS_DONE");
    return;
  }
  if (result.body_present != true) {
    FAIL("expected body_present = 1 (even with 0 length)");
    return;
  }
  if (result.body_length != 0) {
    FAIL("expected body_length = 0");
    return;
  }

  PASS();
}

void test_partial_headers(void) {
  TEST_START("partial headers (NEED_MORE)");

  /* Headers not complete yet */
  const char* request =
      "GET / HTTP/1.1\r\n"
      "Host: localhost\r\n"; /* Missing final CRLF */

  parse_result_t result;
  int status =
      http_parse_request((const uint8_t*)request, strlen(request), &result);

  if (status != PARSE_STATUS_NEED_MORE) {
    FAIL("expected PARSE_STATUS_NEED_MORE for incomplete headers");
    return;
  }

  PASS();
}

/* ============================================================================
 * Main
 * ============================================================================
 */
int main(void) {
  printf("============================================================\n");
  printf("  maxpress Parser Unit Tests\n");
  printf("============================================================\n\n");

  /* Run all tests */
  test_get_no_body();
  test_post_with_body_complete();
  test_post_need_more();
  test_parse_append_body();
  test_malformed_no_crlf();
  test_header_casing();

  /* Additional tests */
  test_query_string_parsing();
  test_http_version_parsing();
  test_empty_path();
  test_zero_content_length();
  test_partial_headers();

  /* Summary */
  printf("\n============================================================\n");
  printf("  Results: %d/%d tests passed\n", tests_passed, tests_run);
  printf("============================================================\n");

  return tests_failed > 0 ? 1 : 0;
}
