#!/bin/bash
#
# iopress Valgrind Memory Check
#
# Run with: npm run memcheck
#
# This script runs the server under Valgrind to detect memory leaks.
# Note: Valgrind is Linux-only. On macOS, use Leak Sanitizer instead.

set -e

echo "=== iopress Valgrind Memory Check ==="
echo ""

# Check if running on Linux
if [[ "$OSTYPE" != "linux-gnu"* ]]; then
    echo "⚠️  Valgrind is only available on Linux."
    echo "   On macOS, consider using:"
    echo "   - AddressSanitizer: ASAN_OPTIONS=detect_leaks=1"
    echo "   - Instruments: leaks command"
    echo ""
    echo "   Running Node.js memory test instead..."
    node --expose-gc test/memory.test.js
    exit 0
fi

# Check for valgrind
if ! command -v valgrind &> /dev/null; then
    echo "❌ Valgrind not found. Install with:"
    echo "   sudo apt-get install valgrind"
    exit 1
fi

echo "Building native addon..."
npm run build

echo ""
echo "Starting server under Valgrind..."
echo "This will take a few minutes."
echo ""

# Create a simple server script for Valgrind
cat > /tmp/iopress-valgrind-server.js << 'EOF'
const iopress = require('./js/index.js');
const app = iopress();

app.get('/health', (req, res) => {
    res.json({ status: 'ok' });
});

const server = app.listen(8888, () => {
    console.log('Server ready');
});

// Stop after 30 seconds
setTimeout(() => {
    console.log('Stopping server...');
    process.exit(0);
}, 30000);
EOF

# Run valgrind
valgrind \
    --tool=memcheck \
    --leak-check=full \
    --show-leak-kinds=definite,indirect \
    --track-origins=yes \
    --error-exitcode=1 \
    --suppressions=./scripts/valgrind-suppressions.txt \
    --log-file=./valgrind.log \
    node --expose-gc /tmp/iopress-valgrind-server.js &

VALGRIND_PID=$!

# Wait for server to start
sleep 5

# Send some requests
echo "Sending requests..."
for i in {1..100}; do
    curl -s http://localhost:8888/health > /dev/null || true
done

# Wait for valgrind to finish
wait $VALGRIND_PID || true

echo ""
echo "=== Valgrind Results ==="
echo ""

# Parse results
if [ -f ./valgrind.log ]; then
    echo "Summary:"
    grep -E "(definitely lost|indirectly lost|possibly lost|still reachable)" ./valgrind.log || echo "No leaks detected"
    echo ""
    echo "Full log: ./valgrind.log"

    # Check for definitely lost
    if grep -q "definitely lost: [1-9]" ./valgrind.log; then
        echo ""
        echo "❌ Memory leaks detected!"
        exit 1
    else
        echo ""
        echo "✅ No definite memory leaks"
    fi
else
    echo "❌ Valgrind log not found"
    exit 1
fi

# Cleanup
rm -f /tmp/iopress-valgrind-server.js

echo ""
echo "Done."
