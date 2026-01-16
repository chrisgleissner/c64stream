# Technical Details

This document contains technical implementation details for various features of the c64stream plugin.

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
