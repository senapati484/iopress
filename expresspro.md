# ExpressPro — Modular Prompt Playbook
> A structured sequence of AI prompts for building a Node.js native addon module.
> Each prompt is self-contained but designed to chain. Copy-paste into Cursor, Claude, Copilot, or any LLM IDE.

---

## How to Use This Playbook

**Chaining rule:** End every session with a **Progress Summary Block** (template at bottom).  
Paste it at the top of the next prompt as context. This prevents context drift across sessions.

**Iterative refinement:** If output is wrong, append:  
> "The above output has issue: [X]. Revise only [specific part], keep everything else."

**Adapting to other stacks:** Replace `node-gyp / N-API` with your native bridge. Replace `wrk` with your benchmark tool. Everything else is stack-agnostic.

---

## Section 1 — Setup & Repository Scaffolding

---

### P1.1 — Initialize Project Structure

**Role/Context:** You are a senior Node.js native addon engineer scaffolding a new open-source module.  
**Objective:** Create the full directory tree, `package.json`, `.gitignore`, and `binding.gyp` for a cross-platform Node.js native addon called `iopress`.  
**Expected Output:** Shell commands + file contents ready to run.

**Inputs/Assumptions:**
- Node.js 18+, npm 9+
- Native addon via node-gyp + N-API
- Cross-platform: Linux (io_uring), macOS (kqueue), Windows (IOCP), fallback (libuv)
- TypeScript types will be added later

**Checklist:**
- [ ] `npm init -y` with correct `name`, `main`, `engines` fields
- [ ] `binding.gyp` with conditional platform sources (see PLAN.md architecture)
- [ ] `.gitignore` covering `build/`, `node_modules/`, `*.node`
- [ ] `scripts`: `"build": "node-gyp rebuild"`, `"test": "node --test"`, `"bench": "bash benchmark/run.sh"`
- [ ] Directory stubs: `src/`, `js/`, `test/`, `examples/`, `benchmark/`

**Constraints:** No TypeScript yet. No dependencies except `node-gyp` as devDependency.

**Expected outcome:** `npm run build` succeeds (even if the addon does nothing yet).

---

### P1.2 — binding.gyp Cross-Platform Configuration

**Role/Context:** You are configuring node-gyp for conditional compilation across 4 platforms.  
**Objective:** Write a complete `binding.gyp` that compiles the correct backend source per OS, with max optimization flags.  
**Expected Output:** Complete `binding.gyp` file content.

**Inputs/Assumptions:**
- Sources: `src/binding.c`, `src/router.c`, `src/parser.c` always included
- Platform sources: `server_uring.c` (linux), `server_kevent.c` (mac), `server_iocp.c` (win), `server_libuv.c` (fallback)
- Linux links `-luring`; others need no extra libs

**Checklist:**
- [ ] `cflags`: `-O3 -march=native -funroll-loops -fomit-frame-pointer`
- [ ] `conditions` block for all 4 OS cases
- [ ] `defines` per platform: `USE_IO_URING`, `USE_KQUEUE`, `USE_IOCP`, `USE_LIBUV`
- [ ] Verify with `npx node-gyp configure --verbose`

**Expected outcome:** `binding.gyp` compiles without error on macOS (primary dev machine).

---

### P1.3 — Git Hooks & Linting Setup

**Role/Context:** Setting up code quality gates before any code is written.  
**Objective:** Configure `clang-format` for C files and `eslint` for JS files, plus a pre-commit hook.  
**Expected Output:** Config files + `package.json` script additions.

**Checklist:**
- [ ] `.clang-format` with Google style (or LLVM — pick one, be consistent)
- [ ] `eslint.config.js` targeting `js/` directory, Node environment
- [ ] `scripts.lint`: `"clang-format --dry-run src/*.c && eslint js/"`
- [ ] Pre-commit hook via `scripts.prepare` + simple shell check (no extra deps)

**Expected outcome:** `npm run lint` runs cleanly on empty stubs.

---

### P1.4 — CI/CD Workflow (GitHub Actions)

**Role/Context:** You are setting up automated builds for a multi-platform native addon.  
**Objective:** Write a GitHub Actions workflow that builds and tests on Linux, macOS, and Windows.  
**Expected Output:** `.github/workflows/ci.yml`

**Checklist:**
- [ ] Matrix: `ubuntu-latest`, `macos-latest`, `windows-latest`
- [ ] Node.js 18 and 20 in matrix
- [ ] Steps: `npm ci` → `npm run build` → `npm test`
- [ ] Cache `~/.node-gyp` and `build/` between runs
- [ ] On Linux: `sudo apt-get install -y liburing-dev` before build

