#!/usr/bin/env bash
set -euo pipefail

show_help() {
  cat <<'EOF'
Usage: extract.frame [OPTIONS]

Extract a single video frame from an MP4, either by exact frame index or
the first frame whose timestamp is >= the specified time.

Required:
  -i, --input  PATH     Input video file (e.g., e2e_recording.mp4)
  -o, --output PATH     Output image file (PNG recommended)

Choose one:
  -t, --time   SECONDS  Extract first frame at or after this timestamp (float)
  -f, --frame  INDEX    Extract the exact frame number (0-based index)

Options:
  -v, --verbose         Verbose logging
  -h, --help            Show this help

This tool prefers ffmpeg when available. If ffmpeg is missing, it falls back
to a Python implementation using OpenCV (cv2) and Pillow (PIL).
EOF
}

INPUT=""
OUTPUT=""
TIME_S=""
FRAME_IDX=""
VERBOSE=false

log() {
  if [[ "$VERBOSE" == true ]]; then
    echo "[extract.frame] $*" >&2
  fi
}

die() { echo "Error: $*" >&2; exit 1; }

have_cmd() { command -v "$1" >/dev/null 2>&1; }

ensure_parent_dir() {
  local path="$1"; mkdir -p "$(dirname -- "$path")"
}

verify_png() {
  # Try Python Pillow verification if available; otherwise rely on file existence
  if have_cmd python3; then
    python3 - <<'PY' "$OUTPUT" || true
import sys
try:
    from PIL import Image  # type: ignore
    p = sys.argv[1]
    im = Image.open(p)
    im.verify()  # raises if invalid
    print("PNG_OK", p)
except Exception as e:
    print("PNG_VERIFY_FAILED", e)
    sys.exit(0)
PY
  fi
}

# Parse args
while [[ $# -gt 0 ]]; do
  case "$1" in
    -i|--input)  INPUT="${2:-}"; shift 2;;
    -o|--output) OUTPUT="${2:-}"; shift 2;;
    -t|--time)   TIME_S="${2:-}"; shift 2;;
    -f|--frame)  FRAME_IDX="${2:-}"; shift 2;;
    -v|--verbose) VERBOSE=true; shift;;
    -h|--help)   show_help; exit 0;;
    *) die "Unknown argument: $1";;
  esac
done

[[ -n "$INPUT" ]]  || die "--input is required"
[[ -n "$OUTPUT" ]] || die "--output is required"
if [[ -z "$TIME_S" && -z "$FRAME_IDX" ]]; then
  die "Specify either --time or --frame"
fi
if [[ -n "$TIME_S" && -n "$FRAME_IDX" ]]; then
  die "Specify only one of --time or --frame"
fi
[[ -f "$INPUT" ]] || die "Input file not found: $INPUT"
ensure_parent_dir "$OUTPUT"

extract_with_ffmpeg_time() {
  local input="$1" out="$2" t="$3"
  # First frame with timestamp >= t
  # Note: Use select filter for accurate thresholding
  local vf_prefix=""
  local -a hwaccel_args=()
  if [[ "${C64_E2E_USE_NVIDIA:-}" == "1" ]]; then
    vf_prefix="hwdownload,format=nv12,format=rgb24,"
    hwaccel_args=("-hwaccel" "cuda" "-hwaccel_output_format" "cuda")
  fi
  ffmpeg -hide_banner -loglevel error -y \
    "${hwaccel_args[@]}" \
    -i "$input" \
    -vf "${vf_prefix}select=gte(t\,$t)" -frames:v 1 -q:v 2 \
    "$out"
}

extract_with_ffmpeg_frame() {
  local input="$1" out="$2" n="$3"
  # Exact frame by index
  local vf_prefix=""
  local -a hwaccel_args=()
  if [[ "${C64_E2E_USE_NVIDIA:-}" == "1" ]]; then
    vf_prefix="hwdownload,format=nv12,format=rgb24,"
    hwaccel_args=("-hwaccel" "cuda" "-hwaccel_output_format" "cuda")
  fi
  ffmpeg -hide_banner -loglevel error -y \
    "${hwaccel_args[@]}" \
    -i "$input" \
    -vf "${vf_prefix}select=eq(n\,$n)" -frames:v 1 -q:v 2 \
    "$out"
}

extract_with_python() {
  local input="$1" out="$2" mode="$3" value="$4"
  python3 - <<'PY' "$input" "$out" "$mode" "$value"
import sys, os, math
inp, outp, mode, val = sys.argv[1:5]
try:
    import cv2  # type: ignore
    from PIL import Image  # type: ignore
except Exception as e:
    print("FALLBACK_MISSING_DEPS", e)
    sys.exit(2)

if not os.path.exists(inp):
    print("INPUT_NOT_FOUND", inp)
    sys.exit(1)

cap = cv2.VideoCapture(inp)
if not cap.isOpened():
    print("OPEN_FAIL")
    sys.exit(3)

fps = cap.get(cv2.CAP_PROP_FPS)
if not fps or fps <= 0:
    fps = 60.0

if mode == 'time':
    try:
        t = float(val)
    except Exception:
        print("BAD_TIME", val)
        sys.exit(4)
    frame_index = int(math.ceil(t * fps))
else:
    try:
        frame_index = int(val)
    except Exception:
        print("BAD_FRAME", val)
        sys.exit(5)

cap.set(cv2.CAP_PROP_POS_FRAMES, frame_index)
ok, frame = cap.read()
if not ok or frame is None:
    print("READ_FAIL_AT", frame_index)
    sys.exit(6)

rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
Image.fromarray(rgb).save(outp, format='PNG')
cap.release()
print("WROTE", outp)
PY
}

if have_cmd ffmpeg; then
  if [[ -n "$TIME_S" ]]; then
    log "Using ffmpeg, time=$TIME_S"
    extract_with_ffmpeg_time "$INPUT" "$OUTPUT" "$TIME_S" || die "ffmpeg time extract failed"
  else
    log "Using ffmpeg, frame=$FRAME_IDX"
    extract_with_ffmpeg_frame "$INPUT" "$OUTPUT" "$FRAME_IDX" || die "ffmpeg frame extract failed"
  fi
else
  if [[ -n "$TIME_S" ]]; then
    log "Using python fallback, time=$TIME_S"
    extract_with_python "$INPUT" "$OUTPUT" time "$TIME_S" || die "python time extract failed"
  else
    log "Using python fallback, frame=$FRAME_IDX"
    extract_with_python "$INPUT" "$OUTPUT" frame "$FRAME_IDX" || die "python frame extract failed"
  fi
fi

if [[ ! -s "$OUTPUT" ]]; then
  die "No output produced: $OUTPUT"
fi

verify_png || true
echo "WROTE $OUTPUT"
