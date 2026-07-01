## v1.0.5 — Performance & Stability Release

This release brings significant performance improvements, critical bug fixes, and broader platform compatibility.

### Performance

- **3x fewer heap allocations per request**: method, path, and query strings are now merged into a single `malloc` instead of three separate ones
- **Faster `Response.send()`**: `typeof` fast-path avoids the `String()` built-in call for string data
- **Reduced N-API overhead**: stack buffer in `SendResponse` halves the number of N-API calls for header extraction

### Critical Bug Fixes

- **`req.headers` is no longer empty on the native path** — Express-compatible header objects are now correctly populated
- **Chunked body assembly fixed**: raw chunked format is no longer passed to JS handlers; the dechunked body is delivered as expected
- **`bytes_consumed` corrected for chunked requests** — fixes keep-alive and pipelining behavior
- **Parser robustness**: validated chunk sizes prevent over-read scenarios (thanks to @servelt for the report)

### Platform & Compatibility

- **Windows CI now builds successfully** on MSVC — replaced C11 `<stdatomic.h>` with `_InterlockedExchangeAdd64` intrinsics
- **Node.js 16+** minimum for wider npm compatibility
- **uuid CVE fixed** — dev dependency updated to 11.1.1 via overrides
- **Windows IOCP backend**: fixed buffer mismatch where data was read into `ov->buffer` but the binding read from `conn->buffer`

### New APIs

- **`app.metrics()`** — exposes atomic counters for pending/total/drops/errors

### Chores

- `node-gyp` moved to runtime dependencies for `npm install` scenarios
- Pre-commit formatting checks enforced