**Expected outcome:** Green CI on a repo with stub C files.

---

### P1.5 — TypeScript Declarations Stub

**Role/Context:** Consumers of this module expect type safety.  
**Objective:** Create `index.d.ts` with complete TypeScript declarations matching `js/index.js` API surface.  
**Expected Output:** `index.d.ts` content.

**Inputs:** See PLAN.md Section "Supported Express APIs" — `app.get/post/use/listen`, `req.body/params/method/path`, `res.json/send/status`.

**Checklist:**
- [ ] `ExpressProOptions` interface with `initialBufferSize`, `maxBodySize`, `streamBody`
- [ ] `Request` and `Response` interfaces
- [ ] `ExpressPro` class with all method signatures
- [ ] Export default `expressPro()` factory function
- [ ] Add `"types": "index.d.ts"` to `package.json`

**Expected outcome:** A TypeScript project can `import ExpressPro from 'iopress'` with full autocomplete.

---

## Section 2 — API Design & Module Boundaries

---

### P2.1 — Define the C/JS Boundary Contract

**Role/Context:** You are designing the N-API surface — the contract between C and JavaScript.  
**Objective:** Document every function exported from `binding.c` with its exact signature, argument types, return value, and threading constraints.  
**Expected Output:** A `BINDING_CONTRACT.md` or inline comments in `binding.c`.

**Inputs/Assumptions:**
- 5 exported functions: `RegisterRoute`, `Listen`, `SendResponse`, `SetServerOptions`, `StreamData`
- JS callbacks must only be invoked via `napi_threadsafe_function`

**Checklist:**
- [ ] For each function: JS call signature, C function signature, argument validation, error codes
- [ ] Threading constraint: which functions are safe to call from C threads vs main thread only
- [ ] Document the `napi_threadsafe_function` lifecycle (create at Listen, release at Stop)
- [ ] Define error object shape returned to JS on failure

**Expected outcome:** Any engineer can implement `binding.c` from this contract alone without reading C code.

---

### P2.2 — JS API Surface Design (Express Compatibility Matrix)

**Role/Context:** You are defining exactly which Express.js APIs are supported, partially supported, and explicitly out of scope.  
**Objective:** Create a compatibility matrix and document any behavioral differences from Express 4.x.  
**Expected Output:** `COMPATIBILITY.md`

**Checklist:**
- [ ] Table: Feature | Express 4 | ExpressPro v1 | Notes
- [ ] Explicitly mark unsupported: `express.static`, `app.engine`, `res.render`, `req.cookies`
- [ ] Document body parsing differences (4KB default, `maxBodySize` option)
- [ ] Document that `next(err)` error handling works but `app.use(err, req, res, next)` is deferred to v2
- [ ] Note Linux-only features (io_uring performance tier)

**Expected outcome:** Users know exactly what they can migrate from Express without surprises.

---

### P2.3 — `server.h` Interface Design

**Role/Context:** You are designing the shared C interface that all 4 platform backends must implement.  
**Objective:** Write `src/server.h` with all type definitions, the `server_config_t` struct, and function declarations.  
**Expected Output:** Complete `src/server.h` content.

**Checklist:**
- [ ] `server_config_t`: `initial_buffer_size`, `max_body_size`, `streaming_enabled`
- [ ] `connection_t`: `buffer`, `buffer_cap`, `buffer_len`, `body_remaining`, `streaming`
- [ ] `parse_result_t`: `status` (DONE/NEED_MORE/ERROR), `body_present`, `body_length`, `body_start`
- [ ] Function declarations for all 5 backend interface functions
- [ ] `#define DEFAULT_INITIAL_BUFFER_SIZE 4096` and `DEFAULT_MAX_BODY_SIZE 1048576`
- [ ] Include guards + C linkage guards for C++ compatibility

**Expected outcome:** All 4 `server_*.c` files compile against this header with zero warnings.

---

### P2.4 — Error Handling Strategy

**Role/Context:** Native addons crash Node processes on unhandled errors. Design must be deliberate.  
**Objective:** Define the complete error handling strategy for C-level errors surfaced to JavaScript.  
**Expected Output:** Error handling section in `BINDING_CONTRACT.md` + code snippets.

