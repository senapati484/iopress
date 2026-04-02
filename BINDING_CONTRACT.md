# Express-Pro N-API Binding Contract

> Version: 1.0.0
> Last Updated: 2026-04-01

This document defines the complete contract between the JavaScript layer and the native C implementation. Any engineer implementing `binding.c` should be able to do so using only this document.

---

## Table of Contents

1. [Overview](#overview)
2. [Exported Functions](#exported-functions)
3. [Threading Model](#threading-model)
4. [Threadsafe Function Lifecycle](#threadsafe-function-lifecycle)
5. [Error Handling](#error-handling)
6. [Data Structures](#data-structures)

---

## Overview

The native addon exports a single module object with 5 methods. All asynchronous operations use `napi_threadsafe_function` to safely call JavaScript callbacks from C worker threads.

### Module Export Structure

```javascript
// JS usage
const native = require('./build/Release/express_pro_native');

native.RegisterRoute(method, path, handlerRef);
native.Listen(port, host, onRequestCallback, onErrorCallback);
native.SendResponse(requestId, statusCode, headers, body);
native.SetServerOptions(options);
native.StreamData(requestId, chunk, isLast);
```

---

## Exported Functions

### 1. RegisterRoute

Registers a route handler for incoming HTTP requests.

#### JavaScript Call Signature

```javascript
RegisterRoute(method: string, path: string, handlerRef: number): void
```

#### C Function Signature

```c
napi_value RegisterRoute(napi_env env, napi_callback_info info);
```

#### Arguments

| Position | Name | Type | Required | Validation |
|----------|------|------|----------|------------|
| 0 | `method` | `string` | Yes | Must be one of: `"GET"`, `"POST"`, `"PUT"`, `"DELETE"`, `"PATCH"`, `"HEAD"`, `"OPTIONS"` |
| 1 | `path` | `string` | Yes | Must start with `/`. Supports `/:param` and `/*` patterns |
| 2 | `handlerRef` | `number` | Yes | Positive integer referencing a JS function stored in a registry |

#### Return Value

- **Success**: `undefined`
- **Failure**: Throws `TypeError` for invalid arguments

#### Threading Constraint

**MAIN THREAD ONLY** - Must be called synchronously from JavaScript.

#### Error Codes

| Error | Condition |
|-------|-----------|
| `TypeError` | `method` is not a string or invalid HTTP method |
| `TypeError` | `path` is not a string or invalid format |
| `RangeError` | `handlerRef` is not a positive integer |
| `Error` | Route already exists (duplicate method+path) |

---

### 2. Listen

Starts the HTTP server and establishes the event loop.

#### JavaScript Call Signature

```javascript
Listen(
  port: number,
  host: string,
  onRequest: function,
  onError: function
): ServerHandle
```

#### C Function Signature

```c
napi_value Listen(napi_env env, napi_callback_info info);
```

#### Arguments

| Position | Name | Type | Required | Validation |
|----------|------|------|----------|------------|
| 0 | `port` | `number` | Yes | Integer, 1-65535 |
| 1 | `host` | `string` | Yes | Valid IP address or hostname |
| 2 | `onRequest` | `function` | Yes | Callback for incoming requests |
| 3 | `onError` | `function` | Yes | Callback for server errors |

#### Return Value

- **Success**: `ServerHandle` object with properties:
  - `port: number` - Actual port bound (may differ if 0 was passed)
  - `host: string` - Bound host address
  - `backend: string` - Async I/O backend used ("io_uring", "kqueue", "iocp", "libuv")
  - `stop: function` - Method to stop the server
- **Failure**: Throws `Error` with message

#### Threading Constraint

**MAIN THREAD ONLY** for initial call. Creates `napi_threadsafe_function` instances that allow C threads to invoke `onRequest` and `onError` callbacks.

#### Error Codes

| Error | Condition |
|-------|-----------|
| `RangeError` | `port` out of valid range |
| `TypeError` | `host` is not a string |
| `TypeError` | `onRequest` or `onError` is not a function |
| `Error` | Address already in use (EADDRINUSE) |
| `Error` | Permission denied (EACCES) - binding to privileged port |
| `Error` | Platform-specific async I/O initialization failure |

---

### 3. SendResponse

Sends an HTTP response for a specific request.

#### JavaScript Call Signature

```javascript
SendResponse(
  requestId: number,
  statusCode: number,
  headers: object,
  body: Buffer | string | null
): boolean
```

#### C Function Signature

```c
napi_value SendResponse(napi_env env, napi_callback_info info);
```

#### Arguments

| Position | Name | Type | Required | Validation |
|----------|------|------|----------|------------|
| 0 | `requestId` | `number` | Yes | Positive 64-bit integer from request event |
| 1 | `statusCode` | `number` | Yes | Integer, 100-599 |
| 2 | `headers` | `object` | Yes | Plain object with string values |
| 3 | `body` | `Buffer \| string \| null` | No | Response body. `null` for no body |

#### Return Value

- **Success**: `true` if response was queued successfully
- **Failure**: `false` if request ID is invalid/expired

#### Threading Constraint

**MAIN THREAD ONLY** - Must be called from JavaScript callback context.

#### Error Codes

| Error | Condition |
|-------|-----------|
| `RangeError` | `requestId` is not a valid positive integer |
| `RangeError` | `statusCode` out of valid HTTP range |
| `TypeError` | `headers` is not a plain object |
| `TypeError` | Header values are not strings |
| `Error` | Request already responded (duplicate call) |
| `Error` | Connection closed before response |

---

### 4. SetServerOptions

Configures server-wide options before calling `Listen`.

#### JavaScript Call Signature

```javascript
SetServerOptions(options: {
  initialBufferSize?: number,  // default: 16384
  maxBodySize?: number,        // default: 1048576 (1MB)
  streamBody?: boolean,        // default: false
  keepAliveTimeout?: number,    // default: 5000 (ms)
  requestTimeout?: number       // default: 30000 (ms)
}): void
```

#### C Function Signature

```c
napi_value SetServerOptions(napi_env env, napi_callback_info info);
```

#### Arguments

| Position | Name | Type | Required | Validation |
|----------|------|------|----------|------------|
| 0 | `options` | `object` | Yes | Configuration object |

#### Options Object

| Property | Type | Default | Validation |
|----------|------|---------|------------|
| `initialBufferSize` | `number` | 16384 | >= 1024, <= 1048576 |
| `maxBodySize` | `number` | 1048576 | >= 0 (0 = unlimited) |
| `streamBody` | `boolean` | false | - |
| `keepAliveTimeout` | `number` | 5000 | >= 0 |
| `requestTimeout` | `number` | 30000 | >= 0 |

#### Return Value

- **Success**: `undefined`
- **Failure**: Throws `TypeError` or `RangeError`

#### Threading Constraint

**MAIN THREAD ONLY** - Must be called before `Listen`.

#### Error Codes

| Error | Condition |
|-------|-----------|
| `TypeError` | `options` is not an object |
| `RangeError` | Any option value out of valid range |
| `Error` | Called after `Listen` (server already started) |

---

### 5. StreamData

Streams a chunk of data for a response (used when `streamBody: true`).

#### JavaScript Call Signature

```javascript
StreamData(
  requestId: number,
  chunk: Buffer | string,
  isLast: boolean
): boolean
```

#### C Function Signature

```c
napi_value StreamData(napi_env env, napi_callback_info info);
```

#### Arguments

| Position | Name | Type | Required | Validation |
|----------|------|------|----------|------------|
| 0 | `requestId` | `number` | Yes | Valid request ID from request event |
| 1 | `chunk` | `Buffer \| string` | Yes | Data chunk to stream |
| 2 | `isLast` | `boolean` | Yes | `true` if this is the final chunk |

#### Return Value

- **Success**: `true` if chunk was queued
- **Failure**: `false` if backpressure is active or connection closed

#### Threading Constraint

**MAIN THREAD ONLY** - Must be called from JavaScript callback context.

#### Error Codes

| Error | Condition |
|-------|-----------|
| `RangeError` | `requestId` invalid |
| `TypeError` | `chunk` is not Buffer or string |
| `TypeError` | `isLast` is not boolean |
| `Error` | Called on non-streaming response |
| `Error` | Connection closed |

---

## Threading Model

### Thread Safety Matrix

| Function | Main Thread | C Worker Thread | Notes |
|----------|-------------|-----------------|-------|
| `RegisterRoute` | ✅ | ❌ | Must be called before `Listen` |
| `Listen` | ✅ | ❌ | Creates threadsafe functions |
| `SendResponse` | ✅ | ❌ | Called from JS callback only |
| `SetServerOptions` | ✅ | ❌ | Must be called before `Listen` |
| `StreamData` | ✅ | ❌ | Called from JS callback only |

### C Callbacks to JS

The following events are sent from C worker threads to JS via `napi_threadsafe_function`:

1. **OnRequest**: New HTTP request received
2. **OnError**: Server-level error occurred
3. **OnStreamData**: Body data chunk available (when streaming)

---

## Threadsafe Function Lifecycle

### Creation

Threadsafe functions are created during `Listen`:

```c
// Pseudocode for threadsafe function creation
napi_status status = napi_create_threadsafe_function(
    env,
    js_callback,           // The JS function passed to Listen
    NULL,                  // async_resource (optional)
    async_name,            // Resource name for async_hooks
    0,                     // max_queue_size (0 = unlimited)
    1,                     // initial_thread_count
    NULL,                  // thread_finalize_data
    NULL,                  // thread_finalize_cb
    context,               // context (passed to call_js)
    CallJsCallback,        // call_js function
    &threadsafe_function  // OUT: threadsafe function handle
);
```

### Reference Counting

- **Acquire**: `napi_ref_threadsafe_function()` when starting to listen
- **Release**: `napi_release_threadsafe_function()` when server stops

### Threadsafe Call Pattern

```c
// From C worker thread
napi_call_threadsafe_function(
    threadsafe_function,
    call_data,      // Data to pass to JS callback
    napi_tsfn_nonblocking
);
```

### Lifecycle Diagram

```
Listen()
   │
   ├── Create napi_threadsafe_function (onRequest)
   ├── Create napi_threadsafe_function (onError)
   │
   ├── Start event loop (C worker thread)
   │      │
   │      ├── Request received ──▶ napi_call_threadsafe_function()
   │      ├── Error occurred ────▶ napi_call_threadsafe_function()
   │      │
   │      └── ... (loop until stop)
   │
   └── ServerHandle.stop() called
          │
          ├── Signal event loop to stop
          ├── Wait for pending callbacks
          ├── napi_release_threadsafe_function(onRequest)
          └── napi_release_threadsafe_function(onError)
```

### Important Notes

1. **Queue Size**: Default unlimited (0). If requests arrive faster than JS can process, memory will grow.
2. **Non-blocking**: `napi_tsfn_nonblocking` is used. If queue is full, call returns immediately.
3. **Cleanup**: Always release threadsafe functions before closing the event loop to prevent memory leaks.
4. **Thread Count**: Start with 1, can increase for multi-threaded acceptors.

---

## Error Handling

The error handling strategy in Express-Pro is designed to **prevent any unhandled exception from crashing the Node process** while providing clear, actionable error information to JavaScript.

### Design Principles

1. **No silent failures** - All errors are either thrown synchronously or passed to callbacks
2. **Categorize by severity** - Fatal errors vs. per-request errors have different handling paths
3. **Automatic recovery** - Common HTTP errors (413 payload too large) generate responses without JS involvement
4. **Process safety** - N-API exception handling ensures JS handler throws don't crash the process

---

### Error Severity Levels

| Level | C Source | JS Behavior | Examples |
|-------|----------|-------------|----------|
| **Fatal** | `FATAL_ERROR` | Throw synchronously from `Listen()` | OOM, pthread_create failure, epoll/kqueue init failure |
| **Startup** | `LISTEN_ERROR` | Throw synchronously from `Listen()` | `EADDRINUSE`, `EACCES`, `EADDRNOTAVAIL` |
| **Parse** | `PARSE_ERROR` | Call `next(err)` in JS middleware | Malformed HTTP, invalid chunked encoding |
| **Request** | `REQUEST_ERROR` | Emit 'error' event or auto-respond | Body too large, connection reset |
| **Runtime** | `RUNTIME_ERROR` | Emit 'error' event | Write after end, broken pipe |

---

### Error Object Shape

All native errors surfaced to JavaScript have this structure:

```javascript
{
  name: 'Error' | 'TypeError' | 'RangeError',
  message: string,              // Human-readable description
  code: string | undefined,     // Machine-readable error code (see table below)
  syscall: string | undefined,  // System call that failed (e.g., 'bind')
  expose: boolean             // Whether error details should be exposed to client
}
```

### Error Code Registry

C errors are mapped to JS Error objects with standardized `code` properties:

| C Condition | JS `code` | HTTP Status | Behavior |
|-------------|-----------|-------------|----------|
| `ENOMEM` | `ENOMEM` | - | Fatal: Throw from `Listen()` |
| `EACCES` | `EACCES` | - | Fatal: Throw from `Listen()` (privileged port) |
| `EADDRINUSE` | `EADDRINUSE` | - | Fatal: Throw from `Listen()` |
| `EADDRNOTAVAIL` | `EADDRNOTAVAIL` | - | Fatal: Throw from `Listen()` |
| `EMFILE` / `ENFILE` | `EMFILE` / `ENFILE` | - | Fatal: Too many open files |
| Parse failure | `EPARSE` | 400 | Per-request: Call `next(err)` |
| Invalid HTTP version | `EHTTPVERSION` | 505 | Per-request: Call `next(err)` |
| Header too large | `EHEADER` | 431 | Auto-response (Request Header Fields Too Large) |
| Body exceeds `max_body_size` | `ETOOBIG` | 413 | Auto-response (Payload Too Large) |
| Connection reset | `ECONNRESET` | - | Silently close, log at debug level |
| Broken pipe | `EPIPE` | - | Silently close, log at debug level |
| Write timeout | `ETIMEDOUT` | 504 | Per-request: Call `next(err)` |
| Invalid character in header | `EBADCHAR` | 400 | Per-request: Call `next(err)` |
| Request timeout | `EREQUESTTIMEOUT` | 408 | Auto-response (Request Timeout) |
| Double response | `EDOUBLERESPONSE` | - | Log warning, ignore second response |
| Stream backpressure | `EBACKPRESSURE` | - | Return false from `StreamData()` |

---

### Fatal Error Handling (Crash Prevention)

Fatal errors are detected **before** the event loop starts and throw synchronously:

```c
// In binding.c - Listen() implementation
napi_value Listen(napi_env env, napi_callback_info info) {
    server_config_t config;
    server_handle_t handle;

    // Attempt to initialize platform backend
    handle = server_init(&config, on_request, on_connection, user_data);

    if (handle == NULL) {
        // Fatal: pthread_create failure, OOM, or platform init failure
        // Map errno to JS error and throw
        napi_throw_error(env, "ENOMEM",
            "Failed to initialize server: out of memory");
        return NULL;
    }

    // Further checks for resource limits
    if (getrlimit(RLIMIT_NOFILE, &rlim) == 0 && rlim.rlim_cur < 1024) {
        napi_throw_error(env, "ELIMIT",
            "File descriptor limit too low for high-performance server");
        return NULL;
    }

    // Start the server
    if (server_start(handle) != 0) {
        // EADDRINUSE, EACCES, etc. - throw with syscall info
        napi_value error = create_error_with_code(env, errno, "bind");
        napi_throw(env, error);
        return NULL;
    }

    // Success - return server handle
}
```

**Key rule:** After `Listen()` returns successfully, the process will not crash due to C-level errors under normal operating conditions.

---

### Per-Request Parse Errors

HTTP parsing errors call `next(err)` in the JS middleware chain:

```c
// In platform backend (e.g., server_kevent.c)
void handle_readable(connection_t *conn) {
    parse_result_t result;
    int status = http_parse_request(conn->buffer, conn->buffer_len, &result);

    switch (status) {
        case PARSE_STATUS_DONE:
            // Normal flow - invoke JS handler
            invoke_request_callback(conn, &result);
            break;

        case PARSE_STATUS_NEED_MORE:
            // Need more data - wait for next read event
            return;

        case PARSE_STATUS_ERROR:
            // Parse error - create error object and call next(err)
            error_info_t err = {
                .code = result.error_code == ERROR_HEADER_TOO_LARGE ?
                        "EHEADER" : "EPARSE",
                .status = result.error_code == ERROR_HEADER_TOO_LARGE ?
                          431 : 400,
                .message = parse_error_string(result.error_code)
            };

            // Queue error to JS via threadsafe function
            queue_error_to_js(conn, &err);

            // For 431/413, auto-respond if no JS handler catches it
            if (err.status == 431 || err.status == 413) {
                send_auto_response(conn, err.status);
            }
            break;
    }
}
```

```javascript
// JS side - error middleware
app.onError((err, req, res) => {
    if (err.code === 'EPARSE') {
        // Malformed HTTP request
        res.status(400).json({ error: 'Bad Request', message: err.message });
    } else if (err.code === 'EHEADER') {
        // Header fields too large - already sent 431 by C layer
        // but JS can override if needed
        res.status(431).json({ error: 'Request Header Fields Too Large' });
    } else {
        // Internal server error
        console.error('Unhandled error:', err);
        res.status(500).json({ error: 'Internal Server Error' });
    }
});
```

---

### Automatic HTTP Error Responses (C-Level)

Certain errors generate HTTP responses directly from C **before** reaching JavaScript:

#### 413 Payload Too Large

When `max_body_size` is exceeded:

```c
// In platform backend during body reading
ssize_t read_request_body(connection_t *conn, uint8_t *buf, size_t len) {
    size_t new_total = conn->body_received + len;

    if (new_total > conn->config.max_body_size) {
        // Immediately send 413 and close connection
        const char *response =
            "HTTP/1.1 413 Payload Too Large\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            "\r\n";

        write(conn->fd, response, strlen(response));
        conn->closed = true;

        // Optionally notify JS via error callback (fire-and-forget)
        error_info_t err = {
            .code = "ETOOBIG",
            .status = 413,
            .message = "Request body exceeds max_body_size"
        };
        notify_error_async(conn, &err);  // Don't block, may fail silently

        return -1;  // Signal error to caller
    }

    // Continue reading...
}
```

#### 408 Request Timeout

When request timeout expires:

```c
void on_request_timeout(connection_t *conn) {
    const char *response =
        "HTTP/1.1 408 Request Timeout\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";

    write(conn->fd, response, strlen(response));
    close_connection(conn);
}
```

#### 431 Request Header Fields Too Large

When header size exceeds `MAX_HEADER_SIZE`:

```c
if (header_len > MAX_HEADER_SIZE) {
    send_http_response(conn, 431, "Request Header Fields Too Large");
    close_connection(conn);
    return PARSE_ERROR;
}
```

**Rationale:** These errors indicate protocol violations or resource exhaustion. Generating them in C prevents:
1. Memory exhaustion from buffering oversized payloads
2. Event loop starvation from slow clients
3. Denial of service from header abuse

---

### JS Handler Exception Safety

When a JS handler throws, N-API catches the exception and Express-Pro converts it to a 500 response:

```c
// In threadsafe function callback
void call_js_callback(napi_env env, napi_value js_callback, void *context, void *data) {
    request_context_t *ctx = (request_context_t *)data;
    napi_value result;

    // Call the JS handler
    napi_status status = napi_call_function(env, undefined, js_callback, argc, argv, &result);

    if (status == napi_pending_exception) {
        // Exception was thrown - get and clear it
        napi_value exception;
        napi_get_and_clear_last_exception(env, &exception);

        // Log the error (but don't crash)
        log_js_exception(env, exception, ctx->request_id);

        // Check if response already sent
        if (!ctx->response_sent) {
            // Send 500 Internal Server Error
            send_http_response(ctx->conn, 500, "Internal Server Error");
            ctx->response_sent = true;
        }

        // Clean up request context
        free_request_context(ctx);
        return;
    }

    // Normal completion...
}
```

```javascript
// Example: Throwing in JS handler
app.get('/crash', (req, res) => {
    throw new Error('Something went wrong');
    // C layer catches this, sends 500, logs error
    // Process continues running
});

// Async errors are also caught
app.get('/async-crash', async (req, res) => {
    await Promise.reject(new Error('Async error'));
    // Rejection is caught by N-API promise wrapper
    // 500 response sent automatically
});
```

**Key guarantees:**
- JS exceptions never crash the Node process
- Unhandled exceptions in handlers result in 500 responses
- The connection is properly closed after error response
- Request context is always freed (no memory leaks)

---

### Error Code Prefixes

| Prefix | Source | Example |
|--------|--------|---------|
| `E` | POSIX errno | `ECONNRESET`, `EPIPE`, `ETIMEDOUT` |
| `NAPI_` | Node-API internal | `NAPI_GENERIC_FAILURE` |
| `EXPR_` | Express-Pro application | `EXPR_ETOOBIG`, `EXPR_EPARSE` |

---

### Error Code Reference

#### Connection Errors
- `ECONNRESET` - Connection reset by peer
- `EPIPE` - Broken pipe (write to closed connection)
- `ETIMEDOUT` - Operation timed out
- `ECONNABORTED` - Software caused connection abort
- `ENETUNREACH` - Network is unreachable
- `EHOSTUNREACH` - Host is unreachable

#### Parse Errors
- `EPARSE` - Generic HTTP parse error
- `EHTTPVERSION` - Unsupported HTTP version
- `EMETHOD` - Invalid or unsupported HTTP method
- `EURI` - Invalid request URI
- `EHEADER` - Malformed header
- `EHEADERFIELD` - Invalid header field name
- `EHEADERVALUE` - Invalid header field value

#### Protocol Errors
- `EREQUESTTIMEOUT` - Request timeout
- `EHEADEREXCEED` - Header size exceeds limit
- `EBODYEXCEED` / `ETOOBIG` - Body size exceeds limit
- `EINVALIDCHUNK` - Invalid chunked encoding
- `EINVALIDLENGTH` - Invalid Content-Length
- `EDOUBLERESPONSE` - Response already sent

#### System Errors
- `ENOMEM` - Out of memory
- `EMFILE` - Too many open files (process limit)
- `ENFILE` - Too many open files (system limit)
- `ENOMEMPOOL` - Connection pool exhausted
- `ETHREAD` - Thread creation failed
- `EPLATFORM` - Platform-specific init failure

---

### Common Error Scenarios

1. **Port already in use** (Fatal - throws synchronously):
   ```javascript
   try {
     app.listen(3000);
   } catch (err) {
     console.error(err.code); // 'EADDRINUSE'
     process.exit(1);
   }
   ```

2. **Header too large** (Auto-response, then error event):
   ```javascript
   app.onError((err, req, res) => {
     if (err.code === 'EHEADER') {
       // Client already received 431
       // Can log or ignore
     }
   });
   ```

3. **Body too large** (Auto-response from C):
   ```javascript
   // Client receives 413 before reaching this handler
   app.post('/upload', (req, res) => {
     // Only called if body is within max_body_size
   });
   ```

4. **JS handler throws** (Caught, 500 auto-response):
   ```javascript
   app.get('/error', (req, res) => {
     throw new Error('Crash!');
   });
   // Client receives HTTP 500
   // Process continues running
   ```

5. **Parse error** (Calls next(err)):
   ```javascript
   app.onError((err, req, res) => {
     if (err.code === 'EPARSE') {
       res.status(400).json({ error: 'Invalid request' });
     }
   });
   ```

---

## Data Structures

### Request Event (passed to OnRequest callback)

```c
typedef struct {
    uint64_t requestId;      // Unique ID for this request
    const char* method;      // HTTP method (GET, POST, etc.)
    const char* path;        // Request path
    const char* query;       // Query string (without ?)
    const char* headers;     // JSON-encoded headers object
    size_t headersLen;       // Length of headers JSON
    const uint8_t* body;     // Body data (may be NULL)
    size_t bodyLen;          // Body length
    bool bodyComplete;       // true if body is fully received
    const char* clientIp;    // Client IP address string
} RequestEvent;
```

### Response Data (passed to SendResponse)

```c
typedef struct {
    uint64_t requestId;
    int statusCode;
    const char* headersJson;  // JSON-encoded headers
    const uint8_t* body;
    size_t bodyLen;
    bool streamResponse;      // If true, use StreamData for body
} ResponseData;
```

### Platform Backend Info

```c
typedef enum {
    BACKEND_IO_URING,   // Linux
    BACKEND_KQUEUE,     // macOS/BSD
    BACKEND_IOCP,       // Windows
    BACKEND_LIBUV       // Fallback
} AsyncBackend;
```

---

## Implementation Checklist

For engineers implementing `binding.c`:

- [ ] Validate all input arguments with `napi_get_value_*` functions
- [ ] Check thread safety - only call threadsafe functions from correct threads
- [ ] Create `napi_threadsafe_function` in `Listen`, release in `Stop`
- [ ] Set up error objects with proper `name`, `message`, `code`
- [ ] Handle platform-specific includes (`#ifdef USE_IO_URING` etc.)
- [ ] Implement request ID generation (64-bit counter)
- [ ] Manage response lifecycle (track pending responses)
- [ ] Handle memory cleanup in all error paths
- [ ] Add tracing/logging for debugging (optional)

---

## References

- [Node-API Documentation](https://nodejs.org/api/n-api.html)
- [N-API Thread-safe Functions](https://nodejs.org/api/n-api.html#asynchronous-thread-safe-function-calls)
- [liburing Documentation](https://manpages.debian.org/unstable/liburing-dev/)
- [kqueue(2) man page](https://man.freebsd.org/cgi/man.cgi?query=kqueue)
- [Windows IOCP](https://docs.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports)
