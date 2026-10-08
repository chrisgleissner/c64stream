# Discovery and Device Switch: Hardware-in-the-Loop Tests

`tests/hil/c64stream_hil.py` tests device discovery and device switching against real C64
Ultimate / Ultimate 64 devices. It complements the CI coverage:

| Layer | Where it runs | What it covers |
| --- | --- | --- |
| `tests/network/test_c64_device_scan_hermetic.c` | ctest on Linux, macOS, Windows CI | Real probe path against fake devices on loopback: foreign servers, passwords, stalls, truncated and oversized responses, retries, long hostnames, the concurrent reachability check, interface enumeration on the runner |
| `tests/network/test_c64_device_switch.c`, `test_c64_ingest_filter.c`, `test_c64_device.c`, `test_c64_device_scan.c` | ctest on Linux, macOS, Windows CI | Stop queue, handover admission, sender verification policy, selection policy |
| `tests/e2e/scenarios/ntsc_device_switch_speed` | every CI build (Linux, real OBS, mock devices) | Switch pause and first-frame time ≤ 1 s for live back-and-forth, rapid fire and round trips to an unreachable device |
| `tests/hil/c64stream_hil.py` | local only | Real devices, real LAN, network faults, power cycles, long soak |

**Local only.** It needs devices on the LAN, Xvfb, OBS Studio and Docker. Never run it in CI.

## Isolation

- OBS runs headless on its own Xvfb display with a temporary `HOME`, so your OBS profile, scenes
  and device registry (`~/Documents/obs-studio/c64stream/settings`) are not touched. The freshly
  built `build_x86_64/c64stream.so` and the repository's `data/` are installed into that `HOME`.
- Each device is fingerprinted by temporarily setting its VIC-II border colour over REST
  (C64U green, U64 red). A source screenshot then shows which device OBS is displaying, and
  changing the colour proves the picture is live rather than a frozen last frame. Original
  colours are restored on exit.
- Network faults are injected with iptables/tcpdump inside a privileged helper container on the
  host network namespace (`docker --context default run --net=host --cap-add NET_ADMIN`, image
  `c64stream-netctl:local`: Alpine with iptables, tcpdump, iproute2). All rules live in the
  `C64HIL` chain, which is removed on exit. Rules only ever match the device addresses.

Build the helper image once:

```bash
mkdir -p /tmp/netctl && printf 'FROM alpine:latest\nRUN apk add --no-cache iptables tcpdump iproute2\n' > /tmp/netctl/Dockerfile
docker --context default build -t c64stream-netctl:local /tmp/netctl
```

## Power cycles

The `power` scenario and the last soak phase switch a C64U off and on. They run only when
`--c64u-outlet-mac` names the smart outlet that feeds **only** the C64U (`powerctl list`). The
harness checks that the C64U's **Power On After Power Loss** setting is `On`, shuts the machine
down over `PUT /v1/machine:poweroff`, then cuts and restores that outlet with `powerctl`. Never
pass the MAC of an outlet that feeds anything else.

## Running

```bash
./build --tests
python3 tests/hil/c64stream_hil.py \
  --c64u c64u --u64 u64 \
  --c64u-wifi 192.168.1.129 --c64u-wired 192.168.1.146 --u64-wifi 192.168.1.70 \
  --c64u-outlet-mac EC:B9:31:C8:4E:EE
```

Without scenario names every scenario runs (about 30 minutes). Name scenarios to run a subset,
e.g. `switch soak`. `--switches`, `--soak-switches`, `--rapid-switches`, `--offline-switches`,
`--loss` and `--gap-limit-ms` size the runs. Logs, the OBS log and the isolated `HOME` stay in the
printed work directory.

## Scenarios

| Scenario | What it does | Pass condition |
| --- | --- | --- |
| `fresh` | Empty registry, default host | Video without configuration; both devices registered |
| `switch` | Alternates between the devices | Each switch shows the target and never the previous device again; previous device stops streaming |
| `rapid` | Switches every 250 ms | Last choice wins; the other device stops |
| `wifi` | C64U Host set to the C64U's Wi-Fi address | Video (A/V leaves the wired port) |
| `wifi_learn` | Same, with the wired control port blocked so discovery cannot find it | Plugin verifies the wired sender by `unique_id` and shows its video |
| `late` | Source created while the device is unreachable | Connects once the device appears |
| `outage` | 3 s, 15 s, 40 s outages while streaming | Live video again after each |
| `offline_switch` | Switch to an unreachable device, it appears, switch back | Previous picture cleared, previous device stopped, target appears, switch back works |
| `restart_selection` | OBS restarts while the selected device is unreachable | Selection kept (no hop to the other device); connects when it returns |
| `power` | C64U power cycles while streaming | Live video after each boot |
| `lossy` | Switching under 1 % and 3 % random packet loss | Every switch lands on the right device |
| `soak` | 200 live switches with random dwell; 60 rapid-fire switches; 15 round trips to an unreachable U64; 15 round trips to a powered-off C64U | Every live-to-live pause ≤ 1 s (from the plugin's per-switch log); back on a live device within 1 s; previous picture cleared; devices return |

After every scenario the harness also checks that OBS destroyed the removed source. A source
that is never destroyed keeps streaming and holds its UDP ports, and a destroy that hangs blocks
every later one.

## Measuring the switch pause

On every switch the plugin logs:

```
Device switch from 192.168.1.146: first frame 245 ms after the request; longest pause without video 41 ms
```

The pause is the longest interval between two frames handed to OBS from the switch request until
the new device's first frame. Because the previous device's picture keeps running until the new
device's first packet arrives, this is the pause a viewer sees.

## Results (2026-10-08, C64 Ultimate fw 1.2.1RC2 + Ultimate 64 Elite fw 3.15, Kubuntu 24.04, OBS 32.2)

See the pull request that introduced this harness for the full table. Typical values: pause
without video 30-60 ms between live devices; first frame of the new device about 250 ms after the
request (REST round trips of the devices); back on a live device from an unreachable or
powered-off one within 0.6 s.
