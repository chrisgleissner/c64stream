# INVESTIGATION.md

## Purpose
Process + ledger for open-ended investigations (regressions, hard bugs). Optimised for autonomous LLM work with partial context.

## Global Rules

### When to Use
Use this file when root cause is unknown and correctness requires evidence (not plausible explanations). Feature work stays in plans.md.

### States
- ACTIVE: investigation ongoing
- RESOLVED: fixed + validated
- BLOCKED: cannot proceed (must include proof + requirements)

### Hypotheses
Each hypothesis must include:
- Status: active | falsified | confirmed
- Scope: files / functions / threads involved
- Falsification: what evidence or test would disprove it

No investigation may be RESOLVED while any hypothesis is active.

### Evidence
Only accept: test outputs, measured counters, logs/pcaps, deterministic reproduction, code-path reasoning tied to observations. No “likely”.

### Mandatory Validation (All Bugs)
Before marking RESOLVED, run baseline E2E scenario:
- `NTSC_default` (exact scenario name as defined in repo)
All assertions must pass.

If the agent claims a fix at any point, it must run `NTSC_default` immediately before claiming improvement.

### External Attribution
Blaming OS/network/OBS is forbidden unless all internal hypotheses are falsified AND evidence shows packets exist outside the process (for example pcap shows arrival but app does not process).

### Archival
Move resolved investigations verbatim to:
`docs/investigations/INVESTIGATION-YYYY-MM-DD.md`
Keep only a short reference here.

---

## Active Investigations

### 2026-01-09: Real-device streaming requires opening Properties UI

Status: RESOLVED

Archived: `docs/investigations/INVESTIGATION-2026-01-09.md` (Real-device streaming requires opening Properties UI)

### UDP Packet Loss in C64 Stream OBS Source
Status: RESOLVED
### 2026-01-09: Mechanical diff artifact created (e1d1ad2 → d33a10a)

- Created [doc/c64stream-udp-issue.diff](doc/c64stream-udp-issue.diff) from `git diff e1d1ad2..d33a10a -- src tests/e2e/config/obs-studio/basic/scenes/C64StreamTest.json`.
- Created [doc/c64stream-udp-issue.md](doc/c64stream-udp-issue.md) to classify the changes in that diff by likelihood and to define a step-by-step “apply hunks until it breaks” experiment order.

