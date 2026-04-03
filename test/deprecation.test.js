/**
 * @file Test deprecation warning utilities
 */

const { describe, it } = require('node:test');
const assert = require('node:assert');
const deprecation = require('../js/deprecation');

describe('Deprecation utilities', () => {
  it('should emit deprecation warning for deprecated options', (t) => {
    const warnings = [];
    const originalEmit = process.emitWarning;

    // Capture warnings
    process.emitWarning = (message, options) => {
      warnings.push({ message, options });
    };

    // Simulate deprecated option usage (only old option provided)
    const options = {
      oldOption: 'value'
    };

    const result = deprecation.deprecatedOption(
      options,
      'oldOption',
      'newOption',
      'default',
      {
        deprecatedIn: '1.3.0',
        removalIn: '2.0.0',
        alternative: 'option "newOption"',
        code: 'DEP_OLD_OPTION'
      }
    );

    // Restore
    process.emitWarning = originalEmit;

    // Assertions
    assert.strictEqual(warnings.length, 1);
    assert.ok(warnings[0].message.includes('oldOption'));
    assert.ok(warnings[0].message.includes('deprecated'));
    assert.ok(warnings[0].message.includes('2.0.0'));
  });

  it('should prevent duplicate warnings for same feature', (t) => {
    const warnings = [];
    const originalEmit = process.emitWarning;

    process.emitWarning = (message, options) => {
      warnings.push({ message, options });
    };

    // Call twice with same feature
    deprecation.emitDeprecationWarning('duplicateTest', '1.0.0', '2.0.0');
    deprecation.emitDeprecationWarning('duplicateTest', '1.0.0', '2.0.0');

    process.emitWarning = originalEmit;

    assert.strictEqual(warnings.length, 1, 'Should only emit one warning');
  });

  it('should wrap deprecated functions with warning', (t) => {
    const warnings = [];
    const originalEmit = process.emitWarning;

    process.emitWarning = (message, options) => {
      warnings.push({ message, options });
    };

    const oldFn = (a, b) => a + b;
    const wrapped = deprecation.deprecatedFunction(oldFn, 'addNumbers', {
      deprecatedIn: '1.2.0',
      removalIn: '2.0.0',
      alternative: 'add()'
    });

    // First call should emit warning
    const result1 = wrapped(1, 2);
    // Second call should not emit warning
    const result2 = wrapped(3, 4);

    process.emitWarning = originalEmit;

    assert.strictEqual(result1, 3);
    assert.strictEqual(result2, 7);
    assert.strictEqual(warnings.length, 1);
    assert.ok(warnings[0].message.includes('addNumbers'));
  });
});

describe('Runtime deprecation checks', () => {
  it('should detect --pending-deprecation flag', () => {
    const result = deprecation.hasPendingDeprecation();
    // This will depend on how tests are run
    assert.strictEqual(typeof result, 'boolean');
  });

  it('should detect --throw-deprecation flag', () => {
    const result = deprecation.hasThrowDeprecation();
    // This will depend on how tests are run
    assert.strictEqual(typeof result, 'boolean');
  });
});
