# Discovery and Device Switch: Hardware-in-the-Loop Tests

`tests/hil/c64stream_hil.py` tests device discovery and device switching against real C64
Ultimate / Ultimate 64 devices. It complements the CI coverage:

| Layer | Where it runs | What it covers |
| --- | --- | --- |
| `tests/network/test_c64_device_scan_hermetic.c` | ctest on Linux, macOS, Windows x64 and ARM64 CI (label `platform-ci`) | Real probe path against fake devices on loopback: foreign servers, passwords, stalls, truncated and oversized responses, retries, long hostnames, the concurrent reachability check, interface enumeration on the runner |
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
printed work directory. `--video-port` moves the test OBS's UDP ports (default 11000, audio on the
next port) when another OBS on the machine already uses them.

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
| `palette` | Follow device with the device palette: built-in, file A, file B, file B overwritten with the same size, Force Legacy and back, then switching between the devices with a different file each | Border (colour 14) shows each palette's colour within 10 s of the setting change (20 s for the same-size overwrite); default palette under Force Legacy; each device shown with its own palette after every switch. Restores both settings and deletes the uploaded files |
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

| Check | Result |
| --- | --- |
| 400 switches between the two live devices (two runs of 200) | pause without video: median 46 ms, p95 65 ms, max 176 ms; none over 1 s |
| Visual: OBS program recording, 44 device changes at 60 fps | visible gap median 33 ms, max 67 ms; no frame of the previous device after the new one |
| Visual: screen grab of the OBS window, 30 fps | gap median 33 ms, max 67 ms |
| 60 rapid-fire switches, 30-300 ms apart | last choice wins; previous device stops |
| 15 round trips to an unreachable U64 | back on the live device within 0.52 s; previous picture cleared within 0.39 s |
| 15 round trips to a powered-off C64U | back on the U64 within 0.50 s |
| Source created while its device is unreachable | connects once the device is reachable |
| 3 s, 15 s and 40 s outages; 2 power cycles; OBS restart with selected device offline | recovers each time; selection kept |
| C64U Host set to the Wi-Fi address, wired control port blocked | video via the verified wired sender |
| Switching under 1 % and 3 % random packet loss | every switch lands on the right device |
| Removed sources | every one destroyed |

The new device's first frame usually arrives about 250 ms after the request (the devices' REST
round trips). About every five minutes the Ultimate 64 Elite's web server stalls a request for
~1 s and then ignores start commands for ~3 s; the previous device's picture is kept on screen
during that time, so the pause stays below 200 ms.