**Checklist:**
- [ ] C errors map to JS `Error` objects with `code` property (e.g., `ECONNRESET`, `EPARSE`, `ETOOBIG`)
- [ ] Fatal C errors (OOM, pthread failure) throw synchronously from `Listen()`
- [ ] Per-request parse errors call `next(err)` in JS middleware chain
- [ ] `max_body_size` exceeded → 413 auto-response from C (never reaches JS)
- [ ] Document what happens when a JS handler throws (caught by N-API, 500 auto-response)

**Expected outcome:** No unhandled exception can crash the Node process under normal operating conditions.

---

### P2.5 — Request/Response Object Shape

**Role/Context:** Designing the JS objects passed to route handlers.  
**Objective:** Define the exact shape of `req` and `res` objects created in `js/index.js`.  
**Expected Output:** JSDoc annotations + example objects.

**Checklist:**
- [ ] `req`: `method`, `path`, `params`, `query`, `headers`, `body`, `raw` (Buffer), `socket` (fd as number)
- [ ] `res`: `status(code)` (chainable), `json(obj)`, `send(str)`, `set(header, value)`, `end()`
- [ ] Document which fields are populated by C (method, path, body) vs JS (params, query)
- [ ] Define `req.on('data')` / `req.on('end')` contract when `streamBody: true`

**Expected outcome:** `js/index.js` has a complete, testable object factory before any C code runs.

---

## Section 3 — Core Functionality Implementation

---

### P3.1 — Implement `src/parser.c`

**Role/Context:** You are implementing a zero-copy HTTP/1.1 request parser in C. This is the hottest code in the project.  
**Objective:** Implement `parse_http_request()` and `parse_append_body()` per the spec in `src/server.h`.  
**Expected Output:** Complete `src/parser.c` + a standalone `test_parser.c` with a `main()`.

**Inputs:** `server.h` header. HTTP/1.1 wire format (method SP path SP HTTP/1.1 CRLF headers CRLF body).

**Checklist:**
- [ ] Single-pass: scan for first space (method), second space (path), `\r\n\r\n` (end of headers)
- [ ] Use `memchr` for scanning (SIMD-optimized in libc)
- [ ] Extract `Content-Length` header with `strncasecmp`
- [ ] Fast path: if `Content-Length` ≤ remaining buffer → set `body_start`, return `PARSE_DONE`
- [ ] Slow path: return `PARSE_NEED_MORE` with `body_length` set
- [ ] `parse_append_body`: append new data, check if `body_remaining` reaches 0
- [ ] Never call `malloc` if body fits in initial buffer

**Acceptance criteria:**
- `GET /health HTTP/1.1\r\n\r\n` → `PARSE_DONE`, no body
- `POST /echo` with `Content-Length: 13` and full body → `PARSE_DONE`, `body_start` points correctly
- `POST /upload` with `Content-Length: 1000000` and partial data → `PARSE_NEED_MORE`
- All cases pass Valgrind with no leaks

**Expected outcome:** Standalone `./test_parser` binary prints PASS for all test cases.

---

### P3.2 — Implement `src/router.c`

**Role/Context:** You are implementing an O(k) trie-based HTTP router in C.  
**Objective:** Implement route registration and lookup with `:param` support. Zero heap allocation during lookup.  
**Expected Output:** Complete `src/router.c` and `src/router.h`.

**Checklist:**
- [ ] Trie node struct: `segment[64]`, `is_param`, `children[8]`, `child_count`, `handlers[5]` (per method)
- [ ] `router_add(method, path, handler_fn)` — splits path by `/`, inserts nodes
- [ ] `router_lookup(method, path, params_out)` — traverses trie, populates params on stack
- [ ] Method enum: GET=0, POST=1, PUT=2, PATCH=3, DELETE=4
- [ ] Return `NULL` for no match (will become 404 in binding.c)
- [ ] Test: `/users/:id/posts/:postId` extracts both params correctly

**Acceptance criteria:**
- `GET /users/42` matches `GET /users/:id`, `params[0] = {key:"id", val:"42"}`
- `POST /users/42` does NOT match `GET /users/:id`
- Unknown route returns NULL
- 10,000 lookups with no allocations (verify with sanitizer)

**Expected outcome:** Standalone test binary passes all route matching cases.

---

### P3.3 — Implement `src/server_kevent.c` (macOS Backend)

**Role/Context:** You are on macOS. This is your primary development backend. Build it first, validate locally, then mirror the pattern for other backends.  
**Objective:** Implement the full kqueue event loop satisfying the `server.h` interface.  
**Expected Output:** Complete `src/server_kevent.c`.

