const expressPro = require('./build/Release/express_pro_native');

module.exports = {
  version: expressPro.version,
  platform: expressPro.platform,
  backend: expressPro.backend,
  createServer: expressPro.createServer
};
