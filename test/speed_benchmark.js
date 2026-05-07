/**
 * iopress Speed Benchmark
 *
 * Self-contained benchmark that starts both iopress and Express in-process,
 * runs autocannon across multiple scenarios, and prints a comparison table.
 *
 * Run: node test/speed_benchmark.js
 * Run (short): node test/speed_benchmark.js --quick
 */

'use strict';

const autocannon = require('autocannon');
const http = require('http');
const { promisify } = require('util');
const os = require('os');

const QUICK = process.argv.includes('--quick');
const DURATION = QUICK ? 3 : 8;      // seconds per scenario
const CONNECTIONS = QUICK ? 100 : 200; // concurrent connections
const PIPELINING = 1;

const IOPRESS_PORT = 4001;
const EXPRESS_PORT = 4002;

const GREEN  = '\x1b[32m';
const RED    = '\x1b[31m';
const YELLOW = '\x1b[33m';
const CYAN   = '\x1b[36m';
const BOLD   = '\x1b[1m';
const RESET  = '\x1b[0m';

// ─────────────────────────── helpers ───────────────────────────

function fmt(n)    { return (n / 1000).toFixed(1) + 'k'; }
function fmtMs(n)  { return n.toFixed(2) + ' ms'; }
function pass(ok)  { return ok ? `${GREEN}✔ PASS${RESET}` : `${RED}✖ FAIL${RESET}`; }

function bar(ratio, width = 20) {
  const filled = Math.round(ratio * width);
  return GREEN + '█'.repeat(Math.min(filled, width)) + RESET +
         '░'.repeat(Math.max(0, width - filled));
}

function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

async function runAutocannon(url, opts = {}) {
  return new Promise((resolve, reject) => {
    const instance = autocannon({
      url,
      duration:    opts.duration    ?? DURATION,
      connections: opts.connections ?? CONNECTIONS,
      pipelining:  opts.pipelining  ?? PIPELINING,
      method:      opts.method      ?? 'GET',
      body:        opts.body        ?? undefined,
      headers:     opts.headers     ?? {},
    }, (err, result) => {
      if (err) reject(err); else resolve(result);
    });
    autocannon.track(instance, { renderProgressBar: false });
  });
}

// ─────────────────────────── iopress server ───────────────────────────

function startIopress(port) {
  return new Promise((resolve, reject) => {
    let mod;
    try { mod = require('../js/index.js'); }
    catch (e) { reject(e); return; }

    const app = mod();

    app.get('/', (req, res) => {
      res.json({ message: 'Hello World' });
    });

    app.get('/health', (req, res) => {
      res.json({ status: 'ok' });
    });

    app.get('/users/:id', (req, res) => {
      res.json({ userId: req.params.id });
    });

    app.post('/echo', (req, res) => {
      res.json({ received: req.body });
    });

    app.get('/search', (req, res) => {
      res.json({ results: [] });
    });

    let server;
    server = app.listen(port, () => resolve({ app, server }));
    server?.on?.('error', reject);
    // Fallback: resolve after a short delay (native server may not emit 'error')
    setTimeout(() => resolve({ app, server }), 200);
  });
}

// ─────────────────────────── Express server ───────────────────────────

function startExpress(port) {
  return new Promise((resolve, reject) => {
    let express;
    try { express = require('express'); }
    catch (e) { reject(new Error('Express not installed: ' + e.message)); return; }

    const app = express();
    app.use(express.json());

    app.get('/', (req, res) => res.json({ message: 'Hello World' }));
    app.get('/health', (req, res) => res.json({ status: 'ok' }));
    app.get('/users/:id', (req, res) => res.json({ userId: req.params.id }));
    app.post('/echo', (req, res) => res.json({ received: req.body }));
    app.get('/search', (req, res) => res.json({ results: [] }));

    const server = app.listen(port, () => resolve({ app, server }));
    server.on('error', reject);
  });
}

// ─────────────────────────── scenarios ───────────────────────────