**Checklist:**
- [ ] `server_init`: create TCP socket, `SO_REUSEPORT`, bind, listen, create kqueue fd
- [ ] `server_run`: main event loop — `kevent(kq, NULL, 0, events, 64, NULL)`
- [ ] On new connection: `accept()`, add to kqueue with `EVFILT_READ`
- [ ] On readable: call `parse_http_request`, handle PARSE_DONE / PARSE_NEED_MORE / PARSE_ERROR
- [ ] On PARSE_DONE: call `napi_call_threadsafe_function` with request data
- [ ] `server_send_response`: write status line + headers + body, optionally keep-alive
- [ ] Connection pool: static `conn_pool[MAX_CONNS]` — index by fd, zero malloc on hot path
- [ ] Run in background thread via `pthread_create` (called from `binding.c`)

**Acceptance criteria:**
- `curl http://localhost:3000/health` returns a response
- `wrk -t4 -c100 -d10s` shows > 50,000 req/s on MacBook (M1/M2) or > 30,000 on Intel
- No crashes under 10,000 concurrent connections (use `ulimit -n 65535`)

**Expected outcome:** A working HTTP server on macOS controlled from C, no JS yet.

---

### P3.4 — Implement `src/binding.c` (N-API Bridge)

**Role/Context:** You are implementing the N-API bridge — the most complex part due to threading.  
**Objective:** Expose `RegisterRoute`, `Listen`, `SendResponse`, `SetServerOptions` to JavaScript.  
**Expected Output:** Complete `src/binding.c`.

**Threading contract (critical):**
- `server_run` runs in a `pthread` background thread
- All `napi_*` calls must happen on the main Node.js thread
- Use `napi_create_threadsafe_function` once at `Listen` time
- The TSFN callback reconstructs `req` object and calls the JS route handler

**Checklist:**
- [ ] `Listen(port, options)`: parse options → `server_config_t`, call `server_init`, `pthread_create` with `server_run`
- [ ] `RegisterRoute(method, path, jsFn)`: store JS function ref, call `router_add`
- [ ] TSFN callback (`CallJS`): build `napi_value` req object from C `http_request_t`, call JS handler
- [ ] `SendResponse(fd, status, bodyStr)`: call `server_send_response` (can be called from JS main thread)
- [ ] `SetServerOptions(options)`: update `server_config_t` fields
- [ ] `napi_module_init`: register all 5 functions

**Acceptance criteria:**
```javascript
const binding = require('./build/Release/express_pro');
binding.RegisterRoute('GET', '/health', (req, res) => res.send('ok'));
binding.Listen(3000, {});
// curl localhost:3000/health → "ok"
```

**Expected outcome:** Working end-to-end request → JS handler → response cycle.

---

### P3.5 — Implement `js/index.js` (Express-Compatible Wrapper)

**Role/Context:** You are building the thin JS layer that provides the Express-compatible API.  
**Objective:** Implement the `ExpressPro` class using the native binding internally.  
**Expected Output:** Complete `js/index.js`.

**Checklist:**
- [ ] `ExpressPro` class with `use`, `get`, `post`, `put`, `patch`, `delete`, `listen`
- [ ] Middleware chain: `[...globalMiddleware, routeHandler]` executed via async `reduce` or recursive `next()`
- [ ] `req` object: augment C-provided fields with `query` (parsed from path), `headers` object
- [ ] `res` object: `status(code)` sets code and returns `this`; `json(obj)` calls `binding.SendResponse`
- [ ] `listen(port, options, cb)`: call `SetServerOptions` then `Listen`, invoke `cb` on success
- [ ] `req.body`: parse JSON if `Content-Type: application/json` else raw string

**Acceptance criteria:**
```javascript
const app = require('iopress')();
app.use((req, res, next) => { req.start = Date.now(); next(); });
app.get('/health', (req, res) => res.json({ status: 'ok' }));
app.listen(3000, () => console.log('ready'));
```

**Expected outcome:** The above snippet works identically to Express 4.x syntax.

---

### P3.6 — Implement `src/server_uring.c` (Linux Backend)

**Role/Context:** Porting the validated kqueue backend logic to io_uring for maximum Linux performance.  
**Objective:** Implement the io_uring backend satisfying the same `server.h` interface.  
**Expected Output:** Complete `src/server_uring.c`.

**Key differences from kqueue backend:**
- Use `io_uring_prep_accept` / `io_uring_prep_recv` / `io_uring_prep_send` instead of `kevent`
- Submit a batch of SQEs before calling `io_uring_submit` (amortize syscalls)
- Use `io_uring_sqe_set_data` to tag each SQE with a `connection_t*` pointer
- Retrieve `connection_t*` via `io_uring_cqe_get_data` in the completion handler

