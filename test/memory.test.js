/**
 * maxpress Memory Leak Tests
 *
 * Tests for memory stability under load.
 *
 * Run: node --test test/memory.test.js
 */

'use strict';

const { describe, it, before, after } = require('node:test');
const assert = require('node:assert');
const http = require('http');
const maxpress = require('../js/index.js');

const TEST_PORT = 3462;
const TEST_URL = `http://localhost:${TEST_PORT}`;

// Helper to make HTTP requests
function makeRequest(path) {
  return new Promise((resolve, reject) => {
    const req = http.get(`${TEST_URL}${path}`, (res) => {
      let data = '';
      res.on('data', chunk => data += chunk);
      res.on('end', () => resolve({ status: res.statusCode, body: data }));
    });
    req.on('error', reject);
    req.setTimeout(5000, () => {
      req.destroy();
      reject(new Error('Timeout'));
    });
  });
}

// Get memory usage in MB
function getMemoryMB() {
  const usage = process.memoryUsage();
  return {
    heapUsed: usage.heapUsed / 1024 / 1024,
    heapTotal: usage.heapTotal / 1024 / 1024,
    rss: usage.rss / 1024 / 1024,
    external: usage.external / 1024 / 1024
  };
}

describe('Memory Leak Tests', () => {
  let app;
  let server;

  before(async () => {
    app = maxpress();

    // Various endpoints for testing
    app.get('/health', (req, res) => {
      res.json({ status: 'ok' });
    });

    app.post('/echo', (req, res) => {
      res.json({ received: req.body });
    });

    app.get('/data/:id', (req, res) => {
      res.json({ id: req.params.id, data: 'x'.repeat(100) });
    });

    server = app.listen(TEST_PORT, () => {});
    await new Promise(resolve => setTimeout(resolve, 100));

    // Force GC if available (needs --expose-gc flag)
    if (global.gc) {
      global.gc();
      await new Promise(resolve => setTimeout(resolve, 100));
    }
  });

  after(async () => {
    if (app) {
      await new Promise((resolve) => {
        app.close(() => resolve());
      });
    }
  });

  describe('Heap stability under load', () => {
    it('should not leak memory over 1,000 requests', { timeout: 60000 }, async () => {

      const initialMemory = getMemoryMB();
      console.log('\n  Initial memory:', initialMemory);

      // Number of requests (reduced for CI speed)
      const requestCount = 1000;
      const batchSize = 100;
      const batches = Math.ceil(requestCount / batchSize);

      console.log(`  Sending ${requestCount} requests in ${batches} batches...`);

      let completed = 0;
      for (let batch = 0; batch < batches; batch++) {
        const promises = [];
        for (let i = 0; i < batchSize && completed < requestCount; i++) {
          promises.push(makeRequest('/health').catch(() => null));
          completed++;
        }
        await Promise.all(promises);

        // Progress indicator every 20%
        if ((batch + 1) % Math.ceil(batches / 5) === 0) {
          process.stdout.write('.');
        }

        // Small delay between batches to prevent overwhelming
        if (batch < batches - 1) {
          await new Promise(resolve => setTimeout(resolve, 5));
        }
      }
      console.log('');

      // Allow time for GC
      if (global.gc) {
        global.gc();
        await new Promise(resolve => setTimeout(resolve, 500));
      }

      const finalMemory = getMemoryMB();
      console.log('  Final memory:', finalMemory);

      const heapGrowth = finalMemory.heapUsed - initialMemory.heapUsed;
      const rssGrowth = finalMemory.rss - initialMemory.rss;

      console.log(`  Heap growth: ${heapGrowth.toFixed(2)} MB`);
      console.log(`  RSS growth: ${rssGrowth.toFixed(2)} MB`);

      // Assert heap growth is reasonable (< 8MB accounts for V8 variance + fast path cache)
      // In CI, allow slightly more variance
      const maxGrowth = process.env.CI ? 12 : 8;
      assert.ok(
        heapGrowth < maxGrowth,
        `Heap grew by ${heapGrowth.toFixed(2)}MB, expected < ${maxGrowth}MB`
      );
    });

    it('should handle repeated large allocations', async () => {
      const initialMemory = getMemoryMB();

      // Make requests that return data
      for (let i = 0; i < 1000; i++) {
        await makeRequest(`/data/${i}`);
      }

      if (global.gc) {
        global.gc();
        await new Promise(resolve => setTimeout(resolve, 100));
      }

      const finalMemory = getMemoryMB();
      const heapGrowth = finalMemory.heapUsed - initialMemory.heapUsed;

      console.log(`  Large alloc test - Heap growth: ${heapGrowth.toFixed(2)} MB`);
      // Allow up to 10MB for V8 variance and test data
      assert.ok(heapGrowth < 10, `Heap grew by ${heapGrowth.toFixed(2)}MB after large allocations`);
    });
  });

  describe('Native module memory', () => {
    it('should not crash on rapid connection open/close', async () => {
      // Rapidly open and close connections
      for (let i = 0; i < 100; i++) {
        try {
          await makeRequest('/health');
        } catch (err) {
          // Some failures are acceptable
        }
      }

      // Server should still be responsive
      const response = await makeRequest('/health');
      assert.strictEqual(response.status, 200);
    });
  });
});
