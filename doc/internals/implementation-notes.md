# Implementation Notes

This document contains implementation notes for various features of the c64stream plugin.

## Keyboard Control

### Escape Key Behavior

When keyboard capture is active (OBS interact mode), the Escape key provides special control functions for the C64.

#### Escape (alone)

**Function:** BASIC Warm Start

**Behavior:**

- Aborts the currently running BASIC program
- Returns to the READY prompt
- **Non-destructive** - preserves user memory and BASIC program
- Does NOT reset the machine

**Technical Implementation:**

1. Reads the current IRQ vector from $0314/$0315
2. Writes BASIC warm start address ($A474) to the IRQ vector
3. Waits 40ms for the warm start to take effect
4. Restores the original IRQ vector

**Use Case:** Break out of a running BASIC program without losing your work.

#### Ctrl+Escape

**Function:** C64 Reset

**Behavior:**

- Performs a soft reset of the C64 via REST API
- Equivalent to pressing the reset button
- Clears the keyboard buffer
- **Destructive** - resets machine state

**Technical Implementation:**

- Calls the REST API endpoint: `PUT /v1/machine:reset`

**Use Case:** Fully reset the C64 to start fresh.

### Keyboard Capture

- Keyboard capture is automatically active when the source is in OBS interact mode
- No manual toggle is required
- Focus the C64 source in OBS to enable keyboard capture
- Unfocus or switch sources to disable keyboard capture

### Implementation Notes

- The 40ms delay for BASIC warm start occurs in the keyboard worker thread and **does not** affect video/audio processing
- Both Escape operations are safe to perform repeatedly
- Original IRQ vector is always restored after warm start

### Keyboard Injection (REST DMA)

**Mechanism:** background worker polls the C64 KERNAL keyboard buffer length and injects bytes only when empty.

**Memory locations:**

- Keyboard buffer: $0277–$0280 (max 10 bytes)
- Keyboard buffer length: $00C6

**REST endpoints used:**

- Read buffer length: `GET /v1/machine:readmem?address=00C6&length=1`
- Write buffer bytes: `PUT /v1/machine:writemem?address=0277&data=<hex>`
- Write buffer length: `PUT /v1/machine:writemem?address=00C6&data=<len>`

**Timing/backpressure:**

- Poll interval: 50 ms
- Only inject when `$00C6 == 0`
- Inject up to 10 bytes per write, then set `$00C6` to the injected byte count

**Consumption check (keystrokes read):**

- Poll `$00C6` until it returns to `0`.
- Optional verification: `GET /v1/machine:readmem?address=0277&length=<n>` to confirm staged bytes.

### Disk Image Autostart (D64/G64/D71/G71/D81)

**Mount + inject flow:**

- Optional reset before mount: `PUT /v1/machine:reset`, then wait 500 ms (RESET_DELAY_MS).
- Mount disk image from Ultimate filesystem: `POST /v1/drives/a:mount?path=<url-encoded-path>`.
- Inject autostart template via keyboard buffer (defaults to `LOAD"*",8,1\rRUN\r`).

**Autostart bytes (hex) for `LOAD"*",8,1⏎RUN⏎`:**

`4C 4F 41 44 22 2A 22 2C 38 2C 31 0D 52 55 4E 0D`

**Notes:**

- Bytes are written as raw PETSCII/ASCII; for this sequence, ASCII and PETSCII are identical.
- The template is configurable via `d64_autostart_template`; `\r` maps to byte `0x0D` (RETURN).
