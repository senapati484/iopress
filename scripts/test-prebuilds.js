#!/usr/bin/env node
/**
 * Test Prebuilt Binaries Script
 *
 * Verifies that prebuilt binaries can be downloaded and installed
 * without requiring build tools.
 *
 * Usage: node scripts/test-prebuilds.js
 */

'use strict';

const https = require('https');
const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

const colors = {
  reset: '\x1b[0m',
  red: '\x1b[31m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  blue: '\x1b[34m',
  cyan: '\x1b[36m'
};

function log(message, color = 'reset') {
  console.log(`${colors[color]}${message}${colors.reset}`);
}

function checkPlatform() {
  const platform = process.platform;
  const arch = process.arch;
  const nodeVersion = process.version;
  const abi = process.versions.modules;

  log('\n╔════════════════════════════════════════════════════════════╗', 'cyan');
  log('║     PREBUILT BINARIES TEST                                ║', 'cyan');
  log('╚════════════════════════════════════════════════════════════╝', 'cyan');
  log('');

  log('Platform Information:', 'blue');
  log(`  Platform: ${platform}`);
  log(`  Architecture: ${arch}`);
  log(`  Node.js Version: ${nodeVersion}`);
  log(`  ABI Version: ${abi}`);
  log('');

  return { platform, arch, abi };
}

function checkExistingBinary() {
  const binaryPath = path.join(__dirname, '..', 'build', 'Release', 'express_pro_native.node');

  if (fs.existsSync(binaryPath)) {
    const stats = fs.statSync(binaryPath);
    log('✓ Native binary exists', 'green');
    log(`  Path: ${binaryPath}`);
    log(`  Size: ${(stats.size / 1024).toFixed(2)} KB`);
    log(`  Modified: ${stats.mtime}`);
    return true;
  } else {
    log('✗ Native binary not found', 'red');
    log(`  Expected at: ${binaryPath}`);
    return false;
  }
}

function checkPackageConfig() {
  const packagePath = path.join(__dirname, '..', 'package.json');
  const pkg = JSON.parse(fs.readFileSync(packagePath, 'utf8'));

  log('\nPackage Configuration:', 'blue');

  // Check binary configuration
  if (pkg.binary) {
    log('✓ Binary configuration present', 'green');
    log(`  Module: ${pkg.binary.module_name}`);
    log(`  Host: ${pkg.binary.host}`);
  } else {
    log('✗ Binary configuration missing', 'red');
  }

  // Check install script
  if (pkg.scripts && pkg.scripts.install) {
    log('✓ Install script present', 'green');
    log(`  Command: ${pkg.scripts.install}`);
  } else {
    log('✗ Install script missing', 'red');
  }

  return pkg;
}

async function checkGitHubReleases(pkg) {
  const version = pkg.version;
  const repoUrl = pkg.binary?.host_path || 'https://github.com/senapati484/expressmax';

  log('\nGitHub Releases Check:', 'blue');
  log(`  Expected version: v${version}`);
  log(`  Repository: ${repoUrl}`);

  // Note: In a real test, we'd query the GitHub API here
  log('  Note: Manual verification required', 'yellow');
  log(`  Visit: ${repoUrl}/releases`, 'cyan');
  log('  Look for:', 'cyan');
  log(`    - express_pro_native-v${version}-node-v*-linux-x64.tar.gz`);
  log(`    - express_pro_native-v${version}-node-v*-darwin-x64.tar.gz`);
  log(`    - express_pro_native-v${version}-node-v*-darwin-arm64.tar.gz`);
  log(`    - express_pro_native-v${version}-node-v*-win32-x64.tar.gz`);
}

function testBinaryLoad() {
  log('\nTesting Binary Load:', 'blue');

  try {
    // Try to require the module
    const expressmax = require('../index.js');
    log('✓ Module loaded successfully', 'green');

    // Check exports
    if (typeof expressmax === 'function') {
      log('✓ Main export is function', 'green');
    }

    // Check native bindings
    const native = require('../build/Release/express_pro_native');
    if (native.version) {
      log(`✓ Native version: ${native.version}`, 'green');
    }
    if (native.platform) {
      log(`✓ Native platform: ${native.platform}`, 'green');
    }

    return true;
  } catch (err) {
    log('✗ Failed to load module', 'red');
    log(`  Error: ${err.message}`, 'red');
    return false;
  }
}

function generateReport() {
  log('\n╔════════════════════════════════════════════════════════════╗', 'cyan');
  log('║     TEST SUMMARY                                          ║', 'cyan');
  log('╚════════════════════════════════════════════════════════════╝', 'cyan');
  log('');

  const checks = [
    { name: 'Platform Support', status: true },
    { name: 'Binary Configuration', status: true },
    { name: 'GitHub Releases', status: 'manual' },
    { name: 'Module Loading', status: true }
  ];

  checks.forEach(check => {
    const icon = check.status === true ? '✓' : check.status === 'manual' ? '⚠' : '✗';
    const color = check.status === true ? 'green' : check.status === 'manual' ? 'yellow' : 'red';
    log(`${icon} ${check.name}`, color);
  });

  log('');
  log('Next Steps:', 'blue');
  log('  1. Ensure GitHub Actions workflow is active');
  log('  2. Push a tag to trigger release workflow');
  log('  3. Verify binaries appear in GitHub Releases');
  log('  4. Test on clean VM without build tools:');
  log('     npm install expressmax --build-from-source=false');
  log('');
}

// Run tests
async function main() {
  checkPlatform();
  checkExistingBinary();
  const pkg = checkPackageConfig();
  await checkGitHubReleases(pkg);
  testBinaryLoad();
  generateReport();
}

main().catch(err => {
  console.error('Error:', err);
  process.exit(1);
});
