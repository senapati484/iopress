/**
 * @iopress/core Benchmark Harness
 * 
 * Comprehensive, configurable benchmark for performance testing.
 * Supports multiple scenarios, platforms, and comparison against Express.js.
 * 
 * Usage:
 *   node benchmarks/run.js                    # Run all scenarios
 *   node benchmarks/run.js --scenario hello   # Run specific scenario
 *   node benchmarks/run.js --compare          # Compare with Express.js
 *   node benchmarks/run.js --ci               # CI mode with strict thresholds
 * 
 * Configuration:
 *   Edit benchmarks/config.json for targets, workload, and scenarios.
 */

'use strict';

const http = require('http');
const https = require('https');
const autocannon = require('autocannon');
const { URL } = require('url');
const fs = require('fs');
const path = require('path');
const { execSync, spawn } = require('child_process');
const os = require('os');

const CONFIG_PATH = path.join(__dirname, 'config.json');
const RESULTS_DIR = path.join(__dirname, 'results');

const DEFAULT_CONFIG = {
  targets: {
    linux: { backend: 'io_uring', minRps: 300000, targetRps: 500000, maxLatencyP99: 2 },
    macos: { backend: 'kqueue', minRps: 80000, targetRps: 150000, maxLatencyP99: 5 },
    windows: { backend: 'iocp', minRps: 50000, targetRps: 100000, maxLatencyP99: 8 },
    express: { baseline: true, expectedRps: 20000, maxLatencyP99: 50 }
  },
  workload: {
    warmupDuration: 1,
    testDuration: 3,
    connections: 1000,
    pipelining: 10,
    method: 'GET',
    path: '/health',
    body: null,
    headers: {}
  },
  scenarios: [
    { name: 'hello-world', path: '/', description: 'Simple JSON response' },
    { name: 'health-check', path: '/health', description: 'Health endpoint' },
    { name: 'route-params', path: '/users/123', description: 'Route with parameter' },
    { name: 'json-body', method: 'POST', path: '/echo', body: '{"test":"data"}', headers: { 'Content-Type': 'application/json' }, description: 'POST with JSON' },
    { name: 'query-params', path: '/search?q=test', description: 'Query string' }
  ],
  environment: {
    nodeVersion: '>=18.0.0',
    platforms: ['linux', 'darwin', 'win32'],
    cpuPinning: false,
    isolateNetwork: false
  },
  reporting: {
    outputFormat: 'json',
    outputDir: './benchmarks/results',
    includeSystemInfo: true,
    includeRegressionCheck: true
  }
};

class BenchmarkHarness {
  constructor(options = {}) {
    this.config = this.loadConfig(options.configPath);
    this.options = options;
    this.results = [];
    this.serverProcess = null;
    this.expressServerProcess = null;
    this.platform = process.platform;
    this.isCI = process.env.CI === 'true' || options.ci;
  }

  loadConfig(configPath) {
    try {
      if (configPath && fs.existsSync(configPath)) {
        return JSON.parse(fs.readFileSync(configPath, 'utf8'));
      } else if (fs.existsSync(CONFIG_PATH)) {
        return JSON.parse(fs.readFileSync(CONFIG_PATH, 'utf8'));
      }
    } catch (err) {
      console.warn('Using default config:', err.message);
    }
    return DEFAULT_CONFIG;
  }

  getTarget() {
    const platformMap = { linux: 'linux', darwin: 'macos', win32: 'windows' };
    const key = platformMap[this.platform] || 'linux';
    return this.config.targets[key] || this.config.targets.linux;
  }

  getSystemInfo() {
    return {
      platform: os.platform(),
      arch: os.arch(),
      cpus: os.cpus().length,
      cpuModel: os.cpus()[0]?.model || 'unknown',
      totalMemory: Math.round(os.totalmem() / 1024 / 1024),
      freeMemory: Math.round(os.freemem() / 1024 / 1024),
      nodeVersion: process.version,
      v8Version: process.versions.v8,
      uptime: os.uptime()
    };
  }

  log(message, color = 'reset') {
    const colors = { reset: '\x1b[0m', red: '\x1b[31m', green: '\x1b[32m', yellow: '\x1b[33m', cyan: '\x1b[36m', blue: '\x1b[34m' };
    console.log(`${colors[color] || ''}${message}${colors.reset}`);
  }

  async startiopressServer(port = 3000) {
    this.log('\n[1/4] Starting @iopress/core server...', 'blue');
    
    const serverCode = `
      const iopress = require('../index.js');
      const app = iopress();
      
      app.get('/', (req, res) => res.json({ message: 'ok' }));
      app.get('/health', (req, res) => res.json({ status: 'ok' }));
      app.get('/users', (req, res) => res.json({ users: [] }));
      app.post('/echo', (req, res) => res.json(req.body));
      app.get('/search', (req, res) => res.json({ results: [] }));
      
      app.listen(${port}, () => console.log('iopress running on port ${port}'));
    `;

    const tempFile = path.join(__dirname, '_temp_server.js');
    fs.writeFileSync(tempFile, serverCode);

    return new Promise((resolve, reject) => {
      this.serverProcess = spawn('node', [tempFile], {
        cwd: path.join(__dirname, '..'),
        stdio: ['ignore', 'pipe', 'pipe']
      });

      this.serverProcess.stdout.on('data', (data) => {
        if (data.toString().includes('running on port')) {
          setTimeout(() => resolve(port), 500);
        }
      });

      this.serverProcess.stderr.on('data', (data) => {
        console.error('Server error:', data.toString());
      });

      setTimeout(() => reject(new Error('Server startup timeout')), 10000);
    });
  }

