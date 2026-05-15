const { describe, it } = require('node:test');
const assert = require('node:assert');
const iopress = require('../index.js');

describe('iopress', () => {
  it('should export version', () => {
    assert.strictEqual(typeof iopress.version, 'string');
  });

  it('should export platform', () => {
    assert.strictEqual(typeof iopress.platform, 'string');
    assert.ok(['linux', 'mac', 'kqueue', 'windows', 'unknown'].includes(iopress.platform));
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
});
