#!/bin/sh

set -eu

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROJECT_ROOT=$(cd "$SCRIPT_DIR/../../.." && pwd)

# Accept optional positional argument: ./fuzz.sh [seconds]
# This overrides FUZZ_TIME_SECONDS env var.
if [ "${1:-}" != "" ]; then
    FUZZ_TIME_SECONDS="$1"
fi

BUILD_DIR=${FUZZ_BUILD_DIR:-"$PROJECT_ROOT/build_fuzz_c64script"}
RESULTS_DIR="$SCRIPT_DIR/results"
CRASH_DIR="$RESULTS_DIR/crashes"
LOG_DIR="$RESULTS_DIR/logs"
OUTPUT_CORPUS="$RESULTS_DIR/corpus"
SEED_CORPUS=${FUZZ_SEED_DIR:-"$RESULTS_DIR/seed"}
SUMMARY_FILE="$RESULTS_DIR/summary.txt"
RUN_ID=$(date +%Y%m%d-%H%M%S)
LIBFUZZER_DIR="$SCRIPT_DIR/libfuzzer"
LIBFUZZER_VERSION=${FUZZ_LIBFUZZER_LLVM_VERSION:-"12.0.1"}
LIBFUZZER_SRC="$LIBFUZZER_DIR/llvm-project-$LIBFUZZER_VERSION.src"
LIBFUZZER_BUILD="$LIBFUZZER_DIR/build"
LIBFUZZER_LIB_DEFAULT="$LIBFUZZER_DIR/libFuzzer.a"

rm -rf "$CRASH_DIR"
mkdir -p "$CRASH_DIR" "$LOG_DIR" "$OUTPUT_CORPUS" "$SEED_CORPUS"

if [ -n "${FUZZ_CC:-}" ] && [ -n "${FUZZ_CXX:-}" ]; then
    :
elif command -v clang-18 >/dev/null 2>&1; then
    FUZZ_CC=clang-18
    FUZZ_CXX=clang++-18
elif command -v clang-17 >/dev/null 2>&1; then
    FUZZ_CC=clang-17
    FUZZ_CXX=clang++-17
elif command -v clang-16 >/dev/null 2>&1; then
    FUZZ_CC=clang-16
    FUZZ_CXX=clang++-16
elif command -v clang-15 >/dev/null 2>&1; then
    FUZZ_CC=clang-15
    FUZZ_CXX=clang++-15
elif command -v clang-14 >/dev/null 2>&1; then
    FUZZ_CC=clang-14
    FUZZ_CXX=clang++-14
elif command -v clang-13 >/dev/null 2>&1; then
    FUZZ_CC=clang-13
    FUZZ_CXX=clang++-13
elif command -v clang >/dev/null 2>&1; then
    FUZZ_CC=clang
    FUZZ_CXX=clang++
else
    echo "clang not found; install clang (or set FUZZ_CC/FUZZ_CXX)." >&2
    exit 1
fi

if [ -d "$BUILD_DIR" ]; then
    rm -rf "$BUILD_DIR"
fi

LIBFUZZER_LIBRARY=${LIBFUZZER_LIBRARY:-"$LIBFUZZER_LIB_DEFAULT"}
if [ ! -f "$LIBFUZZER_LIBRARY" ]; then
    mkdir -p "$LIBFUZZER_DIR"
    if [ ! -d "$LIBFUZZER_SRC" ]; then
        ARCHIVE="$LIBFUZZER_DIR/llvm-project-$LIBFUZZER_VERSION.src.tar.xz"
        URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-$LIBFUZZER_VERSION/llvm-project-$LIBFUZZER_VERSION.src.tar.xz"
        if command -v curl >/dev/null 2>&1; then
            curl -L -o "$ARCHIVE" "$URL"
        elif command -v wget >/dev/null 2>&1; then
            wget -O "$ARCHIVE" "$URL"
        else
            echo "curl or wget is required to download llvm-project for libFuzzer" >&2
            exit 1
        fi
        if [ ! -s "$ARCHIVE" ] || [ "$(wc -c <"$ARCHIVE")" -lt 1000000 ]; then
            echo "llvm-project download failed or incomplete: $ARCHIVE" >&2
            exit 1
        fi
        tar -xf "$ARCHIVE" -C "$LIBFUZZER_DIR"
    fi

    PATCH_FILE="$LIBFUZZER_SRC/compiler-rt/lib/fuzzer/FuzzerTracePC.cpp"
    if [ -f "$PATCH_FILE" ] && grep -q "WarnAboutDeprecatedInstrumentation" "$PATCH_FILE"; then
        if grep -q "exit(1);" "$PATCH_FILE"; then
            sed -i "s/exit(1);/return;/" "$PATCH_FILE"
        fi
    fi

    LIBFUZZER_SRC_DIR="$LIBFUZZER_SRC/compiler-rt/lib/fuzzer"
    (cd "$LIBFUZZER_SRC_DIR" && CXX="$FUZZ_CXX" sh ./build.sh)
    if [ -f "$LIBFUZZER_SRC_DIR/libFuzzer.a" ]; then
        cp "$LIBFUZZER_SRC_DIR/libFuzzer.a" "$LIBFUZZER_LIBRARY"
    fi
