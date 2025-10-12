#!/bin/bash
#
# Validation Script for C64 Stream Acceptance Test Framework
# Runs comprehensive checks to ensure everything works correctly
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$SCRIPT_DIR/../.."
BUILD_DIR="$REPO_ROOT/build_x86_64"

echo "============================================================"
echo "C64 Stream Acceptance Test Framework Validation"
echo "============================================================"
echo ""

PASS=0
FAIL=0

# Test 1: Check if Python scripts are executable
echo "Test 1: Checking Python scripts..."
for script in generate_packets.py run_acceptance_test.py verify_output.py; do
    if [ -x "$SCRIPT_DIR/$script" ]; then
        echo "  ✅ $script is executable"
        ((PASS++))
    else
        echo "  ❌ $script is not executable"
        ((FAIL++))
    fi
done
echo ""

# Test 2: Check if Python dependencies are available
echo "Test 2: Checking Python dependencies..."
if python3 -c "import numpy" 2>/dev/null; then
    echo "  ✅ numpy is available"
    ((PASS++))
else
    echo "  ❌ numpy is not installed (run: pip3 install numpy)"
    ((FAIL++))
fi
echo ""

# Test 3: Check if UDP replay tool is built
echo "Test 3: Checking UDP replay tool..."
if [ -f "$BUILD_DIR/tests/acceptance/udp_replay" ]; then
    echo "  ✅ udp_replay is built"
    ((PASS++))
else
    echo "  ❌ udp_replay not found (run: cd build_x86_64 && cmake --build . --target udp_replay)"
    ((FAIL++))
fi
echo ""

# Test 4: Generate test packets
echo "Test 4: Generating test packets (3 frames, PAL)..."
if "$SCRIPT_DIR/generate_packets.py" --frames 3 --format PAL --output "$SCRIPT_DIR/test_packets" >/dev/null 2>&1; then
    echo "  ✅ Packet generation successful"
    ((PASS++))
else
    echo "  ❌ Packet generation failed"
    ((FAIL++))
fi
echo ""

# Test 5: Validate packet format
echo "Test 5: Validating packet format..."
cd "$SCRIPT_DIR"
if python3 - <<'EOF' >/dev/null 2>&1
import struct
from pathlib import Path
video_packet = Path("test_packets/video/PAL/frame_0000_pkt_000.bin").read_bytes()
assert len(video_packet) == 780, f"Wrong video packet size: {len(video_packet)}"
seq, frame, line, width, lines_per, bpp, encoding = struct.unpack('<HHHHBBH', video_packet[:12])
assert width == 384, f"Wrong width: {width}"
assert lines_per == 4, f"Wrong lines per packet: {lines_per}"
assert bpp == 4, f"Wrong bits per pixel: {bpp}"
audio_packet = Path("test_packets/audio/PAL/frame_0000.bin").read_bytes()
assert len(audio_packet) == 770, f"Wrong audio packet size: {len(audio_packet)}"
EOF
then
    echo "  ✅ Packet format validation passed"
    ((PASS++))
else
    echo "  ❌ Packet format validation failed"
    ((FAIL++))
fi
cd "$SCRIPT_DIR"
echo ""

# Test 6: Test UDP replay
echo "Test 6: Testing UDP replay tool..."
if "$BUILD_DIR/tests/acceptance/udp_replay" \
    "$SCRIPT_DIR/test_packets/video/PAL" \
    127.0.0.1 11000 780 >/dev/null 2>&1; then
    echo "  ✅ UDP replay successful"
    ((PASS++))
else
    echo "  ❌ UDP replay failed"
    ((FAIL++))
fi
echo ""

# Test 7: Check documentation
echo "Test 7: Checking documentation..."
if [ -f "$SCRIPT_DIR/README.md" ] && [ -f "$REPO_ROOT/doc/acceptance-testing.md" ]; then
    echo "  ✅ Documentation files present"
    ((PASS++))
else
    echo "  ❌ Documentation files missing"
    ((FAIL++))
fi
echo ""

# Test 8: Check GitHub Actions workflow
echo "Test 8: Checking GitHub Actions workflow..."
if [ -f "$REPO_ROOT/.github/workflows/acceptance-test.yaml" ]; then
    echo "  ✅ GitHub Actions workflow present"
    ((PASS++))
else
    echo "  ❌ GitHub Actions workflow missing"
    ((FAIL++))
fi
echo ""

# Cleanup
echo "Cleaning up test artifacts..."
rm -rf "$SCRIPT_DIR/test_packets"
echo ""

# Summary
echo "============================================================"
echo "Validation Results"
echo "============================================================"
echo "Passed: $PASS"
echo "Failed: $FAIL"
echo ""

if [ $FAIL -eq 0 ]; then
    echo "✅ All validation tests passed!"
    echo ""
    echo "The acceptance test framework is ready to use."
    echo "Run './quick_test.sh' for a quick demonstration."
    exit 0
else
    echo "❌ Some validation tests failed!"
    echo ""
    echo "Please fix the issues above before using the framework."
    exit 1
fi
