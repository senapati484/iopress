# express-pro Roadmap

This document outlines the planned features and direction for express-pro. Roadmap items are organized by milestone and version.

## Current Status: v1.0

**Focus**: Stable HTTP/1.1 server with Express-compatible API and maximum performance.

**Complete**:
- Native HTTP server with platform-specific async I/O (io_uring/kqueue/IOCP)
- Express-compatible API (routing, middleware, request/response objects)
- TypeScript support
- Prebuilt binary distribution
- Comprehensive test suite and benchmarks
- Security policy and vulnerability response process

## v1.x Maintenance

**Focus**: Stability, bug fixes, performance optimizations.

**Accepted Contributions**:
- Bug fixes for HTTP/1.1 edge cases
- Performance improvements (must show benchmark gains)
- Platform-specific optimizations
- Documentation improvements
- Additional platform backends (FreeBSD, Solaris)

**Will Not Accept in v1**:
- Breaking API changes
- HTTP/2 (v2 feature)
- Native HTTPS (v2 feature)
- Major architectural changes

## v2.0 — Production-Ready Platform

**Target**: Q3 2026  
**Focus**: Modern HTTP features and production readiness.

### v2.0 Features

#### 1. HTTP/2 Support
**Status**: Planned  
**Priority**: High  
**Effort**: Large

- [ ] Integrate `nghttp2` for HTTP/2 framing
- [ ] Push promise support for server push
- [ ] Stream multiplexing per connection
- [ ] ALPN negotiation (h2, http/1.1)
- [ ] Priority and flow control

**API Considerations**:
```javascript
const app = expresspro({
  http2: true,
  allowHTTP1: true  // Upgrade from HTTP/1.1
});

// Server push
res.push('/static/style.css', {
  ':authority': 'example.com',
  ':method': 'GET'
});
```

**Platform Notes**:
- HTTP/2 over TCP will use existing io_uring/kqueue/IOCP backends
- Requires `nghttp2` as optional dependency

---

#### 2. Native HTTPS Support
**Status**: Planned  
**Priority**: High  
**Effort**: Medium

- [ ] `SSL_CTX` integration in C layer
- [ ] Platform-specific TLS optimizations
  - Linux: OpenSSL + io_uring async TLS (OpenSSL 3.0+)
  - macOS: SecureTransport integration
  - Windows: Schannel (Windows TLS) support
- [ ] Certificate hot-reload
- [ ] OCSP stapling
- [ ] Session resumption

**API**:
```javascript
const app = expresspro();

app.listen(443, {
  tls: {
    key: fs.readFileSync('server.key'),
    cert: fs.readFileSync('server.crt'),
    // Optional: custom CA, cipher suites, etc.
  }
});
```

**Dependencies**:
- `node-addon-api` will need OpenSSL headers
- Optional: `boringssl` for consistent TLS across platforms

---

#### 3. Enhanced Error Middleware
**Status**: Planned  
**Priority**: Medium  
**Effort**: Small

- [ ] Support Express-style error middleware signature:
  ```javascript
  app.use((err, req, res, next) => {
    // Error handling logic
    res.status(500).json({ error: err.message });
  });
  ```
- [ ] Async error catching (unhandled promise rejections in handlers)
- [ ] Error handler ordering (respect middleware chain position)
- [ ] Default error handler with stack traces (dev mode) / sanitized (production)

**Breaking Change**: Yes (new middleware type) — targets v2.0

---

#### 4. Static File Serving
**Status**: Planned  
**Priority**: Medium  
**Effort**: Medium

- [ ] `expresspro.static()` middleware
- [ ] Platform-optimized sendfile:
  - Linux: `io_uring` with `IORING_OP_SEND`
  - macOS: `sendfile()` syscall
  - Windows: `TransmitFile` (IOCP)
- [ ] Range request support (HTTP 206)
- [ ] ETag generation and conditional requests
- [ ] Gzip/Brotli pre-compressed file support

