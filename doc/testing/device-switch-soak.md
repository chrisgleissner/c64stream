# Device Switch Soak Test

How to run a repeated device-switch soak using OBS + the `c64stream` plugin, either against the
CI-safe mock scenario or a long-duration run against two real C64 Ultimate devices.

**`ntsc_device_switch_soak`** runs automatically in CI (and locally via `./e2e.sh --scenario
ntsc_device_switch_soak`) against two independent mock devices, switching twice by default — this
is the scenario described in `doc/testing/e2e.md`.

**`ntsc_real_switch_soak`** is the real-hardware, long-duration variant described in this
document. It is marked `ci_skip: true` and is **never run automatically** — it requires two
physical devices and a working GUI for OBS, and must always be invoked explicitly by name.

## Purpose

Switching between C64 Ultimate devices is driven by a small state machine
(`doc/internals/device-switch.md`): stop the old device, tear down local sockets, start the new
one. This soak repeatedly exercises that path — including REST negotiation and fallback, since
real hardware (unlike the CI mocks) speaks REST — to catch anything that only shows up after many
repetitions: a leaked socket, a stuck key, a demotion state that never clears, a resource leak in
the transition bookkeeping.

## One-time setup: register both devices

The soak script (`tests/e2e/scripts/device_switch.c64script`) switches devices by **host**, e.g.
`SWITCH_DEVICE "c64u"`, which resolves against the plugin's device registry
(`~/Documents/obs-studio/c64stream/settings/device-*.ini`). Register both devices once via OBS,
**before the first soak run**:

1. Add a C64 Stream source (or open the existing one used for E2E testing).
2. In Properties, set **Host** to `u64`, click **Save** in the Device section to register it.
3. Change **Host** to `c64u`, click **Save** again to register the second device.

Both hostnames must resolve on the network the OBS machine is on (they already do for the
`ntsc_default_avsync_device` scenario, which defaults to `c64u`). After this one-time step, the
registry has two devices whose `host` field is exactly `u64` / `c64u`, which is what
`SWITCH_DEVICE` matches against.

## Running the soak

```bash
export C64_DEVICE_SWITCH_COUNT=200          # number of switches (a<->b counts as 2)
export C64_DEVICE_SWITCH_INTERVAL_MS=3000   # wait between switches
export C64_DEVICE_SWITCH_DEVICE_A=u64
export C64_DEVICE_SWITCH_DEVICE_B=c64u
export C64_DEVICE_SWITCH_DISCOVER=0         # devices are already registered; see below

cd tests/e2e
./e2e.sh --scenario ntsc_real_switch_soak --duration 660 --verbose
```

`--duration` bounds how long OBS records/runs; it does not control the script's own pacing. Size
it to comfortably exceed the switch loop's expected runtime, e.g.
`count * interval_seconds + 60s` margin. The example above (200 switches, 3s apart) runs the
script for ~10 minutes; `--duration 660` gives it an 11-minute window.

For a **quick smoke check** before committing to a long run, lower the count and interval:

```bash
export C64_DEVICE_SWITCH_COUNT=4
export C64_DEVICE_SWITCH_INTERVAL_MS=1000
./e2e.sh --scenario ntsc_real_switch_soak --duration 30 --verbose
```

### Using discovery instead of manual registration

Setting `C64_DEVICE_SWITCH_DISCOVER=1` runs `DISCOVER_DEVICES` at script start, which refreshes
the registry from a live LAN scan. This re-validates each device's current network settings, which
is useful if IPs can change (DHCP). It does **not** remove the one-time registration step above:
discovery only probes a host **by name** if that host is already the scenario's `c64_host` (the
first device) or already present in the registry (the second device) — an IP-only subnet sweep
would find the second device under its IP address, not its hostname, and `SWITCH_DEVICE "c64u"`
would then fail to match it.

## What to check afterward

- **No stuck keys / joystick / restore**: the plugin's teardown path calls `release_all` on every
  switch (`doc/internals/keyboard-injection.md`); confirm neither device shows a held key
  afterward.
- **OBS log** (`obs_log.txt` in the session output directory): every switch logs `Completing
  asynchronous device transition: stopping <old> before starting <new>`. Count should match
  `C64_DEVICE_SWITCH_COUNT`.
- **No REST demotion pileup**: if a device's REST endpoint flakes mid-soak, the stream-control
  negotiation demotes to legacy for a bounded retry window (`doc/internals/device-switch.md`) —
  it should recover, not stay demoted permanently unless the device genuinely lacks REST support.
- **Resource use**: no growth in open file descriptors / threads across the run (a leaked socket
  or thread from an interrupted transition would show up here first).
- **Clean state after the run**: both devices stopped, no stream left running on either.
