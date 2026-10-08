# Seamless Device Transition

How the plugin lets a user switch between several C64 Ultimate devices from a dropdown,
without retyping hosts, without leaving the previous device streaming, and using the REST
API where the device supports it — falling back to the legacy port-64 protocol where it does
not.

## Device Registry (`src/device/c64-device.{h,c}`)

Device profiles are modelled the same way as the existing palette registry
(`src/video/c64-palette.h`): a small in-memory array (`C64_DEVICE_MAX` = 64 entries),
persisted as one `.ini` file per device under the user settings directory
(`c64_get_user_dir(C64_USER_DIR_SETTINGS, ...)`), guarded by a single mutex.

```c
typedef struct c64_device {
    char id[64];            // stable key: unique_id when known, else a slug of the host
    char name[64];
    char host[64];
    char dns_server_ip[64];
    uint32_t video_port, audio_port, control_port;
} c64_device_t;
```

**Passwords are never part of a device profile.** They live in OBS source settings under a
per-device key (`device_password.<id>`, built by `c64_device_password_key`), so a device's
`.ini` file is safe to attach to a bug report. `c64_device_registry_apply_selected` copies the
selected device's network fields *and* its password into the live `c64_host`/`c64_password`
settings that the rest of the plugin already reads; `c64_device_registry_migrate_legacy`
performs the reverse on first load — if the registry is empty and a legacy `c64_host` is
present, it creates a `Default` device from the existing settings, moves the password to that
device's key, and selects it.

Migration is a **one-shot per source**, latched by the `c64_device_migrated` bool in the
source's OBS settings. An empty registry cannot be the trigger on its own: the legacy
`c64_host` key is deliberately kept for a compatibility release, so an empty-registry trigger
re-fires on the very next `c64_update` and resurrects the profile the moment the user deletes
their last device. The latch is set once migration has been *considered*, whether or not it
had anything to migrate — the one exception being a failed registry write, which leaves it unset so
the next update retries.

The `Device` dropdown in `src/ui/c64-properties.c` is populated by
`c64_device_registry_populate_list`, mirroring how the palette and keymap lists are built.

## Ingest Ownership Filter (`src/network/c64-ingest-filter.h`)

Before a device switch existed, the video/audio UDP receivers accepted packets from whatever
sent them. With two devices potentially reachable on the same network, a stale device could
still write into the wrong source's frame buffer during a switch. `c64_packet_from_expected_peer`
is a single inline check, shared by the video (`src/video/c64-video.c`, both the Linux
`recvmmsg` batch path and the `recvfrom` fallback) and audio (`src/audio/c64-audio.c`) receivers,
that drops any packet whose source IP does not match `context->expected_peer_ip`.

It **fails open** when `expected_peer_ip_set` is false (unresolved DNS, non-IPv4) — the filter
must never black out a stream that was working before the feature existed. Drops are counted in
`debug_packets_dropped_peer` and logged at DEBUG with a 1-in-1024 throttle, so a rogue device
sending at full rate cannot flood the log.

## REST Outcome Classification (`src/network/c64-rest-client.{h,c}`)

Every REST call used to collapse `404`/`501`/`403`/`400`/timeout into a single `bool`. Callers
that need to react differently — retry via REST, fall back to legacy, or surface an error —
need the real status. `c64_rest_classify_status` maps an HTTP status to one of:

| Outcome | Status codes | Meaning |
|---|---|---|
| `C64_REST_OK` | 2xx | success |
| `C64_REST_NOT_SUPPORTED` | 404, 501 | endpoint absent — safe to fall back |
| `C64_REST_FORBIDDEN` | 401, 403 | authentication refusal — **never** fall back |
| `C64_REST_BAD_REQUEST` | 400 | the plugin sent a malformed request — surface, don't fall back |
| `C64_REST_SERVER_ERROR` | other 5xx | surface, don't fall back |
| `C64_REST_UNREACHABLE` | no HTTP response | device down or web server stalled — AUTO tries legacy once |

`c64_rest_get_last_status`/`c64_rest_get_last_outcome` expose the last classified result per
client, mirroring the existing `error_msg` accessor. Port 64 (the legacy control protocol) has
no authentication, so **falling back from a `403`/`401` would be an auth bypass** — this is
enforced at the classification layer, not left to each caller to get right.

## Stream Control Negotiation (`src/network/c64-stream-control.{h,c}`)

