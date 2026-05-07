const iopress = require('./js/index.js');
const app = iopress();
app.get('/health', (req, res) => res.json({ status: 'ok' }));
app.listen(3461, () => {
  console.log('Server started on 3461');
});