**Checklist:**
- [ ] `io_uring_queue_init(QUEUE_DEPTH, &ring, 0)` in `server_init`
- [ ] Re-arm accept after each connection: `io_uring_prep_accept` → submit
- [ ] For recv: check `cqe->res` (bytes read), call parser, re-arm if PARSE_NEED_MORE
- [ ] For send: build response buffer, `io_uring_prep_send`, set data = conn
- [ ] Close connection on keep-alive timeout or explicit close: `io_uring_prep_close`
- [ ] `SO_REUSEPORT` enabled for multi-core scaling

**Acceptance criteria:**
- `wrk -t12 -c400 -d30s http://localhost:3000/health` → ≥ 300,000 req/s on 8-core Linux VM
- No memory leaks under `valgrind --tool=massif`

**Expected outcome:** Linux benchmark matches or exceeds PLAN.md targets.

--- 
## Section 4 — Testing & Quality Assurance

---

### P4.1 — C Unit Tests for Parser

**Role/Context:** Testing the most critical C component before integration.  
**Objective:** Write a standalone C test suite for `parser.c` covering all parse paths.  
**Expected Output:** `test/parser.test.c` compilable with `gcc test/parser.test.c src/parser.c -o test_parser`.

**Test cases to cover:**
- [ ] GET with no body → `PARSE_DONE`, `body_present = 0`
- [ ] POST with body fitting in buffer → `PARSE_DONE`, `body_start` correct
- [ ] POST with `Content-Length` > buffer → `PARSE_NEED_MORE`, correct `body_length`
- [ ] `parse_append_body` completes a partial body correctly
- [ ] Malformed request (no CRLF) → `PARSE_ERROR`
- [ ] Request with unusual but valid header casing (`content-length` vs `Content-Length`)

**Expected outcome:** `./test_parser` exits 0 with all PASS lines.

---

### P4.2 — C Unit Tests for Router

**Role/Context:** Validating trie correctness and zero-allocation guarantee.  
**Objective:** Write `test/router.test.c` covering route registration and lookup.  
**Expected Output:** Standalone compilable test binary.

**Test cases:**
- [ ] Exact match: `GET /health` matches `GET /health`
- [ ] Param extraction: `GET /users/42` → `id = "42"`
- [ ] Multi-param: `/users/:id/posts/:postId`
- [ ] Method mismatch: `POST /health` does not match `GET /health`
- [ ] 404: unknown path returns NULL
- [ ] Route conflict: `GET /users/me` (exact) takes priority over `GET /users/:id`

**Expected outcome:** `./test_router` exits 0 with all PASS lines.

---

### P4.3 — Node.js Integration Tests

**Role/Context:** Testing the full stack — from JS API call down through C and back.  
**Objective:** Write integration tests using Node's built-in `node:test` runner and `fetch` for HTTP calls.  
**Expected Output:** `test/integration.test.js`

**Checklist:**
- [ ] Start server in `before()`, stop in `after()`
- [ ] Test: `GET /health` → 200 `{ status: 'ok' }`
- [ ] Test: `POST /echo` with JSON body → 200 echoes body
- [ ] Test: Unknown route → 404
- [ ] Test: Middleware runs before handler (verify ordering)
- [ ] Test: `res.status(201).json(...)` sets correct status code
- [ ] Test: Body larger than `maxBodySize` → 413

**Constraints:** Use `node:test` (built-in, no Jest). Use native `fetch` (Node 18+). No test framework dependencies.

**Expected outcome:** `npm test` passes all integration tests on macOS and Linux CI.

---

### P4.4 — Large Body & Streaming Tests

**Role/Context:** Validating the dynamic body path that diverges from the fast path.  
**Objective:** Write tests specifically for `maxBodySize`, `streamBody`, and chunked transfer.  
**Expected Output:** `test/streaming.test.js` and `test/large-body.test.js`

**Checklist:**
- [ ] 1KB body (fits in buffer) → handler receives complete `req.body`
- [ ] 10KB body (exceeds default 4KB) → handler receives complete `req.body` after buffering
- [ ] Body at exactly `maxBodySize` → succeeds
- [ ] Body at `maxBodySize + 1` → 413 response
- [ ] `streamBody: true` → `req.on('data')` fires multiple times, `req.on('end')` fires once
- [ ] Streaming: total bytes received across all chunks = `Content-Length`