This is the single place that decides REST vs. legacy for starting/stopping a video or audio
stream — no other call site branches on transport.

```c
bool c64_stream_control_should_fallback(c64_rest_outcome_t outcome); // true only for NOT_SUPPORTED

bool c64_stream_control_to(struct c64_source *context, const char *host, uint32_t control_port,
                           bool enable, uint8_t stream_id, const char *destination);
bool c64_stream_control(struct c64_source *context, bool enable, uint8_t stream_id,
                        const char *destination);
```

`stream_id == 0` is video, `stream_id == 1` is audio. The `Stream control transport` setting
(`context->stream_control_transport`) is `Auto` (0, default), `Force REST` (1), or `Force
Legacy` (2):

- **Force Legacy** never attempts REST.
- **Force REST** attempts REST and, on failure, returns `false` unconditionally — it never
  falls back, even for an outcome that would normally be fallback-eligible.
- **Auto** attempts REST if a client exists and the device is not currently demoted, then:
  - success → done.
  - `NOT_SUPPORTED` via **404** → demote **permanently** (`stream_rest_demoted_until_ns =
    UINT64_MAX`) and fall back to legacy for this call.
  - `NOT_SUPPORTED` via **501** → demote **with a 60-second expiry** and fall back — a `501`
    is treated as FPGA/firmware state, not a fixed capability, so it is retried later.
  - `UNREACHABLE` (no HTTP answer within the 1.5-second bound for stream start/stop) → try the
    legacy command for this call only, **without** demoting REST. The Ultimate's web server
    occasionally stalls a single request while the control port still answers; without this a
    stall froze a device switch for the full five-second timeout. A device that is really down
    fails both, as before.
  - `FORBIDDEN`, `BAD_REQUEST`, `SERVER_ERROR` → **no fallback**, return `false`.
    A `403` must surface as an error, never silently retry over the unauthenticated legacy path.

`c64_stream_control` is a thin wrapper that reads `context->ip_address`/`context->control_port`;
every other caller passes an explicit host/port so a device switch can never aim the wrong
endpoint (the ambient-state bug this replaces). The negotiation table above is covered directly
by `tests/network/test_c64_stream_control.c`, which stubs the REST/legacy calls and exercises
all six outcomes plus the forced-transport and demotion-state edge cases.

## Device Transition (`src/c64-source.c`)

Switching the active device (or changing host/ports while already streaming) must never leave
the old device streaming, and must never do network I/O on the OBS UI thread. `c64_update` (the
UI-thread settings callback) only *records* that a transition is needed:

```c
if (needs_device_transition && !context->device_transition_pending) {
    context->device_transition_host = <old ip_address>;
    context->device_transition_control_port = <old control_port>;
    context->device_transition_pending = true;
}
```

It also records the previous device's password and REST demotion state, and opens a
*handover*: the previous device's address is kept in `handover_peer_ip`.

All network I/O happens off the UI thread, in the background retry worker
(`c64_async_retry_task`). A switch is **make before break**: the new device is started first
and the previous one is stopped afterwards, so neither the old device's round trips nor its
timeouts (when it is switched off or unreachable) delay the new picture.

1. `c64_take_pending_device_transition` moves the previous device into the worker's
   `pending_stops` queue, clears the REST demotion state and retargets the source's REST client
   at the new device. A switch back to a device that is still queued removes it from the queue,
   so its stop can never follow the new start.
2. `c64_start_streaming` runs with `start_is_device_switch` set:
   - a fast reachability check (`c64_test_connectivity_any`: REST port and control port connected
     concurrently, 250 ms) fails an unreachable target at once. The receivers are stopped and the
     idle logo is shown, because OBS would otherwise keep displaying the previous device's last
     frame;
   - the proactive stop of the new device is skipped (it only matters for a stale stream left from
     an earlier session);
   - the new device's video start is sent while the receivers still run with the previous
     device's picture. Only then are the sockets and threads recycled, then audio is started.
3. Until the first packet from the new device arrives, `c64_packet_admit` still admits packets
   from the handover address, so the previous picture stays live during the start round trip;
   from that packet on, the previous device's packets are dropped. The pause a viewer sees is
   the receiver restart only. Every switch logs it:
   `Device switch from HOST: first frame N ms after the request; longest pause without video M ms`.
