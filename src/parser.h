/**
 * iopress HTTP Parser Header
 *
 * @file parser.h
 */

#ifndef EXPRESSMAX_PARSER_H
#define EXPRESSMAX_PARSER_H

#include "server.h"

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

#ifdef __cplusplus
}
#endif

#endif /* EXPRESS_PRO_PARSER_H */
