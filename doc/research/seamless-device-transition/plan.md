# Seamless Device Transition — Implementation Plan

**Companion to:** [`research.md`](./research.md) — read it first; it carries the evidence and the
rejected alternatives.
**Status:** READY TO IMPLEMENT
**Date:** 2026-07-14

---

## Goal

Let a user with several Ultimate devices switch between them from a dropdown, without retyping
hosts, without leaving the previous device streaming, and using the REST API where the device
supports it — falling back to the legacy transports where it does not.

Delivers approach **B (device registry)** with **A**'s teardown mechanism, **D**'s ingest filter,
**E**'s discovery half (scan), and the two REST migrations.

**Not built:** approach C (lease-based streaming — REST carries no `duration`; shelved). E's
cross-source broker (recorded as a known gap; file an issue).

---

## Binding Constraints

From the owner, non-negotiable:

1. **KISS and DRY.** Explicitly requested. See [DRY Ledger](#dry-ledger) — this codebase already has
   four hand-rolled `.ini` parsers and two near-duplicate control-command paths. Do not add a fifth
   or a third.
2. **Device profiles carry network settings only.** No effects, no recording paths.
3. **Per-device passwords.** Two devices may have different passwords. The registry entry owns the
   password *reference*.
4. **Password storage is split.** Registry `.ini` = network settings, shareable. Password = OBS
   source settings, keyed by device id. **Never write a password to the `.ini`** — that would
   silently reverse the deliberate decision at `src/ui/c64-properties.c:2471`.
5. **Backwards compatibility is mandatory** for both REST migrations. A device that does not support
   an endpoint must keep working exactly as it does today.
6. **`403` never triggers fallback.** Port 64 has no auth; falling back from a `403` is an
   authentication bypass. Surface it.

---

## Established Facts

Verified against real hardware (`c64u`, `u64`) — do not re-litigate these:

| Fact | Consequence for this plan |
|---|---|
| Cross-transport teardown works both ways, both product lines | No session pinning. Mid-session demotion is safe. |
| `c64u` fw `1.2.0`, `u64` fw `3.15`; `/v1/version` = `0.1` on both | **Version gating is impossible.** Probe capability, never infer it. |
| `unique_id` present on both (`5D4E12`, `38C1BA`) | Registry keys on `unique_id` when available. |
| Held keys persist until released | `release_all` on teardown is **mandatory**, not hygiene. |
| `machine:input` needs `Content-Type: application/json` | Existing client does not set it on writes. |
| A second stream start replaces the destination | Reconnect to same device is clean. Does **not** help the two-device case. |
| Neither device returns `501` | The demotion path has no hardware trigger — **must** be covered by the mock server. |

---

## Phases

Ordered by dependency. Each phase is independently shippable and independently testable.

### Phase 0 — REST status plumbing (prerequisite)

**Why first:** nothing else can be correct. `http_request_ex` collapses `404`/`501`/`403`/`400`/
timeout to one `bool` (`src/network/c64-rest-client.c:610-617`), so no caller can choose a reaction.

**KISS choice:** do **not** re-sign the ~40 public functions in `c64-rest-client.h`. Mirror the
existing `error_msg` pattern — store the status on the client, expose one accessor. A `CURL` easy
handle is not thread-safe, so a client is already single-threaded by construction; a `last_status`
field is exactly as safe as the `error_msg` field beside it.

**Changes** — `src/network/c64-rest-client.c` / `.h`:

```c
typedef enum {
    C64_REST_OK,            // 2xx
    C64_REST_NOT_SUPPORTED, // 404, 501 — fall back
    C64_REST_FORBIDDEN,     // 401, 403 — auth. DO NOT fall back
    C64_REST_BAD_REQUEST,   // 400    — our bug. Log loudly, do not fall back
    C64_REST_UNREACHABLE,   // transport error — device down; fallback will also fail
} c64_rest_outcome_t;

long c64_rest_get_last_status(const c64_rest_client_t *client);      // raw HTTP code, 0 if none
c64_rest_outcome_t c64_rest_get_last_outcome(const c64_rest_client_t *client);
```

- Set both in `http_request_ex` where `http_code` is already fetched.
- Set `C64_REST_UNREACHABLE` on the `curl_easy_perform` failure path.
- Add `Content-Type: application/json` for JSON bodies (required by `machine:input`).

**Tests:** unit-test `classify(status)` for 200/400/401/403/404/500/501/0.

**Acceptance:** existing REST tests pass unchanged; callers can distinguish all five outcomes.

---

### Phase 1 — Ingest ownership filter (approach D)

**Why now:** independent of everything else, ~40 lines, ships immediately. Closes the race window
every explicit-teardown design has. Retires dead code that currently misleads readers into thinking
ingest is already protected.

`expected_peer_ip` is populated at `src/c64-source.c:613, 1496, 1654` and **never read**. Both
receivers capture the sender and ignore it.

**Changes:**

- `src/video/c64-video.c` — `recvmmsg` batch path uses `addrs[i]` (already filled at line 932); the
  `recvfrom` fallback uses `sender_addr` (line 999).
- `src/audio/c64-audio.c` — `sender_addr` at line 43.
- One shared inline helper (**DRY** — do not write the check twice):

```c
// c64-protocol.h
static inline bool c64_packet_from_expected_peer(const struct c64_source *ctx,
                                                 const struct sockaddr_in *from);
```

- Drop mismatches; count them in a diagnostic counter; log at DEBUG with rate limiting (a rogue
  device at 4000 pkt/s must not flood the log — see the OBS log-dedup memory).
- Fail **open** when `expected_peer_ip_set == false` (unresolved DNS, non-IPv4): accept, as today.
  Never let this filter black out a working stream.

**Tests:** unit — two synthetic senders, assert only the expected peer's packets are assembled.

**Acceptance:** with two devices streaming, video from the non-selected device is not assembled; the
drop counter is non-zero.

---

### Phase 2 — Device registry (approach B) + per-device passwords

The main work. Model devices as objects; route every change through one activation function.

**New module** `src/device/c64-device.{h,c}`, modelled on the palette registry
(`src/video/c64-palette.h`) — same shape, same idioms, same user-directory persistence.

```c
typedef struct {
    char id[64];          // stable key: unique_id when known, else slug of host
    char name[64];        // display name; defaults from product/hostname
    char host[64];        // hostname or IP as entered
    char dns_server_ip[64];
    uint32_t video_port, audio_port, control_port;
    // NO password here — see storage split below.
    // NO effects/recording — network settings only.
} c64_device_t;

bool c64_device_registry_init(void);
void c64_device_registry_cleanup(void);
void c64_device_registry_populate_list(obs_property_t *prop);   // mirrors c64_palette_populate_list
const c64_device_t *c64_device_registry_get(const char *id);
bool c64_device_registry_upsert(const c64_device_t *device);
bool c64_device_registry_delete(const char *id);
```

**Persistence:** one `.ini` per device under `c64_get_user_dir(C64_USER_DIR_SETTINGS, ...)` — the
directory already exists (`src/util/c64-file.h:39`).

**Password storage (constraint 4):** the password lives in OBS source settings under a per-device
key, e.g. `device_password.<id>`. The registry `.ini` never contains it. This preserves the existing
posture (`src/ui/c64-properties.c:2471`) and keeps profiles safe to attach to a bug report.

**The activation chokepoint** — this is what fixes Issue 2:

```c
bool c64_device_activate(struct c64_source *ctx, const char *device_id)
{
    if (active && active != next) {
        c64_stream_control_stop(ctx, active, STREAM_VIDEO);   // farewell — Phase 3
        c64_stream_control_stop(ctx, active, STREAM_AUDIO);
        c64_keyboard_release_all(ctx, active);                // Phase 4; mandatory (held keys)
    }
    // teardown local sockets/threads, swap active, start against next
}
```

The outgoing device's endpoint is a live object, not a string about to be `strncpy`'d over — the
teardown target cannot be wrong.

**UI** (`src/ui/c64-properties.c`): `Device` dropdown (`OBS_COMBO_TYPE_LIST`, populated exactly as
palette/keymap are at lines 1828/1980), plus Save / Rename / Delete, plus a password field bound to
the selected device. Keep `c64_host` visible for manual entry of a device not yet saved.

**Migration:** on first load with a non-empty legacy `c64_host` and an empty registry, create a
`Default` device from the existing settings and select it. Move the existing `c64_password` to that
device's key. **Must be right first time** — getting it wrong silently orphans a working config.
Test explicitly.

**Cleanup enabled:** `c64_update`'s snapshot-and-strcmp change detection
(`src/c64-source.c:1427-1450`) mostly collapses into a device-id comparison. Do it — it is the debt
this phase pays down.

**Tests:** unit — registry round-trip, id derivation, migration from legacy settings, delete. E2E —
two mock devices, switch, assert stop went to the first and start to the second.

**Acceptance:** switching devices via the dropdown sends stop to the old device and start to the new;
no retyping; passwords survive a switch and differ per device.

---

### Phase 3 — Transport abstraction + REST stream control (Task 2)

**The DRY core of this plan.** One interface; callers never branch on transport:

```c
// src/network/c64-stream-control.{h,c}
bool c64_stream_control_start(struct c64_source *ctx, const c64_device_t *dev,
                              c64_stream_id_t stream, const char *dest /* "IP:PORT" */);
bool c64_stream_control_stop(struct c64_source *ctx, const c64_device_t *dev,
                             c64_stream_id_t stream);
```

Internally: consult capability → try REST → classify outcome → maybe demote → maybe retry legacy.
**Every existing call site loses its transport knowledge**, including the proactive disconnect at
`src/c64-source.c:1645`.

**Mapping** (verified on hardware):

| Legacy (port 64) | REST |
|---|---|
| `FF20` + duration + `"IP:PORT"` | `PUT /v1/streams/video:start?ip=<IP:PORT>` |
| `FF21` + duration + `"IP:PORT"` | `PUT /v1/streams/audio:start?ip=<IP:PORT>` |
| `FF30` / `FF31` | `PUT /v1/streams/{video,audio}:stop` |

The `dest_str` the plugin already builds (`src/network/c64-protocol.c:143-168`) is reusable verbatim
as `ip=`. **Extract that construction — do not duplicate it.**

**Legacy path parameterisation (approach A's mechanism):**

```c
void c64_send_control_command_to(const char *host, uint32_t control_port,
                                 bool enable, uint8_t stream_id, const char *dest);
```

`c64_send_control_command` becomes a thin wrapper passing `context->ip_address` — **the ambient-state
read that caused Issue 2 now exists in exactly one place** instead of three.

**Capability negotiation** — shared, per-endpoint, per-device (`c64_device_caps_t` on the device):

- Seed at activation: `GET /v1/info` (also yields `unique_id`, `product`, `hostname`).
- `C64_REST_NOT_SUPPORTED` (404) → demote for the session.
- `C64_REST_NOT_SUPPORTED` (501) → demote **with expiry** (retry after N minutes); it is an FPGA
  state, not a firmware fact.
- `C64_REST_FORBIDDEN` → **do not demote**; surface an auth error.
- `C64_REST_UNREACHABLE` → **do not demote**; the device is down and legacy will fail too.
- Setting: `Stream control transport` = `Auto` (default) / `Force REST` / `Force Legacy`.

**No session pinning** — hardware-verified as safe.

**Bonus:** REST returns a status, so `c64_device_activate` can *confirm* the farewell landed. Legacy
cannot: `src/network/c64-protocol.c:182` casts the stop `send` result to `(void)`. Warn the user when
a REST stop fails.

**Tests:** unit — negotiation table, all five outcomes. E2E — mock returns `404`/`501`/`403`, assert
fall back / fall back-with-retry / raise respectively. Real hardware — switch between `c64u` and
`u64` (different firmware lines, same behaviour expected).

**Acceptance:** streaming starts and stops via REST on both devices; forcing legacy still works; a
mock returning `501` falls back transparently and retries later; a `403` surfaces an error and does
**not** fall back.

---

### Phase 4 — REST keyboard input (Task 1)

**Not a transport swap — a model translation.** Current: poll `$00C6`, write ≤10 PETSCII bytes to
`$0277`, ~5 round-trips (`src/ui/c64-keyboard.c:1076-1129`). New: matrix-level press/release/tap,
≤8 inputs/event, ≤64 events per POST, one round-trip.

**Translation** of the three keymap output modes (`src/ui/c64-keyboard.h:25-29`):

| Mode | Strategy |
|---|---|
| `C64_OUTPUT_SYMBOLIC` (`c64:RETURN`) | Direct name → `KeyboardInput` enum table |
| `C64_OUTPUT_PETSCII` (`petscii:0x41`) | Decompose → key + shift |
| `C64_OUTPUT_TEXT` (`text:"HI"`) | Decompose per char, then batch |

**The transliteration table is the work:** ~96 PETSCII entries → `{key, needs_shift}`. `!` is
`left_shift`+`1`; `A` is `left_shift`+`a`. Emit as a single `tap` with both inputs (verified working:
`{"inputs":["left_shift","a"],"transition":"tap"}` → `200`). **One table, used by both PETSCII and
TEXT modes** — DRY.

**Fallback is at batch granularity, never per-keystroke** — a batch split across matrix and KERNAL
buffer would interleave out of order. On demotion, retry the whole batch on the legacy path.

**The fallback is not feature-equivalent.** Held keys, joystick, and `restore` have no legacy
representation. When requested on a demoted device: log a specific reason and surface it in the UI
for user-initiated actions. **Never silently drop** — "nothing happened and nothing was said" is the
worst outcome.

**Mandatory `release_all`** (hardware-verified: `press b` stays held indefinitely) on stream
teardown, device switch, and source destroy. A crash mid-hold leaves a key down on the device.

**Error surfacing:** `machine:input` returns precise per-event errors —
`400 {"errors":["events[0]: 'not_a_key' is not a valid keyboard input."]}`. Log verbatim; do not
flatten to "input failed".

**Tests:** unit — transliteration table (every printable PETSCII round-trips to the right key+shift),
batch chunking at the 64-event limit. E2E — mock `501` → legacy fallback; real hardware — type a
string on both devices and read back via `GET /v1/machine:input`.

**Acceptance:** typing works on both devices via REST; forcing legacy still types; held keys and
`restore` work via REST and report clearly when unavailable; no key is ever left stuck.

---

### Phase 5 — Scan button (approach E's discovery half)

**Port, do not invent.** `/home/chris/dev/c64/c64commander/android/app/src/main/java/uk/gleissner/c64commander/DeviceDiscoveryPlugin.kt`
is battle-tested; its comments encode real incidents. Reproduce the algorithm and its guardrails:

- Probe `GET http://<host>:80/v1/info`; accept only if `product` contains `"ultimate"`
  (case-insensitive) or equals `"c64u"`.
- Enumerate site-local IPv4 subnets from local interfaces; skip down/loopback/virtual.
  **Clamp prefix to `[24, 30]`** — the guardrail that stops a `/16` becoming a 65k sweep. Skip
  network, broadcast, own address.
- Probe known hosts alongside the sweep; dedupe; tag source `hostname` vs `lan-scan`.
- **Bounded concurrency:** 24 workers (clamp 1–64); overall timeout 8s (1–30s); per-probe
  connect/read timeout 650ms (200ms–5s).
- **`401`/`403` = present but password-protected → surface as a candidate requiring a password, do
  not drop.** Accept `403` only if the body parses as `{"errors":[...]}` (generic proxies return
  `403` freely and would pollute the list).
- **Resolve the IP only after a successful connect** — a pre-connect `getByName()` on a name that
  never answers blocks a worker on a non-interruptible DNS call. `u64`/`c64u` are exactly that case.
- **Release the connection in all paths** — their HARD9-076 was an FD leak per probe, accumulating
  across rescans.

**UI:** a `Scan` button that populates/refreshes the device dropdown. Must run **off** the OBS UI
thread (the codebase is strict about this — see `src/c64-source.c:1490`, "do not do DNS resolution in
`c64_update`"). Reuse the existing async-task pattern.

**Tests:** unit — subnet enumeration incl. the `/16` clamp, product matching, error-envelope
detection. E2E — mock devices discovered; a `403` mock appears as password-required.

**Acceptance:** Scan finds `c64u` and `u64` on the real network within the timeout, names them from
`product`/`hostname`, and a password-protected device appears as requiring a password rather than
vanishing.

---

## DRY Ledger

The user asked for DRY. These are the specific duplications to avoid creating — and the one to fix:

| Item | Rule |
|---|---|
| `.ini` parsing | **Four hand-rolled parsers already exist** (`c64-palette.c:665`, `c64-properties.c:2625`, `c64-properties.c:3572`, `c64-keyboard.c:596`). **Do not write a fifth.** Extract a minimal `c64_ini_foreach(path, cb, ctx)` into `c64-file.c`, use it for the registry. Retrofitting the existing four is **optional follow-up, not this plan's scope.** |
| Transport branching | Exactly one place: `c64-stream-control.c`. No caller branches on REST vs legacy. |
| Ambient `ip_address` reads | Exactly one: the `c64_send_control_command` wrapper. Everything else passes an explicit endpoint. |
| Destination string | Extract from `c64-protocol.c:143-168`; used by both transports. |
| Peer-IP check | One inline helper, used by video and audio receivers. |
| PETSCII→matrix table | One table, used by both PETSCII and TEXT modes. |
| Capability negotiation | One implementation, shared by stream control and keyboard. |
| Registry shape | Mirror `c64-palette.h`. Do not invent a new registry idiom. |

---

## Risks

| Risk | Mitigation |
|---|---|
| **Migration orphans a working config** | Highest-blast-radius item. Explicit unit test; migrate only when registry is empty and `c64_host` is non-empty; never delete legacy keys in the same release. |
| **Peer filter blacks out a working stream** | Fail open when `expected_peer_ip_set == false`. Ship Phase 1 alone first. |
| **`501` path never exercised** (no hardware trigger) | Must be covered by `tests/e2e/mock_c64u_server.py`. Do not ship the negotiation untested. |
| **Password leaks into a shareable `.ini`** | Constraint 4. Add a test asserting no `.ini` under `settings/` contains a password key. |
| **Stuck keys after a crash** | `release_all` on every teardown path. |
| **E2E stop-assertions give false negatives** | **Flush the socket buffer before measuring** — this bit us during research (a 97% packet drop read as "still streaming"). Any "did it stop?" assertion must drain first. |
| **Scan floods a large network** | Prefix clamp `[24,30]`, bounded concurrency, hard timeout. |
| **Scan blocks the OBS UI thread** | Async task; the codebase already forbids DNS on the UI thread. |

---

## Out of Scope

- **Approach C (lease-based streaming)** — shelved; REST has no `duration`.
- **E's cross-source broker** — two sources selecting the same device still collide. **File an
  issue**; do not build.
- **Retrofitting the four existing `.ini` parsers** — optional follow-up.
- **`debug` stream** — exists in the enum, unused, undocumented.
- **Effects/recording in device profiles** — explicitly excluded. KISS.

---

## Definition of Done

- [ ] Switching devices via the dropdown stops the old device and starts the new one — verified with
      `c64u` and `u64` both streaming.
- [ ] No host is ever retyped after first save; Scan finds both real devices.
- [ ] Per-device passwords work and differ; no password appears in any `.ini`.
- [ ] REST used for stream control and keyboard on both devices; `Force Legacy` still works.
- [ ] Mock `501` → transparent fallback + later retry; mock `403` → surfaced error, **no** fallback.
- [ ] Held keys, joystick, and `restore` work; no key left stuck after switch/crash/destroy.
- [ ] Existing legacy behaviour unchanged for a device without REST support.
- [ ] `./build --tests --script-tests` green; `./build-aux/run-clang-format --check` clean.
- [ ] `README.md` and `doc/` updated (per `AGENTS.md`).
