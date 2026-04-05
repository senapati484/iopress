/**
 * iopress HTTP/2 Support
 *
 * Simple HTTP/2 server implementation for high-performance serving.
 * Uses HPACK for header compression and stream multiplexing.
 *
 * @file http2.c
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef HTTP2_H
#define HTTP2_H

#define HTTP2_MAX_FRAME_SIZE 16384
#define HTTP2_INITIAL_WINDOW 65535
#define HTTP2_MAX_CONCURRENT_STREAMS 100

/* HTTP/2 Frame Types */
#define HTTP2_FRAME_DATA 0x00
#define HTTP2_FRAME_HEADERS 0x01
#define HTTP2_FRAME_SETTINGS 0x04
#define HTTP2_FRAME_PING 0x06
#define HTTP2_FRAME_GOAWAY 0x07
#define HTTP2_FRAME_WINDOW_UPDATE 0x08

/* HTTP/2 Flags */
#define HTTP2_FLAG_END_STREAM 0x01
#define HTTP2_FLAG_END_HEADERS 0x04
#define HTTP2_FLAG_PADDED 0x08
#define HTTP2_FLAG_ACK 0x01

/* HTTP/2 Settings */
#define HTTP2_SETTINGS_HEADER_TABLE_SIZE 0x01
#define HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS 0x03
#define HTTP2_SETTINGS_INITIAL_WINDOW_SIZE 0x04
#define HTTP2_SETTINGS_MAX_FRAME_SIZE 0x05

/* HTTP/2 Stream States */
typedef enum {
  HTTP2_STREAM_IDLE,
  HTTP2_STREAM_OPEN,
  HTTP2_STREAM_HALF_CLOSED_LOCAL,
  HTTP2_STREAM_HALF_CLOSED_REMOTE,
  HTTP2_STREAM_CLOSED
} http2_stream_state_t;

/* HTTP/2 Stream */
typedef struct {
  uint32_t stream_id;
  http2_stream_state_t state;
  uint8_t* headers;
  size_t headers_len;
  uint8_t* body;
  size_t body_len;
  size_t body_sent;
} http2_stream_t;

/* HTTP/2 Connection */
typedef struct {
  int fd;
  http2_stream_t* streams[HTTP2_MAX_CONCURRENT_STREAMS];
  int stream_count;
  uint32_t last_stream_id;
  uint32_t settings[16];
  uint32_t local_window;
  uint32_t remote_window;
} http2_conn_t;

/* Build HTTP/2 SETTINGS frame */
static void http2_build_settings(uint8_t* out, size_t* out_len, bool ack) {
  out[0] = 0x00;
  out[1] = 0x00;
  out[2] = ack ? 0x00 : 0x06;
  out[3] = HTTP2_FRAME_SETTINGS;
  out[4] = 0x00;
  out[5] = 0x00;
  out[6] = 0x00;
  out[7] = 0x00;

  if (!ack) {
    out[9] = 0x00;
    out[10] = HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS;
    out[11] = 0x00;
    out[12] = HTTP2_MAX_CONCURRENT_STREAMS >> 8;
    out[13] = HTTP2_MAX_CONCURRENT_STREAMS & 0xFF;
  }

  *out_len = ack ? 9 : 14;
}

/* Build HTTP/2 PING frame */
static void http2_build_ping(uint8_t* out, size_t* out_len, bool ack) {
  out[0] = 0x00;
  out[1] = 0x00;
  out[2] = 0x08;
  out[3] = HTTP2_FRAME_PING;
  out[4] = ack ? HTTP2_FLAG_ACK : 0x00;
  out[5] = 0x00;
  out[6] = 0x00;
  out[7] = 0x00;
  out[8] = 0x00;

  if (!ack) {
    memset(out + 9, 0, 8);
  }

  *out_len = 17;
}

