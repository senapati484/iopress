[![Iopress Logo](/docs/img/logo.png)](https://github.com/senapati484/iopress)

**High-performance, unopinionated, minimalist native HTTP engine for [Node.js](https://nodejs.org).**

## Installation

```bash
npm install @iopress/core
```

Node.js 18+ required. On Linux, install `liburing-dev` for io_uring support.

## Quick Start

```js
const iopress = require('@iopress/core')
const app = iopress()

app.get('/', (req, res) => {
  res.send('Hello World')
})

app.listen(3000)
```

## Features

* **Native I/O** — Platform-specific backends (io_uring, kqueue, IOCP) bypass libuv.
* **C fast path** — Static routes handled entirely in C, zero JS overhead.
* **Express-compatible API** — `req.params`, `req.query`, `req.body`, `res.json()`, `res.send()`, middleware chain.
* **Zero-copy body** — Request body handed to V8 as external Buffer, no copy.
* **Built-in JSON parsing** — `req.body` auto-parses `application/json`.
* **No dependencies** — The runtime package has zero JS dependencies.

## Performance

| Framework | Platform | Hello World (RPS) | Slow Path (AVG RPS) |
|-----------|----------|------------------:|--------------------:|
| **iopress** | macOS (kqueue) | **~210,000** | **~154,000** |
| **iopress** | Linux (io_uring) | *pending* | *pending* |
| **iopress** | Windows (IOCP) | *pending* | *pending* |
| Express.js | Any | ~17,000 | ~17,000 |

*Measured on Apple M2 (macOS), 500 connections, pipelining 10, 5s duration.
Thermal throttling can reduce throughput by 20-30% on sustained loads.*

### Per-Scenario Breakdown (Slow Path)

| Scenario | RPS | p99 |
|----------|----:|----:|
| Route params (`/users/42`) | ~187,000 | <40ms |
| JSON body (POST `/echo`) | ~118,000 | <65ms |
| Query params (`/search?q=x`) | ~163,000 | <35ms |

## Benchmarks

```bash
npm run bench:quick   # fast path (hello-world)
npm run bench:full    # all 5 scenarios vs Express
node benchmarks/slow-path.js  # slow-path scenarios
```

## Examples

```bash
git clone https://github.com/senapati484/iopress.git --depth 1
cd iopress && npm install && node examples/basic.js
```

## Platform Backends

| Platform | Backend | Status |
|----------|---------|--------|
| macOS 10.14+ | kqueue | Production-ready |
| Linux 5.1+ | io_uring | Tested |
| Windows | IOCP | Prebuilt binaries pending |
| Any | libuv fallback | Functional |

## Philosophy

iopress delivers native I/O performance to Node.js without sacrificing the minimalist developer experience of Express. Instead of routing through libuv, iopress talks directly to kernel async I/O interfaces — `io_uring` on Linux, `kqueue` on macOS, `IOCP` on Windows.

## API Reference

See the [docs](./docs/README.md) for full API reference, guides, and migration from Express.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) and [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

### Running Tests

```bash
npm test                                        # JS tests
gcc -Isrc test/parser.test.c src/parser.c -o /tmp/ptest && /tmp/ptest  # C tests
```

## License

ISC. Copyright (c) 2024 senapati484. See [LICENSE](LICENSE).
