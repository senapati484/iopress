# @iopress/core — Codebase Context

## Project
High-performance Node.js HTTP framework (~212K req/s kqueue, 300K+ io_uring).
C native addon with 4 platform backends, Express-compatible JS API.

## Directory Structure
```
src/              C sources (N-API binding + 4 backends)
├── binding.c          N-API bridge (exports Listen, SendResponse, RegisterFastRoute, Close)
├── parser.c/.h        Zero-copy HTTP/1.1 parser (single-pass, memchr-based)
├── router.c/.h        Trie-based O(k) route matcher
├── fast_router.c/.h   Hash-table fast route with pre-built HTTP responses
├── server.h           Platform-agnostic interface (connection_t, response_t, server_* functions)
├── server_kevent.c    macOS/BSD kqueue backend (primary dev target)
├── server_uring.c     Linux io_uring backend (liburing)
├── server_iocp.c      Windows IOCP backend
├── server_libuv.c     Fallback stub
└── error_handling.c   Reference error handling patterns

js/               JavaScript layer
├── index.js           Main module: Request/Response classes, router, middleware, cluster
└── fallback.js        Pure JS fallback (node:http, no native addon)

test/             Tests (node:test runner)
├── test.js, integration.test.js, streaming.test.js, large-body/response.test.js
├── memory.test.js, deprecation.test.js, default-routes.test.js
├── parser.test.c, router.test.c       C unit tests (gcc, standalone)

benchmarks/       Performance
├── run.js            Autocannon-based harness, 5 scenarios, platform thresholds
└── config.json       Targets: linux 300K, darwin 150K, win32 50K min RPS
```

## Build & Test
```sh
npm run build          # node-gyp rebuild (platform-conditional C compilation)
npm test               # node --test --test-timeout=30000 test/*.test.js test/test.js
npm run bench:quick    # node benchmarks/run.js --scenario hello-world
npm run bench:full     # All 5 scenarios, compare vs Express
gcc -Isrc test/parser.test.c src/parser.c -o /tmp/parser.test && /tmp/parser.test # C tests
```

## Key Architecture
1. **binding.c** receives request via `napi_threadsafe_function` from C event loop thread → calls JS handler → `SendResponse` sends headers + body back
2. **fast_router.c** pre-builds full HTTP responses at registration time; `fast_router_try_handle_full()` returns ready-to-send buffer, bypasses JS entirely
3. **server_send_response()** uses `conn_try_write`: tries sync `write()` first, buffers remainder on EAGAIN via `conn_write_buffered`
4. **JS send()** → `native.SendResponse(fd, status, body, headers)` which iterates headers via N-API and calls `server_send_response`
5. **connection_t** has `out_buffer` for partial write buffering, `buffer` for read accumulation, `assembled_body` for chunked dechunking

## Security Config
- `X-Content-Type-Options: nosniff` set in Response constructor
- `escapeHtml()` in `js/fallback.js` for CodeQL XSS compliance
- `encodeURI(req.path)` on 404 JSON handlers
- `overrides.tar: 7.5.19` in package.json (CVE fix)
- `node-gyp@^12.4.0` (resolves ip-address CVE)

## Critical Metrics
- Hello-world fast route: **212K req/s** on Mac M2 (kqueue), **11.3-12.3x vs Express**
- JS-handled route: ~80K req/s (macOS), ~150K (Linux)
- 53 JS tests, 17 C parser tests: all pass
- 9 Dependabot alerts: 8 fixed, 1 remaining (uuid dev-only via autocannon)
- Current version: **1.0.5**, target: **1.1.0**

## Key Conventions
- No `snprintf` in hot path (use `memcpy` + pre-formatted strings + `append_uint64`)
- Single `write()`/`writev()` per response (avoid splitting headers + body)
- All backends must conform to `server.h` interface
- N-API only, no nan/v8 direct API
- clang-format enforced via pre-commit hook
- Priority order for optimizations: syscall count → memory copy → format parsing

## Remaining Work
- Windows prebuilt binary upload to GitHub Releases
- `npm publish` (--access public) after all security reports closed
- Thread-safe io_uring SQE submission (IORING_SETUP_SQPOLL)
- uuid CVE-2026-41907: dev-only via autocannon→hyperid→uuid, fix would break benchmark
- keep-alive edge case: after response, arm recv. If client closes before arm, server doesn't learn until next event
