/**
 * Stress Test Example - Performance and Concurrency Testing
 *
 * Demonstrates:
 * - High-performance request handling
 * - Concurrent connection handling
 * - Metrics collection (req/sec, latency, errors)
 *
 * Run: node examples/stress-test.js
 * Requires: npm install && npm run build
 *
 * This starts a server and runs automated stress tests using Node's http module.
 */

'use strict';

const expresspro = require('../index.js');
const http = require('http');

const app = expresspro();

// Stats tracking
let stats = {
  totalRequests: 0,
  successful: 0,
  failed: 0,
  latencies: [],
  bytesTransferred: 0
};

// Sample data
const users = [
  { id: 1, name: 'Alice', role: 'admin' },
  { id: 2, name: 'Bob', role: 'user' },
  { id: 3, name: 'Charlie', role: 'user' }
];

// Routes

// Simple GET - fastest path
app.get('/', (req, res) => {
  res.json({ status: 'ok', timestamp: Date.now() });
});

// Health check
app.get('/health', (req, res) => {
  res.json({
    status: 'healthy',
    uptime: process.uptime(),
    memory: process.memoryUsage(),
    platform: expresspro.platform
  });
});

// Get all users
app.get('/users', (req, res) => {
  res.json({ users, count: users.length });
});

// Get single user
app.get('/users/:id', (req, res) => {
  const user = users.find(u => u.id === parseInt(req.params.id, 10));
  if (user) {
    res.json(user);
  } else {
    res.status(404).json({ error: 'User not found' });
  }
});

// POST with body
app.post('/users', (req, res) => {
  const { name, role } = req.body || {};
  if (!name) {
    return res.status(400).json({ error: 'Name is required' });
  }
  const newUser = {
    id: users.length + 1,
    name,
    role: role || 'user',
    createdAt: Date.now()
  };
  users.push(newUser);
  res.status(201).json(newUser);
});

// Echo endpoint
app.post('/echo', (req, res) => {
  res.json({
    method: req.method,
    path: req.path,
    body: req.body,
    query: req.query
  });
});

// CPU simulation
app.get('/compute/:n', (req, res) => {
  const n = parseInt(req.params.n, 10) || 10;
  let result = 0;
  for (let i = 0; i < n; i++) {
    result += Math.random() * i;
  }
  res.json({ computed: result.toFixed(4) });
});

// Configuration
const PORT = parseInt(process.env.PORT, 10) || 3000;
const CONCURRENCY = parseInt(process.env.CONCURRENCY, 10) || 50;
const REQUESTS_PER_BATCH = parseInt(process.env.REQUESTS, 10) || 1000;

// HTTP request helper
function makeRequest(path, method = 'GET', body = null) {
  return new Promise((resolve) => {
    const start = Date.now();

    const options = {
      hostname: 'localhost',
      port: PORT,
      path: path,
      method: method,
      headers: {
        'Accept': 'application/json',
        'Connection': 'keep-alive'
      },
      agent: agent // Reuse connections
    };

    if (body) {
      options.headers['Content-Type'] = 'application/json';
    }

    const req = http.request(options, (res) => {
      let data = '';
      res.on('data', chunk => { data += chunk; stats.bytesTransferred += chunk.length; });
      res.on('end', () => {
        const latency = Date.now() - start;
        resolve({
          status: res.statusCode,
          latency: latency,
          size: data.length
        });
      });
    });

    req.on('error', () => {
      resolve({ status: 0, latency: Date.now() - start, error: true });
    });

    req.setTimeout(5000, () => {
      req.destroy();
      resolve({ status: 0, latency: Date.now() - start, timeout: true });
    });

    if (body) {
      req.write(body);
    }

    req.end();
  });
}

// Keep-alive agent for connection reuse
const agent = new http.Agent({
  keepAlive: true,
  maxSockets: CONCURRENCY
});

function resetStats() {
  stats = {
    totalRequests: 0,
    successful: 0,
    failed: 0,
    latencies: [],
    bytesTransferred: 0
  };
}

