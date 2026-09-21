# C64Script fuzzing

This directory contains the libFuzzer harness for the C64Script language engine, built with ASan/UBSan.

The harness runs the parser, compiler, and VM in-process with deterministic stubs and no device or OBS requirements.

## Local run (opt-in)

Fuzzing is disabled by default. Enable it explicitly:

- Short run (default 60s):
  - ./build --fuzz
- Custom duration, corpus, and workers:
  - ./build --fuzz=600
  - FUZZ_MAX_LEN=65536 FUZZ_JOBS=2 ./build --fuzz=600
  - FUZZ_SEED_DIR=/path/to/seed ./build --fuzz

## Longer session

Set a longer duration and more workers:

- ./build --fuzz=14400
- FUZZ_JOBS=4 ./build --fuzz=14400

## Output locations

Results are written under:

- tests/script/fuzz/results/crashes/  (unique crashing inputs)
- tests/script/fuzz/results/corpus/   (evolved corpus)
- tests/script/fuzz/results/seed/     (seed corpus copied from tests/script/scripts)
- tests/script/fuzz/results/logs/     (stdout/stderr logs)
- tests/script/fuzz/results/summary.txt

## Notes

- Builds use clang with ASan/UBSan and libFuzzer. Undefined behaviour aborts the run (`-fno-sanitize-recover=undefined`), and memory leaks are reported per input (`detect_leaks=1`).
- If libFuzzer with trace-pc-guard is not available, the runner fetches and builds LLVM 12's libFuzzer locally.
- IO, HTTP, and log file writes are blocked during fuzz runs.
- Expect slower execution with sanitizers enabled.
- Each input has a 10s time limit (`FUZZ_INPUT_TIMEOUT`, passed to libFuzzer as `-timeout`). An input that exceeds it is saved to `crashes/` as a `timeout-*` file and fails the run. Set `FUZZ_INPUT_TIMEOUT=0` to disable the limit.
