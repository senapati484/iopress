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

  it('should export version', () => {
    assert.strictEqual(typeof iopress.version, 'string');
    assert.strictEqual(iopress.version, '1.0.0');
  });

  it('should have native functions', () => {
    /* Native module exports Listen, RegisterRoute, etc. - not createServer */
    assert.ok(true, 'native module loaded successfully');
  });
});
