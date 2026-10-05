# Adversarial review: 1.1.2 to 1.2.0-rc2

Reviewed on 2026-10-05. The published releases and remote tag hashes were checked
before comparing:

- Official release: [1.1.2](https://github.com/chrisgleissner/c64stream/releases/tag/1.1.2),
  `e777f9980a1e5055782a1f56afa8adf2bd6d1a22`.
- Release candidate: [1.2.0-rc2](https://github.com/chrisgleissner/c64stream/releases/tag/1.2.0-rc2),
  `45c6f67d33c54e71f9e17c327f71e52e4ce53bd6`.

The comparison changes 182 files. Review concentrated on the runtime additions
and their failure paths: device registry/discovery, endpoint transitions, REST
negotiation, keyboard/joystick injection, audio sequencing/concealment, asynchronous
recording and file cleanup. Packet buffering, palette isolation, script/VM changes,
build configuration and test harness changes were also examined. Fixes are based
on current main, preserving the parser/VM/fuzz corrections already merged in #132.

## Findings fixed

| Finding | Trigger and effect | Fix and evidence |
| --- | --- | --- |
| Discovery destroys saved settings | A scan unconditionally upserts its generated name, empty DNS value and default ports. The UI completion then reapplies them to the selected source, overwriting user edits and potentially changing its UDP ports. | Merge discovered addresses into the existing profile under the registry mutex. `test_rescan_preserves_saved_settings` reproduces the overwrite and verifies persistence after reloading the registry. |
| First discovery discards migrated settings | A legacy scene migrates to a host-based profile. Identifying that host under its hardware ID creates a separate profile with default ports, and automatic selection replaces the configured UDP ports. Local OBS transport runs reproduced the resulting loss of video. | Carry settings from the matching migrated host profile into the new hardware profile. `test_discovery_preserves_migrated_profile` failed before the fix; it also verifies hardware profiles take precedence and another physical device at the same address does not inherit them. |
| Force REST silently uses legacy | With no REST client, `try_rest` is false regardless of the forced transport. The legacy path runs and reports success. | Reject forced REST when its client is absent. The new start/stop regression failed before the fix. Receive-only `0.0.0.0` is covered separately for all three transport modes. |
| Legacy failures report success | The legacy sender returns void on connection or send failure, but the new negotiation layer returns true unconditionally. Startup/retry treats failed commands as successful. | Return and propagate delivery success. Tests cover forced legacy, missing-client AUTO and negotiated fallback failures, plus the real sender's command bytes, failed connections and failed sends using local socket pairs. |
| Audio stop is skipped after video failure | Device teardown uses `!stop_video || !stop_audio`; the first failure short-circuits the audio request and can leave the old device sending audio. | Share a teardown helper that always attempts both stops. Regression tests cover REST and legacy failures, and successful teardown. |
| Keyboard demotion survives device switches | The same keyboard worker survives an in-place REST retarget. Its local 404/501 demotion cache is never reset, so a new device inherits the old firmware's fallback decision. | Notify the worker through a mutex-protected generation counter when the REST target changes. Tests retain caching on one device and verify renegotiation after both 404 and 501 responses. The test failed against an isolated build of the RC2 keyboard implementation. |
| Failed demo copies leak destination handles | `copy_ok && !ferror(src_file) && fclose(dst_file) == 0` skips `fclose` after a short write or read error, then clears the only handle pointer. On Windows that can also prevent removal of the incomplete file. | Evaluate read status and destination close independently on both platform paths. Both paths were inspected and the plugin was rebuilt. |
| Failover policy test can pass on failure | Its `CHECK` macro returns integer 1 inside a bool helper, which converts to true. Main therefore treats a failed assertion as success. | Use an integer helper with zero meaning success, and check that result explicitly. Existing policy cases remain intact. |

## Validation

- Baseline: `./build --tests --script-tests` passed 52 native tests, 19 Python unit
  tests and repository-wide validation of 73 scripts.
- After fixes: the same workflow passed 53 native tests, 19 Python unit tests and
  all 73 scripts. The additional native test exercises legacy command delivery.
- `./build-aux/run-clang-format --check`, `gersemi --check tests/network/CMakeLists.txt`
  and `git diff --check` passed.
- Local OBS scenarios `ntsc_transport_rest`, `ntsc_transport_legacy` and
  `ntsc_discovery_dual_address_switch` passed, including all scenario assertions.
  Transport runs verified video, audio, frame progression and keyboard injection.
  Discovery verified alternate-interface recovery, both device switches and
  the corresponding rendered patterns.

Final integration runs used a container on the local GUI workstation with Xvfb
and OBS 30.2.3.1, with the plugin rebuilt against that OBS version. Each scenario
had an isolated Documents directory, also linked at `~/Documents` for the test
artifact collector. This resolved the initial host setup problems: restricted
port 80, an occupied default UDP port and the collector's fixed Documents path.
The original host OBS configuration was backed up and restored. No E2E command
was run in CI or a cloud shell.

The discovery topology intentionally generates no A/V pop markers and replays
separate streams across a longer sequence of device switches. Its generic
validator therefore reports no-pop, duration and packet-order warnings; the
scenario's recovery/switch/pattern assertions all passed. The transport scenarios
reported no validation warnings. Existing cleanup WebSocket requests after OBS
exit and unavailable optional OBS hardware modules were also checked; these are
test-environment diagnostics rather than failures of the changed runtime paths.

No physical-device, Windows or macOS integration run is claimed.
