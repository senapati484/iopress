# Benchmark Runbook

## Overview

This runbook documents how to run performance benchmarks for `@iopress/core`, compare against Express.js, and interpret results.

## Quick Start

```bash
# Install dependencies and build native addon
npm install
npm run build

# Run quick benchmark (iopress only)
node benchmarks/run.js

# Run full comparison with Express.js
node benchmarks/run.js --compare

# Run in CI mode (strict thresholds)
node benchmarks/run.js --ci
```

## Prerequisites

- Node.js 18+ 
- Platform-specific build tools:
  - **Linux**: `liburing-dev` package
  - **macOS**: Xcode Command Line Tools
  - **Windows**: Visual Studio Build Tools

## Benchmark Scenarios

| Scenario | Method | Path | Description |
|----------|--------|------|-------------|
| hello-world | GET | `/` | Simple JSON response |
| health-check | GET | `/health` | Health endpoint |
| route-params | GET | `/users/123` | Route parameter extraction |
| json-body | POST | `/echo` | POST with JSON body |
| query-params | GET | `/search?q=test` | Query string parsing |

## Interpreting Results

### Target Thresholds

| Platform | Backend | Minimum RPS | Target RPS | Max p99 Latency |
|----------|---------|-------------|------------|-----------------|
| Linux | io_uring | 300,000 | 500,000 | <2ms |
| macOS | kqueue | 80,000 | 150,000 | <5ms |
| Windows | IOCP | 50,000 | 100,000 | <8ms |
| Express.js | - | 18,000 | 20,000 | <50ms |

### Success Criteria

1. **RPS meets minimum threshold** for your platform
2. **p99 latency** below platform target
3. **Consistent results** within ±5% across 3 runs

## Troubleshooting

### Low Performance

If results are below expected thresholds:

1. **Check CPU governor** (Linux): `echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor`
2. **Disable thermal throttling**: Ensure laptop is not overheating
3. **Check for background processes**: Close unnecessary applications
4. **Verify Release build**: Ensure using optimized build, not debug

### Benchmark Errors

- **Connection refused**: Server failed to start - check build succeeded
- **High error rate**: Check server logs for errors
- **Timeout**: Increase test duration or reduce concurrency

## Platform-Specific Notes

### Linux (io_uring)

- Requires kernel 5.1+
- Install liburing: `sudo apt-get install liburing-dev` (Ubuntu/Debian)
- Best performance: 500,000+ req/s expected

### macOS (kqueue)

- Works out of the box on macOS 10.14+
- Expected: 150,000+ req/s

### Windows (IOCP)

- Requires Windows 10/Server 2016+
- Expected: 100,000+ req/s

## CI Integration

For automated CI runs:

```bash
# Set CI environment variable
export CI=true

# Run regression test
npm run bench:ci

# Check exit code (0 = pass, 1 = fail)
```

## Output

Results are saved to `benchmarks/results/` as JSON files:

```json
{
  "timestamp": "2024-01-15T10:30:00Z",
  "platform": "darwin",
  "target": { "backend": "kqueue", "minRps": 80000, "targetRps": 150000 },
  "results": {
    "iopress": { "hello-world": { "stats": { "rps": 155000, "p99": 2.1 } } },
    "express": { "hello-world": { "stats": { "rps": 18000, "p99": 22 } } }
  }
}
```