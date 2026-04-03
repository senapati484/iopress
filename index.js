const path = require('path');
const { existsSync } = require('fs');

// Try to load native addon, build if not found
const buildPath = path.join(__dirname, 'build', 'Release', 'express_pro_native');

if (!existsSync(buildPath + '.node')) {
  throw new Error(
    `Failed to load native addon. Run 'npm run build' to compile from source.`
  );
}

require(buildPath);

const iopress = require('./js/index.js');

module.exports = iopress;