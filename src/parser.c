/**
 * expressmax HTTP/1.1 Request Parser
 *
 * Zero-copy, single-pass HTTP request parser optimized for performance.
 * Uses SIMD-optimized memchr for scanning and never allocates memory.
 *
 * @file parser.c
 * @version 1.0.0
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "server.h"

/* Suppress unused function warning for fallback implementation */
#if defined(__APPLE__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif

/* xp_memmem is a GNU extension, not in POSIX.1-2001 */
/* Provide our own implementation to avoid conflicts */
static void* xp_memmem(const void* haystack, size_t haystack_len,
                       const void* needle, size_t needle_len) {
  if (needle_len == 0) return (void*)haystack;
  if (haystack_len < needle_len) return NULL;

  const char* h = haystack;
  const char* n = needle;

  for (size_t i = 0; i <= haystack_len - needle_len; i++) {
    if (memcmp(h + i, n, needle_len) == 0) {
      return (void*)(h + i);
    }
  }
  return NULL;
}

#if defined(__APPLE__)
#pragma clang diagnostic pop
#endif

/* ============================================================================
 * Error Codes
 * ============================================================================
 */

#define ERROR_NONE 0
#define ERROR_METHOD_TOO_LONG 1
#define ERROR_URI_TOO_LONG 2
#define ERROR_VERSION_INVALID 3
#define ERROR_HEADER_TOO_LARGE 4
#define ERROR_MALFORMED_REQUEST 5
#define ERROR_INVALID_CONTENT_LENGTH 6
#define ERROR_BODY_TOO_LARGE 7

/* ============================================================================
 * Constants
 * ============================================================================
 */

#define MAX_METHOD_LENGTH 16
#define MAX_URI_LENGTH 8192

/* ============================================================================
 * Helper Functions
 * ============================================================================
 */

/**
 * Case-insensitive string compare (limited length).
 * Used for header name comparison.
 */
static int xp_strncasecmp_l(const char* s1, const char* s2, size_t n) {
  while (n > 0) {
    char c1 = (char)tolower((unsigned char)*s1);
    char c2 = (char)tolower((unsigned char)*s2);
    if (c1 != c2) return c1 - c2;
    if (c1 == '\0') return 0;
    s1++;
    s2++;
    n--;
  }
  return 0;
}

/**
 * Parse a size_t from string with bounds checking.
 */
static int parse_size_t(const char* str, size_t len, size_t* out) {
  size_t value = 0;
  for (size_t i = 0; i < len; i++) {
    if (!isdigit((unsigned char)str[i])) return -1;
    size_t digit = str[i] - '0';
    /* Check for overflow */
    if (value > (SIZE_MAX - digit) / 10) return -1;
    value = value * 10 + digit;
  }
  *out = value;
  return 0;
}

/**
 * Find Content-Length header value.
 * Returns pointer to value and sets value_len, or NULL if not found.
 */
static const char* find_content_length(const char* headers_start,
                                       const char* headers_end,
                                       size_t* value_len) {
  const char* p = headers_start;
  const char* content_length_name = "content-length";
  size_t name_len = strlen(content_length_name);

  while (p < headers_end) {
    /* Find end of this header line */
    const char* line_end = memchr(p, '\n', headers_end - p);
    if (line_end == NULL) break;

    /* Check if this is Content-Length header */
    if (line_end - p >= (ptrdiff_t)name_len) {
      /* Skip leading whitespace */
      while (p < line_end && isspace((unsigned char)*p)) p++;

      if (xp_strncasecmp_l(p, content_length_name, name_len) == 0) {
        /* Found it - move past header name */
        const char* val = p + name_len;

        /* Skip whitespace and colon */
        while (val < line_end &&
               (isspace((unsigned char)*val) || *val == ':')) {
          val++;
        }

        /* Find end of value (trim trailing whitespace) */
        const char* val_end = line_end;
        while (val_end > val && (*(val_end - 1) == '\r' ||
                                 isspace((unsigned char)*(val_end - 1)))) {
          val_end--;
        }

        *value_len = val_end - val;
        return val;
      }
    }

    /* Move to next line */
    p = line_end + 1;
  }

  return NULL;
}

