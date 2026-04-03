/**
 * Basic Example - norvex Hello World
 *
 * Demonstrates:
 * - Creating an norvex application
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

const norvex = require('../index.js');

// Create application
const app = norvex();

console.log('norvex v' + norvex.version);
console.log('Platform:', norvex.platform);
console.log('Backend:', norvex.backend);

// GET /
app.get('/', (req, res) => {
  res.json({
    message: 'Hello from norvex!',
    timestamp: Date.now(),
    platform: norvex.platform,
    backend: norvex.backend
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
    version: norvex.version,
    platform: norvex.platform,
    backend: norvex.backend,
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