**API**:
```javascript
app.use(expresspro.static('public', {
  maxAge: '1d',
  etag: true,
  lastModified: true,
  setHeaders: (res, path) => {
    res.set('X-Custom-Header', 'value');
  }
}));
```

**Performance Target**: Zero-copy file serving where platform supports it.

---

#### 5. Worker Thread Pool
**Status**: Planned  
**Priority**: Medium  
**Effort**: Large

- [ ] Offload CPU-bound handlers to worker threads
- [ ] Transparent marshaling of `req`/`res` objects
- [ ] Automatic load balancing across workers
- [ ] Shared buffer pool for zero-copy where possible

**API**:
```javascript
// Option 1: Explicit worker pool
const worker = require('express-pro/worker');

app.get('/compute', worker((req, res) => {
  // Runs in worker thread
  const result = heavyComputation(req.body.data);
  res.json({ result });
}));

// Option 2: Automatic detection based on CPU usage
app.listen(3000, {
  workers: os.cpus().length,
  offloadThreshold: 50  // % CPU usage before offloading
});
```

**Platform Support**:
- Node.js worker_threads integration
- Native N-API thread-safe functions
- Requires careful handling of liburing/kqueue contexts

---

### v2.0 Infrastructure

#### Build System
- [ ] Support for optional dependencies (`nghttp2`, TLS backends)
- [ ] Feature flags in `binding.gyp` (e.g., `--without-http2`)
- [ ] Better Windows build experience (prebuilt binaries critical)

#### Documentation
- [ ] Migration guide from v1 to v2
- [ ] HTTP/2-specific patterns and best practices
- [ ] TLS configuration hardening guide

---

## v2.x Future Considerations

**Not committed, under consideration**:

### HTTP/3 and QUIC
- **Status**: Research phase
- **Blocker**: QUIC implementations still stabilizing
- **Candidate**: `ngtcp2` + `nghttp3`

### WebSocket Support
- **Status**: Planned for v2.x (post v2.0)
- **Implementation**: Either native or via upgrade to separate module
- **Platform**: Can leverage existing io_uring/kqueue for frame handling

### Clustering
- **Status**: Under consideration
- **Approach**: SO_REUSEPORT (Linux) with per-process io_uring instances
- **Alternative**: Master-worker with IPC (like Node.js cluster module)

### Middleware Ecosystem
- **Status**: Community-driven
- **Goal**: Compatibility with popular Express middleware:
  - `cors`
  - `helmet`
  - `compression` (native gzip/brotli)
  - `morgan` (logging)

---

## How to Contribute to Roadmap

### Picking Up v2 Items

1. **Check Issues**: Look for `v2`, `help wanted`, or `good first issue` labels
2. **Comment**: Express interest on the issue to avoid duplicate work
3. **Design Doc**: For large features, open a discussion first
4. **Implementation**: Follow [CONTRIBUTING.md](./CONTRIBUTING.md)

### Proposing New Features

Open a discussion with:
- Problem statement
- Proposed solution
- API design (if applicable)
- Platform considerations
- Willingness to implement

### Timeline Adjustments

Roadmap dates are estimates. Priorities may shift based on:
- Community demand
- Security considerations
- Platform API availability (e.g., io_uring features in new kernels)

---

## Version Support Policy

| Version | Status | End of Support |
|---------|--------|----------------|
| v1.x | Maintenance | 6 months after v2.0 release |
| v2.0 | Active development | Current |
| v2.x | Future | TBD |

---

## References

- [GitHub Projects](https://github.com/senapati484/express-pro/projects) — Kanban boards for v2 development
- [Milestones](https://github.com/senapati484/express-pro/milestones) — Release planning
- [Discussions](https://github.com/senapati484/express-pro/discussions) — RFCs and design discussions

---

**Last Updated**: 2026-04-03  
**Next Review**: Monthly
