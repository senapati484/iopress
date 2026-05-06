# @iopress/core Full-Stack Audit & Validation Plan

**Date:** 2026-05-06  
**Status:** COMPLETED  
**Approach:** Inside-Out (Native -> N-API -> JavaScript -> Stress)

## 1. Objective
To perform a rigorous, "one-by-one" validation of the `@iopress/core` codebase to ensure it is production-ready, memory-safe, and meets its performance targets.

## 2. Scope of Audit

### Phase 1: Native Component Validation (C)
Verify the core logic of the server without the overhead of Node.js.
- **Parser (`src/parser.c`):** Validate HTTP parsing against standard and malformed requests.
- **Fast Router (`src/fast_router.c` / `src/router.c`):** Verify O(1) and dynamic route matching.
- **Platform Backend (`src/server_kevent.c`):** Audit the `kqueue` event loop for edge-case handling (connection resets, partial reads).

### Phase 2: N-API Binding & JS API Verification
Verify the integration between the native layer and the Node.js runtime.
- **Binding Layer (`src/binding.c`):** Ensure thread safety and correct data conversion between C and JS.
- **Functional API (`js/index.js`):** Run the complete test suite (`test/*.js`) to verify Express compatibility, middleware chaining, and error handling.

### Phase 3: Stability & Performance
Test the system under realistic and extreme conditions.
- **Memory Safety:** Use `leaks` (macOS) and `test/memory.test.js` to identify native or JS-level memory leaks.
- **Performance:** Validate 200k+ RPS target on macOS using `npm run bench`.
- **Stress:** Test high concurrency (1000+ simultaneous connections) for stability.

## 3. Results Summary
- **Native Logic:** Verified stable.
- **Stability:** Fixed keep-alive and partial write issues.
- **Performance:** ~145k RPS on macOS.

## 4. Post-Audit Implementation
- **Write Queue:** Implemented non-blocking write queue for handling large responses and partial writes.
- **N-API Reset:** Fixed connection state reset for keep-alive connections.
- **Fast Path:** Added method validation to fast-path routes.
