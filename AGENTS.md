# Codex Agent Guide for c64stream

## Purpose

- Centralize expectations for autonomous agents working on this repository.
- Reduce discovery time by highlighting the build, test, and review routines that matter most.

## Repository Snapshot

- Native OBS Studio source plugin written in C for streaming video/audio from C64 Ultimate hardware.
- Core sources live in `src/` (networking, video, audio, protocol, OBS integration).
- Protocol description: `doc/c64-stream-spec.md`. Public overview: `README.md`.
- Assets and defaults: `data/`. Developer docs: `doc/` and `docs/`.

## Key Directories & Files

- `src/c64-source.c|h`: OBS source implementation entry point.
- `src/c64-network.c|h`, `src/c64-video.c|h`, `src/c64-audio.c|h`, `src/c64-protocol.c|h`: major subsystems.
- `src/plugin-main.c`: OBS plugin registration.
- `tests/`: e2e harness (`e2e/`), supporting scripts, and CMake glue.
- `tests/e2e/`: automated capture/playback scenarios, Python helpers, config presets.
- `build-aux/`: formatting, workflow validation, Windows helper scripts.
- `tools/`: developer utilities (plugin install, preview scripts, Docker helpers).
- `CMakePresets.json`: platform build presets (`ubuntu-x86_64`, `macos`, `windows-x64`).
- `local-build.sh` / `.bat`: orchestrated local builds, optional install, e2e runs.

## Build Workflow (Linux example)

1. Configure: `cmake --preset ubuntu-x86_64`
2. Build: `cmake --build build_x86_64`
3. Artifact check: confirm `build_x86_64/c64stream.so`
4. Optional scripted flow: `./local-build.sh linux --config RelWithDebInfo --install`

macOS (`cmake --preset macos`) and Windows (`cmake --preset windows-x64`) follow the same pattern; presets enforce warnings-as-errors in `*-ci-*` variants.

## Validation Checklist (MANDATORY)
- `./build-aux/run-clang-format` (or `--check` before committing).
- `./build-aux/run-gersemi --check` for CMake formatting.
- `./build-aux/validate-workflows` when touching `.github/workflows/`.
- Clean rebuild before sign-off when touching core code: `rm -rf build_x86_64 && cmake --preset ubuntu-x86_64 && cmake --build build_x86_64`.
- Ensure new/modified source files include the required GPLv2+ header.

## Testing Guidance

- **E2E harness:** `tests/e2e/e2e.sh` or `./local-build.sh linux --e2e` (requires OBS, xvfb, additional deps). Scenarios live under `tests/e2e/scenarios/`; results land in `tests/e2e/results/`.
- **Packet utilities:** `tests/e2e/generate_packets.py`, `udp_replay/udp_replay.c`, and `verify_output.py` assist with replay-based regression checks.
- **Clean rebuilds:** `cmake --preset ubuntu-x86_64` followed by `cmake --build build_x86_64` catches most regressions when unit tests are unavailable.

## Coding Guidelines

- Prioritize: performance (low latency, preallocated buffers), robustness (input validation, network failure handling), simplicity, consistency, cross-platform behavior, and maintainability.
- Follow existing patterns—prefer atomics/semaphores from OBS utilities over ad-hoc mutexes.
- Maintain shared abstractions in `c64-network.h` to preserve Windows/Linux parity (WinSock vs BSD sockets).
- Keep changes self-explanatory; add concise comments only when necessary for complex logic.
- Enforce KISS and DRY relentlessly—prefer small, focused changes that reuse existing helpers.
- Avoid introducing new markdown in repo root unless explicitly requested (this file is user-mandated).

## Helpful Scripts & Shortcuts
- `./build-aux/run-clang-format --check`, `./build-aux/run-gersemi --check`, `./build-aux/validate-workflows`.
- `./local-build.sh <platform> [--install|--e2e]` for end-to-end developer loops.
- `build-docker.sh` and `tools/test-docker-build.sh` for containerized builds.
- Windows validation helpers: `build-aux/test-windows-*.sh`, `build-aux/verify-windows-build.sh`.
- Plugin version utilities: `build-aux/resolve-plugin-version.sh`, `tools/switch-plugin-version.sh`.
- Preview tooling: `tools/vic_preview.py`, `tools/vic_preview_live.py`.

## Reference Documents

- OBS plugin spec & usage: `README.md`, images in `docs/images/`.
- Protocol details: `doc/c64-stream-spec.md`.
- Additional developer notes live under `doc/` (kebab-case filenames).
- GitHub CI build flow: `.github/build-instructions.md`.

## Engagement Tips for Future Agents

- Start by skimming `src/c64-source.c` plus any subsystem specific to the task.
- Use `rg` for navigation; e.g., `rg "c64_stream"` or `rg "TODO"` within `src/`.
- Before editing, check `git status` to understand existing local changes (never reset user work).
- After edits, run formatting checks and rebuild before declaring success.
- Surface any cross-platform implications early (socket APIs, filesystem paths, atomic operations).
- If a task touches streaming protocol details, re-read `doc/c64-stream-spec.md` to avoid regressions.

## Collaboration Notes

- Keep commit messages and PR descriptions concise, action-oriented, and free of filler—call out scope, risk, and validation succinctly.

Keep this guide updated when workflows change so future sessions stay fast and aligned with project standards.
