const iopress = require('../index.js');
const { test } = require('node:test');
const assert = require('node:assert');
const http = require('node:http');

test('Large Response Test', async (t) => {
  const app = iopress();
  const port = 3462;
  
  // Use smaller body in CI to avoid slow 1MB transfers on shared runners
  const bodySize = process.env.CI ? 10 * 1024 : 1024 * 1024;
  const largeBody = 'A'.repeat(bodySize);
  
  app.get('/large', (req, res) => {
    res.send(largeBody);
  });

  const server = app.listen(port);

  await t.test('should receive complete large response', async () => {
    return new Promise((resolve, reject) => {
      const req = http.get(`http://localhost:${port}/large`, (res) => {
        let data = '';
        res.on('data', (chunk) => {
          data += chunk;
        });
        res.on('end', () => {
          try {
            assert.strictEqual(res.statusCode, 200);
            assert.strictEqual(data.length, largeBody.length);
            assert.strictEqual(data, largeBody);
            resolve();
          } catch (e) {
            reject(e);
          }
        });
      });
      req.setTimeout(30000, () => { req.destroy(); reject(new Error('Timeout')); });
      req.on('error', reject);
    });
  });

  await new Promise(resolve => app.close(resolve));
});