4. The previous device is kept streaming until the new device's first packet arrives, at most
   five seconds (`C64_HANDOVER_HOLD_NS`). Meanwhile the worker repeats the new device's start
   once per second: an Ultimate 64 Elite (firmware 3.15) was seen to stall its web server about
   every five minutes and then ignore start commands for about three seconds. Without the hold
   the screen showed the logo for that time; with it, the previous picture continues until the
   new one is there. A new device that never delivers is given up on after the hold.
5. `c64_process_previous_device_stops` then stops each queued device (the queue is
   `src/device/c64-device-switch.{h,c}`, keyed by host and control port) with a REST client of its own
   (300 ms cap per request): `release_all` (held keys must never survive a switch), then video
   and audio stop, with the usual legacy fallback on `404`/`501` only. A newer switch request
   interrupts it between requests; a device that does not confirm is retried with backoff up to
   five times.

Measured on a C64 Ultimate and an Ultimate 64 Elite on the same LAN (see
`doc/testing/discovery-hil.md`): the pause without video is typically 30-60 ms, and switching
back from an unreachable or powered-off device to a live one shows the live device in under
0.5 s.

### Retry worker

The worker (`c64_retry_thread_main`) keeps running until the source streams and no previous
device is waiting to be stopped. Only the receive threads detect a stalled stream, and they run
only while streaming, so a start that fails because the device is off would otherwise never be
retried. Attempts back off from 1 s to 5 s and are woken at once by a new request
(`retry_requested`): a settings change that arrives while an attempt is running is never dropped.

### Sender verification

An Ultimate answers REST and the control port on every interface, but the interface its A/V
leaves from is not always the one it was addressed on (the C64 Ultimate streams from its wired
port; an Ultimate 64 Elite was seen streaming from Wi-Fi under packet loss). When a started
stream produces no video while the ingest filter keeps rejecting one sender, the retry worker
asks both addresses for their `/v1/info` `unique_id` (`c64_try_adopt_rejected_sender`). Only the
same physical device is accepted (`learned_peer_ip`) and its address is stored as the profile's
`peer_host`; any other sender stays dropped.

`c64_abort_stream_start` sends `release_all` and stops both streams if a remote start partially
succeeds and a local worker then fails, so a failure mid-transition can't leave a key held or a
stream running on a device OBS no longer thinks is active.

## Device Scan (`src/device/c64-device-scan.{h,c}`)

**Find Devices** (`src/ui/c64-properties.c`) and the startup scan discover devices without the
user typing an IP:

- Probes every registry host and the configured `c64_host` first (retried up to three times, so
  a slower Wi-Fi interface is not lost to one timeout), then every address of each up,
  non-loopback, non-point-to-point IPv4 interface.
- **Subnets are ordered**: the interface carrying the default route first, then RFC 1918
  networks, then others, link-local last (`c64_device_scan_subnet_rank`). Host enumeration is
  capped at 1024 addresses, so on a machine with Docker, libvirt, Hyper-V or VPN adapters the
  user's LAN is never the part that gets cut.
- **Subnet prefix is clamped to `[24, 30]`** (`c64_device_scan_enumerate_subnet`).
- The worker pool is sized from the host count (48 to 128, `c64_device_scan_worker_count`) so
  every enumerated address is probed within the 12-second deadline; each scan logs
  `DEVICE: discovery probed X of Y addresses ...` and warns if the deadline cut it short.
- An address qualifies when `GET /v1/info` (650 ms) reports a streaming-capable `product`
  (`Ultimate 64` family or `C64 Ultimate`) and the control port accepts a connection.
  **`401`, or `403` with an Ultimate-shaped `{"errors":[...]}` body**, is "present but
  password-protected". Bodies larger than the 2 KB buffer are truncated, not rejected.
- One physical device answering at several addresses (Ethernet and Wi-Fi) becomes one profile:
  the first enumerated address wins, another is kept as `peer_host`.
- A source whose configured host is any address of a discovered device is attached to it.
- The startup scan publishes its results after the first sweep, then retries silent addresses
  once (for interfaces that come up with OBS) and publishes again only if that found more.
- A completed scan replaces the source's selection only when the selection is a placeholder
  (empty, unknown, or a host-derived legacy ID) and exactly one device answered. A
  hardware-identified device that is merely switched off stays selected
  (`c64_device_profile_is_identified`), so a source never hops to another machine.
