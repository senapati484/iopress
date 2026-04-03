# Prebuilt Binaries Guide

## Overview

norvex provides prebuilt binaries for major platforms, eliminating the need for users to install build tools (Python, C++ compiler, etc.).

## Supported Platforms

| Platform | Architecture | Node.js Versions | Status |
|----------|-------------|------------------|--------|
| Linux (glibc 2.28+) | x64, arm64 | 18.x, 20.x | ✅ Supported |
| macOS (11.0+) | x64, arm64 | 18.x, 20.x | ✅ Supported |
| Windows (10+) | x64 | 18.x, 20.x | ✅ Supported |

## Installation

### Standard Installation (Recommended)

```bash
npm install norvex
```

This will:
1. Try to download prebuilt binary from GitHub Releases
2. Fall back to compiling from source if binary not found

### Force Prebuilt Binary (Fail if not available)

```bash
npm install norvex --build-from-source=false
```

### Force Compile from Source

```bash
npm install norvex --build-from-source
```

## Binary Naming Convention

Prebuilt binaries follow this naming pattern:

```
express_pro_native-v{VERSION}-node-v{ABI}-{PLATFORM}-{ARCH}.tar.gz

Examples:
express_pro_native-v1.0.0-node-v115-linux-x64.tar.gz
express_pro_native-v1.0.0-node-v115-darwin-arm64.tar.gz
express_pro_native-v1.0.0-node-v115-win32-x64.tar.gz
```

### ABI Versions

| Node.js Version | ABI Version |
|-----------------|-------------|
| 18.x | 108 |
| 20.x | 115 |
| 21.x | 120 |

## Troubleshooting

### "Cannot find module" Error

If you see this error after installation:

```bash
Error: Cannot find module './build/Release/express_pro_native'
```

**Solution:**
1. Check if binary exists: `ls node_modules/norvex/build/Release/`
2. If missing, reinstall: `npm install norvex --build-from-source`

### "GLIBC version not found" (Linux)

Your Linux distribution may have an older glibc version.

**Check your glibc version:**
```bash
ldd --version
```

**Minimum requirements:**
- glibc 2.28+ for Linux x64
- glibc 2.31+ for Linux arm64

**If your glibc is too old:**
```bash
# Compile from source instead
npm install norvex --build-from-source
```

### "Invalid ELF header" (Linux)

This means the binary was downloaded for the wrong architecture.

**Check your architecture:**
```bash
uname -m
```

**Solution:**
```bash
# Remove cached binary
rm -rf node_modules/norvex

# Reinstall with correct arch
npm install norvex
```

### Windows: "The specified module could not be found"

Missing Visual C++ Redistributables.

**Solution:**
Download and install from: https://aka.ms/vs/17/release/vc_redist.x64.exe

## Testing Prebuilt Binaries

### Automated Test

```bash
npm run test:prebuilds
```

### Manual Test

```bash
# Create test directory
mkdir test-install
cd test-install

# Initialize package
npm init -y

# Install without build tools
npm install norvex --build-from-source=false

# Test
node -e "const app = require('norvex')(); console.log('✓ Installed successfully');"
```

## For Package Maintainers

### Building Prebuilt Binaries Locally

```bash
# Build and package
npm run prebuild

# Result in: build/stage/*.tar.gz
ls build/stage/
```

### Uploading to GitHub Releases

Binaries are automatically uploaded via GitHub Actions when you push a tag:

```bash
# Create new version
npm run release

# Push tag
git push --follow-tags origin main
```

### Manual Upload

```bash
# Build for all platforms
npm run prebuild

# Upload (requires GITHUB_TOKEN)
npm run prebuild:upload
```

## CI/CD Testing

Use the GitHub Actions workflow to test prebuilt binaries:

1. Go to Actions tab
2. Select "Test Prebuilt Binaries"
3. Click "Run workflow"
4. Enter version to test

This tests installation on clean VMs without build tools.

## Size Optimization

Prebuilt binaries are compressed to minimize download size:

| Platform | Uncompressed | Compressed |
|----------|--------------|------------|
| Linux x64 | ~150 KB | ~50 KB |
| macOS arm64 | ~200 KB | ~70 KB |
| Windows x64 | ~180 KB | ~60 KB |

## Security

All prebuilt binaries are:
- Built from source in CI (GitHub Actions)
- Signed with GitHub's attestations
- Checksum verified on download

## Verification

Verify binary authenticity:

```bash
# Check binary was downloaded (not built locally)
ls -la node_modules/norvex/build/stage/

# Verify checksum (if available)
cat node_modules/norvex/build/stage/*.tar.gz.sha256
```

## Fallback Behavior

If prebuilt binary download fails:

1. npm logs warning: "Failed to download prebuilt binary"
2. Falls back to `node-gyp rebuild`
3. Compiles from source (requires build tools)

To disable fallback:
```json
{
  "scripts": {
    "install": "node-pre-gyp install"
  }
}
```

## Platform-Specific Notes

### Linux

- Requires glibc 2.28+ (Ubuntu 18.04+, Debian 10+, RHEL 8+)
- Alpine Linux uses musl (not supported by prebuilt binaries)
- For Alpine, use: `npm install norvex --build-from-source`

### macOS

- Supports both Intel (x64) and Apple Silicon (arm64)
- macOS 11.0 (Big Sur) minimum
- No additional dependencies required

### Windows

- Requires Visual C++ Redistributables
- Windows 10/11 or Server 2019+
- Both PowerShell and CMD supported

## Getting Help

If prebuilt binaries don't work:

1. Check [GitHub Releases](https://github.com/senapati484/norvex/releases) for your platform
2. Run `npm run test:prebuilds` for diagnostics
3. Open an issue with:
   - Platform: `uname -a`
   - Node version: `node --version`
   - npm version: `npm --version`
   - Error log: `npm install norvex --verbose`
