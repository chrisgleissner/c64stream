# C64Script Tutorial

C64Script is the scripting language built into C64 Stream.

It is used to automate the parts of a C64 streaming or capture setup that are otherwise repetitive: changing effects, typing commands into the C64, starting recordings, launching programs on the Ultimate 64, waiting for specific timing points, and checking captured output. 

The language uses a line-oriented style that will feel natural to anyone who has used C64 BASIC.

This tutorial introduces the practical parts of the language by building from simple scripts to more useful automation examples. For full language rules, please see the [C64Script language specification](c64script-spec.md).

## What you can automate

Good first uses for C64Script are:

- cycling effects or color palettes while a demo runs
- resetting the machine, typing a short BASIC program, and then executing it
- playing SIDs or running PRGs while advancing through them with precise automated timing
- fetching data from external websites or local programs to feed directly into the C64
- starting and stopping OBS recording
- taking a screenshot and comparing it with a known-good image for tests
- logging each step so you can see what happened later

C64Script runs only when you explicitly start it from the plugin properties,
unless you enable the Auto-start script option yourself.

## Where scripts live

C64Script files use the `.c64script` extension. The plugin ships with examples in
[`data/scripts/`](../../data/scripts/), including:

| Script                                                                                        | What it demonstrates                                 |
| --------------------------------------------------------------------------------------------- | ---------------------------------------------------- |
| [`hello_world.c64script`](../../data/scripts/hello_world.c64script)                           | Variables, logging, labels, `IF`, `GOTO`, and `WAIT` |
| [`demo_palette_cycle.c64script`](../../data/scripts/demo_palette_cycle.c64script)             | Arrays, `FOR`/`NEXT`, and `PALETTE`                  |
| [`demo_effect_preset_cycle.c64script`](../../data/scripts/demo_effect_preset_cycle.c64script) | Effect presets and simple timing                     |
| [`demo_basic_hello_world.c64script`](../../data/scripts/demo_basic_hello_world.c64script)     | Resetting the C64 and typing a BASIC program         |
| [`demo_sid_playback_cycle.c64script`](../../data/scripts/demo_sid_playback_cycle.c64script)   | Playing SID files from `c64u:` paths                 |

You can copy one of those scripts and edit it, or create a new text file with a
`.c64script` suffix.

## Running your first script

1. Open the properties for your C64 Stream source or C64 Stream Effects filter.
2. In the scripting section, choose a `.c64script` file with **Browse**.
3. Click **Start Script**.
4. Watch the script status, and check the OBS log if the script uses `PRINT`.

Try the shipped palette demo first:

```text
data/scripts/demo_palette_cycle.c64script
```

It cycles through palette names, waits two seconds between each one, then exits.
That is a nice first test because it does not reset your C64 or touch files on
the Ultimate.

## Your first small script

Create a file called `my-first.c64script`:

```basic
REM My first C64Script

LOG "Starting my first script"

EFFECT "Classic CRT"
WAIT 2s

EFFECT "Green Monitor"
WAIT 2s

EFFECT "Default"

LOG "All done"
```

This script writes a few lines to the script log, changes the effect, waits, and
returns to the default look. It is deliberately simple: the first victory should
come quickly.

The important ideas are:

- `REM` starts a comment.
- Keywords are case-insensitive, but uppercase reads nicely if you grew up with
  BASIC.
- `WAIT 2s` waits two seconds. You can also use `500ms`, `2.5m`, `1h`, and so on.
- A script normally finishes when it reaches the end. You can use `STOP` or `END`
  when you want to be explicit.

## Logging

You can use logging to better understand what's going on whilst a script runs. This is also helpful for debugging if something goes wrong.

```basic
LOGFILE "my-session.log" TRUNCATE
LOG "Session started at " + TIME$()

TRON
EFFECT "Vintage TV"
WAIT 2s
EFFECT "Default"
TROFF

LOG "Session finished"
```

Use:

- `LOGFILE` to choose a log file;
- `LOG` for your own messages;
- `TRON` and `TROFF` to trace executed statements automatically;
- `PRINT` when you want output in the OBS log.

## Variables, strings, and numbers

Variables are created when you first assign to them:

```basic
COUNT = 3
TITLE$ = "Welcome back to the breadbin"

LOG TITLE$
LOG "Count is " + STR$(COUNT)
```

Some examples:

| Form        | Meaning                | Example                            |
| ----------- | ---------------------- | ---------------------------------- |
| `NAME$`     | String                 | `PATH$ = "c64u:/Games/demo.d64"`   |
| `COUNT`     | Numeric                | `COUNT = 5`                        |
| `I%`        | Integer-style variable | `I% = 3`                           |
| `ITEMS$()`  | String array           | `DIM ITEMS$(4)`                    |
| `CONFIG${}` | String map             | `CONFIG${"host"} = "192.168.1.64"` |

