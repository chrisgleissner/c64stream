#!/bin/bash

# Local Docker build script for C64 Stream Ubuntu build environment
# This builds the pre-built container locally for testing

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

echo "🐳 Building C64 Stream Ubuntu build container..."

# Build the Ubuntu build environment image
docker build \
    -f "${SCRIPT_DIR}/docker/Dockerfile.ubuntu-build" \
    -t c64stream/ubuntu-build:latest \
    "${PROJECT_ROOT}"

echo "✅ Container built successfully!"
echo ""
echo "🧪 Testing the container..."

# Test that the container works
docker run --rm c64stream/ubuntu-build:latest bash -c '
    echo "Testing build tools..."
    cmake --version
    ninja --version  
    obs --version
    ccache --version
    
    echo "Testing Qt6..."
    pkg-config --exists Qt6Core && echo "✅ Qt6Core found"
    pkg-config --exists Qt6Widgets && echo "✅ Qt6Widgets found"
    pkg-config --exists Qt6Svg && echo "✅ Qt6Svg found"
    
    echo "Testing OBS headers..."
    if [ -f "/usr/include/obs/obs.h" ]; then
        echo "✅ OBS headers found"
    else
        echo "❌ OBS headers missing"
        exit 1
    fi
    
    echo "✅ All tests passed!"
'

echo ""
echo "🚀 Container ready! You can now use it with:"
echo "   docker run --rm -v \$(pwd):/workspace c64stream/ubuntu-build:latest bash -c 'cd /workspace && cmake --preset ubuntu-x86_64 && cmake --build build_x86_64'"