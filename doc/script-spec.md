# C64 Stream  Script Language Specification

This document defines the **current** `.c64script` language (“v1”) as implemented by the plugin, and proposes a **BASIC-inspired** evolution (“v2”) aimed at Commodore 64 users (labels and optional BASIC-style line numbers; `GOTO`/`GOSUB`; structured loops).

Scope:
- **v1 (implemented)**: `src/c64-script-parser.c/h`, `src/c64-script-executor.c/h`
- **v2 (proposed)**: a compatible evolution with proper block structure, expressions, and reusable subroutines.

Non-goals:
- This document does not specify UI/OBS integration details (Properties UI, key capture UX, etc.).
- This document does not guarantee that every command is fully implemented at runtime (see v1 “Runtime support notes”).

---

## 1. File Format (All Versions)

- **File extension**: `.c64script`
- **Encoding**: UTF-8 (ASCII subset is sufficient and recommended)
- **Line endings**: `LF` (`\n`) or `CRLF` (`\r\n`)
- **Size limit (v1 parser)**: ≤ 1 MiB
- **Primary mental model**: a script is a sequence of statements executed by a worker thread, sequentially, unless control flow changes the next statement.

---

## 2. C64Script v1 (Current Implementation)

### 2.1 Lexical Rules

v1 is a **line-oriented, whitespace-tokenized command language**.

- **Whitespace**: spaces and tabs separate tokens.
- **Empty lines**: ignored.
- **Comments**: a line whose first non-whitespace character is `#` is ignored.
- **Inline comments**: not a formal feature, but extra tokens are ignored by the parser for most commands; therefore `# ...` after the required arguments is effectively ignored.
- **Case sensitivity**: **keywords are case-sensitive** (must match exactly, e.g. `wait`, not `WAIT`).
- **No quoting / no escaping**:
  - Arguments cannot contain whitespace.
  - This particularly affects effect preset names and file paths containing spaces.

### 2.2 v1 Grammar (EBNF)

The grammar below is normative for what the v1 parser accepts.

**Lexemes**

```
WS          = {" " | "\t"} ;
EOL         = "\n" | "\r\n" ;
DIGIT       = "0"…"9" ;
INT         = ["-"], DIGIT, {DIGIT} ;
REAL        = ["-"], DIGIT, {DIGIT}, [".", DIGIT, {DIGIT}] ;
TOKEN       = ? any non-empty sequence of non-space and non-tab characters ? ;
LABELNAME   = TOKEN ;
PATH        = TOKEN ;
```

**Script**

```
script      = { line }, EOF ;
line        = [WS], ( comment | statement | empty ), EOL ;
empty       = "" ;
comment     = "#", { ? any character except EOL ? } ;
```

**Statements**

```
statement =
    effect
  | effect_param
  | palette
  | play_sid
  | run_prg
  | mount_disk
  | autostart
  | reset
  | reboot
  | wait
  | record_start
  | record_stop
  | stop
  | loop
  | label
  | goto
  ;

effect          = "effect", WS, TOKEN, {WS, TOKEN} ;
effect_param    = "effect_param", WS, TOKEN, WS, TOKEN, {WS, TOKEN} ;
palette         = "palette", WS, TOKEN, {WS, TOKEN} ;

play_sid        = "play_sid", WS, PATH, [WS, songnr], {WS, TOKEN} ;
songnr          = "songnr=", INT ;

run_prg         = "run_prg", WS, PATH, {WS, TOKEN} ;
mount_disk      = "mount_disk", WS, PATH, {WS, TOKEN} ;

autostart       = "autostart", {WS, TOKEN} ;
reset           = "reset", {WS, TOKEN} ;
reboot          = "reboot", {WS, TOKEN} ;

wait            = "wait", WS, duration, {WS, TOKEN} ;
duration        = REAL, ("ms" | "s" | "m") ;

record_start    = "record_start", {WS, TOKEN} ;
record_stop     = "record_stop", {WS, TOKEN} ;
stop            = "stop", {WS, TOKEN} ;

loop            = "loop", [WS, INT], {WS, TOKEN} ;
label           = "label", WS, LABELNAME, {WS, TOKEN} ;
goto            = "goto", WS, LABELNAME, {WS, TOKEN} ;
```

Notes:
- The `{WS, TOKEN}` tails above reflect the fact that v1 does not reject extra tokens.
- `PATH` is additionally classified at parse time as either:
  - **Local**: `PATH` not starting with `c64u:`
  - **C64U**: `PATH` starting with `c64u:`; the stored path becomes the substring after `c64u:`