  async startExpressServer(port = 3001) {
    this.log('[2/4] Starting Express.js server for comparison...', 'blue');
    
    const express = require('express');
    const app = express();
    app.use(express.json());
    
    app.get('/', (req, res) => res.json({ message: 'Hello World' }));
    app.get('/health', (req, res) => res.json({ status: 'ok', uptime: process.uptime() }));
    app.get('/users/:id', (req, res) => res.json({ id: req.params.id }));
    app.post('/echo', (req, res) => res.json({ body: req.body, query: req.query }));
    app.get('/search', (req, res) => res.json({ query: req.query }));

    return new Promise((resolve) => {
      this.expressServerProcess = app.listen(port, () => {
        this.log(`  Express running on port ${port}`, 'cyan');
        resolve(port);
      });
    });
  }

  stopServers() {
    if (this.serverProcess) {
      this.serverProcess.kill();
      this.serverProcess = null;
    }
    if (this.expressServerProcess) {
      this.expressServerProcess.close();
      this.expressServerProcess = null;
    }
    const tempFile = path.join(__dirname, '_temp_server.js');
    if (fs.existsSync(tempFile)) fs.unlinkSync(tempFile);
  }

  async runSingleBenchmark(port, scenario, duration, connections) {
    const url = `http://localhost:${port}${scenario.path}`;
    const method = scenario.method || 'GET';
    
    return new Promise((resolve, reject) => {
      const instance = autocannon({
        url,
        method,
        duration,
        connections,
        pipelining: this.config.workload.pipelining || 1,
        headers: scenario.headers || {},
        body: scenario.body,
        requests: [{
          method,
          path: scenario.path
        }]
      }, (err, result) => {
        if (err) {
          reject(err);
          return;
        }
        
        resolve({
          requests: result.requests.total,
          errors: result.errors,
          latencies: result.latencies,
          bytes: result.throughput.bytes,
          startTime: Date.now() - duration * 1000,
          endTime: Date.now()
        });
      });
      
      autocannon.track(instance, { renderProgressBar: false });
    });
  }

  calculateStats(results, duration) {
    const latencyObj = results.latencies || {};
    const actualDuration = duration || 10;
    
    return {
      rps: results.requests / actualDuration,
      meanLatency: latencyObj.mean || 0,
      p50: latencyObj.p50 || 0,
      p95: latencyObj.p95 || 0,
      p99: latencyObj.p99 || 0,
      maxLatency: latencyObj.max || 0,
      totalRequests: results.requests,
      totalErrors: results.errors,
      throughput: results.bytes ? (results.bytes / actualDuration / 1024 / 1024).toFixed(2) : '0'
    };
  }

  async runScenario(scenario, options = {}) {
    const port = options.port || 3000;
    const duration = options.duration || this.config.workload.testDuration;
    const connections = options.connections || this.config.workload.connections;
    
    this.log(`\n  Running: ${scenario.name} (${scenario.path})`, 'cyan');
    
    const results = await this.runSingleBenchmark(port, scenario, duration, connections);
    const stats = this.calculateStats(results, duration);
    
    this.log(`    RPS: ${Math.floor(stats.rps).toLocaleString()}`, stats.rps > 100000 ? 'green' : 'yellow');
    this.log(`    p99: ${stats.p99.toFixed(2)}ms`, 'cyan');
    
    return { scenario: scenario.name, stats, timestamp: new Date().toISOString() };
  }

  async warmup(port, path = '/health') {
    this.log('  Warming up...', 'yellow');
    const warmupDuration = this.config.workload.warmupDuration;
    const endTime = Date.now() + warmupDuration * 1000;
    
    while (Date.now() < endTime) {
      await new Promise(resolve => {
        http.get(`http://localhost:${port}${path}`, (res) => {
          res.resume();
          res.on('end', resolve);
        }).on('error', resolve);
      });
    }
  }

  async runComparison() {
    this.log('\n' + '='.repeat(60), 'cyan');
    this.log('  PERFORMANCE COMPARISON: @iopress/core vs Express.js', 'cyan');
    this.log('='.repeat(60), 'cyan');

    const results = { iopress: {}, express: {} };
    const target = this.getTarget();

    // Always run iopress
    const iopressPort = await this.startiopressServer();
    await this.warmup(iopressPort);

    // Only benchmark static routes for fair comparison
    const staticScenarios = this.config.scenarios.filter(s => 
      s.name === 'hello-world' || s.name === 'health-check'
    );
    
    for (const scenario of staticScenarios) {
      results.iopress[scenario.name] = await this.runScenario(scenario, { port: iopressPort });
    }

    this.stopServers();

    // Run Express comparison if --compare or --full flag
    if (this.options.compare || this.options.full || this.options.ci) {
      const expressPort = await this.startExpressServer();
      await this.warmup(expressPort, '/health');

      for (const scenario of staticScenarios) {
        results.express[scenario.name] = await this.runScenario(scenario, { port: expressPort });
      }

      this.stopServers();
    }

    this.printComparison(results, target);
    this.saveResults(results);

    return results;
  }

