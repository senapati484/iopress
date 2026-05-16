# Changelog

All notable changes to this project will be documented in this file.

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
