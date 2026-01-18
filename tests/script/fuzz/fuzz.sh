#!/bin/sh

set -eu

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROJECT_ROOT=$(cd "$SCRIPT_DIR/../../.." && pwd)

BUILD_DIR=${FUZZ_BUILD_DIR:-"$PROJECT_ROOT/build_fuzz_c64script"}
RESULTS_DIR="$SCRIPT_DIR/results"
CRASH_DIR="$RESULTS_DIR/crashes"
LOG_DIR="$RESULTS_DIR/logs"
OUTPUT_CORPUS="$RESULTS_DIR/corpus"
SEED_CORPUS=${FUZZ_SEED_DIR:-"$RESULTS_DIR/seed"}
SUMMARY_FILE="$RESULTS_DIR/summary.txt"

mkdir -p "$CRASH_DIR" "$LOG_DIR" "$OUTPUT_CORPUS" "$SEED_CORPUS"

if command -v clang-15 >/dev/null 2>&1; then
    FUZZ_CC=clang-15
    FUZZ_CXX=clang++-15
elif command -v clang-17 >/dev/null 2>&1; then
    FUZZ_CC=clang-17
    FUZZ_CXX=clang++-17
elif command -v clang >/dev/null 2>&1; then
    FUZZ_CC=clang
    FUZZ_CXX=clang++
else
    echo "clang not found; install clang-17 or clang." >&2
    exit 1
fi

if [ -d "$BUILD_DIR" ]; then
    rm -rf "$BUILD_DIR"
fi

SEED_SRC="$PROJECT_ROOT/tests/script/scripts"
if [ -d "$SEED_SRC" ]; then
    rm -rf "$SEED_CORPUS"
    mkdir -p "$SEED_CORPUS"
    for seed in "$SEED_SRC"/*.c64script; do
        [ -f "$seed" ] || continue
        cp "$seed" "$SEED_CORPUS/"
    done
fi

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER="$FUZZ_CC" \
    -DCMAKE_CXX_COMPILER="$FUZZ_CXX" \
    -DENABLE_TESTS=ON \
    -DC64STREAM_ENABLE_FUZZING=ON

cmake --build "$BUILD_DIR" --target c64script_fuzz

FUZZ_BIN="$BUILD_DIR/tests/script/fuzz/c64script_fuzz"
if [ ! -x "$FUZZ_BIN" ]; then
    echo "Fuzz binary not found: $FUZZ_BIN" >&2
    exit 1
fi

MAX_LEN=${FUZZ_MAX_LEN:-65536}
MAX_TIME=${FUZZ_TIME_SECONDS:-60}
JOBS=${FUZZ_JOBS:-1}
INPUT_TIMEOUT=${FUZZ_INPUT_TIMEOUT:-5}
TIMEOUT_GRACE=${FUZZ_TIMEOUT_GRACE:-15}

LOG_FILE="$LOG_DIR/fuzz-$(date +%Y%m%d-%H%M%S).log"

FUZZ_CMD="\"$FUZZ_BIN\" -artifact_prefix=\"$CRASH_DIR/\" -max_len=\"$MAX_LEN\" -max_total_time=\"$MAX_TIME\" -timeout=\"$INPUT_TIMEOUT\" -jobs=\"$JOBS\" -workers=\"$JOBS\" \"$OUTPUT_CORPUS\" \"$SEED_CORPUS\""

set +e
if command -v timeout >/dev/null 2>&1; then
    timeout "$((MAX_TIME + TIMEOUT_GRACE))" sh -c "$FUZZ_CMD" >"$LOG_FILE" 2>&1
    EXIT_CODE=$?
else
    sh -c "$FUZZ_CMD" >"$LOG_FILE" 2>&1
    EXIT_CODE=$?
fi
set -e

WRAPPER_TIMEOUT=0
if [ "$EXIT_CODE" -eq 124 ]; then
    WRAPPER_TIMEOUT=1
    EXIT_CODE=0
fi

{
    echo "Command: $FUZZ_BIN -artifact_prefix=$CRASH_DIR/ -max_len=$MAX_LEN -max_total_time=$MAX_TIME -timeout=$INPUT_TIMEOUT -jobs=$JOBS -workers=$JOBS $OUTPUT_CORPUS $SEED_CORPUS"
    if [ "$WRAPPER_TIMEOUT" -eq 1 ]; then
        echo "Note: timeout wrapper reached ${MAX_TIME}s + ${TIMEOUT_GRACE}s grace."
    fi
    echo "Exit code: $EXIT_CODE"
    echo "Log: $LOG_FILE"
    echo "Crash inputs:"
    if ls "$CRASH_DIR"/* >/dev/null 2>&1; then
        ls -1 "$CRASH_DIR"
    else
        echo "(none)"
    fi
    if [ -f "$LOG_FILE" ]; then
        echo ""
        echo "=== Sanitizer output (first trace) ==="
        awk 'found {print} /^==[0-9]+==/ {found=1; print}' "$LOG_FILE" | sed -n '1,200p'
    fi
} >"$SUMMARY_FILE"

exit "$EXIT_CODE"