### 2.3 Execution Model (v1)

1. Parse the file into a linear list of commands.
2. Build a label map from `label <name>` statements (duplicates are an error).
3. Execute commands sequentially on a worker thread.
4. Some commands mutate the “next command” index (`goto`); `wait` blocks with cancellation checks.
5. `stop` requests termination.

#### 2.3.1 Timing Resolution

`wait <duration>` is parsed in milliseconds, but the executor sleeps in **100 ms polling steps** and may overshoot:
- Small waits like `wait 50ms` will effectively sleep ~100 ms.

### 2.4 Command Reference (v1)

This section defines the **intended meaning** of each statement; runtime support depends on the executor implementation and available subsystems (REST client, source settings, etc.).

#### 2.4.1 Visual Control

- `effect <preset_name>`
  - Applies an effect preset to the source settings.
  - **Limitation (v1 syntax)**: `<preset_name>` cannot include spaces.

- `effect_param <name> <value>`
  - Sets a numeric effect parameter (`<value>` is parsed as `double`).

- `palette <palette_name>`
  - Sets the active palette by ID/name (string).

#### 2.4.2 C64U Playback & Machine Control

- `play_sid <path> [songnr=N]`
  - Plays a SID file.
  - `songnr=` is optional; if absent, defaults to `0`.
  - `<path>` may be prefixed with `c64u:` to reference the C64U filesystem.

- `run_prg <path>`
  - Runs a PRG.

- `mount_disk <path>`
  - Mounts a disk image (default drive A in the current executor).

- `autostart`
  - Intended: inject a `LOAD"*",8,1` + `RUN` sequence.

- `reset`
  - Soft reset of the machine.

- `reboot`
  - Hard reboot of the machine.

#### 2.4.3 Recording Control

- `record_start`
  - Starts plugin-side capture (CSV/network recording) if available.

- `record_stop`
  - Stops plugin-side capture (CSV/network recording) if available.

#### 2.4.4 Control Flow

- `label <name>`
  - Declares a jump target. Labels are “passive” at runtime.

- `goto <name>`
  - Transfers control to the labeled statement.

- `loop [count]`
  - v1 includes syntax for loops, but the current executor’s loop semantics are **not fully specified** by v1 and are not reliable as a structured block mechanism.
  - v2 replaces this with proper `FOR`/`WHILE` blocks.

- `stop`
  - Stops script execution.

### 2.5 Runtime Support Notes (v1)

As of the current executor implementation:
- `c64u:` paths are supported for `play_sid`, `run_prg`, and `mount_disk`.
- Local file upload variants are placeholders (`TODO`) and currently fail.
- `autostart` currently fails (not implemented in the executor).

### 2.6 Implementation Limits (v1)

These limits are implementation-defined by the current parser/executor and may change:

- Max script size: **1 MiB**
- Max labels: **64** (`MAX_LABELS`)
- Max loop nesting: **16** (`MAX_LOOP_STACK`)
- Fixed-size argument buffers per command (long tokens may be truncated):
  - `arg1`: 512 bytes
  - `arg2`: 256 bytes

---

## 3. C64Script v2 (Proposed): BASIC-Inspired, Label-Oriented

### 3.1 Design Goals

- **Familiar to Commodore 64 users**:
  - BASIC-like keywords (`IF`, `THEN`, `FOR`, `NEXT`, `GOSUB`, `RETURN`, `REM`, …)
  - Numeric truthiness (`0` = false, non-zero = true)
  - Optional `LET`
- **Labels first; line numbers optional**:
  - Labels allow assembly-like structure (`START:`) without forcing line numbers.
  - BASIC-style line numbers (`10`, `20`, …) are optional and act like implicit labels for `GOTO`/`GOSUB`.
  - Labels are case-insensitive and may be alphanumeric or numeric-only.
- **Proper structured blocks**:
  - `FOR … NEXT`, `WHILE … WEND` (or `ENDWHILE`), block `IF … THEN … ENDIF`
  - Nested blocks are allowed.
- **Script ergonomics**:
  - Quoted strings allow spaces in preset names and file paths.
  - `:` statement separator allows “BASIC-like” single-line sequences.