const SCENARIOS = [
  {
    name: 'GET /health (fast-path)',
    desc: 'Static fast-path: pre-built C response, zero JS overhead',
    iiopath: '/health',
    expath: '/health',
    method: 'GET',
    fastPath: true,
  },
  {
    name: 'GET / (fast-path)',
    desc: 'Root fast-path C response',
    iiopath: '/',
    expath: '/',
    method: 'GET',
    fastPath: true,
  },
  {
    name: 'GET /users/42 (JS route params)',
    desc: 'JS-handled dynamic route with param extraction',
    iiopath: '/users/42',
    expath: '/users/42',
    method: 'GET',
    fastPath: false,
  },
  {
    name: 'POST /echo (JS POST handler)',
    desc: 'POST with JSON body through JS layer',
    iiopath: '/echo',
    expath: '/echo',
    method: 'POST',
    body: '{"msg":"bench","num":1}',
    headers: { 'Content-Type': 'application/json' },
    fastPath: false,
  },
  {
    name: 'GET /search (JS handler)',
    desc: 'JS-handled simple GET',
    iiopath: '/search',
    expath: '/search',
    method: 'GET',
    fastPath: false,
  },
];

// ─────────────────────────── main ───────────────────────────

async function main() {
  console.log();
  console.log(`${BOLD}${CYAN}══════════════════════════════════════════════════════${RESET}`);
  console.log(`${BOLD}${CYAN}   iopress Speed Benchmark  (autocannon)${RESET}`);
  console.log(`${BOLD}${CYAN}══════════════════════════════════════════════════════${RESET}`);
  console.log(`  Platform   : ${os.platform()} ${os.arch()}`);
  console.log(`  Node.js    : ${process.version}`);
  console.log(`  CPUs       : ${os.cpus().length}x ${os.cpus()[0].model.trim()}`);
  console.log(`  Duration   : ${DURATION}s per scenario (${QUICK ? 'quick' : 'full'} mode)`);
  console.log(`  Connections: ${CONNECTIONS} concurrent`);
  console.log();

  // ── Start servers ──────────────────────────────────────────
  process.stdout.write('  Starting iopress server … ');
  const { app: ioApp } = await startIopress(IOPRESS_PORT);
  await sleep(300);
  console.log(`${GREEN}ready on :${IOPRESS_PORT}${RESET}`);

  let expressHandle = null;
  process.stdout.write('  Starting Express server  … ');
  try {
    expressHandle = await startExpress(EXPRESS_PORT);
    await sleep(200);
    console.log(`${GREEN}ready on :${EXPRESS_PORT}${RESET}`);
  } catch (e) {
    console.log(`${YELLOW}skipped (${e.message})${RESET}`);
  }

  await sleep(300);

  // ── Warm-up ────────────────────────────────────────────────
  process.stdout.write('\n  Warming up iopress… ');
  await runAutocannon(`http://localhost:${IOPRESS_PORT}/health`, { duration: 1, connections: 10 });
  console.log('done');

  if (expressHandle) {
    process.stdout.write('  Warming up Express…  ');
    await runAutocannon(`http://localhost:${EXPRESS_PORT}/health`, { duration: 1, connections: 10 });
    console.log('done');
  }

  // ── Run scenarios ──────────────────────────────────────────
  const results = [];

  for (const s of SCENARIOS) {
    console.log();
    console.log(`${BOLD}  ▶ ${s.name}${RESET}`);
    console.log(`    ${s.desc}`);

    // Allow previous autocannon connections to drain
    await sleep(400);

    // iopress
    process.stdout.write('    iopress  : ');
    const ioRes = await runAutocannon(`http://localhost:${IOPRESS_PORT}${s.iiopath}`, {
      method: s.method, body: s.body, headers: s.headers,
    });
    const ioRps = ioRes.requests.mean;
    const ioP99 = ioRes.latency.p99;
    console.log(`${GREEN}${Math.round(ioRps).toLocaleString()} req/s${RESET}  p99=${fmtMs(ioP99)}`);

    // Express
    let exRps = null, exP99 = null;
    if (expressHandle) {
      process.stdout.write('    Express  : ');
      const exRes = await runAutocannon(`http://localhost:${EXPRESS_PORT}${s.expath}`, {
        method: s.method, body: s.body, headers: s.headers,
      });
      exRps = exRes.requests.mean;
      exP99 = exRes.latency.p99;
      console.log(`${Math.round(exRps).toLocaleString()} req/s  p99=${fmtMs(exP99)}`);
    }

    results.push({ s, ioRps, ioP99, exRps, exP99 });
  }

  // ── Summary table ──────────────────────────────────────────
  console.log();
  console.log(`${BOLD}${CYAN}══ Results Summary ════════════════════════════════════${RESET}`);
  console.log();
  let allPass = true;
  const FAST_TARGET = 80000;
  const JS_TARGET   = 30000;

  for (const { s, ioRps, ioP99, exRps, exP99 } of results) {
    const multiplier = exRps ? (ioRps / exRps) : null;
    const target = s.fastPath ? FAST_TARGET : JS_TARGET;
    const ok = ioRps >= target;
    if (!ok) allPass = false;

    const ratio = Math.min(ioRps / (target * 1.5), 1); // scale bar to 1.5x target
    console.log(`  ${BOLD}${s.name}${RESET}`);
    console.log(`    ${bar(ratio)}  ${GREEN}${Math.round(ioRps).toLocaleString()}${RESET} req/s   p99 ${fmtMs(ioP99)}   ${pass(ok)}`);
    if (exRps) {
      const mStr = multiplier >= 1
        ? `${GREEN}${multiplier.toFixed(1)}x faster${RESET}`
        : `${RED}${(1/multiplier).toFixed(1)}x slower${RESET}`;
      console.log(`    vs Express: ${Math.round(exRps).toLocaleString()} req/s → ${mStr}`);
    }
    console.log();
  }

  // ── Performance thresholds ─────────────────────────────────
  // Fast-path (C only, no JS): ≥ 80k req/s on macOS kqueue
  // JS-path (TSFN overhead):   ≥ 30k req/s on macOS kqueue

  console.log(`${BOLD}${CYAN}══ Threshold Checks ═══════════════════════════════════${RESET}`);
  console.log(`  ${CYAN}[fast-path]${RESET} target ≥ ${(FAST_TARGET/1000).toFixed(0)}k req/s  ` +
              `${CYAN}[js-path]${RESET} target ≥ ${(JS_TARGET/1000).toFixed(0)}k req/s`);
  console.log();

  let passCount = 0;
  for (const { s, ioRps } of results) {
    const target = s.fastPath ? FAST_TARGET : JS_TARGET;
    const ok = ioRps >= target;
    if (ok) passCount++;
    const label = s.fastPath ? `${CYAN}[fast-path]${RESET}` : `[js-path]  `;
    const tStr  = s.fastPath ? `≥${(FAST_TARGET/1000).toFixed(0)}k` : `≥${(JS_TARGET/1000).toFixed(0)}k`;
    console.log(`  ${label} ${s.name.padEnd(38)} ${pass(ok)}  ${Math.round(ioRps).toLocaleString()} req/s  (${tStr})`);
  }

  console.log();
  if (passCount === results.length) {
    console.log(`${BOLD}${GREEN}  ✔ All ${results.length} scenarios meet performance targets${RESET}`);
  } else {
    console.log(`${BOLD}${RED}  ✖ ${results.length - passCount}/${results.length} scenarios below threshold${RESET}`);
  }

  // ── Teardown ───────────────────────────────────────────────
  console.log();
  process.stdout.write('  Shutting down servers… ');
  try { ioApp.close?.(() => {}); } catch (_) {}
  if (expressHandle) {
    await new Promise(r => expressHandle.server.close(r));
  }
  console.log('done\n');

  process.exit(passCount === results.length ? 0 : 1);
}

main().catch(e => { console.error(e); process.exit(1); });
