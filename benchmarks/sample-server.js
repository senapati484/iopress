/**
 * Sample Test Server for Benchmarking
 * 
 * This server is used by the benchmark harness to test performance.
 * It provides multiple endpoints that match benchmark scenarios.
 * 
 * Run: node benchmarks/sample-server.js
 */

const iopress = require('../index.js');

const PORT = process.env.PORT || 3000;

const app = iopress();

app.get('/', (req, res) => {
  res.json({ message: 'Hello World' });
});

app.get('/health', (req, res) => {
  res.json({ status: 'ok' });
});

app.get('/users/:id', (req, res) => {
  res.json({ id: req.params.id });
});

app.post('/echo', (req, res) => {
  res.json(req.body);
});

app.get('/search', (req, res) => {
  res.json({ results: [] });
});

app.onError((err, req, res) => {
  console.error('Error:', err);
  res.status(500).json({ error: err.message });
});

app.listen(PORT, () => {
  console.log(`Benchmark server running on http://localhost:${PORT}`);
});