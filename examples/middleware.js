/**
 * Middleware Example - Authentication, Logging, and Error Handling
 *
 * Demonstrates:
 * - Global middleware (logging, request ID)
 * - Path-specific middleware (API key auth)
 * - Middleware chaining with next()
 * - Error handling middleware
 * - Timing requests
 *
 * Run: node examples/middleware.js
 * Requires: npm install && npm run build
 *
 * Test with curl:
 *
 *   # Public route (no auth required)
 *   curl http://localhost:3000/
 *
 *   # Protected route without API key (should fail with 401)
 *   curl http://localhost:3000/api/protected
 *
 *   # Protected route with API key (should succeed)
 *   curl http://localhost:3000/api/protected \
 *     -H "X-API-Key: secret123"
 *
 *   # Simulate an error
 *   curl http://localhost:3000/error
 *
 *   # POST with body logging
 *   curl -X POST http://localhost:3000/api/data \
 *     -H "X-API-Key: secret123" \
 *     -H "Content-Type: application/json" \
 *     -d '{"foo":"bar"}'
 *
 *   # Slow endpoint to see timing
 *   curl http://localhost:3000/api/slow \
 *     -H "X-API-Key: secret123"
 */

'use strict';

const iopress = require('../index.js');

const app = iopress();

// Store for request IDs
const requestStore = new Map();

/**
 * Generate a unique request ID
 */
function generateRequestId() {
  return `${Date.now()}-${Math.random().toString(36).substr(2, 9)}`;
}

/**
 * Global middleware: Request ID and timing
 * This runs for every request
 */
app.use((req, res, next) => {
  // Attach request ID and start time
  const requestId = generateRequestId();
  req.requestId = requestId;
  req.startTime = Date.now();

  // Store in request store for later
  requestStore.set(requestId, {
    method: req.method,
    path: req.path,
    startTime: req.startTime
  });

  // Add request ID header to response
  res.set('X-Request-Id', requestId);

  next();
});

/**
 * Global middleware: Request logging
 */
app.use((req, res, next) => {
  console.log(`[${new Date().toISOString()}] ${req.method} ${req.path} - Request ID: ${req.requestId}`);
  next();
});

/**
 * Path-specific middleware: API Key Authentication
 * Only applied to routes under /api
 */
function requireApiKey(req, res, next) {
  const apiKey = req.get('X-API-Key');

  if (!apiKey) {
    return res.status(401).json({
      error: 'Unauthorized',
      message: 'X-API-Key header is required'
    });
  }

  // Simple check (in production, validate against a database)
  if (apiKey !== 'secret123') {
    return res.status(403).json({
      error: 'Forbidden',
      message: 'Invalid API key'
    });
  }

  // Add user info to request (simulated)
  req.user = { id: 1, name: 'Test User', apiKey };
  next();
}

/**
 * Middleware: Log response time
 */
function logResponseTime(req, res, next) {
  // Override end to capture response time
  const originalEnd = res.end.bind(res);
  res.end = function(data) {
    const duration = Date.now() - req.startTime;
    console.log(`[${req.requestId}] Request completed in ${duration}ms - Status: ${res.statusCode}`);

    // Clean up request store
    requestStore.delete(req.requestId);

    return originalEnd(data);
  };
  next();
}

// Apply response time logging to API routes
app.use('/api', logResponseTime);

// Public routes (no auth)
app.get('/', (req, res) => {
  res.json({
    message: 'Middleware Example - Public endpoint',
    requestId: req.requestId,
    timestamp: Date.now()
  });
});

// Health check (no auth)
app.get('/health', (req, res) => {
  res.json({
    status: 'ok',
    uptime: process.uptime(),
    activeRequests: requestStore.size
  });
});

// Protected API routes (require API key)
app.use('/api', requireApiKey);

// GET /api/protected - Protected resource
app.get('/api/protected', (req, res) => {
  res.json({
    message: 'This is a protected resource',
    requestId: req.requestId,
    user: req.user,
    headers: req.headers
  });
});

// POST /api/data - Echo with logging
app.post('/api/data', (req, res) => {
  console.log(`[${req.requestId}] Received body:`, req.body);

  res.json({
    message: 'Data received',
    requestId: req.requestId,
    received: {
      method: req.method,
      path: req.path,
      body: req.body,
      query: req.query
    },
    authenticatedUser: req.user
  });
});

// GET /api/slow - Simulated slow endpoint
app.get('/api/slow', async (req, res) => {
  // Simulate async work
  await new Promise(resolve => setTimeout(resolve, 500));

  res.json({
    message: 'Slow response completed',
    requestId: req.requestId,
    processedBy: req.user.name
  });
});

// Error endpoint to demonstrate error handling
app.get('/error', (req, res, next) => {
  // Pass an error to the error handler
  next(new Error('Simulated error for testing'));
});

// Error handling middleware
app.onError((err, req, res, _next) => {
  console.error(`[${req.requestId || 'unknown'}] Error:`, err.message);

  // Don't leak stack traces in production
  const isDevelopment = process.env.NODE_ENV === 'development';

  res.status(500).json({
    error: 'Internal Server Error',
    message: isDevelopment ? err.message : 'Something went wrong',
    requestId: req.requestId,
    ...(isDevelopment && { stack: err.stack })
  });
});

// 404 handler for unmatched routes
app.use((req, res) => {
  res.status(404).json({
    error: 'Not Found',
    path: req.path,
    method: req.method
  });
});

// Start server
const PORT = parseInt(process.env.PORT, 10) || 3000;

app.listen(PORT, () => {
  console.log(`Middleware Example Server running on http://localhost:${PORT}`);
  console.log('\nTest commands:');
  console.log('  # Public route');
  console.log(`  curl http://localhost:${PORT}/`);
  console.log('\n  # Protected route (will fail)');
  console.log(`  curl http://localhost:${PORT}/api/protected`);
  console.log('\n  # Protected route with API key');
  console.log(`  curl http://localhost:${PORT}/api/protected -H "X-API-Key: secret123"`);
  console.log('\n  # Error endpoint');
  console.log(`  curl http://localhost:${PORT}/error`);
  console.log('\nPress Ctrl+C to stop');
});
