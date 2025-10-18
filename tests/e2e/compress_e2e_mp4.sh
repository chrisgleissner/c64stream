#!/usr/bin/env bash
set -euo pipefail

INPUT="test_output/c64_recording.mp4"
OUTPUT="test_output/c64_recording_compressed.mp4"

if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "Error: ffmpeg not found. Install with: sudo apt install ffmpeg"
  exit 1
fi

echo "Compressing: $INPUT"
echo "Output will be: $OUTPUT"
echo

# High compression while preserving perceptual fidelity
ffmpeg -y -i "$INPUT" \
  -c:v libx265 \
  -preset slow \
  -x265-params crf=24:qcomp=0.7:psy-rd=1.0:aq-strength=1.0:me=2:subme=3 \
  -c:a aac -b:a 128k \
  -movflags +faststart \
  "$OUTPUT"

echo
echo "✅ Compression complete."
echo "Original file size: $(du -h "$INPUT" | cut -f1)"
echo "Compressed file size: $(du -h "$OUTPUT" | cut -f1)"
echo "Size reduction: $(du -h "$INPUT" "$OUTPUT" | awk 'NR==1{orig=$1} NR==2{comp=$1} END{printf "%.2f%%\n", (1 - comp/orig) * 100}')"
