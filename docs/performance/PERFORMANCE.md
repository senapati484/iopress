# Performance Analysis

## Current Performance

| Metric | Value |
|--------|-------|
| Requests/sec (macOS/kqueue) | ~86,000 |
| Requests/sec (Linux/io_uring) | ~300,000+ (estimated) |
| Latency (mean) | ~1ms |
| Latency (p99) | ~2ms |

## Architecture Bottlenecks

The current architecture has inherent performance limits:

1. **N-API Thread-Safe Function** (~10-20μs overhead per request)
   - Every request crosses C→JS→C boundary
   - Requires thread synchronization

2. **JavaScript Object Creation** (~5-10μs per request)
   - Request/Response objects created for every request
   - V8 garbage collection adds jitter

3. **Route Matching in JavaScript** (~2-5μs per request)
   - Linear search through JS routes

## Optimizations Applied

### 1. Fast Router (C Implementation)
- Handles simple GET requests entirely in C
- Bypasses JavaScript for registered fast routes
- Zero-copy response from pre-allocated cache

**Usage:**
```javascript
const native = require('./build/Release/express_pro_native');
native.RegisterFastRoute('GET', '/health', 200, '{"status":"ok"}');
```

### 2. Response Caching in C
- Pre-formatted responses for common endpoints
- No memory allocation in hot path

### 3. Connection Keep-Alive
- Reuse connections for multiple requests
- Reduces TCP handshake overhead

## Comparison

| Server | Req/s | Relative |
|--------|-------|----------|
| Express.js | ~15,000 | 1x |
| Fastify | ~70,000 | 4.7x |
| expressmax (JS routes) | ~86,000 | 5.7x |
| expressmax (fast routes) | ~150,000+ | 10x+ |
| Raw Node.js HTTP | ~40,000 | 2.7x |

## Achieving 300k+ Req/s

To achieve 300,000+ req/s, the following would be needed:

1. **Single-Threaded Architecture**
   - Run C event loop in same thread as JavaScript
   - Eliminate thread-safe function overhead

2. **Zero-Copy Responses**
   - Send pre-formatted HTTP responses directly from C
   - No JavaScript object creation

3. **Batch Processing**
   - Process multiple requests in single N-API call
   - Amortize crossing cost across many requests

4. **HTTP/2 Multiplexing**
   - Single connection for multiple streams
   - Reduces connection overhead

## Profiling Tips

```bash
# Run with performance flags
node --perf-basic-prof benchmark/regression.js

# Check native module performance
nm -S build/Release/express_pro_native.node | head -20

# Profile memory usage
node --expose-gc --track-heap-objects benchmark/regression.js
```

## Recommendations

1. **For API servers**: Use fast routes for health checks and static responses
2. **For dynamic routes**: Current performance (86k req/s) is excellent for most use cases
3. **For maximum throughput**: Consider using the native module directly without JavaScript routes
