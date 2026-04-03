# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Planned: WebSocket support via io_uring/zm
- Planned: HTTP/2 server push
- Planned: Clustering mode for multi-core utilization

## [1.0.0] — 2026-04-02

### Added

- **Core HTTP Server**
  - Native HTTP server with platform-specific async I/O
  - Linux: io_uring backend for maximum performance (500,000+ req/s)
  - macOS: kqueue backend (150,000+ req/s)
  - Windows: IOCP backend (100,000+ req/s)
  - Fallback: libuv for other platforms (40,000+ req/s)

- **Express-Compatible API**
  - Application class with `use()`, `get()`, `post()`, `put()`, `patch()`, `delete()` methods
  - Route parameters with `:param` syntax (`/users/:id`)
  - Query string parsing with URLSearchParams
  - Middleware chain with `next()` support
  - Error handling middleware via `onError()`

- **Request Object**
  - `req.method`, `req.path`, `req.url`
  - `req.params` — route parameters
  - `req.query` — parsed query string
  - `req.headers` — HTTP headers
  - `req.body` — parsed request body (JSON auto-parsed)
  - `req.rawBody` — raw body buffer
  - `req.get()` — case-insensitive header lookup

- **Response Object**
  - `res.status(code)` — set HTTP status
  - `res.json(obj)` — send JSON response
  - `res.send(data)` — send string/Buffer/object
  - `res.set()` — set headers
  - `res.get()` — get header value
  - `res.redirect(url, code)` — HTTP redirect
  - `res.end()` — end response

- **Configuration Options**
  - `initialBufferSize` — initial buffer size (default: 16KB)
  - `maxBodySize` — maximum body size (default: 1MB)
  - `streamBody` — stream large bodies (default: false)

- **TypeScript Support**
  - Complete TypeScript declarations in `index.d.ts`
  - Type definitions for all public APIs

- **Testing & Benchmarking**
  - Autocannon-based benchmark suite
  - Regression testing with platform-specific thresholds
  - Memory leak detection with Valgrind
  - Integration test suite

- **Documentation**
  - README with quickstart, API reference, and platform support
  - Migration guide from Express.js
  - JSDoc-generated API documentation
  - Working examples: basic, REST API, middleware, upload

### Changed

- N/A (initial release)

### Fixed

- N/A (initial release)

### Removed

- N/A (initial release)

### Security

- Body size limiting to prevent memory exhaustion attacks
- Header injection protection via controlled header setting

## Commit Message Convention

This project uses [Conventional Commits](https://www.conventionalcommits.org/).

See [CONTRIBUTING.md](./CONTRIBUTING.md) for details.

---

[unreleased]: https://github.com/senapati484/maxpress/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/senapati484/maxpress/releases/tag/v1.0.0
