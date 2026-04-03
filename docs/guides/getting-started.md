# Getting Started with norvex

Welcome to norvex! This guide will walk you through creating your first high-performance HTTP server using our native C++ HTTP engine.

## Table of Contents

1. [Installation](#installation)
2. [Your First Server](#your-first-server)
3. [Basic Routing](#basic-routing)
4. [Request & Response](#request--response)
5. [Middleware](#middleware)
6. [Running Your Server](#running-your-server)
7. [Next Steps](#next-steps)

---

## Installation

### Prerequisites

Before you begin, ensure you have the following installed:

- **Node.js**: Version 18.x or higher ([Download](https://nodejs.org/))
- **npm**: Version 9.x or higher (comes with Node.js)
- **C++ Build Tools**:
  - **Linux**: `sudo apt-get install build-essential`
  - **macOS**: Install Xcode Command Line Tools (`xcode-select --install`)
  - **Windows**: Install Visual Studio Build Tools with "Desktop development with C++" workload

### Install norvex

Create a new directory for your project and install norvex:

```bash
mkdir my-first-server
cd my-first-server
npm init -y
npm install norvex
```

**Note**: On Linux, you may need `liburing-dev` for optimal performance:
```bash
sudo apt-get install liburing-dev  # Ubuntu/Debian
```

---

## Your First Server

Create a file named `server.js` in your project directory:

```javascript
// server.js
const norvex = require('norvex');

// Create an application instance
const app = norvex();

// Define a route
app.get('/', (req, res) => {
  res.json({ message: 'Hello from norvex!' });
});

// Start the server
const PORT = 3000;
app.listen(PORT, () => {
  console.log(`Server is running on http://localhost:${PORT}`);
  console.log(`Native backend: ${norvex.backend}`);
});
```

Run your server:

```bash
node server.js
```

You should see:
```
Server is running on http://localhost:3000
Native backend: kqueue  (or io_uring/iocp depending on your platform)
```

Test it:
```bash
curl http://localhost:3000/
# Output: {"message":"Hello from norvex!"}
```

---

## Basic Routing

norvex supports Express-style routing with HTTP methods:

```javascript
const app = norvex();

// GET request
app.get('/users', (req, res) => {
  res.json({ users: ['Alice', 'Bob', 'Charlie'] });
});

// POST request
app.post('/users', (req, res) => {
  res.status(201).json({ created: true, body: req.body });
});

// Route parameters
app.get('/users/:id', (req, res) => {
  res.json({ userId: req.params.id });
});

// PUT request
app.put('/users/:id', (req, res) => {
  res.json({ updated: req.params.id });
});

// DELETE request
app.delete('/users/:id', (req, res) => {
  res.status(204).end();
});

// PATCH request
app.patch('/users/:id', (req, res) => {
  res.json({ patched: req.params.id });
});
```

---

## Request & Response

### Request Object (`req`)

The request object provides information about the incoming HTTP request:

```javascript
app.get('/demo', (req, res) => {
  // Request properties
  console.log(req.method);      // HTTP method (GET, POST, etc.)
  console.log(req.path);        // URL path
  console.log(req.url);         // Full URL including query string
  console.log(req.query);       // Parsed query parameters as object
  console.log(req.headers);     // Request headers
  console.log(req.body);        // Parsed request body (JSON automatically parsed)
  console.log(req.params);      // Route parameters
  
  // Get specific header (case-insensitive)
  const auth = req.get('Authorization');
  
  res.json({ received: true });
});
```

### Response Object (`res`)

The response object provides methods to send HTTP responses:

```javascript
app.get('/response-examples', (req, res) => {
  // Send JSON response (automatically sets Content-Type: application/json)
  res.json({ status: 'success', data: [1, 2, 3] });
  
  // Send plain text
  res.send('Hello World');
  
  // Set status code
  res.status(404).send('Not Found');
  
  // Set headers
  res.set('X-Custom-Header', 'value');
  
  // Redirect
  res.redirect('/new-path');        // 302 redirect
  res.redirect('/new-path', 301);   // 301 redirect
  
  // Chain methods
  res.status(201).json({ created: true });
});
```

### Query Parameters Example

```javascript
// GET /search?q=express&limit=10
app.get('/search', (req, res) => {
  const query = req.query.q;       // "express"
  const limit = req.query.limit;   // "10" (or 10 if using middleware)
  
  res.json({ query, limit });
});
```

### Request Body Example

```javascript
// POST /users with JSON body
app.post('/users', (req, res) => {
  // req.body is automatically parsed for application/json
  const user = req.body;
  
  res.status(201).json({ 
    message: 'User created',
    user: user 
  });
});
```

---

## Middleware

Middleware functions execute before your route handlers:

```javascript
const app = norvex();

// Global middleware (runs on every request)
app.use((req, res, next) => {
  console.log(`${req.method} ${req.path} - ${new Date().toISOString()}`);
  next(); // Continue to next middleware or route handler
});

// Path-specific middleware
app.use('/api', (req, res, next) => {
  // Only runs for paths starting with /api
  console.log('API request detected');
  next();
});

// Authentication middleware example
function authenticate(req, res, next) {
  const token = req.get('Authorization');
  if (!token) {
    return res.status(401).json({ error: 'Unauthorized' });
  }
  // Add user info to request
  req.user = { id: '123', name: 'Alice' };
  next();
}

// Use middleware on specific routes
app.get('/protected', authenticate, (req, res) => {
  res.json({ message: 'Secret data', user: req.user });
});

// Error handling
app.onError((err, req, res, next) => {
  console.error('Error:', err);
  res.status(500).json({ 
    error: 'Internal Server Error',
    message: err.message 
  });
});
```

---

## Running Your Server

### Development Mode

```bash
node server.js
```

### Production Mode

For production, set the `NODE_ENV` environment variable:

```bash
NODE_ENV=production node server.js
```

### Graceful Shutdown

Handle shutdown signals properly:

```javascript
const app = norvex();
const server = app.listen(3000, () => {
  console.log('Server started');
});

// Graceful shutdown
process.on('SIGTERM', () => {
  console.log('SIGTERM received, shutting down gracefully');
  app.close(() => {
    console.log('Server closed');
    process.exit(0);
  });
});

process.on('SIGINT', () => {
  console.log('SIGINT received, shutting down gracefully');
  app.close(() => {
    console.log('Server closed');
    process.exit(0);
  });
});
```

---

## Server Configuration

Configure server options when starting:

```javascript
app.listen(3000, {
  initialBufferSize: 16384,  // 16KB initial buffer (default: 4KB)
  maxBodySize: 10 * 1024 * 1024  // 10MB max body size (default: 1MB)
}, () => {
  console.log('Server started with custom configuration');
});
```

---

## Complete Example: REST API

Here's a complete REST API example:

```javascript
// api-server.js
const norvex = require('norvex');

const app = norvex();

// In-memory "database"
const users = [
  { id: '1', name: 'Alice', email: 'alice@example.com' },
  { id: '2', name: 'Bob', email: 'bob@example.com' }
];

// Middleware
app.use((req, res, next) => {
  console.log(`${req.method} ${req.path}`);
  next();
});

// Health check
app.get('/health', (req, res) => {
  res.json({ status: 'ok', backend: norvex.backend });
});

// Get all users
app.get('/api/users', (req, res) => {
  res.json({ users });
});

// Get user by ID
app.get('/api/users/:id', (req, res) => {
  const user = users.find(u => u.id === req.params.id);
  if (!user) {
    return res.status(404).json({ error: 'User not found' });
  }
  res.json({ user });
});

// Create user
app.post('/api/users', (req, res) => {
  const { name, email } = req.body || {};
  if (!name || !email) {
    return res.status(400).json({ error: 'Name and email required' });
  }
  
  const newUser = {
    id: String(users.length + 1),
    name,
    email
  };
  users.push(newUser);
  
  res.status(201).json({ user: newUser });
});

// Update user
app.put('/api/users/:id', (req, res) => {
  const user = users.find(u => u.id === req.params.id);
  if (!user) {
    return res.status(404).json({ error: 'User not found' });
  }
  
  const { name, email } = req.body || {};
  if (name) user.name = name;
  if (email) user.email = email;
  
  res.json({ user });
});

// Delete user
app.delete('/api/users/:id', (req, res) => {
  const index = users.findIndex(u => u.id === req.params.id);
  if (index === -1) {
    return res.status(404).json({ error: 'User not found' });
  }
  
  users.splice(index, 1);
  res.status(204).end();
});

// 404 handler
app.use((req, res) => {
  res.status(404).json({ error: 'Not found' });
});

// Error handler
app.onError((err, req, res, next) => {
  console.error('Error:', err);
  res.status(500).json({ error: 'Internal Server Error' });
});

// Start server
const PORT = process.env.PORT || 3000;
app.listen(PORT, () => {
  console.log(`API Server running on http://localhost:${PORT}`);
  console.log(`Native backend: ${norvex.backend}`);
});
```

Run it:
```bash
node api-server.js
```

Test it:
```bash
# Health check
curl http://localhost:3000/health

# Get all users
curl http://localhost:3000/api/users

# Get user by ID
curl http://localhost:3000/api/users/1

# Create user
curl -X POST http://localhost:3000/api/users \
  -H "Content-Type: application/json" \
  -d '{"name":"Charlie","email":"charlie@example.com"}'

# Update user
curl -X PUT http://localhost:3000/api/users/1 \
  -H "Content-Type: application/json" \
  -d '{"name":"Alice Updated"}'

# Delete user
curl -X DELETE http://localhost:3000/api/users/2
```

---

## Performance Tips

1. **Use Keep-Alive**: Reuse connections for multiple requests
2. **Set Appropriate Limits**: Configure `maxBodySize` based on your needs
3. **Avoid Heavy Computation**: Offload CPU-intensive tasks to worker threads
4. **Monitor Memory**: Run with `--expose-gc` to profile memory usage

---

## Troubleshooting

### "Module not found" Error

```bash
# Rebuild native module
npm run build
# or
npm run configure && npm run build
```

### "Cannot find module" for norvex

```bash
# Ensure you're in the correct directory
cd my-first-server

# Reinstall dependencies
rm -rf node_modules package-lock.json
npm install
```

### Port Already in Use

```bash
# Find process using port
lsof -i :3000  # macOS/Linux
netstat -ano | findstr :3000  # Windows

# Kill process or use different port
app.listen(3001);  # Use port 3001 instead
```

---

## Next Steps

- **Benchmarking**: Run `npm run bench` to test performance
- **Examples**: Check the `examples/` directory for more patterns
- **API Reference**: See `index.d.ts` for TypeScript definitions
- **Contributing**: See `CONTRIBUTING.md` to contribute to the project

---

**Happy Coding!** 🚀

For more information, visit our [GitHub Repository](https://github.com/senapati484/norvex).
