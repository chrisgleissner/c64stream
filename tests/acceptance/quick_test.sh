#!/bin/bash
#
# Quick Test Script for C64 Stream Acceptance Tests
# Demonstrates the complete workflow without requiring OBS
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../../build_x86_64"

echo "================================================"
echo "C64 Stream Acceptance Test - Quick Demo"
echo "================================================"
echo ""

# Check if udp_replay is built
if [ ! -f "$BUILD_DIR/tests/acceptance/udp_replay" ]; then
    echo "❌ UDP replay tool not found. Please build it first:"
    echo "   cd build_x86_64 && cmake --build . --target udp_replay"
    exit 1
fi

echo "✅ UDP replay tool found"
echo ""

# Generate test packets if they don't exist
if [ ! -d "$SCRIPT_DIR/test_packets" ]; then
    echo "📦 Generating test packets (5 frames, PAL)..."
    "$SCRIPT_DIR/generate_packets.py" --frames 5 --format PAL --output "$SCRIPT_DIR/test_packets"
    echo ""
else
    echo "✅ Test packets already exist"
    echo ""
fi

# Show packet statistics
echo "📊 Test Packet Statistics:"
echo "   Video packets: $(find $SCRIPT_DIR/test_packets/video/PAL -name '*.bin' | wc -l)"
echo "   Audio packets: $(find $SCRIPT_DIR/test_packets/audio/PAL -name '*.bin' | wc -l)"
echo ""

# Test UDP replay
echo "🚀 Testing UDP replay tool..."
echo ""
echo "Video stream replay:"
"$BUILD_DIR/tests/acceptance/udp_replay" \
    "$SCRIPT_DIR/test_packets/video/PAL" \
    127.0.0.1 11000 780 --verbose

echo ""
echo "Audio stream replay:"
"$BUILD_DIR/tests/acceptance/udp_replay" \
    "$SCRIPT_DIR/test_packets/audio/PAL" \
    127.0.0.1 11001 770 --verbose

echo ""
echo "================================================"
echo "✅ Acceptance Test Quick Demo Complete"
echo "================================================"
echo ""
echo "To run with actual OBS integration:"
echo "  1. Start OBS and add a C64 Stream source"
echo "  2. Configure source to use ports 11000 (video) and 11001 (audio)"
echo "  3. Start recording in OBS"
echo "  4. Run this script to replay packets"
echo "  5. Stop OBS recording and verify output"
echo ""