/* Build HTTP/2 WINDOW_UPDATE frame */
static void http2_build_window_update(uint8_t* out, size_t* out_len,
                                      uint32_t increment) {
  out[0] = 0x00;
  out[1] = 0x00;
  out[2] = 0x04;
  out[3] = HTTP2_FRAME_WINDOW_UPDATE;
  out[4] = 0x00;
  out[5] = 0x00;
  out[6] = 0x00;
  out[7] = 0x00;
  out[8] = (increment >> 24) & 0xFF;
  out[9] = (increment >> 16) & 0xFF;
  out[10] = (increment >> 8) & 0xFF;
  out[11] = increment & 0xFF;

  *out_len = 13;
}

/* Simple HPACK encoder (static table only, no dynamic) */
static size_t http2_encode_integer(uint8_t* out, uint32_t value,
                                   int prefix_bits) {
  size_t i = 0;
  uint32_t mask = (1 << prefix_bits) - 1;

  if (value < mask) {
    out[i++] = value & mask;
  } else {
    out[i++] = mask;
    value -= mask;
    while (value >= 128) {
      out[i++] = (value & 0x7F) | 0x80;
      value >>= 7;
    }
    out[i++] = value;
  }

  return i;
}

/* Build HTTP/2 HEADERS frame with simple pseudo-headers */
static size_t http2_build_response(uint8_t* out, uint16_t status,
                                   const uint8_t* body, size_t body_len,
                                   const char* content_type) {
  size_t pos = 0;
  uint8_t* p = out;

  /* Frame header will be filled at the end */
  p += 9;
  pos += 9;

  /* Encode pseudo-headers: :status, :method, :path, content-type */
  const char* status_str;
  switch (status) {
    case 200:
      status_str = "200";
      break;
    case 201:
      status_str = "201";
      break;
    case 204:
      status_str = "204";
      break;
    case 301:
      status_str = "301";
      break;
    case 302:
      status_str = "302";
      break;
    case 400:
      status_str = "400";
      break;
    case 401:
      status_str = "401";
      break;
    case 403:
      status_str = "403";
      break;
    case 404:
      status_str = "404";
      break;
    case 500:
      status_str = "500";
      break;
    default:
      status_str = "200";
  }

  /* :status */
  *p++ = 0x88; /* Indexed header field, index 8 for :status */
  p += http2_encode_integer(p, status, 7);
  memcpy(p, status_str, 3);
  p += 3;
  pos += 2 + 3;

  if (content_type) {
    /* content-type */
    *p++ = 0x4F; /* Indexed, index 31 */
    pos++;
  }

  /* content-length */
  if (body_len > 0) {
    *p++ = 0x1F; /* Indexed, index 63 */
    p += http2_encode_integer(p, body_len, 5);
    char lenstr[16];
    sprintf(lenstr, "%zu", body_len);
    size_t lenlen = strlen(lenstr);
    memcpy(p, lenstr, lenlen);
    p += lenlen;
    pos += 2 + lenlen;
  }

  /* pad length = 0 */
  *p++ = 0x00;
  pos++;

  /* END_HEADERS flag */
  out[4] = HTTP2_FLAG_END_HEADERS;

  /* Fill frame length */
  size_t frame_len = p - out - 9;
  out[0] = (frame_len >> 16) & 0xFF;
  out[1] = (frame_len >> 8) & 0xFF;
  out[2] = frame_len & 0xFF;

  /* Stream ID = 1 */
  out[5] = 0x00;
  out[6] = 0x00;
  out[7] = 0x00;
  out[8] = 0x01;

  /* Add DATA frame if body present */
  if (body_len > 0 && body) {
    out[3] = HTTP2_FRAME_DATA;

    /* DATA frame header */
    out[frame_len + 9] = (body_len >> 16) & 0xFF;
    out[frame_len + 10] = (body_len >> 8) & 0xFF;
    out[frame_len + 11] = body_len & 0xFF;
    out[frame_len + 12] = HTTP2_FRAME_DATA;
    out[frame_len + 13] = HTTP2_FLAG_END_STREAM;
    out[frame_len + 14] = 0x00;
    out[frame_len + 15] = 0x00;
    out[frame_len + 16] = 0x00;
    out[frame_len + 17] = 0x01;

    memcpy(out + frame_len + 18, body, body_len);

    return frame_len + 18 + body_len;
  }

  return p - out;
}

#endif /* HTTP2_H */
