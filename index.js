const path = require('path');
const { existsSync } = require('fs');

// Try to load prebuilt binary via node-pre-gyp, fallback to build directory
let native;
try {
  const preGyp = require('@mapbox/node-pre-gyp');
  const bindingPath = preGyp.find(path.resolve(path.join(__dirname, './package.json')));
  native = require(bindingPath);
} catch (err) {
  // Fallback to local build
  const buildPath = path.join(__dirname, 'build', 'Release', 'express_pro_native');
  if (existsSync(buildPath + '.node')) {
    native = require(buildPath);
  } else {
    throw new Error(
      `Failed to load native addon. Run 'npm run build' to compile from source.\nOriginal error: ${err.message}`
    );
  }
}

const expresspro = require('./js/index.js');

module.exports = expresspro;
