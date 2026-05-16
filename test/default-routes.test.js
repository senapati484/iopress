/**
 * Integration test for default routes control feature
 * 
 * Tests that:
 * 1. Default routes are disabled by default (enableDefaultRoutes: false)
 * 2. Default routes can be enabled with enableDefaultRoutes: true
 * 3. User-defined routes override default routes
 */

'use strict';

const assert = require('assert');
const iopress = require('../index.js');
const http = require('http');

const PORT_BASE = 8100;
let portCounter = PORT_BASE;

function makeRequest(port, path, method = 'GET') {
  return new Promise((resolve, reject) => {
    const options = {
      hostname: 'localhost',
      port: port,
      path: path,
      method: method,
      timeout: 2000
    };

    const req = http.request(options, (res) => {
      let data = '';
      res.on('data', (chunk) => { data += chunk; });
      res.on('end', () => {
        resolve({ status: res.statusCode, data: data });
      });
    });

    req.on('error', reject);
    req.on('timeout', () => {
      req.destroy();
      reject(new Error('Request timeout'));
    });
    req.end();
  });
}

async function delay(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

async function runTests() {
  console.log('\n=== Testing Default Routes Control Feature ===\n');

  try {
    // TEST 1: Default routes disabled (default behavior)
    console.log('Test 1: Default routes disabled (enableDefaultRoutes: false)');
    const port1 = portCounter++;
    const app1 = iopress({ enableDefaultRoutes: false });
    
    app1.get('/', (req, res) => {
      res.send('Hello from user');
    });

    app1.listen(port1);
    await delay(500);

    // User route should work
    const res1a = await makeRequest(port1, '/');
    assert.strictEqual(res1a.status, 200, 'User route / should return 200');
    assert(res1a.data.includes('Hello'), 'User route should return user content');
    console.log('  ✓ User route / works');

    // Default routes should NOT work (should return 404)
    const res1b = await makeRequest(port1, '/health');
    assert.strictEqual(res1b.status, 404, 'Default route /health should return 404 when disabled');
    console.log('  ✓ Default route /health returns 404 (as expected)');

    const res1c = await makeRequest(port1, '/ping');
    assert.strictEqual(res1c.status, 404, 'Default route /ping should return 404 when disabled');
    console.log('  ✓ Default route /ping returns 404 (as expected)');

    // TEST 2: Default routes enabled
    console.log('\nTest 2: Default routes enabled (enableDefaultRoutes: true)');
    const port2 = portCounter++;
    const app2 = iopress({ enableDefaultRoutes: true });
    
    app2.get('/', (req, res) => {
      res.send('Hello from user');
    });

    app2.listen(port2);
    await delay(500);

    // User route should work
    const res2a = await makeRequest(port2, '/');
    assert.strictEqual(res2a.status, 200, 'User route / should return 200');
    assert(res2a.data.includes('Hello'), 'User route should return user content');
    console.log('  ✓ User route / works');

    // Default routes should work
    const res2b = await makeRequest(port2, '/health');
    assert.strictEqual(res2b.status, 200, 'Default route /health should return 200 when enabled');
    console.log('  ✓ Default route /health works');

    const res2c = await makeRequest(port2, '/ping');
    assert.strictEqual(res2c.status, 200, 'Default route /ping should return 200 when enabled');
    assert(res2c.data.includes('pong'), 'Default route /ping should return pong');
    console.log('  ✓ Default route /ping works');

    // TEST 3: User route overrides default route
    console.log('\nTest 3: User-defined route overrides default route');
    const port3 = portCounter++;
    const app3 = iopress({ enableDefaultRoutes: true });
    
    // User defines /health with custom response
    app3.get('/health', (req, res) => {
      res.send('custom health check');
    });

    app3.listen(port3);
    await delay(500);

    // User's /health should be used instead of default
    const res3 = await makeRequest(port3, '/health');
    assert.strictEqual(res3.status, 200, 'User /health route should return 200');
    assert(res3.data.includes('custom'), 'User /health route should return custom content');
    console.log('  ✓ User route overrides default route');

    console.log('\n✅ All tests passed!\n');
    process.exit(0);

  } catch (err) {
    console.error('\n❌ Test failed:', err.message);
    console.error(err.stack);
    process.exit(1);
  }
}

runTests();