Use `STR$()` to turn a number into text and `VAL()` to turn text into a number:

```basic
COUNT = VAL(ENV("EFFECT_COUNT", "3"))
LOG "Running " + STR$(COUNT) + " effect cycles"
```

That `ENV()` example is handy when you want the same script to run with different
settings from a test runner or shell.

## Loops without line-numbers

Line numbers are supported, but you do not need them. Labels are usually easier
to read:

```basic
COUNT = 0

AGAIN:
    COUNT = COUNT + 1
    LOG "Cycle " + STR$(COUNT)
    EFFECT "Amber Monitor"
    WAIT 1s
    EFFECT "Green Monitor"
    WAIT 1s

    IF COUNT < 3 THEN GOTO AGAIN

EFFECT "Default"
```

For counted loops, use `FOR` and `NEXT`:

```basic
FOR I = 1 TO 3
    LOG "Pass " + STR$(I)
    WAIT 500ms
NEXT I
```

And when you have a list, arrays keep the script tidy:

```basic
DIM EFFECTS$(4)
EFFECTS$(0) = "Default"
EFFECTS$(1) = "Classic CRT"
EFFECTS$(2) = "Amber Monitor"
EFFECTS$(3) = "Green Monitor"

FOR I = 0 TO 3
    EFFECT EFFECTS$(I)
    WAIT 2s
NEXT I
```

## Automating effects and palettes

Effects and palettes are the safest place to start because they change the OBS
presentation without touching the C64 state:

```basic
PALETTE "Default"
EFFECT "Classic CRT"
WAIT 3s

EFFECTPARAM "preserve_size" 1
EFFECT "Arcade Cabinet"
WAIT 3s

EFFECT "Default"
```

`EFFECTPARAM` lets you tune effect parameters directly. The commonly useful
`preserve_size` parameter keeps the OBS source footprint stable while you change
effects.

You can also adjust individual C64 palette colors:

```basic
PALETTE "Default"
PALETTECOLOR 0, 0, 0, 0
PALETTECOLOR 1, 255, 255, 255
PALETTECOLOR 6, 0, 0, 170
```

The color index is `0` to `15`; RGB values are `0` to `255`.

## Typing into the C64

`TYPE` sends text through the C64 keyboard buffer. `KEY` sends a single key or
byte. This is excellent for BASIC loaders, menu selections, and simple startup
sequences:

```basic
LOG "Resetting and typing a tiny BASIC program"

RESET
WAIT 5s

TYPE "10 PRINT \"HELLO FROM C64SCRIPT\""
KEY 13
WAIT 500ms

TYPE "20 GOTO 10"
KEY 13
WAIT 500ms

TYPE "RUN"
KEY 13
```

You can also put carriage returns directly into a string:

`TYPE "LOAD\"*\",8,1\rRUN\r"`

Two practical notes:

- `TYPE` and `KEY` enqueue keystrokes; they do not prove the C64 has consumed
  them yet. Add a `WAIT`, or poll memory with `PEEK()` for tighter control.
- Keyboard injection uses the KERNAL keyboard buffer. Programs that scan the CIA
  keyboard matrix directly may ignore it.

## Running disks, PRGs, and SIDs

Ultimate commands can use paths on the device with the `c64u:` prefix, or local
files from the OBS computer where supported. Device paths are especially useful
for repeatable show setups:

```basic
DRIVE_MOUNT "c64u:/your/path/demo.d64"
RUNPRG "c64u:/your/path/intro.prg"
PLAYSID "c64u:/your/path/tune.sid" SONGNR 0
```

`DRIVE_MOUNT` mounts a disk image, `RUNPRG` runs a PRG, and `PLAYSID` plays a
SID.

Use paths that really exist on your device, or use local files from
the OBS computer if you prefer.

Some commands also have underscore aliases, such as `PLAY_SID`, `RUN_PRG`,
`MOUNT_DISK`, `RUN_LOCAL`, and `PALETTE_COLOR`. The compact forms above match the
language reference and are easy to type.

## Recording and screenshots in OBS

C64Script can control OBS recording:

```basic
LOG "Starting recording"
OBS RECORDING START

EFFECT "Classic CRT"
WAIT 5s

OBS RECORDING STOP
LOG "Recording stopped"
```

Always pair `OBS RECORDING START` with `OBS RECORDING STOP`. If a script starts
recording and then finishes without stopping it, OBS keeps recording.

For screenshots:

```basic
EFFECT "Default"
OBS WAIT FRAMES 2
OBS SCREENSHOT TARGET SOURCE PATH "current-frame.png"
```

`OBS WAIT FRAMES` is worth using before screenshots. It waits for fresh rendered
frames so you do not accidentally capture a stale image.

