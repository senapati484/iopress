# @iopress/core Performance Audit Context

## 1. Environment and Scope

### 1.1 Repository Access
- **Repository**: https://github.com/senapati484/iopress (public)
- **Package name**: `@iopress/core` (npm)
- **Local path**: `/Users/sayansenapati/Desktop/Dev/Innovation/express-pro`

### 1.2 Supported Runtimes
- **Node.js**: v18.0.0+ (LTS 18.x, 20.x, 22.x)
- **Package manager**: npm (v9+)
- **Platforms**:
  - Linux (kernel 5.1+ with io_uring support)
  - macOS 10.14+
  - Windows 10/Server 2016+

### 1.3 Acceptance Criteria
| Platform | Target RPS | Minimum RPS | p99 Latency |
|----------|------------|-------------|-------------|
| Linux (io_uring) | ≥500,000 | ≥300,000 | <2ms |
| macOS (kqueue) | ≥150,000 | ≥80,000 | <5ms |
| Windows (IOCP) | ≥100,000 | ≥50,000 | <8ms |
| Express.js baseline | ~20,000 | N/A | <50ms |

- Results must be consistent within ±5% across 3+ runs
- Benchmark harness must be automated and reproducible

---

## 2. Codebase Architecture

### 2.1 Core Modules

```
src/
├── server_uring.c      # Linux io_uring backend (main implementation)
├── server_kevent.c    # macOS kqueue backend
├── server_iocp.c      # Windows IOCP backend
├── server_libuv.c     # libuv fallback
├── parser.c           # HTTP request parser
├── fast_router.c      # C-level route handler (optional)
├── binding.c          # Node.js NAPI binding
└── include/
    ├── server.h       # Server interface
    └── common.h       # Common types

js/
└── index.js           # JavaScript API layer

index.js               # Main entry point
```

### 2.2 Routing Strategy
- **Primary routing**: JavaScript-based in `js/index.js` (class `iopress`)
- **Fast path (optional)**: C-level router in `fast_router.c` for static routes
- Route matching: Linear search with parameter extraction (`:param` syntax)
- Middleware chain execution with async support

### 2.3 Middleware Design
- Pattern: `(req, res, next) => void`
- Support for path-specific middleware via `app.use(path, handler)`
- Terminal middleware for catch-all handlers
- Error handler: `(err, req, res, next) => void`

### 2.4 Platform-Specific Logic

| Platform | Backend | File | Key Features |
|----------|---------|------|--------------|
| Linux | io_uring | `server_uring.c` | Queue depth 256, batch submission |
| macOS | kqueue | `server_kevent.c` | Kevent-based event loop |
| Windows | IOCP | `server_iocp.c` | Windows async I/O |
| Fallback | libuv | `server_libuv.c` | Node.js core fallback |

---

## 3. Performance Goals

### 3.1 Speed Claims (from README)
- **Linux + io_uring**: 500,000+ req/s
- **macOS + kqueue**: 150,000+ req/s
- **Windows + IOCP**: 100,000+ req/s
- **Express.js**: ~20,000 req/s

### 3.2 Deployment Targets
- High-throughput API servers
- Microservices with low latency requirements
- Real-time applications
- Replacement for Express.js in performance-critical paths

### 3.3 Known Bottlenecks and Risk Areas

| Area | Risk Level | Description |
|------|------------|-------------|
| Route matching | Medium | Linear O(n) search; no_radix tree |
| JavaScript overhead | High | JS router adds latency vs C fast path |
| Body parsing | Low | Built-in, but adds overhead |
| No HTTP/2 | Medium | Only HTTP/1.1 currently supported |
| Connection pooling | Low | Basic implementation in C |

---

## 4. Benchmark Results Baseline

### 4.1 Expected Baseline (from package docs)
| Server | Requests/sec | Latency (p99) |
|--------|-------------|---------------|
| @iopress/core (io_uring) | 520,000 | 0.8ms |
| @iopress/core (kqueue) | 155,000 | 2.1ms |
| @iopress/core (IOCP) | 105,000 | 3.5ms |
| Node.js http | 45,000 | 8.2ms |
| Express.js | 18,000 | 22ms |
| Fastify | 65,000 | 5.1ms |

### 4.2 Test Environment Assumptions
- **Hardware**: AMD Ryzen 9 5900X or equivalent
- **OS**: Platform-specific latest stable
- **Node.js**: v20 LTS
- **Warm-up**: 2-5 seconds before measurement
- **Duration**: 5-10 seconds per test

---

## 5. Verification Checklist

- [ ] Native addon builds successfully on target platform
- [ ] Server starts and accepts connections
- [ ] All HTTP methods work (GET, POST, PUT, DELETE, PATCH)
- [ ] Route parameters are correctly extracted
- [ ] Query strings are parsed
- [ ] JSON body parsing works
- [ ] Middleware chain executes correctly
- [ ] Error handling works
- [ ] Benchmark harness runs without errors
- [ ] Results are consistent across multiple runs

---

## 6. Notes

- This package uses native C code compiled via node-gyp
- Requires platform-specific build tools (see README)
- Linux requires liburing-dev for io_uring support
- The fast router is disabled by default; can be enabled explicitly
