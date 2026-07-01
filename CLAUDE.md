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

### Fast path (hello-world, C fast router bypasses JS)
- **~212K req/s** on Mac M2 (kqueue), **11.3-12.3x vs Express** — measured on a cool system
- Drops to **~150-180K req/s** under sustained thermal load (Mac M2 thermal throttles after 1-2 minutes at 100% CPU across multiple cores)

### Slow path (JS-handled routes, the path real Express users hit)
- **Historical peak: 184K avg RPS** at commit `50a7e08` (2026-07-01 17:15 IST) — see `benchmarks/slow-path.js` baseline. **NOTE: this measurement was on a broken framework where `req.headers` was always empty**. The slow-path bench then did not include header building because the framework was missing the headers fix entirely.
- **Honest slow-path (post-audit, working `req.headers`): 150K peak, ~70-100K steady-state** on Mac M2. The 150K is a cool-system measurement; the 70-100K is what you get under sustained bench load (thermal throttle).
- The drop from 184K → 150K is the **real cost of fixing the `req.headers` bug** (commit `3ba6527`). Building the headers object in C with N N-API calls is unavoidable for correct Express compat.
- **The 184K is not reproducible** without reverting the headers fix (which would break Express users). The 150K is the best honest ceiling on this hardware.

### Per-scenario breakdown (slow path, 500 conns / pipelining 10 / 5s)
| Scenario | Honest RPS | Notes |
|---|---|---|
| route-params (`/users/42`) | 80-165K | Most stable; varies with thermal state |
| json-body (POST `/echo` with `{"x":1}`) | 55-120K | Actually parses JSON post-fix |
| query-params (`/search?q=...`) | 70-175K | Most thermal-sensitive |

### Test coverage
- 53 JS tests, 17 C parser tests: all pass
- 9 Dependabot alerts: 8 fixed, 1 remaining (uuid dev-only via autocannon)
- Current version: **1.0.5**, target: **1.1.0**

### Performance measurement caveats
- **Thermal throttling dominates measurement noise on Mac M2.** The 184K / 150K / 215K numbers are all real but they are upper bounds measured on a cool system. A bench that runs 3 scenarios × 5s × 3 runs heats the CPU enough to throttle. **A single 5s bench run on a fresh system gives a different number than 3 sequential ones.**
- **The slow-path bench script (`benchmarks/slow-path.js`) spawns a child node process per run.** Under thermal-throttled load, the child server can be killed by macOS resource limits or hit autocannon timeouts, producing 0 RPS in the output. This is not a framework bug — direct autocannon on a long-lived server works fine. Treat the slow-path bench as a noisy upper-bound indicator, not a precise measurement.
- **Never trust a single bench run.** Re-run 3+ times, on a cool system, and take the median.

## Audit history (2026-07-01)
- `3ba6527` — **CRITICAL**: fixed `req.headers` always being `{}` on native path. Cost: 184K → 150K slow path.
- `3dc0637` — added `app.metrics()` (pending/total/drops/errors atomic counters).
- `aa64641` — wired `total_errors` from JS `_executeChain` catch path.
- `385b645` / `06dbf64` — audit document + drain deferral.
- See `AUDIT.md` for the full production-readiness audit.

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
