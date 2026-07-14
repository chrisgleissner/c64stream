---
description: Implement seamless multi-device transition — device registry, REST stream control, REST keyboard input, and network scan — with graceful fallback to the legacy transports
---

# Implement Seamless Device Transition

Implement the plan in `doc/research/seamless-device-transition/plan.md`.

**Read first, in this order:**

1. `doc/research/seamless-device-transition/plan.md` — what to build, phase by phase.
2. `doc/research/seamless-device-transition/research.md` — why. Carries the evidence, the hardware
   test results, and the rejected alternatives. **Do not re-litigate decisions it records.**
3. `AGENTS.md` — repo guardrails. They apply in full.

---

# What You Are Building

A user with several Ultimate devices can switch between them from a dropdown: no retyping hosts, no
device left streaming after a switch, REST used where the device supports it and the legacy
transports where it does not.

Five phases, in dependency order. **Each phase is independently shippable — build and test them one
at a time, in order. Do not start a phase before the previous one is green.**

| Phase | Deliverable |
|---|---|
| 0 | REST status plumbing (prerequisite — nothing else is correct without it) |
| 1 | Ingest ownership filter (~40 lines, independent, ships first) |
| 2 | Device registry + per-device passwords + activation chokepoint |
| 3 | Transport abstraction + REST stream control, with fallback |
| 4 | REST keyboard input, with fallback |
| 5 | Network scan button |

---

# Non-Negotiable Constraints

These come from the project owner and from hardware testing. Violating any of them fails the task.

1. **KISS and DRY**, explicitly requested. Consult the plan's **DRY Ledger** before writing any new
   module. The codebase already has four hand-rolled `.ini` parsers and three places that read
   `context->ip_address` as ambient state. **Do not add to either count.**
2. **Never write a password to a `.ini` file.** Registry `.ini` = network settings only. Passwords
   live in OBS source settings keyed by device id. `src/ui/c64-properties.c:2471` records this
   decision deliberately — writing credentials into `~/Documents` would silently reverse it.
3. **`403`/`401` must never trigger a fallback to legacy.** Port 64 has no authentication, so falling
   back from an auth refusal is an authentication bypass. Surface the error instead.
4. **Never infer capability from a version string.** Hardware-verified: `c64u` reports firmware
   `1.2.0` and `u64` reports `3.15` — disjoint version spaces — and `/v1/version` returns `0.1` on
   both. Probe the endpoint; classify the response.
5. **Device profiles carry network settings only.** No effects, no recording paths.
6. **Backwards compatibility is mandatory.** A device without REST support must behave exactly as it
   does today. Verify this, do not assume it.
7. **`release_all` on every teardown path** (stream stop, device switch, source destroy). Held keys
   persist on the device indefinitely — hardware-verified. A crash mid-hold leaves a key down.
8. **Never do DNS or network I/O on the OBS UI thread.** The codebase is strict about this — see the
   comment at `src/c64-source.c:1490`. Use the existing async-task pattern.

---

# Established Facts — Do Not Re-Derive

Verified against the real `c64u` and `u64` devices during research:

- **Cross-transport teardown works both ways on both product lines.** A REST stop halts a
  port-64-started stream and vice versa. **No session pinning is needed**; mid-session demotion is
  safe.
- **`unique_id` exists** on both (`5D4E12`, `38C1BA`). Key the registry on it when available; fall
  back to a host slug when absent (it is not a required field).
- **`machine:input` requires `Content-Type: application/json`.** Omitting it yields
  `400 {"errors":["Content type should be 'application/json'."]}`. The existing client does not set
  it on writes.
- **Held keys work:** `press b` stays held indefinitely; `release b` clears it.
- **Chords work:** `{"inputs":["left_shift","a"],"transition":"tap"}` → `200`.
- **A second stream start replaces the destination** — so reconnecting to the *same* device is clean.
  This does **not** help the two-device case; do not treat it as a shortcut.
- **Neither device ever returns `501`**, so the demotion path has no hardware trigger. It **must** be
  covered by `tests/e2e/mock_c64u_server.py`.

---

# Testing

Both real devices are on the network and available for probing: **`c64u`** and **`u64`**.

Per `AGENTS.md`:

```bash
./build --tests --script-tests
./build-aux/run-clang-format --check
```

**Do not run E2E in CI or a cloud shell** — local GUI machine only.

## The Trap That Will Bite You

Any test asserting "the stream stopped" **must fully flush the UDP socket buffer before measuring**.
During research, a correct implementation was measured as "STILL STREAMING" because ~200 packets
still sat in the kernel receive buffer against a ~6800/2s baseline. A 97% drop is a *drain*, not a
live stream.

Correct sequence: **stop → sleep for propagation → drain the buffer to empty → only then measure a
clean window.** Also bound the drain — at ~4000 pkt/s an active stream never leaves a gap, so an
unbounded drain loop hangs forever.

## Required Coverage

- **Unit:** REST outcome classification (200/400/401/403/404/500/501/0); registry round-trip and id
  derivation; **legacy-settings migration**; PETSCII→matrix transliteration for every printable
  character; batch chunking at the 64-event limit; subnet enumeration including the `/16` prefix
  clamp; Ultimate error-envelope detection.
- **Mock (`tests/e2e/mock_c64u_server.py`):** `501` → transparent fallback **and** later retry;
  `404` → fallback for the session; `403` → surfaced error and **no** fallback; two mock devices for
  the switch assertion.
- **Real hardware:** switch between `c64u` and `u64` (deliberately different firmware lines —
  `1.2.0` vs `3.15`); confirm the old device stops; type on both; confirm no key is left stuck.

---

# Working Method

- **One phase at a time.** Build, test, and format each phase before starting the next. Commit per
  phase.
- **Investigate every failure to root cause.** Never skip a test or weaken an assertion to get green
  (`AGENTS.md`).
- **Update docs you touch** — `README.md`, `doc/` — before declaring done.
- **If the plan turns out to be wrong, say so and stop.** The plan is evidence-based, but evidence
  can be incomplete. A surprising result is a finding to report, not an obstacle to route around.
  Report it with the evidence rather than adapting the code until it passes.
- **Leave the devices clean.** After any hardware probing: stop streams and send `release_all` to
  both.

---

# Definition of Done

- [ ] Switching devices via the dropdown stops the old device and starts the new — verified with
      `c64u` and `u64` both streaming simultaneously beforehand.
- [ ] No host retyped after first save; Scan finds both real devices.
- [ ] Per-device passwords work and differ; **no password in any `.ini`** (assert this in a test).
- [ ] REST used for stream control and keyboard on both devices; `Force Legacy` still works.
- [ ] Mock `501` → transparent fallback + later retry; mock `403` → surfaced error, no fallback.
- [ ] Held keys, joystick, and `restore` work; unavailable capabilities report clearly rather than
      failing silently; no key ever left stuck.
- [ ] Legacy behaviour unchanged for a device without REST support.
- [ ] `./build --tests --script-tests` green; `./build-aux/run-clang-format --check` clean.
- [ ] `README.md` and `doc/` updated.
