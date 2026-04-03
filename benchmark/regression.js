/**
 * Express-Pro Benchmark Regression Test
 *
 * Automated performance testing with autocannon.
 * Fails CI if req/s drops below platform-specific thresholds.
 *
 * Run: node benchmark/regression.js
 */

'use strict';

const http = require('http');
const expresspro = require('../js/index.js');

// Platform detection
const platform = process.platform;
const isCI = process.env.CI === 'true' || process.env.GITHUB_ACTIONS === 'true';

// Platform-specific thresholds (req/s)
const THRESHOLDS = {
  linux: { min: 300000, target: 500000 },    // 300k min, 500k target
  darwin: { min: 80000, target: 150000 },      // 80k min, 150k target (macOS)
  win32: { min: 50000, target: 100000 },      // 50k min, 100k target (Windows)
};

// Get threshold for current platform
function getThreshold() {
  const key = platform === 'darwin' ? 'darwin' : platform;
  return THRESHOLDS[key] || THRESHOLDS.linux; // Default to Linux thresholds
}

// Colors for terminal output
const colors = {
  reset: '\x1b[0m',
  red: '\x1b[31m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  blue: '\x1b[34m',
  cyan: '\x1b[36m',
};

function log(message, color = 'reset') {
  console.log(`${colors[color]}${message}${colors.reset}`);
}

// Format number with commas
function formatNumber(num) {
  return num.toString().replace(/\B(?=(\d{3})+(?!\d))/g, ',');
}

// Simple HTTP benchmark without autocannon (if not available)
async function runSimpleBenchmark(port, duration) {
  const results = {
    requests: { mean: 0 },
    latency: { mean: 0, p99: 0 },
    throughput: { mean: 0 }
  };

  let totalRequests = 0;
  let totalBytes = 0;
  const startTime = Date.now();
  const endTime = startTime + (duration * 1000);

  // Warmup phase
  log('  Warming up...', 'yellow');
  for (let i = 0; i < 100; i++) {
    await new Promise((resolve) => {
      http.get(`http://localhost:${port}/health`, (res) => {
        res.resume();
        res.on('end', resolve);
      }).on('error', () => resolve());
    });
  }

  log('  Benchmarking...', 'cyan');
  const latencies = [];

  // Send requests as fast as possible
  const requests = [];
  while (Date.now() < endTime) {
    const reqStart = process.hrtime.bigint();

    const promise = new Promise((resolve) => {
      const req = http.get(`http://localhost:${port}/health`, (res) => {
        let size = 0;
        res.on('data', (chunk) => { size += chunk.length; });
        res.on('end', () => {
          const latency = Number(process.hrtime.bigint() - reqStart) / 1000000; // ms
          latencies.push(latency);
          totalBytes += size;
          resolve();
        });
      });
      req.on('error', () => resolve());
      req.setTimeout(5000, () => {
        req.destroy();
        resolve();
      });
    });

    requests.push(promise);
    totalRequests++;

    // Limit concurrent requests
    if (requests.length > 100) {
      await Promise.all(requests.splice(0, 50));
    }
  }

  await Promise.all(requests);

  const actualDuration = (Date.now() - startTime) / 1000;
  results.requests.mean = totalRequests / actualDuration;
  results.throughput.mean = (totalBytes / actualDuration) / 1024 / 1024; // MB/s

  if (latencies.length > 0) {
    latencies.sort((a, b) => a - b);
    results.latency.mean = latencies.reduce((a, b) => a + b, 0) / latencies.length;
    results.latency.p99 = latencies[Math.floor(latencies.length * 0.99)];
  }

  return results;
}

// Run autocannon benchmark if available
async function runAutocannon(port, duration) {
  try {
    const autocannon = require('autocannon');

    const result = await autocannon({
      url: `http://localhost:${port}/health`,
      duration: duration,
      connections: 100,
      pipelining: 1,
      warmup: [{
        connections: 10,
        duration: 2
      }]
    });

    return result;
  } catch (err) {
    log('  autocannon not available, using simple benchmark', 'yellow');
    return runSimpleBenchmark(port, duration);
  }
}

// Print results table
function printResults(results, threshold) {
  const { min, target } = threshold;
  const reqPerSec = results.requests.mean;
  const latencyMean = results.latency.mean.toFixed(2);
  const latencyP99 = results.latency.p99.toFixed(2);
  const throughput = results.throughput.mean.toFixed(2);

  const passed = reqPerSec >= min;
  const status = passed ? 'PASS' : 'FAIL';
  const statusColor = passed ? 'green' : 'red';

  console.log('');
  log('╔════════════════════════════════════════════════════════════╗', 'cyan');
  log('║              BENCHMARK RESULTS                             ║', 'cyan');
  log('╠════════════════════════════════════════════════════════════╣', 'cyan');
  log(`║ Platform:        ${platform.padEnd(45)}║`, 'cyan');
  log(`║ CI Mode:         ${(isCI ? 'YES' : 'NO').padEnd(45)}║`, 'cyan');
  log('╠════════════════════════════════════════════════════════════╣', 'cyan');
  log('║ Metric                    │ Value                          ║', 'cyan');
  log('╠════════════════════════════════════════════════════════════╣', 'cyan');
  log(`║ Requests/sec (mean)       │ ${formatNumber(Math.floor(reqPerSec)).padEnd(30)} ║`, reqPerSec >= target ? 'green' : (reqPerSec >= min ? 'yellow' : 'red'));
  log(`║ Latency (mean)            │ ${(latencyMean + ' ms').padEnd(30)} ║`, 'cyan');
  log(`║ Latency (p99)             │ ${(latencyP99 + ' ms').padEnd(30)} ║`, 'cyan');
  log(`║ Throughput                │ ${(throughput + ' MB/s').padEnd(30)} ║`, 'cyan');
  log('╠════════════════════════════════════════════════════════════╣', 'cyan');
  log('║ Thresholds                │                                ║', 'cyan');
  log(`║   Minimum (CI fail)       │ ${formatNumber(min).padEnd(30)} ║`, 'cyan');
  log(`║   Target                  │ ${formatNumber(target).padEnd(30)} ║`, 'cyan');
  log(`║   Actual                  │ ${formatNumber(Math.floor(reqPerSec)).padEnd(30)} ║`, statusColor);
  log(`║   Status                  │ ${status.padEnd(30)} ║`, statusColor);
  log('╚════════════════════════════════════════════════════════════╝', 'cyan');
  console.log('');

  return passed;
}

// Main benchmark runner
async function main() {
  log('');
  log('╔════════════════════════════════════════════════════════════╗', 'cyan');
  log('║     EXPRESS-PRO BENCHMARK REGRESSION TEST                ║', 'cyan');
  log('╚════════════════════════════════════════════════════════════╝', 'cyan');
  log('');

  const threshold = getThreshold();
  const port = 9999;
  const duration = isCI ? 5 : 3; // 5 seconds in CI, 3 locally

  // Create app and start server
  log('Starting server...', 'blue');
  const app = expresspro();

  // Try to register fast route for /health (bypasses JavaScript entirely)
  const native = require('../build/Release/express_pro_native');
  if (native.RegisterFastRoute) {
    log('  Using fast router for /health', 'cyan');
    native.RegisterFastRoute('GET', '/health', 200, '{"status":"ok"}');
  } else {
    // Fallback to JavaScript route
    app.get('/health', (req, res) => {
      res.json({ status: 'ok' });
    });
  }

  // Start server
  const server = app.listen(port, () => {
    log(`  Server listening on port ${port}`, 'green');
  });

  // Wait for server to be ready
  await new Promise(resolve => setTimeout(resolve, 100));

  try {
    // Run benchmark
    log(`Running ${duration}s benchmark...`, 'blue');
    const results = await runAutocannon(port, duration);

    // Print results
    const passed = printResults(results, threshold);

    // Cleanup
    log('Stopping server...', 'blue');
    await new Promise(resolve => setTimeout(resolve, 50));

    if (passed) {
      log('✓ Benchmark PASSED', 'green');
      process.exit(0);
    } else {
      log('✗ Benchmark FAILED - Performance regression detected', 'red');
      process.exit(1);
    }
  } catch (err) {
    log(`Error: ${err.message}`, 'red');
    process.exit(1);
  }
}

// Run if called directly
if (require.main === module) {
  main().catch(err => {
    console.error('Fatal error:', err);
    process.exit(1);
  });
}

module.exports = { main, getThreshold, THRESHOLDS };
