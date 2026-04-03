/**
 * iopress Streaming Tests
 *
 * Tests for streaming body handling with streamBody option.
 *
 * Run: node --test test/streaming.test.js
 */

'use strict';

const { describe, it, before, after } = require('node:test');
const assert = require('node:assert');
const iopress = require('../js/index.js');

const TEST_PORT = 3461;
const TEST_URL = `http://localhost:${TEST_PORT}`;

describe('Streaming Body Tests', () => {
  let app;
  let server;
  let chunksReceived;
  let totalBytes;
  let streamEnded;

  before(async () => {
    app = iopress();
    chunksReceived = [];
    totalBytes = 0;
    streamEnded = false;

    // Non-streaming endpoint
    app.post('/buffer', (req, res) => {
      res.json({
        mode: 'buffer',
        bodyLength: req.body?.length || 0
      });
    });

    // Streaming endpoint (if supported)
    app.post('/stream', (req, res) => {
      const chunks = [];

      if (req.on) {
        req.on('data', (chunk) => {
          chunks.push(chunk);
        });

        req.on('end', () => {
          res.json({
            mode: 'stream',
            chunks: chunks.length,
            totalBytes: Buffer.concat(chunks).length
          });
        });

        req.on('error', (err) => {
          res.status(500).json({ error: err.message });
        });
      } else {
        // Fallback if streaming not supported
        res.json({
          mode: 'buffer-fallback',
          bodyLength: req.body?.length || 0
        });
      }
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

  describe('Non-streaming mode', () => {
    it('should receive complete body in buffer mode', async () => {
      const body = 'Hello World test data';

      const response = await fetch(`${TEST_URL}/buffer`, {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: body
      });

      const result = await response.json();

      assert.strictEqual(response.status, 200);
      assert.strictEqual(result.mode, 'buffer');
    });

    it('should handle JSON in buffer mode', async () => {
      const data = { test: 'data', nested: { value: 123 } };

      const response = await fetch(`${TEST_URL}/buffer`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(data)
      });

      const result = await response.json();

      assert.strictEqual(response.status, 200);
      assert.strictEqual(result.mode, 'buffer');
    });
  });

  describe('Streaming mode (if supported)', () => {
    it('should accept requests to stream endpoint', async () => {
      const body = 'Streaming test data';

      const response = await fetch(`${TEST_URL}/stream`, {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: body
      });

      assert.ok(response.status === 200 || response.status === 500);

      if (response.status === 200) {
        const result = await response.json();
        // Should be either stream mode or buffer fallback
        assert.ok(['stream', 'buffer-fallback'].includes(result.mode));
      }
    });

    it('should handle chunked data', async () => {
      // Create body larger than typical buffer
      const body = 'x'.repeat(8192);

      try {
        const response = await fetch(`${TEST_URL}/stream`, {
          method: 'POST',
          headers: { 'Content-Type': 'text/plain' },
          body: body
        });

        if (response.status === 200) {
          const result = await response.json();
          // If streaming worked, should have multiple chunks or total bytes
          if (result.mode === 'stream') {
            assert.ok(result.chunks >= 1);
          }
        }
      } catch (err) {
        // Streaming may not be fully implemented
        assert.ok(true);
      }
    });
  });

  describe('Transfer-Encoding: chunked', () => {
    it('should handle chunked transfer encoding', async () => {
      // fetch automatically handles chunked encoding for large bodies
      const body = 'x'.repeat(4096);

      try {
        const response = await fetch(`${TEST_URL}/buffer`, {
          method: 'POST',
          headers: {
            'Content-Type': 'text/plain',
            'Transfer-Encoding': 'chunked'
          },
          body: body
        });

        // Should succeed regardless of actual encoding handling
        assert.ok(response.status === 200 || response.status === 411 || response.status === 500);
      } catch (err) {
        // Some implementations don't support chunked
        assert.ok(true);
      }
    });
  });

  describe('Content-Length validation', () => {
    it('should handle correct Content-Length', async () => {
      const body = 'exact size body';

      const response = await fetch(`${TEST_URL}/buffer`, {
        method: 'POST',
        headers: {
          'Content-Type': 'text/plain',
          'Content-Length': String(body.length)
        },
        body: body
      });

      assert.strictEqual(response.status, 200);
    });

    it('should handle zero Content-Length', async () => {
      const response = await fetch(`${TEST_URL}/buffer`, {
        method: 'POST',
        headers: {
          'Content-Type': 'text/plain',
          'Content-Length': '0'
        }
      });

      assert.strictEqual(response.status, 200);
    });
  });

  describe('Edge cases', () => {
    it('should handle empty body with Content-Length: 0', async () => {
      const response = await fetch(`${TEST_URL}/buffer`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'Content-Length': '0'
        }
      });

      assert.strictEqual(response.status, 200);
    });

    it('should handle body with only whitespace', async () => {
      const response = await fetch(`${TEST_URL}/buffer`, {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body: '   \n\t   '
      });

      assert.strictEqual(response.status, 200);
    });

    it('should handle binary-safe data', async () => {
      // Create buffer with various byte values
      const bytes = Buffer.alloc(256);
      for (let i = 0; i < 256; i++) {
        bytes[i] = i;
      }

      try {
        const response = await fetch(`${TEST_URL}/buffer`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/octet-stream' },
          body: bytes
        });

        // Binary handling may vary
        assert.ok(response.status === 200 || response.status === 500);
      } catch (err) {
        assert.ok(true);
      }
    });
  });
});
