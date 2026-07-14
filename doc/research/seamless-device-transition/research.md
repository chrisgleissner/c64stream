# Seamless Device Transition — Research

**Status:** Research only. No code changes proposed for immediate implementation.
**Date:** 2026-07-14
**Scope:** Switching a C64 Stream source between multiple Ultimate 64 / C64 Ultimate devices.

## Contents

- [Problem Statement](#problem-statement)
- [Current Behaviour](#current-behaviour)
  - [Issue 1: Hosts Must Be Retyped](#issue-1-hosts-must-be-retyped)
  - [Issue 2: The Old Device Is Never Told To Stop](#issue-2-the-old-device-is-never-told-to-stop)
  - [Issue 3 (latent): The Ingest Path Accepts Packets From Anybody](#issue-3-latent-the-ingest-path-accepts-packets-from-anybody)
- [The Five Approaches](#the-five-approaches)
  - [A. Transition-Aware Handoff](#a-transition-aware-handoff)
  - [B. Device Registry](#b-device-registry)
  - [C. Lease-Based Streaming](#c-lease-based-streaming)
  - [D. Ingest Ownership Filter](#d-ingest-ownership-filter)
  - [E. Connection Broker With Discovery](#e-connection-broker-with-discovery)
- [Evaluation](#evaluation)
  - [Dimensions and Weights](#dimensions-and-weights)
  - [Scoring Matrix](#scoring-matrix)
  - [Ranking](#ranking)
- [The Winner: B, Device Registry](#the-winner-b-device-registry)
- [Extension: Migrating to the 3.15alpha REST API](#extension-migrating-to-the-315alpha-rest-api)
  - [What the Spec Actually Offers](#what-the-spec-actually-offers)
  - [The Shared Problem: Capability Negotiation](#the-shared-problem-capability-negotiation)
  - [Negotiation Strategies, Rated](#negotiation-strategies-rated)
  - [Task 1: REST Keyboard Input](#task-1-rest-keyboard-input)
  - [Task 2: REST Stream Control](#task-2-rest-stream-control)
  - [Revision: How This Changes the Earlier Analysis](#revision-how-this-changes-the-earlier-analysis)
- [Final Design Decisions](#final-design-decisions)
- [Recommended Composition](#recommended-composition)
- [Answers and Findings](#answers-and-findings)
  - [Answered by the Project Owner](#answered-by-the-project-owner)
  - [Answered Empirically](#answered-empirically-probed-against-c64u-and-u64)
  - [Answered by C64 Commander](#answered-by-c64-commander)
  - [Newly Open](#newly-open-raised-by-the-answers-not-blocking)

---

## Problem Statement

A user who owns more than one Ultimate device cannot move a C64 Stream source between them
cleanly. Two distinct problems compound each other:

1. **Re-entry friction.** The host is a free-text field. Switching from device A to device B means
   typing B's hostname, and switching back means typing A's again — from memory, with no history.
2. **No teardown of the previous device.** The plugin sends a start command to the new device but
   never sends a stop command to the old one. Both devices then stream UDP to the same OBS ports
   simultaneously. Their packets interleave, the frame assembler sees two unrelated frame-number
   sequences, and the result is corrupted video plus doubled network load.

The second problem is the more damaging one: it produces visible corruption rather than mere
inconvenience, and the user has no obvious way to recover short of power-cycling the abandoned
device.

## Current Behaviour

### Issue 1: Hosts Must Be Retyped

The host is registered as a plain text property with no history and no completion:

```c
// src/ui/c64-properties.c:1666
obs_property_t *host_prop =
    obs_properties_add_text(network_props, "c64_host", obs_module_text("C64UHost"), OBS_TEXT_DEFAULT);
```

Host, password, DNS server, and the three ports are all flat, singular settings on the source
(`src/util/c64-types.h:89-101`). There is exactly one host's worth of configuration in existence at
any moment. Nothing in the data model can represent "my two devices" — so nothing in the UI can
offer them.

The config export/import feature (`src/ui/c64-properties.c:2455` onward) is the closest existing
workaround: a user can export a full `.ini` per device and re-import to switch. It is not a device
switcher, though — it round-trips *every* setting including effects and recording paths, and
deliberately drops the password for security reasons (`src/ui/c64-properties.c:2471`).

### Issue 2: The Old Device Is Never Told To Stop

This is a three-part failure, and each part independently prevents the stop command from reaching
the old device.

**Part 1 — the stop path sends no stop command.** When the host changes while streaming,
`c64_update` calls `c64_stop_streaming` (`src/c64-source.c:1464`) and even sleeps 100 ms afterwards
with the comment "Give the C64 Ultimate device time to process stop commands". But
`c64_stop_streaming` (`src/c64-source.c:1776-1829`) only closes local sockets, joins threads, and
clears buffers. It never calls `c64_send_control_command`. There are no stop commands to process;
the sleep waits for nothing. The device keeps streaming to a port nobody is bound to.

**Part 2 — the old address is destroyed before it could be used.** Immediately after the stop,
`c64_update` overwrites the address in place:

```c
// src/c64-source.c:1492
if (host_changed) {
    strncpy(context->ip_address, new_host, sizeof(context->ip_address) - 1);
    context->ip_address[sizeof(context->ip_address) - 1] = '\0';
}
```

`old_hostname` is snapshotted a few lines earlier (`src/c64-source.c:1431`) purely to compute the
`host_changed` boolean, then discarded. Once the copy lands there is no record anywhere in the
context of who the previous device was.

**Part 3 — the "proactive disconnect" is aimed at the wrong device.** `c64_start_streaming` does
attempt a defensive stop before starting:

```c
// src/c64-source.c:1645
if (strcmp(context->ip_address, "0.0.0.0") != 0) {
    C64_LOG_DEBUG("Sending proactive disconnect for all streams before starting");
    c64_send_control_command(context, false, 0); // Stop video
    c64_send_control_command(context, false, 1); // Stop audio
    os_sleep_ms(50);
}
```

This looks like it addresses the problem, and it is why the bug is easy to miss on inspection. But
`c64_send_control_command` derives its target from `context->ip_address`
(`src/network/c64-protocol.c:123, 175, 219`) — which by this point is already the **new** device.
The plugin therefore politely tells the new device to stop, waits 50 ms, then tells it to start,
while the old device streams on untouched. The proactive disconnect is doing real work for the
"reconnect to the same device" case it was written for, and no work at all for the switching case.

The net effect: the plugin has three separate mechanisms that each look like they handle teardown,
and the intersection of all three handles nothing.

### Issue 3 (latent): The Ingest Path Accepts Packets From Anybody

Worth surfacing because it determines how bad Issue 2 looks in practice, and because a fix is
mostly already written.

The context carries an expected peer address:

```c
// src/util/c64-types.h:96-97
bool expected_peer_ip_set; // Whether expected_peer_ip contains a valid IPv4 address
uint32_t expected_peer_ip; // Expected peer IPv4 address in network byte order (AF_INET)
```

It is populated diligently in three places (`src/c64-source.c:613, 1496, 1654`) — and then never
read. Both receiver threads capture the sender's address and ignore it:

- `src/video/c64-video.c:999` passes `&sender_addr` to `recvfrom`, never inspects it. The Linux
  `recvmmsg` batch path fills an `addrs[]` array (`src/video/c64-video.c:932`) with the same result.
- `src/audio/c64-audio.c:43` does the same.

So `expected_peer_ip` is dead code: the plumbing exists, the check does not. Every UDP packet
arriving on the video or audio port is assembled regardless of origin. Two devices streaming
concurrently do not merely compete for bandwidth — their packets are actively merged into one frame
assembler, which is what turns contention into visible corruption.

## The Five Approaches

Each approach attacks the problem through a different mechanism: an imperative fix, a data model, a
protocol property, a receiver-side defence, and an architectural component.

### A. Transition-Aware Handoff

**Mechanism: imperative repair of the switch path.**

Treat "switch device" as a first-class operation inside `c64_update` rather than an accident of
mutating `ip_address`. Snapshot the old endpoint (`ip_address` + `control_port`) before it is
overwritten, and hand it to a new `c64_send_control_command_to(host, port, enable, stream_id)` —
a variant of the existing function that takes its target as a parameter instead of reading
`context->ip_address`. Send `FF30`/`FF31` to the old endpoint, then proceed with the existing start
sequence against the new one.

For the UX half, promote `c64_host` from `obs_properties_add_text` to an editable combo
(`OBS_COMBO_TYPE_EDITABLE`, `OBS_COMBO_FORMAT_STRING`) backed by a most-recently-used list of the
last N hosts, persisted in the source settings. The user still types a host once, then picks it from
the dropdown forever after.

**Character:** the smallest change that makes both symptoms go away. Roughly 150 lines. It leaves the
architecture exactly as it is — including the fact that host state lives as flat strings on the
context and that `c64_update` is the god-function coordinating everything.

**Notable gap:** the teardown is best-effort and synchronous with the switch. If the old device is
unplugged, asleep, or on a network that has since gone away, the stop command fails silently and the
device resumes streaming the moment it returns. Nothing recovers from an OBS crash, which leaves the
device streaming forever.

### B. Device Registry

**Mechanism: change the data model — make devices objects rather than strings.**

Introduce `c64_device_t` (name, host, password, DNS server, video/audio/control ports) and a registry
module that owns a collection of them, persisted as `.ini` files under the existing
`C64_USER_DIR_SETTINGS` directory (`src/util/c64-file.h:39`). This mirrors the palette system almost
exactly — `c64_palette_init` / `c64_palette_populate_list` / `c64_palette_select` /
`c64_palette_save_as` / `c64_palette_delete` (`src/video/c64-palette.h`) is a proven, shipped
registry-plus-dropdown-plus-user-directory pattern in this codebase, and a device registry would be
its structural twin.

The UI becomes a `Device` dropdown listing saved devices, plus Save / Rename / Delete buttons,
populated exactly as the palette and keymap dropdowns are today
(`src/ui/c64-properties.c:1828, 1980`).

The load-bearing part is the transition itself. All device changes funnel through one function:

```
c64_device_activate(context, next_device):
    if active_device is set and differs from next_device:
        send FF30 + FF31 to active_device's endpoint    // farewell — see approach A
        wait for the existing stop grace period
    tear down local sockets and threads
    swap active_device := next_device
    start streaming against next_device
```

Because the old device's full endpoint is a live object rather than a string about to be
`strncpy`'d over, the teardown target is unambiguous. Switching becomes correct by construction: the
only route to a different device is through the function that says goodbye first.

**Character:** a real refactor — data model, persistence, UI, and a migration path for existing
`c64_host` settings. Roughly 600–900 lines. It also drains complexity out of `c64_update`, which
currently reconstructs "what changed?" by string-comparing snapshots (`src/c64-source.c:1427-1450`).

**Notable gap:** still nothing for the crash / device-offline cases. A registry makes the switch
correct; it does not make the system self-healing.

### C. Lease-Based Streaming

**Mechanism: use a protocol feature the plugin currently declines to use.**

The stream spec's enable command takes a duration:

> Duration in 5 ms ticks (0 = infinite) — `doc/c64/c64u-stream-spec.md:267`

The plugin hardcodes infinity:

```c
// src/network/c64-protocol.c:200
cmd[4] = 0x00; // Duration: 0 = forever (little endian)
cmd[5] = 0x00;
```

Instead, request a bounded lease — say 5 seconds (1000 ticks) — and renew it from a heartbeat thread
every ~2 seconds while the source is healthy. An abandoned device then stops on its own when its
lease lapses, with no stop command, no memory of the old host, and no cooperation from the plugin
required.

This is the only approach that is robust by construction rather than by diligence. It covers the
switch case, the OBS-crash case, the pulled-network-cable case, the killed-process case, and the
"user deleted the source" case with one mechanism.

**Character:** ~300 lines, but they sit on the live streaming path.

**Notable gap, and it is a serious one:** a missed renewal drops a live stream. For a plugin whose
stated priorities are "low latency and robustness over new features" (`AGENTS.md`), introducing a
mechanism where a scheduling hiccup, a GC-style stall, or a brief TCP failure on port 64 can black
out an in-progress broadcast is a hard trade. The lease TTL is a direct tension: short leases mean
fast cleanup and fragile streams; long leases mean sturdy streams and a long interleaving window
after a switch. It also does nothing whatsoever for Issue 1.

### D. Ingest Ownership Filter

**Mechanism: receiver-side defence — stop trusting the network.**

Finish the `expected_peer_ip` feature that is already half-built (see Issue 3). Compare each packet's
source address against the expected peer in both receiver threads and drop mismatches, counting them
in a diagnostic. Extend the idea with a generation counter — an epoch incremented on every
start/stop — so that packets in flight from a previous session are discarded rather than folded into
the new stream's frame assembly.

**Character:** the cheapest change on the list, perhaps 40 lines, most of it in two loops. The struct
fields exist, the values are already computed and kept current, the sender addresses are already
being captured. Only the `if` is missing.

**Notable gap:** it fixes the *corruption*, not the *cause*. The abandoned device keeps transmitting
at full rate; the packets still cross the network, still hit the NIC, still consume kernel buffer
space — they just get dropped one instruction later. The user sees clean video from the right device
while an unnoticed device floods their LAN indefinitely. As a standalone answer to the problem
statement, that is not a fix. As a safety net under any of the other approaches, it is close to free
and it closes the race window that every explicit-teardown design inherently has.

### E. Connection Broker With Discovery

**Mechanism: architectural — a module-level component owning all device sessions.**

Introduce a plugin-global singleton that owns every device connection across every C64 Stream source
in the OBS session, enforcing an exactly-one-streamer-per-device invariant centrally. Sources no
longer talk to devices; they ask the broker for a session and the broker guarantees the previous
holder was torn down. Pair it with active discovery — an mDNS listener, or a subnet sweep probing
TCP/64 and the REST port — so the device dropdown populates itself and the user never types a
hostname at all.

This is the only approach that addresses a problem the others do not: two C64 Stream sources in the
same scene collection pointed at the same device. Nothing today prevents that, and it produces the
same interleaving pathology through a different door.

**Character:** by far the largest, ~1500+ lines, and the highest blast radius. A cross-source
singleton must get OBS module load/unload ordering right, be thread-safe against multiple sources'
update callbacks, and handle sources being created and destroyed underneath it.

**Notable gaps:** discovery is the weak leg. The Ultimate's REST API as documented
(`doc/c64/c64u-openapi.yaml`, `doc/c64/c64u-rest-api.md`) exposes no discovery or device-identity
endpoint, so there is no reliable way to confirm a responding host is an Ultimate, or to tell two of
them apart other than by address. mDNS may not be advertised at all; a subnet sweep is slow, fails
across routed or VLAN'd networks, trips firewalls and IDS, and requires per-platform interface
enumeration. The discovery half could easily consume more effort than approaches A through D
combined while remaining unreliable in exactly the environments where users have several devices.

## Evaluation

### Dimensions and Weights

Ten dimensions, weighted by how directly each bears on the problem statement. Higher scores are
always better, including for cost dimensions (Implementation effort 10 = cheapest; Regression risk
10 = safest).

| # | Dimension | Weight | What it measures |
|---|---|---|---|
| 1 | Stale-stream elimination | 3.0 | Does the abandoned device actually stop transmitting? |
| 2 | Switching UX | 3.0 | Is the user freed from retyping hosts? |
| 3 | Failure robustness | 2.0 | Holds up when the old device is offline, or OBS crashes? |
| 4 | Implementation effort | 2.0 | Cost to build (inverted: 10 = cheapest) |
| 5 | Regression risk | 2.0 | Safety for existing streaming (inverted: 10 = safest) |
| 6 | Testability | 1.5 | Unit-testable, and coverable by the mock C64U server? |
| 7 | Transition latency | 1.0 | Time from switch to clean first frame |
| 8 | Architectural fit | 2.0 | Does it match how this codebase is already built? |
| 9 | Contention resilience | 1.5 | Is the network itself relieved, not just the display? |
| 10 | Extensibility | 1.0 | Does it enable multi-source, scripting, future work? |

Dimensions 1 and 2 carry the highest weight because they *are* the problem statement. Effort,
regression risk, and architectural fit are weighted next because `AGENTS.md` states the project
prioritises "low latency and robustness over new features" and keeping modules focused.

### Scoring Matrix

Raw scores, 0–10 per dimension:

| Dimension (weight) | A. Handoff | B. Registry | C. Lease | D. Filter | E. Broker |
|---|---|---|---|---|---|
| 1. Stale-stream elimination (3.0) | 8 | 9 | 9 | 2 | 10 |
| 2. Switching UX (3.0) | 6 | 10 | 2 | 1 | 10 |
| 3. Failure robustness (2.0) | 4 | 6 | 10 | 7 | 7 |
| 4. Implementation effort (2.0) | 9 | 5 | 6 | 10 | 2 |
| 5. Regression risk (2.0) | 8 | 6 | 5 | 9 | 3 |
| 6. Testability (1.5) | 7 | 8 | 6 | 9 | 5 |
| 7. Transition latency (1.0) | 8 | 8 | 7 | 10 | 6 |
| 8. Architectural fit (2.0) | 6 | 9 | 7 | 8 | 7 |
| 9. Contention resilience (1.5) | 6 | 7 | 5 | 4 | 9 |
| 10. Extensibility (1.0) | 3 | 9 | 5 | 3 | 10 |

Weighted totals (maximum possible = 190):

| Rank | Approach | Weighted score | % of max |
|---|---|---|---|
| 1 | **B. Device Registry** | **148.5** | **78.2%** |
| 2 | E. Connection Broker With Discovery | 135.0 | 71.1% |
| 3 | A. Transition-Aware Handoff | 126.5 | 66.6% |
| 4 | C. Lease-Based Streaming | 117.5 | 61.8% |
| 5 | D. Ingest Ownership Filter | 109.5 | 57.6% |

### Ranking

**B (148.5)** wins by being the only approach that scores well on both halves of the problem at a
cost the project can absorb. It is the best answer to Issue 1 available at any price, it makes Issue 2
correct by construction, and it fits the codebase's existing registry idiom rather than fighting it.

**E (135.0)** has the highest ceiling — it is the only approach that solves the multi-source case,
and auto-discovery is a better user experience than any dropdown. It loses on cost and risk. Its
discovery leg depends on capabilities the Ultimate is not documented to have, which means the
expensive half of the work is also the half most likely to disappoint.

**A (126.5)** is the pragmatist's answer and rates highly on effort and safety. It ranks third
because it buys down the symptoms without changing the thing that caused them: host configuration
stays a set of flat mutable strings, and the next feature that touches host state will hit the same
wall. Its mechanism, however, is exactly right — see below.

**C (117.5)** is the most intellectually satisfying approach and the only genuinely self-healing one,
and it scores highest of all on failure robustness. It ranks fourth because it addresses only half
the problem statement and because "a missed heartbeat blacks out a live stream" is a poor trade for
a broadcast tool.

**D (109.5)** ranks last as a standalone approach, and the ranking undersells it. It does not solve
the stated problem — an invisible device flooding the LAN is still a bug. But it has the best
cost-to-benefit ratio on the list by a wide margin, and it should be built regardless of which
approach wins.

## The Winner: B, Device Registry

Model devices as first-class persisted objects and route every device change through a single
activation function that tears down the outgoing device before establishing the incoming one.

**Why it wins:**

*It is the only approach that answers the actual question.* The user's request is "make it easy to
switch between my devices, and stop the old one when I do". C wins the second half decisively and
ignores the first. D ignores both. A does both adequately and neither well. E does both superbly and
costs more than the rest combined. B does both, well, once.

*It converts a correctness problem into a structural invariant.* Issue 2 exists because the switch
path is an implicit consequence of `strncpy`-ing over a string, with three separate mechanisms that
each nearly-but-not-quite handle teardown. The bug is not that someone forgot a stop command — it is
that the code has no place to put one, because by the time you know a switch happened, you have
already destroyed the address you would need. Once "the currently active device" is an object with a
lifetime, "stop the previous one" has an obvious and unavoidable home. A future contributor cannot
reintroduce the bug without deliberately routing around `c64_device_activate`.

*It matches how this codebase already works.* The palette system is a registry of user-editable
entities, persisted as `.ini` under a user directory, discovered at init, surfaced through an OBS
dropdown, with select/save-as/delete operations. A device registry is that same shape with different
fields. Reviewers already know the pattern, the persistence helpers exist
(`c64_get_user_dir(C64_USER_DIR_SETTINGS, ...)`), and the dropdown-population idiom is already used
three times in the properties UI.

*It is testable in the ways this project tests things.* The registry is pure data — parse, serialise,
select, list — and unit-testable with no network. The transition sequence is exactly the kind of thing
`tests/e2e/mock_c64u_server.py` exists to verify: run two mock instances, switch, assert `FF30`/`FF31`
arrived at the first and `FF20`/`FF21` at the second. Neither is true of E's discovery, and only
awkwardly true of C's time-dependent leases.

*It pays down debt rather than adding to it.* `c64_update` is ~200 lines of change-detection that
recomputes what changed by string-comparing snapshots against live state. A device object turns most
of that into a pointer comparison.

**What it costs, honestly:** it is the second-most expensive approach on the list. It changes the
settings schema, so existing users' `c64_host` values need migrating into an implicit "Default"
device — a migration that must be right the first time, because getting it wrong silently orphans a
working configuration. And it does not make the system self-healing: a device that was offline at
switch time still resumes streaming when it comes back.

## Extension: Migrating to the 3.15alpha REST API

Two further requirements were added to this research:

1. **Use `/v1/machine:input` for keyboard input if available**, falling back gracefully to the
   existing keyboard-buffer injection if not.
2. **Use the REST API to start and stop streaming instead of TCP port 64 if available**, with a
   seamless fallback to port 64 for backwards compatibility.

Both were checked against
`/home/chris/dev/c64/c64commander/docs/c64/devices/u64e/3.15alpha/u64e-openapi.yaml`. Both endpoints
exist. They are not, however, two independent features — they are two consumers of one missing
mechanism, and the spec has consequences that reach back into the five approaches above.

### What the Spec Actually Offers

**`POST /v1/machine:input`** (spec line 1114) is not a REST wrapper around the current mechanism. It
is a fundamentally more capable model:

- **Matrix-level, not PETSCII.** `KeyboardInput` is an enum of 66 *physical keys* — `a`–`z`, `0`–`9`,
  `left_shift`, `right_shift`, `ctrl`, `commodore`, `run_stop`, `restore`, `arrow_left`, `pound`,
  and the rest of the real C64 keyboard.
- **Press / release / tap.** `InputTransition` distinguishes key-down from key-up, so keys can be
  *held* — which the KERNAL buffer fundamentally cannot express.
- **Chords.** Up to 8 simultaneous `inputs` per event.
- **Batching.** Up to 64 events per `InputBatch`, in one POST.
- **Joysticks.** `JoystickEvent` covers ports 1–2 with `up/down/left/right/fire/fire2/fire3`.
- **`release_all`.** A `ReleaseAllEvent` kind for recovering from stuck keys.
- **`restore`.** The RESTORE key is in the enum — it is an NMI line, not a matrix key, and is
  unreachable by any buffer write.
- **`GET /v1/machine:input`** reads back the currently-injected state.

**`PUT /v1/streams/{stream}:start?ip=<dest>`** and **`PUT /v1/streams/{stream}:stop`** (spec lines
1339, 1364) cover stream control. `{stream}` is an enum of `video`, `audio`, `debug` — note `debug`,
a third stream the plugin does not currently consume. The `ip` query parameter is *"Destination IPv4
address, optionally with port"*, which matches the `IP:PORT` destination string the plugin already
builds at `src/network/c64-protocol.c:143-168`.

**`GET /v1/info`** (spec line 580) returns `product`, `firmware_version`, `fpga_version`,
`hostname`, and — critically — **`unique_id`**. **`GET /v1/version`** returns the REST API version
as a bare string (example: `"0.1"`).

Three findings from the spec are load-bearing and easy to miss:

**Finding 1 — `501` is a *runtime* signal, not a routing signal.** The spec defines it as *"Firmware
route exists, but the current FPGA or hardware capability is unavailable."* (line 532). Both
`machine:input` verbs list `501` among their responses. This means capability **cannot be determined
by probing once at connect time, nor inferred from a firmware version string** — a device can have
the route, advertise the firmware, and still refuse the call because of what the FPGA is currently
doing. Any design that probes once and caches "supported = true" forever will break.

**Finding 2 — REST stream control has no `duration` parameter.** The port-64 enable command takes
one (`doc/c64/c64u-stream-spec.md:267`, `0` = infinite). `streams:{stream}:start` takes only `ip`.
The two transports are therefore **not equivalent in capability**, and migrating to REST *removes*
the ability to request a bounded stream. This is not a small detail — it is the entire mechanism
behind approach C.

**Finding 3 — `403 Forbidden` is listed on every one of these endpoints, and port 64 has no
authentication at all.** The REST client sends the password as an `X-Password` header
(`src/network/c64-rest-client.h:26`); the port-64 control path sends raw command bytes with no
credential of any kind (`src/network/c64-protocol.c:175-217`). A device that answers `403` to
`streams:video:start` will happily obey the same instruction on port 64. **Falling back from `403`
to port 64 would be an authentication bypass** — the plugin would be routing around a refusal the
user configured deliberately. `403` must be surfaced as an error, never demoted.

### The Shared Problem: Capability Negotiation

Neither task is really about its endpoint. Both need the same thing the codebase does not have: a
way to ask *"can this specific device do this specific thing right now?"* and to act on the answer
without stalling the caller or corrupting state.

The REST client actively destroys the information required to answer that. Every call funnels
through `http_request_ex` (`src/network/c64-rest-client.c:526`), which collapses all outcomes to a
`bool`:

```c
// src/network/c64-rest-client.c:610-617
long http_code = 0;
curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &http_code);
C64_LOG_DEBUG(REST_LOG_PREFIX "HTTP response code: %ld", http_code);

if (http_code < 200 || http_code >= 300) {
    ...
    return false;
}
```

The status code is logged and thrown away. Every public function in `c64-rest-client.h` returns
`bool`. So today a caller cannot distinguish:

| Outcome | Correct reaction |
|---|---|
| `404` route absent | Fall back to legacy transport, permanently for this firmware |
| `501` capability unavailable | Fall back **for now**, retry later — this can change |
| `403` forbidden | **Do not fall back.** Surface an auth error to the user |
| `400` bad request | Bug in our payload. Log loudly; falling back hides it |
| Connection refused / timeout | Device down. Fall back will also fail — this is not a capability signal |

All five are `false`. **A status-aware error channel on the REST client is a hard prerequisite for
both tasks**, and it is the first thing that should be built. It is a mechanical, low-risk refactor
(add an out-param or a `c64_rest_result_t`), but nothing else can be correct without it.

### Negotiation Strategies, Rated

Four candidate mechanisms for deciding which transport to use. Weighted across six dimensions
(higher is better throughout; effort and risk are inverted). Correctness carries the heaviest weight
because a wrong transport decision cannot be retrofitted, whereas all four options can be made fast:

| Dimension (weight) | 1. Probe once at connect | 2. Optimistic per-call | 3. Version gate | 4. Probe + per-call demotion |
|---|---|---|---|---|
| Correctness vs. `501` semantics (4.0) | 2 | 9 | 1 | 9 |
| Latency on the hot path (2.0) | 9 | 3 | 10 | 8 |
| Handles auth (`403`) safely (2.0) | 5 | 7 | 3 | 9 |
| Implementation effort (1.5) | 9 | 8 | 10 | 5 |
| Regression risk (1.5) | 8 | 5 | 9 | 6 |
| Observability / debuggability (1.0) | 6 | 5 | 7 | 9 |
| **Weighted total (max 120)** | **67.5** | **80.5** | **65.5** | **95.5** |

**1. Probe once at connect** — `GET /v1/version` at connect, cache the result. Cheap and fast, but
Finding 1 kills it: a cached `supported = true` will hard-fail the first time the FPGA returns `501`,
with no recovery path.

**2. Optimistic per-call (80.5)** — always try REST, fall back on failure. Correct, and it scores
second precisely because correctness dominates. But it pays a full failed round-trip on every call
for every legacy device. For keyboard input, where latency is the whole point, that is unacceptable.
It is the right *fallback posture* wrapped in the wrong *scheduling*, which is essentially why
option 4 wins: option 4 is option 2 with the redundant round-trips cached away.

**3. Version gate (65.5)** — parse `firmware_version` and require ≥ 3.15. Fastest and simplest, and
wrong on two counts: the example value is `"3.15 alpha"`, which is not a parseable semver, and per
Finding 1 the firmware version does not predict FPGA capability anyway. It ranks last despite
topping three of six dimensions — a good illustration of why correctness carries weight 4.0 here.
Rated for completeness; not viable.

**4. Probe + per-call demotion (recommended, 95.5)** — a hybrid, and the only one that respects what
the spec actually says:

- **Optimism is seeded, not assumed.** `GET /v1/version` and `GET /v1/info` at device activation set
  the initial per-endpoint preference. This is one round-trip on a path that already tolerates them.
- **Demotion is per-endpoint and reason-aware.** A `404` demotes permanently for the session. A `501`
  demotes with an expiry (retry after N minutes) because it can change. A `403` does **not** demote —
  it raises. A timeout does not demote — it is a connectivity failure, and the legacy transport will
  fail too.
- **State is per-device.** A `c64_device_caps_t` carried on the device object.
- **The user can override.** A three-way setting — `Auto` / `Force REST` / `Force Legacy` — for
  field diagnosis. `Auto` is the default; the other two exist because a user with a broken device
  should not have to rebuild the plugin to work around it.

Its cost is real: it is the most complex of the four, and per-endpoint state with expiry needs to be
tested properly. That is the right trade — the alternatives are fast and wrong.

**Where the capability state lives is the interesting part.** It is per-device, discovered at
activation, and invalidated when the device changes. That is precisely the lifetime of the device
object in **approach B**. The registry is not just a nice home for it — without a device object,
capability state has to be bolted onto the flat context as yet more parallel strings and booleans
(`rest_input_supported`, `rest_streams_supported`, `rest_probed_at`...), and reset correctly by hand
on every host change. **This is an independent argument for B**, arrived at from a completely
different direction than the switching problem.

### Task 1: REST Keyboard Input

**Current mechanism.** A polling state machine (`src/ui/c64-keyboard.c:1076-1129`) that: reads
`$00C6` (KERNAL buffer length) until it reads zero, clears the STOP flag, writes up to 10 PETSCII
bytes to the buffer at `$0277`, writes the length back, then verifies. Roughly 5 REST round-trips
per batch of ≤10 characters, and there is an IRQ-vector manipulation workaround at
`src/ui/c64-keyboard.c:1366` to force a warm start.

Its limits are structural, not incidental:

- **10 bytes per batch**, bounded by the KERNAL buffer.
- **Requires a running KERNAL.** Games and demos that take over the IRQ vector or bypass the buffer
  never see the input — the plugin writes to a buffer nobody reads.
- **No key-down/key-up.** A held key is inexpressible, so games are unplayable.
- **No modifiers, no joystick, no RESTORE.**
- **Writes into live machine memory**, which is inherently invasive.

**The REST API removes every one of those limits.** It is a strictly better mechanism operating at
the hardware layer rather than the KERNAL layer, and it makes things possible that are not merely
faster but currently impossible — held keys, joystick, and RUN/STOP+RESTORE.

**The migration is not a transport swap — it is a model translation, and that is where the cost
is.** The keymap system emits three output modes (`src/ui/c64-keyboard.h:25-29`):

| Mode | Example | Maps to `machine:input` how? |
|---|---|---|
| `C64_OUTPUT_SYMBOLIC` | `c64:RETURN` | **Direct.** Name → `KeyboardInput` enum. Straightforward table. |
| `C64_OUTPUT_PETSCII` | `petscii:0x41` | **Requires decomposition.** One PETSCII byte → key + shift state. |
| `C64_OUTPUT_TEXT` | `text:"HELLO"` | **Requires decomposition per character**, then batching. |

Decomposition is the real work. PETSCII `!` is not a key — it is `left_shift`+`1` held together and
released. `A` is `left_shift`+`a`. The plugin needs a PETSCII→matrix transliteration table with
shift state, roughly 96 entries, and it must emit correct chords: press shift, tap key, release
shift — or a single `tap` event with both inputs, which the 8-input allowance permits.

Two things make this cheaper than it sounds. The existing keymap `.ini` files already encode
symbolic C64 key names, so the vocabulary largely exists. And **the batching limits favour REST**:
64 events per POST versus 10 bytes per KERNAL batch, in **one** round-trip rather than five. The
REST path should be both more capable *and* faster.

**Fallback design.** Fallback must be at the *batch* level, not the keystroke level — a batch half
in the matrix and half in the KERNAL buffer would interleave out of order. On demotion, the current
batch is retried in its entirety via the legacy path.

The fallback is **not** feature-equivalent, and this must be surfaced rather than silently degraded.
A held key, a joystick event, or `restore` has no legacy representation. When such an input is
requested on a demoted device the plugin must log a clear, specific reason and, for user-initiated
actions, say so in the UI — not quietly drop it. "Nothing happened and nothing was said" is the worst
possible outcome. `release_all` on demotion is a good hygiene step where supported.

### Task 2: REST Stream Control

**Mapping.** The translation is nearly one-to-one:

| Current (port 64) | REST equivalent |
|---|---|
| `FF20` + duration + `"IP:PORT"` | `PUT /v1/streams/video:start?ip=<IP:PORT>` |
| `FF21` + duration + `"IP:PORT"` | `PUT /v1/streams/audio:start?ip=<IP:PORT>` |
| `FF30` | `PUT /v1/streams/video:stop` |
| `FF31` | `PUT /v1/streams/audio:stop` |
| — | `PUT /v1/streams/debug:start\|stop` (unused today) |
| duration ticks (`0` = infinite) | **no equivalent** |

The destination string the plugin already builds is directly reusable as the `ip` parameter.

**This task and the seamless-transition work reinforce each other.** The REST client is constructed
per base URL (`c64_rest_client_create(base_url, password)` — `src/network/c64-rest-client.h:28`), so
it is *inherently* addressed to a specific device. The port-64 path, by contrast, reads its target
from ambient mutable state — `context->ip_address` — which is exactly the design flaw that produced
[Issue 2](#issue-2-the-old-device-is-never-told-to-stop). Approach A exists largely to retrofit an
explicit target parameter onto `c64_send_control_command`; **the REST client already has that
property for free.** Saying goodbye to the outgoing device becomes
`PUT http://<old-host>/v1/streams/video:stop` against a client built for the old device — impossible
to aim at the wrong box, because the address is baked into the client rather than read from a field
that has already been overwritten.

**Four risks specific to this task:**

1. **Loss of the duration capability (Finding 2).** REST cannot express a bounded stream. If stream
   control migrates to REST, **approach C becomes impossible on REST-capable devices** — the exact
   devices most likely to be new enough to want it. C is not merely deprioritised by this work; on
   the REST path it is foreclosed. This deserves a question to the firmware author before any of it
   is built (see Open Questions).
2. ~~**Mixed-transport teardown is unverified.**~~ **Resolved by testing — it works both ways on both
   product lines** (see [Answered Empirically](#answered-empirically-probed-against-c64u-and-u64)).
   Stream state is transport-agnostic on the device, so the session-pinning constraint this section
   originally proposed is **not needed**, and mid-session demotion is safe.
3. **Mixed-transport *fleets* are the normal case, not the edge case.** A user switching between a
   3.15alpha device and an older one needs a *different transport per device*, decided per device and
   held across switches. Approach B's per-device object is the only clean place for that. With flat
   context strings, the transport preference is one more thing to reset by hand on every host change.
4. **The `403` trap (Finding 3).** Worth restating because it will look like a bug report: a device
   with a password set and a stale/absent password in the plugin will `403` on REST. Falling back to
   port 64 would make it *work* — and would be an authentication bypass, hiding a misconfiguration
   the user asked for.

**Where the transports genuinely differ, and it favours REST:** port 64 is fire-and-forget over a
throwaway TCP connection. The existing code opens a socket, `send()`s, and closes without reading a
response — see `src/network/c64-protocol.c:182`, where the return value of the stop `send` is cast
to `(void)`. **The plugin cannot currently tell whether a stream command was honoured.** REST returns
a status code. That is a real improvement for the transition problem specifically: `c64_device_activate`
could *confirm* the outgoing device stopped rather than hoping, and warn the user if it did not.

### Revision: How This Changes the Earlier Analysis

The spec answers questions the first pass had to leave open, and the honest thing is to record what
moved rather than quietly restate the conclusion.

**Open Question 2 is answered: `unique_id` exists.** `GET /v1/info` returns a stable device identity,
plus `product` and `hostname`. This strengthens **approach B** materially — a registry can key on
`unique_id` rather than a user-typed name, detect that a DHCP lease moved a known device to a new
address, auto-name new entries from `product`/`hostname`, and warn when an address now answers as a
*different* box. Its failure-robustness score rises from 6 to 7 (**148.5 → 150.5**).

**Open Question 1 is partly answered.** There is still no mDNS or discovery endpoint, so **approach
E**'s subnet-sweep problem stands. But `/v1/info` is a proper identity probe: a sweep can now
*confirm* a responder is an Ultimate and identify which one, rather than guessing from an open port.
E's robustness rises 7 → 8 and testability 5 → 6 (**135.0 → 138.5**).

**Approach C is further undermined.** Finding 2 means the lease mechanism does not exist on the
transport the project is being asked to move to. C ranked fourth on its own merits; it is now
foreclosed on exactly the devices it would target. Its score is unchanged — the port-64 path still
supports duration — but its strategic outlook is materially worse, and the recommendation to defer it
becomes a recommendation to shelve it pending a firmware answer.

**The ranking is unchanged: B still wins, and by more than before.** Both new tasks need per-device
capability state with a lifetime tied to device activation. Approach B is the only one of the five
that provides an object with that lifetime. The two REST tasks were specified independently of the
switching problem and arrive at the same structural conclusion — which is the strongest evidence in
this document that the conclusion is right.

| Approach | Original | Revised | Δ |
|---|---|---|---|
| **B. Device Registry** | 148.5 | **150.5** | +2.0 |
| E. Connection Broker | 135.0 | 138.5 | +3.5 |
| A. Transition-Aware Handoff | 126.5 | 126.5 | — |
| C. Lease-Based Streaming | 117.5 | 117.5 | — (outlook worse) |
| D. Ingest Ownership Filter | 109.5 | 109.5 | — |

## Final Design Decisions

The owner's answers resolve the remaining design freedom. Recording them here as the binding
constraints for `plan.md`:

**1. B absorbs E's scan; E's broker stays shelved.** The answers split approach E cleanly in two. Its
*discovery* half is now in scope and cheap — a Scan button probing `/v1/info`, ported from C64
Commander's proven algorithm, populating B's dropdown. Its *broker* half (a cross-source singleton)
remains out of scope: it addresses the multi-source collision, which is a real but separate gap.
**B + scan delivers E's entire user-facing benefit at a fraction of its risk.** That was the one
thing the first pass ranked E highly for and doubted was affordable; it turns out to be affordable
because someone already wrote it.

**2. Approach C is shelved, not deferred.** REST carries no `duration` (Finding 2), the lease failure
rate is unknown, and stream control is moving to REST. The mechanism does not exist on the transport
we are adopting. Revisit only if the firmware gains `duration`.

**3. Device profiles carry network settings only.** No effects, no recording paths. KISS.

**4. Per-device passwords — and the storage question they force.**

Passwords must be per-device: the registry entry owns the password, and two devices may have
different ones. This is a strong reinforcement of B — a single `c64_password` on the context
(`src/util/c64-types.h:92`) cannot express it at all.

But it collides with an existing, deliberate security decision. Config export **excludes** the
password on purpose:

```c
// src/ui/c64-properties.c:2471
// NOTE: Password is intentionally excluded from export for security.
// Passwords remain in OBS settings which are protected by OS-level access controls.
```

Device profiles are proposed as `.ini` files under `~/Documents/obs-studio/c64stream/settings/`.
Writing passwords there would put plaintext credentials in a Documents folder that users back up,
sync, and attach to bug reports — **strictly worse than today's posture, and a silent reversal of a
decision someone made deliberately.**

**Decision: split the storage.** The registry `.ini` holds network settings and is freely shareable.
The password stays in OBS source settings, keyed by device id, exactly where it lives today and under
the same OS-level protection. The registry entry references the password; it does not contain it.
This keeps the existing security posture intact while making passwords per-device, and it keeps
profiles safe to attach to a bug report — which is what the export feature is for.

**Corollary — the scan must prompt, not guess.** C64 Commander surfaces `401`/`403` responders as
candidates *requiring a password* rather than dropping them. The plugin should do the same: a
password-protected device must appear in the scan results and prompt for its password on selection.
Dropping it would make a user's own device invisible to the feature built to find it.

## Recommended Composition

The approaches are not mutually exclusive, and B does not stand alone. Ranked as designs, they
compose as follows:

**B needs A's mechanism.** A's `c64_send_control_command_to(host, port, ...)` — the parameterised
variant of the existing function — is precisely what `c64_device_activate` calls to say goodbye.
Without it, B is a nice dropdown that still leaves two devices streaming. A is the engine; B is the
model that makes the engine impossible to forget to start. They are one piece of work, not two.

**D should be built regardless, and probably first.** It is ~40 lines, it is independently valuable,
it is independently testable, and it can ship before any of the larger work lands. Every
explicit-teardown design has a race window between "stop sent" and "device actually stops"; the peer
filter closes it. It also retires dead code that currently misleads readers into thinking the ingest
path is already protected.

**C is shelved.** Its value was real but orthogonal — it was the only thing that helped when OBS
crashes. REST carries no `duration`, so the mechanism does not exist on the transport being adopted.
Revisit only if the firmware gains it.

**E splits.** Its **discovery half is in scope** and folded into B as a Scan button — the owner
confirmed no mDNS, but a `/v1/info` sweep works and C64 Commander has already de-risked it. Its
**broker half is not built**: the exactly-one-streamer invariant *across sources* is a genuine gap
that B does not close — two sources with the same device selected still collide. That is worth an
issue, not a rewrite.

**The two REST tasks slot in around B rather than beside it.** Both depend on the status-aware error
channel, and both want per-device capability state — so the order is forced more than chosen:

0. **Status-aware REST errors.** Prerequisite for everything REST. Mechanical, low-risk, independently
   valuable: today every failure mode in the client is indistinguishable from every other.
1. **D — ingest ownership filter.** Independent of all of the above, ~40 lines, ships immediately.
2. **A's parameterised send + B's registry and activation path.** The main work, and the point at
   which `c64_device_caps_t` has somewhere to live.
3. **Task 2 — REST stream control.** Before Task 1, deliberately: it is the smaller translation
   (a near one-to-one endpoint mapping versus a PETSCII→matrix decomposition), it exercises the
   negotiation machinery on a path that already tolerates round-trips, and it makes B's farewell step
   *verifiable* rather than fire-and-forget.
4. **Task 1 — REST keyboard input.** Last, because it carries the transliteration table and the
   not-feature-equivalent fallback, and because it benefits from negotiation that has already been
   proven in the field by step 3. Worth noting: it unlocks held keys, joystick, and RESTORE — this is
   a capability feature wearing a refactor's clothes, and probably deserves its own plan.
5. **Scan button** (E's discovery half), ported from C64 Commander. Independent of 3 and 4 — it only
   needs B's registry to populate. Can run in parallel with the REST tasks.
6. **Not built:** approach C (shelved — no `duration` over REST), E's cross-source broker (recorded
   as a known gap).

The through-line: every one of these items either needs a per-device object or is made safer by one.
Building B first is not a preference, it is the ordering the dependencies impose.

Empirical results have simplified two of these. Cross-transport teardown works both ways, so step 3
needs no session pinning. And the scan is a port rather than an invention, so step 5 is far cheaper
than E's original rating assumed.

## Answers and Findings

All original questions are now answered — by the project owner, by C64 Commander's shipped
implementation, or by probing the two devices on the network (`c64u` and `u64`) directly.

### Answered by the Project Owner

1. **Does the Ultimate advertise itself via mDNS or any broadcast?** → **No.** But a **network scan**
   is viable and is already shipped in C64 Commander. The plugin shall offer a **Scan** button that
   auto-refreshes the device dropdown. This rescues discovery without mDNS: see
   [The Scan](#the-scan-ported-from-c64-commander).
2. **Is there any stable device identity retrievable over REST?** → **Yes** — `unique_id`, confirmed
   on real hardware below.
3. **How does the Ultimate behave with two enable commands naming different destinations?** →
   **It streams to the latest.** The second start replaces the first destination.
4. **What is the real lease-renewal failure rate under load?** → **Unknown**, and now moot: combined
   with Finding 2 (no `duration` over REST), approach C is **shelved**.
5. **Should device profiles carry effects settings?** → **No.** Network settings only. KISS.

**A correction I owe on Q3.** The first pass speculated that if a second enable *replaces* the
destination, "part of the interleaving problem may have a much cheaper fix than any approach here."
**That was wrong, and the answer disproves it.** Replace-latest is per-device state: device A holds
one destination, device B holds one destination, and both can point at the same OBS instance
simultaneously. Nothing about replace-latest prevents two *different* devices from streaming to us at
once — which is the actual bug. The answer does confirm one useful thing: reconnecting to the *same*
device from a new OBS IP is inherently clean, and no stale destination accumulates on a device.
There is no cheap shortcut here; the teardown work is necessary.

### Answered Empirically (probed against `c64u` and `u64`)

**Device identity — confirmed, and it kills the version gate for good.**

| | `c64u` | `u64` |
|---|---|---|
| `product` | `C64 Ultimate` | `Ultimate 64 Elite` |
| `firmware_version` | **`1.2.0`** | **`3.15`** |
| `core_version` | `1.4D` | `1.4B` |
| `unique_id` | `5D4E12` | `38C1BA` |
| `hostname` | `c64u` | `Ultimate-64-Elite-F83C87` |
| `GET /v1/version` | `0.1` | `0.1` |
| `GET /v1/machine:input` | `200` | `200` |

Two independent nails in the coffin of the version-gate strategy, neither of which was visible from
the spec alone:

- **The two product lines have disjoint version number spaces.** A `>= 3.15` gate would reject the
  C64 Ultimate on firmware `1.2.0` — a device that demonstrably *does* support `machine:input`.
- **`/v1/version` returns `0.1` on both**, despite different firmware, different products, and
  different cores. The REST API version does not vary with capability, so it cannot gate it.

Capability must be discovered by asking the endpoint, exactly as strategy 4 does. This is no longer
a judgement call.

**Cross-transport teardown — WORKS BOTH WAYS, on both product lines.** This was the question
blocking Task 2's demotion design. Tested by starting a video stream on one transport and stopping it
on the other, then measuring a clean UDP window:

| Test | `u64` | `c64u` |
|---|---|---|
| Baseline (streaming) | 6819 pkt/2s | ~6800 pkt/2s |
| **REST stop** of a **port-64-started** stream | **0 pkt/2s — STOPPED** | **0 pkt/2s — STOPPED** |
| **Port-64 stop** of a **REST-started** stream | **0 pkt/2s — STOPPED** | **0 pkt/2s — STOPPED** |

Stream state is transport-agnostic on the device. **This removes the constraint the first pass
imposed** — transport no longer needs to be pinned for the lifetime of a stream session, and
mid-session demotion is safe. Task 2 gets materially simpler.

> **A methodology note worth keeping.** The first run of this test reported "STILL STREAMING" for
> both directions (201 and 96 packets against a 6068 baseline). That was a **false negative**: the
> counts were packets already sitting in the kernel receive buffer, not live traffic. A 97% drop
> should have read as "stopped, with a drain" rather than "still streaming". The corrected test
> sleeps for propagation, *fully flushes the socket buffer*, and only then measures a clean window.
> Any E2E test asserting that a stream stopped must flush before measuring, or it will assert the
> opposite of the truth.

**`machine:input` — works on both devices, and holds keys.**

- Held keys are real: `press b` → readback shows `{"inputs":["b"]}`, still held a second later →
  `release b` → `{"inputs":[]}`. This is impossible via the KERNAL buffer and is the headline
  capability.
- **Consequence: stuck keys are a real hazard.** A held key survives until explicitly released, so if
  the plugin dies mid-hold the device is left with a key down. **`release_all` on stream teardown,
  device switch, and source destroy is mandatory, not hygiene.**
- Chords work: `{"inputs":["left_shift","a"],"transition":"tap"}` → `200`, readback shows both.
- `restore` is accepted (`200`) — genuinely unreachable by any buffer write.
- **`Content-Type: application/json` is mandatory.** Omitting it returns `400
  {"errors":["Content type should be 'application/json'."]}`. Easy to miss; the existing REST client
  does not set it on writes.
- Errors are precise and per-event: an invalid key yields
  `400 {"errors":["events[0]: 'not_a_key' is not a valid keyboard input."]}`. Worth surfacing
  verbatim in logs rather than flattening to "input failed".

**Not testable here:** neither device returns `501` for `machine:input`, so the demotion path has no
natural hardware trigger. It must be covered by the mock server in E2E — which is an argument for
building the negotiation logic against the mock rather than trusting field behaviour.

### Answered by C64 Commander

**The scan, ported from C64 Commander.** `android/app/src/main/java/uk/gleissner/c64commander/DeviceDiscoveryPlugin.kt`
is a battle-tested implementation whose comments encode real incident history. The algorithm to port:

- **Probe `GET http://<host>:80/v1/info`** per candidate; accept only if `product` contains
  `"ultimate"` (case-insensitive) or equals `"c64u"`.
- **Enumerate site-local IPv4 subnets** from local interfaces, skipping down/loopback/virtual ones.
  **Clamp the prefix length to `[24, 30]`** — this is the guardrail that stops a `/16` from becoming
  a 65k-host sweep. Skip the network, broadcast, and own address.
- **Probe known hosts alongside the sweep**, deduped, tagged `hostname` vs `lan-scan` by source.
- **Bounded concurrency:** default 24 workers (clamp 1–64), overall timeout 8s (1–30s), per-probe
  connect/read timeout 650ms (200ms–5s).
- **`401`/`403` means the device is present but password-protected — surface it as a candidate that
  needs a password, do not drop it.** Accept a `403` only if the body parses as the Ultimate's
  `{"errors":[...]}` envelope, since generic proxies and web servers return `403` freely and would
  otherwise pollute the list. (Confirms Finding 3 from a second direction: `403` means *auth*, never
  *absent*.)
- **Resolve the IP only *after* a successful connect** — a pre-connect `getByName()` on a name that
  never answers blocks a worker on a non-interruptible DNS call. Named hosts like `u64`/`c64u` are
  exactly the risk case.
- **Release the connection in a `finally`** — a device dropping mid-body leaked an FD per probe and
  accumulated across rescans (their HARD9-076).

### Newly Open (raised by the answers, not blocking)

1. **Is the omission of `duration` from `streams:{stream}:start` deliberate?** Approach C is shelved
   either way; this only matters if C is ever revived. Worth asking the firmware author opportunistically.
2. **Does `machine:input` work while a game has taken over the machine (no KERNAL)?** The hardware-level
   design implies yes, and it is the most compelling user-facing reason for Task 1 — but it is unverified,
   and verifying it needs a game loaded. Not blocking: the feature is justified by held keys and joystick
   regardless.
3. **What does the `debug` stream carry?** Listed in the `{stream}` enum, unused, undocumented.
   Ignorable for now.
