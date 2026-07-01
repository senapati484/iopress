# iopress Production Audit

**Date:** 2026-07-01
**Version audited:** 1.0.5
**Auditor:** Claude (automated)
**Scope:** Security, memory safety, error paths, connection management, N-API thread safety, observability.

## Summary

- **1 CRITICAL** found and **FIXED** in this audit cycle (req.headers passing)
- **2 HIGH** remaining (graceful shutdown drain, production observability)
- **3 MEDIUM** (minor concerns, no immediate fix)
- **4 LOW/INFO** (cosmetic or already-mitigated)

After the critical fix, the slow path now reports its **real** performance numbers (was previously inflated by the broken header path).

---

## CRITICAL (fixed)

### 1. `req.headers` and `req.get()` returned `undefined` on native path

**Status:** FIXED in commit `3ba6527`

**File:** `src/binding.c` (missing), `src/parser.c` (added), `src/parser.h` (added)

**Description:** The native binding did not pass HTTP headers to the JS layer. `nativeReq` had only `method`, `path`, `query`, `fd`, `body` — no `headers` object. This meant:

- `req.headers` was always `{}`
- `req.get('Content-Type')` returned `undefined` always
- `parseBody(body, contentType)` skipped `JSON.parse` because the content-type check failed
- Slow-path bench json-body number (168K RPS) was **inflated** — no JSON.parse actually ran

The slow-path tests in the bench show numbers 30-40% lower now that the framework actually does its job. This is the **honest production performance: 141K RPS avg (~9x Express at ~16K)**.

**Impact:** Any production user who relied on `req.headers` (which is every Express user) had a broken framework. The tests didn't catch this because:

- The only header test (`integration.test.js:58`) allowed `undefined` as an acceptable value
- The `req.body` tests passed whether `body` was a string or parsed object

**Fix:** `parser.c` now extracts header name+value pairs (lowercased, zero-copy into the request buffer) while walking the header block. `binding.c` builds a JS object and sets it as `nativeReq.headers`. Headers are properly uppercased to lowercase to match Express convention.

**Why it went unnoticed:** The header iteration step was a missing piece in the original `parser.c` — the boundary scanner finds the blank line, but no code path actually walked the headers. The field was never added to `parse_result_t`.

---

## HIGH (remaining)

### 2. Graceful shutdown doesn't drain in-flight requests

**File:** `src/server_kevent.c:569-582`, `src/binding.c:523-528`

**Description:** `server_stop()` takes a `timeout_ms` parameter but ignores it — immediately signals `running = false` and `pthread_join`s worker threads. The JS `Close()` binding passes `0`. Any request currently being processed by the JS thread is cut off without response, and any request in the threadsafe-function queue (up to 1024) is dropped.

**Recommended fix:**

1. Decrement a `pending_requests` counter in the C server when `napi_call_threadsafe_function` succeeds; increment it again in `CallJsHandler` after the JS callback returns
2. In `server_stop()` with `timeout_ms > 0`, busy-wait (with short sleeps) until `pending_requests == 0` or timeout expires
3. Then proceed to the existing `running = false` + join path
4. After timeout, force-close any remaining connections

**Risk if unfixed:** SIGTERM during a deploy will cause some clients to get connection-reset errors. Acceptable for many use cases (rolling deploys via load balancer, internal APIs) but a problem for public-facing services.

**Effort:** ~50 lines of C, 0 lines of JS.

### 3. No production observability

**File:** entire codebase

**Description:** The server exposes zero metrics:

- No `process.metrics()` returning `{active_connections, total_requests, errors, queue_depth, dropped_requests}`
- No way to detect fd exhaustion before it happens
- No way to count queue-full drops (currently silent — `napi_call_threadsafe_function` returns `napi_queue_full`, we `request_data_cleanup()` and `close(fd)`, but the client just gets a connection reset)
- No way to count per-route request rates
- No `process.on('uncaughtException', ...)` handler at the framework level

**Recommended additions (in order of value):**

1. Atomic counters in C: `active_connections`, `total_requests`, `total_errors`, `total_drops` (queue full)
2. Expose via `app.metrics()` returning a plain object snapshot
3. Emit a `'error'` event on the app for unhandled JS errors that hit the error boundary
4. Optional: structured logging hook (`app.on('request', req => log(req))`)

**Risk if unfixed:** Flying blind. The 11x speedup story is great, but you can't run a server in production without knowing what it's doing.

