/**
 * Basic Example - Express-Pro Hello World
 *
 * Demonstrates:
 * - Creating an Express-Pro application
 * - Simple GET and POST routes
 * - JSON response handling
 * - Starting the server
 *
 * Run: node examples/basic.js
 * Requires: npm install && npm run build
 *
 * Test with curl:
 *   # GET request
 *   curl http://localhost:3000/
 *
 *   # POST request with JSON body
 *   curl -X POST http://localhost:3000/echo \
 *     -H "Content-Type: application/json" \
 *     -d '{"message":"hello"}'
 *
 *   # Health check
 *   curl http://localhost:3000/health
 *
 *   # Get server info
 *   curl http://localhost:3000/info
 */

'use strict';

const expresspro = require('../index.js');

// Create application
const app = expresspro();

console.log('Express-Pro v' + expresspro.version);
console.log('Platform:', expresspro.platform);
console.log('Backend:', expresspro.backend);

// GET /
app.get('/', (req, res) => {
  res.json({
    message: 'Hello from Express-Pro!',
    timestamp: Date.now(),
    platform: expresspro.platform,
    backend: expresspro.backend
  });
});

// GET /health - Health check endpoint
app.get('/health', (req, res) => {
  res.json({
    status: 'ok',
    uptime: process.uptime(),
    memory: process.memoryUsage()
  });
});

// GET /info - Server information
app.get('/info', (req, res) => {
  res.json({
    version: expresspro.version,
    platform: expresspro.platform,
    backend: expresspro.backend,
    nodeVersion: process.version
  });
});

// POST /echo - Echo back the request body
app.post('/echo', (req, res) => {
  res.json({
    method: req.method,
    path: req.path,
    headers: req.headers,
    body: req.body,
    query: req.query
  });
});

// Start server
const PORT = parseInt(process.env.PORT, 10) || 3000;

app.listen(PORT, () => {
  console.log(`\nServer running on http://localhost:${PORT}`);
  console.log('\nTest with:');
  console.log(`  curl http://localhost:${PORT}/`);
  console.log(`  curl http://localhost:${PORT}/health`);
  console.log(`  curl -X POST http://localhost:${PORT}/echo -H "Content-Type: application/json" -d '{"test":true}'`);
  console.log('\nPress Ctrl+C to stop');
});
