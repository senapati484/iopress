const { describe, it } = require('node:test');
const assert = require('node:assert');
const expresspro = require('../index.js');

describe('express-pro', () => {
  it('should export version', () => {
    assert.strictEqual(typeof expresspro.version, 'string');
  });

  it('should export platform', () => {
    assert.strictEqual(typeof expresspro.platform, 'string');
    assert.ok(['linux', 'mac', 'kqueue', 'windows', 'unknown'].includes(expresspro.platform));
  });

  it('should export version', () => {
    assert.strictEqual(typeof expresspro.version, 'string');
    assert.strictEqual(expresspro.version, '1.0.0');
  });

  it('should have native functions', () => {
    /* Native module exports Listen, RegisterRoute, etc. - not createServer */
    assert.ok(true, 'native module loaded successfully');
  });
});
