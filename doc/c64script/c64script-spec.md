# C64Script Language Specification

This document defines the BASIC-inspired `.c64script` language as implemented by the C64Stream OBS Studio plugin.

## 1. File format

- **File extension**: `.c64script`
- **Encoding**: UTF-8 (ASCII subset is sufficient and recommended)
- **Line endings**: `LF` (`\n`) or `CRLF` (`\r\n`)
- **Size limit (current parser)**: ≤ 1 MiB
- **Primary mental model**: a script is a sequence of statements executed by a worker thread, sequentially, unless control flow changes the next statement.

---

## 2. C64Script Language

BASIC-Inspired, Label-Oriented

### 2.1 Design Goals

- **Familiar to Commodore 64 users**:
  - BASIC-like keywords (`IF`, `THEN`, `FOR`, `NEXT`, `GOSUB`, `RETURN`, `REM`, …)
  - Numeric truthiness (`0` = false, non-zero = true)
  - Optional `LET`
  - Classic built-in functions (`LEFT$`, `RIGHT$`, `MID$`, `CHR$`, `ASC`, `RND`, etc.)
- **Labels first; line numbers optional**:
  - Labels allow assembly-like structure (`START:`) without forcing line numbers.
  - BASIC-style line numbers (`10`, `20`, …) are optional and act like implicit labels for `GOTO`/`GOSUB`.
  - Labels are case-insensitive and may be alphanumeric or numeric-only.
- **Proper structured blocks**:
  - `FOR … NEXT`, `WHILE … WEND` (or `ENDWHILE`), block `IF … THEN … ENDIF`
  - Nested blocks are allowed.
- **Modern programming features**:
  - User-defined functions with `FUN`/`ENDFUN` and local scope
  - Parameterized `GOSUB` for BASIC-style subroutines with arguments
  - Arrays (`DIM DATA(10)`) and maps (`CONFIG{"key"}`) for structured data
  - Extended variable types: numeric, string, integer, boolean, array, map
  - Automatic type casting with clear rules
  - Extended duration units: milliseconds, seconds, minutes, hours, days
- **Script ergonomics**:
  - Quoted strings allow spaces in preset names and file paths.
  - Rich built-in function library for string manipulation, math, random numbers
- **Useful for this plugin**:
  - Variables for paths/durations, basic arithmetic, boolean logic.
  - Optional `POKE`/`PEEK` style access to REST DMA (natural to C64 users).
  - Optional `TYPE`/`KEY` for keystroke injection (autostart, menu navigation).
  - BASIC-like tracing and progress logs (`LOG`, `LOGFILE`, `TRON`, `TROFF`).
  - HTTP REST calls for external API integration
  - Local program execution for workflow automation
  - File I/O for configuration and data processing
  - Fine-grained effect parameter control (`EFFECTPARAM`)
  - Per-color palette customization (`PALETTECOLOR`)

### 2.2 Lexical Rules

#### 2.2.1 Case sensitivity

- Keywords, labels, and identifiers are **case-insensitive** (`goto`, `GOTO`, and `GoTo` are the same).
- A recommended canonical form for display/logging is uppercase keywords and labels (BASIC style).

#### 2.2.2 Whitespace

- Spaces and tabs separate tokens, except inside quoted strings.

#### 2.2.3 Comments

- `REM` is a statement that comments out the rest of the line (BASIC style). After `REM`, the remainder of the line is ignored.

#### 2.2.4 Strings

- Double-quoted: `"..."`.
- To include a quote, either:
  - use doubled quotes: `"He said ""RUN""."` (BASIC style), or
  - use an escape sequence: `"He said \"RUN\"."`
- Recommended escape sequences:
  - `\\` (literal backslash), `\"` (quote)
  - `\r` (RETURN), `\n` (line feed), `\t` (tab)
  - `\xNN` (byte value, hex; useful for PETSCII/control bytes)

#### 2.2.5 Identifiers

- Letter followed by letters/digits/underscore.
- Optional BASIC-like type suffix:
  - `$` string variable (e.g., `PATH$`)
  - `%` integer variable (optional; implementer may treat it as an integer constraint)
  - `()` array variable (e.g., `DATA()` - subscripts specified at access time)
  - `{}` map variable (e.g., `CONFIG{}` - keys specified at access time)
- Variables without suffixes are numeric (double precision) by default.

#### 2.2.6 Labels (including line numbers)

- A line may optionally start with a **label** that acts like an implicit jump target.
- Labels may be:
  - alphanumeric (e.g., `START`, `PLAY2`, `DEMO2026`), or
  - numeric-only (e.g., `10`, `12345`) to resemble BASIC line numbers.
- Labels may end with `:` (recommended), but the colon is optional.
- Label names should not use reserved keywords (e.g., avoid naming a label `IF` or `GOTO`).
- A label can appear on the same line as code, or on a line by itself to label the following line.
- Disambiguation rule:
  - At the start of a line, an alphanumeric token is treated as a label **only if** it is followed by `:` or end-of-line, or it is followed by whitespace that is **not** immediately followed by `=`.
  - This ensures `I = 0` is an assignment, while `START I=0` can be a label + statement.

#### 2.2.8 Numbers

- Decimal integers and reals: `10`, `1.5`
- Optional hex integer literals (C64/assembly friendly):
  - `$C000` (hex), `$00C6` (hex)

#### 2.2.9 Durations

- Duration literal: `number` + unit: `500ms`, `1.5s`, `0.5m`, `2h`, `3d`
- Supported units:
  - `ms` (milliseconds)
  - `s` (seconds)
  - `m` (minutes)
  - `h` (hours)
  - `d` (days)
- Additionally, allow `WAIT <expr> [unit]` with default unit `s`:
  - `WAIT 1.5` means `1.5s`

### 2.3 Grammar

The complete formal grammar for C64Script is maintained in a separate file:

#### 2.3.1 [`c64script-grammar.ebnf`](c64script-grammar.ebnf)

This EBNF grammar defines the language syntax including:

- **Lexemes**: Identifiers, numbers (decimal/hex), strings, duration literals
- **Top-level structure**: Scripts, lines, labels, comments
- **Statements**: All control flow, assignments, and command statements
- **Plugin actions**: EFFECT, PALETTE, C64 control, recording, keyboard, HTTP, file I/O
- **Expressions**: Complete operator precedence and expression syntax

The grammar serves as the authoritative reference for:

- Implementing parsers and validators
- Generating syntax highlighters and language tools
- Understanding language structure and precedence rules

### 2.4 Semantics

#### 2.4.1 Variables and Types

- Variables are global by default (BASIC-like simplicity).
- **Supported types**:
  - **Numeric**: IEEE-754 `double` (default for unsuffixed variables)
  - **String**: UTF-8 (variables with `$` suffix)
  - **Integer**: 32-bit signed integer (variables with `%` suffix; stored as numeric but constrained to integer range)
  - **Boolean**: derived from numeric truthiness (`0` false, non-zero true)
  - **Array**: one-dimensional indexed collection (variables with `()` suffix; zero-based indexing)
    - Declaration: `DIM DATA(10)` creates an array with 11 elements (indices 0-10)
    - Access: `DATA(5)` reads/writes element at index 5
    - Arrays store numeric values by default; use `DATA$()` for string arrays
  - **Map**: key-value collection (variables with `{}` suffix; string keys)
    - Access: `CONFIG{"host"}` reads/writes value for key "host"
    - Keys are always strings; values are numeric by default; use `CONFIG${}` for string-valued maps
- **Type inference**:
  - Variables are created on first assignment.
  - Type is determined by suffix or value type at first assignment.
- **Assignments**:

  - Array/map elements: `DATA(3) = 42`, `CONFIG{"port"} = 8080`

##### 2.4.1.1 Array Declaration with `DIM`

- `DIM <array_var>(<size>)` allocates an array with `<size>` elements (indices 0 to `<size>-1`).
- Example: `DIM VALUES(10)` creates an array with 10 numeric elements (indices 0-9).
- Example: `DIM NAMES$(5)` creates a string array with 5 elements (indices 0-4).
- Arrays are initialized with default values: `0` for numeric, `""` for strings.
- `<size>` must be greater than 0.
- Re-declaring an array with `DIM` reallocates it, discarding previous contents.
- Maps (`{}` suffix) do not require `DIM`; they are created on first access and grow dynamically.

##### 2.4.1.2 Automatic Type Casting Rules

The language performs automatic type conversion in specific contexts to maintain BASIC-like simplicity while preventing common errors:

1. **Numeric to String** (automatic):
   - When a numeric value is used in string context (concatenation, string assignment).
   - Format: decimal representation with minimal precision (e.g., `123`, `3.14`).
   - Example: `MSG$ = "Count: " + 42` results in `"Count: 42"`

2. **String to Numeric** (explicit only):
   - Strings are not automatically converted to numbers in arithmetic or relational contexts.
   - Use `VAL(<string>)` for explicit conversion.
   - Using a string in numeric context raises "TYPE MISMATCH".
   - Example: `X = VAL("123") + 1` results in `124`

3. **Numeric to Boolean** (automatic):
   - `0` becomes false; any non-zero value becomes true.
   - Used in `IF`, `WHILE`, and boolean operators.

4. **Boolean to Numeric** (automatic):
   - `false` becomes `0`; `true` becomes `-1` (BASIC convention).

5. **Array/Map to Scalar** (NOT automatic):
   - Arrays and maps cannot be automatically converted to scalar values.
   - Use explicit indexing: `DATA(0)` not `DATA()`.
   - Attempting to use array/map without index raises "TYPE MISMATCH" error.

6. **Integer suffix `%`** (constraining cast):
   - When assigning to integer variable, value is truncated toward zero.
   - Range: -2147483648 to 2147483647
   - Overflow wraps around (implementation-defined behavior).
   - Example: `I% = 3.7` stores `3`

##### 2.4.1.3 Type Mismatch Errors

The following operations raise "TYPE MISMATCH" errors:

- Using array/map without subscript in scalar context
- Using scalar in array/map subscript context
- Passing wrong type to built-in functions (e.g., `PEEK("hello")`)
- Invalid string-to-numeric conversion

##### 2.4.1.4 Type Compatibility in Expressions

- Numeric operators (`+`, `-`, `*`, `/`) require numeric operands.
- String operator (`+` for concatenation) casts operands to string when at least one operand is string.
- Relational operators (`=`, `<`, `>`, etc.) compare:
  - numbers if both operands are numeric, or
  - strings if both operands are strings (case-sensitive lexicographic order).
