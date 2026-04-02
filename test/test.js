const { describe, it } = require('node:test');
const assert = require('node:assert');
const expressPro = require('../index.js');

describe('express-pro', () => {
  it('should export version', () => {
    assert.strictEqual(typeof expressPro.version, 'string');
  });

  it('should export platform', () => {
    assert.strictEqual(typeof expressPro.platform, 'string');
    assert.ok(['linux', 'macos', 'windows', 'unknown'].includes(expressPro.platform));
  });

  it('should export backend', () => {
    assert.strictEqual(typeof expressPro.backend, 'string');
    assert.ok(['io_uring', 'kqueue', 'iocp', 'epoll', 'libuv'].includes(expressPro.backend));
  });

  it('should create server', () => {
    const server = expressPro.createServer();
    assert.strictEqual(typeof server, 'object');
    assert.strictEqual(typeof server.port, 'number');
    assert.strictEqual(typeof server.running, 'boolean');
    assert.strictEqual(typeof server.backend, 'string');
  });
});
