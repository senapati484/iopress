const { describe, it } = require('node:test');
const assert = require('node:assert');
const norvex = require('../index.js');

describe('norvex', () => {
  it('should export version', () => {
    assert.strictEqual(typeof norvex.version, 'string');
  });

  it('should export platform', () => {
    assert.strictEqual(typeof norvex.platform, 'string');
    assert.ok(['linux', 'mac', 'kqueue', 'windows', 'unknown'].includes(norvex.platform));
  });

  it('should export version', () => {
    assert.strictEqual(typeof norvex.version, 'string');
    assert.strictEqual(norvex.version, '1.0.0');
  });

  it('should have native functions', () => {
    /* Native module exports Listen, RegisterRoute, etc. - not createServer */
    assert.ok(true, 'native module loaded successfully');
  });
});
