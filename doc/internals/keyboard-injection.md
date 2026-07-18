# REST Keyboard Injection

How keyboard input reaches the C64 Ultimate over the REST `machine:input` endpoint, and how it
falls back to the legacy KERNAL-buffer protocol on devices/firmware that don't support it.

## Legacy vs. REST

The legacy path (still the fallback) polls the KERNAL keyboard-buffer length at `$00C6`, writes
up to 10 PETSCII bytes to the buffer at `$0277`, and writes the length — roughly five REST-memory
round-trips per batch, with an explicit `WAIT_BUFFER_EMPTY` → `WRITE` → `VERIFY` state machine
(`injection_worker` in `src/ui/c64-keyboard.c`) so a previous batch is never overwritten mid-flight.

REST input (`POST /v1/machine:input`) works at the matrix level instead of PETSCII: it takes a
batch of up to `C64_REST_INPUT_BATCH_SIZE` (64) key press/release/tap events in one request, one
round-trip regardless of batch size.

## PETSCII → Matrix Translation

`petscii_to_matrix` (`src/ui/c64-keyboard.c`) is the one table used by both the `PETSCII` and
`TEXT` keymap output modes (`c64-keyboard.h`'s `C64_OUTPUT_PETSCII`/`C64_OUTPUT_TEXT`) — whichever
mode queues a byte, it goes through the same translation before becoming a REST event:

- PETSCII `A`..`Z` and ASCII text `a`..`z` map to unshifted matrix keys; PETSCII
  `$C1`..`$DA` maps to Shift+`a`..`z`. This preserves host-key capitalization while allowing
  C64Script `TYPE "hello"` to use REST input.
- Digits and punctuation map to their physical C64 matrix keys. For example, `'!'` maps to
  Shift+`"1"`, `'+'` maps to `"plus"`, `'['` maps to Shift+`"colon"` (the `:` key's shifted
  legend on the C64), `']'` maps to Shift+`"semicolon"`, and `'^'` / `'_'` map to the
  dedicated `"arrow_up"` / `"arrow_left"` keys.
- A shifted character becomes a single hardware chord in the request body:
  `{"kind":"keyboard","inputs":["left_shift","<key>"],"transition":"tap"}`.

`C64_OUTPUT_SYMBOLIC` mode (`c64:RETURN`-style names from the keymap) resolves through a separate
symbol table (`lookup_symbolic_key`) directly to a matrix key name, bypassing PETSCII entirely.

## Batching (`build_rest_input_batch`)

`injection_worker` pulls up to 64 queued bytes at a time (`queue_peek_batch`) and builds one JSON
body containing one event per byte, in order:

```json
{"events":[{"kind":"keyboard","inputs":["a"],"transition":"tap"},
           {"kind":"keyboard","inputs":["left_shift","1"],"transition":"tap"}]}
```

If any byte in the batch can't be translated (a control code outside the printable range), the
whole batch is rejected before being sent — it never partially builds a request.

**Fallback is at batch granularity, never per-keystroke.** If `machine:input` fails with an
outcome the transport allows falling back from, the *entire pending batch* — still ordered in the
queue — is retried through the legacy KERNAL path in the next loop iteration. Splitting a batch
across the matrix and KERNAL-buffer paths would risk interleaving keystrokes out of order, so the
implementation never does it.

## Transport and Fallback Policy

The same `Auto` / `Force REST` / `Force Legacy` setting used by stream control
(`c64_stream_control_transport`, see [`device-switch.md`](./device-switch.md)) governs keyboard
input via `c64_keyboard_set_transport`:

- **Force Legacy**: REST is never attempted; batches go straight to the KERNAL-buffer state
  machine, truncated to `C64_KEYBOARD_BUFFER_SIZE` (10 bytes) per write, same as before this
  feature existed.
- **Force REST**: on failure, the batch is discarded and marked failed — **no legacy fallback**,
  even for an outcome that would otherwise be fallback-eligible.
- **Auto**: on a `machine:input` failure with outcome `C64_REST_NOT_SUPPORTED`, the batch falls
  back to the legacy path for this call. Any other outcome (`FORBIDDEN`, `BAD_REQUEST`,
  `SERVER_ERROR`, `UNREACHABLE`) discards the batch and reports failure without falling back —
  the same "never fall back from an auth refusal" rule as stream control.

Note: unlike the stream-control path, a keyboard REST failure does **not** demote the device for
subsequent batches — each batch independently checks the transport and retries REST on the next
one. Failures are logged with the HTTP status (`C64_LOG_ERROR(... "refused batch (HTTP %ld)")`);
the plugin does not parse or forward the device's JSON error body.

## `release_all`

Held keys persist on the device indefinitely once pressed — verified on real hardware. Every
teardown path calls `c64_keyboard_release_all`, which sends a REST `release_all` command:

- Stream stop against the old device during a switch (`c64_stop_streaming_to`).
- Aborting a partially-started stream (`c64_abort_stream_start`).
- Source destroy.

`release_all` failure is logged as a warning (with the last HTTP status) but never blocks
teardown — a device that can't confirm release is still torn down locally.

## Testing

- `tests/script/test_keyboard_worker.c` — the injection worker's state machine (buffer-empty
  wait, write, verify, retry/backoff) against a stubbed REST client memory image.
- `tests/script/test_c64script_keyboard_injection.c` — keyboard injection driven through
  C64Script automation.
- `tests/script/test_interact_key.c` — interactive key handling through OBS's interact mode.
- `tests/e2e/scenarios/ntsc_transport_legacy` / `ntsc_transport_rest` — OBS-driven, run
  automatically in CI: a `TYPE`/`KEY` script against the mock device
  (`framework/c64u_mock/server.py`), which serves both `machine:readmem`/`writemem` (KERNAL-buffer
  polling) and `machine:input` (matrix) on port 80, forced one way per scenario via
  `stream_control_transport`.