- When discovery re-keys a migrated host-based profile to the hardware ID, the old profile is
  removed, so the device is not listed twice.
- The whole scan runs on a detached background thread; the UI is only touched via
  `obs_queue_task(OBS_TASK_UI, ...)`.

## Scripting: `SWITCH_DEVICE` and `DISCOVER_DEVICES`

Two C64Script commands (`src/script/vm/c64-script-vm-dispatch-machine.c`) drive device switching
and discovery from an automation script, for repeated/soak testing:

```
SWITCH_DEVICE "u64"                 REM by registry id, or by host if no id matches
DISCOVER_DEVICES                    REM synchronous LAN scan, probes port 80
DISCOVER_DEVICES PROBE_PORT 8080    REM probe a different port (e.g. a test mock)
```

`SWITCH_DEVICE` resolves its argument against `c64_device_registry_get` (by id) and falls back to
`c64_device_registry_find_by_host` (by host), then applies it via the same
`c64_script_queue_source_update` path effects use (`OP_PALETTE`, etc.) — setting the `"c64_device"`
source setting on the OBS UI thread, which triggers `c64_update()` and the exact device-transition
machinery described above. `DISCOVER_DEVICES` calls a synchronous variant of the scan,
`c64_device_scan_sync`, directly on the script executor thread (already off the OBS UI thread), so
the script blocks until discovery completes (bounded by the scan's own 12-second deadline) before
continuing.

Both commands are no-ops (not errors) when the script runs without an attached OBS source — the
same convention `PALETTE`/`EFFECT` already use — so they don't break script tests that exercise the
VM without full plugin context.

See `doc/testing/device-switch-soak.md`: `ntsc_device_switch_soak` runs automatically in CI against
two mock devices; `ntsc_real_switch_soak` is the explicit-only, real-hardware, long-duration
variant.

## Testing

- `tests/network/test_c64_device.c` — registry round-trip, id derivation, legacy migration.
- `tests/network/test_c64_device_scan.c` — subnet enumeration and its `[24,30]` clamp, product
  matching, candidate/error-envelope detection, selection policy, profile re-keying.
- `tests/network/test_c64_device_scan_hermetic.c` — the full probe path against in-process fake
  Ultimates on loopback: foreign servers, password protection, unresponsive and truncated
  responses, oversized bodies, known-host retry, two addresses of one device, long hostnames,
  the concurrent reachability check and local interface enumeration. Runs in CI on Linux, macOS
  and Windows.
- `tests/network/test_c64_device_switch.c` — the stop queue: switching back cancels a pending
  stop, same address with different control ports, repeated switches, overflow, give-up.
- `tests/network/test_c64_ingest_filter.c` — also the switch handover and learned-peer admission.
- `tests/e2e/scenarios/ntsc_device_switch_speed` — runs on every CI build: real OBS with two mock
  devices and an unreachable registered device; 40 live switches, rapid fire and round trips to
  the unreachable device. `assertions/device_switch_speed.py` requires every pause between live
  devices and every first frame to stay within one second.
- `tests/hil/c64stream_hil.py` — hardware-in-the-loop suite against real devices (local only,
  see `doc/testing/discovery-hil.md`).
- `tests/network/test_c64_ingest_filter.c` — fail-open behaviour, match/drop, NULL safety.
- `tests/network/test_c64_rest_outcome.c` — status → outcome classification for every code.
- `tests/network/test_c64_stream_control.c` — the negotiation table above, forced-transport
  overrides, and demotion-state transitions.
- `tests/script/test_c64script_parser.c` / `test_c64script_compiler.c` — parsing and execution of
  `SWITCH_DEVICE`/`DISCOVER_DEVICES`, including the no-obs-source no-op path and type-mismatch
  errors.
- `tests/e2e/scenarios/ntsc_device_switch_soak` — OBS-driven E2E scenario, run automatically in CI,
  with two independent mock devices; switches twice and asserts (via
  `assertions/device_switch_log.py`) that the plugin logged the expected number of device
  transitions.
- `tests/e2e/scenarios/ntsc_transport_legacy` / `ntsc_transport_rest` — dedicated scenarios forcing
  each stream-control transport against the mock (which now speaks both port 64 and REST port 80;
  see `framework/c64u_mock/server.py`), asserting on transport-specific log evidence
  (`assertions/transport_log.py`).
