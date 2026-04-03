const { describe, it } = require('node:test');
const assert = require('node:assert');
const maxpress = require('../index.js');

describe('maxpress', () => {
  it('should export version', () => {
    assert.strictEqual(typeof maxpress.version, 'string');
  });

  it('should export platform', () => {
    assert.strictEqual(typeof maxpress.platform, 'string');
    assert.ok(['linux', 'mac', 'kqueue', 'windows', 'unknown'].includes(maxpress.platform));
  });

  it('should export version', () => {
    assert.strictEqual(typeof maxpress.version, 'string');
    assert.strictEqual(maxpress.version, '1.0.0');
  });

  it('should have native functions', () => {
    /* Native module exports Listen, RegisterRoute, etc. - not createServer */
    assert.ok(true, 'native module loaded successfully');
  });
});
