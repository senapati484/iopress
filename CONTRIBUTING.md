# Contributing to expressmax

Thank you for your interest in contributing! This guide covers everything you need to know to contribute effectively.

## Table of Contents

- [Development Setup](#development-setup)
- [Project Structure](#project-structure)
- [Adding a New Platform Backend](#adding-a-new-platform-backend)
- [Coding Standards](#coding-standards)
- [Testing](#testing)
- [Pull Request Process](#pull-request-process)
- [Release Process](#release-process)

## Development Setup

### Prerequisites

#### Linux (Ubuntu/Debian)

```bash
# Required for io_uring backend
sudo apt-get update
sudo apt-get install -y build-essential liburing-dev

# Optional: clang-format for linting
sudo apt-get install -y clang-format

# Node.js 18+ and npm 9+
nvm install 18
nvm use 18
```

#### macOS

```bash
# Xcode Command Line Tools
xcode-select --install

# Optional: clang-format via Homebrew
brew install clang-format

# Node.js 18+ and npm 9+
nvm install 18
nvm use 18
```

#### Windows

```powershell
# Visual Studio Build Tools or Visual Studio Community
# Download from: https://visualstudio.microsoft.com/downloads/
# Required: "Desktop development with C++" workload

# Optional: LLVM for clang-format
choco install llvm

# Node.js 18+ and npm 9+
# Download from nodejs.org or use nvm-windows
```

### Repository Setup

```bash
# Fork and clone
git clone https://github.com/YOUR_USERNAME/expressmax.git
cd expressmax

# Install dependencies
npm ci

# Build native addon
npm run build

# Run tests
npm test

# Run linter
npm run lint
```

## Project Structure

```
expressmax/
├── src/                    # C source files
│   ├── binding.c          # Node-API entry point
│   ├── server.h           # Platform interface (implement this!)
│   ├── server_linux.c     # io_uring backend
│   ├── server_kevent.c    # kqueue backend
│   ├── server_iocp.c      # IOCP backend
│   ├── server_libuv.c     # Fallback backend
│   ├── router.c           # HTTP routing logic
│   └── parser.c           # HTTP parser
├── js/                     # JavaScript implementation
│   ├── index.js           # Main entry point
│   ├── application.js     # App logic
│   ├── router.js          # JS-side routing
│   ├── request.js         # Request object
│   ├── response.js        # Response object
│   ├── middleware.js      # Middleware chain
│   ├── stream.js          # Streaming support
│   └── deprecation.js     # Deprecation utilities
├── test/                   # Test suite
├── benchmark/              # Performance benchmarks
├── binding.gyp            # node-gyp configuration
└── index.d.ts             # TypeScript definitions
```

## Adding a New Platform Backend

To add support for a new platform (e.g., Solaris event ports, FreeBSD kqueue variants):

### 1. Create Platform-Specific Source File

Create `src/server_newplatform.c` implementing the interface from `server.h`:

```c
// src/server_newplatform.c
#include "server.h"
#include <sys/event.h>  // or your platform's headers

int platform_init(server_t *server) {
    // Initialize your platform's async I/O mechanism
    // Return 0 on success, -1 on error
}

int platform_listen(server_t *server, int port) {
    // Create listening socket and bind to port
    // Set up your platform's event notification
    // Return socket fd or -1 on error
}

int platform_accept(server_t *server, client_t *client) {
    // Accept new connections using your platform's mechanism
    // Populate client->fd
    // Return 0 on success, -1 on error
}

ssize_t platform_recv(server_t *server, client_t *client, void *buf, size_t len) {
    // Receive data using platform-specific async I/O
    // Return bytes read or -1 on error
}

ssize_t platform_send(server_t *server, client_t *client, const void *buf, size_t len) {
    // Send data using platform-specific async I/O
    // Return bytes sent or -1 on error
}

int platform_close_client(server_t *server, client_t *client) {
    // Close client connection
    // Return 0 on success
}

void platform_cleanup(server_t *server) {
    // Clean up platform-specific resources
}

const char *platform_name(void) {
    return "newplatform";  // Used in logs/version info
}
```

### 2. Update binding.gyp

Add your platform to the conditions in `binding.gyp`:

```json
{
  "targets": [{
    "target_name": "express_pro_native",
    "sources": [ /* common sources */ ],
    "conditions": [
      ["OS=='linux'", { /* linux config */ }],
      ["OS=='mac'", { /* macOS config */ }],
      ["OS=='win'", { /* Windows config */ }],
      ["OS=='newplatform'", {
        "sources": ["src/server_newplatform.c"],
        "defines": ["USE_NEWPLATFORM"],
        "libraries": ["-lnewplatform"]  // if needed
      }]
    ]
  }]
}
```

### 3. Update Platform Detection

In `src/binding.c`, add detection for your platform:

```c
#if defined(USE_IO_URING)
    #include "server_linux.c"
#elif defined(USE_KQUEUE)
    #include "server_kevent.c"
#elif defined(USE_IOCP)
    #include "server_iocp.c"
#elif defined(USE_NEWPLATFORM)
    #include "server_newplatform.c"
#else
    #include "server_libuv.c"
#endif
```

### 4. Add Platform Detection to Build

Update the platform detection logic in `js/index.js` or wherever platform detection happens:

```javascript
const getPlatform = () => {
  const platform = os.platform();
  switch (platform) {
    case 'linux': return 'linux';
    case 'darwin': return 'mac';
    case 'win32': return 'win';
    case 'sunos': return 'newplatform';  // Example: Solaris
    default: return 'libuv';
  }
};
```

### 5. Testing

1. **Unit tests**: Add tests in `test/` covering your platform
2. **Integration tests**: Ensure `npm test` passes
3. **Benchmarks**: Run `npm run bench` and verify performance meets thresholds

### Platform Backend Checklist

- [ ] Implements all functions in `server.h`
- [ ] Handles connection lifecycle (accept → recv → send → close)
- [ ] Properly manages memory (no leaks in Valgrind)
- [ ] Supports graceful shutdown
- [ ] Thread-safe (if using worker threads)
- [ ] Returns correct platform name from `platform_name()`

## Coding Standards

### C Code Standards

#### Hot Path Rules (Critical for Performance)

Code in the request/response path must follow these rules:

**❌ No Dynamic Memory Allocation in Hot Path**

```c
// WRONG: malloc in request handler
void handle_request(client_t *c) {
    buffer_t *buf = malloc(sizeof(buffer_t));  // ❌ NEVER
    // ...
}

// RIGHT: Use pre-allocated buffers
void handle_request(client_t *c) {
    buffer_t *buf = &c->server->buffer_pool[c->id];  // ✅ Pre-allocated
    // ...
}
```

**❌ No Locks in Hot Path**

```c
// WRONG: mutex in request path
void process_request(request_t *req) {
    pthread_mutex_lock(&stats_lock);  // ❌ NEVER
    stats.requests++;
    pthread_mutex_unlock(&stats_lock);
}

// RIGHT: Lock-free statistics (atomic operations)
#include <stdatomic.h>
_Atomic size_t request_count = 0;
void process_request(request_t *req) {
    atomic_fetch_add(&request_count, 1);  // ✅ Atomic, no lock
}
```

**✅ Use Branch Prediction Hints**

```c
#include "server.h"  // Contains likely/unlikely macros

// Use likely() for branches expected to be true
if (likely(req->method == METHOD_GET)) {
    // Fast path: most requests are GET
}

// Use unlikely() for error/edge cases
if (unlikely(req->body_len > MAX_BODY_SIZE)) {
    // Slow path: body too large (rare)
    return error_413();
}
```

**Macros (from `server.h`):**

```c
#if defined(__GNUC__) || defined(__clang__)
    #define likely(x)   __builtin_expect(!!(x), 1)
    #define unlikely(x) __builtin_expect(!!(x), 0)
#else
    #define likely(x)   (x)
    #define unlikely(x) (x)
#endif
```

#### General C Guidelines

- **Indentation**: 4 spaces (no tabs)
- **Line length**: 100 characters max
- **Braces**: K&R style (same line)
- **Variable naming**: `snake_case` for locals, descriptive names
- **Constants**: `UPPER_CASE` for macros, `kCamelCase` for enum values

```c
// Good example
static int process_http_request(client_t *client) {
    const size_t max_headers = MAX_HEADERS;
    
    if (unlikely(client == NULL)) {
        return ERROR_INVALID_CLIENT;
    }
    
    for (size_t i = 0; i < max_headers; i++) {
        // ...
    }
    
    return SUCCESS;
}
```

### JavaScript Code Standards

- **Linting**: ESLint with `@eslint/js` recommended rules
- **Formatting**: Run `npm run lint:fix` before committing
- **JSDoc**: All public APIs must have JSDoc comments
- **Strict mode**: All files start with `'use strict';`

```javascript
/**
 * Creates a new Application instance
 * @param {Object} [options] - Configuration options
 * @param {number} [options.maxBodySize=1048576] - Maximum body size in bytes
 * @returns {Application} Express-pro application instance
 */
function createApplication(options = {}) {
    'use strict';
    // ...
}
```

## Testing

### Test Requirements

Every new feature must include:

1. **Unit tests** — Test individual functions in isolation
2. **Integration tests** — Test full request/response cycles
3. **Benchmark regression tests** — Ensure no performance degradation

### Running Tests

```bash
# Full test suite
npm test

# Specific test file
node --test test/router.test.c

# Memory leak detection
npm run test:memory

# With deprecation warnings
npm run test:strict  # Throws on any deprecation warning

# Benchmark regression
npm run bench:ci
```

### Writing Tests

#### JavaScript Tests (Node Test Runner)

```javascript
// test/my-feature.test.js
const { describe, it } = require('node:test');
const assert = require('node:assert');

describe('My Feature', () => {
    it('should do something', () => {
        const result = myFeature.doSomething();
        assert.strictEqual(result, 'expected');
    });
    
    it('should handle edge cases', () => {
        assert.throws(() => {
            myFeature.doSomething(null);
        }, /TypeError/);
    });
});
```

#### C Tests

```c
// test/my-feature.test.c
#include "../src/server.h"
#include <assert.h>
#include <stdio.h>

void test_my_feature() {
    server_t server = {0};
    int result = my_feature(&server);
    assert(result == 0);
    printf("✓ my_feature test passed\n");
}

int main() {
    test_my_feature();
    printf("All tests passed!\n");
    return 0;
}
```

### Test Coverage Requirements

| Component | Minimum Coverage |
|-----------|------------------|
| JavaScript logic | 80% |
| C hot paths | 90% |
| Error handling | 70% |
| Platform backends | Smoke tests on CI |

## Pull Request Process

### Before Submitting

Run the full check suite:

```bash
# 1. Build passes
npm run build

# 2. All tests pass
npm test

# 3. Linter passes (no errors)
npm run lint

# 4. No memory leaks
npm run test:memory

# 5. Benchmark doesn't regress (>90% of baseline)
npm run bench:ci

# 6. Security audit clean
npm audit --audit-level=high
```

### PR Checklist

- [ ] **Description**: Clear explanation of what and why
- [ ] **Tests**: New tests for new features, all tests pass
- [ ] **Documentation**: JSDoc updated, README updated if needed
- [ ] **CHANGELOG**: Entry added under `[Unreleased]`
- [ ] **Linting**: `npm run lint` passes
- [ ] **Benchmarks**: No regression (>90% of baseline)
- [ ] **Memory**: Valgrind shows no leaks
- [ ] **Platform**: Tested on target platform(s)

### Commit Message Convention

We use [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<scope>): <description>

[optional body]

[optional footer(s)]
```

**Types:**
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation only
- `style`: Code style (formatting, no logic change)
- `refactor`: Code refactoring
- `perf`: Performance improvement
- `test`: Adding tests
- `chore`: Build process, dependencies

**Examples:**

```bash
feat(router): add wildcard route matching

fix(parser): reject malformed Content-Length headers

perf(linux): batch io_uring submissions

docs(api): clarify middleware execution order
```

### Review Process

1. **Automated checks** must pass (CI)
2. **Code review** by at least one maintainer
3. **Approval** required before merge
4. **Squash merge** to keep history clean

## Release Process

Releases are automated via GitHub Actions:

1. Merge to `main` triggers CI
2. If tests pass, `standard-version` bumps version
3. GitHub Release created with binaries
4. Published to npm

**Manual release (maintainers only):**

```bash
# Dry run
npm run release:dry-run

# Actual release
npm run release
# Follow prompts, then:
git push --follow-tags origin main
```

## Questions?

- **Discord**: [Join our community](https://discord.gg/expressmax)
- **Issues**: [GitHub Issues](../../issues)
- **Security**: See [SECURITY.md](./SECURITY.md)

## Code of Conduct

This project follows the [Contributor Covenant Code of Conduct](https://www.contributor-covenant.org/version/2/1/code_of_conduct/).

---

**Thank you for contributing to expressmax!** 🚀
