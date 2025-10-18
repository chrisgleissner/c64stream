#!/usr/bin/env bash
set -euo pipefail

INPUT="${1:-test_output/c64_recording.mp4}"
OUTPUT_DEFAULT="test_output/c64_recording_compressed.mp4"
OUTPUT="${2:-$OUTPUT_DEFAULT}"

if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "Error: ffmpeg not found. Install with: sudo apt install ffmpeg"
  exit 1
fi

echo "Compressing: $INPUT"
echo "Output will be: $OUTPUT"
echo

# High compression while preserving perceptual fidelity
tmp_out="$OUTPUT"
# If output equals input, write to a temp file then move over the original
if [[ "$OUTPUT" == "$INPUT" ]]; then
  tmp_out="${OUTPUT}.tmp"
fi

# Detect input FPS via ffprobe (fallback to 50)
in_fps=$(ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate -of default=nokey=1:noprint_wrappers=1 "$INPUT" | awk -F'/' '{ if ($2>0) printf "%.6f", $1/$2; else print $1 }' || true)
if [[ -z "$in_fps" ]]; then in_fps=50; fi
# Snap to nearest common rate (50 or 60) to preserve cadence
target_fps=50
awk_cmp=$(awk -v f="$in_fps" 'BEGIN{print (f>55)?"60":"50"}')
if [[ "$awk_cmp" == "60" ]]; then target_fps=60; fi

ffmpeg -y -i "$INPUT" \
  -c:v libx265 \
  -preset slow \
  -x265-params crf=24:qcomp=0.7:psy-rd=1.0:aq-strength=1.0:me=2:subme=3 \
  -r "$target_fps" -vsync cfr \
  -c:a aac -b:a 128k \
  -movflags +faststart \
  "$tmp_out"

# If we wrote to a temp file for in-place overwrite, move it over
if [[ "$tmp_out" != "$OUTPUT" ]]; then
  mv -f "$tmp_out" "$OUTPUT"
fi

echo
echo "✅ Compression complete."
echo "Original file size: $(du -h "$INPUT" | cut -f1)"
echo "Compressed file size: $(du -h "$OUTPUT" | cut -f1)"