function printStats(testName, duration) {
  const avgLatency = stats.latencies.length > 0
    ? stats.latencies.reduce((a, b) => a + b, 0) / stats.latencies.length
    : 0;
  const minLatency = stats.latencies.length > 0 ? Math.min(...stats.latencies) : 0;
  const maxLatency = stats.latencies.length > 0 ? Math.max(...stats.latencies) : 0;

  // Calculate percentiles
  const sorted = [...stats.latencies].sort((a, b) => a - b);
  const p50 = sorted[Math.floor(sorted.length * 0.5)] || 0;
  const p95 = sorted[Math.floor(sorted.length * 0.95)] || 0;
  const p99 = sorted[Math.floor(sorted.length * 0.99)] || 0;

  const rps = duration > 0 ? (stats.totalRequests / duration).toFixed(2) : 0;
  const throughput = duration > 0 ? (stats.bytesTransferred / duration / 1024).toFixed(2) : 0;

  console.log(`\n--- ${testName} Results ---`);
  console.log(`Duration:        ${duration.toFixed(2)}s`);
  console.log(`Total Requests:  ${stats.totalRequests}`);
  console.log(`Successful:      ${stats.successful} (${((stats.successful/stats.totalRequests)*100).toFixed(1)}%)`);
  console.log(`Failed:          ${stats.failed} (${((stats.failed/stats.totalRequests)*100).toFixed(1)}%)`);
  console.log(`Requests/sec:    ${rps}`);
  console.log(`Throughput:      ${throughput} KB/s`);
  console.log('\nLatency (ms):');
  console.log(`  Min:           ${minLatency.toFixed(2)}`);
  console.log(`  Avg:           ${avgLatency.toFixed(2)}`);
  console.log(`  Max:           ${maxLatency.toFixed(2)}`);
  console.log(`  P50:           ${p50.toFixed(2)}`);
  console.log(`  P95:           ${p95.toFixed(2)}`);
  console.log(`  P99:           ${p99.toFixed(2)}`);
}

// Sequential stress test
async function runSequentialTest() {
  console.log(`\n=== Sequential Test: ${REQUESTS_PER_BATCH} requests ===`);
  resetStats();
  const startTime = Date.now();

  for (let i = 0; i < REQUESTS_PER_BATCH; i++) {
    const result = await makeRequest('/');
    stats.totalRequests++;
    if (result.status === 200) {
      stats.successful++;
      stats.latencies.push(result.latency);
    } else {
      stats.failed++;
    }

    if ((i + 1) % 100 === 0) {
      process.stdout.write(`\r  Progress: ${i + 1}/${REQUESTS_PER_BATCH}`);
    }
  }

  const duration = (Date.now() - startTime) / 1000;
  printStats('Sequential', duration);
}

// Concurrent burst test
async function runBurstTest() {
  console.log(`\n=== Burst Test: ${CONCURRENCY} concurrent requests ===`);
  resetStats();
  const startTime = Date.now();

  // Create batches of concurrent requests
  const batchSize = CONCURRENCY;
  const batches = Math.ceil(REQUESTS_PER_BATCH / batchSize);

  for (let b = 0; b < batches; b++) {
    const promises = [];
    const remaining = REQUESTS_PER_BATCH - (b * batchSize);
    const currentBatchSize = Math.min(batchSize, remaining);

    for (let i = 0; i < currentBatchSize; i++) {
      const endpoint = Math.random() > 0.7 ? '/users' : '/';
      promises.push(makeRequest(endpoint));
    }

    const results = await Promise.all(promises);
    results.forEach(result => {
      stats.totalRequests++;
      if (result.status === 200) {
        stats.successful++;
        stats.latencies.push(result.latency);
      } else {
        stats.failed++;
      }
    });

    process.stdout.write(`\r  Progress: ${Math.min((b + 1) * batchSize, REQUESTS_PER_BATCH)}/${REQUESTS_PER_BATCH}`);
  }

  const duration = (Date.now() - startTime) / 1000;
  printStats('Burst', duration);
}