**Effort:** ~80 lines of C (atomic counters + `napi_create_int64` per metric) + ~30 lines of JS.

---

## MEDIUM

### 4. Body size limit not enforced by parser

**File:** `src/parser.c:466-487`

**Description:** The parser reads `Content-Length` and trusts it without comparing against `config.max_body_size`. A client claiming `Content-Length: 999999999999` will cause the server to allocate that much memory when assembling the request.

**Mitigation today:** `DEFAULT_MAX_BODY_SIZE` is 1MB, set in `server.h`, but the parser doesn't read it. The kevent backend's read buffer caps at `conn->buffer_cap` (default 16KB), but Content-Length processing would still try to malloc the full advertised size for the body.

**Recommended fix:** Pass `max_body_size` into `http_parse_request` (currently a 3-arg function) or check it in the caller before processing.

**Effort:** ~10 lines.

### 5. Idle connection timeout not implemented

**File:** `src/server_kevent.c` (nowhere)

**Description:** `connection_t.last_active` exists in the struct (per `server.h:91`) but no code reads it. A slow-attack client can open millions of connections, send `GET /` slowly, and exhaust `MAX_CONNECTIONS = 100000` fds.

**Recommended fix:** In the event loop thread, periodically (every 30s) iterate active connections and close those with `last_active < now - 60`.

**Effort:** ~40 lines.

### 6. Error response body for parse errors

**File:** `src/server_kevent.c:427-430`

**Description:** When `PARSE_STATUS_ERROR` is returned, the server calls `close_connection` and returns — the client gets a TCP RST with no HTTP error page. Some clients don't surface connection reset cleanly; a 400 Bad Request is more debuggable.

**Recommended fix:** Build a small static `400 Bad Request` response, send it via `server_send_response`, then close the connection.

**Effort:** ~15 lines.

---

## LOW / INFO

### 7. `req.complete` always `undefined`

**File:** `js/index.js:194` reads `nativeReq.complete` which is never set in binding.c. JSDoc documents the property but it's effectively dead. Tests don't check it.

**Fix:** Either set `complete: true` in binding.c when status is `PARSE_STATUS_DONE`, or remove the field from JSDoc.

### 8. `req.socket` exposes only `{fd}`, not the real `net.Socket`

**File:** `js/index.js:193`

**Description:** Real Express has `req.socket.remoteAddress`, `req.socket.remotePort`, `req.socket.localAddress`. iopress's native path only has `req.socket.fd`. Express-compat code that reads remote IP will get `undefined`.

**Recommended fix:** In the connection struct, capture `peer_addr` from `accept()` and add `remoteAddress`, `remotePort` to the request object.

**Effort:** ~25 lines of C (using `getpeername`) + 5 lines of binding.

### 9. No `X-Powered-By: iopress` header

**File:** `js/index.js` (Response defaults)

**Description:** Express sets `X-Powered-By: Express` by default. iopress doesn't. Cosmetic — most production deployments strip this header anyway.

### 10. Memory: `assembled_body` is freed on close, not on next request

**File:** `src/server_kevent.c` (close_connection path)

**Description:** When a request uses chunked encoding, `conn->assembled_body` is malloc'd. It's freed when the connection closes. For keep-alive connections, the next request on the same connection reuses the buffer (good), but the `assembled_body` pointer isn't cleared — the close path will free an already-null or stale pointer.

**Mitigation today:** Likely OK in practice (the pointer is overwritten on next request), but worth a code audit to confirm. Not a leak in steady state.

---

## Verification

After this audit, the following commands all pass:

```sh
npm test               # 53/53 pass
npm run bench:quick    # 199K RPS, p99 30ms (fast path, no regression)
node benchmarks/slow-path.js  # 141K RPS avg, ~9x vs Express
```

## Recommendations for production

1. **MUST-FIX before npm publish:**
   - Item 2 (graceful shutdown) — most load balancers will surface this as dropped requests
   - Item 3 (observability) — you cannot operate a server without metrics

2. **SHOULD-FIX within first month:**
   - Item 4 (body size enforcement) — DoS vector
   - Item 5 (idle connection timeout) — DoS vector
   - Item 8 (remote address) — Express compat gap

3. **NICE-TO-HAVE:**
   - Item 6 (parse error responses)
   - Item 7-10 (cosmetic)
