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
device's key, and selects it. Migration only runs when the registry is empty, so it can never
overwrite a device the user already created.

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
| `C64_REST_UNREACHABLE` | no HTTP response | device down — legacy would fail too |

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
  - `FORBIDDEN`, `BAD_REQUEST`, `SERVER_ERROR`, `UNREACHABLE` → **no fallback**, return `false`.
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

The actual teardown happens off the UI thread, in the background retry worker
(`c64_async_retry_task`), via `c64_complete_pending_device_transition`:

1. Send `release_all` to the **old** device (REST if available, otherwise a no-op — held keys
   must never survive a switch).
2. Stop video and audio streaming against the old device's explicit host/port
   (`c64_stop_streaming_to`), so the teardown target is a captured value, not a live ambient
   read that could have already changed.
3. Tear down local sockets/threads (`c64_stop_streaming_local`).
4. Clear the REST demotion state and rebuild the REST client for the new device
   (`c64_rebuild_rest_client`), so a device that supports REST is not left demoted because of
   the *previous* device's capabilities.

`c64_abort_stream_start` applies the same release-all-then-stop sequence if a remote start
partially succeeds and a local worker then fails, so a crash mid-transition can't leave a key
held or a stream running on a device OBS no longer thinks is active.

## Device Scan (`src/device/c64-device-scan.{h,c}`)

The `Scan` button (`src/ui/c64-properties.c`) discovers devices without the user typing an IP,
porting the algorithm already proven in C64 Commander's Android discovery plugin:

- Probes every registry host, the manually-entered `c64_host`, and every address on each
  up/non-loopback/non-virtual local IPv4 subnet.
- **Subnet prefix is clamped to `[24, 30]`** (`c64_device_scan_enumerate_subnet`) — the guardrail
  that stops a `/16` interface from becoming a 65k-address sweep.
- **24 workers**, an 8-second overall deadline, and a 650ms per-probe connect/read timeout.
- A candidate is accepted if `GET /v1/info` returns a `product` containing `"ultimate"`
  (case-insensitive) or exactly `"c64u"` (`c64_device_scan_product_matches`).
- **`401`, or `403` with an Ultimate-shaped `{"errors":[...]}` body**, is treated as "present but
  password-protected" rather than dropped (`c64_device_scan_response_is_candidate` +
  `c64_device_scan_is_ultimate_error`) — a generic reverse proxy that returns `403` for anything
  does not have that error envelope and is correctly excluded.
- The whole scan runs on a detached background thread; the UI is only touched once, at
  completion, via `obs_queue_task(OBS_TASK_UI, ...)`.

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
the script blocks until discovery completes (bounded by the scan's own 8-second deadline) before
continuing.

Both commands are no-ops (not errors) when the script runs without an attached OBS source — the
same convention `PALETTE`/`EFFECT` already use — so they don't break script tests that exercise the
VM without full plugin context.

See `doc/testing/device-switch-soak.md`: `ntsc_device_switch_soak` runs automatically in CI against
two mock devices; `ntsc_device_switch_soak_real` is the explicit-only, real-hardware, long-duration
variant.

## Testing

- `tests/network/test_c64_device.c` — registry round-trip, id derivation, legacy migration.
- `tests/network/test_c64_device_scan.c` — subnet enumeration and its `[24,30]` clamp, product
  matching, candidate/error-envelope detection.
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