// Mixed workload test
async function runMixedTest() {
  console.log('\n=== Mixed Workload Test ===');
  resetStats();
  const startTime = Date.now();

  const endpoints = [
    { path: '/', weight: 40 },
    { path: '/users', weight: 30 },
    { path: '/users/1', weight: 15 },
    { path: '/health', weight: 10 },
    { path: '/compute/1000', weight: 5 }
  ];

  const promises = [];
  for (let i = 0; i < REQUESTS_PER_BATCH; i++) {
    // Weighted random endpoint selection
    const rand = Math.random() * 100;
    let cumulative = 0;
    let selected = endpoints[0];
    for (const ep of endpoints) {
      cumulative += ep.weight;
      if (rand <= cumulative) {
        selected = ep;
        break;
      }
    }

    promises.push(
      makeRequest(selected.path).then(result => {
        stats.totalRequests++;
        if (result.status === 200) {
          stats.successful++;
          stats.latencies.push(result.latency);
        } else {
          stats.failed++;
        }
      })
    );

    if ((i + 1) % 50 === 0) {
      process.stdout.write(`\r  Progress: ${i + 1}/${REQUESTS_PER_BATCH}`);
    }
  }

  await Promise.all(promises);
  const duration = (Date.now() - startTime) / 1000;
  printStats('Mixed Workload', duration);
}

// POST body test
async function runPostTest() {
  console.log('\n=== POST with Body Test ===');
  resetStats();
  const startTime = Date.now();

  const promises = [];
  for (let i = 0; i < Math.min(REQUESTS_PER_BATCH, 500); i++) {
    const body = JSON.stringify({
      name: `User${i}`,
      email: `user${i}@example.com`
    });

    promises.push(
      makeRequest('/users', 'POST', body).then(result => {
        stats.totalRequests++;
        if (result.status === 201) {
          stats.successful++;
          stats.latencies.push(result.latency);
        } else {
          stats.failed++;
        }
      })
    );

    if ((i + 1) % 50 === 0) {
      process.stdout.write(`\r  Progress: ${i + 1}/${Math.min(REQUESTS_PER_BATCH, 500)}`);
    }
  }

  await Promise.all(promises);
  const duration = (Date.now() - startTime) / 1000;
  printStats('POST with Body', duration);
}

// Run all tests
async function runAllTests() {
  console.log('='.repeat(60));
  console.log('Express-Pro Stress Test');
  console.log('Platform:', expresspro.platform);
  console.log('Backend:', expresspro.backend);
  console.log('Configuration:');
  console.log(`  - Port: ${PORT}`);
  console.log(`  - Concurrency: ${CONCURRENCY}`);
  console.log(`  - Requests per test: ${REQUESTS_PER_BATCH}`);
  console.log('='.repeat(60));

  // Warm up
  console.log('\nWarming up server...');
  for (let i = 0; i < 20; i++) {
    await makeRequest('/');
  }

  await runSequentialTest();
  await new Promise(r => setTimeout(r, 500));

  await runBurstTest();
  await new Promise(r => setTimeout(r, 500));

  await runMixedTest();
  await new Promise(r => setTimeout(r, 500));

  await runPostTest();

  console.log('\n' + '='.repeat(60));
  console.log('All stress tests complete!');
  console.log('='.repeat(60));

  // Cleanup
  agent.destroy();
  app.close(() => {
    process.exit(0);
  });
}

// Start server
app.listen(PORT, () => {
  console.log(`Stress Test Server running on http://localhost:${PORT}`);
  console.log('\nTest endpoints:');
  console.log('  GET  /          - Simple response');
  console.log('  GET  /health    - Health check');
  console.log('  GET  /users     - List users');
  console.log('  GET  /users/:id - Get user');
  console.log('  POST /users     - Create user');
  console.log('  POST /echo      - Echo request');
  console.log('\nRunning stress tests in 2 seconds...');
  console.log('(Set CONCURRENCY and REQUESTS env vars to customize)');

  setTimeout(runAllTests, 2000);
});

// Handle graceful shutdown
process.on('SIGINT', () => {
  console.log('\nShutting down...');
  agent.destroy();
  app.close(() => {
    process.exit(0);
  });
});

// Handle errors
process.on('uncaughtException', (err) => {
  console.error('Uncaught error:', err);
  agent.destroy();
  app.close(() => {
    process.exit(1);
  });
});