**Expected outcome:** All cases pass; no memory leaks on repeated large-body requests.

---

### P4.5 — Benchmark Regression Tests

**Role/Context:** Ensuring performance doesn't regress between commits.  
**Objective:** Write an automated benchmark that fails CI if req/s drops below a threshold.  
**Expected Output:** `benchmark/regression.js` + CI step addition.

**Checklist:**
- [ ] Start server, run `autocannon` (programmatic, no external process) for 5 seconds
- [ ] Assert: `result.requests.mean >= THRESHOLD` (set thresholds per platform in config)
- [ ] Platform thresholds: Linux 300k, macOS 80k, Windows 50k (conservative for CI VMs)
- [ ] Print formatted table comparing current vs threshold
- [ ] Exit code 1 if below threshold (fails CI)

**Constraints:** Use `autocannon` (npm) for programmatic benchmarks. Do not use `wrk` in CI (not available on all runners).

**Expected outcome:** `npm run bench:ci` exits 0 on healthy code, 1 on regression.

---

### P4.6 — Memory & Leak Testing

**Role/Context:** Native addons are the most common source of Node.js memory leaks.  
**Objective:** Run the server under load and verify no memory growth over time.  
**Expected Output:** `test/memory.test.js` + instructions for Valgrind on Linux.

**Checklist:**
- [ ] Send 100,000 requests programmatically (via `autocannon`)
- [ ] Sample `process.memoryUsage().heapUsed` before and after
- [ ] Assert: memory growth < 5MB (accounts for V8 GC variance)
- [ ] Provide `npm run memcheck` script: `valgrind --leak-check=full node server.js` (Linux only)
- [ ] Document expected Valgrind output (some "still reachable" from V8 is normal)

**Expected outcome:** Zero "definitely lost" blocks in Valgrind output; Node heap stable over 100k requests.

---

## Section 5 — Documentation & Usage

---

### P5.1 — README.md

**Role/Context:** You are writing the primary documentation for an open-source performance-focused Node.js module.  
**Objective:** Write a complete `README.md` that covers install, quickstart, API reference, performance numbers, and platform support.  
**Expected Output:** `README.md` (~300–500 lines).

**Sections to include:**
- [ ] Hero: benchmark numbers table (from PLAN.md expected results)
- [ ] Install: `npm install iopress` + platform prerequisites (liburing on Linux)
- [ ] Quickstart: 10-line working example
- [ ] API Reference: all methods with signatures and examples
- [ ] Options: `initialBufferSize`, `maxBodySize`, `streamBody` — when and why to change each
- [ ] Platform Support table: Linux/macOS/Windows + expected performance tier
- [ ] Migrating from Express: link to `COMPATIBILITY.md`
- [ ] Building from source: `npm run build` steps

**Expected outcome:** A new user can go from `npm install` to a working server in under 5 minutes using only the README.

---

### P5.2 — API Reference (JSDoc → Typedoc)

**Role/Context:** Generating API docs from inline code comments.  
**Objective:** Add complete JSDoc to `js/index.js` and generate HTML docs.  
**Expected Output:** JSDoc-annotated `js/index.js` + `npm run docs` script.

**Checklist:**
- [ ] `@param`, `@returns`, `@example` for every public method
- [ ] `@throws` documented for methods that can throw
- [ ] `@since` tags for v1 vs planned v2 features
- [ ] `npm run docs` runs `typedoc --entryPoints js/index.js`
- [ ] Output to `docs/` directory (add to `.gitignore`, serve via GitHub Pages)

**Expected outcome:** `npm run docs` generates browsable HTML API reference.

---

### P5.3 — Examples Directory

**Role/Context:** Examples are the fastest way users validate the module works for their use case.  
**Objective:** Write 4 self-contained examples covering the most common patterns.  
**Expected Output:** `examples/basic.js`, `examples/rest-api.js`, `examples/middleware.js`, `examples/upload.js`

**Each example must:**
- [ ] Work with zero configuration after `npm install && npm run build`
- [ ] Include a comment block at the top explaining what it demonstrates
- [ ] Include `curl` commands to test each route
- [ ] `upload.js` specifically: demonstrate streaming body with `streamBody: true`

**Expected outcome:** `node examples/basic.js` runs without modification and produces expected output.

---

### P5.4 — Migration Guide (Express → ExpressPro)

**Role/Context:** Most users will be migrating existing Express apps, not starting fresh.  
**Objective:** Write a step-by-step migration guide with code diffs showing before/after.  
**Expected Output:** `MIGRATION.md`

