## v1.0.5 — Security Fix Release

This release fixes a remotely reachable out-of-bounds read (CWE-126) in the native HTTP chunked parser, along with several other correctness and compatibility fixes.

### Security

**CWE-126: Buffer over-read in chunked HTTP parser**

A remote unauthenticated attacker could declare a large chunk size in a `Transfer-Encoding: chunked` request while providing much less data, causing the parser to mark the request as complete with an inflated `body_length`. During normal request dispatch to JavaScript, this could result in an out-of-bounds read from the connection buffer.

The fix enforces strict chunk validation: declared chunk sizes are verified against the received buffer, each chunk's trailing CRLF is validated, and the final `0\r\n\r\n` marker is accepted only as a proper terminal chunk.

*Reported by **@servelt** — thank you for the detailed report and responsible disclosure.*

### Fixed

- **CWE-126** buffer over-read in chunked HTTP parser (reported by @servelt)
- Chunked body assembly: raw chunked format passed to JS instead of dechunked body
- `bytes_consumed` for chunked requests (fixes keep-alive/pipelining)
- `find_header_boundary` rejecting `\r\n` as valid blank line
- Windows IOCP backend buffer mismatch: data read into `ov->buffer` but binding read from `conn->buffer`

### Changed

- Minimum Node.js version lowered to 16.x for wider npm compatibility
- `node-gyp` moved to runtime dependencies for `npm install` scenarios
