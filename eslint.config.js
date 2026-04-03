const js = require('@eslint/js');
const globals = require('globals');

/** @type {import('eslint').Linter.Config[]} */
module.exports = [
  js.configs.recommended,
  {
    files: ['js/**/*.js', 'examples/**/*.js'],
    languageOptions: {
      ecmaVersion: 2022,
      sourceType: 'commonjs',
      globals: {
        ...globals.node,
        ...globals.es2022
      }
    },
    rules: {
      'indent': ['error', 2],
      'linebreak-style': ['error', 'unix'],
      'quotes': ['error', 'single'],
      'semi': ['error', 'always'],
      'no-unused-vars': ['error', { 
        'argsIgnorePattern': '^_', 
        'varsIgnorePattern': '^_'
      }],
      'no-console': 'off'
    }
  },
  {
    files: ['index.js'],
    languageOptions: {
      ecmaVersion: 2022,
      sourceType: 'commonjs',
      globals: {
        ...globals.node,
        '__dirname': 'readonly'
      }
    }
  },
  {
    files: ['test/**/*.js', 'benchmark/**/*.js', 'scripts/**/*.js'],
    languageOptions: {
      ecmaVersion: 2022,
      sourceType: 'commonjs',
      globals: {
        ...globals.node,
        ...globals.es2022,
        describe: 'readonly',
        it: 'readonly',
        before: 'readonly',
        after: 'readonly',
        fetch: 'readonly',
        setTimeout: 'readonly',
        global: 'readonly'
      }
    },
    rules: {
      'no-unused-vars': ['warn', { 
        'argsIgnorePattern': '^_', 
        'varsIgnorePattern': '^_'
      }]
    }
  },
  {
    ignores: ['eslint.config.js']
  }
];