Observed: Severe UDP reception/logging shortfall in synthetic E2E tests. Current repro on `test/av-sync-instrumentation` HEAD: receiving/logging 9,611/30,803 packets with a ~3.4s network span (expected ~8.0s). Severe regression from last known good CI build (https://github.com/chrisgleissner/c64stream/actions/runs/20816649924).

#### Activity Log
- 2026-01-09: CONFIRMED regression cause (mechanical A/B): enabling only the "white frame detection" code path in `c64_render_frame_direct()` reproduces the UDP packet logging loss + short network span in `ntsc_default`.
  - Baseline (`e1d1ad2` in `wt_exp`): PASS `30803/30803` packets, span `~8023ms`.
  - With ONLY the white-detection block enabled (no other hunks): FAIL `13530/30803` packets, span `~3524ms`.
  - After reverting that single hunk: PASS again `30803/30803` packets, span `~8023ms`.
  - Interpretation: this debug-only path runs in E2E because `tests/e2e/properties_e2e_{local,ci}.ini` sets `debug_logging=true`, and the implementation does a full-frame pixel scan plus multiple `C64_LOG_INFO` calls per frame.
- 2026-01-09: Implemented fast debug pop detection on current HEAD.
  - Video: replace full-frame scan with tiny probes (no per-frame logging).
  - Audio: replace full buffer scan with sparse peak-amplitude probe and remove per-packet debug logging.
  - Local validation: `./local-build.sh linux --install --e2e=ntsc_default` now completes with correct network span (~8s) and near-full packet logging (latest observed: 34 packets missing).
  - Next step: reduce debug-path overhead further to eliminate packet loss completely (goal: `30803/30803`).
- 2026-01-09: Investigation started. Initial observation: 16% loss (16,249/19,252 packets in 300-frame test).
- 2026-01-09: Discovered static variable persistence bug causing session accumulation (84 time resets found, 30,804 packets across multiple sessions).
- 2026-01-09: Fixed static variable bug (commit pending) - moved `last_video_packet_us` and `last_audio_packet_us` to context struct.
- 2026-01-09: Session accumulation fixed (verified: single session now has correct packet count without resets).
- 2026-01-09: Loss persists post-fix. New test: 13,493/30,803 packets (44% loss, 480 frames, 8.0s duration).
- 2026-01-09: Verified UDP system settings correct: rmem_default=2MB, rmem_max=8MB, netdev_max_backlog=5000.
- 2026-01-09: Reproduced current failure: `./local-build.sh linux --install --e2e=ntsc_default` logs 9,611/30,803 packets; network span ~3.4s.
- 2026-01-09: Found strong evidence of retry-thrashing during the initial no-packet window: OBS log shows repeated "No video packets for X.Ys ... recreating UDP sockets" every second before replay starts.
- 2026-01-09: Implemented mitigation: add `last_start_command_time_ns` tracking + 15s initial grace before scheduling no-packet retries when no video has ever been received.

#### Hypotheses
- H1 UDP receive buffer exhaustion
  - Status: falsified (partial)
  - Scope: UDP socket init + recv loop, system UDP buffers
  - Evidence: System buffers adequate (rmem_default=2MB, rmem_max=8MB, netdev_max_backlog=5000). Plugin sets SO_RCVBUF=4MB.
  - Note: Need to check `/proc/net/snmp` for RcvbufErrors to confirm no kernel drops

- H2 Static variable persistence causing incorrect packet accounting
  - Status: confirmed + fixed (but loss persists)
  - Scope: `src/c64-record-network.c` lines 77, 131
  - Evidence: Found `static uint64_t last_video_packet_us` and `last_audio_packet_us` never reset between sessions. Caused 84 time resets in network.csv. Multiple test sessions accumulated into single CSV file.
  - Fix: Moved variables to `struct c64_source` context, reset in `c64_network_write_header()`
  - Result: Session accumulation eliminated, but packet loss persists

- H3 CSV truncation affecting packet count validation
  - Status: falsified
  - Scope: `tests/e2e/e2e.py` CSV file handling
  - Evidence: CSV truncation (DEFAULT_CSV_MAX_ROWS=2000) happens AFTER network analysis. Original counts stored in `self._original_csv_counts` before truncation. Validation uses original counts, not truncated CSVs.
  - Conclusion: CSV truncation is cosmetic, does not affect packet reception or validation

- H4 udp_replay sender not sending all packets
  - Status: falsified
  - Scope: `tests/e2e/util/udp_replay.c` packet transmission
  - Evidence: Verified udp_replay reports 19,252 packets sent with 0 send errors in earlier test. Timing correct (278us intervals).
  - Conclusion: Sender is working correctly

- H5 Plugin receive loop dropping packets (thread scheduling/backpressure/processing)
  - Status: confirmed (via H9)
  - Scope: `src/c64-network.c` UDP receive thread, `src/c64-video.c` packet processing
  - Falsification: Instrument raw recv counts vs processed/logged counts; check whether loss is at socket level vs after buffering.
  - Note: Current evidence points more strongly at retry/socket-recreate behavior than pure scheduling.

- H9 White-frame detection debug path overwhelms receiver
  - Status: confirmed (PRIMARY CAUSE for `e1d1ad2..d33a10a` regression)
  - Scope: `src/c64-video.c` `c64_render_frame_direct()` + `c64_debug_frame_is_all_white()`
  - Evidence: Mechanical A/B in `wt_exp` isolates the issue to this single hunk; enabling it reproduces the short span + packet logging loss, reverting restores full reception.
  - Fix direction: Make debug pop detection O(1) per frame (tiny pixel probe), and avoid per-frame `C64_LOG_INFO` spam.

- H8 Aggressive no-packet retry recreates sockets during initial startup
  - Status: active (PRIMARY SUSPECT)
  - Scope: `src/c64-video.c` retry trigger, `src/c64-source.c` retry task + `c64_start_streaming()` socket teardown/recreate
  - Evidence: OBS log repeatedly prints "No video packets for N seconds ... recreating UDP sockets" before replay begins. This repeatedly stops threads, closes sockets, recreates sockets, and re-sends START commands.
  - Expected effect: When UDP replay begins, sockets/threads may be mid-restart, causing sustained packet loss and shortened reception window.
  - Mitigation implemented: track `last_start_command_time_ns` and apply an initial grace period before scheduling no-packet retries when we have never received any video packets.

- H6 Packet validation/filtering discarding valid packets
  - Status: active
  - Scope: Packet header validation, sequence checking, filtering logic
  - Falsification: Log all packet rejections with reasons. Compare raw socket receive count vs validation pass count.

- H7 Test infrastructure issue (timing/burst rate overwhelming receiver)
  - Status: active
  - Scope: `tests/e2e/e2e.py` packet replay timing, burst intervals
  - Evidence: Test sends 30,803 packets over 8 seconds = ~3850 packets/second average. Real C64U sends ~3400 pkt/s (PAL) or ~4080 pkt/s (NTSC). Test rate is within spec.
  - Falsification: Compare reception rate against real hardware E2E test. If real hardware has same loss, issue is in plugin not test harness.

#### Evidence Log

**Test Results (Current Build - Post Static Variable Fix):**
- Scenario: `ntsc_default` (480 frames, 8.0 seconds, NTSC format)
- Expected: 30,803 packets (28,800 video + 2,003 audio)
- Received (latest repro): 9,611 logged packets (8,745 video, 850 audio)
- Receiver counters at last logged packet (from network.csv fields): ~12,205 video packets received, 849 audio packets received
- Network duration: 3400.7ms (first→last logged packet)
- Session files: Clean single session, no accumulation, no time resets

**Test Results (Pre-Fix):**
- Scenario: `ntsc_default` (300 frames, 5.0 seconds)
- Expected: 19,252 packets
- Received: 16,249 packets (84% reception, 16% loss)
- Session corruption: 84 time resets, 30,804 total packets across sessions

**System Configuration:**
- OS: Ubuntu 24.04.3 LTS, kernel 6.14.0-37-generic
- UDP buffers: rmem_default=2097152 (2MB), rmem_max=8388608 (8MB)
- netdev_max_backlog: 5000 packets
- Plugin SO_RCVBUF: attempts 16MB, fallback 4MB (Linux)

**Source Code:**
- Bug location: `src/c64-record-network.c:77,131` (static variables)
- Fix location: `src/c64-types.h:183-184` (added context fields), `src/c64-record-network.c:33-34` (reset in header write)

#### Open Questions
1. Why did packet loss WORSEN after static variable fix (16% → 56%)?
2. Is the plugin receive loop actually calling recvfrom() for all arriving packets?
3. Are packets being filtered/rejected at validation stage?
4. Does loss occur during network reception or later in processing pipeline?
5. What is the actual receive rate (packets/second) vs expected?
6. Do real hardware E2E tests show similar loss patterns?
7. Does suppressing initial retry thrash restore full reception in synthetic E2E?

#### Next Steps (Priority Order)
1. Add instrumentation to count raw recvfrom() calls vs successful packet processing
2. Check `/proc/net/snmp` for RcvbufErrors before and after test
3. Run real hardware E2E test (`tests/e2e/real-device-av-sync.sh`) for comparison
4. Add packet rejection logging to identify filtering issues
5. Profile receive thread CPU usage and scheduling latency
6. Compare packet loss pattern: random vs systematic (specific sequences/frames)

#### Validation (Specific to This Investigation)
Must pass with >95% packet reception + all assertions:
1) Baseline (global): `tests/e2e/e2e.sh --scenario ntsc_default` (exact command)
2) Synthetic debug: `tests/e2e/e2e.sh --scenario ntsc_default_debug` (exact command)
3) Real hardware: `tests/e2e/real-device-av-sync.sh --duration 120` (exact command)

All three tests must show >95% packet reception with zero RcvbufErrors.

---

## Resolved Investigations
None.
