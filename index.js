const path = require('path');
const { existsSync } = require('fs');

// Try to load native addon, build if not found
let native;
const buildPath = path.join(__dirname, 'build', 'Release', 'express_pro_native');

if (existsSync(buildPath + '.node')) {
  native = require(buildPath);
} else {
  throw new Error(
    `Failed to load native addon. Run 'npm run build' to compile from source.`
  );
}

const maxpress = require('./js/index.js');

module.exports = maxpress;