You are an expert systems programmer and language implementer working on a C-based OBS Studio plugin.

MUST READ::
- script-spec: Detailed language spec.
- script-tasks.md: Breaks down spec into high-level tasks and fills in blanks, but spec remains source of truth where there are conflicts.
- script-progress.md: Contains concrete, low-level tasks, and tracks progress

Authoritative specification:
- The language to implement is defined in the document:
  “C64 Stream Script Language Specification” (`script-spec.md`)
- This document is the single source of truth for:
  - Language name: C64 Stream Script (short form: C64Script)
  - File extension: .c64script
  - Syntax, grammar, semantics, examples, limits, and execution model
- The specification contains a legacy reference section for historical context; the implemented language is the modern, BASIC-inspired language described in the v2 sections.

Do not invent new language features.
Optional extensions may be discussed only if clearly marked as optional and disabled by default.

Must Have Work Approach (NON-NEGOTIABLE):
- Create a script-progress.md with tasks and subtasks in bullet lists, each prefixed with a `[ ]` checkbox.
- This checkbox will be ticked off when a task is done.
- Tasks must be implemented sequentially and include unit tests to ensure the language works properly.
- The unit tests need to be run on each build. Before moving on to a new task, ensure all of its subtasks are done, the build passes locally.
- Before completing the entire work, push and ensure the build passes on CI. If not, keep fixing until it does.
- Any issue you encounter, take full ownership.

----------------------------------------------------------------
MISSION
----------------------------------------------------------------

Design and plan the implementation of **C64 Stream Script** in C such that it is:

- Fast and deterministic
- Extensible and future-proof
- Safe to run inside an OBS plugin
- Strictly defined and well-diagnosed
- Cleanly architected, with no legacy baggage

The result must be suitable for long-term maintenance and incremental evolution.

----------------------------------------------------------------
HIGH-LEVEL GOALS
----------------------------------------------------------------

1. Single coherent language
   - One tokenizer
   - One parser
   - One execution model
   - No legacy modes or fallbacks

2. Minimal dependencies
   - Portable C (C11 acceptable)
   - No external runtimes
   - No unusual libraries
   - Suitable for Windows, macOS, and Linux OBS plugin builds

3. Deterministic execution
   - Stable parsing
   - Stable execution order
   - Reproducible logs and timing behavior
   - Explicit cancellation points

4. Strong diagnostics
   - Clear parse-time errors with line and column
   - Clear runtime errors using BASIC-style terminology where specified
   - No silent failure modes

----------------------------------------------------------------
REQUIRED OUTPUT
----------------------------------------------------------------

Produce a **full design and implementation plan** with the following sections.

----------------------------------------------------------------
A. ARCHITECTURE OVERVIEW
----------------------------------------------------------------

Provide a concrete architecture for:

- Tokenizer (lexer)
- Parser
- Intermediate representation
- Execution engine
- Integration boundaries with OBS and the Ultimate 64 REST and keyboard subsystems

Explicitly state:
- Threading assumptions (worker-thread execution, OBS main-thread interaction)
- Cancellation model (STOP, WAIT polling, network I/O)
- How deterministic execution is preserved

----------------------------------------------------------------
B. IMPLEMENTATION STRATEGY
----------------------------------------------------------------

Choose and justify ONE of the following approaches:

1) AST with direct interpretation
2) AST compiled to bytecode with a small virtual machine

Prefer option (2) if it improves:
- Performance
- Tracing (TRON/TROFF)
- Pause/resume
- Future debugging or stepping

If bytecode is chosen:
- Define the VM model
- Explain why it is appropriate and not overengineered

----------------------------------------------------------------
C. CORE DATA STRUCTURES (C)
----------------------------------------------------------------

Propose concrete C-level data structures for:

- Tokens
  - Type
  - Lexeme slices
  - Line and column
  - Cached numeric values where applicable

- AST or IR nodes
  - Statement kinds
  - Expression kinds
  - Source location tracking

- Bytecode (if used)
  - Opcode enum
  - Instruction layout
  - Constant pool
  - Jump patching strategy

- Runtime values
  - Numeric (double)
  - String (UTF-8)
  - Boolean via numeric truthiness

- Execution context
  - Instruction pointer
  - Variable store
  - FOR / WHILE / GOSUB stacks
  - Cancellation flag
  - Trace and logging state

