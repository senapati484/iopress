
      const iopress = require('./index.js');
      const app = iopress();
      
      app.get('/', (req, res) => res.json({ message: 'ok', worker: process.env.IOPRESS_WORKER_ID }));
      app.get('/health', (req, res) => res.json({ status: 'ok', worker: process.env.IOPRESS_WORKER_ID }));
      app.get('/users', (req, res) => res.json({ users: [], worker: process.env.IOPRESS_WORKER_ID }));
      app.post('/echo', (req, res) => res.json({ ...req.body, worker: process.env.IOPRESS_WORKER_ID }));
      app.get('/search', (req, res) => res.json({ results: [], worker: process.env.IOPRESS_WORKER_ID }));
      
      app.listen(9999, { reusePort: true }, () => console.log('Worker ' + process.env.IOPRESS_WORKER_ID + ' listening'));
    