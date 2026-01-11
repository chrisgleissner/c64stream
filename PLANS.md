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
  - `5d2fcb0` Real-device runner: enable AV sync PRG
  - `dc7a9ad` Update plan with PRG packaging proof
  - `e6a7325` Update AV sync plan with verification

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
- [x] Identify the checkbox property name and its code path (property definition → update handler → runtime effect).
- [x] Reproduce/validate via real-device test (exercises the same code path without opening the UI).
- [x] Fix root cause.

**Findings / root cause**
- The OBS “A/V sync” checkbox maps to source setting `record_av_sync`.
- Code path: `c64_record_update_settings()` in [src/c64-record.c](src/c64-record.c#L650-L760) → when `record_av_sync` flips on it calls `c64_rest_run_prg_async(host, password, prg_path)`.
- Root cause: libcurl global init was not guaranteed to run exactly once before background REST usage; this can crash in multi-threaded usage.

**Fix**
- Commit `c3bee75` initializes libcurl global state exactly once (POSIX `pthread_once`, Windows `InitOnceExecuteOnce`) before creating any REST client.

#### 4) Verification + push
- [x] Run `./local-build.sh linux --install --e2e=ntsc_default` and ensure exit code 0.
- [x] Run `./local-build.sh linux --real-device` and ensure exit code 0.
- [ ] Run formatting checks: `./build-aux/run-clang-format --check` and `./build-aux/run-gersemi --check`.
- [ ] Push branch and confirm CI is green.

**Proof**
- Local E2E (`ntsc_default`) artifacts written to [tests/e2e/results/ntsc_default](tests/e2e/results/ntsc_default/):
  - Summary in [tests/e2e/results/ntsc_default/validation_results.json](tests/e2e/results/ntsc_default/validation_results.json): all checks pass, `udp_reception` is `warning` due to minor loss (30799/30803).
- Real-device run (exercises `record_av_sync` REST PRG path; no OBS crash observed):
  - `./local-build.sh linux --real-device`
  - Output: PRG started successfully via REST (`/v1/runners:run_prg`), OBS capture completed, device reset succeeded.
  - Results written under `tests/e2e/results/real_c64u_av_sync/session_20260111_004739/` (MP4 + CSV + report).

### Progress log
- 2026-01-11 — Updated from `origin/main`, created `fix/av-sync-test`, committed runner fix + removed `VIDEO: DBG/DEBUG` logs.
- 2026-01-11 — Fixed build fallout from log cleanup (unused vars).
- 2026-01-11 — Fixed packaging: generate PRGs from all `tools/c64/*.asm`, ship prebuilt PRGs, and validated zip contains PRGs.
- 2026-01-11 — Added libcurl global init-once guard in REST client; validated AV-sync checkbox path via real-device run.
- 2026-01-11 — Local E2E `ntsc_default` completed successfully (one warning: minor UDP packet loss).