fi

if [ ! -f "$LIBFUZZER_LIBRARY" ]; then
    echo "libFuzzer library not found after build." >&2
    exit 1
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
    -DC64STREAM_ENABLE_FUZZING=ON \
    -DLIBFUZZER_LIBRARY="$LIBFUZZER_LIBRARY"

cmake --build "$BUILD_DIR" --target c64script_fuzz

FUZZ_BIN="$BUILD_DIR/tests/script/fuzz/c64script_fuzz"
if [ ! -x "$FUZZ_BIN" ]; then
    echo "Fuzz binary not found: $FUZZ_BIN" >&2
    exit 1
fi

MAX_LEN=${FUZZ_MAX_LEN:-1024}
MAX_TIME=${FUZZ_TIME_SECONDS:-60}
JOBS=${FUZZ_JOBS:-1}
INPUT_TIMEOUT=${FUZZ_INPUT_TIMEOUT:-0}
TIMEOUT_GRACE=${FUZZ_TIMEOUT_GRACE:-0}

LOG_FILE="$LOG_DIR/fuzz-$RUN_ID.log"
RUN_REPORT="$RESULTS_DIR/run-$RUN_ID.md"

JOB_ARGS=""
if [ "$JOBS" -gt 1 ]; then
    JOB_ARGS="-jobs=\"$JOBS\" -workers=\"$JOBS\""
fi

CMD_PREFIX=""
if command -v stdbuf >/dev/null 2>&1; then
    CMD_PREFIX="stdbuf -oL -eL"
fi

FUZZ_CMD="$CMD_PREFIX \"$FUZZ_BIN\" -artifact_prefix=\"$CRASH_DIR/\" -max_len=\"$MAX_LEN\" -max_total_time=\"$MAX_TIME\" -timeout=\"$INPUT_TIMEOUT\" $JOB_ARGS -print_final_stats=1 -verbosity=1 \"$OUTPUT_CORPUS\" \"$SEED_CORPUS\""

ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}"; export ASAN_OPTIONS

echo "=== Starting fuzzing for ${MAX_TIME}s (run ${RUN_ID}) ==="
echo "  Binary:  $FUZZ_BIN"
echo "  Corpus:  $OUTPUT_CORPUS"
echo "  Crashes: $CRASH_DIR"
echo "  Log:     $LOG_FILE"

# Temp file to capture fuzz binary exit code; POSIX sh lacks PIPESTATUS.
FUZZ_EXIT_FILE="$RESULTS_DIR/.fuzz_exit_${RUN_ID}.tmp"

set +e
START_TS=$(date +%s)
{
    if command -v timeout >/dev/null 2>&1; then
        timeout -s INT "$((MAX_TIME + TIMEOUT_GRACE))" sh -c "$FUZZ_CMD"
    else
        sh -c "$FUZZ_CMD"
    fi
    printf '%s' "$?" >"$FUZZ_EXIT_FILE"
} 2>&1 | tee "$LOG_FILE"
END_TS=$(date +%s)
set -e

if [ -f "$FUZZ_EXIT_FILE" ]; then
    EXIT_CODE=$(cat "$FUZZ_EXIT_FILE")
    rm -f "$FUZZ_EXIT_FILE"
else
    EXIT_CODE=1
fi

DURATION=$((END_TS - START_TS))
echo "=== Fuzzing completed after ${DURATION}s (expected ${MAX_TIME}s) ==="

# Detect wrapper timeout (exit 124 from GNU timeout).
WRAPPER_TIMEOUT=0
if [ "$EXIT_CODE" -eq 124 ]; then
    WRAPPER_TIMEOUT=1
    EXIT_CODE=0
fi

# Guard: fail if the log is empty - the fuzz binary produced no output.
LOG_EMPTY=0
if [ ! -s "$LOG_FILE" ]; then
    LOG_EMPTY=1
    EXIT_CODE=1
    echo "FATAL: Fuzz log is empty - binary produced no output." >&2
fi

