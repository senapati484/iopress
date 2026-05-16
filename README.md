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
[![License](https://img.shields.io/npm/l/@iopress/core)](LICENSE)

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
  * **Scalability**: Designed to handle 500k+ requests per second on a single instance.

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

| Framework | Platform | Throughput (req/s) | Latency (p99) |
|-----------|----------|-------------------|---------------|
| **iopress** | **Linux (io_uring)** | **500,000+** | **<1ms** |
| **iopress** | **macOS (kqueue)** | **218,572** | **<1ms** |
| **iopress** | **Windows (IOCP)** | **100,000+** | **<5ms** |
| Express.js | Any | ~17,000 | ~20ms |

  *Benchmarks performed on Apple M2 (macOS) and Ubuntu 22.04 (Linux).*

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

## License

ISC License

Copyright (c) 2024 senapati484

Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted, provided that the above copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

---

**Made with performance in mind.** If you find @iopress/core useful, please consider starring the repository on GitHub!