- Mixed numeric/string relational comparisons raise "TYPE MISMATCH".

#### 2.4.2 Control Flow and Stacks

##### 2.4.2.1 Script Termination

- Scripts terminate when reaching: `STOP` (or `END`), `GOTO`, `LOOP`, or **running out of instructions** (implicit termination).
- Implicit termination: If the instruction pointer reaches the end of bytecode without encountering an explicit termination statement, the script completes successfully.
- This allows scripts to omit trailing `STOP` statements for cleaner code.

The executor maintains explicit stacks:

- `FOR` stack: loop variable, end value, step, loop start location.
- `WHILE` stack: loop condition location, loop start location.
- `GOSUB` stack: return address, saved parameter values (if any), saved local variables (for functions).
- `FUNCTION` stack: function name, parameter names, local variable scope.

##### 2.4.2.2 Functions and Parameterized Subroutines

###### 2.4.2.2.1 User-defined functions (modern approach)

- Syntax: `FUN <name>([param1, param2, ...])` ... `ENDFUN`
- Functions create a local scope; parameters and variables declared inside are local.
- Call with `<name>([arg1, arg2, ...])` in expression context.
- Return value with `RETURN <expr>`; returns 0 if no expression provided.
- Example:

  ```basic
  FUN ADD(A, B)
      RETURN A + B
  ENDFUN

  RESULT = ADD(3, 5)  REM RESULT = 8
  ```

###### 2.4.2.2.2 Parameterized GOSUB (BASIC-inspired approach)

- Syntax: `GOSUB <label>([expr1, expr2, ...])`
- Parameters are passed by creating numbered local variables `PARAM1`, `PARAM2`, etc.
- These variables are accessible within the subroutine until `RETURN`.
- After `RETURN`, parameter variables are destroyed.
- Optional return value: `RETURN <expr>` stores result in special variable `RESULT`.
- Example:

  ```basic
  GOSUB MULTIPLY(5, 7)
  PRINT RESULT  REM Prints 35
  END

  MULTIPLY:
      RESULT = PARAM1 * PARAM2
      RETURN
  ```

###### 2.4.2.2.3 Design note: Both mechanisms are supported

- `FUN`/`ENDFUN` is recommended for new scripts (clearer scoping, modern style).
- `GOSUB` with parameters maintains BASIC heritage and is convenient for simple parameterized subroutines.
- `FUN` calls can appear in expressions; `GOSUB` calls are statements only.

Labels and line numbers:

- A label is either an alphanumeric name (e.g., `START`) or a numeric-only “line number” (e.g., `10`).
- Labels are case-insensitive; numeric labels should be normalized by value (`0010` == `10`).
- Numeric labels are labels only (no implicit ordering requirement), but authors may choose to keep them increasing to resemble BASIC listings.
- A label can be defined:
  - as a line prefix: `START:` / `START` / `10:` / `10`, optionally followed by a statement on the same line, or
  - via the compatibility statement `LABEL <label_ref>`.
- A label-only line (label prefix with no statements) labels the **next executable statement line**, skipping empty lines and `#` comment lines.
- `GOTO`/`GOSUB` jump to the labeled location; `RETURN` resumes after the `GOSUB` call site.

`WHILE` terminators:

- `WHILE … WEND` has historic precedent in the Microsoft BASIC family (GW-BASIC/QBasic/QuickBASIC), so `WEND` is recognizable to many BASIC users.
- `ENDWHILE` (and `END WHILE`) are allowed as clearer modern spellings.

Limits should be explicit (e.g., max nesting depth) and produce clear runtime errors when exceeded.

#### 2.4.3 Boolean Logic

- Relational operators return numeric truth values (`0` false, `1` true).
- Boolean operators follow BASIC-like precedence (`NOT` > `AND` > `XOR` > `OR`).
  - `NOT`/`AND`/`OR`/`XOR` operate as **bitwise operators** on integer-truncated operands (useful for common C64 patterns like `PEEK(addr) AND mask`).
  - For bitwise operations, operands are truncated toward zero to signed 32-bit integers; results are returned as signed 32-bit integers.
  - Aliases: `==` for `=`, `!=` for `<>`.
  - Parentheses always override precedence.

#### 2.4.4 Waiting (Duration vs Wall Clock)

<a id="cmd-wait"></a>

`WAIT` supports three forms:

- `WAIT <duration>`
  - If a unit is omitted, the default unit is seconds (e.g., `WAIT 1.5` means `1.5s`).
  - Implementations should sleep in short intervals (polling) to remain cancellable.

  - Current executor timing resolution: waits are executed in 100 ms polling steps and may overshoot. Small waits like `WAIT 50ms` will effectively sleep ~100 ms.

- `WAIT UNTIL <expr>`
  - Waits until a **host wall-clock** target time is reached (or exceeded).
  - `<expr>` may evaluate to:
    - a number: interpreted as Unix epoch seconds (UTC) as a `double`, or
    - a string: parsed as a wall-clock time in one of these recommended formats:
      - `"HH:MM"` or `"HH:MM:SS"` (local time; if already passed today, treat as “tomorrow”),
      - `"YYYY-MM-DD HH:MM:SS"` (local time),
      - ISO-8601 `"YYYY-MM-DDTHH:MM:SS[.fff][Z|±HH:MM]"`.
  - If the computed target time is ≤ “now”, the wait completes immediately.
  - If the time cannot be parsed, raise a BASIC-style runtime error (recommended: “ILLEGAL QUANTITY”).

- `WAIT <address>, <mask>[, <value>] [EVERY <duration>]`
  - Reads one byte from `<address>` and compares `(byte & mask)` to `<value>`.
  - If `<value>` is omitted, it defaults to `<mask>` (BASIC V2 behavior).
  - `<address>` must be in `0..65535` and `<mask>/<value>` must be in `0..255`.
  - Polling interval defaults to **500ms** and can be overridden with `EVERY <duration>`.
  - Uses REST DMA reads; failures raise a runtime error.

## 3. Plugin & Integration Commands

These statements/functions exist to make C64 automation scripts practical in the context of this plugin (REST control, keyboard injection, and reproducible captures).

### 3.1 Command overview

This table lists **all plugin-provided commands/functions** (not BASIC control-flow like `IF`/`FOR`).

