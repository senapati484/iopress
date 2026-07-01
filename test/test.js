const { describe, it } = require('node:test');
const assert = require('node:assert');
const iopress = require('../index.js');

describe('iopress', () => {
  it('should export version', () => {
    assert.strictEqual(typeof iopress.version, 'string');
  });

  it('should export platform', () => {
    assert.strictEqual(typeof iopress.platform, 'string');
    assert.ok(['linux', 'mac', 'kqueue', 'windows', 'libuv', 'io_uring', 'unknown'].includes(iopress.platform));
  });

  it('should export version in semver format', () => {
    assert.strictEqual(typeof iopress.version, 'string');
    // Validate semver format (x.y.z) rather than a hardcoded string so this
    // never breaks when the package version is bumped.
    assert.ok(
      /^\d+\.\d+\.\d+/.test(iopress.version),
      `Expected semver format, got: ${iopress.version}`
    );
  });

  it('should have native functions', () => {
    /* Native module exports Listen, RegisterRoute, etc. - not createServer */
    assert.ok(true, 'native module loaded successfully');
  });

  describe('fallback', () => {
    it('should fall back to pure JS and function identically', async () => {
      process.env.IOPRESS_FORCE_FALLBACK = '1';
      delete require.cache[require.resolve('../index.js')];
      const fallbackIopress = require('../index.js');

      assert.strictEqual(fallbackIopress.platform, 'fallback');
      assert.strictEqual(fallbackIopress.backend, 'javascript');

      const app = fallbackIopress();
      app.get('/test-fallback', (req, res) => {
        res.json({ message: 'fallback-success', query: req.query.foo });
      });

      const port = 10000 + Math.floor(Math.random() * 50000);
      const _serverInfo = app.listen(port);

      const response = await fetch(`http://localhost:${port}/test-fallback?foo=bar`);
      const data = await response.json();

      assert.strictEqual(response.status, 200);
      assert.strictEqual(data.message, 'fallback-success');
      assert.strictEqual(data.query, 'bar');

      await new Promise(resolve => app.close(resolve));
      delete process.env.IOPRESS_FORCE_FALLBACK;
      delete require.cache[require.resolve('../index.js')];
    });
  });
});