- **Useful for this plugin**:
  - Variables for paths/durations, basic arithmetic, boolean logic.
  - Optional `POKE`/`PEEK` style access to REST DMA (natural to C64 users).
  - Optional `TYPE`/`KEY` for keystroke injection (autostart, menu navigation).
  - BASIC-like tracing and progress logs (`LOG`, `LOGFILE`, `TRON`, `TROFF`).

### 3.2 Compatibility Strategy

Recommended: make v2 a **superset** of v1.

- v1 scripts remain valid in v2 (same command vocabulary, same duration literals).
- v2 adds quoting, expressions, structured blocks, and BASIC-like spelling.
- Keywords, labels, and identifiers are case-insensitive in v2 (`WAIT`, `wait`, `WaIt` are equivalent).
- Where v1 uses underscores (e.g., `play_sid`, `run_prg`), v2 may additionally accept underscorless aliases (e.g., `PLAYSID`, `RUNPRG`).

### 3.3 Lexical Rules (v2)

**Case sensitivity**
- Keywords, labels, and identifiers are **case-insensitive** (`goto`, `GOTO`, and `GoTo` are the same).
- A recommended canonical form for display/logging is uppercase keywords and labels (BASIC style).

**Whitespace**
- Spaces and tabs separate tokens, except inside quoted strings.

**Comments**
- `#` at the start of a (trimmed) line is a comment (v1 compatibility).
- `REM` is a statement that comments out the rest of the line (BASIC style). After `REM`, the remainder of the line is ignored (including any `:`).

**Statement separator**
- `:` separates multiple statements on the same line (BASIC style).

**Strings**
- Double-quoted: `"..."`.
- To include a quote, either:
  - use doubled quotes: `"He said ""RUN""."` (BASIC style), or
  - use an escape sequence: `"He said \"RUN\"."`
- Recommended escape sequences:
  - `\\` (literal backslash), `\"` (quote)
  - `\r` (RETURN), `\n` (line feed), `\t` (tab)
  - `\xNN` (byte value, hex; useful for PETSCII/control bytes)

**Identifiers**
- Letter followed by letters/digits/underscore.
- Optional BASIC-like type suffix:
  - `$` string variable (e.g., `PATH$`)
  - `%` integer variable (optional; implementer may treat it as an integer constraint)

**Labels (including line numbers)**
- A line may optionally start with a **label** that acts like an implicit jump target.
- Labels may be:
  - alphanumeric (e.g., `START`, `PLAY2`, `DEMO2026`), or
  - numeric-only (e.g., `10`, `12345`) to resemble BASIC line numbers.
- Labels may end with `:` (recommended), but the colon is optional.
- A label can appear on the same line as code, or on a line by itself to label the following line.
- Disambiguation rule (keeps the language usable without becoming a hybrid monster):
  - At the start of a line, an alphanumeric token is treated as a label **only if** it is followed by `:` or end-of-line, or it is followed by whitespace that is **not** immediately followed by `=`.
  - This ensures `I = 0` is an assignment, while `START I=0` can be a label + statement.

**Numbers**
- Decimal integers and reals: `10`, `1.5`
- Optional hex integer literals (C64/assembly friendly):
  - `$C000` (hex), `$00C6` (hex)

**Durations**
- Duration literal: `number` + unit: `500ms`, `1.5s`, `0.5m`
- Additionally, allow `WAIT <expr> [unit]` with default unit `s`:
  - `WAIT 1.5` means `1.5s`

### 3.4 v2 Grammar (EBNF)

This grammar defines the proposed v2 language.

**Lexemes**

```
WS              = {" " | "\t"} ;
EOL             = "\n" | "\r\n" ;
DIGIT           = "0"…"9" ;
HEX             = DIGIT | "A"…"F" | "a"…"f" ;

identifier      = ( "A"…"Z" | "a"…"z" ), { "A"…"Z" | "a"…"z" | DIGIT | "_" }, ["$" | "%"] ;
label_name      = ( "A"…"Z" | "a"…"z" ), { "A"…"Z" | "a"…"z" | DIGIT } ;

number          = DIGIT, {DIGIT}, [".", DIGIT, {DIGIT}] ;
hex_number      = "$", HEX, {HEX} ;

line_number     = DIGIT, {DIGIT} ;
label           = label_name | line_number ;
label_ref       = label ;

string_literal  = "\"", { string_char }, "\"" ;
string_char     = ? any character except `"`, `\`, and EOL ? | "\"\"" | escape_seq ;
escape_seq      = "\\", ("\\" | "\"" | "r" | "n" | "t" | ("x", HEX, HEX)) ;