**Checklist:**
- [ ] Step 1: Replace `require('express')` with `require('iopress')`
- [ ] Show: what works identically (routes, middleware, `res.json`, `req.params`)
- [ ] Show: what needs change (`express.static` → serve files manually or use a CDN)
- [ ] Show: what doesn't exist yet (template engines, `res.render`, cookies)
- [ ] Include a diff of a real Express app converted to ExpressPro
- [ ] Note: Linux-only performance tier — document graceful degradation on other OSes

**Expected outcome:** An experienced Express developer can assess migration cost in 10 minutes.

---

### P5.5 — Changelog & Release Notes Template

**Role/Context:** Maintaining a predictable changelog format for open-source releases.  
**Objective:** Set up `CHANGELOG.md` using Keep a Changelog format and document the v1.0.0 entry.  
**Expected Output:** `CHANGELOG.md` + commit message convention docs.

**Checklist:**
- [ ] Format: `## [version] — YYYY-MM-DD` with Added / Changed / Fixed / Removed sections
- [ ] v1.0.0 entry: document all initial features
- [ ] `CONTRIBUTING.md`: commit message convention (feat/fix/perf/docs/chore)
- [ ] Link to conventional commits spec

**Expected outcome:** `CHANGELOG.md` is human-readable and tooling-compatible (standard-version, semantic-release).

---


⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️⭐️


## Section 6 — Release & Maintenance

---

### P6.1 — Semantic Versioning & Automated Releases

**Role/Context:** Setting up automated version bumping and changelog generation.  
**Objective:** Configure `semantic-release` or `standard-version` to automate releases on merge to main.  
**Expected Output:** `.releaserc` config + GitHub Actions release workflow.

**Checklist:**
- [ ] Install `standard-version` as devDependency
- [ ] `npm run release` bumps version, updates CHANGELOG, creates git tag
- [ ] GitHub Actions: on push to `main` → run tests → if passing, `npm run release` → `npm publish`
- [ ] Add `"publishConfig": { "access": "public" }` to `package.json`
- [ ] Ensure `build/` is excluded from npm package but prebuilt binaries are included

**Expected outcome:** Merging to `main` triggers test → release → publish pipeline automatically.

---

### P6.2 — Prebuilt Binary Distribution (node-pre-gyp)

**Role/Context:** Requiring users to compile C from source is a major install friction point.  
**Objective:** Set up `@mapbox/node-pre-gyp` to distribute prebuilt `.node` binaries per platform.  
**Expected Output:** Updated `package.json`, `binding.gyp`, and GitHub Actions upload step.

**Checklist:**
- [ ] Install `@mapbox/node-pre-gyp`
- [ ] Add `binary` field to `package.json` with S3 or GitHub Releases URL
- [ ] Update `binding.gyp` to use `node_pre_gyp` path conventions
- [ ] GitHub Actions: build matrix → upload binaries to GitHub Releases
- [ ] `install` script: `node-pre-gyp install --fallback-to-build`
- [ ] Test: `npm install` on a machine without build tools succeeds by downloading prebuilt

**Expected outcome:** `npm install iopress` works on Linux/macOS/Windows without compiler installed.

---

### P6.3 — Deprecation Policy & Long-Term Support

**Role/Context:** Defining how you'll manage breaking changes and communicate them to users.  
**Objective:** Write a deprecation policy and set up runtime deprecation warnings.  
**Expected Output:** `DEPRECATION_POLICY.md` + code pattern for runtime warnings.

**Checklist:**
- [ ] Policy: deprecated features show `process.emitWarning` for 2 minor versions before removal
- [ ] Policy: breaking changes only in major versions (semver)
- [ ] Pattern: `if (options.legacyMode) { process.emitWarning('legacyMode deprecated in v1.3, removed in v2.0', 'DeprecationWarning'); }`
- [ ] Add `node --pending-deprecation` to test scripts to surface warnings in CI

**Expected outcome:** Users are never surprised by removals; deprecations are visible in logs and CI.

---

### P6.4 — Security Policy & Vulnerability Response

**Role/Context:** A native addon with a C HTTP parser is a security-sensitive component.  
**Objective:** Write a `SECURITY.md` and set up automated dependency scanning.  
**Expected Output:** `SECURITY.md` + GitHub Actions security scan step.

**Checklist:**
- [ ] `SECURITY.md`: private disclosure process (GitHub Security Advisory or email)
- [ ] Response SLA: acknowledge in 48 hours, patch in 7 days for critical
- [ ] GitHub Dependabot enabled for npm dependencies
- [ ] Add `npm audit` step to CI — fail on high/critical
- [ ] Document known attack surfaces: HTTP request smuggling, body size limits, path traversal in router

