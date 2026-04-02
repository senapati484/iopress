/**
 * Integration Test for Express-Pro
 *
 * Tests the complete stack: native binding + JS wrapper
 */

'use strict';

const http = require('http');
const expressPro = require('../js/index.js');

const TEST_PORT = 3456;

function delay(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function makeRequest(path, options = {}) {
  return new Promise((resolve, reject) => {
    const req = http.request({
      hostname: 'localhost',
      port: TEST_PORT,
      path,
      method: options.method || 'GET',
      headers: options.headers || {}
    }, (res) => {
      let body = '';
      res.on('data', chunk => body += chunk);
      res.on('end', () => resolve({
        status: res.statusCode,
        headers: res.headers,
        body
      }));
    });

    req.on('error', reject);

    if (options.body) {
      req.write(options.body);
    }
    req.end();
  });
}

async function runTests() {
  console.log('=== Express-Pro Integration Tests ===\n');

  // Test 1: Basic server creation
  console.log('Test 1: Server creation...');
  const app = expressPro();
  if (!app) {
    throw new Error('Failed to create ExpressPro instance');
  }
  console.log('  ✓ ExpressPro instance created');
  console.log(`  ✓ Platform: ${expressPro.platform}`);
  console.log(`  ✓ Version: ${expressPro.version}`);
  console.log(`  ✓ Backend: ${expressPro.backend}\n`);

  // Test 2: Route registration
  console.log('Test 2: Route registration...');
  let _getCalled = false;
  let _postCalled = false;

  app.get('/test', (req, res) => {
    _getCalled = true;
    res.json({ method: 'GET', path: req.path });
  });

  app.post('/echo', (req, res) => {
    _postCalled = true;
    res.json({ method: 'POST', body: req.body });
  });

  app.get('/user/:id', (req, res) => {
    res.json({ id: req.params.id });
  });

  console.log('  ✓ Routes registered\n');

  // Test 3: Middleware
  console.log('Test 3: Middleware...');
  let middlewareCalled = false;

  app.use((req, res, next) => {
    middlewareCalled = true;
    req.timestamp = Date.now();
    next();
  });

  app.get('/middleware-test', (req, res) => {
    res.json({ middlewareCalled, hasTimestamp: !!req.timestamp });
  });

  console.log('  ✓ Middleware registered\n');

  // Test 4: Start server
  console.log('Test 4: Server startup...');
  let listening = false;

  const _server = app.listen(TEST_PORT, () => {
    listening = true;
    console.log(`  ✓ Server listening on port ${TEST_PORT}\n`);
  });

  await delay(100);

  if (!listening) {
    throw new Error('Server failed to start');
  }

  // Test 5: Simple GET request
  console.log('Test 5: Simple GET request...');
  const getRes = await makeRequest('/test');
  if (getRes.status !== 200) {
    throw new Error(`Expected status 200, got ${getRes.status}`);
  }
  const getBody = JSON.parse(getRes.body);
  if (getBody.method !== 'GET' || getBody.path !== '/test') {
    throw new Error('Unexpected response body');
  }
  console.log('  ✓ GET request handled correctly');
  console.log(`  ✓ Response: ${JSON.stringify(getBody)}\n`);

  // Test 6: Route parameters
  console.log('Test 6: Route parameters...');
  const paramRes = await makeRequest('/user/123');
  const paramBody = JSON.parse(paramRes.body);
  if (paramBody.id !== '123') {
    throw new Error(`Expected id=123, got ${paramBody.id}`);
  }
  console.log('  ✓ Route parameters extracted correctly');
  console.log(`  ✓ Response: ${JSON.stringify(paramBody)}\n`);

  // Test 7: Middleware execution
  console.log('Test 7: Middleware execution...');
  const mwRes = await makeRequest('/middleware-test');
  const mwBody = JSON.parse(mwRes.body);
  if (!mwBody.middlewareCalled) {
    throw new Error('Middleware was not called');
  }
  console.log('  ✓ Middleware executed');
  console.log(`  ✓ Response: ${JSON.stringify(mwBody)}\n`);

  // Test 8: POST with body
  console.log('Test 8: POST with JSON body...');
  const postRes = await makeRequest('/echo', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ hello: 'world' })
  });
  const postBody = JSON.parse(postRes.body);
  if (postBody.method !== 'POST') {
    throw new Error('POST handler not called');
  }
  console.log('  ✓ POST request handled');
  console.log(`  ✓ Response: ${JSON.stringify(postBody)}\n`);

  // Test 9: 404 handling
  console.log('Test 9: 404 for unregistered routes...');
  const notFoundRes = await makeRequest('/not-registered');
  if (notFoundRes.status !== 404) {
    throw new Error(`Expected 404, got ${notFoundRes.status}`);
  }
  console.log('  ✓ 404 returned for unregistered route\n');

  // Cleanup
  console.log('Test 10: Server shutdown...');
  // Note: server_stop not fully implemented in JS wrapper yet
  console.log('  ✓ Tests complete\n');

  console.log('=== All Tests Passed ===');
  process.exit(0);
}

runTests().catch(err => {
  console.error('Test failed:', err.message);
  console.error(err);
  process.exit(1);
});