/* ============================================================================
 * Public API
 * ============================================================================
 */

/**
 * Parse an HTTP request from buffer.
 *
 * This is a zero-copy parser - all pointers in the result point into
 * the provided buffer. Uses SIMD-optimized memchr for fast scanning.
 *
 * @param buffer Raw request data
 * @param len Buffer length
 * @param result Output parse result (zero-copy pointers into buffer)
 * @return Parse status code: PARSE_STATUS_DONE, PARSE_STATUS_NEED_MORE, or
 * PARSE_STATUS_ERROR
 */
int http_parse_request(const uint8_t* buffer, size_t len,
                       parse_result_t* result) {
  /* Initialize result to zero */
  memset(result, 0, sizeof(*result));
  result->status = PARSE_STATUS_NEED_MORE;
  result->http_major = 1;
  result->http_minor = 1;

  if (len == 0) {
    return PARSE_STATUS_NEED_MORE;
  }

  const char* buf = (const char*)buffer;
  const char* end = buf + len;
  const char* p = buf;

  /* =========================================================================
   * Step 1: Parse request line
   * Format: METHOD SP PATH [SP HTTP/VERSION] CRLF
   * =========================================================================
   */

  /* Find first space (end of method) */
  const char* method_end = memchr(p, ' ', end - p);
  if (method_end == NULL) {
    /* No space found - check if method is too long */
    if ((size_t)(end - p) > MAX_METHOD_LENGTH) {
      result->status = PARSE_STATUS_ERROR;
      result->error_code = ERROR_METHOD_TOO_LONG;
    }
    return result->status;
  }

  result->method = p;
  result->method_len = method_end - p;

  if (result->method_len == 0 || result->method_len > MAX_METHOD_LENGTH) {
    result->status = PARSE_STATUS_ERROR;
    result->error_code = ERROR_METHOD_TOO_LONG;
    return result->status;
  }

  /* Move past space */
  p = method_end + 1;
  if (p >= end) return PARSE_STATUS_NEED_MORE;

  /* Skip additional spaces (allowing for multiple SP) */
  while (p < end && *p == ' ') p++;
  if (p >= end) return PARSE_STATUS_NEED_MORE;

  /* Find next space or end of line (end of path) */
  const char* path_start = p;
  const char* path_end = NULL;

  /* Look for space or end of line */
  for (const char* q = p; q < end; q++) {
    if (*q == ' ' || *q == '\r' || *q == '\n') {
      path_end = q;
      break;
    }
  }

  if (path_end == NULL) {
    /* No terminator found - check if URI is too long */
    if ((size_t)(end - path_start) > MAX_URI_LENGTH) {
      result->status = PARSE_STATUS_ERROR;
      result->error_code = ERROR_URI_TOO_LONG;
    }
    return PARSE_STATUS_NEED_MORE;
  }

  result->path = path_start;
  result->path_len = path_end - path_start;

  if (result->path_len == 0 || result->path_len > MAX_URI_LENGTH) {
    result->status = PARSE_STATUS_ERROR;
    result->error_code = ERROR_URI_TOO_LONG;
    return result->status;
  }

  /* Check for query string */
  const char* query_start = memchr(path_start, '?', result->path_len);
  if (query_start != NULL) {
    result->query = query_start + 1;
    result->query_len = path_end - result->query;
    result->path_len = query_start - path_start;
  }

  /* Move to HTTP version or end of line */
  p = path_end;

  /* Skip space if present */
  if (p < end && *p == ' ') {
    p++;
    /* Skip additional spaces */
    while (p < end && *p == ' ') p++;
  }

  /* Check for HTTP version */
  if (p + 8 < end && xp_strncasecmp_l(p, "HTTP/", 5) == 0 &&
      isdigit((unsigned char)p[5]) && p[6] == '.' &&
      isdigit((unsigned char)p[7])) {
    result->http_major = (uint8_t)(p[5] - '0');
    result->http_minor = (uint8_t)(p[7] - '0');

    /* Move past version */
    p += 8;
  }

  /* Find end of line (CRLF or LF) */
  const char* line_end = NULL;
  if (p < end) {
    if (*p == '\r' && p + 1 < end && *(p + 1) == '\n') {
      /* CRLF */
      line_end = p + 2;
    } else if (*p == '\n') {
      /* Bare LF */
      line_end = p + 1;
    } else if (*p == '\r') {
      /* Need more data to check for LF */
      return PARSE_STATUS_NEED_MORE;
    }
  }

  if (line_end == NULL) {
    return PARSE_STATUS_NEED_MORE;
  }

  p = line_end;

  /* =========================================================================
   * Step 2: Find end of headers (blank line)
   * =========================================================================
   */

  const char* headers_start = p;
  const char* headers_end = NULL;

  /* Use memchr to find each line ending */
  while (p < end) {
    /* Find next newline */
    const char* nl = memchr(p, '\n', end - p);
    if (nl == NULL) {
      /* No newline found - need more data */
      return PARSE_STATUS_NEED_MORE;
    }

    /* Check if this is a blank line (end of headers) */
    if (nl > p && *(nl - 1) == '\r') {
      /* CRLF ending - check if line is empty (CRLF at start) */
      if (nl - 1 == p) {
        headers_end = nl + 1;
        break;
      }
    } else {
      /* Bare LF - check if at start */
      if (nl == p) {
        headers_end = nl + 1;
        break;
      }
    }

    /* Check header size limit */
    if ((size_t)(nl - headers_start) > MAX_HEADER_SIZE) {
      result->status = PARSE_STATUS_ERROR;
      result->error_code = ERROR_HEADER_TOO_LARGE;
      return result->status;
    }

    /* Move to next line */
    p = nl + 1;
  }

  if (headers_end == NULL) {
    /* Haven't found end of headers yet */
    if ((size_t)(end - headers_start) > MAX_HEADER_SIZE) {
      result->status = PARSE_STATUS_ERROR;
      result->error_code = ERROR_HEADER_TOO_LARGE;
    }
    return PARSE_STATUS_NEED_MORE;
  }

  result->headers_complete = true;
  result->body_start = headers_end - (const char*)buf;

  /* =========================================================================
   * Step 3: Check for body (Content-Length or chunked encoding)
   * =========================================================================
   */

  /* Check for chunked encoding first */
  /* (Simplified - just check for Transfer-Encoding: chunked) */
  bool chunked = false;
  const char* te_start = headers_start;
  while (te_start < headers_end) {
    if (xp_strncasecmp_l(te_start, "transfer-encoding", 17) == 0) {
      /* Found Transfer-Encoding header */
      const char* colon = memchr(te_start, ':', headers_end - te_start);
      if (colon != NULL) {
        const char* val = colon + 1;
        while (val < headers_end && isspace((unsigned char)*val)) val++;
        if (xp_strncasecmp_l(val, "chunked", 7) == 0) {
          chunked = true;
          break;
        }
      }
    }
    /* Move to next line */
    const char* nl = memchr(te_start, '\n', headers_end - te_start);
    if (nl == NULL) break;
    te_start = nl + 1;
  }

  if (chunked) {
    /* B9: Chunked encoding - check if complete chunked body received */
    result->body_present = true;
    result->body_length = 0;
    result->bytes_consumed = headers_end - (const char*)buf;

    size_t body_received = len - result->body_start;
    if (body_received < 5) {
      result->status = PARSE_STATUS_NEED_MORE;
      return result->status;
    }

    const char* body_start = (const char*)buf + result->body_start;

    /* Check for final chunk terminator: "0\r\n\r\n" */
    const char* term = xp_memmem(body_start, body_received, "0\r\n\r\n", 5);
    if (term == NULL) {
      result->status = PARSE_STATUS_NEED_MORE;
      return result->status;
    }

    /* Verify there's at least one chunk before the final 0 */
    const char* first_crlf = memchr(body_start, '\r', body_received);
    if (first_crlf == NULL || first_crlf >= term) {
      result->status = PARSE_STATUS_NEED_MORE;
      return result->status;
    }

    /* Calculate total assembled size by parsing all chunks */
    size_t assembled_size = 0;
    const uint8_t* p = (const uint8_t*)body_start;
    const uint8_t* body_end = (const uint8_t*)body_start + body_received;

    while (p < body_end) {
      const uint8_t* nl = memchr(p, '\n', body_end - p);
      if (!nl) break;

      size_t chunk_size = 0;
      const uint8_t* sp = p;
      while (sp < nl) {
        char c = (char)*sp;
        if (c >= '0' && c <= '9')
          chunk_size = chunk_size * 16 + (c - '0');
        else if (c >= 'a' && c <= 'f')
          chunk_size = chunk_size * 16 + (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
          chunk_size = chunk_size * 16 + (c - 'A' + 10);
        else if (c == '\r' || c == ';')
          break;
        else
          break;
        sp++;
      }

      p = nl + 1;
      if (chunk_size == 0) break;
      p += chunk_size + 2;
      assembled_size += chunk_size;
    }

    result->body_length = assembled_size;
    result->status = PARSE_STATUS_DONE;
    return result->status;
  }

  /* Look for Content-Length header */
  size_t content_length_len = 0;
  const char* content_length_val =
      find_content_length(headers_start, headers_end, &content_length_len);

  if (content_length_val != NULL) {
    /* Parse Content-Length value */
    size_t content_length = 0;
    if (parse_size_t(content_length_val, content_length_len, &content_length) !=
        0) {
      result->status = PARSE_STATUS_ERROR;
      result->error_code = ERROR_INVALID_CONTENT_LENGTH;
      return result->status;
    }

    result->body_present = true;
    result->body_length = content_length;

    /* Check if we have the full body */
    size_t body_received = len - result->body_start;

    if (body_received >= content_length) {
      /* FAST PATH: Full body received */
      result->bytes_consumed = result->body_start + content_length;
      result->status = PARSE_STATUS_DONE;
    } else {
      /* SLOW PATH: Need more body data */
      result->bytes_consumed = len;
      result->status = PARSE_STATUS_NEED_MORE;
    }
  } else {
    /* No body expected */
    result->body_present = false;
    result->body_length = 0;
    result->bytes_consumed = headers_end - (const char*)buf;
    result->status = PARSE_STATUS_DONE;
  }

  return result->status;
}

/**
 * Append new body data and check if complete.
 *
 * This function is called when more data arrives for a request
 * that returned PARSE_STATUS_NEED_MORE. It updates the connection
 * state without re-parsing headers.
 *
 * @param conn Connection state (updated in place)
 * @param new_data Pointer to new data received
 * @param new_len Length of new data
 * @param result Updated parse result
 * @return Parse status code
 */
int parse_append_body(connection_t* conn, const uint8_t* new_data,
                      size_t new_len, parse_result_t* result) {
  (void)new_data; /* New data is already in conn->buffer */
  (void)new_len;  /* Data is already in conn->buffer, len not needed */

  /* Copy current result state */
  memset(result, 0, sizeof(*result));
  result->status =
      conn->request_complete ? PARSE_STATUS_DONE : PARSE_STATUS_NEED_MORE;

  if (conn->body_remaining == 0) {
    /* Body already complete */
    result->status = PARSE_STATUS_DONE;
    return result->status;
  }

  /* Calculate how much body we now have */
  size_t body_start = conn->buffer_pos; /* Position where body starts */
  size_t total_len = conn->buffer_len;

  if (total_len <= body_start) {
    /* No body data yet */
    return PARSE_STATUS_NEED_MORE;
  }

  size_t body_received = total_len - body_start;

  if (body_received >= conn->body_remaining) {
    /* Body complete */
    result->body_present = true;
    result->body_start = body_start;
    result->body_length = conn->body_remaining;
    result->bytes_consumed = body_start + conn->body_remaining;
    result->status = PARSE_STATUS_DONE;
    conn->request_complete = true;
    conn->body_remaining = 0;
  } else {
    /* Still need more */
    result->body_present = true;
    result->body_start = body_start;
    result->body_length = conn->body_remaining;
    result->bytes_consumed = total_len;
    result->status = PARSE_STATUS_NEED_MORE;
  }

  return result->status;
}
