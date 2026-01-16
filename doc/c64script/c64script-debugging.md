# C64Script Debugging Guide

This guide describes the source-level debugging features available for C64Script, designed for casual users and non-developers.

## Overview

C64Script now includes minimal, intuitive debugging controls directly in the OBS properties panel. These controls allow you to:

- Pause and resume script execution
- Step through your script line-by-line
- View which line is currently executing
- See the next line that will execute
- Log all variable values to the OBS log

## Debug Controls

All debug controls are located in the OBS properties panel for your C64Stream source, in the REST Control & Automation section.

### Start / Stop Button

- **Label when stopped**: "Start"
- **Label when running**: "Stop"
- **Function**:
  - When stopped: Starts script execution from the beginning, resetting all state
  - When running/paused: Immediately stops the script and resets all state

### Pause / Resume Button

- **Label when running**: "Pause"
- **Label when paused**: "Resume"
- **Enabled**: Only when script is running or paused
- **Function**:
  - When running: Pauses execution at the next source line boundary
  - When paused: Resumes normal execution

**Note**: Pause occurs at clean source-line boundaries, not mid-instruction.

### Step Button

- **Label**: "Step"
- **Enabled**: Only when script is paused
- **Function**: Executes exactly one source line, then pauses again

This allows you to walk through your script line-by-line to understand its behavior or diagnose issues.

### Log Variables Button

- **Label**: "Log variables"
- **Enabled**: When script is running or paused
- **Function**: Logs all currently defined variables (names, types, and values) to the OBS log

Variable output format:

```log
🕹 SCRIPT: === Variable Dump ===
🕹 SCRIPT:   X = 42 (number)
🕹 SCRIPT:   MESSAGE = "Hello, World!" (string)
🕹 SCRIPT: === End Variable Dump ===
```

## Execution Visibility

### Execution State

Shows the current execution state:

- `stopped` - Script is not running
- `running` - Script is executing normally
- `paused` - Script is paused (use Resume or Step)
- `error` - Script encountered an error
- `completed` - Script finished successfully

### Last Executed Line

Shows the most recent source line that completed execution, in the format:

```log
<lineNumber>: <source line text>
```

Example: `5: X = X + 1`

At script start, this may show `(not started)`.

### Next Line to Execute

Shows the next source line that will execute if you continue, in the format:

```log
<lineNumber>: <source line text>
```

Example: `6: WAIT 1s`

At script completion, this shows `(completed)`.

### Last Runtime Error

Only displayed when an error occurs. Shows the error message from the script runtime.

## Debugging Workflow

### Basic Debugging

1. Load your script in the Script File property
2. Click **Start** to begin execution
3. Click **Pause** when you want to inspect state
4. Click **Log variables** to see current variable values in OBS log
5. Click **Step** to execute one line at a time
6. Watch the "Last executed" and "Next to execute" displays to follow execution
7. Click **Resume** to continue normal execution
8. Click **Stop** to terminate execution

### Debugging a Problem

If your script isn't behaving as expected:

1. Click **Start** to run the script
2. Click **Pause** before the problematic section
3. Click **Step** repeatedly to execute line-by-line
4. Watch the "Last executed" and "Next to execute" displays
5. Click **Log variables** after each step to see how values change
6. Check the OBS log for variable values and any error messages
7. Look for the "Last runtime error" display if an error occurs

### Debugging with Wait Commands

When stepping through a script that contains `WAIT` or `WAIT UNTIL` commands:

- In step mode, wait commands **execute immediately without waiting**
- This prevents the debugger from getting stuck on long waits
- The script will proceed to the next line instantly
- Normal wait behavior resumes when you click **Resume**

This design ensures smooth debugging without long pauses.

## Example Script

Here's a simple script you can use to test the debugging features:

```c64script
REM Simple counter with logging
COUNTER = 0

LABEL loop
  COUNTER = COUNTER + 1
  LOG "Counter value: " + STR(COUNTER)

  IF COUNTER >= 5 THEN
    GOTO done
  ENDIF

  WAIT 500ms
GOTO loop

LABEL done
LOG "Completed!"
```

Try this workflow:

1. Load and **Start** the script
2. Immediately click **Pause**
3. Click **Step** several times to watch COUNTER increment
4. Click **Log variables** to see COUNTER's value
5. Click **Resume** to let it complete
6. Review the OBS log to see the logged messages

## Tips

- **Use Step sparingly**: Stepping is most useful when you need to understand complex logic. For simple scripts, use Pause at key points.
- **Watch the line displays**: The "Last executed" and "Next to execute" displays are your primary debugging tools.
- **Check the OBS log**: Variable logging and script LOG commands output to the OBS log, which provides a complete execution trace.
- **Start fresh**: Always use **Stop** then **Start** to ensure clean state when debugging the same script multiple times.

## Limitations

- Debugging is **source-level only** - you see source lines, not internal VM instructions
- You cannot set **breakpoints** - use Pause when approaching the code you want to inspect
- You cannot **edit variables** during debugging - script state is read-only
- **No backwards stepping** - you can only step forward through the script

These limitations keep the debugger simple and focused on casual users.

## Performance

Debugging features are designed to have **no measurable performance impact** when:

- Script is running normally (not paused)
- Debugging controls are not being used

When actively stepping or paused, the plugin performs minimal overhead checks at source-line boundaries.
