/**
 * REST API Example - CRUD Operations with Route Parameters
 *
 * Demonstrates:
 * - In-memory CRUD operations (Create, Read, Update, Delete)
 * - Route parameters (e.g., /users/:id)
 * - HTTP methods (GET, POST, PUT, PATCH, DELETE)
 * - Status codes and error responses
 *
 * Run: node examples/rest-api.js
 * Requires: npm install && npm run build
 *
 * Test with curl:
 *
 *   # List all users
 *   curl http://localhost:3000/users
 *
 *   # Create a user
 *   curl -X POST http://localhost:3000/users \
 *     -H "Content-Type: application/json" \
 *     -d '{"name":"Alice","email":"alice@example.com"}'
 *
 *   # Get a specific user (replace :id with actual ID)
 *   curl http://localhost:3000/users/1
 *
 *   # Update a user (full update)
 *   curl -X PUT http://localhost:3000/users/1 \
 *     -H "Content-Type: application/json" \
 *     -d '{"name":"Alice Updated","email":"alice.new@example.com"}'
 *
 *   # Partial update
 *   curl -X PATCH http://localhost:3000/users/1 \
 *     -H "Content-Type: application/json" \
 *     -d '{"name":"Alice Smith"}'
 *
 *   # Delete a user
 *   curl -X DELETE http://localhost:3000/users/1
 *
 *   # Query users by name
 *   curl "http://localhost:3000/users?name=Alice"
 */

'use strict';

const expressmax = require('../index.js');

const app = expressmax();

// In-memory data store
const users = new Map();
let nextId = 1;

// Seed with some data
users.set(nextId++, { id: 1, name: 'Alice', email: 'alice@example.com', createdAt: Date.now() });
users.set(nextId++, { id: 2, name: 'Bob', email: 'bob@example.com', createdAt: Date.now() });

// GET /users - List all users (with optional name filter)
app.get('/users', (req, res) => {
  let result = Array.from(users.values());

  // Filter by name query param if provided
  if (req.query.name) {
    const nameFilter = req.query.name.toLowerCase();
    result = result.filter(user =>
      user.name.toLowerCase().includes(nameFilter)
    );
  }

  res.json({
    count: result.length,
    users: result
  });
});

// GET /users/:id - Get a specific user
app.get('/users/:id', (req, res) => {
  const id = parseInt(req.params.id, 10);

  if (isNaN(id)) {
    return res.status(400).json({ error: 'Invalid user ID' });
  }

  const user = users.get(id);

  if (!user) {
    return res.status(404).json({ error: 'User not found' });
  }

  res.json(user);
});

// POST /users - Create a new user
app.post('/users', (req, res) => {
  const { name, email } = req.body || {};

  if (!name || !email) {
    return res.status(400).json({
      error: 'Missing required fields: name and email are required'
    });
  }

  const user = {
    id: nextId++,
    name,
    email,
    createdAt: Date.now()
  };

  users.set(user.id, user);

  res.status(201).json(user);
});

// PUT /users/:id - Full update (replace entire resource)
app.put('/users/:id', (req, res) => {
  const id = parseInt(req.params.id, 10);

  if (isNaN(id)) {
    return res.status(400).json({ error: 'Invalid user ID' });
  }

  const existing = users.get(id);
  if (!existing) {
    return res.status(404).json({ error: 'User not found' });
  }

  const { name, email } = req.body || {};

  if (!name || !email) {
    return res.status(400).json({
      error: 'Missing required fields: name and email are required'
    });
  }

  // Replace entire resource
  const updated = {
    id,
    name,
    email,
    createdAt: existing.createdAt,
    updatedAt: Date.now()
  };

  users.set(id, updated);
  res.json(updated);
});

// PATCH /users/:id - Partial update
app.patch('/users/:id', (req, res) => {
  const id = parseInt(req.params.id, 10);

  if (isNaN(id)) {
    return res.status(400).json({ error: 'Invalid user ID' });
  }

  const existing = users.get(id);
  if (!existing) {
    return res.status(404).json({ error: 'User not found' });
  }

  // Merge partial update
  const updated = {
    ...existing,
    ...req.body,
    id, // Prevent ID from being changed
    updatedAt: Date.now()
  };

  users.set(id, updated);
  res.json(updated);
});

// DELETE /users/:id - Delete a user
app.delete('/users/:id', (req, res) => {
  const id = parseInt(req.params.id, 10);

  if (isNaN(id)) {
    return res.status(400).json({ error: 'Invalid user ID' });
  }

  const existing = users.get(id);
  if (!existing) {
    return res.status(404).json({ error: 'User not found' });
  }

  users.delete(id);
  res.status(204).end();
});

// Start server
const PORT = parseInt(process.env.PORT, 10) || 3000;

app.listen(PORT, () => {
  console.log(`REST API Server running on http://localhost:${PORT}`);
  console.log('\nTest commands:');
  console.log('  # List users');
  console.log(`  curl http://localhost:${PORT}/users`);
  console.log('\n  # Create user');
  console.log(`  curl -X POST http://localhost:${PORT}/users -H "Content-Type: application/json" -d '{"name":"Alice","email":"alice@example.com"}'`);
  console.log('\n  # Get user');
  console.log(`  curl http://localhost:${PORT}/users/1`);
  console.log('\n  # Update user');
  console.log(`  curl -X PUT http://localhost:${PORT}/users/1 -H "Content-Type: application/json" -d '{"name":"Alice Updated","email":"alice.new@example.com"}'`);
  console.log('\n  # Delete user');
  console.log(`  curl -X DELETE http://localhost:${PORT}/users/1`);
  console.log('\nPress Ctrl+C to stop');
});
