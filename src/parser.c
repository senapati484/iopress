/**
 * iopress HTTP/1.1 Request Parser
 *
 * Zero-copy, single-pass HTTP request parser optimized for performance.
 * Uses SIMD-optimized memchr for scanning and never allocates memory.
 *
 * @file parser.c
 * @version 1.0.0
 */

#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "server.h"

static const char* find_header_boundary(const char* buf, size_t len) {
  if (len < 4) {
    if (len >= 2 && buf[len - 2] == '\n' && buf[len - 1] == '\n') {
      return buf + len - 2;
    }
    if (len >= 2 && buf[0] == '\r' && buf[1] == '\n') {
      return buf;
    }
    return NULL;
  }
  const char* end = buf + len - 3;
  for (const char* p = buf; p < end; p++) {
    if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') {
      return p;
    }
    if (p[0] == '\n' && p[1] == '\n') {
      return p;
    }
  }
  if (buf[len - 2] == '\n' && buf[len - 1] == '\n') {
    return buf + len - 2;
  }
  return NULL;
}

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

  /* Fast path for common methods */
  bool is_get = false;
  if (result->method_len == 3 && memcmp(p, "GET", 3) == 0) {
    is_get = true;
  }

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
  const char* path_end = memchr(p, ' ', end - p);

  if (path_end == NULL) {
    /* No space found, look for CRLF */
    path_end = memchr(p, '\r', end - p);
    if (path_end == NULL) {
      path_end = memchr(p, '\n', end - p);
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

  /* Check for query string.
   * A '?' requires at least one character before it, so for a 1-byte
   * path (the common hello-world "/") skip the memchr entirely. The
   * result->query / result->query_len are pre-zeroed by the memset
   * at the top of this function, so leaving them NULL is correct. */
  if (result->path_len >= 2) {
    const char* query_start = memchr(result->path, '?', result->path_len);
    if (query_start != NULL) {
      result->query = query_start + 1;
      result->query_len = path_end - result->query;
      result->path_len = query_start - result->path;
    }
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
  if (p + 8 < end && p[0] == 'H' && p[1] == 'T' && p[2] == 'T' && p[3] == 'P' &&
      p[4] == '/') {
    result->http_major = (uint8_t)(p[5] - '0');
    result->http_minor = (uint8_t)(p[7] - '0');
    p += 8;
  }

  /* Find end of line (CRLF or LF) */
  const char* line_end = memchr(p, '\n', end - p);
  if (line_end == NULL) return PARSE_STATUS_NEED_MORE;
  p = line_end + 1;

  /* =========================================================================
   * Step 2: Find end of headers (blank line)
   * =========================================================================
   */

  const char* headers_start = p;
  const char* headers_end = NULL;

  /* Use highly optimized, single-pass boundary scanner */
  const char* blank_line = find_header_boundary(p, end - p);
  if (blank_line != NULL) {
    if (blank_line[0] == '\r' && blank_line[1] == '\n') {
      size_t remaining = end - blank_line;
      if (remaining >= 4 && blank_line[2] == '\r' && blank_line[3] == '\n') {
        headers_end = blank_line + 4;
      } else {
        headers_end = blank_line + 2;
      }
    } else {
      headers_end = blank_line + 2;
    }
  }

  if (headers_end == NULL) {
    /* Haven't found end of headers yet */
    if ((size_t)(end - headers_start) > MAX_HEADER_SIZE) {
      result->status = PARSE_STATUS_ERROR;
      result->error_code = ERROR_HEADER_TOO_LARGE;
    }
    return PARSE_STATUS_NEED_MORE;
  }

  /* =========================================================================
   * Step 2.5: Extract headers. We have headers_start..headers_end; walk
   * each line, split on first ':' into name + value, lower-case the name,
   * trim leading whitespace from value, and append to result arrays. The
   * pointers stay into the request buffer (zero-copy). Done before the
   * body-length check so binding.c can hand the JS layer a populated
   * req.headers on the slow path.
   * =========================================================================
   */
  {
    const char* hp = headers_start;
    while (hp < headers_end && result->header_count < MAX_HEADERS) {
      const char* line_end = memchr(hp, '\n', headers_end - hp);
      if (!line_end) break;
      const char* name = hp;
      const char* name_end = name;
      /* Walk to ':' or end-of-line. */
      while (name_end < line_end && *name_end != ':' && *name_end != '\r') {
        name_end++;
      }
      if (name_end == name || name_end >= line_end || *name_end != ':') {
        /* Malformed line (no colon, or empty name) — skip it. RFC 7230
         * says we should reject the request, but defensive parsing is
         * more useful for a high-perf server. */
        hp = line_end + 1;
        continue;
      }
      const char* val = name_end + 1;
      /* Trim leading whitespace and CR. */
      while (val < line_end && (*val == ' ' || *val == '\t')) val++;
      const char* val_end = line_end;
      if (val_end > val && *(val_end - 1) == '\r') val_end--;
      size_t name_len = (size_t)(name_end - name);
      if (name_len == 0 || name_len > MAX_HEADER_LINE) {
        hp = line_end + 1;
        continue;
      }
      /* Reject if a single header line is absurdly long. */
      if ((size_t)(line_end - name) > MAX_HEADER_LINE) {
        result->status = PARSE_STATUS_ERROR;
        result->error_code = ERROR_HEADER_TOO_LARGE;
        return result->status;
      }
      /* Lowercase the name in place. The buffer is mutable from the
       * server's perspective (it's the read buffer), so this is safe
       * and avoids a copy. */
      for (size_t i = 0; i < name_len; i++) {
        const char c = name[i];
        if (c >= 'A' && c <= 'Z') ((char*)name)[i] = (char)(c + ('a' - 'A'));
      }
      result->header_names[result->header_count] = name;
      result->header_name_lens[result->header_count] = name_len;
      result->header_values[result->header_count] = val;
      result->header_value_lens[result->header_count] = (size_t)(val_end - val);
      result->header_count++;
      hp = line_end + 1;
    }
  }

  result->headers_complete = true;
  result->body_start = headers_end - (const char*)buf;

  /* =========================================================================
   * Step 3: Check for body (Content-Length or chunked encoding)
   * =========================================================================
   */

  if (is_get) {
    /* GET requests rarely have bodies */
    result->body_present = false;
    result->body_length = 0;
    result->bytes_consumed = headers_end - (const char*)buf;
    result->status = PARSE_STATUS_DONE;
    return result->status;
  }

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
    result->body_present = true;
    result->chunked = true;
    result->body_length = 0;
    result->bytes_consumed = headers_end - (const char*)buf;

    size_t body_received = len - result->body_start;
    if (body_received < 5) {
      result->status = PARSE_STATUS_NEED_MORE;
      return result->status;
    }

    const uint8_t* p = (const uint8_t*)buf + result->body_start;
    const uint8_t* body_end = p + body_received;
    size_t assembled_size = 0;
    bool found_final = false;

    while (p < body_end) {
      const uint8_t* nl = memchr(p, '\n', body_end - p);
      if (!nl) break;

      size_t chunk_size = 0;
      const uint8_t* sp = p;
      while (sp < nl) {
        char c = (char)*sp;
        if (c >= '0' && c <= '9') {
          size_t digit = (size_t)(c - '0');
          if (chunk_size > (SIZE_MAX - digit) / 16) {
            result->status = PARSE_STATUS_ERROR;
            result->error_code = ERROR_BODY_TOO_LARGE;
            return result->status;
          }
          chunk_size = chunk_size * 16 + digit;
        } else if (c >= 'a' && c <= 'f') {
          size_t digit = (size_t)(c - 'a' + 10);
          if (chunk_size > (SIZE_MAX - digit) / 16) {
            result->status = PARSE_STATUS_ERROR;
            result->error_code = ERROR_BODY_TOO_LARGE;
            return result->status;
          }
          chunk_size = chunk_size * 16 + digit;
        } else if (c >= 'A' && c <= 'F') {
          size_t digit = (size_t)(c - 'A' + 10);
          if (chunk_size > (SIZE_MAX - digit) / 16) {
            result->status = PARSE_STATUS_ERROR;
            result->error_code = ERROR_BODY_TOO_LARGE;
            return result->status;
          }
          chunk_size = chunk_size * 16 + digit;
        } else if (c == '\r' || c == ';') {
          break;
        } else {
          break;
        }
        sp++;
      }

      p = nl + 1;

      if (chunk_size == 0) {
        found_final = true;
        size_t remaining = body_end - p;
        if (remaining >= 2 && p[0] == '\r' && p[1] == '\n') {
          p += 2;
        }
        break;
      }

      if (chunk_size > SIZE_MAX - 2 ||
          (size_t)(body_end - p) < chunk_size + 2) {
        result->status = PARSE_STATUS_NEED_MORE;
        return result->status;
      }

      if (assembled_size > SIZE_MAX - chunk_size) {
        result->status = PARSE_STATUS_ERROR;
        result->error_code = ERROR_BODY_TOO_LARGE;
        return result->status;
      }

      if (p[chunk_size] != '\r' || p[chunk_size + 1] != '\n') {
        result->status = PARSE_STATUS_ERROR;
        result->error_code = ERROR_MALFORMED_REQUEST;
        return result->status;
      }

      p += chunk_size + 2;
      assembled_size += chunk_size;
    }

    result->body_length = assembled_size;
    if (found_final) {
      result->bytes_consumed = (const char*)p - (const char*)buf;
    }
    result->status = found_final ? PARSE_STATUS_DONE : PARSE_STATUS_NEED_MORE;
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

int http_assemble_chunked_body(const uint8_t* buffer,
                               const parse_result_t* result, uint8_t* output,
                               size_t output_cap) {
  if (!result->chunked || !result->body_present || result->body_length == 0 ||
      output == NULL || output_cap == 0) {
    return 0;
  }

  const uint8_t* p = buffer + result->body_start;
  const uint8_t* body_end = buffer + result->bytes_consumed;
  size_t written = 0;

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

    size_t to_copy = chunk_size;
    if (written + to_copy > output_cap) {
      to_copy = output_cap - written;
    }
    memcpy(output + written, p, to_copy);
    written += to_copy;

    p += chunk_size + 2;
  }

  return (int)written;
}
