# Performance Mechanics

`@iopress/core` is designed for maximum throughput and minimal latency. This document explains the core architectural principles that allow it to outperform traditional Node.js frameworks by up to 30x.

## Core Architectural Principles

### 1. Platform-Native Async I/O
While Node.js uses `libuv` as a generic abstraction layer, `@iopress/core` bypasses this for its HTTP engine and speaks directly to the kernel using the most efficient APIs available:
- **Linux**: `io_uring` (Kernel 5.1+)
- **macOS**: `kqueue`
- **Windows**: `IOCP` (I/O Completion Ports)

This reduced abstraction allows for batching multiple syscalls into a single kernel entry, significantly reducing context-switching overhead.

### 2. Fast Path Routing (C-Level)
One of the biggest bottlenecks in Node.js servers is the cost of crossing the bridge between the native C++ layer and the JavaScript engine. 
`@iopress/core` implements a **Fast Path Router** entirely in C. When a request matches a pre-registered static route (like `/health` or `/ping`):
- The response is generated from a **pre-computed template** in native memory.
- The response is sent directly via the kernel buffer.
- **Zero JavaScript** is executed for these requests.

This allows static routes to handle **500k+ req/s** on Linux, while dynamic routes (which fall back to JS) handle ~30k-50k req/s.

### 3. Unified Write Queue
To handle large responses (>32KB) without blocking the event loop or truncating data, we implemented a unified non-blocking write queue. Each platform uses its native async primitive to drain the queue:
- **kqueue**: Uses `EVFILT_WRITE` with edge-triggered events.
- **io_uring**: Uses async `SEND` operations with completion queue tracking.
- **IOCP**: Uses `WSASend` with overlapped I/O.

### 4. Zero-Allocation Parsing
Our HTTP parser (`parser.c`) is designed to be extremely fast and memory-efficient. It uses pointer arithmetic to reference slices of the original buffer instead of allocating new strings for headers or paths during the initial pass.

---

## Performance Benchmark Comparison

| Framework | Platform | Throughput (req/s) | Latency (p99) |
|-----------|----------|-------------------|---------------|
| **iopress** | **Linux (io_uring)** | **500,000+** | **<1ms** |
| **iopress** | **macOS (kqueue)** | **218,572** | **<1ms** |
| **iopress** | **Windows (IOCP)** | **100,000+** | **<5ms** |
| Express.js | Any | ~17,000 | ~20ms |

*Benchmarks performed on Apple M2 (macOS) and Ubuntu 22.04 (Linux).*

---

## Optimization Techniques

### Batch Processing
The event loop doesn't just process one event at a time. It batches them:
- **Event Batching**: Up to 1024 events are pulled from the kernel in one `kevent` or `io_uring_wait_cqe` call.
- **Accept Batching**: Up to 512 new connections are accepted in a single wakeup.
- **Pipeline Batching**: Up to 64 HTTP requests are processed per connection before yielding back to the loop.

### Socket Tuning
Every socket opened by `@iopress/core` is automatically tuned for performance:
- `TCP_NODELAY`: Disabled Nagle's algorithm for immediate packet delivery.
- `SO_SNDBUF/SO_RCVBUF`: Increased to 1MB to handle bursty traffic.
- `SO_REUSEPORT`: Enables kernel-level load balancing across multiple worker processes.

### Memory Architecture
The server uses a pre-allocated pool of buffers to prevent frequent memory allocation and garbage collection pressure. This keeps the memory footprint stable even under extreme load.

---

## Performance Tips for Developers

1.  **Prefer Static Routes**: Use simple paths without parameters for health checks, pings, and high-frequency heartbeats to benefit from the C-level fast path.
2.  **Enable Keep-Alive**: Ensure your clients use persistent connections to avoid the overhead of TCP handshakes.
3.  **Optimize JSON**: Since dynamic routes use JavaScript, the bottleneck is often `JSON.stringify`. For ultra-high performance, consider pre-serializing common responses.
4.  **Use Worker Threads**: On multi-core systems, use the `workers` option in `app.listen()` to scale linearly across CPU cores.

---

**Back to [Reference](../README.md)**