# Guard: fail if the log contains no libFuzzer output markers.
if [ "$LOG_EMPTY" -eq 0 ] && ! grep -qE "^#[0-9]|exec/s:|INFO:" "$LOG_FILE"; then
    echo "FATAL: Fuzz log contains no libFuzzer output markers (no #N lines, no exec/s, no INFO:)." >&2
    EXIT_CODE=1
fi

# Guard: fail if actual runtime is under 50% of expected when no crash was found.
# This catches silent exits where the binary quit immediately without fuzzing.
CRASH_COUNT=$(ls -1 "$CRASH_DIR" 2>/dev/null | wc -l | tr -d ' ')
MIN_EXPECTED=$((MAX_TIME / 2))
if [ "$EXIT_CODE" -eq 0 ] && [ "$CRASH_COUNT" -eq 0 ] && [ "$WRAPPER_TIMEOUT" -eq 0 ] \
   && [ "$DURATION" -lt "$MIN_EXPECTED" ] && [ "$MAX_TIME" -gt 10 ]; then
    echo "FATAL: Actual runtime ${DURATION}s is less than 50% of expected ${MAX_TIME}s with no crashes; binary may have exited silently." >&2
    EXIT_CODE=1
fi

SEED_COUNT=$(ls -1 "$SEED_CORPUS" 2>/dev/null | wc -l | tr -d ' ')
CORPUS_COUNT=$(ls -1 "$OUTPUT_CORPUS" 2>/dev/null | wc -l | tr -d ' ')
ITERATIONS=$(grep -E "stat::number_of_executed_units" "$LOG_FILE" | tail -1 | awk '{print $2}')
if [ -z "$ITERATIONS" ]; then
    ITERATIONS="unavailable"
fi
EXEC_PER_SEC=$(grep -E "stat::average_exec_per_sec" "$LOG_FILE" | tail -1 | awk '{print $2}')
if [ -z "$EXEC_PER_SEC" ]; then
    EXEC_PER_SEC="unavailable"
fi
STATUS="completed"
if [ "$WRAPPER_TIMEOUT" -eq 1 ]; then
    STATUS="completed (wrapper timeout)"
fi
if [ "$EXIT_CODE" -ne 0 ]; then
    STATUS="failed"
fi
if [ "$LOG_EMPTY" -eq 1 ]; then
    STATUS="failed (no fuzzer output)"
fi

{
    if [ "$JOBS" -gt 1 ]; then
        echo "Command: $FUZZ_BIN -artifact_prefix=$CRASH_DIR/ -max_len=$MAX_LEN -max_total_time=$MAX_TIME -timeout=$INPUT_TIMEOUT -jobs=$JOBS -workers=$JOBS -print_final_stats=1 -verbosity=1 $OUTPUT_CORPUS $SEED_CORPUS"
    else
        echo "Command: $FUZZ_BIN -artifact_prefix=$CRASH_DIR/ -max_len=$MAX_LEN -max_total_time=$MAX_TIME -timeout=$INPUT_TIMEOUT -print_final_stats=1 -verbosity=1 $OUTPUT_CORPUS $SEED_CORPUS"
    fi
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

cat >"$RUN_REPORT" <<EOF
# C64Script fuzz run report

- Run ID: $RUN_ID
- Status: $STATUS
- Duration: ${DURATION}s
- Expected duration: ${MAX_TIME}s
- Seed inputs: $SEED_COUNT
- Corpus inputs: $CORPUS_COUNT
- Crash inputs: $CRASH_COUNT
- Iterations: $ITERATIONS
- Exec/s: $EXEC_PER_SEC
- Command: $FUZZ_BIN -artifact_prefix=$CRASH_DIR/ -max_len=$MAX_LEN -max_total_time=$MAX_TIME -timeout=$INPUT_TIMEOUT$( [ "$JOBS" -gt 1 ] && printf " -jobs=%s -workers=%s" "$JOBS" "$JOBS" ) -print_final_stats=1 -verbosity=1 $OUTPUT_CORPUS $SEED_CORPUS
- Log: $LOG_FILE

## Crash inputs

EOF

if ls "$CRASH_DIR"/* >/dev/null 2>&1; then
    ls -1 "$CRASH_DIR" >>"$RUN_REPORT"
else
    echo "(none)" >>"$RUN_REPORT"
fi

printf "\n## Notes\n\n" >>"$RUN_REPORT"
if [ "$WRAPPER_TIMEOUT" -eq 1 ]; then
    echo "Timeout wrapper reached ${MAX_TIME}s + ${TIMEOUT_GRACE}s grace." >>"$RUN_REPORT"
else
    echo "Run completed without wrapper timeout." >>"$RUN_REPORT"
fi
echo "Leak detection disabled for fuzzing (ASAN_OPTIONS=detect_leaks=0)." >>"$RUN_REPORT"

exit "$EXIT_CODE"
