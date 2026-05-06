const iopress = require('../index.js');
const { test } = require('node:test');
const assert = require('node:assert');
const http = require('node:http');

test('Large Response Test', async (t) => {
  const app = iopress();
  const port = 3462;
  
  // Create a large body (1MB)
  const largeBody = 'A'.repeat(1024 * 1024);
  
  app.get('/large', (req, res) => {
    res.send(largeBody);
  });

  const server = app.listen(port);

  await t.test('should receive complete 1MB response', async () => {
    return new Promise((resolve, reject) => {
      http.get(`http://localhost:${port}/large`, (res) => {
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
      }).on('error', reject);
    });
  });

  await new Promise(resolve => app.close(resolve));
});
