# expressmax Compatibility Matrix

> **Version:** 1.0.0
> **Target:** Express.js 4.x API compatibility with performance-first design

This document defines the Express.js API compatibility surface for ExpressPro v1. Use it to assess migration feasibility from existing Express applications.

---

## Quick Reference

| Migration Readiness | Feature Count |
|---------------------|---------------|
| ✅ **Drop-in** | Core routing, middleware, JSON responses |
| ⚠️ **Minor Changes** | Body parsing, error handling patterns |
| ❌ **Not Supported** | View engines, static files, session middleware |
| 🐧 **Linux Only** | io_uring performance tier |

---

## Core Application API

| Feature | Express 4 | ExpressPro v1 | Notes |
|---------|-----------|-----------------|-------|
| `express()` / `expressmax()` | ✅ | ✅ | Factory function equivalent |
| `app.use()` | ✅ | ✅ | Middleware mounting with path support |
| `app.get()` | ✅ | ✅ | Route registration |
| `app.post()` | ✅ | ✅ | Route registration |
| `app.put()` | ✅ | ✅ | Route registration |
| `app.delete()` | ✅ | ✅ | Route registration |
| `app.patch()` | ✅ | ✅ | Route registration |
| `app.all()` | ✅ | ✅ | All HTTP methods |
| `app.route()` | ✅ | ❌ | Use individual method calls instead |
| `app.param()` | ✅ | ❌ | Route param callbacks not supported |
| `app.locals` | ✅ | ⚠️ | Partial - basic object only |
| `app.mountpath` | ✅ | ❌ | Sub-app mounting not supported |
| **Router** | ✅ | ❌ | v2 - Use `app.use()` with path prefix |

---

## Request Object (`req`)

