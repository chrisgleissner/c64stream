# Handover — Seamless Device Transition (Phases 2–5)

**For:** the LLM continuing this implementation.
**Branch:** `feat/seamless-device-transition`
**Date:** 2026-07-14
**State:** Phases 0 and 1 are **complete, tested, formatted, and committed**. Phases 2–5 remain.

---

## Read these first, in this order

1. [`plan.md`](./plan.md) — what to build, phase by phase. **The source of truth for scope.**
2. [`research.md`](./research.md) — the evidence and rejected alternatives. **Do not re-litigate its decisions.**
3. [`prompt.md`](./prompt.md) — the original task brief with non-negotiable constraints.
4. `AGENTS.md` (repo root) — guardrails that apply in full.
5. This handover — what's already done and what decisions the prior sessions made.

---

## What is DONE (committed)

Two commits on `feat/seamless-device-transition`:

### Phase 0 — REST status plumbing (commit `892cf8a`) ✅
- Added `c64_rest_outcome_t` enum to `src/network/c64-rest-client.h`:
  `OK` (2xx) · `NOT_SUPPORTED` (404/501/**and other unexpected codes incl. 500**) · `FORBIDDEN` (401/403) · `BAD_REQUEST` (400) · `UNREACHABLE` (transport / status 0).
- `c64_rest_classify_status(long)` is **exposed** (not static) so it is unit-testable.
- `c64_rest_get_last_status()` / `c64_rest_get_last_outcome()` accessors mirror the existing `error_msg` pattern.
- `http_request_ex()` sets `last_status`/`last_outcome` on every path: default `UNREACHABLE` at the top, `OK` on the reset-close-accepted path, transport failure stays `UNREACHABLE`, and the real HTTP code is classified after `curl_easy_getinfo`.
- `Content-Type: application/json` is now appended to headers **whenever a request body is present** in `http_request_ex` (required by `machine:input`; no current caller sends a body, so zero behaviour change).
- **Judgment call (documented in code):** `500` and all other non-auth/non-400 codes map to `NOT_SUPPORTED` (fallback-eligible). Rationale: they are neither an auth refusal nor a payload bug, so falling back to legacy is the robust choice and **never bypasses authentication**. The raw `last_status` is still available for Phase 3 to distinguish permanent (404) vs retry (501) demotion.
- Test: `tests/network/test_c64_rest_outcome.c` (classifies 200/201/204/400/401/403/404/500/501/502/405/429/0 + NULL-safe accessors).

### Phase 1 — Ingest ownership filter (commit `61cb6d4`) ✅
- New `src/network/c64-ingest-filter.h` with a shared **pure** `static inline bool c64_packet_from_expected_peer(const struct c64_source *ctx, const struct sockaddr_in *from)`. Fails **open** when `expected_peer_ip_set == false` (never blacks out a stream); compares network-byte-order `from->sin_addr.s_addr == ctx->expected_peer_ip`.
- Wired into **both** receivers: `src/video/c64-video.c` (Linux `recvmmsg` batch loop using `&addrs[i]` **and** the non-Linux `recvfrom` path using `&sender_addr`) and `src/audio/c64-audio.c` (`&sender_addr`).
- New diagnostic counter `debug_packets_dropped_peer` on `struct c64_source` (`src/util/c64-types.h`). Drops are counted + logged at DEBUG with a 1-in-1024 throttle (`& 0x3FF`) so a rogue device at ~4000 pkt/s can't flood the log.
- **Placement note (important for DRY ledger fidelity):** the plan suggested `c64-protocol.h` for the helper. That is **impossible** here because `c64-types.h` includes `c64-protocol.h` *before* defining `struct c64_source`, so an inline body there cannot dereference the struct's members. The helper lives in a self-contained header that includes `c64-types.h`. The DRY intent (one shared check, not duplicated) is preserved.
- Test: `tests/network/test_c64_ingest_filter.c` (fail-open, match/drop, NULL safety).

### Build/test state
- `./build --tests --script-tests` green. `ctest` = **26/26 passing** (was 24 at baseline; +`c64_rest_outcome`, +`c64_ingest_filter`).
- `./build-aux/run-clang-format --check` clean.
- **Do not run E2E in CI/cloud shells** — local GUI machine only (AGENTS.md).

---

## What REMAINS

| Phase | Deliverable | Plan section |
|---|---|---|
| **2** | Device registry (`src/device/c64-device.{h,c}`) + per-device passwords + the `c64_device_activate` chokepoint + migration from legacy `c64_host` | plan §Phase 2 |
| **3** | Transport abstraction (`src/network/c64-stream-control.{h,c}`) + REST stream control with fallback; parameterised `c64_send_control_command_to(host,port,…)`; capability negotiation (`c64_device_caps_t`) | plan §Phase 3 |
| **4** | REST keyboard input (`POST /v1/machine:input`) + PETSCII→matrix transliteration table + batch chunking + not-feature-equivalent fallback | plan §Phase 4 |
| **5** | Network scan button (port C64 Commander's algorithm) populating the device dropdown, off the OBS UI thread | plan §Phase 5 |

**Build and test each phase to green before starting the next. Commit per phase** (the task author explicitly asked for per-phase commits; the repo has a pre-commit format hook that re-stages — let it).

---

## Non-negotiable constraints (from `prompt.md` — violating any fails the task)

1. **KISS and DRY.** Consult the plan's **DRY Ledger** before writing new modules. The codebase already has **four** hand-rolled `.ini` parsers and **three** ambient reads of `context->ip_address`. **Do not add to either count.**
2. **Never write a password to a `.ini` file.** Registry `.ini` = network settings only. Passwords live in OBS source settings keyed by device id (`device_password.<id>`). `src/ui/c64-properties.c:2471` records this deliberately.
3. **`403`/`401` must never trigger a fallback to legacy.** Port 64 has no auth → falling back from an auth refusal is an auth bypass. Surface the error. (Phase 0's `FORBIDDEN` outcome enforces this at the classification layer; Phase 3 negotiation must respect it.)
4. **Never infer capability from a version string.** `c64u` reports fw `1.2.0`, `u64` reports `3.15` — disjoint — and `/v1/version` returns `0.1` on both. **Probe the endpoint; classify the response.**
5. **Device profiles carry network settings only.** No effects, no recording paths.
6. **Backwards compatibility is mandatory.** A device without REST support must behave exactly as today. Verify, don't assume.
7. **`release_all` on every teardown path** (stream stop, device switch, source destroy). Held keys persist on the device indefinitely (hardware-verified). A crash mid-hold leaves a key down.
8. **Never do DNS or network I/O on the OBS UI thread.** See `src/c64-source.c:1490`. Use the existing async-task pattern.

---

## Established facts (hardware-verified — do NOT re-derive)

- **Cross-transport teardown works both ways on both product lines.** A REST stop halts a port-64-started stream and vice versa. **No session pinning needed**; mid-session demotion is safe.
- **`unique_id` exists on both** (`5D4E12`, `38C1BA`). Key the registry on it when available; fall back to a host slug when absent.
- **`machine:input` requires `Content-Type: application/json`.** (Phase 0 now sets it for bodies — but double-check any new REST write paths set it too.)
- **Held keys work:** `press b` stays held; `release b` clears it. **Chords work:** `{"inputs":["left_shift","a"],"transition":"tap"}` → `200`.
- **A second stream start replaces the destination** — reconnect to the *same* device is clean. Does **not** help the two-device case; do not treat it as a shortcut.
- **Neither device ever returns `501`** → the demotion path has **no hardware trigger** and **must** be covered by `tests/e2e/mock_c64u_server.py`.
- `GET /v1/info` yields `product`, `firmware_version`, `hostname`, `unique_id`. `GET /v1/version` returns a bare string (`"0.1"`).
- REST stream control: `PUT /v1/streams/{video|audio}:start?ip=<IP:PORT>` and `PUT /v1/streams/{video|audio}:stop`. No `duration` param (this is why approach C / lease-based streaming is shelved).

---

## The trap that will bite you (read twice)

Any test asserting "the stream stopped" **must fully flush the UDP socket buffer before measuring.** During research, a correct implementation measured as "STILL STREAMING" because ~200 packets sat in the kernel receive buffer against a ~6800 pkt/2s baseline. A 97% drop is a *drain*, not a live stream.

**Correct sequence:** stop → sleep for propagation → **drain the buffer to empty** (bounded — at ~4000 pkt/s an active stream never leaves a gap, so an unbounded drain hangs forever) → only then measure a clean window.

---

## Required test coverage (per phase)

- **Phase 2:** registry round-trip; id derivation; **legacy-settings migration** (highest blast radius — must be right first time); delete; **assert no password in any `.ini`** under `settings/`.
- **Phase 3:** negotiation table (all five Phase-0 outcomes → policy); mock `501` → transparent fallback **+ later retry**; mock `404` → fallback for the session; mock `403` → surfaced error, **no** fallback.
- **Phase 4:** PETSCII→matrix transliteration for **every printable character**; batch chunking at the 64-event limit; mock `501` → legacy fallback (whole batch, never per-keystroke).
- **Phase 5:** subnet enumeration including the `/16` → `/24` prefix clamp `[24,30]`; product matching; Ultimate `{"errors":[...]}` envelope detection; `403` appears as password-required.
- **Mock:** extend `tests/e2e/mock_c64u_server.py` for the `501`/`404`/`403` paths and two-device switch.
- **Real hardware (local GUI only):** switch between `c64u` (fw 1.2.0) and `u64` (fw 3.15) with both streaming; confirm old device stops; type on both; confirm no key stuck. **Leave devices clean afterward** (stop streams + `release_all` to both).

---

## Key files to study before each phase

- **Phase 2:** mirror `src/video/c64-palette.h`/`.c` (the registry idiom to copy); `src/util/c64-file.h:39` (`C64_USER_DIR_SETTINGS`); `src/ui/c64-properties.c` (dropdown population at ~1828/1980; export/password decision at ~2471; ~2625/3572 are existing `.ini` parsers — extract `c64_ini_foreach` into `c64-file.c`, do **not** write a 5th parser); `src/c64-source.c:1427-1500` (the change-detection god-function to collapse into a device-id comparison).
- **Phase 3:** `src/network/c64-protocol.c:143-168` (destination-string builder — **extract & reuse**, don't duplicate), `:175-217` (control command `send`, result cast to `(void)` at `:182`); `src/network/c64-protocol.h:54` (`c64_send_control_command`); `src/c64-source.c:1645` (proactive disconnect aimed at the *new* device — the core of Issue 2); Phase 0's `c64-rest-client.{h,c}` (client is already addressed per-URL — REST farewell is impossible to aim wrong).
- **Phase 4:** `src/ui/c64-keyboard.c:1076-1129` (current KERNAL-buffer polling), `:1366` (IRQ workaround); `src/ui/c64-keyboard.h:25-29` (three output modes). The transliteration table (~96 PETSCII → `{key, needs_shift}`) is the bulk of the work; **one table for both PETSCII and TEXT modes**.
- **Phase 5:** `/home/chris/dev/c64/c64commander/android/app/src/main/java/uk/gleissner/c64commander/DeviceDiscoveryPlugin.kt` — port its algorithm and guardrails verbatim (prefix clamp, bounded concurrency, resolve IP **after** connect, release connection in all paths, `401/403` = password-required candidate).

---

## How to verify your work each phase

```bash
./build --tests --script-tests          # build + unit + script tests
ctest --test-dir build_x86_64 --output-on-failure   # focused unit tests
./build-aux/run-clang-format --check    # MUST be clean before commit
```

**Gotcha:** `./build-aux/run-clang-format <file>` only accepts C/C++ source paths. **Never pass `CMakeLists.txt` to it** — it will corrupt the CMake file (it formats CMake as if it were C). The `--check` mode correctly ignores non-`.{c,cpp,h,hpp,m,mm}` files.

---

## Working method (from the task author)

- **One phase at a time.** Build, test, format, commit each before starting the next.
- **Investigate every failure to root cause.** Never skip a test or weaken an assertion to get green.
- **Update docs you touch** (`README.md`, `doc/`) before declaring a phase done.
- **If the plan turns out to be wrong, say so and stop.** Report a surprising result with evidence rather than adapting code until it passes.
- **Commit per phase.** The task author explicitly requested this.

## Definition of Done (whole feature)

- [ ] Switching devices via the dropdown stops the old device and starts the new — verified with `c64u` and `u64` both streaming beforehand.
- [ ] No host retyped after first save; Scan finds both real devices.
- [ ] Per-device passwords work and differ; **no password in any `.ini`** (assert in a test).
- [ ] REST used for stream control and keyboard on both devices; `Force Legacy` still works.
- [ ] Mock `501` → transparent fallback + later retry; mock `403` → surfaced error, no fallback.
- [ ] Held keys, joystick, and `restore` work; unavailable capabilities report clearly; no key ever left stuck.
- [ ] Legacy behaviour unchanged for a device without REST support.
- [ ] `./build --tests --script-tests` green; `./build-aux/run-clang-format --check` clean.
- [ ] `README.md` and `doc/` updated.
