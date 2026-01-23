#!/usr/bin/env bash
set -euo pipefail

# Creates 5-minute FFmpeg testsrc2 videos at C64 PAL/NTSC-like rates.

OUT_DIR="test-patterns/ffmpeg-testsrc2"
DURATION_SECONDS=300

mkdir -p "${OUT_DIR}"

ffmpeg -hide_banner -y \
  -f lavfi -i "testsrc2=size=384x240:rate=59.826" -t "${DURATION_SECONDS}" \
  -vf "format=yuv420p" \
  -c:v libx264 -preset veryfast -tune zerolatency \
  -pix_fmt yuv420p -movflags +faststart \
  "${OUT_DIR}/c64_ntsc_testsrc2_384x240_59.826Hz_5m.mp4"

ffmpeg -hide_banner -y \
  -f lavfi -i "testsrc2=size=384x272:rate=50.125" -t "${DURATION_SECONDS}" \
  -vf "format=yuv420p" \
  -c:v libx264 -preset veryfast -tune zerolatency \
  -pix_fmt yuv420p -movflags +faststart \
  "${OUT_DIR}/c64_pal_testsrc2_384x272_50.125Hz_5m.mp4"

echo "Created test videos in: ${OUT_DIR}"
ls -lah "${OUT_DIR}"