duration_lit    = number, ("ms" | "s" | "m") ;
```

**Top level**

```
script          = { line }, EOF ;
line            = [WS], [label_def], [stmt_list], EOL
                | [WS], comment_line, EOL
                | [WS], EOL ;

label_def       = label, [":"], [WS] ;
stmt_list       = statement, { [WS], ":", [WS], statement } ;
comment_line    = "#", { ? any character except EOL ? } ;
```

**Statements**

```
statement =
    rem_stmt
  | assignment
  | if_stmt
  | for_stmt
  | while_stmt
  | label_stmt
  | goto_stmt
  | gosub_stmt
  | return_stmt
  | stop_stmt
  | wait_stmt
  | effect_stmt
  | effect_param_stmt
  | palette_stmt
  | play_sid_stmt
  | run_prg_stmt
  | mount_disk_stmt
  | autostart_stmt
  | reset_stmt
  | reboot_stmt
  | record_start_stmt
  | record_stop_stmt
  | type_stmt
  | key_stmt
  | logfile_stmt
  | log_stmt
  | tron_stmt
  | troff_stmt
  | print_stmt
  | poke_stmt
  ;

rem_stmt         = "REM", { ? any character except EOL ? } ;

assignment      = ["LET", WS], identifier, [WS], "=", [WS], expr ;

label_stmt      = "LABEL", WS, label_ref ;
goto_stmt       = "GOTO", WS, label_ref ;
gosub_stmt      = "GOSUB", WS, label_ref ;
return_stmt     = "RETURN" ;
stop_stmt       = "STOP" | "END" ;

wait_stmt       = "WAIT", WS, (wait_until | wait_duration) ;
wait_duration   = wait_arg ;
wait_until      = "UNTIL", WS, expr ;
wait_arg        = duration_lit | expr, [WS, ("ms" | "s" | "m")] ;

if_stmt         =
    "IF", WS, bool_expr, WS, "THEN", WS, statement, [WS, "ELSE", WS, statement]
  | "IF", WS, bool_expr, WS, "THEN", EOL,
        { line },
    ["ELSE", EOL, { line }],
    "ENDIF"
  ;

for_stmt        = "FOR", WS, identifier, [WS], "=", [WS], expr, WS, "TO", WS, expr,
                  [WS, "STEP", WS, expr], EOL,
                      { line },
                  "NEXT", [WS, identifier] ;

while_stmt      = "WHILE", WS, bool_expr, EOL,
                      { line },
                  ("WEND" | "ENDWHILE" | ("END", WS, "WHILE")) ;
```

**Plugin actions**

```
effect_stmt         = "EFFECT", WS, string_or_ident ;
effect_param_stmt   = ("EFFECT_PARAM" | "EFFECTPARAM"), WS, identifier, WS, expr ;
palette_stmt        = "PALETTE", WS, string_or_ident ;

play_sid_stmt       = ("PLAY_SID" | "PLAYSID"), WS, path_expr, [WS, "SONGNR", [WS], "=", [WS], expr] ;
run_prg_stmt        = ("RUN_PRG" | "RUNPRG"), WS, path_expr ;
mount_disk_stmt     = ("MOUNT_DISK" | "MOUNTDISK"), WS, path_expr, [WS, "DRIVE", [WS], "=", [WS], string_or_ident] ;

autostart_stmt      = "AUTOSTART", [WS, string_literal] ;
reset_stmt          = "RESET" ;
reboot_stmt         = "REBOOT" ;

record_start_stmt   = ("RECORD_START" | "RECORDSTART") ;
record_stop_stmt    = ("RECORD_STOP" | "RECORDSTOP") ;

type_stmt           = "TYPE", WS, expr ;
key_stmt            = "KEY", WS, (string_or_ident | number | hex_number) ;

logfile_stmt        = "LOGFILE", WS, path_expr, [WS, ("APPEND" | "TRUNCATE")] ;
log_stmt            = "LOG", WS, expr ;

tron_stmt           = "TRON" ;
troff_stmt          = "TROFF" ;

print_stmt          = "PRINT", WS, expr ;

poke_stmt           = "POKE", WS, expr, [WS], ",", [WS], expr
                    | "POKE", WS, expr, [WS], ",", [WS], "[", expr, { [WS], ",", [WS], expr }, "]" ;
