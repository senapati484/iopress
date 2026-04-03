# norvex Documentation

Welcome to the norvex documentation! Choose your path:

## Quick Start

- [**Getting Started**](guides/getting-started.md) - Your first norvex server in 5 minutes
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
| [Fast Router](../src/fast_router.c) | C-level routing for maximum speed |

## Development

| Topic | Description |
|-------|-------------|
| [Roadmap](../ROADMAP.md) | Future features and v2 plans |
| [Changelog](../CHANGELOG.md) | Version history |
| [Development Roadmap](development-roadmap.md) | Implementation priorities |

## Key Features

- ⚡ **High Performance**: 300k+ req/s on Linux with io_uring
- 🚀 **Native Speed**: Written in C with platform-specific optimizations
- 📦 **Express Compatible**: Drop-in replacement for Express.js
- 🔧 **Prebuilt Binaries**: No build tools required
- 💪 **Load Balancer Optional**: Single process handles massive traffic

## Platform Support

| Platform | Status | Performance |
|----------|--------|-------------|
| Linux (io_uring) | ✅ Full | 300k-500k req/s |
| macOS (kqueue) | ✅ Full | 80k+ req/s |
| Windows (IOCP) | ✅ Full | 100k+ req/s |

## Quick Links

- [GitHub Repository](https://github.com/senapati484/norvex)
- [npm Package](https://www.npmjs.com/package/norvex)
- [Issues & Bug Reports](https://github.com/senapati484/norvex/issues)
- [Discussions](https://github.com/senapati484/norvex/discussions)

## Community

- 💬 [Discord](https://discord.gg/norvex)
- 🐦 [Twitter](https://twitter.com/express_pro)
- 📧 [Email Support](mailto:support@norvex.dev)

---

**License:** ISC | **Author:** senapati484
