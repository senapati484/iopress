/**
 * Deprecation warning utilities for maxpress
 * @module deprecation
 */

'use strict';

const pkg = require('../package.json');
const PACKAGE_NAME = pkg.name;

/**
 * Cache for emitted warnings to prevent duplicates
 * @type {Set<string>}
 */
const emittedWarnings = new Set();

/**
 * Emits a deprecation warning for a feature
 * @param {string} feature - The deprecated feature name
 * @param {string} deprecatedIn - Version when deprecated (e.g., '1.3.0')
 * @param {string} [removalIn] - Version when will be removed (e.g., '2.0.0')
 * @param {string} [alternative] - Suggested replacement API
 * @param {string} [code] - Optional error code (e.g., 'EXP_PRO_LEGACY_MODE')
 */
function emitDeprecationWarning(feature, deprecatedIn, removalIn, alternative, code) {
  // Skip in production if explicitly silenced
  if (process.env.NO_DEPRECATION === 'true' || process.noDeprecation) {
    return;
  }

  const cacheKey = `${feature}:${deprecatedIn}`;

  // Prevent duplicate warnings for same feature in same process
  if (emittedWarnings.has(cacheKey)) {
    return;
  }
  emittedWarnings.add(cacheKey);

  // Build warning message
  let message = `${PACKAGE_NAME}: '${feature}' is deprecated since v${deprecatedIn}`;

  if (removalIn) {
    message += `, will be removed in v${removalIn}`;
  }

  if (alternative) {
    message += `. Use '${alternative}' instead`;
  }

  // Emit via Node's warning system
  const warning = new Error(message);
  warning.name = 'DeprecationWarning';

  const warningOptions = {
    type: 'DeprecationWarning',
    code: code || `DEP_${feature.toUpperCase().replace(/[^A-Z0-9]/g, '_')}`
  };

  // Use process.emitWarning for proper handling
  process.emitWarning(message, warningOptions);
}

/**
 * Marks an option as deprecated and returns the modern alternative value
 * @param {Object} options - Options object
 * @param {string} oldKey - Deprecated option key
 * @param {string} newKey - New option key (if renamed)
 * @param {*} [defaultValue] - Default value if neither provided
 * @param {Object} metadata - Deprecation metadata
 * @param {string} metadata.deprecatedIn - Version when deprecated
 * @param {string} metadata.removalIn - Version when will be removed
 * @param {string} [metadata.alternative] - Alternative usage message
 * @returns {*} The value to use (from old or new key)
 */
function deprecatedOption(options, oldKey, newKey, defaultValue, metadata) {
  const hasOld = options && oldKey in options;
  const hasNew = options && newKey in options;

  if (hasOld) {
    const altMessage = metadata.alternative ||
      (newKey !== oldKey ? `option '${newKey}'` : 'the modern API');

    emitDeprecationWarning(
      `option '${oldKey}'`,
      metadata.deprecatedIn,
      metadata.removalIn,
      altMessage,
      metadata.code
    );

    if (hasNew) {
      // Both provided - warn and use new
      process.emitWarning(
        `${PACKAGE_NAME}: Both '${oldKey}' and '${newKey}' provided, using '${newKey}'`,
        { type: 'DeprecationWarning' }
      );
      return options[newKey];
    }

    return options[oldKey];
  }

  if (hasNew) {
    return options[newKey];
  }

  return defaultValue;
}

/**
 * Wrapper that warns on first call to a deprecated function
 * @param {Function} fn - The deprecated function
 * @param {string} name - Function name for warning
 * @param {Object} metadata - Deprecation metadata
 * @param {string} metadata.deprecatedIn - Version when deprecated
 * @param {string} metadata.removalIn - Version when will be removed
 * @param {string} metadata.alternative - Alternative function name
 * @returns {Function} Wrapped function that emits warning on first call
 */
function deprecatedFunction(fn, name, metadata) {
  let warned = false;

  return function(...args) {
    if (!warned) {
      warned = true;
      emitDeprecationWarning(
        `function '${name}()'`,
        metadata.deprecatedIn,
        metadata.removalIn,
        metadata.alternative ? `${metadata.alternative}()` : undefined,
        metadata.code
      );
    }
    return fn.apply(this, args);
  };
}

/**
 * Checks if running with --pending-deprecation flag
 * @returns {boolean}
 */
function hasPendingDeprecation() {
  return process.execArgv.includes('--pending-deprecation');
}

/**
 * Checks if running with --throw-deprecation flag
 * @returns {boolean}
 */
function hasThrowDeprecation() {
  return process.execArgv.includes('--throw-deprecation');
}

module.exports = {
  emitDeprecationWarning,
  deprecatedOption,
  deprecatedFunction,
  hasPendingDeprecation,
  hasThrowDeprecation
};
