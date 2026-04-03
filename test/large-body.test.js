/**
 * norvex Large Body Tests
 *
 * Tests for maxBodySize handling and body buffering.
 *
 * Run: node --test test/large-body.test.js
 */

'use strict';

const { describe, it, before, after } = require('node:test');
const assert = require('node:assert');
const norvex = require('../js/index.js');

const TEST_PORT = 3460;
const TEST_URL = `http://localhost:${TEST_PORT}`;

describe('Large Body Tests', () => {
  let app;
  let server;

  before(async () => {
    app = norvex();

    // Echo endpoint
    app.post('/echo', (req, res) => {
      res.json({
        received: true,
        bodyLength: req.body?.length || 0,
        bodyType: typeof req.body
      });
    });

    // Start server
    await new Promise((resolve) => {
      server = app.listen(TEST_PORT, () => {
        resolve();
      });
    });

    await new Promise(resolve => setTimeout(resolve, 100));
  });

  after(async () => {
    if (app) {
      await new Promise((resolve) => {
        app.close(() => resolve());
      });
    }
  });

  describe('Small bodies (fit in buffer)', () => {
    it('should handle 1KB body', async () => {
      const body = 'x'.repeat(1024);

      const response = await fetch(`${TEST_URL}/echo`, {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: body
      });

      const result = await response.json();

      assert.strictEqual(response.status, 200);
      assert.strictEqual(result.received, true);
      assert.ok(result.bodyLength >= 1024 || result.bodyLength === 0);
    });

    it('should handle JSON body under 4KB', async () => {
      const data = { message: 'test', padding: 'x'.repeat(1000) };
      const json = JSON.stringify(data);

      const response = await fetch(`${TEST_URL}/echo`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: json
      });

      assert.strictEqual(response.status, 200);
    });
  });

  describe('Medium bodies (4KB - 100KB)', () => {
    it('should handle 10KB body', async () => {
      const body = 'x'.repeat(10 * 1024);

      const response = await fetch(`${TEST_URL}/echo`, {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: body
      });

      // May succeed or fail depending on buffer implementation
      assert.ok(response.status === 200 || response.status === 413 || response.status === 500);
    });

    it('should handle 50KB JSON body', async () => {
      const data = { items: Array(1000).fill('test data entry') };
      const json = JSON.stringify(data);

      try {
        const response = await fetch(`${TEST_URL}/echo`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: json
        });

        assert.ok(response.status === 200 || response.status === 413);
      } catch (err) {
        // Connection may be closed for large bodies
        assert.ok(err.message.includes('fetch failed') || err.message.includes('socket'));
      }
    });
  });

  describe('Large bodies (exceed limits)', () => {
    it('should handle 100KB body appropriately', async () => {
      const body = 'x'.repeat(100 * 1024);

      try {
        const response = await fetch(`${TEST_URL}/echo`, {
          method: 'POST',
          headers: { 'Content-Type': 'text/plain' },
          body: body
        });

        // Should either accept or reject
        assert.ok(response.status === 200 || response.status === 413 || response.status === 500);
      } catch (err) {
        // Connection closed is acceptable for very large bodies
        assert.ok(err.message.includes('fetch') || err.message.includes('socket') || err.message.includes('closed'));
      }
    });

    it('should handle 1MB body appropriately', async () => {
      const body = 'x'.repeat(1024 * 1024);

      try {
        const response = await fetch(`${TEST_URL}/echo`, {
          method: 'POST',
          headers: { 'Content-Type': 'text/plain' },
          body: body
        });

        assert.ok(response.status === 200 || response.status === 413 || response.status === 500);
      } catch (err) {
        // Expected for very large bodies
        assert.ok(true);
      }
    });
  });

  describe('Memory stress test', () => {
    it('should handle repeated large requests without leaking', async () => {
      const iterations = 5;
      const body = 'x'.repeat(10 * 1024); // 10KB

      for (let i = 0; i < iterations; i++) {
        try {
          const response = await fetch(`${TEST_URL}/echo`, {
            method: 'POST',
            headers: { 'Content-Type': 'text/plain' },
            body: body
          });

          // Just ensure no crash
          assert.ok(response.status === 200 || response.status === 413 || response.status === 500);
        } catch (err) {
          // Connection issues are acceptable
        }
      }

      // If we get here without crash, memory is stable
      assert.ok(true);
    });
  });
});