```

**Expressions**

```
expr            = or_expr ;
bool_expr       = or_expr ;

or_expr         = xor_expr, { WS, ("OR" | "||"), WS, xor_expr } ;
xor_expr        = and_expr, { WS, "XOR", WS, and_expr } ;
and_expr        = not_expr, { WS, ("AND" | "&&"), WS, not_expr } ;
not_expr        = ["NOT" | "!"], [WS], rel_expr ;

rel_expr        = add_expr, [WS, rel_op, WS, add_expr] ;
rel_op          = "=" | "<>" | "<" | "<=" | ">" | ">=" ;

add_expr        = mul_expr, { [WS], ("+" | "-"), [WS], mul_expr } ;
mul_expr        = unary_expr, { [WS], ("*" | "/"), [WS], unary_expr } ;
unary_expr      = ["+" | "-"], [WS], primary | primary ;

primary         = number | hex_number | string_literal | function_call | identifier | "(", [WS], expr, [WS], ")" ;

function_call   = identifier, [WS], "(", [WS], [expr, { [WS], ",", [WS], expr }], [WS], ")" ;

string_or_ident = string_literal | identifier ;
path_expr       = string_literal | identifier ;
label_ref       = label ;
```

### 3.5 Semantics (v2)

#### 3.5.1 Variables and Types

- Variables are global by default (BASIC-like simplicity).
- Types:
  - Numeric: IEEE-754 `double` (recommended)
  - String: UTF-8
  - Boolean: derived from numeric truthiness (`0` false, non-zero true)
- Assignments:
  - `LET` is optional: `X = 10` and `LET X = 10` are equivalent.

#### 3.5.2 Control Flow and Stacks

The executor maintains explicit stacks:
- `FOR` stack: loop variable, end value, step, loop start location.
- `WHILE` stack: loop condition location, loop start location.
- `GOSUB` stack: return address.

Limits should be explicit (e.g., max nesting depth) and produce clear runtime errors when exceeded.

#### 3.5.3 Boolean Logic

- Relational operators return numeric truth values (`0` or `1` recommended).
- `NOT`, `AND`, `OR` follow typical BASIC precedence (`NOT` > `AND` > `OR`).

#### 3.5.4 Plugin-Specific I/O

These statements/functions exist to make C64 automation scripts practical in the context of this plugin (REST control, keyboard injection, and reproducible captures).

**Effects / palettes**
- `EFFECT`, `EFFECTPARAM`, `PALETTE` update OBS source settings.

**C64U runners / machine control**
- `PLAYSID`, `RUNPRG`, `MOUNTDISK`, `RESET`, `REBOOT` call Ultimate 64 REST actions.

**Memory access (`PEEK`/`POKE`)**
- `POKE <address>, <value>` writes one byte to C64 memory via DMA.
  - `<address>` must be in `0..65535` (hex form: `$0000..$FFFF`).
  - `<value>` is truncated to 8-bit (`value & 255`), matching C64 expectations.
  - If `<address>`/`<value>` are not numeric at runtime, the executor should raise a BASIC-style “TYPE MISMATCH” error.
- `POKE <address>, [v1, v2, ...]` writes a contiguous byte block starting at `<address>`.
  - Each element is truncated to 8-bit.
  - Implementations should chunk writes to match REST constraints (e.g., ≤ 128 bytes per DMA write).
- `PEEK(<address>)` returns a numeric value `0..255` read from C64 memory via DMA.
  - `PEEK` is a built-in function; unknown function calls should raise an “UNDEF'D FUNCTION” style error.
  - Built-in function names should be treated case-insensitively (`peek($00C6)` is valid).
  - If `<address>` is not numeric at runtime, the executor should raise a BASIC-style “TYPE MISMATCH” error.
  - `PEEK`/`POKE` are network operations and may fail or time out; failures should stop execution with a clear error message by default.

Important REST constraint:
- DMA writes cannot target 6510 I/O registers; `POKE $D020, ...` (border color) is expected to fail on real hardware.

**Keyboard injection (`TYPE`/`KEY`)**
- `TYPE <string_expr>` enqueues keystrokes derived from text.
  - The string is converted to injected bytes using the “BASIC-friendly ASCII→PETSCII” rules described in `doc/rest-control.md`.
  - Escape sequences like `\r` are useful for RETURN.
  - If `<string_expr>` is not a string at runtime, the executor should raise a BASIC-style “TYPE MISMATCH” error.
- `KEY <name>` enqueues one symbolic key press (or one raw byte).
  - `<name>` is an identifier or string like `RETURN`, `RUNSTOP`, `HOME`, `CLEAR`, `CURSOR_UP`, `CURSOR_DOWN`, `CURSOR_LEFT`, `CURSOR_RIGHT`.
  - If a numeric/hex literal is provided (`13` or `$0D`), it injects that byte value directly.
  - Key names should be treated case-insensitively (`return`, `RETURN`, `ReTuRn`).
  - `TYPE`/`KEY` enqueue locally; they do not imply the C64 has already consumed the keystrokes. Use `WAIT` or memory polling (`PEEK`) where necessary.

Important injection constraint:
- Keyboard injection is KERNAL keyboard-buffer based; it will not work for software that reads the CIA keyboard matrix directly.

**Logging / tracing (`LOG`, `LOGFILE`, `TRON`, `TROFF`)**
- `LOGFILE <path> [APPEND|TRUNCATE]` selects a script log file destination.
  - Relative paths should resolve relative to the script file’s directory.
  - If the mode is omitted, default is `APPEND`.
  - If `LOGFILE` was never called, the first `LOG` or `TRON` should implicitly open a default log file (recommended: `<script_basename>.log` next to the script).
  - If `<path>` is not a string at runtime, the executor should raise a BASIC-style “TYPE MISMATCH” error.
- `LOG <expr>` appends a line to the script log file (string values written as-is; numeric values formatted in decimal).
- `TRON` enables tracing: the executor logs every executed statement automatically (recommended to include line number + label + progress).
- `TROFF` disables tracing.
- `PRINT <expr>` writes to the OBS log (not the script log file).

### 3.6 Examples (v2)

**Example A: BASIC-like sequence with quoted preset names**

```basic
REM Fade in, then run a demo
EFFECT "Classic CRT" : PALETTE "colodore"
WAIT 2
RUNPRG "c64u:/Programs/demo.prg"
WAIT 60s
END
```

**Example B: Label + IF + GOTO (no line numbers)**

```basic
START:
I = 0

