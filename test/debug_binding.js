/**
 * Debug test for native binding
 */

const native = require('../build/Release/express_pro_native');
const http = require('http');

console.log('Native module loaded:');
console.log('  version:', native.version);
console.log('  platform:', native.platform);
console.log('  functions:', Object.keys(native).filter(k => typeof native[k] === 'function'));

// Start server with a simple handler
console.log('Starting server...');
const server = native.Listen(3457, { initialBufferSize: 4096, maxBodySize: 1048576 }, (req, _res) => {
  console.log('Request received:', req.method, req.path);
  console.log('  fd:', req.fd);

  // Try to send response
  native.SendResponse(req.fd, 200, 'Hello World');
  console.log('Response sent');
});

console.log('Server started on port', server.port);

// Wait and make a request
setTimeout(() => {
  console.log('Making request...');

  const req = http.get('http://localhost:3457/test', (res) => {
    let body = '';
    res.on('data', chunk => body += chunk);
    res.on('end', () => {
      console.log('Response received:', res.statusCode, body);
      process.exit(0);
    });
  });

  req.on('error', (err) => {
    console.error('Request error:', err.message);
    process.exit(1);
  });

  req.setTimeout(5000, () => {
    console.error('Request timeout');
    process.exit(1);
  });
}, 500);
