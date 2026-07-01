/**
 * @iopress/core Slow-Path Benchmark
 *
 * Focuses on the request handling path that real Express users hit:
 * route-params, json-body, query-params. These all go through the
 * JS layer (C fast router only handles literal paths).
 *
 * Run:
 *   node benchmarks/slow-path.js
 *   node benchmarks/slow-path.js --compare    # also bench Express
 *
 * Output:
 *   Per-scenario RPS, p50/p99 latency.
 *   Aggregate "slow path" metric (geometric mean of the 3 scenarios).
 */

'use strict';

const fs = require('fs');
const http = require('http');
const path = require('path');
const { spawn } = require('child_process');
const net = require('net');
const autocannon = require('autocannon');

const COLORS = {
  reset: '\x1b[0m',
  red: '\x1b[31m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  cyan: '\x1b[36m',
  bold: '\x1b[1m',
};

const CONFIG = {
  warmupDuration: 2,    // seconds
  testDuration: 5,      // seconds per scenario
  connections: 500,
  pipelining: 10,
  scenarios: [
    {
      name: 'route-params',
      method: 'GET',
      path: '/users/42',
      description: 'Parametric route (C parser → JS _matchRoute → handler)',
    },
    {
      name: 'json-body',
      method: 'POST',
      path: '/echo',
      body: '{"x":1}',
      headers: { 'Content-Type': 'application/json' },
      description: 'POST with body (C parser body extraction → JS handler)',
    },
    {
      name: 'query-params',
      method: 'GET',
      path: '/search?q=hello&page=2',
      description: 'Query string (C parser → JS Map construction)',
    },
  ],
};

function log(msg, color = 'reset') {
  console.log(COLORS[color] + msg + COLORS.reset);
}

function median(arr) {
  const sorted = [...arr].sort((a, b) => a - b);
  return sorted[Math.floor(sorted.length / 2)];
}

async function getPort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => {
      const { port } = server.address();
      server.close(() => resolve(port));
    });
  });
}

function buildIopressServer(port) {
  // enableDefaultRoutes is FALSE so the C fast router doesn't kick in.
  // Every request goes through the JS slow path.
  const code = `
    const iopress = require('../index.js');
    const app = iopress({ enableDefaultRoutes: false });

    app.get('/users/:id', (req, res) => res.json({ id: req.params.id }));
    app.post('/echo', (req, res) => res.json(req.body));
    app.get('/search', (req, res) => res.json({ q: req.query.q, p: req.query.page }));

    app.listen(${port}, () => console.log('iopress slow-path on ${port}'));
  `;
  return code;
}

function buildExpressServer(port) {
  const code = `
    const express = require('express');
    const app = express();
    app.use(express.json());

    app.get('/users/:id', (req, res) => res.json({ id: req.params.id }));
    app.post('/echo', (req, res) => res.json(req.body));
    app.get('/search', (req, res) => res.json({ q: req.query.q, p: req.query.page }));

    app.listen(${port}, () => console.log('express slow-path on ${port}'));
  `;
  return code;
}

