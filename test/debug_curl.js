/**
 * Debug test using curl
 */

const native = require('../build/Release/express_pro_native');
const { exec } = require('child_process');

console.log('Starting server on port 3458...');

const server = native.Listen(3458, { initialBufferSize: 4096, maxBodySize: 1048576 }, (req, _res) => {
  try {
    console.log('Request received:', req);
    console.log('req.method:', req && req.method);
    console.log('req.path:', req && req.path);
    console.log('req.fd:', req && req.fd);
    native.SendResponse(req.fd, 200, 'Hello World');
  } catch(e) {
    console.error('Error in handler:', e);
  }
});

console.log('Server started on port', server.port);

// Wait a bit then use curl
setTimeout(() => {
  console.log('Making curl request...');
  exec('curl -s http://localhost:3458/test', (error, stdout, stderr) => {
    console.log('Curl stdout:', stdout);
    console.log('Curl stderr:', stderr);
    console.log('Curl error:', error);
    process.exit(0);
  });
}, 500);
