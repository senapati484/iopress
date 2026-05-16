# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.4] - 2026-05-16

### Added
- Multi-threaded `kqueue` support for macOS backend.
- Performance optimizations for fast-path routing in C.
- Official support for Node.js 22.

### Fixed
- Windows header include order in `server_iocp.c` to resolve build failures.
- File descriptor leaks in `io_uring` backend on Linux.
- Macro collision with `FAR` in Windows system headers.
- Race condition causing `SIGABRT` on macOS during shutdown.
- Sequential HTTP request loop hang in Ubuntu CI.

### Changed
- Updated `README.md` with final benchmark results.
- Refined `SECURITY.md` contact info and SLAs.
- Simplified npm-release CI workflow.

## [1.0.3] - 2026-05-14

### Changed
- Chore: Bump version and internal dependency updates.

## [1.0.2] - 2026-04-20

### Fixed
- Initial stability fixes for cross-platform builds.

## [1.0.0] - 2026-04-01

### Added
- Initial release of `@iopress/core`.
- Support for `io_uring` (Linux), `kqueue` (macOS/BSD), and `IOCP` (Windows).
- Express-compatible API layer.
- Fast-path routing engine.

[1.0.4]: https://github.com/senapati484/iopress/compare/v1.0.3...v1.0.4
[1.0.3]: https://github.com/senapati484/iopress/compare/v1.0.2...v1.0.3
[1.0.2]: https://github.com/senapati484/iopress/compare/v1.0.0...v1.0.2
[1.0.0]: https://github.com/senapati484/iopress/releases/tag/v1.0.0
