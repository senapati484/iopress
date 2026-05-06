# @iopress/core Audit Results - 2026-05-06

## 1. Executive Summary
`@iopress/core` delivers impressive performance on macOS, achieving **144,362 req/s** (approx. 10x speedup over Express.js). The core HTTP parser and native router are stable and passed 100% of their unit tests. However, the system requires architectural improvements in output buffering and platform parity before being considered production-ready.

## 2. Test Results

### Phase 1: Native Components (C)
| Component | Test File | Result | Notes |
|-----------|-----------|--------|-------|
| HTTP Parser | `test_parser.c` | ✅ 22/22 PASS | Handles malformed and edge-case requests correctly. |
| Router | `test_router.c` | ✅ 22/22 PASS | Trie-based matching and param extraction are robust. |
| kqueue Backend | `server_kevent.c` | ⚠️ AUDITED | **BUG FOUND:** Fast-path ignored HTTP methods (fixed). |

### Phase 2: JS API & Bindings
| Component | Test Command | Result | Notes |
|-----------|--------------|--------|-------|
| Integration Tests | `npm test` | ✅ PASS | All 18+ tests pass after fast-path and keep-alive fixes. |
| Memory Stability | `memory.test.js` | ✅ PASS | Flat memory curve over 1,000 requests. |
| Express Compat | Integration | ✅ PASS | Middleware and route handlers behave as expected. |

### Phase 3: Performance Benchmarks
**Environment:** macOS (arm64), kqueue backend.

| Metric | Result | Target | Status |
|--------|--------|--------|--------|
| Requests/sec | 144,362 | 215,000 | ⚠️ BELOW TARGET |
| Latency (Avg) | 34.45 ms | < 10 ms | ⚠️ BELOW TARGET |
| Stability (Stress) | 1000 conn | PASS | No crashes during high-concurrency burst. |

## 3. Key Findings & Bug Fixes

### Fixed During Audit
1.  **Fast Path Method Masking:** Fixed a bug where `server_kevent.c` would return static responses for `POST` or `PUT` requests to paths like `/health`.
2.  **Health Check Response:** Updated the static `/health` response to match the expected `{"status":"ok"}` JSON format.
3.  **Keep-Alive State Reset:** Fixed a bug in `binding.c` where the connection buffer and state were not reset after a JS-handled request, breaking pipelining.

### Identified Critical Risks
1.  **Partial Write Failure:** The server currently uses non-blocking `write()` without handling `EAGAIN`. Responses larger than the socket buffer (~32KB+) will be truncated or fail.
2.  **Platform Inconsistency:** The Linux (`io_uring`) and Windows (`IOCP`) backends are missing the "Fast Path" and "Zero-Allocation" optimizations present in the macOS backend.
3.  **Thread Safety:** While TSFN is used, the sharing of the `connection_t` pointer between C and JS relies on the JS layer finishing before the C layer reuses the buffer.

## 4. Conclusion
**Status: STABLE BUT INCOMPLETE.**
The foundation is highly performant. The next development phase must focus on implementing a proper output write queue and achieving feature parity for Linux `io_uring`.
