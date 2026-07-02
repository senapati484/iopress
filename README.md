[![Iopress Logo](/docs/img/logo.png)](https://github.com/senapati484/iopress)

**High-performance, unopinionated, minimalist native HTTP engine for [Node.js](https://nodejs.org).**

**This project has a [Code of Conduct](https://github.com/senapati484/iopress/blob/main/CODE_OF_CONDUCT.md).**

## Table of contents

- [Table of contents](#table-of-contents)
- [Installation](#installation)
- [Features](#features)
- [Docs & Community](#docs--community)
- [Quick Start](#quick-start)
- [Philosophy](#philosophy)
- [Performance (Why iopress?)](#performance-why-iopress)
- [Examples](#examples)
- [Contributing](#contributing)
  - [Security Issues](#security-issues)
  - [Running Tests](#running-tests)
- [License](#license)
- [Changelog](CHANGELOG.md)

[![NPM Version](https://img.shields.io/npm/v/@iopress/core)](https://npmjs.org/package/@iopress/core)
[![NPM Downloads](https://img.shields.io/npm/dm/@iopress/core)](https://npmcharts.com/compare/@iopress/core?minimal=true)
[![CI Build](https://github.com/senapati484/iopress/actions/workflows/ci.yml/badge.svg)](https://github.com/senapati484/iopress/actions)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

```js
const iopress = require('@iopress/core')
const app = iopress()

app.get('/', (req, res) => {
  res.send('Hello World')
})

app.listen(3000, () => {
  console.log('Server is running on http://localhost:3000')
})
```

## Installation

This is a [Node.js](https://nodejs.org/en/) module available through the
[npm registry](https://www.npmjs.com/).

Before installing, [download and install Node.js](https://nodejs.org/en/download/).
Node.js 18 or higher is required.

Installation is done using the
[`npm install` command](https://docs.npmjs.com/downloading-and-installing-packages-locally):

```bash
npm install @iopress/core
```

*Note: On Linux, `liburing-dev` is recommended for optimal performance via io_uring.*

## Features

  * **Native Performance**: Built on platform-specific async I/O (`io_uring`, `kqueue`, `IOCP`).
  * **Fast Path Routing**: Static routes are handled entirely in C without crossing into JavaScript.
  * **Express Compatible**: Familiar API for seamless migration.
  * **Zero Dependency**: Minimal runtime footprint with focused native core.
  * **Built-in Body Parsing**: High-performance JSON and raw body parsing included.
  * **Scalability**: Designed to handle 500k+ requests per second on Linux via `io_uring` (projected — see [Performance](#performance-why-iopress)).

## Docs & Community

  * [Official Documentation](./docs/README.md) - Full Diátaxis-based guides and reference.
  * [API Reference](./docs/reference/api/index.md) - Detailed documentation of methods and types.
  * [Getting Started Guide](./docs/tutorials/getting-started.md) - Your first server in 5 minutes.
  * [Migration Guide](./docs/how-to/migration-guide.md) - Porting from Express.js to iopress.

## Quick Start

The quickest way to get started is to create a simple script:

1. Create a directory: `mkdir my-app && cd my-app`
2. Initialize: `npm init -y`
3. Install: `npm install @iopress/core`
4. Create `app.js` and paste the snippet above.
5. Run: `node app.js`

## Philosophy

  The iopress philosophy is to deliver **native performance** to the Node.js ecosystem without sacrificing the **minimalist developer experience** pioneered by Express.

  While Express relies on the generic `libuv` event loop, iopress utilizes the latest kernel-level asynchronous interfaces directly. This allows iopress to bypass traditional bottlenecks, making it the ideal engine for high-throughput APIs, proxy layers, and performance-critical services.

## Performance (Why iopress?)

iopress is designed to outperform traditional Node.js frameworks by leveraging direct kernel communication and a "fast-path" routing system.

> **Tested** — macOS (Apple M2, `kqueue`) numbers below come from real benchmark runs.
> **Untested** — Linux (`io_uring`) and Windows (`IOCP`) numbers are estimated from the known characteristics of each backend relative to `kqueue`, and will be swapped for real measurements once re-run on that hardware. Expect Linux to outperform macOS, and Windows to trail behind it.

### 🏆 Summary

| Framework   | Platform                 | Throughput (req/s)         | Latency (p99)            |
| ----------- | ------------------------- | ---------------------------- | --------------------------- |
| **iopress** | **Linux (io_uring)**       | **500,000+** *(untested)*    | **<15ms** *(untested)*      |
| **iopress** | **macOS (kqueue)**         | **211,854** *(tested)*       | **28ms** *(tested)*         |
| **iopress** | **Windows (IOCP)**         | **100,000+** *(untested)*    | **<45ms** *(untested)*      |
| Express.js  | Any *(tested on macOS)*    | 17,280 *(tested)*            | 1,531ms *(tested)*          |

*Benchmarked on Apple M2 (macOS 14, Node.js 18+). Linux figures were previously benchmarked on Ubuntu 22.04 during early development; Windows figures are estimated. Fresh Linux/Windows numbers will be published once re-run on current hardware.*

### 📈 Detailed benchmark results

<details>
<summary><b>⚡ Fast path — static routes (click to expand)</b></summary>

<br>

Static routes (`hello-world`, `health-check`), handled entirely in the native fast path — head-to-head vs. Express.js on identical hardware:

| Scenario            | @iopress/core   | Express.js   |
| -------------------- | ---------------- | ------------- |
| `hello-world`         | **211,854 req/s** | —             |
| `health-check`         | **209,164 req/s** | —             |
| Head-to-head (avg)     | **211,854 req/s** | 17,280 req/s  |
| Mean latency            | **23.36 ms**      | 107.33 ms     |
| p99 latency             | **28 ms**         | 1,531 ms      |

> 🚀 **~12.3x** more throughput than Express.js, with **~55x** lower p99 latency.

</details>

<details>
<summary><b>🐢 Slow path — JS-handled routes (click to expand)</b></summary>

<br>

Routes requiring JS-side handling (param parsing, JSON body parsing, query parsing). Test config: 500 connections, pipelining 10, 5s × 3 runs.

| Scenario         | Throughput (req/s) | p99 latency |
| ------------------ | --------------------- | -------------- |
| `route-params`       | 159,639                | 38 ms          |
| `json-body`           | 122,946                | 43 ms          |
| `query-params`        | 163,643                | 39 ms          |
| **Average**            | **148,743**             | —              |

> ⚠️ `json-body` is the current bottleneck on the slow path due to JSON parsing overhead — this is a known area for future optimization.

> ✅ All benchmarks pass the project's minimum throughput threshold of **150,000 req/s** on the fast path.

</details>

## Examples

  To view the examples, clone the repository:

```bash
git clone https://github.com/senapati484/iopress.git --depth 1 && cd iopress
```

  Then install the dependencies and run an example:

```bash
npm install
node examples/basic.js
```

## Contributing

The iopress project welcomes all constructive contributions. See the [Contributing Guide](CONTRIBUTING.md) for more technical details.

### Security Issues

If you discover a security vulnerability, please see our [Security Policy](SECURITY.md).

### Running Tests

```bash
npm install
npm test
```

## Troubleshooting

Since iopress uses native bindings for extreme performance, it interacts directly with kernel-level I/O. If you encounter issues after code changes, a hard restart of the Node process is recommended:

```bash
pkill -f node
```

## License

Licensed under the **[Apache License 2.0](LICENSE)**.

"Iopress" and the Iopress logo are trademarks of senapati484. Forks and derivative projects are welcome under the license terms above, but should not use the Iopress name or logo in a way that implies official endorsement.

---

<div align="center">

Made with ❤️ by [**senapati484**](https://github.com/senapati484)

If iopress helped you build something fast, consider giving it a ⭐ on GitHub — it genuinely helps!

</div>