function startServer(code, port) {
  return new Promise((resolve, reject) => {
    const tempFile = path.join(__dirname, '_temp_slowpath.js');
    fs.writeFileSync(tempFile, code);
    const proc = spawn('node', [tempFile], {
      cwd: path.join(__dirname, '..'),
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    let resolved = false;
    const startupTimer = setTimeout(() => {
      if (!resolved) { resolved = true; reject(new Error('Server startup timeout')); }
    }, 10000);
    proc.stdout.on('data', (d) => {
      if (!resolved && d.toString().includes(`on ${port}`)) {
        resolved = true;
        clearTimeout(startupTimer);
        setTimeout(() => resolve(proc), 500);
      }
    });
    proc.stderr.on('data', (d) => console.error('  [server stderr]', d.toString().trim()));
    proc.once('exit', (code) => {
      if (!resolved) { resolved = true; clearTimeout(startupTimer); reject(new Error(`Server exited code=${code}`)); }
    });
  });
}

function runScenario(port, scenario, duration, connections) {
  return new Promise((resolve, reject) => {
    const instance = autocannon({
      url: `http://localhost:${port}${scenario.path}`,
      method: scenario.method || 'GET',
      duration,
      connections,
      pipelining: CONFIG.pipelining,
      headers: scenario.headers || {},
      body: scenario.body,
    }, (err, result) => {
      if (err) return reject(err);
      const lat = result.latency || result.latencies || {};
      resolve({
        rps: result.requests.total / duration,
        p50: lat.p50 || 0,
        p99: lat.p99 || 0,
        mean: lat.mean || 0,
      });
    });
  });
}

async function warmup(port) {
  log('  Warming up...', 'yellow');
  const end = Date.now() + CONFIG.warmupDuration * 1000;
  while (Date.now() < end) {
    await new Promise((r) => {
      http.get(`http://localhost:${port}/search?q=warmup`, (res) => {
        res.resume();
        res.on('end', r);
      }).on('error', r);
    });
  }
}

async function benchIopress(runs) {
  log('\n[1/2] @iopress/core (slow path — JS-handled routes)', 'cyan');
  const port = await getPort();
  const proc = await startServer(buildIopressServer(port), port);
  await warmup(port);

  const results = {};
  for (const scenario of CONFIG.scenarios) {
    const samples = [];
    for (let i = 0; i < runs; i++) {
      const r = await runScenario(port, scenario, CONFIG.testDuration, CONFIG.connections);
      samples.push(r);
    }
    const med = {
      rps: Math.round(median(samples.map((s) => s.rps))),
      p50: median(samples.map((s) => s.p50)),
      p99: median(samples.map((s) => s.p99)),
      mean: median(samples.map((s) => s.mean)),
    };
    results[scenario.name] = med;
    log(`    ${scenario.name.padEnd(15)} → ${med.rps.toLocaleString().padStart(8)} RPS   p99 ${med.p99.toFixed(1).padStart(6)}ms`, med.rps > 100000 ? 'green' : 'yellow');
  }
  proc.kill();
  return results;
}

async function benchExpress(runs) {
  log('\n[2/2] Express.js (slow path)', 'cyan');
  const port = await getPort();
  const proc = await startServer(buildExpressServer(port), port);
  await warmup(port);

  const results = {};
  for (const scenario of CONFIG.scenarios) {
    const samples = [];
    for (let i = 0; i < runs; i++) {
      const r = await runScenario(port, scenario, CONFIG.testDuration, CONFIG.connections);
      samples.push(r);
    }
    const med = {
      rps: Math.round(median(samples.map((s) => s.rps))),
      p50: median(samples.map((s) => s.p50)),
      p99: median(samples.map((s) => s.p99)),
      mean: median(samples.map((s) => s.mean)),
    };
    results[scenario.name] = med;
    log(`    ${scenario.name.padEnd(15)} → ${med.rps.toLocaleString().padStart(8)} RPS   p99 ${med.p99.toFixed(1).padStart(6)}ms`, 'yellow');
  }
  proc.kill();
  return results;
}

function summary(ios, exp) {
  log('\n' + '═'.repeat(70), 'cyan');
  log('  SLOW-PATH RESULTS', 'cyan');
  log('═'.repeat(70), 'cyan');
  log('  Scenario        │ @iopress/core │ Express.js    │ Speedup', 'cyan');
  log('═'.repeat(70), 'cyan');

  let totalIos = 0;
  let totalExp = 0;
  let count = 0;

  for (const scenario of CONFIG.scenarios) {
    const i = ios[scenario.name].rps;
    const e = exp ? exp[scenario.name].rps : 0;
    const speedup = e > 0 ? (i / e).toFixed(1) + 'x' : 'N/A';
    log(`  ${scenario.name.padEnd(15)} │ ${i.toLocaleString().padStart(13)} │ ${e > 0 ? e.toLocaleString().padStart(13) : 'N/A'.padStart(13)} │ ${speedup}`, 'cyan');
    if (e > 0) {
      totalIos += i;
      totalExp += e;
      count++;
    }
  }
  log('═'.repeat(70), 'cyan');

  if (count > 0) {
    const avgIos = Math.round(totalIos / count);
    const avgExp = Math.round(totalExp / count);
    const speedup = (avgIos / avgExp).toFixed(1) + 'x';
    log(`  AVG             │ ${avgIos.toLocaleString().padStart(13)} │ ${avgExp.toLocaleString().padStart(13)} │ ${speedup}`, avgIos > avgExp ? 'green' : 'red');
    log('═'.repeat(70), 'cyan');
    log('  Slow-path gate: 80K RPS (Round 2 target: 120-150K)', 'yellow');
  } else {
    const avgIos = Math.round(Object.values(ios).reduce((a, b) => a + b.rps, 0) / CONFIG.scenarios.length);
    log(`  AVG iopress     │ ${avgIos.toLocaleString().padStart(13)}`, 'cyan');
  }
}

async function main() {
  const args = process.argv.slice(2);
  const compare = args.includes('--compare');
  const runs = (() => {
    const idx = args.indexOf('--runs');
    if (idx >= 0 && args[idx + 1]) return Math.max(1, parseInt(args[idx + 1], 10));
    return 3;
  })();

  log('\n' + '═'.repeat(70), 'cyan');
  log('  iopress SLOW-PATH BENCHMARK', 'cyan');
  log(`  ${CONFIG.connections} connections, pipelining ${CONFIG.pipelining}, ${CONFIG.testDuration}s × ${runs} runs`, 'cyan');
  log('═'.repeat(70), 'cyan');

  const ios = await benchIopress(runs);
  const exp = compare ? await benchExpress(runs) : null;
  summary(ios, exp);

  // Cleanup
  const tempFile = path.join(__dirname, '_temp_slowpath.js');
  if (fs.existsSync(tempFile)) fs.unlinkSync(tempFile);
}

main().catch((err) => {
  console.error('Benchmark failed:', err);
  const tempFile = path.join(__dirname, '_temp_slowpath.js');
  if (fs.existsSync(tempFile)) fs.unlinkSync(tempFile);
  process.exit(1);
});