All limits (nesting depth, stack size, script size) must be explicit and produce clear errors when exceeded.

----------------------------------------------------------------
D. TOKENIZER DESIGN
----------------------------------------------------------------

Design a tokenizer that fully supports the language defined in the specification:

- Case-insensitive keywords, labels, and identifiers
- Identifiers with optional $ / % suffix
- Decimal numbers and hex literals ($C000)
- Duration literals (500ms, 1.5s, 0.5m)
- String literals:
  - Double-quoted
  - BASIC-style doubled quotes
  - Backslash escapes (\r, \n, \t, \xNN)
- Operators:
  - Arithmetic
  - Relational (=, ==, <>, !=, <, <=, >, >=)
  - Boolean (NOT, AND, XOR, OR)
- Parentheses and commas
- Label prefixes at start of line (with or without :)
- Optional numeric labels (BASIC-style line numbers)
- REM comments
- # comments at start of line

Explain how ambiguous cases are resolved, especially:
- Label vs assignment at start of line
- Numeric labels vs numeric literals

----------------------------------------------------------------
E. PARSER DESIGN
----------------------------------------------------------------

Use recursive descent parsing.

Cover all constructs from the specification, including:

- Assignment (LET optional)
- IF / THEN (single-line and block forms)
- FOR / TO / STEP / NEXT
- WHILE / WEND / ENDWHILE / END WHILE
- GOTO, GOSUB, RETURN, STOP / END
- WAIT <duration> and WAIT UNTIL <expr>
- Plugin actions:
  EFFECT, EFFECTPARAM, PALETTE,
  PLAYSID, RUNPRG, MOUNTDISK,
  AUTOSTART, RESET, REBOOT,
  RECORDSTART, RECORDSTOP,
  TYPE, KEY,
  LOGFILE, LOG,
  TRON, TROFF,
  PRINT,
  POKE
- Built-in functions, especially PEEK(address)

Explain:
- Operator precedence (must exactly match the specification)
- Label resolution rules
- Jump and return patching strategy

----------------------------------------------------------------
F. EXECUTION MODEL
----------------------------------------------------------------

Define precise execution semantics:

- Sequential execution unless control flow changes it
- STOP / END behavior
- FOR, WHILE, and GOSUB stack behavior
- WAIT semantics:
  - Duration waits
  - WAIT UNTIL wall-clock semantics and time parsing

Define runtime error handling:
- Parse-time vs runtime errors
- BASIC-style error terminology where specified
- Network failures (PEEK/POKE) must fail clearly and deterministically

Explain how:
- REST DMA constraints are enforced (e.g. illegal I/O register writes)
- Keyboard injection constraints are handled
- Execution remains cancellable at all times

----------------------------------------------------------------
G. EXTENSIBILITY AND FUTURE-PROOFING
----------------------------------------------------------------

Show how the design allows:

- Adding new statements
- Adding new built-in functions
- Improving diagnostics and tracing
- Optional future value types without large refactors

Propose:
- A builtin registry model
- Opcode or dispatch-table organization
- A clean versioning strategy for the language itself (not legacy compatibility)

----------------------------------------------------------------
H. PERFORMANCE CONSIDERATIONS
----------------------------------------------------------------

Identify performance-sensitive areas:

- Tokenization and string handling
- Expression evaluation
- Bytecode dispatch (if applicable)
- WAIT polling granularity
- REST batching for POKE blocks
- OBS property update frequency

Provide concrete, maintainable optimizations only.

----------------------------------------------------------------
I. TESTING AND VALIDATION
----------------------------------------------------------------

Provide a test strategy including:

- Lexer and parser unit tests
- Grammar coverage tests
- Error-message golden tests
- Runtime tests with mocked REST backend
- Determinism tests (same script, same output)

Include a small set of test scripts derived from the specification examples.

----------------------------------------------------------------
OUTPUT FORMAT
----------------------------------------------------------------

Produce:

1) A concise architecture overview
2) A detailed technical design with C-level structures
3) A proposed bytecode instruction set (if used)
4) A step-by-step implementation roadmap with merge-safe milestones

Constraints recap:
- Portable C only
- No unusual dependencies
- No legacy compatibility modes
- No invented language features
- Correctness and clarity over premature optimization

Now produce the full design and implementation plan.
