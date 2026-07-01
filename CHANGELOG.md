# Changelog

All notable changes to this project will be documented in this file.

## [1.0.5] - 2026-07-01

### Fixed
- CWE-126 buffer over-read in chunked HTTP parser
- Chunked body assembly: raw chunked format passed to JS instead of dechunked body
- `bytes_consumed` for chunked requests (fixes keep-alive/pipelining)
- `find_header_boundary` rejecting `\r\n` as valid blank line
- Windows IOCP backend buffer mismatch: data read into `ov->buffer` but binding read from `conn->buffer`

### Changed
- Minimum Node.js version lowered to 16.x for wider npm compatibility
- `node-gyp` moved to runtime dependencies for `npm install` scenarios

## [1.0.4] - 2026-05-16

### 🚀 Added
- Multi-threaded `kqueue` support for macOS.
- Node.js 22 LTS compatibility.
- Fast-path routing performance optimizations.

### 🛠 Fixed
- Windows header ordering and `FAR` macro conflicts.
- Linux `io_uring` file descriptor leaks.
- macOS `SIGABRT` shutdown race condition.
- CI stability improvements across all platforms.

### 📚 Documentation
- Verified benchmark results in README.
- Security reporting refinements in `SECURITY.md`.
- Troubleshooting tip for hard restarts added to README.

## [1.0.3] - 2026-05-14
- Chore: Version bump and internal cleanup.

## [1.0.0] - 2026-04-01
- Initial release with native `io_uring`, `kqueue`, and `IOCP` backends.
