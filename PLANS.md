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

#### 1) Remove remaining `DBG`/`DEBUG` log spam
- [ ] Search for remaining case-sensitive `DBG` / `DEBUG` log strings in `src/`.
- [ ] Remove or downgrade only the low-value ones (keep meaningful `C64_LOG_DEBUG(...)` usage).
- [ ] Build + quick sanity run of `ntsc_default`.

#### 2) Fix distributable zip: include `data/prg/*.prg`
- [ ] Inventory `tools/c64/*.asm` and expected `.prg` outputs.
- [ ] Find current packaging/install path for `data/` and where `data/prg` comes from.
- [ ] Add CMake build steps to generate `.prg` for each `.asm` (via `64tass`) into the build tree.
- [ ] Add explicit `install(FILES ...)` rules so generated `.prg` land in `.../data/prg/` for all platforms.
- [ ] Validate by producing a package (`cmake --build build_x86_64 --target package`) and inspecting zip contents.

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
