# PLANS.md — Multi-hour plans for C64 Stream

This file tracks multi-step work items that require investigation + verification.

---

## Current Task (2026-01-11): Fix AV-sync test, packaging, and crash

**User request (summary)**
- Create branch `fix/av-sync-test` from updated `main`, commit the real-device runner fix, and pull latest changes from `main`.
- Remove low-value `VIDEO: DBG` / `VIDEO: DEBUG` log spam.
- Fix packaging: distributable zip must include `data/prg/*.prg` built from all `tools/c64/*.asm`.
- Fix crash: ticking the A/V sync checkbox in OBS Properties crashes OBS after ~3s; deduce and fix root cause.
- Verify locally: `ntsc_default` E2E passes and real-device E2E passes; then push and confirm CI is green.

**Constraints**
- Do not run OBS-driving E2E in cloud/CI environments.
- Fix root causes; do not skip/disable tests.
- Before pushing: run `./build-aux/run-clang-format --check` and `./build-aux/run-gersemi --check`.

### Plan (checklist)

#### 0) Branch + baseline hygiene
- [x] Fast-forward local `main` from `origin/main`.
- [x] Create branch `fix/av-sync-test` off updated `main`.
- [x] Re-apply local changes on top of updated `main`.
- [x] Commit real-device runner fix.
- [x] Commit removal of `VIDEO: DBG/DEBUG` log spam.

**Proof**
- `git pull --ff-only origin main` → fast-forwarded to `e7eb6b6`
- `git checkout -b fix/av-sync-test`
- Commits on branch:
  - `4be960d` Fix real-device AV sync analyzer args
  - `be95240` Remove low-value VIDEO DBG logs
  - `cd3b09c` Fix build after VIDEO log cleanup
  - `e4a3e8e` Ship PRGs for all C64 tools
  - `c3bee75` Init curl global state once

#### 1) Remove remaining `DBG`/`DEBUG` log spam
- [ ] Search for remaining case-sensitive `DBG` / `DEBUG` log strings in `src/`.
- [ ] Remove or downgrade only the low-value ones (keep meaningful `C64_LOG_DEBUG(...)` usage).
- [ ] Build + quick sanity run of `ntsc_default`.

#### 2) Fix distributable zip: include `data/prg/*.prg`
- [x] Inventory `tools/c64/*.asm` and expected `.prg` outputs.
- [x] Find current packaging/install path for `data/` and where `data/prg` comes from.
- [x] Add CMake build steps to generate `.prg` for each `.asm` (via `64tass`) into the build tree.
- [x] Add explicit `install(FILES ...)` rules so generated `.prg` land in install artifacts.
- [x] Add fallback to install prebuilt `data/prg/*.prg` when `64tass` is unavailable (Windows CI).
- [x] Validate by producing a zip package and inspecting contents.

**Proof**
- Inventory: `tools/c64/*.asm` → `av-sync.asm`, `av-sync-auto.asm`, `digit-cycle.asm`
- Prebuilt PRGs committed under `data/prg/` (unignored via `.gitignore`).
- Zip created locally: `release/c64stream-1.0.3-x86_64-linux-gnu.zip`
- Zip contains:
  - `.../usr/share/obs/obs-plugins/c64stream/prg/av-sync.prg`
  - `.../usr/share/obs/obs-plugins/c64stream/prg/av-sync-auto.prg`
  - `.../usr/share/obs/obs-plugins/c64stream/prg/digit-cycle.prg`

#### 3) Fix OBS crash when enabling A/V sync checkbox
- [ ] Identify the checkbox property name and its code path (property definition → update handler → runtime effect).
- [ ] Reproduce crash deterministically (prefer `tests/e2e` path; otherwise run OBS under `gdb`).
- [ ] Capture backtrace/core dump and correlate to plugin code.
- [ ] Fix root cause (likely lifecycle/threading/NULL pointer around enabling AV-sync recording).
- [ ] Add targeted guardrails (validation + error logs) so similar failures are diagnosable.

#### 4) Verification + push
- [ ] Run `./local-build.sh --install --e2e=ntsc_default` and ensure exit code 0.
- [ ] Run `./local-build.sh --real-device` and ensure exit code 0.
- [ ] Run formatting checks: `./build-aux/run-clang-format --check` and `./build-aux/run-gersemi --check`.
- [ ] Push branch and confirm CI is green.

### Progress log
- 2026-01-11 — Updated from `origin/main`, created `fix/av-sync-test`, committed runner fix + removed `VIDEO: DBG/DEBUG` logs.
- 2026-01-11 — Fixed build fallout from log cleanup (unused vars).
- 2026-01-11 — Fixed packaging: generate PRGs from all `tools/c64/*.asm`, ship prebuilt PRGs, and validated zip contains PRGs.
- 2026-01-11 — Added libcurl global init-once guard in REST client (candidate fix for AV-sync checkbox crash).
