#!/bin/bash

set -e

echo "=== Express-Pro Benchmark ==="
echo "Platform: $(uname -s)"
echo "Node: $(node --version)"
echo ""

echo "Building native addon..."
npm run build

echo ""
echo "Running benchmarks..."
echo "(stub - implement actual benchmarks here)"
