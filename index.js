const path = require('path');
const { existsSync } = require('fs');

const forceFallback = process.env.IOPRESS_FORCE_FALLBACK === '1';
let iopress;

if (forceFallback) {
  iopress = require('./js/fallback.js');
} else {
  const buildPath = path.join(__dirname, 'build', 'Release', 'express_pro_native');
  try {
    if (!existsSync(buildPath + '.node')) {
      throw new Error('Native addon binary not found');
    }
    require(buildPath);
    iopress = require('./js/index.js');
  } catch (err) {
    if (process.env.NODE_ENV !== 'test') {
      console.warn(
        `[iopress] Warning: Failed to load native addon (${err.message}). ` +
        `Falling back to pure JavaScript engine.`
      );
    }
    iopress = require('./js/fallback.js');
  }
}

module.exports = iopress;