LOOP:
I = I + 1
EFFECT "Vintage TV"
WAIT 1.5

IF I < 10 THEN GOTO LOOP ELSE GOTO DONE

DONE:
EFFECT "Default"
END
```

**Example C: FOR/NEXT**

```basic
FOR I = 1 TO 5
    PALETTE "pepto_ntsc"
    WAIT 1
    PALETTE "vice_new"
    WAIT 1
NEXT I
END
```

**Example D: GOSUB/RETURN as “functions”**

```basic
PATH$ = "c64u:/Temp/music/galway_collection.sid"

GOSUB PLAYTRACK
TRACK = 2 : GOSUB PLAYTRACK
TRACK = 3 : GOSUB PLAYTRACK
END

PLAYTRACK:
PLAYSID PATH$ SONGNR=TRACK
WAIT 20
RETURN
```

**Example E: PEEK/POKE + typing an autostart sequence**

```basic
LOGFILE "run.log" TRUNCATE
LOG "Starting"

REM Write a small marker block (example RAM area)
POKE $C000, [1, 2, 3, 4]
LOG "Marker written"

REM Wait until the KERNAL keyboard buffer is empty, then type
WHILE PEEK($00C6) <> 0
    WAIT 50ms
WEND

TYPE "LOAD\"*\",8,1\rRUN\r"
WAIT 10s
END
```

**Example F: TRON/TROFF for automatic progress logging**

```basic
LOGFILE "trace.log" TRUNCATE
TRON

EFFECT "Classic CRT" : PALETTE "colodore"
WAIT 2
EFFECT "Default"

TROFF
LOG "Done"
END
```

---

## 4. Recommended Next Steps (Implementation Roadmap)

If/when implementing v2 in code:

1. Add a v2 parser (tokenizer + recursive descent) with quoted strings and `:` statement separators.
2. Keep v1 compatibility mode (existing whitespace-command lines) as a fallback.
3. Implement structured block execution (`FOR`/`WHILE`/block `IF`) with explicit stacks and clear diagnostics.
4. Add `GOSUB`/`RETURN` (subroutines) before full user-defined functions; it matches C64 BASIC idioms and solves most reuse needs.
5. Add `POKE`/`PEEK`, `TYPE`/`KEY`, and `LOG`/`TRON` to unlock classic C64 automation workflows.
