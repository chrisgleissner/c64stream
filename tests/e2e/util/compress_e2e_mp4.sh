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

tmp_out="$OUTPUT"
if [[ "$OUTPUT" == "$INPUT" ]]; then
  tmp_out="${OUTPUT}.tmp"
fi

in_fps=$(ffprobe -v error -select_streams v:0 \
  -show_entries stream=r_frame_rate -of default=nokey=1:noprint_wrappers=1 "$INPUT" | \
  awk -F'/' '{ if ($2>0) printf "%.6f", $1/$2; else print $1 }' || true)
if [[ -z "$in_fps" ]]; then in_fps=50; fi

target_fps=50
awk_cmp=$(awk -v f="$in_fps" 'BEGIN{print (f>55)?"60":"50"}')
if [[ "$awk_cmp" == "60" ]]; then target_fps=60; fi

# You can detect a hardware H.264 encoder similarly if you want (e.g., h264_qsv or h264_nvenc).
use_h264_qsv=false
if ffmpeg -hide_banner -encoders 2>/dev/null | grep -q 'h264_qsv'; then
  if [[ -r /dev/dri/renderD128 ]]; then
    use_h264_qsv=true
  fi
fi

if [[ "$use_h264_qsv" == "true" ]]; then
  echo "Intel Quick Sync H.264 encoder detected. Using h264_qsv for faster encode."
  hw_init=(-init_hw_device qsv=hw:/dev/dri/renderD128 -filter_hw_device hw)
  if [[ ! -r /dev/dri/renderD128 ]]; then
    hw_init=(-init_hw-device qsv=hw -filter_hw_device hw)
  fi

  ffmpeg -y \
    "${hw_init[@]}" \
    -i "$INPUT" \
    -vf "fps=$target_fps,format=nv12,hwupload=extra_hw_frames=64" \
    -c:v h264_qsv \
    -preset medium \
    -b:v 0 \
    -look_ahead 1 \
    -c:a aac -b:a 128k \
    -movflags +faststart \
    "$tmp_out"
else
  echo "Falling back to libx264 software encode."
  ffmpeg -y -i "$INPUT" \
    -c:v libx264 \
    -preset medium \
    -crf 23 \
    -r "$target_fps" -vsync cfr \
    -c:a aac -b:a 128k \
    -movflags +faststart \
    "$tmp_out"
fi

if [[ "$tmp_out" != "$OUTPUT" ]]; then
  mv -f "$tmp_out" "$OUTPUT"
fi

echo
echo "✅ Compression complete."
echo "Original file size: $(du -h "$INPUT" | cut -f1)"
echo "Compressed file size: $(du -h "$OUTPUT" | cut -f1)"