**Expected outcome:** Security researchers know how to report issues; CI catches vulnerable dependencies.

---

### P6.5 — Contribution Guide

**Role/Context:** Making it easy for external contributors to add platform backends or features.  
**Objective:** Write `CONTRIBUTING.md` covering dev setup, coding standards, and the PR process.  
**Expected Output:** `CONTRIBUTING.md`

**Checklist:**
- [ ] Dev setup: prerequisites per OS (liburing on Linux, Xcode CLI on macOS)
- [ ] How to add a new platform backend: implement `server.h` interface, add to `binding.gyp`
- [ ] Hot path rules: no malloc, no locks, use `likely()`/`unlikely()`
- [ ] Testing requirements: new features need unit tests + integration tests
- [ ] PR checklist: linting, tests pass, benchmark doesn't regress, CHANGELOG updated

**Expected outcome:** A contributor can submit a working Windows IOCP backend by following the guide alone.

---

### P6.6 — v2 Roadmap Stub

**Role/Context:** Communicating future direction to users and contributors.  
**Objective:** Document the v2 roadmap based on known v1 limitations.  
**Expected Output:** `ROADMAP.md`

**Planned v2 features (from PLAN.md constraints):**
- [ ] HTTP/2 support via `nghttp2`
- [ ] HTTPS via `SSL_CTX` + platform TLS integration
- [ ] `app.use((err, req, res, next) => {})` error middleware
- [ ] `express.static` equivalent (sendfile via io_uring / sendfile syscall)
- [ ] Worker thread pool for CPU-bound handlers

**Expected outcome:** Contributors can pick up v2 items as tracked issues.

---

## Example: Minimal Feature Chain

> Implementing `res.redirect(url, code?)` from design → implementation → tests → docs.

```
PROMPT CHAIN: res.redirect feature
════════════════════════════════════════════════════════════════

[P-A] API DESIGN
"In js/index.js, design res.redirect(url, code = 302). It should:
 - Set Location header and status code
 - Call res.end() (no body needed)
 - Be chainable: res.status(301).redirect('/new-path') should NOT double-send
 - Return void
Show the method signature and a 3-line implementation sketch only."

Expected output: method signature + pseudocode

────────────────────────────────────────────────────────────────

[P-B] IMPLEMENTATION
"Based on the design above, implement res.redirect in js/index.js.
 Context: res is built in the handler dispatcher function around line [X].
 res.end() calls binding.SendResponse(fd, this._status, '').
 Constraint: do not add new native binding functions — use existing SendResponse."

Expected output: 10-15 lines of JS

────────────────────────────────────────────────────────────────

[P-C] TESTS
"Write node:test tests for res.redirect in test/integration.test.js.
 Cases: 302 default, 301 explicit, location header correct, body is empty.
 Use existing server setup from the test file (already starts server in before()).
 Do not add new test infrastructure."

Expected output: 4 test cases (~30 lines)

────────────────────────────────────────────────────────────────

[P-D] DOCS
"Add res.redirect(url, code?) to:
 1. index.d.ts — TypeScript signature
 2. README.md API Reference section — one sentence description + one example
 3. CHANGELOG.md — one line under [Unreleased] > Added
 Show only the diff lines, not full file contents."

Expected output: 3 minimal diffs

════════════════════════════════════════════════════════════════
```

---

## Progress Summary Block Template

> Copy this at the end of each session. Paste at the top of the next prompt.

```
=== PROGRESS SUMMARY ===
Date: [YYYY-MM-DD]
Last prompt completed: [P-X.Y — Title]
Status:
  - [x] Scaffolding complete (P1.1–P1.5)
  - [x] server.h interface defined (P2.3)
  - [ ] parser.c in progress (P3.1) — fast path done, slow path TODO
  - [ ] All else: not started
Last known good state: npm run build passes on macOS, test/parser binary exits 0
Blockers: napi_threadsafe_function lifetime management unclear — see P3.4 notes
Next: Continue P3.1 — implement parse_append_body
Files modified this session: src/parser.c, src/server.h
Open questions:
  1. Should parse_append_body allocate or expect caller-provided buffer?
  2. QUEUE_DEPTH tuning — 4096 or 8192 for io_uring?
=========================
```

---

*Playbook version: 1.0 · Matches ExpressPro PLAN.md v2 · MIT*
