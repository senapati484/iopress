/**
 * Example with Default Routes Enabled
 *
 * This example shows how to opt-in to the built-in demo routes.
 * By default, iopress does NOT register default routes.
 * Pass { enableDefaultRoutes: true } to the constructor to enable them.
 *
 * Default routes include:
 * - GET /health
 * - GET /ping
 * - GET /users
 * - POST /echo
 * - GET /search
 * - GET /
 *
 * Run: node examples/basic-with-defaults.js
 *
 * You should see:
 * RegisterFastRoute: GET /health -> result=0
 * RegisterFastRoute: GET /ping -> result=0
 * RegisterFastRoute: GET /users -> result=0
 * RegisterFastRoute: POST /echo -> result=0
 * RegisterFastRoute: GET /search -> result=0
 * Server running on http://localhost:3000
 */

'use strict';

const iopress = require('../index.js');

// Create application with default routes ENABLED
const app = iopress({ enableDefaultRoutes: true });

console.log('iopress v' + iopress.version);
console.log('Platform:', iopress.platform);
console.log('Backend:', iopress.backend);
console.log('\nNotice: You will see RegisterFastRoute messages for the default routes');

// You can still override default routes with your own handlers
app.get('/', (req, res) => {
  res.json({
    message: 'Custom root handler (overriding default)',
    timestamp: Date.now(),
    platform: iopress.platform,
    backend: iopress.backend
  });
});

const PORT = parseInt(process.env.PORT, 10) || 3000;

app.listen(PORT, () => {
  console.log(`\nServer running on http://localhost:${PORT}`);
  console.log('\nTest the default routes:');
  console.log(`  curl http://localhost:${PORT}/health`);
  console.log(`  curl http://localhost:${PORT}/ping`);
  console.log(`  curl http://localhost:${PORT}/users`);
  console.log(`  curl -X POST http://localhost:${PORT}/echo`);
  console.log(`  curl http://localhost:${PORT}/search`);
  console.log(`  curl http://localhost:${PORT}/`);
  console.log('\nPress Ctrl+C to stop');
});