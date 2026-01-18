# C64Script fuzzing

This directory contains the libFuzzer harness for the C64Script language engine, built with ASan/UBSan.
The harness runs the parser, compiler, and VM in-process with deterministic stubs and no device or OBS
requirements.

## Local run (opt-in)

Fuzzing is disabled by default. Enable it explicitly:

- Short run (default 60s):
  - RUN_FUZZ=1 ./local-build.sh linux
- Custom duration, corpus, and workers:
  - RUN_FUZZ=1 FUZZ_TIME_SECONDS=600 FUZZ_MAX_LEN=65536 FUZZ_JOBS=2 ./local-build.sh linux
  - RUN_FUZZ=1 FUZZ_SEED_DIR=/path/to/seed ./local-build.sh linux

## Longer session

Set a longer duration and more workers:

- RUN_FUZZ=1 FUZZ_TIME_SECONDS=14400 FUZZ_JOBS=4 ./local-build.sh linux

## Output locations

Results are written under:

- tests/script/fuzz/results/crashes/  (unique crashing inputs)
- tests/script/fuzz/results/corpus/   (evolved corpus)
- tests/script/fuzz/results/seed/     (seed corpus copied from tests/script/scripts)
- tests/script/fuzz/results/logs/     (stdout/stderr logs)
- tests/script/fuzz/results/summary.txt

## Notes

- Builds use clang with ASan/UBSan and libFuzzer.
- IO, HTTP, and log file writes are blocked during fuzz runs.
- Expect slower execution with sanitizers enabled.
