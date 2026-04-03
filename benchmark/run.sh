#!/bin/bash

set -e

echo "=== expressmax Benchmark ==="
echo "Platform: $(uname -s)"
echo "Node: $(node --version)"
echo ""

echo "Building native addon..."
npm run build

echo ""
echo "Running benchmarks..."
echo "(stub - implement actual benchmarks here)"
