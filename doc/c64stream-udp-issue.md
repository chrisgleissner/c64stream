# UDP packet loss regression (e1d1ad2 → d33a10a)

This document analyzes the diff in [doc/c64stream-udp-issue.diff](doc/c64stream-udp-issue.diff) (generated from `git diff e1d1ad2..d33a10a -- src tests/e2e/config/...`).

Goal: classify *actual changes* between the last-good commit (`e1d1ad2…`) and first-bad commit (`d33a10a…`) by likelihood of causing the observed E2E symptom:

- `tests/e2e` sends ~30,803 packets over ~8s.
- Plugin `network.csv` stops early (~3–4s span) and logs far fewer packets.

This is intentionally mechanical: it only reasons from what changed in the diff and what the E2E harness does (debug logging enabled in E2E `properties.ini`).

## High likelihood

### 1) Video: re-enabled per-frame “white detection” with heavy logging

File: `src/c64-video.c`

Change (from diff): the “white frame detection” block in `c64_render_frame_direct()` was previously commented-out and is now active again.

Why this is a strong candidate:

- E2E runs with `debug_logging=true` (confirmed by OBS log in current repro), so this code is *executed every frame*.
- The code performs:
  - a full scan across all pixels (`for (size_t i = 0; i < pixel_count; i++)`) for every frame, and
  - multiple `C64_LOG_INFO(...)` calls per frame (`Frame … Checking…`, `first pixel`, and a `result`).
- This adds high CPU + high I/O pressure to the video path.
- If video processing falls behind, the UDP receive loop can fail to drain the kernel socket buffer in time → UDP drops.

Concrete mechanism (still fact-based): increasing per-frame work in the hot path reduces available time to service UDP receive, increasing probability of kernel drops.

### 2) Video+Audio: new “drop packets from unexpected sources” filter

Files: `src/c64-video.c`, `src/c64-audio.c`, `src/c64-source.c`, `src/c64-types.h`

Change (from diff): introduced `expected_peer_ip_set` / `expected_peer_ip` and a filter that `continue;`s if a received UDP packet’s sender IP doesn’t match.

Why this is a strong candidate:

- If the expected peer IP is wrong (hostname not resolved yet, DNS changes, multi-NIC confusion, etc.), *all packets* can be dropped immediately.
- The filter executes before packet-size validation and before buffering/logging, so when it triggers it looks like “lost packets” at the logging layer.

What to verify mechanically when applying this hunk:

- Whether the expected IP is set to the correct address at stream start (`c64_set_expected_peer_ip` is called in `c64_refresh_resolved_ip`, `c64_update`, and `c64_start_streaming`).
- Whether the sender address used for comparison is valid on the platform-specific receive path.

## Medium likelihood

### 3) Video: changed handling of the “all-white detection disabled” TODO

File: `src/c64-video.c`

This is really the same functional change as (1), but worth calling out: the previous code explicitly disabled the feature “to unblock CI”; the new code re-enables it without throttling logs.

If the root cause turns out to be CPU starvation / I/O contention, the “fix” is likely to be:

- throttling logging,
- using sampling instead of a full-frame scan,
- or gating the entire detection behind a dedicated E2E-only flag that’s OFF by default.

## Low likelihood (in this diff)

### 4) Scene JSON tweak

File: `tests/e2e/config/obs-studio/basic/scenes/C64StreamTest.json`

This only affects OBS scene/test configuration. It could influence timing (GPU load, filters), but relative to (1)/(2) it’s less directly tied to UDP receive/logging.

### 5) `src/c64-source.c` plumbing for expected-peer

File: `src/c64-source.c`

The helper `c64_set_expected_peer_ip()` and the additional calls are mostly wiring. By itself it should not cause drops unless it leads to a bad `expected_peer_ip` value (which is covered in (2)).

## Proposed mechanical experiment order

When applying changes from first-bad onto last-good in a clean experiment worktree, apply in this order and rerun `./local-build.sh linux --install --e2e=ntsc_default` after each step:

1. **White detection enablement** in `c64_render_frame_direct()` (video-only) → rerun.
2. **Expected peer fields + setter** in `src/c64-types.h` + `src/c64-source.c` (but do NOT add packet drop filters yet) → rerun.
3. Add **video** packet drop filter → rerun.
4. Add **audio** packet drop filter → rerun.

This isolates which change flips the test from pass→fail with minimal ambiguity.