| Property/Method | Express 4 | ExpressPro v1 | Notes |
|-----------------|-----------|-----------------|-------|
| `req.app` | ✅ | ✅ | Reference to ExpressPro instance |
| `req.baseUrl` | ✅ | ❌ | Router not supported |
| `req.body` | ✅ | ✅ | **Behavioral Difference:** See [Body Parsing](#body-parsing) |
| `req.cookies` | ✅ | ❌ | Use external `cookie-parser` (not bundled) |
| `req.fresh` | ✅ | ❌ | Cache checking not implemented |
| `req.hostname` | ✅ | ✅ | From Host header |
| `req.ip` | ✅ | ✅ | Client IP address |
| `req.ips` | ✅ | ❌ | Proxy chain not tracked |
| `req.method` | ✅ | ✅ | HTTP method string |
| `req.originalUrl` | ✅ | ✅ | Original request URL |
| `req.params` | ✅ | ✅ | Route parameters (e.g., `:id`) |
| `req.path` | ✅ | ✅ | URL path only |
| `req.protocol` | ✅ | ✅ | `http` or `https` |
| `req.query` | ✅ | ✅ | Parsed query string |
| `req.route` | ✅ | ❌ | Current route object not exposed |
| `req.secure` | ✅ | ✅ | `protocol === 'https'` |
| `req.signedCookies` | ✅ | ❌ | Cookie signing not supported |
| `req.stale` | ✅ | ❌ | Cache checking not implemented |
| `req.subdomains` | ✅ | ❌ | Subdomain parsing not implemented |
| `req.xhr` | ✅ | ❌ | X-Requested-With header check not implemented |
| `req.accepts()` | ✅ | ❌ | Content negotiation not implemented |
| `req.acceptsCharsets()` | ✅ | ❌ | Not implemented |
| `req.acceptsEncodings()` | ✅ | ❌ | Not implemented |
| `req.acceptsLanguages()` | ✅ | ❌ | Not implemented |
| `req.get()` | ✅ | ✅ | Header retrieval |
| `req.is()` | ✅ | ❌ | Content type checking not implemented |
| `req.param()` | ⚠️ | ❌ | Deprecated in Express 4, not supported |
| `req.range()` | ✅ | ❌ | Range header parsing not implemented |

---

## Response Object (`res`)

| Property/Method | Express 4 | ExpressPro v1 | Notes |
|-----------------|-----------|-----------------|-------|
| `res.app` | ✅ | ✅ | Reference to ExpressPro instance |
| `res.headersSent` | ✅ | ✅ | Boolean, headers sent status |
| `res.locals` | ✅ | ⚠️ | Partial - basic object only |
| `res.append()` | ✅ | ✅ | Append header values |
| `res.attachment()` | ✅ | ❌ | Content-Disposition not implemented |
| `res.cookie()` | ✅ | ❌ | Cookie setting not implemented |
| `res.clearCookie()` | ✅ | ❌ | Cookie clearing not implemented |
| `res.download()` | ✅ | ❌ | File download helper not implemented |
| `res.end()` | ✅ | ✅ | End response |
| `res.format()` | ✅ | ❌ | Content negotiation not implemented |
| `res.get()` | ✅ | ✅ | Get header value |
| `res.json()` | ✅ | ✅ | Send JSON response |
| `res.jsonp()` | ✅ | ❌ | JSONP not supported (security) |
| `res.links()` | ✅ | ❌ | Link header helper not implemented |
| `res.location()` | ✅ | ✅ | Location header |
| `res.redirect()` | ✅ | ✅ | Redirect response |
| `res.render()` | ✅ | ❌ | **Explicitly Not Supported** - No template engines |
| `res.send()` | ✅ | ✅ | Send response body |
| `res.sendFile()` | ✅ | ❌ | File serving not implemented |
| `res.sendStatus()` | ✅ | ⚠️ | Use `res.status().send()` instead |
| `res.set()` | ✅ | ✅ | Set headers |
| `res.status()` | ✅ | ✅ | Set status code |
| `res.type()` | ✅ | ⚠️ | Content-Type shorthand (limited) |
| `res.vary()` | ✅ | ❌ | Vary header helper not implemented |

---

## Middleware

| Middleware | Express 4 | ExpressPro v1 | Notes |
|------------|-----------|-----------------|-------|
| `express.json()` | ✅ | ✅ | Built-in, see [Body Parsing](#body-parsing) |
| `express.raw()` | ✅ | ❌ | Not implemented |
| `express.text()` | ✅ | ❌ | Not implemented |
| `express.urlencoded()` | ✅ | ✅ | Built-in, see [Body Parsing](#body-parsing) |
| `express.static()` | ✅ | ❌ | **Explicitly Not Supported** - Use nginx/CDN |
| `express.Router()` | ✅ | ❌ | v2 - Use `app.use()` with path prefix |
| **Custom Middleware** | ✅ | ✅ | `(req, res, next) => {}` pattern |
| **Error Middleware** | ✅ | ⚠️ | **See [Error Handling](#error-handling)** |

---

## Body Parsing

**ExpressPro has different defaults optimized for performance:**

| Aspect | Express 4 | ExpressPro v1 | Migration Note |
|--------|-----------|---------------|----------------|
| Default limit | 100KB | **4KB** | Increase via `maxBodySize` option |
| JSON parsing | `express.json()` | Built-in | No separate middleware needed |
| URL-encoded | `express.urlencoded()` | Built-in | No separate middleware needed |
| Raw body | `express.raw()` | ❌ | Use `streamBody: true` option |
| Text body | `express.text()` | ❌ | Buffer and convert manually |
| Streaming | ❌ | ✅ | Native support via `streamBody: true` |

### Configuration

```javascript
// Express 4
app.use(express.json({ limit: '10mb' }));
app.use(express.urlencoded({ extended: true }));

// ExpressPro v1
const app = expressmax({
  maxBodySize: 10 * 1024 * 1024,  // 10MB
  streamBody: false               // Buffer entire body (default)
});

// For streaming large bodies
const app = expressmax({
  maxBodySize: 0,      // Unlimited (use with care)
  streamBody: true     // Stream data chunks
});
```

---

## Error Handling

| Pattern | Express 4 | ExpressPro v1 | Notes |
|---------|-----------|---------------|-------|
| `next(err)` | ✅ | ✅ | Propagate error to handlers |
| `next('route')` | ✅ | ❌ | Skip to next route not supported |
| `next('router')` | ✅ | ❌ | Router not supported |
| Synchronous throw | ✅ | ✅ | Caught and passed to error handlers |
| Async throw | ✅ | ✅ | Promise rejections caught |
| `app.use((err, req, res, next) => {})` | ✅ | ⚠️ | **v2 - See below** |

### Error Middleware in v1

ExpressPro v1 supports error handling via `app.onError()`:

```javascript
// Express 4 - 4-argument error middleware
app.use((err, req, res, next) => {
  res.status(500).json({ error: err.message });
});

// ExpressPro v1 - onError method
app.onError((err, req, res) => {
  res.status(500).json({ error: err.message });
});
```

**Important:** The standard Express 4-argument error middleware pattern is **deferred to v2**. Use `app.onError()` in v1.

---

## Explicitly Not Supported

The following Express.js features are **explicitly out of scope** for ExpressPro v1:

| Feature | Reason | Alternative |
|---------|--------|-------------|
| `express.static()` | File I/O blocks event loop | Use nginx, CDN, or dedicated file server |
| `app.engine()` / `res.render()` | Template rendering is CPU-intensive | Pre-render or use dedicated service |
| `req.cookies` / `res.cookie()` | Requires cookie parsing overhead | Use `cookie-parser` package if needed |
| `express-session` | Stateful, requires storage backend | Use JWT tokens or external session store |
| `express.Router()` | Adds complexity, minimal perf benefit | Use `app.use('/prefix', handler)` |
| `app.param()` | Adds indirection | Parse params directly in handlers |

---

## Platform-Specific Features

### Linux io_uring Performance Tier 🐧

| Feature | Linux (io_uring) | macOS (kqueue) | Windows (IOCP) |
|---------|------------------|----------------|----------------|
| Async file I/O | ✅ io_uring | ⚠️ libuv fallback | ⚠️ libuv fallback |
| Zero-copy send | ✅ io_uring | ❌ | ❌ |
| SQPOLL mode | ✅ Optional | N/A | N/A |
| Peak throughput | ~2M req/s | ~800K req/s | ~600K req/s |
| Latency p99 | ~10μs | ~50μs | ~100μs |

**Note:** Benchmarks on AMD EPYC 7763, 64 cores, simple "Hello World" JSON response.

### Feature Availability by Platform

| Feature | Linux | macOS | Windows |
|---------|-------|-------|---------|
| HTTP/1.1 keep-alive | ✅ | ✅ | ✅ |
| Pipelining | ✅ | ✅ | ✅ |
| Streaming responses | ✅ | ✅ | ✅ |
| Connection draining | ✅ | ✅ | ✅ |
| Cluster mode | ⚠️ | ⚠️ | ⚠️ | Use Node.js cluster module |

---

## Behavioral Differences

### 1. Response Chunking

Express 4 automatically handles chunked encoding. ExpressPro v1 requires explicit streaming mode:

```javascript
// Express 4 - automatic chunking
res.write('chunk 1');
res.write('chunk 2');
res.end();

// ExpressPro v1 - enable streaming
app.get('/stream', (req, res) => {
  // Must set up streaming before first write
  res.streamData('chunk 1');
  res.streamData('chunk 2', true); // isLast = true
});
```

### 2. Header Case Sensitivity

Express 4 lowercases header names internally. ExpressPro v1 preserves original casing:

```javascript
// Both are equivalent in ExpressPro
res.set('Content-Type', 'application/json');
res.set('content-type', 'application/json');

// req.headers preserves original casing (implementation dependent)
```

### 3. Query String Parsing

Express 4 uses `qs` library by default. ExpressPro v1 uses native `URLSearchParams`:

| Input | Express 4 | ExpressPro v1 |
|-------|-----------|---------------|
| `?foo[bar]=baz` | `{ foo: { bar: 'baz' } }` | `{ 'foo[bar]': 'baz' }` |
| `?foo=1&foo=2` | `{ foo: ['1', '2'] }` | `{ foo: ['1', '2'] }` |
| `?foo=` | `{ foo: '' }` | `{ foo: '' }` |

**Migration:** Parse complex query strings manually if nested objects are required.

### 4. Route Matching Order

Express 4: Routes added first are checked first (insertion order).
ExpressPro v1: Same behavior, but path matching is faster (radix tree vs RegExp).

### 5. Path Normalization

Express 4 normalizes paths (`/foo//bar` → `/foo/bar`).
ExpressPro v1: Same normalization, but trailing slash handling differs:

| Request | Express 4 | ExpressPro v1 |
|---------|-----------|---------------|
| `/users/` vs `/users` | Separate by default | Same route (configurable in v2) |

---

## Migration Checklist

Before migrating an Express 4 application:

- [ ] Replace `express.static()` with nginx/CDN
- [ ] Replace `res.render()` with API-only responses
- [ ] Check `maxBodySize` (default 4KB vs 100KB)
- [ ] Convert `app.use((err, req, res, next) => {})` to `app.onError()`
- [ ] Replace `req.cookies` with external cookie parser
- [ ] Remove `express.Router()`, use `app.use('/prefix', ...)`
- [ ] Review query string parsing for nested objects
- [ ] Test on target platform (Linux recommended for production)

---

## Version Roadmap

| Feature | v1 (Current) | v2 (Planned) | v3 (Future) |
|---------|--------------|--------------|-------------|
| Core routing | ✅ | ✅ | ✅ |
| Router API | ❌ | ✅ | ✅ |
| Error middleware | `onError()` | 4-arg pattern | ✅ |
| WebSocket upgrade | ❌ | ✅ | ✅ |
| HTTP/2 | ❌ | ✅ | ✅ |
| Cluster mode | Manual | Built-in | Built-in |
| Compression | ❌ | Middleware | Native |
| Caching | ❌ | ❌ | Built-in |

---

## Support

For migration assistance:
- Review [BINDING_CONTRACT.md](./BINDING_CONTRACT.md) for native API details
- Open an issue with `[Migration]` prefix for compatibility questions
- Check [examples/](./examples/) for common patterns

---

*Last updated: 2026-04-01*
