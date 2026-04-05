# iopress Documentation

Welcome to the iopress documentation! Choose your path:

## Quick Start

- [**Getting Started**](guides/getting-started.md) - Your first iopress server in 5 minutes
- [**Installation**](deployment/PREBUILT_BINARIES.md) - Installation options and prebuilt binaries

## Core Documentation

### API Reference

- TypeScript definitions in `../index.d.ts`
- JSDoc comments in source code

### Guides

| Guide | Description |
|-------|-------------|
| [Getting Started](guides/getting-started.md) | First server setup |
| [Contributing](../CONTRIBUTING.md) | How to contribute |
| [Security](../SECURITY.md) | Security policies |

## Deployment

| Topic | Description |
|-------|-------------|
| [Prebuilt Binaries](deployment/PREBUILT_BINARIES.md) | Installation without build tools |
| [Load Balancer Replacement](deployment/LOAD_BALANCER_GUIDE.md) | When you don't need a load balancer |

## Performance

| Topic | Description |
|-------|-------------|
| [Performance Analysis](performance/PERFORMANCE.md) | Benchmarks and optimization |
| [Architecture](architecture.md) | System design and flow |

## Development

| Topic | Description |
|-------|-------------|
| [Roadmap](roadmap.md) | Future features and v2 plans |
| [Changelog](changelog.md) | Version history |

## Key Features

- ⚡ **High Performance**: 218k+ req/s on macOS (tested), 500k+ expected on Linux
- 🚀 **Native Speed**: Written in C with platform-specific optimizations
- 📦 **Express Compatible**: Drop-in replacement for Express.js
- 🔧 **Prebuilt Binaries**: No build tools required
- 💪 **Load Balancer Optional**: Single process handles massive traffic

## Platform Support

| Platform | Status | Performance |
|----------|--------|-------------|
| Linux (io_uring) | ⏳ Pending | 500k+ expected |
| macOS (kqueue) | ✅ Tested | 218k+ req/s |
| Windows (IOCP) | ⏳ Pending | 100k+ expected |

## Quick Links

- [GitHub Repository](https://github.com/senapati484/iopress)
- [npm Package](https://www.npmjs.com/package/@iopress/core)
- [Issues & Bug Reports](https://github.com/senapati484/iopress/issues)
- [Discussions](https://github.com/senapati484/iopress/discussions)

---

**License:** ISC | **Author:** senapati484