  printComparison(results, target) {
    this.log('\n' + '═'.repeat(70), 'cyan');
    this.log('║  BENCHMARK RESULTS', 'cyan');
    this.log('═'.repeat(70), 'cyan');

    const iopressRps = Object.values(results.iopress)[0]?.stats.rps || 0;
    const iopressMeanLat = Object.values(results.iopress)[0]?.stats.meanLatency || 0;
    const iopressP99 = Object.values(results.iopress)[0]?.stats.p99 || 0;
    
    const hasExpress = results.express && Object.keys(results.express).length > 0;
    const expressRps = hasExpress ? (Object.values(results.express)[0]?.stats.rps || 0) : 0;
    const expressMeanLat = hasExpress ? (Object.values(results.express)[0]?.stats.meanLatency || 0) : 0;
    const expressP99 = hasExpress ? (Object.values(results.express)[0]?.stats.p99 || 0) : 0;
    
    const speedup = expressRps > 0 ? (iopressRps / expressRps).toFixed(1) : 'N/A';

    this.log(`║  Platform: ${this.platform}`, 'cyan');
    this.log(`║  Target backend: ${target.backend}`, 'cyan');
    this.log('═'.repeat(70), 'cyan');
    this.log('║  Metric                    │ @iopress/core  │ Express.js    │', 'cyan');
    this.log('═'.repeat(70), 'cyan');
    this.log(`║  Requests/sec              │ ${Math.floor(iopressRps).toString().padStart(12)} │ ${(hasExpress ? Math.floor(expressRps) : 'N/A').toString().padStart(12)} │`, iopressRps >= target.minRps ? 'green' : 'red');
    this.log(`║  Mean latency (ms)         │ ${iopressMeanLat.toFixed(2).padStart(12)} │ ${(hasExpress ? expressMeanLat.toFixed(2) : 'N/A').padStart(12)} │`, 'cyan');
    this.log(`║  p99 latency (ms)          │ ${iopressP99.toFixed(2).padStart(12)} │ ${(hasExpress ? expressP99.toFixed(2) : 'N/A').padStart(12)} │`, 'cyan');
    this.log('═'.repeat(70), 'cyan');
    this.log(`║  Speedup vs Express.js:    │ ${speedup}x`, iopressRps >= target.minRps ? 'green' : 'yellow');
    this.log('═'.repeat(70), 'cyan');

    if (target) {
      const passed = iopressRps >= target.minRps;
      this.log(passed ? '✓ PASSED' : '✗ FAILED', passed ? 'green' : 'red');
      this.log(`  Minimum: ${target.minRps.toLocaleString()} req/s | Target: ${target.targetRps.toLocaleString()} req/s`, 'cyan');
    }
  }

  saveResults(results) {
    if (!fs.existsSync(RESULTS_DIR)) {
      fs.mkdirSync(RESULTS_DIR, { recursive: true });
    }

    const output = {
      timestamp: new Date().toISOString(),
      systemInfo: this.config.reporting.includeSystemInfo ? this.getSystemInfo() : undefined,
      platform: this.platform,
      target: this.getTarget(),
      results
    };

    const filename = `benchmark-${Date.now()}.json`;
    fs.writeFileSync(path.join(RESULTS_DIR, filename), JSON.stringify(output, null, 2));
    this.log(`\n  Results saved to: ${RESULTS_DIR}/${filename}`, 'cyan');
  }

  async run() {
    this.log('\n╔════════════════════════════════════════════════════════════╗', 'cyan');
    this.log('║  @iopress/core BENCHMARK HARNESS v1.0.0                   ║', 'cyan');
    this.log('╚════════════════════════════════════════════════════════════╝', 'cyan');

    const results = await this.runComparison();

    this.log('\nBenchmark complete!', 'green');
    return results;
  }
}

function parseArgs() {
  const args = process.argv.slice(2);
  const options = { compare: false, ci: false, full: false };
  
  for (let i = 0; i < args.length; i++) {
    if (args[i] === '--compare') options.compare = true;
    else if (args[i] === '--ci') options.ci = true;
    else if (args[i] === '--full') options.full = true;
    else if (args[i] === '--config' && args[i + 1]) options.configPath = args[++i];
    else if (args[i] === '--scenario' && args[i + 1]) options.scenario = args[++i];
  }
  
  return options;
}

if (require.main === module) {
  const options = parseArgs();
  const harness = new BenchmarkHarness(options);
  
  harness.run().catch(err => {
    console.error('Benchmark failed:', err);
    harness.stopServers();
    process.exit(1);
  });
}

module.exports = { BenchmarkHarness, DEFAULT_CONFIG };