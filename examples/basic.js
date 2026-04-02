const expressPro = require('../index.js');

console.log('Express-Pro v' + expressPro.version);
console.log('Platform:', expressPro.platform);
console.log('Backend:', expressPro.backend);

const server = expressPro.createServer();
console.log('Server config:', server);