If you include a directory in the screenshot path, that parent directory must
already exist.

## A small image-based test

Sometimes you want to check the video output of the C64 and compare it with a known expected state, e.g. as part of writing tests.

The following script captures a frame and compares the PNG with itself, proving that screenshots and image assertions are wired up:

```basic
LOG "Preparing image check"

EFFECT "Default"
OBS WAIT FRAMES 10
OBS SCREENSHOT TARGET SOURCE PATH "current.png"

ASSERT IMAGE_EQUALS "current.png", "current.png" TOLERANCE 0
LOG "Image matched"
```

If the images differ, the assertion stops the script and writes a `.diff.png`
artifact beside the actual image. For real automated tests, compare the captured
file with a known-good PNG that you have already created and reviewed.

Use a small tolerance only when you genuinely expect harmless pixel differences.
For deterministic source captures, `TOLERANCE 0` is the cleanest target.

## HTTP, local programs, and file I/O

C64Script can talk to the outside world too. This read-only example asks your
Ultimate for its REST API version:

```basic
HTTP GET "http://c64u/v1/version" STATUS CODE RESPONSE BODY$
IF CODE = 200 THEN
    LOG "Ultimate replied: " + BODY$
ELSE
    LOG "Ultimate REST error: " + STR$(CODE)
ENDIF
```

You can run a local program:

```basic
RUNLOCAL "echo" ARGS "Hello from the host" STATUS EXIT_CODE OUTPUT OUT$
LOG "Exit code: " + STR$(EXIT_CODE)
LOG OUT$
```

And read or write local files:

```basic
WRITEFILE "show-title.txt", "Welcome back to the breadbin" TRUNCATE
READFILE "show-title.txt", TITLE$
LOG "Title: " + TITLE$

RUN_AT$ = "Ran at " + TIME$()
WRITEFILE "last-run.txt", RUN_AT$ TRUNCATE
```

These features are powerful. Treat scripts as code: only run scripts you trust,
and be careful with paths, HTTP endpoints, and local commands.

## Debugging your scripts

The built in small debugger allows you to troubleshoot common script issues:

- **Start / Stop** to run or halt a script;
- **Pause / Resume** to inspect what is happening;
- **Step** to execute one source line at a time;
- **Log variables** to dump current variable values to the OBS log;
- status fields for the last executed line, next line, and last runtime error.

For a first debugging pass:

1. Add `LOG` lines around the section you are unsure about.
2. Start the script.
3. Pause before the interesting part.
4. Step line by line.
5. Use **Log variables** when a value looks suspicious.

For more detail, see the [C64Script debugging guide](c64script-debugging.md).

## Creating reliable scripts

Small pauses and explicit checks improve the reliability of your scripts:

- After `RESET`, wait a few seconds before typing.
- After mounting a disk, wait briefly before `LOAD`, `RUN`, or `AUTOSTART`.
- Before screenshots, use `OBS WAIT FRAMES`.
- REST calls can fail or time out. Retry if needed and don't assume success.
- Use `LOG` before commands that change machine state.
- Avoid `POWEROFF` in experiments unless you really mean it.
- Put recordings in a clear `START` / `STOP` pair.
- Keep scripts short at first, then extract repeated work into labels,
  subroutines, or functions once the flow is proven.

## A complete starter script

Let's put some of what we've learned to use.

Here is a script that resets the machine, types a tiny BASIC program, changes the stream look, records for a short period, and then leaves
the C64 in a known reset state:

```basic
REM show-opener.c64script

LOGFILE "show-opener.log" TRUNCATE
LOG "Show opener started at " + TIME$()

RESET
WAIT 5s

TYPE "10 PRINT \"C64 STREAM IS LIVE\""
KEY 13
WAIT 500ms
TYPE "20 GOTO 10"
KEY 13
WAIT 500ms
TYPE "RUN"
KEY 13

EFFECTPARAM "preserve_size" 1
EFFECT "Classic CRT"

OBS RECORDING START
WAIT 5s
OBS RECORDING STOP

EFFECT "Default"
LOG "Show opener finished; resetting machine"
RESET
```

## Where to go next

Congratulations! You've reached the end of this tutorial.

Here are a few ideas on how to continue your journey:

- You may want to read the shipped scripts in [`data/scripts/`](../../data/scripts/) and adapt one small thing at a time.
- The runnable examples in this tutorial are also covered by
the local hardware E2E scenario [`ntsc_script_tutorial`](../../tests/e2e/scenarios/ntsc_script_tutorial/scenario.yaml).
- Finally, when you need the exact grammar, command list, limits, or full reference
examples, use the [C64Script language specification](c64script-spec.md).

Above all, have fun with C64Script!
