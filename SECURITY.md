# Security Policy

## Supported Versions

| Version | Status          | Security Fixes |
|---------|-----------------|----------------|
| 1.x     | Current         | ✅ Supported     |
| < 1.0   | Not released    | ❌ Unsupported   |

## Reporting a Vulnerability

**Please do not open public issues for security vulnerabilities.**

### Private Disclosure Channels

| Severity | Response Time | Method                                                                 |
|----------|---------------|------------------------------------------------------------------------|
| Critical | 24 hours      | GitHub Security Advisory: [Report a vulnerability](../../security/advisories/new) |
| High     | 48 hours      | Email: security@expressmax.dev (or GitHub Security tab)                |
| Moderate | 72 hours      | GitHub Security Advisory                                               |
| Low      | 1 week        | GitHub Security Advisory                                               |

### Disclosure Process

1. **Report**: Submit via [GitHub Security Advisory](../../security/advisories/new) or email
2. **Acknowledge**: Core team acknowledges receipt within SLA timeframe
3. **Assess**: We assess severity and validate the vulnerability
4. **Patch**: Develop and test a fix privately
5. **Release**: Publish patched version and security advisory
6. **Credit**: Publicly credit the reporter (with permission)

### Response SLAs

| Phase                | Critical | High     | Moderate | Low      |
|----------------------|----------|----------|----------|----------|
| Initial Acknowledgment | 24 hours | 48 hours | 72 hours | 1 week   |
| Fix Developed          | 7 days   | 14 days  | 30 days  | 90 days  |
| Advisory Published     | With fix | With fix | With fix | With fix |

## Known Attack Surfaces

### HTTP Request Parsing

**Risk**: HTTP request smuggling, desync attacks

**Mitigations**:
- Strict Content-Length/Transfer-Encoding validation
- Reject malformed request lines
- Single newline (`\n`) not accepted as header terminator (requires `\r\n`)
- Body size limits enforced (default: 1MB, configurable)

**Recommendations**:
```javascript
// Set conservative limits
app.listen(3000, {
  maxBodySize: 1024 * 1024,    // 1MB max
  initialBufferSize: 16 * 1024 // 16KB initial
});
```

### Path Traversal in Router

**Risk**: Access to files outside intended routes

**Mitigations**:
- Router normalizes paths (removes `..` segments before matching)
- URL decoding happens after route matching
- Static file serving should validate paths separately

**Recommendations**:
```javascript
// Router automatically handles this, but for static files:
const path = require('path');
const root = '/var/www/static';

app.get('/files/:name', (req, res) => {
  const filePath = path.join(root, req.params.name);
  // Ensure resolved path is under root
  if (!filePath.startsWith(root)) {
    return res.status(403).end();
  }
  // ... serve file
});
```

### Native Code Execution

**Risk**: Memory safety issues in C parser (buffer overflow, use-after-free)

**Mitigations**:
- Bounds checking on all buffer operations
- Static analysis with clang-analyzer
- Valgrind memory testing in CI
- Fuzzing (planned)

**Report Concerns**:
- SIGSEGV/SIGBUS crashes
- Memory corruption indicators
- Unexpected process termination

### Denial of Service

**Risk**: Resource exhaustion via slowloris, large payloads, connection floods

**Mitigations**:
- Body size limiting (configurable via `maxBodySize`)
- Connection timeouts (platform-specific)
- Request queue limits (io_uring/kqueue/IOCP backpressure)

**Recommendations**:
```javascript
// Production deployment
app.listen(3000, {
  maxBodySize: 10 * 1024 * 1024,  // 10MB
  streamBody: true                   // Stream large bodies
});
```

### Dependency Vulnerabilities

We use automated scanning:
- **Dependabot**: Weekly scans for npm dependencies
- **npm audit**: Runs on every CI build (fails on high/critical)
- **GitHub Security Advisories**: Enabled for this repository

## Security Best Practices

### For Users

1. **Keep updated**: Use latest patch version
2. **Configure limits**: Set `maxBodySize` appropriate for your use case
3. **Validate input**: Don't trust `req.params` or `req.query` without validation
4. **Use HTTPS**: Native module handles HTTP; use reverse proxy (nginx, etc.) for TLS
5. **Monitor**: Watch for unusual memory usage or connection patterns

### For Contributors

1. **Memory safety**: All C code must pass Valgrind checks
2. **Input validation**: Never trust HTTP input length/encoding
3. **Bounds checking**: Verify buffer sizes before writes
4. **Tests**: Add security regression tests for fixes

## CVE Assignment

We work with [MITRE](https://cve.mitre.org/) and [GitHub Security Advisories](https://docs.github.com/en/code-security/security-advisories) to assign CVEs for confirmed vulnerabilities.

## Security Contacts

- **Primary**: GitHub Security Advisory (preferred)
- **Emergency**: security@expressmax.dev
- **PGP Key**: [Download public key](./security-pgp-key.asc) (fingerprint: TBD)

## History

See [GitHub Security Advisories](../../security/advisories) for past disclosures.