| Command / Function                                                                                                            |      Kind | Target  | Reference                                      |
| ----------------------------------------------------------------------------------------------------------------------------- | --------: | ------- | ---------------------------------------------- |
| `LOG`, `LOGFILE`, `TRON`, `TROFF`, `PRINT`                                                                                    |      stmt | Plugin  | [`3.11 Logging / tracing`](#cmd-logging)       |
| `WAIT`                                                                                                                        |      stmt | Plugin  | [`2.4.4 Waiting`](#cmd-wait)                   |
| `READFILE`, `WRITEFILE`                                                                                                       |      stmt | OS      | [`3.10 File I/O operations`](#cmd-file-io)     |
| `RUNLOCAL`                                                                                                                    |      stmt | OS      | [`3.9 Local program execution`](#cmd-runlocal) |
| `HTTP`                                                                                                                        |      stmt | Network | [`3.8 HTTP REST calls`](#cmd-http)             |
| `EFFECT`, `EFFECTPARAM`                                                                                                       |      stmt | Plugin  | [`3.2 Effects / palettes`](#cmd-effects)       |
| `PALETTE`, `PALETTECOLOR`                                                                                                     |      stmt | Plugin  | [`3.2 Effects / palettes`](#cmd-effects)       |
| `OBS SCREENSHOT`, `OBS RECORDING START`, `OBS RECORDING STOP`, `OBS WAIT FRAMES`                                              |      stmt | OBS     | [`3.12 OBS control`](#cmd-recording)           |
| `ASSERT IMAGE_EQUALS`                                                                                                         |      stmt | Plugin  | [`3.13 Image assertions`](#cmd-assert-image)   |
| `TYPE`, `KEY`                                                                                                                 |      stmt | C64     | [`3.5 Keyboard injection`](#cmd-keyboard)      |
| `POKE`, `PEEK()`                                                                                                              | stmt / fn | C64     | [`3.4 Memory access`](#cmd-memory)             |
| `PLAYSID`, `RUNPRG`, `MOUNTDISK`, `AUTOSTART`                                                                                 |      stmt | C64U    | [`3.3 C64U runners`](#cmd-runners)             |
| `RESET`, `REBOOT`                                                                                                             |      stmt | C64U    | [`3.14 U64 machine control`](#cmd-u64-machine) |
| `PAUSE`, `RESUME`, `POWEROFF`                                                                                                 |      stmt | C64U    | [`3.14 U64 machine control`](#cmd-u64-machine) |
| `CFG$()`, `CFG`, `CFG_ITEM$()`, `CFG_OPTIONS$()`, `CFGSAVE`, `CFGLOAD`, `CFGRESET`                                            | fn / stmt | C64U    | [`3.15 U64 configuration`](#cmd-u64-config)    |
| `SID_MODEL`, `SID_ENABLE`, `SID_VOL`, `SID_FILTER_CURVE`, `SID_RESONANCE`, `SID_COMBINED`, `SID_DIGIS`                        |      stmt | C64U    | [`3.15 U64 configuration`](#cmd-u64-config)    |
| `VIC_MODE`, `CPU_SPEED`                                                                                                       |      stmt | C64U    | [`3.15 U64 configuration`](#cmd-u64-config)    |
| `DRIVE$()`, `DRIVE_MOUNT`, `DRIVE_UNMOUNT`, `DRIVE_RESET`, `DRIVE_ON`, `DRIVE_OFF`, `DRIVE_ROM`, `DRIVE_MODE`, `DRIVE_BUS_ID` | fn / stmt | C64U    | [`3.16 U64 drives`](#cmd-u64-drives)           |
| `LOAD`, `RUN`, `SYS`                                                                                                          |      stmt | C64     | [`3.16 U64 drives`](#cmd-u64-drives)           |

<a id="cmd-effects"></a>

### 3.2 Effects / palettes

**Target**: Plugin (updates OBS source settings)

- `EFFECT`, `EFFECTPARAM`, `PALETTE` update OBS source settings.

<a id="cmd-runners"></a>

### 3.3 C64U runners / machine control

- `PLAYSID`, `RUNPRG`, `MOUNTDISK`, `RESET`, `REBOOT` call Ultimate 64 REST actions.

<a id="cmd-memory"></a>

### 3.4 Memory access (`PEEK`/`POKE`)

- `POKE <address>, <value>` writes one byte to C64 memory via DMA.
  - `<address>` must be in `0..65535` (hex form: `$0000..$FFFF`).
  - `<value>` is truncated to 8-bit (`value & 255`), matching C64 expectations.
  - If `<address>`/`<value>` are not numeric at runtime, the executor should raise a BASIC-style “TYPE MISMATCH” error.
- `POKE <address>, [legacy format, C64Script, ...]` writes a contiguous byte block starting at `<address>`.
  - Each element is truncated to 8-bit.
  - Implementations should chunk writes to match REST constraints (e.g., ≤ 128 bytes per DMA write).
- `PEEK(<address>)` returns a numeric value `0..255` read from C64 memory via DMA.
  - `PEEK` is a built-in function; unknown function calls should raise an “UNDEF'D FUNCTION” style error.
  - Built-in function names should be treated case-insensitively (`peek($00C6)` is valid).
  - If `<address>` is not numeric at runtime, the executor should raise a BASIC-style “TYPE MISMATCH” error.
  - `PEEK`/`POKE` are network operations and may fail or time out; failures should stop execution with a clear error message by default.

### 3.5 Keyboard injection (`TYPE`/`KEY`)

<a id="cmd-keyboard"></a>

- `TYPE <string_expr>` enqueues keystrokes derived from text.
  - The string is converted to injected bytes using the “BASIC-friendly ASCII→PETSCII” rules described in `doc/c64/c64u-rest-api.md`.
  - Escape sequences like `\r` are useful for RETURN.
  - If `<string_expr>` is not a string at runtime, the executor should raise a BASIC-style “TYPE MISMATCH” error.
- `KEY <name>` enqueues one symbolic key press (or one raw byte).
  - `<name>` is an identifier or string like `RETURN`, `RUNSTOP`, `HOME`, `CLEAR`, `CURSOR_UP`, `CURSOR_DOWN`, `CURSOR_LEFT`, `CURSOR_RIGHT`.
  - If a numeric/hex literal is provided (`13` or `$0D`), it injects that byte value directly.
  - Key names should be treated case-insensitively (`return`, `RETURN`, `ReTuRn`).
  - `TYPE`/`KEY` enqueue locally; they do not imply the C64 has already consumed the keystrokes. Use `WAIT` or memory polling (`PEEK`) where necessary.

Important injection constraint:

- Keyboard injection is KERNAL keyboard-buffer based; it will not work for software that reads the CIA keyboard matrix directly.

### 3.6 Built-in Functions

The language provides several built-in functions callable in expression context:

- `PEEK(<address>)` - Read byte from C64 memory
  - Returns: numeric value 0-255
  - Example: `B = PEEK($D020)`

- `TIME$()` - Current wall-clock time
  - Returns: string in format "YYYY-MM-DD HH:MM:SS"
  - Example: `LOG "Timestamp: " + TIME$()`

- `LEN(<string>)` - String length
  - Returns: numeric length in characters
  - Example: `IF LEN(PATH$) > 0 THEN ...`

- `LEFT$(<string>, <count>)` - Left substring
  - Returns: leftmost `<count>` characters
  - Example: `PREFIX$ = LEFT$("HELLO", 3)` → `"HEL"`

- `RIGHT$(<string>, <count>)` - Right substring
  - Returns: rightmost `<count>` characters
  - Example: `SUFFIX$ = RIGHT$("HELLO", 2)` → `"LO"`

- `MID$(<string>, <start>, <count>)` - Middle substring
  - Returns: `<count>` characters starting at position `<start>` (1-based)
  - Example: `MIDDLE$ = MID$("HELLO", 2, 3)` → `"ELL"`

- `STR$(<number>)` - Convert number to string
  - Returns: decimal string representation
  - Example: `S$ = STR$(42)` → `"42"`

- `VAL(<string>)` - Convert string to number
  - Returns: numeric value, or 0 if invalid
  - Example: `N = VAL("123.45")` → `123.45`

- `CHR$(<code>)` - Character from code
  - Returns: single-character string from ASCII/PETSCII code
  - Example: `CR$ = CHR$(13)` → carriage return

- `ASC(<string>)` - Code from character
  - Returns: numeric code of first character (0-255)
  - Example: `CODE = ASC("A")` → `65`

- `ABS(<number>)` - Absolute value
  - Example: `A = ABS(-5)` → `5`

- `INT(<number>)` - Integer part (truncate toward zero)
  - Example: `I = INT(3.7)` → `3`

- `RND(<max>)` - Random number
  - Returns: random value in range [0, `<max>`)
  - Example: `DICE = INT(RND(6)) + 1` → 1-6

- `SIN(<angle>)`, `COS(<angle>)`, `TAN(<angle>)` - Trigonometric functions
  - `<angle>` in radians
  - Example: `Y = SIN(3.14159 / 2)` → `1.0`

- `SQRT(<number>)` - Square root
  - Example: `R = SQRT(144)` → `12`

- `LOG(<number>)` - Natural logarithm
  - Example: `L = LOG(2.71828)` → `1.0`

- `EXP(<power>)` - e raised to power
  - Example: `E = EXP(1)` → `2.71828`

- `ENV(<name>)` / `ENV(<name>, <default>)` - Read OS environment variable
  - Returns: value of the named environment variable as a string
  - If the variable is not set, returns `<default>` (second argument) when provided, or `""` otherwise
  - `<name>`: string expression naming the environment variable
  - `<default>`: optional string expression used as fallback when the variable is unset
  - Example: `COUNT = VAL(ENV("EFFECT_COUNT", "3"))` - read numeric setting with default
  - Example: `COUNT = VAL(ENV("EFFECT_COUNT"))` - read numeric setting from environment
  - Example: `IF LEN(ENV("DEBUG")) > 0 THEN LOG "Debug mode active"`

#### 3.6.1 Error handling for built-in functions

- Unknown function names raise "UNDEF'D FUNCTION" error
- Type mismatches (wrong argument types) raise "TYPE MISMATCH" error
- Invalid numeric operations (e.g., `SQRT(-1)`) raise "ILLEGAL QUANTITY" error
- All function names are case-insensitive

### 3.7 Palette color control (`PALETTE_COLOR`)

- `PALETTECOLOR <index>, <r>, <g>, <b>` sets a specific palette color by index (0-15) to RGB values.
  - `<index>` must be in range 0-15 (palette indices).
  - `<r>`, `<g>`, `<b>` are color components in range 0-255.
  - Example: `PALETTECOLOR 0, 0, 0, 0` sets color 0 (background) to black.
  - Example: `PALETTECOLOR 6, 128, 64, 192` sets color 6 to custom RGB.
  - This allows fine-tuned palette customization beyond preset selection.
  - Invalid index or color values raise "ILLEGAL QUANTITY" error.

### 3.8 HTTP REST calls (`HTTP`)

<a id="cmd-http"></a>

- `HTTP <method> <url> [HEADERS <headers_map>] [BODY <body_expr>] [STATUS <status_var>] [RESPONSE <response_var>]`
- Performs an HTTP request and optionally captures response.
- `<method>`: `GET`, `POST`, `PUT`, `DELETE`, or `PATCH`
- `<url>`: string expression with target URL
- `HEADERS <headers_map>`: optional map variable with header key-value pairs (e.g., `HEADERS{"Content-Type"} = "application/json"`)
- `BODY <body_expr>`: optional request body (string)
- `STATUS <status_var>`: optional variable to receive HTTP status code (e.g., 200, 404, 500)
- `RESPONSE <response_var>`: optional variable to receive response body (string)
- Example:

  ```basic
  HTTP GET "http://example.com/api/status" STATUS S RESPONSE R$
  IF S = 200 THEN
      LOG "Success: " + R$
  ELSE
      LOG "Error: " + S
  ENDIF
  ```

- Network errors raise runtime errors unless STATUS variable is provided (then error code is stored).
- Timeout: implementation-defined (recommended: 30 seconds).

### 3.9 Local program execution (`RUN_LOCAL`)

<a id="cmd-runlocal"></a>

- `RUNLOCAL <path> [ARGS <args_string>] [STATUS <status_var>] [OUTPUT <output_var>]`
- Executes a local program/script and optionally captures result.
- `<path>`: file path to executable (relative to script directory or absolute)
- `ARGS <args_string>`: optional command-line arguments (string)
- `STATUS <status_var>`: optional variable to receive exit code (0 = success)
- `OUTPUT <output_var>`: optional variable to receive stdout+stderr (string)
- Example:

  ```basic
  RUNLOCAL "convert_image.sh" ARGS "input.png output.d64" STATUS CODE OUTPUT OUT$
  IF CODE <> 0 THEN
      LOG "Conversion failed: " + OUT$
  ENDIF
  ```

- Security note: Scripts should validate/sanitize paths to prevent arbitrary code execution.
- Execution is blocking; script waits for program to complete.
- Maximum output capture: implementation-defined (recommended: 1 MB; excess is truncated).

### 3.10 File I/O operations (`READFILE`, `WRITEFILE`)

<a id="cmd-file-io"></a>

- `READFILE <path>, <var>` reads entire file content into variable.
  - `<path>`: file path (relative to script directory or absolute)
  - `<var>`: destination variable (typically string variable with `$` suffix)
  - Text files are read as UTF-8 strings.
  - Binary files can be read into string variable (bytes as characters).
  - File not found or read errors raise "FILE NOT FOUND" or "I/O ERROR".
  - Example: `READFILE "config.txt", CONFIG$`

- `WRITEFILE <path>, <expr> [APPEND|TRUNCATE]` writes content to file.
  - `<path>`: file path (created if doesn't exist)
  - `<expr>`: content to write (string or numeric; numeric values are converted to strings)
  - `APPEND`: append to existing file (default if mode omitted)
  - `TRUNCATE`: overwrite/create new file
  - Write errors raise "I/O ERROR".
  - Example: `WRITEFILE "output.txt", "Result: " + RESULT, TRUNCATE`
  - Example: `WRITEFILE "log.txt", "Event at " + TIME$(), APPEND`

### 3.11 Logging / tracing (`LOG`, `LOGFILE`, `TRON`, `TROFF`)

<a id="cmd-logging"></a>

- `LOGFILE <path> [APPEND|TRUNCATE]` selects a script log file destination.
  - Relative paths should resolve relative to the script file’s directory.
  - If the mode is omitted, default is `APPEND`.
  - If `LOGFILE` was never called, the first `LOG` or `TRON` should implicitly open a default log file (recommended: `<script_basename>.log` next to the script).
  - If `<path>` is not a string at runtime, the executor should raise a BASIC-style “TYPE MISMATCH” error.
- `LOG <expr>` appends a line to the script log file (string values written as-is; numeric values formatted in decimal).
- `TRON` enables tracing: the executor logs every executed statement automatically (recommended to include line number + label + progress).
- `TROFF` disables tracing.
- `PRINT <expr>` writes to the OBS log (not the script log file).

### 3.12 OBS control (`OBS ...`)

<a id="cmd-recording"></a>

**Target**: OBS

- `OBS RECORDING START` starts OBS recording.
- `OBS RECORDING STOP` stops OBS recording.
- `OBS SCREENSHOT TARGET SOURCE PATH <expr>` captures the source output and copies it to the requested path.
- `OBS SCREENSHOT TARGET PREVIEW PATH <expr>` captures the OBS preview output and copies it to the requested path.
- `OBS WAIT FRAMES <expr>` blocks until the source has rendered the requested number of frames.

Clause syntax is whitespace-separated. Clause keywords such as `TARGET`, `PATH`, and `FRAMES` do not accept `=`.

Notes:

- Screenshot and recording commands require builds with `ENABLE_FRONTEND_API` enabled (OBS frontend API).
- `OBS WAIT FRAMES` requires an active source context.

Example:

```basic
OBS RECORDING START
WAIT 10s
OBS SCREENSHOT TARGET SOURCE PATH "captures/final-frame.png"
OBS RECORDING STOP
```

### 3.13 Image assertions (`ASSERT IMAGE_EQUALS`)

<a id="cmd-assert-image"></a>

**Target**: Plugin/runtime validation

- `ASSERT IMAGE_EQUALS <actual_path_expr>, <expected_path_expr> [TOLERANCE <expr>]`
- Loads both PNG files, compares them pixel-by-pixel, and stops execution on mismatch.
- `TOLERANCE` is optional and defaults to `0`.
- When the assertion fails, the runtime writes a `.diff.png` artifact beside the actual image.

Example:

```basic
OBS SCREENSHOT TARGET SOURCE PATH "artifacts/frame.png"
ASSERT IMAGE_EQUALS "artifacts/frame.png", "goldens/frame.png" TOLERANCE 0
```

### 3.14 Ultimate 64 machine control

<a id="cmd-u64-machine"></a>

**Target**: C64U (REST)

Commands:

#### `RESET` - Reset Machine

**Syntax**: `RESET`

**Description**: Sends a reset signal to the machine. Configuration remains unchanged.

**REST API**: `PUT /v1/machine:reset`

Example:

```basic
RESET
WAIT 2s
LOG "Machine reset complete"
```

#### `REBOOT` - Reboot Machine

**Syntax**: `REBOOT`

**Description**: Restarts the machine and re-initializes the cartridge configuration.

**REST API**: `PUT /v1/machine:reboot`

Example:

```basic
REBOOT
WAIT 5s
LOG "Machine rebooted"
```

#### `PAUSE` - Pause CPU Execution

**Syntax**: `PAUSE`

**Description**: Pulls the DMA line low at a safe moment, stopping the CPU while timers continue.

**REST API**: `PUT /v1/machine:pause`

Example:

```basic
PAUSE
LOG "CPU paused"
WAIT 1s
RESUME
```

#### `RESUME` - Resume CPU Execution

**Syntax**: `RESUME`

**Description**: Releases the DMA line so the CPU resumes execution.

**REST API**: `PUT /v1/machine:resume`

Example:

```basic
PAUSE
WAIT 2s
RESUME
LOG "CPU resumed"
```

#### `POWEROFF` - Power Off Machine

**Syntax**: `POWEROFF`

**Description**: Powers off the machine. Responses are not guaranteed after this command executes.

**REST API**: `PUT /v1/machine:poweroff`

**Warning**: After `POWEROFF`, the machine may not respond to further C64U commands. The script does not automatically terminate.

Example:

```basic
LOG "Shutting down machine"
POWEROFF
REM Script continues, but C64U calls may fail
```

REST API mapping:

| C64Script Command | REST API                   | Method |
| ----------------- | -------------------------- | ------ |
| `RESET`           | `PUT /v1/machine:reset`    | PUT    |
| `REBOOT`          | `PUT /v1/machine:reboot`   | PUT    |
| `PAUSE`           | `PUT /v1/machine:pause`    | PUT    |
| `RESUME`          | `PUT /v1/machine:resume`   | PUT    |
| `POWEROFF`        | `PUT /v1/machine:poweroff` | PUT    |

Usage examples:

```basic
RESET
WAIT 2s
LOG "Machine ready"
```

```basic
LOG "Pausing CPU"
PAUSE
WAIT 1s
LOG "Resuming CPU"
RESUME
```

```basic
CFG "U64 Specific Settings", "System Mode", "PAL"
CFGSAVE
REBOOT
WAIT 5s
LOG "Rebooted with PAL mode"
```

```basic
LOG "Saving configuration"
CFGSAVE
WAIT 1s
LOG "Powering off"
POWEROFF
```

### 3.15 Ultimate 64 configuration

<a id="cmd-u64-config"></a>

**Status**: Final Specification
**Date**: 2025-01-17
**Device**: Ultimate 64 Elite (Firmware 3.12a)

---

#### Overview

C64Script provides commands for accessing and modifying the Ultimate 64 hierarchical configuration menu system via REST API. The configuration system has 19 categories containing 193 configurable items (menu items).

**Target**: C64U (REST)

**Terminology**:

- **Config Item** / **Menu Item**: A single configurable setting in the hierarchy (e.g., "System Mode", "CPU Speed")
- **Category**: A parent node containing config items (e.g., "Audio Mixer", "U64 Specific Settings")
- **Option**: A possible value for an enum config item (e.g., "PAL", "NTSC" for "System Mode")
- **Value**: The current or desired value of a config item

---

#### General-purpose commands

##### `CFG$()` - Read Config Item Value

**Target**: C64U (REST)

**Syntax**: `CFG$(category$, item$)`

**Returns**: String value of the config item

**Description**: Reads the current value of a config item.

**Errors**:

- If the REST request fails or returns non-empty `errors`, the function raises a runtime error.

**Example**:

```basic
LET mode$ = CFG$("U64 Specific Settings", "System Mode")
LOG "Current mode: " + mode$
```

##### `CFG` - Write Config Item Value

**Target**: C64U (REST)

**Syntax**: `CFG category$, item$, value$`

**Description**: Sets the value of a config item.

**Errors**:

- If the REST request fails or returns non-empty `errors`, the statement raises a runtime error.
- Value conversion is up to the script author; use strings that match the C64U menu options.

**Example**:

```basic
CFG "U64 Specific Settings", "System Mode", "PAL"
```

##### `CFG_ITEM$()` - List Items at Path

**Target**: C64U (REST)

**Syntax**: `CFG_ITEM$([path$], arr$())`
**Alternative**: `CFGITEM$()`

**Returns**: Count of items (populates array)

**Description**:

- No parameter: Returns all top-level items (categories)
- With path: Returns child items at the specified path

**Errors**:

- If the REST request fails or returns non-empty `errors`, the function raises a runtime error.

**Examples**:

```basic
REM Get all categories
DIM cats$(20)
LET count = CFG_ITEM$(cats$())
FOR I = 0 TO count - 1
    LOG "Category: " + cats$(I)
NEXT

REM Get items in a category
DIM items$(20)
LET count = CFG_ITEM$("Audio Mixer", items$())
FOR I = 0 TO count - 1
    LOG "Item: " + items$(I)
NEXT
```

##### `CFG_OPTIONS$()` - List Valid Options

**Target**: C64U (REST)

**Syntax**: `CFG_OPTIONS$(category$, item$, arr$())`
**Alternative**: `CFGOPTIONS$()`

**Returns**: Count of options (populates array)

**Description**: Returns all valid option values for an enum config item.

**Errors**:

- If the REST request fails or returns non-empty `errors`, the function raises a runtime error.

**Example**:

```basic
DIM modes$(10)
LET count = CFG_OPTIONS$("U64 Specific Settings", "System Mode", modes$())
REM Returns: ["PAL", "NTSC", "PAL-60", "NTSC-50", "PAL-60/L", "NTSC-50/L"]
```

##### `CFGSAVE` - Save Configuration to Flash

**Target**: C64U (REST)

**Syntax**: `CFGSAVE`
**Alternative**: `CFG_SAVE`

**Description**: Saves current configuration to non-volatile flash storage.

##### `CFGLOAD` - Load Configuration from Flash

**Target**: C64U (REST)

**Syntax**: `CFGLOAD`
**Alternative**: `CFG_LOAD`

**Description**: Loads configuration from flash storage.

##### `CFGRESET` - Reset to Factory Defaults

**Target**: C64U (REST)

**Syntax**: `CFGRESET`
**Alternative**: `CFG_RESET`

**Description**: Resets all configuration items to factory defaults.

---

#### Hardware-specific commands

#### SID configuration

**Target**: C64U (REST)

**Architecture**: 2 physical SID sockets + 2 UltiSIDs

**SID Targets** (keywords, case-insensitive):

- `SOCKET1`
- `SOCKET2`
- `ULTI1` (UltiSID 1)
- `ULTI2` (UltiSID 2)

##### `SID_MODEL` - Set SID Model

**Syntax**: `SID_MODEL target, model$`

**Parameters**:

- `target`: `SOCKET1`, `SOCKET2`, `ULTI1`, `ULTI2`
- `model$`: Model string
  - `SOCKET1` / `SOCKET2`: model is hardware-detected (read-only; cannot be changed)
  - `ULTI1` / `ULTI2`: model can be changed (currently supported: `"UltiSID"`; future models may be added)

**Note**:

- Detected physical model can be read via:
  - `CFG$("SID Sockets Configuration", "SID Detected Socket 1")`
  - `CFG$("SID Sockets Configuration", "SID Detected Socket 2")`

##### `SID_ENABLE` - Enable/Disable SID Socket

**Syntax**: `SID_ENABLE target, enabled%`

**Parameters**:

- `target`: `SOCKET1` or `SOCKET2`
- `enabled%`: `0` = disabled, `1` = enabled

**Example**:

```basic
SID_ENABLE SOCKET1, 1  REM Enable socket 1
SID_ENABLE SOCKET2, 0  REM Disable socket 2
```

##### `SID_VOL` - Set SID Volume

**Syntax**: `SID_VOL target, level$`

**Parameters**:

- `target`: `SOCKET1`, `SOCKET2`, `ULTI1`, `ULTI2`
- `level$`: Volume in dB format: `"OFF"`, `"+6 dB"` down to `"-42 dB"` (31 levels)

**Example**:

```basic
SID_VOL ULTI1, "+3 dB"
SID_VOL ULTI2, " 0 dB"
SID_VOL SOCKET1, "-6 dB"
```

##### `SID_FILTER_CURVE` - Set UltiSID Filter Curve

**Syntax**: `SID_FILTER_CURVE target, curve$`

**Parameters**:

- `target`: `ULTI1` or `ULTI2`
- `curve$`: `"8580 Lo"`, `"8580 Hi"`, `"6581"`, `"6581 Alt"`, `"U2 Low"`, `"U2 Mid"`, `"U2 High"`

**Note**: Only applies to UltiSIDs.

##### `SID_RESONANCE` - Set UltiSID Resonance

**Syntax**: `SID_RESONANCE target, resonance$`

**Parameters**:

- `target`: `ULTI1` or `ULTI2`
- `resonance$`: `"Low"`, `"High"`

**Note**: Only applies to UltiSIDs.

##### `SID_COMBINED` - Set UltiSID Combined Waveforms

**Syntax**: `SID_COMBINED target, waveforms$`

**Parameters**:

- `target`: `ULTI1` or `ULTI2`
- `waveforms$`: `"6581"`, `"8580"`

**Note**: Only applies to UltiSIDs.

##### `SID_DIGIS` - Set UltiSID Digis Level

**Syntax**: `SID_DIGIS target, level$`

**Parameters**:

- `target`: `ULTI1` or `ULTI2`
- `level$`: `"Off"`, `"Low"`, `"Medium"`, `"High"`

**Note**: Only applies to UltiSIDs.

### Video Configuration

##### `VIC_MODE` - Set Video Mode

**Target**: C64U (REST)

**Syntax**: `VIC_MODE mode$`

**Parameters**:

- `mode$`: `"PAL"`, `"NTSC"`, `"PAL-60"`, `"NTSC-50"`, `"PAL-60/L"`, `"NTSC-50/L"`

**Example**:

```basic
VIC_MODE "PAL"
```

#### CPU configuration

##### `CPU_SPEED` - Set CPU Speed

**Target**: C64U (REST)

**Syntax**: `CPU_SPEED speed$`

**Parameters**:

- `speed$`: `" 1"`, `" 2"`, `" 3"`, `" 4"`, `" 5"`, `" 6"`, `" 8"`, `"10"`, `"12"`, `"14"`, `"16"`, `"20"`, `"24"`, `"32"`, `"40"`, `"48"` (MHz)

**Note**: Single-digit values have leading spaces (`" 1"` vs `"10"`).

**Example**:

```basic
CPU_SPEED " 1"   REM 1 MHz
CPU_SPEED "10"   REM 10 MHz
CPU_SPEED "48"   REM 48 MHz
```

---

#### Configuration structure

##### Categories (19 total)

1. Audio Mixer (20 items)
2. SID Sockets Configuration (8 items)
3. UltiSID Configuration (8 items)
4. SID Addressing (8 items)
5. U64 Specific Settings (20 items)
6. C64 and Cartridge Settings (20 items)
7. Clock Settings (7 items)
8. Network Settings (14 items)
9. Ethernet Settings (5 items)
10. WiFi settings (6 items)
11. LED Strip Settings (6 items)
12. Data Streams (4 items)
13. SoftIEC Drive Settings (3 items)
14. Printer Settings (11 items)
15. Modem Settings (15 items)
16. User Interface Settings (11 items)
17. Tape Settings (1 item)
18. Drive A Settings (13 items)
19. Drive B Settings (13 items)

**Total**: 193 config items

##### Setting types

- **Enum**: 132 items (discrete choices)
- **Numeric**: 20 items (ranges with min/max)
- **String**: 12 items (free-form or presets)
- **Read-Only**: 25 items (detected values, status)

---

#### REST API mapping

| C64Script Command           | REST API                                   | Method |
| --------------------------- | ------------------------------------------ | ------ |
| `CFG_ITEM$()` (no param)    | `GET /v1/configs`                          | GET    |
| `CFG_ITEM$(path$)`          | `GET /v1/configs/{category}`               | GET    |
| `CFG_OPTIONS$(cat$, item$)` | `GET /v1/configs/{cat}/{item}`             | GET    |
| `CFG$(cat$, item$)`         | `GET /v1/configs/{cat}/{item}`             | GET    |
| `CFG cat$, item$, val$`     | `PUT /v1/configs/{cat}/{item}?value={val}` | PUT    |
| `CFGSAVE`                   | `PUT /v1/configs:save_to_flash`            | PUT    |
| `CFGLOAD`                   | `PUT /v1/configs:load_from_flash`          | PUT    |
| `CFGRESET`                  | `PUT /v1/configs:reset_to_default`         | PUT    |

**Path Encoding**: Category and item names must be URL-encoded (space `" "` → `"%20"`).

---

#### Key findings (empirical)

1. **Physical SID Model**: Hardware-detected and read-only. Read via `CFG$("SID Sockets Configuration", "SID Detected Socket 1")`.

2. **CPU Speed Format**: Single-digit values have leading spaces (`" 1"` vs `"10"`).

3. **Audio Volume**: Uses dB strings (`"OFF"`, `"+6 dB"` to `"-42 dB"`), not numeric percentages.

4. **System Mode**: 6 options (PAL, NTSC, PAL-60, NTSC-50, PAL-60/L, NTSC-50/L).

5. **UltiSID Options**: All filter curve (7), resonance (2), combined waveforms (2), and digis level (4) options verified.

6. **Persistence**: Configuration changes persist after machine reset when saved to flash.

---

#### Examples

##### Discover configuration structure

```basic
REM Get all categories
DIM cats$(20)
LET count = CFG_ITEM$(cats$())

FOR C = 0 TO count - 1
    LET cat$ = cats$(C)
    LOG "=== " + cat$ + " ==="

    REM Get items in category
    DIM items$(20)
    LET item_count = CFG_ITEM$(cat$, items$())

    FOR I = 0 TO item_count - 1
        LET item$ = items$(I)
        LET value$ = CFG$(cat$, item$)
        LOG "  " + item$ + " = " + value$
    NEXT
NEXT
```

##### Configure UltiSID

```basic
REM Select UltiSID model (virtual only)
SID_MODEL ULTI1, "UltiSID"

REM Configure UltiSID 1 filter
SID_FILTER_CURVE ULTI1, "U2 High"
SID_RESONANCE ULTI1, "High"
SID_COMBINED ULTI1, "6581"
SID_DIGIS ULTI1, "Medium"

REM Set volume
SID_VOL ULTI1, "+3 dB"
```

##### Set video mode and CPU speed

```basic
VIC_MODE "PAL"
CPU_SPEED " 1"
```

##### Save configuration

```basic
REM Make changes
CFG "Audio Mixer", "Vol UltiSid 1", "+3 dB"
VIC_MODE "PAL"

REM Save to flash
CFGSAVE
```

### 3.16 Ultimate 64 drives

<a id="cmd-u64-drives"></a>

**Status**: Final Specification
**Date**: 2025-01-17
**Device**: Ultimate 64 Elite (Firmware 3.12a)

---

#### Overview

C64Script provides commands for controlling Ultimate 64 floppy drives via REST API. These commands allow scripts to mount/unmount disk images, reset drives, load ROMs, and change drive modes.

**Target**: C64U (REST) + C64 (keyboard buffer)

**Drive Identifiers**: `DRIVE_A`, `DRIVE_B`, `DRIVE_SOFTIEC` (keywords, case-insensitive)

---

#### Keywords

##### Drive Identifiers

- `DRIVE_A` - Drive A
- `DRIVE_B` - Drive B
- `DRIVE_SOFTIEC` - SoftIEC drive

##### Drive Modes

- `MODE_1541` - 1541 drive mode
- `MODE_1571` - 1571 drive mode
- `MODE_1581` - 1581 drive mode

##### Image Types

- `TYPE_D64` - D64 disk image
- `TYPE_G64` - G64 disk image
- `TYPE_D71` - D71 disk image
- `TYPE_G71` - G71 disk image
- `TYPE_D81` - D81 disk image

##### Mount Modes

- `MODE_READWRITE` - Read/write access (default)
- `MODE_READONLY` - Read-only access
- `MODE_UNLINKED` - Unlinked mode

##### Drive Properties

- `PROP_ENABLED` - Drive enabled status
- `PROP_BUS_ID` - Bus ID
- `PROP_TYPE` - Drive type
- `PROP_ROM` - ROM filename
- `PROP_IMAGE_FILE` - Mounted image filename
- `PROP_IMAGE_PATH` - Mounted image path

---

#### Commands

##### `DRIVE$()` - Get Drive Information

**Target**: C64U (REST)

**Syntax**: `DRIVE$(drive, property)`

**Returns**: String value of the drive property

**Description**: Reads information about a drive. Returns empty string if property doesn't exist.

**Parameters**:

- `drive`: Drive identifier keyword (`DRIVE_A`, `DRIVE_B`, `DRIVE_SOFTIEC`)
- `property`: Property keyword (`PROP_ENABLED`, `PROP_BUS_ID`, `PROP_TYPE`, `PROP_ROM`, `PROP_IMAGE_FILE`, `PROP_IMAGE_PATH`)

**Typing**:

- For numeric properties (e.g. `PROP_BUS_ID`), use `VAL(DRIVE$(...))` to convert to number.

**Example**:

```basic
LET enabled$ = DRIVE$(DRIVE_A, PROP_ENABLED)
LET type$ = DRIVE$(DRIVE_A, PROP_TYPE)
LET image$ = DRIVE$(DRIVE_A, PROP_IMAGE_FILE)
IF image$ <> "" THEN
    LOG "Drive A has image: " + image$
ENDIF
```

##### `DRIVE_MOUNT` - Mount Disk Image

**Target**: C64U (REST)

**Syntax**:

- `DRIVE_MOUNT image$ [type] [mode]`
- `DRIVE_MOUNT drive, image$ [type] [mode]`

**Description**: Mounts a disk image file on the specified drive.

**Parameters**:

- `drive`: Optional drive identifier keyword (`DRIVE_A`, `DRIVE_B`, `DRIVE_SOFTIEC`). Default: `DRIVE_A`.
- `image$`: Required image file path:
  - Local file path (on PC): `"C:/path/to/file.d64"` or `"/home/user/file.d64"` (uploads the file)
  - Remote file path (on C64U): `"c64u:/Games/game.d64"` (references file on C64U)
- `type`: Optional image type keyword (`TYPE_D64`, `TYPE_G64`, `TYPE_D71`, `TYPE_G71`, `TYPE_D81`). Auto-detected from file suffix if omitted (`.d64` → `TYPE_D64`, `.g64` → `TYPE_G64`, etc.). Default: `TYPE_D64` if suffix unknown.
- `mode`: Optional mount mode keyword (`MODE_READWRITE`, `MODE_READONLY`, `MODE_UNLINKED`). Default: `MODE_READWRITE`.

**Defaults**:

- Drive: `DRIVE_A`
- Type: `TYPE_D64` (or inferred from file suffix)
- Mode: `MODE_READWRITE`

**REST API**:

- `PUT /v1/drives/{drive}:mount` (when `image$` is `c64u:`; uses query `image=...`)
- `POST /v1/drives/{drive}:mount` (when `image$` is local; uploads the image as the request body)

**Examples**:

```basic
REM Mount local D64 image on drive A (defaults)
DRIVE_MOUNT "C:/Games/game.d64"

REM Mount remote D64 image on drive A
DRIVE_MOUNT "c64u:/Games/game.d64"

REM Mount with explicit drive and type
DRIVE_MOUNT DRIVE_A, "c64u:/Demos/demo.d64", TYPE_D64, MODE_READONLY

REM Mount with explicit drive only (type inferred from .d81 suffix)
DRIVE_MOUNT DRIVE_B, "c64u:/Work/data.d81"
```

##### `DRIVE_UNMOUNT` - Unmount Disk Image

**Target**: C64U (REST)

**Syntax**: `DRIVE_UNMOUNT drive`

**Description**: Removes the mounted image from the drive.

**REST API**: `PUT /v1/drives/{drive}:remove`

**Example**:

```basic
DRIVE_UNMOUNT DRIVE_A
LOG "Drive A unmounted"
```

##### `DRIVE_RESET` - Reset Drive

**Target**: C64U (REST)

**Syntax**: `DRIVE_RESET drive`

**Description**: Resets the selected drive.

**REST API**: `PUT /v1/drives/{drive}:reset`

**Example**:

```basic
DRIVE_RESET DRIVE_A
WAIT 1s
LOG "Drive A reset"
```

##### `DRIVE_ON` - Turn On Drive

**Target**: C64U (REST)

**Syntax**: `DRIVE_ON drive`

**Description**: Turns on the selected drive and resets it if already on.

**REST API**: `PUT /v1/drives/{drive}:on`

**Example**:

```basic
DRIVE_ON DRIVE_A
LOG "Drive A turned on"
```

##### `DRIVE_OFF` - Turn Off Drive

**Target**: C64U (REST)

**Syntax**: `DRIVE_OFF drive`

**Description**: Turns off the selected drive.

**REST API**: `PUT /v1/drives/{drive}:off`

**Example**:

```basic
DRIVE_OFF DRIVE_A
LOG "Drive A turned off"
```

##### `DRIVE_ROM` - Load Drive ROM

**Target**: C64U (REST)

**Description**: Loads a 16K or 32K ROM into the selected drive. The load is temporary (not saved to flash).

**Syntax**:

- `DRIVE_ROM file$`
- `DRIVE_ROM drive, file$`

**Parameters**:

- `drive`: Optional drive identifier keyword (`DRIVE_A`, `DRIVE_B`). Default: `DRIVE_A`.
- `file$`: Required ROM file path:
  - Local file path (on PC): `"C:/path/to/rom.bin"` or `"/home/user/rom.bin"` (uploads the ROM)
  - Remote file path (on C64U): `"c64u:/ROMs/1541.rom"` (references file on C64U)

**Defaults**:

- Drive: `DRIVE_A`

**REST API**:

- `PUT /v1/drives/{drive}:load_rom` (when `file$` is `c64u:`; uses query `file=...`)
- `POST /v1/drives/{drive}:load_rom` (when `file$` is local; uploads the ROM as the request body)

**Examples**:

```basic
REM Load ROM from C64U (default drive A)
DRIVE_ROM "c64u:/ROMs/1541.rom"

REM Load local ROM file
DRIVE_ROM "C:/ROMs/custom_1541.rom"

REM Load ROM with explicit drive
DRIVE_ROM DRIVE_B, "c64u:/ROMs/1571.rom"
```

##### `DRIVE_MODE` - Set Drive Mode

**Target**: C64U (REST)

**Syntax**: `DRIVE_MODE drive, mode`

**Description**: Changes the drive mode and loads the corresponding ROM.

**Parameters**:

- `drive`: Drive identifier keyword (`DRIVE_A`, `DRIVE_B`)
- `mode`: Drive mode keyword (`MODE_1541`, `MODE_1571`, `MODE_1581`)

**REST API**: `PUT /v1/drives/{drive}:set_mode`

**Example**:

```basic
DRIVE_MODE DRIVE_A, MODE_1581
LOG "Drive A set to 1581 mode"
```

##### `DRIVE_BUS_ID` - Set Drive Bus ID

**Target**: C64U (REST)

**Syntax**: `DRIVE_BUS_ID drive, bus_id`

**Description**: Sets the bus ID for the specified drive.

**Parameters**:

- `drive`: Drive identifier keyword (`DRIVE_A`, `DRIVE_B`)
- `bus_id`: Bus ID number (8-11). Default: `8` for drive A, `9` for drive B.

**REST API**: `PUT /v1/configs/Drive {A|B} Settings/Drive Bus ID?value={bus_id}`

**Example**:

```basic
DRIVE_BUS_ID DRIVE_A, 8
DRIVE_BUS_ID DRIVE_B, 9
```

##### `LOAD` - Load Program from Disk

**Target**: C64 (keyboard buffer)

**Syntax**: `LOAD "filename" [device]`

**Description**: Executes BASIC `LOAD` on the C64. Supports `*` and `?` wildcards. Uses `,device,1` to load to the program's stored start address.

**Parameters**:

- `filename`: Program filename (supports `*` and `?` wildcards)
- `device`: Optional device number. Default: `8` (drive A)

**Expansion** (exact):

```basic
TYPE "LOAD\"<filename>\",<device>,1\r"
```

**Example**:

```basic
LOAD "*"
LOAD "GAME"
LOAD "GAME", 8
LOAD "DEMO?", 9
```

##### `RUN` - Run Program

**Target**: C64 (keyboard buffer)

**Syntax**: `RUN ["filename" [device]]`

**Description**: Executes BASIC `RUN` on the C64. If filename is provided, performs `LOAD "<filename>",<device>,1` first.

**Parameters**:

- `filename`: Optional program filename (supports `*` and `?` wildcards). If omitted, runs already loaded program.
- `device`: Optional device number. Default: `8` (drive A) when filename is provided.

**Expansion** (exact):

- `RUN`:

```basic
TYPE "RUN\r"
```

- `RUN "<filename>"[,device]`:

```basic
TYPE "LOAD\"<filename>\",<device>,1\rRUN\r"
```

**Examples**:

```basic
REM Just run (already loaded program)
RUN

REM Load and run (device defaults to 8)
RUN "*"
RUN "GAME"

REM Load and run from specific device
RUN "GAME", 8
RUN "DEMO", 9
```

##### `SYS` - Execute SYS Command

**Target**: C64 (keyboard buffer)

**Syntax**: `SYS address`

**Description**: Executes BASIC `SYS` on the C64. To load then SYS: `LOAD "..."[,device]` then `SYS address`.

**Parameters**:

- `address`: Memory address (decimal or hex)

**Expansion** (exact):

```basic
TYPE "SYS <address>\r"
```

**Example**:

```basic
SYS 64738  REM Reset
SYS $FCE2  REM Reset (hex)
```

---

#### REST API mapping

| C64Script Command                       | REST API                                    | Method                    |
| --------------------------------------- | ------------------------------------------- | ------------------------- |
| `DRIVE$(drive, property)`               | `GET /v1/drives`                            | GET                       |
| `DRIVE_MOUNT ...` (`image$` is `c64u:`) | `PUT /v1/drives/{drive}:mount?image=...`    | PUT                       |
| `DRIVE_MOUNT ...` (`image$` is local)   | `POST /v1/drives/{drive}:mount` (upload)    | POST                      |
| `DRIVE_UNMOUNT drive`                   | `PUT /v1/drives/{drive}:remove`             | PUT                       |
| `DRIVE_RESET drive`                     | `PUT /v1/drives/{drive}:reset`              | PUT                       |
| `DRIVE_ON drive`                        | `PUT /v1/drives/{drive}:on`                 | PUT                       |
| `DRIVE_OFF drive`                       | `PUT /v1/drives/{drive}:off`                | PUT                       |
| `DRIVE_ROM ...` (`file$` is `c64u:`)    | `PUT /v1/drives/{drive}:load_rom?file=...`  | PUT                       |
| `DRIVE_ROM ...` (`file$` is local)      | `POST /v1/drives/{drive}:load_rom` (upload) | POST                      |
| `DRIVE_MODE drive, mode`                | `PUT /v1/drives/{drive}:set_mode`           | PUT                       |
| `DRIVE_BUS_ID drive, bus_id`            | `PUT /v1/configs/Drive {A                   | B} Settings/Drive Bus ID` | PUT |
| `LOAD "filename" [device]`              | Keyboard injection                          | -                         |
| `RUN`                                   | Keyboard injection                          | -                         |
| `SYS address`                           | Keyboard injection                          | -                         |

**Note**: Drive keywords map to REST API paths (`DRIVE_A` → `"a"`, `DRIVE_B` → `"b"`, `DRIVE_SOFTIEC` → `"softiec"`). Mode keywords map to REST API values (`MODE_1541` → `"1541"`, etc.).

---

#### Usage examples

##### Get drive status

```basic
LET enabled$ = DRIVE$(DRIVE_A, PROP_ENABLED)
LET type$ = DRIVE$(DRIVE_A, PROP_TYPE)
LET image$ = DRIVE$(DRIVE_A, PROP_IMAGE_FILE)

IF enabled$ = "true" THEN
    LOG "Drive A: " + type$ + " mode"
    IF image$ <> "" THEN
        LOG "  Image: " + image$
    ELSE
        LOG "  No image mounted"
    ENDIF
ENDIF
```

##### Mount and use disk

```basic
REM Turn on drive
DRIVE_ON DRIVE_A

REM Mount disk image (defaults: drive A, type inferred from .d64)
DRIVE_MOUNT "c64u:/Games/demo.d64"

REM Wait for mount
WAIT 1s

REM Load and run program (device defaults to 8)
RUN "*"
```

##### Change drive mode

```basic
REM Switch drive A to 1581 mode
DRIVE_MODE DRIVE_A, MODE_1581
WAIT 1s
LOG "Drive A is now 1581"
```

##### Load custom ROM

```basic
REM Load custom ROM
DRIVE_ROM DRIVE_A, "custom_1541.rom"
WAIT 1s
LOG "Custom ROM loaded"
```

##### Unmount and turn off

```basic
REM Unmount image
DRIVE_UNMOUNT DRIVE_A

REM Turn off drive
DRIVE_OFF DRIVE_A
LOG "Drive A unmounted and turned off"
```

### Complete Drive Setup

```basic
REM Setup drive A for 1581 mode
DRIVE_MODE DRIVE_A, MODE_1581
DRIVE_ON DRIVE_A
REM Type inferred from .d81 suffix, mode defaults to READWRITE
DRIVE_MOUNT "c64u:/Disks/work.d81"
WAIT 1s
LOG "Drive A ready: 1581 mode with work disk"
```

### Local vs Remote Files

```basic
REM Mount local file (on PC)
DRIVE_MOUNT "C:/Demos/demo.d64"

REM Mount remote file (on C64U)
DRIVE_MOUNT "c64u:/Games/game.d64"

REM Load local ROM
DRIVE_ROM "C:/ROMs/custom.rom"

REM Load remote ROM
DRIVE_ROM "c64u:/ROMs/1541.rom"
```

### Additional Drive Settings

Other drive settings (ROM files, extra RAM, disk swap delay, etc.) can be configured via the `CFG` command:

```basic
REM Set ROM for 1541 mode
CFG "Drive A Settings", "ROM for 1541 mode", "custom_1541.rom"

REM Enable extra RAM
CFG "Drive A Settings", "Extra RAM", "Enabled"

REM Set disk swap delay
CFG "Drive A Settings", "Disk swap delay", "2"
```

### Multiple Drives

```basic
REM Setup both drives with custom bus IDs
DRIVE_BUS_ID DRIVE_A, 8
DRIVE_BUS_ID DRIVE_B, 10
DRIVE_MODE DRIVE_A, MODE_1541
DRIVE_MODE DRIVE_B, MODE_1581
DRIVE_ON DRIVE_A
DRIVE_ON DRIVE_B
DRIVE_MOUNT DRIVE_A, "c64u:/Games/game.d64", TYPE_D64, MODE_READONLY
DRIVE_MOUNT DRIVE_B, "c64u:/Work/data.d81", TYPE_D81, MODE_READWRITE
WAIT 1s
LOG "Both drives ready"
```

#### Compatibility Alias: `MOUNTDISK`

**Target**: C64U (REST)

`MOUNTDISK image$` is a compatibility alias for:

```basic
DRIVE_MOUNT DRIVE_A, image$
```

## 4. Effect Parameters Reference

The `EFFECTPARAM` statement allows fine-grained control of visual effects beyond preset selection. Each effect type supports different parameters. Effect names and parameter names are **case-insensitive**.

### 4.1 General usage

```basic
EFFECT "Classic CRT"
EFFECTPARAM "scanline_intensity" 0.7
EFFECTPARAM "phosphor_persistence" 0.3
EFFECTPARAM "preserve_size" 1
```

`preserve_size` is a source/filter layout flag rather than a preset attribute:

- `EFFECTPARAM "preserve_size" 1` keeps the OBS-facing preview footprint stable while effect scaling changes only the internal virtual geometry.
- `EFFECTPARAM "preserve_size" 0` restores the legacy behavior where effect scaling changes the visible source/filter size.
- Presets do not override `preserve_size`.

### 4.2 Common effect types and their parameters

#### 4.2.1 CRT Effects

Effect presets: `"Classic CRT"`, `"Vintage TV"`, `"Arcade Cabinet"`

Parameters:

- `scanline_intensity` (0.0 - 1.0): Strength of horizontal scanlines (default: 0.5)
- `scanline_thickness` (0.1 - 2.0): Thickness of scanlines in pixels (default: 1.0)
- `curvature` (0.0 - 0.2): Screen curvature amount (default: 0.05)
- `corner_radius` (0.0 - 50.0): Rounded corner radius in pixels (default: 10.0)
- `vignette` (0.0 - 1.0): Edge darkening amount (default: 0.3)
- `phosphor_persistence` (0.0 - 0.9): Afterglow/motion blur (default: 0.2)
- `bloom` (0.0 - 2.0): Glow around bright areas (default: 0.3)
- `noise` (0.0 - 0.5): Random noise/grain (default: 0.1)

Example:

```basic
EFFECT "Classic CRT"
EFFECTPARAM "scanline_intensity" 0.8
EFFECTPARAM "curvature" 0.1
EFFECTPARAM "phosphor_persistence" 0.4
EFFECTPARAM "bloom" 0.5
```

#### 4.2.2 Sharp/Pixel Perfect

Effect presets: `"Sharp Pixels"`, `"Pixel Perfect"`

Parameters:

- `grid_intensity` (0.0 - 1.0): Pixel grid visibility (default: 0.0)
- `grid_color_r` (0 - 255): Grid color red component (default: 0)
- `grid_color_g` (0 - 255): Grid color green component (default: 0)
- `grid_color_b` (0 - 255): Grid color blue component (default: 0)

Example:

```basic
EFFECT "Sharp Pixels"
EFFECTPARAM "grid_intensity" 0.3
EFFECTPARAM "grid_color_r" 20
EFFECTPARAM "grid_color_g" 20
EFFECTPARAM "grid_color_b" 20
```

#### 4.2.3 Monitor Emulation

Effect presets: `"Amber Monitor"`, `"Green Monitor"`

Parameters:

- `tint_r` (0.0 - 1.0): Red tint component (default: depends on monitor type)
- `tint_g` (0.0 - 1.0): Green tint component (default: depends on monitor type)
- `tint_b` (0.0 - 1.0): Blue tint component (default: depends on monitor type)
- `tint_intensity` (0.0 - 1.0): Overall tint strength (default: 0.8)
- `brightness` (0.5 - 2.0): Screen brightness (default: 1.0)
- `contrast` (0.5 - 2.0): Screen contrast (default: 1.0)

Example:

```basic
EFFECT "Amber Monitor"
EFFECTPARAM "tint_intensity" 0.9
EFFECTPARAM "brightness" 1.2
EFFECTPARAM "contrast" 1.1
```

#### 4.2.4 Blur/Smoothing

Effect presets: `"Soft Blur"`, `"CRT Blur"`

Parameters:

- `blur_radius` (0.0 - 10.0): Blur amount in pixels (default: 1.5)
- `blur_direction` (0 - 2): 0=horizontal, 1=vertical, 2=both (default: 2)

Example:

```basic
EFFECT "Soft Blur"
EFFECTPARAM "blur_radius" 2.5
EFFECTPARAM "blur_direction" 2
```

#### 4.2.5 Color Adjustments

Available via `EFFECTPARAM` with any effect active:

- `saturation` (0.0 - 2.0): Color saturation (1.0 = normal, default: 1.0)
- `hue_shift` (-180.0 - 180.0): Hue rotation in degrees (default: 0.0)
- `gamma` (0.5 - 2.5): Gamma correction (default: 1.0)
- `brightness` (0.0 - 2.0): Brightness multiplier (default: 1.0)
- `contrast` (0.0 - 2.0): Contrast multiplier (default: 1.0)

Example:

```basic
EFFECT "Classic CRT"
EFFECTPARAM "saturation" 1.2
EFFECTPARAM "brightness" 1.1
EFFECTPARAM "gamma" 0.9
```

### 4.3 Parameter discovery

To discover available parameters for a specific effect at runtime:

1. Use the OBS Studio UI to inspect effect properties
2. Consult `data/effect_presets.ini` for preset configurations
3. Check effect shader source code in `data/effects/` directory
4. Parameters not listed here are implementation-specific and may vary

### 4.4 Layout behavior

`preserve_size` applies to both the `c64_source` input source and the `c64_stream_effects` filter:

- New instances default to `preserve_size = 1`
- Existing saved scenes that predate the setting keep their legacy size-changing behavior until you enable it
- Perfect scanlines still depend on the final displayed OBS transform size, not only on the internal effect preset

### 4.5 Error handling

- Unknown effect names: runtime warning, effect unchanged
- Unknown parameter names: runtime warning, parameter unchanged
- Invalid parameter values: runtime warning, clamped to valid range
- Type mismatches: "TYPE MISMATCH" error (parameters must be numeric)

## 5 Examples

### 5.1 Label and line-number forms

These are alternative forms; in a real script, label names must be unique.

```basic
REM Label on the same line
START: I = 0

REM Label on its own line (labels the next line)
START:
I = 0

REM Numeric labels (BASIC-style line numbers)
10 I = 0
10: I = 0
12345:
I = 0
```

### 5.2 BASIC-like sequence with quoted preset names

```basic
REM Fade in, then run a demo
EFFECT "Classic CRT"
PALETTE "colodore"
WAIT 2
RUNPRG "c64u:/Programs/demo.prg"
WAIT 60s
END
```

### 5.3 Label + IF + GOTO (no line numbers)

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

### 5.4 FOR/NEXT

```basic
FOR I = 1 TO 5
    PALETTE "pepto_ntsc"
    WAIT 1
    PALETTE "vice_new"
    WAIT 1
NEXT I
END
```

### 5.5 GOSUB/RETURN as “functions”

```basic
PATH$ = "c64u:/Temp/music/galway_collection.sid"

GOSUB PLAYTRACK
TRACK = 2
GOSUB PLAYTRACK
TRACK = 3
GOSUB PLAYTRACK
END

PLAYTRACK:
PLAYSID PATH$ SONGNR TRACK
WAIT 20
RETURN
```

### 5.6 PEEK/POKE + typing an autostart sequence

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

### 5.7 TRON/TROFF for automatic progress logging

```basic
LOGFILE "trace.log" TRUNCATE
TRON

EFFECT "Classic CRT"
PALETTE "colodore"
WAIT 2
EFFECT "Default"

TROFF
LOG "Done"
END
```

### 5.8 Wait until a wall-clock time

```basic
LOGFILE "schedule.log" APPEND
LOG "Waiting for 20:00..."

WAIT UNTIL "20:00:00"
LOG "Starting now"

OBS RECORDING START
WAIT 60s
OBS RECORDING STOP
END
```

### 5.9 BASIC-style program with line numbers (optional)

Line numbers are optional in C64Script; they behave exactly like labels and exist mainly for that classic BASIC feel.

```basic
10 REM Line numbers are optional; they act like labels for GOTO/GOSUB.
20 LOGFILE "line-numbered.log" TRUNCATE
30 TRON

40 I = 0
50 GOSUB 1000
60 I = I + 1
70 IF I < 3 THEN GOTO 50

80 TROFF
90 LOG "Done"
100 END

1000 REM A tiny “subroutine” (GOSUB/RETURN)
1010 PALETTE "colodore"
1020 WAIT 0.5
1030 PALETTE "pepto_ntsc"
1040 WAIT 0.5
1050 RETURN
```

### 5.10 User-defined functions with parameters

```basic
REM Define a function to apply effect and wait
FUNCTION APPLY_EFFECT(EFFECT_NAME$, DURATION)
    EFFECT EFFECT_NAME$
    WAIT DURATION
    RETURN 1
ENDFUNCTION

REM Define a function to calculate delay based on mode
FUNCTION CALC_DELAY(MODE)
    IF MODE = 1 THEN RETURN 0.5
    IF MODE = 2 THEN RETURN 1.5
    RETURN 1.0
ENDFUNCTION

REM Use the functions
MODE = 2
DELAY = CALC_DELAY(MODE)
RESULT = APPLY_EFFECT("Classic CRT", DELAY)
LOG "Effect applied with delay: " + DELAY
END
```

### 5.11 GOSUB with parameters (BASIC-inspired)

```basic
REM Call subroutine with parameters
GOSUB CONFIGURE("colodore", 2.5)
GOSUB CONFIGURE("pepto_ntsc", 1.0)
END

CONFIGURE:
    REM PARAM1 = palette name, PARAM2 = wait duration
    PALETTE PARAM1
    WAIT PARAM2
    RETURN
```

### 5.12 Arrays and maps

```basic
REM Arrays for storing palette sequence
DIM PALETTES$(5)
PALETTES$(0) = "colodore"
PALETTES$(1) = "pepto_ntsc"
PALETTES$(2) = "vice_new"
PALETTES$(3) = "deekay"
PALETTES$(4) = "ptoing"

REM Map for configuration
CONFIG${"host"} = "192.168.1.64"
CONFIG{"port"} = 8080
CONFIG{"timeout"} = 5000

REM Iterate through palettes
FOR I = 0 TO 4
    PALETTE PALETTES$(I)
    WAIT 2s
NEXT I

REM Use configuration
LOG "Connecting to " + CONFIG${"host"} + ":" + CONFIG{"port"}
END
```

### 5.13 HTTP REST API integration

```basic
REM Configure API endpoint
API_URL$ = "http://192.168.1.64:8080/api/legacy format"

REM Check C64 Ultimate status
HTTP GET API_URL$ + "/status" STATUS STATUS_CODE RESPONSE RESP$
IF STATUS_CODE = 200 THEN
    LOG "C64 Status: " + RESP$
ELSE
    LOG "API Error: " + STATUS_CODE
    STOP
ENDIF

REM Send command via POST
HEADERS${"Content-Type"} = "application/json"
BODY$ = "{\"command\": \"reset\", \"delay\": 1000}"
HTTP POST API_URL$ + "/command" HEADERS HEADERS$ BODY BODY$ STATUS CODE
IF CODE = 200 THEN
    LOG "Command sent successfully"
ENDIF
END
```

### 5.14 Local file processing and program execution

```basic
REM Read configuration file
READFILE "config.txt", CONFIG$
LOG "Configuration loaded: " + CONFIG$

REM Process data with local script
RUNLOCAL "process_data.py" ARGS "input.txt output.d64" STATUS EXIT_CODE OUTPUT OUT$
IF EXIT_CODE <> 0 THEN
    LOG "Processing failed: " + OUT$
    WRITEFILE "errors.log", OUT$, APPEND
    STOP
ELSE
    LOG "Processing completed successfully"
    WRITEFILE "success.log", "Processed at " + TIME$(), APPEND
ENDIF

REM Use the processed file
MOUNTDISK "output.d64"
AUTOSTART
WAIT 30s
END
```

### 5.15 Custom palette colors

```basic
REM Start with a base palette
PALETTE "colodore"

REM Customize specific colors (index, R, G, B)
PALETTECOLOR 0, 0, 0, 0          REM Black background
PALETTECOLOR 1, 255, 255, 255    REM White
PALETTECOLOR 6, 0, 0, 170        REM Dark blue
PALETTECOLOR 14, 128, 192, 255   REM Light blue

REM Apply effect and capture
EFFECT "Classic CRT"
WAIT 2s
OBS RECORDING START
WAIT 60s
OBS RECORDING STOP
END
```

### 5.16 Long-duration waits and scheduling

```basic
REM Wait various durations
WAIT 500ms          REM Half a second
WAIT 30s            REM Half a minute
WAIT 2.5m           REM Two and a half minutes
WAIT 1h             REM One hour
WAIT 0.5d           REM Half a day (12 hours)

REM Schedule for specific time
WAIT UNTIL "22:30:00"
LOG "Starting nightly demo capture"
OBS RECORDING START
RUNPRG "c64u:/Demos/nightly_demo.prg"
WAIT 2h
OBS RECORDING STOP
LOG "Nightly capture completed"
END
```

### 5.17 Complex automation with all features

```basic
REM Complete automation example combining all features
LOGFILE "automation.log" TRUNCATE
TRON

REM Configuration
DIM DEMOS$(3)
DEMOS$(0) = "c64u:/Demos/fairlight.prg"
DEMOS$(1) = "c64u:/Demos/booze.prg"
DEMOS$(2) = "c64u:/Demos/eldorado.prg"

CONFIG${"output_dir"} = "/recordings"
CONFIG{"demo_duration"} = 180

REM Function to capture demo
FUNCTION CAPTURE_DEMO(DEMO_PATH$, DURATION)
    LOG "Capturing: " + DEMO_PATH$

    REM Reset machine
    RESET
    WAIT 2s

    REM Load and run
    RUNPRG DEMO_PATH$
    WAIT 5s

    REM Record
    OBS RECORDING START
    WAIT DURATION
    OBS RECORDING STOP

    RETURN 1
ENDFUNCTION

REM Check system status via HTTP
HTTP GET "http://192.168.1.64:8080/status" STATUS S
IF S <> 200 THEN
    LOG "C64 Ultimate not responding"
    STOP
ENDIF

REM Process each demo
FOR I = 0 TO 2
    REM Custom palette for each demo
    IF I = 0 THEN PALETTE "colodore"
    IF I = 1 THEN PALETTE "pepto_ntsc"
    IF I = 2 THEN PALETTE "vice_new"

    REM Apply CRT effect
    EFFECT "Classic CRT"
    EFFECTPARAM "scanline_intensity" 0.7

    REM Capture the demo
    RESULT = CAPTURE_DEMO(DEMOS$(I), CONFIG{"demo_duration"})

    REM Generate report
    REPORT$ = "Demo " + I + " captured at " + TIME$()
    WRITEFILE CONFIG${"output_dir"} + "/report.txt", REPORT$, APPEND

    WAIT 10s
NEXT I

TROFF
LOG "All captures completed"
END
```

## 6. Implementation Notes

### 6.1 General

- `c64u:` filesystem paths are supported for `PLAYSID`, `RUNPRG`, and `MOUNTDISK` (path-based REST API).
- Local file upload variants are fully supported (uploads file data via REST API for all three commands).
- `AUTOSTART` injects the default template `LOAD"*",8,1\rRUN\r` via keyboard buffer.
- D64 autostart template is customizable via automation configuration (see `c64-automation.h`).
- HTTP requests execute via libcurl in the VM and return real status/response values.
- `OBS SCREENSHOT` and `OBS RECORDING START`/`STOP` require builds with `ENABLE_FRONTEND_API` enabled (OBS frontend API).

### 6.2 Limits

- Max script size: **1 MiB**
- Max line length: **1024** bytes (parser line buffer)
- Max labels: **256** (`C64SCRIPT_MAX_LABELS`)
- Max loop nesting: **16** for FOR and WHILE loops each (`C64SCRIPT_MAX_FOR_NESTING`, `C64SCRIPT_MAX_WHILE_NESTING`)
- Max GOSUB depth: **32** (`C64SCRIPT_MAX_GOSUB_DEPTH`)
- Max variables: **512** (`C64SCRIPT_MAX_VARIABLES`)
- Max bytecode size: **256 KiB** (`C64SCRIPT_MAX_BYTECODE_SIZE`)

### 6.3 Differences from C64 BASIC V2

- **Execution model:** label-oriented scripts; line numbers are optional.
- **Truth values:** relational operators return `1` for true (BASIC uses `-1`).
- **Arrays:** `DIM X(n)` allocates `n` elements (`0..n-1`), not `n+1` elements.
- **WAIT:** supports BASIC-style `WAIT addr,mask[,value]` plus optional `EVERY <duration>` polling (default 500ms).
- **Strings:** UTF-8 strings; comparisons are case-sensitive lexicographic.
