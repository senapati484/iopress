/**
 * iopress HTTP Parser Header
 *
 * @file parser.h
 */

#ifndef EXPRESSMAX_PARSER_H
#define EXPRESSMAX_PARSER_H

#include "server.h"

/* Parsing status codes */
#define PARSE_STATUS_DONE 0
#define PARSE_STATUS_NEED_MORE 1
#define PARSE_STATUS_ERROR 2

#define MAX_HEADER_SIZE 32768

/* Hard cap on header count per request. RFC 7230 doesn't mandate a
 * limit, but 128 is more than any reasonable client needs and protects
 * the parser from a hostile request with thousands of headers. */
#define MAX_HEADERS 128
/* Per-header name+value length cap. Same rationale. */
#define MAX_HEADER_LINE 8192

typedef struct parse_result_s {
  int status;
  int error_code;
  const char* method;
  size_t method_len;
  const char* path;
  size_t path_len;
  const char* query;
  size_t query_len;
  uint8_t http_major;
  uint8_t http_minor;
  bool headers_complete;
  size_t body_start;
  bool body_present;
  size_t body_length;
  size_t bytes_consumed;
  bool chunked;
  /* Parsed headers: arrays of name/value pointers and lengths, parallel.
   * All point into the request buffer (zero-copy). header_count entries.
   * Lowercased on parse so consumers can do case-insensitive lookup with
   * a single string compare. Valid only when status == PARSE_STATUS_DONE
   * for the request that finishes header parsing. */
  const char* header_names[MAX_HEADERS];
  size_t header_name_lens[MAX_HEADERS];
  const char* header_values[MAX_HEADERS];
  size_t header_value_lens[MAX_HEADERS];
  size_t header_count;
} parse_result_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parse an HTTP request from buffer.
 *
 * @param buffer Raw request data
 * @param len Buffer length
 * @param result Output parse result
 * @return Parse status code: PARSE_STATUS_DONE, PARSE_STATUS_NEED_MORE, or
 * PARSE_STATUS_ERROR
 */
int http_parse_request(const uint8_t* buffer, size_t len,
                       parse_result_t* result);

/**
 * Append new body data and check if complete.
 *
 * @param conn Connection state
 * @param new_data Pointer to new data
 * @param new_len Length of new data
 * @param result Updated parse result
 * @return Parse status code
 */
int parse_append_body(connection_t* conn, const uint8_t* new_data,
                      size_t new_len, parse_result_t* result);

int http_assemble_chunked_body(const uint8_t* buffer,
                               const parse_result_t* result, uint8_t* output,
                               size_t output_cap);

#ifdef __cplusplus
}
#endif

#endif /* EXPRESS_PRO_PARSER_H */
