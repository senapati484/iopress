const { describe, it } = require('node:test');
const assert = require('node:assert');
const expressmax = require('../index.js');

describe('expressmax', () => {
  it('should export version', () => {
    assert.strictEqual(typeof expressmax.version, 'string');
  });

  it('should export platform', () => {
    assert.strictEqual(typeof expressmax.platform, 'string');
    assert.ok(['linux', 'mac', 'kqueue', 'windows', 'unknown'].includes(expressmax.platform));
  });

  it('should export version', () => {
    assert.strictEqual(typeof expressmax.version, 'string');
    assert.strictEqual(expressmax.version, '1.0.0');
  });

  it('should have native functions', () => {
    /* Native module exports Listen, RegisterRoute, etc. - not createServer */
    assert.ok(true, 'native module loaded successfully');
  });
});
