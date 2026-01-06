/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/**
 * C64Script - BASIC-Inspired Scripting Language
 *
 * Features :
 * - Case-insensitive keywords and identifiers
 * - BASIC-style control flow (IF/FOR/WHILE/GOSUB/RETURN)
 * - Expressions with proper operator precedence
 * - Optional BASIC-style line numbers
 * - Built-in functions (PEEK/POKE for DMA)
 * - Keyboard injection (TYPE/KEY)
 * - Logging and tracing (LOG/TRON/TROFF)
 * - Plugin control (EFFECT/PALETTE/etc.)
 */

// ============================================================================
// LIMITS AND CONSTANTS
// ============================================================================

#define C64SCRIPT_MAX_TOKEN_LENGTH 512
#define C64SCRIPT_MAX_STRING_LENGTH 4096
#define C64SCRIPT_MAX_LINE_LENGTH 1024
#define C64SCRIPT_MAX_SCRIPT_SIZE (1024 * 1024) // 1 MiB
#define C64SCRIPT_MAX_LABELS 256
#define C64SCRIPT_MAX_VARIABLES 512
#define C64SCRIPT_MAX_CONSTANTS 1024
#define C64SCRIPT_MAX_STACK_DEPTH 64
#define C64SCRIPT_MAX_FOR_NESTING 16
#define C64SCRIPT_MAX_WHILE_NESTING 16
#define C64SCRIPT_MAX_GOSUB_DEPTH 32
#define C64SCRIPT_MAX_BYTECODE_SIZE (256 * 1024) // 256 KiB

// ============================================================================
// TOKEN TYPES
// ============================================================================

typedef enum {
    // End of file
    TOKEN_EOF,

    // Literals
    TOKEN_NUMBER,     // 123, 1.5
    TOKEN_HEX_NUMBER, // $C000
    TOKEN_STRING,     // "hello"
    TOKEN_DURATION,   // 500ms, 1.5s
    TOKEN_C64U_PATH,  // c64u:/path (unquoted)

    // Identifiers and labels
    TOKEN_IDENTIFIER, // var, VAR, var$, var%
    TOKEN_LABEL,      // label:, 10:

    // Keywords - control flow
    TOKEN_IF,
    TOKEN_THEN,
    TOKEN_ELSE,
    TOKEN_ENDIF,
    TOKEN_FOR,
    TOKEN_TO,
    TOKEN_STEP,
    TOKEN_NEXT,
    TOKEN_WHILE,
    TOKEN_WEND,
    TOKEN_ENDWHILE,
    TOKEN_END,
    TOKEN_GOTO,
    TOKEN_GOSUB,
    TOKEN_RETURN,
    TOKEN_STOP,
    TOKEN_LABEL_KW, // LABEL keyword (not a label definition)

    // Keywords - variables
    TOKEN_LET,
    TOKEN_DIM,

    // Keywords - functions
    TOKEN_FUN,
    TOKEN_ENDFUN,

    // Keywords - comments
    TOKEN_REM,

    // Keywords - waiting
    TOKEN_WAIT,
    TOKEN_UNTIL,

    // Keywords - plugin actions
    TOKEN_EFFECT,
    TOKEN_EFFECTPARAM,
    TOKEN_PALETTE,
    TOKEN_PALETTECOLOR,
    TOKEN_PLAYSID,
    TOKEN_RUNPRG,
    TOKEN_RUNLOCAL,
    TOKEN_MOUNTDISK,
    TOKEN_AUTOSTART,
    TOKEN_RESET,
    TOKEN_REBOOT,
    TOKEN_RECORDSTART,
    TOKEN_RECORDSTOP,

    // Keywords - I/O
    TOKEN_TYPE_KEYWORD,
    TOKEN_KEY,
    TOKEN_POKE,
    TOKEN_PEEK, // Also a function name
    TOKEN_PRINT,
    TOKEN_LOG,
    TOKEN_LOGFILE,
    TOKEN_TRON,
    TOKEN_TROFF,
    TOKEN_READFILE,
    TOKEN_WRITEFILE,

    // Keywords - log file mode
    TOKEN_APPEND,
    TOKEN_TRUNCATE,

    // Keywords - PLAYSID parameter
    TOKEN_SONGNR,

    // Keywords - RUNLOCAL parameters
    TOKEN_ARGS,
    TOKEN_STATUS,
    TOKEN_OUTPUT,

    // Keywords - HTTP
    TOKEN_HTTP,
    TOKEN_GET,
    TOKEN_POST,
    TOKEN_PUT,
    TOKEN_DELETE,
    TOKEN_PATCH,
    TOKEN_HEADERS,
    TOKEN_BODY,
    TOKEN_RESPONSE,

    // Operators - arithmetic
    TOKEN_PLUS,     // +
    TOKEN_MINUS,    // -
    TOKEN_MULTIPLY, // *
    TOKEN_DIVIDE,   // /

    // Operators - relational
    TOKEN_EQ,     // =
    TOKEN_EQ_EQ,  // ==
    TOKEN_NE,     // <>
    TOKEN_NE_ALT, // !=
    TOKEN_LT,     // <
    TOKEN_LE,     // <=
    TOKEN_GT,     // >
    TOKEN_GE,     // >=

    // Operators - boolean
    TOKEN_NOT,
    TOKEN_AND,
    TOKEN_XOR,
    TOKEN_OR,

    // Delimiters
    TOKEN_LPAREN,   // (
    TOKEN_RPAREN,   // )
    TOKEN_LBRACKET, // [
    TOKEN_RBRACKET, // ]
    TOKEN_LBRACE,   // {
    TOKEN_RBRACE,   // }
    TOKEN_COMMA,    // ,
    TOKEN_COLON,    // :

    // Special
    TOKEN_NEWLINE,
    TOKEN_ERROR,

} c64script_token_type_t;

// ============================================================================
// TOKEN STRUCTURE
// ============================================================================

typedef struct {
    c64script_token_type_t type;

    // Position in source
    int line;
    int column;

    // Lexeme (slice of source text)
    const char *start;
    size_t length;

    // Cached parsed values (populated during tokenization)
    union {
        double number;        // For TOKEN_NUMBER, TOKEN_HEX_NUMBER
        uint32_t duration_ms; // For TOKEN_DURATION
        // String content is stored in string_pool during parsing
    } value;

} c64script_token_t;

// ============================================================================
// AST NODE TYPES
// ============================================================================

typedef enum {
    // Statements
    AST_STMT_EMPTY,
    AST_STMT_REM,
    AST_STMT_LABEL,
    AST_STMT_ASSIGNMENT,
    AST_STMT_DIM,
    AST_STMT_ARRAY_SET,
    AST_STMT_MAP_SET,
    AST_STMT_IF,
    AST_STMT_FOR,
    AST_STMT_WHILE,
    AST_STMT_FUNCTION_DEF,
    AST_STMT_GOTO,
    AST_STMT_GOSUB,
    AST_STMT_RETURN,
    AST_STMT_STOP,
    AST_STMT_WAIT,
    AST_STMT_WAIT_UNTIL,
    AST_STMT_EFFECT,
    AST_STMT_EFFECTPARAM,
    AST_STMT_PALETTE,
    AST_STMT_PALETTECOLOR,
    AST_STMT_PLAYSID,
    AST_STMT_RUNPRG,
    AST_STMT_RUNLOCAL,
    AST_STMT_MOUNTDISK,
    AST_STMT_AUTOSTART,
    AST_STMT_RESET,
    AST_STMT_REBOOT,
    AST_STMT_RECORDSTART,
    AST_STMT_RECORDSTOP,
    AST_STMT_TYPE,
    AST_STMT_KEY,
    AST_STMT_POKE,
    AST_STMT_LOGFILE,
    AST_STMT_LOG,
    AST_STMT_PRINT,
    AST_STMT_TRON,
    AST_STMT_TROFF,
    AST_STMT_READFILE,
    AST_STMT_WRITEFILE,
    AST_STMT_HTTP,

    // Expressions
    AST_EXPR_NUMBER,
    AST_EXPR_STRING,
    AST_EXPR_IDENTIFIER,
    AST_EXPR_ARRAY_ACCESS,
    AST_EXPR_MAP_ACCESS,
    AST_EXPR_UNARY,
    AST_EXPR_BINARY,
    AST_EXPR_CALL,

} c64script_ast_type_t;

typedef enum {
    C64SCRIPT_WAIT_UNIT_MS,
    C64SCRIPT_WAIT_UNIT_S,
    C64SCRIPT_WAIT_UNIT_M,
} c64script_wait_unit_t;

typedef enum {
    C64SCRIPT_BUILTIN_LEN,
    C64SCRIPT_BUILTIN_LEFT,
    C64SCRIPT_BUILTIN_RIGHT,
    C64SCRIPT_BUILTIN_MID,
    C64SCRIPT_BUILTIN_CHR,
    C64SCRIPT_BUILTIN_ASC,
    C64SCRIPT_BUILTIN_VAL,
    C64SCRIPT_BUILTIN_ABS,
    C64SCRIPT_BUILTIN_INT,
    C64SCRIPT_BUILTIN_RND,
    C64SCRIPT_BUILTIN_SIN,
    C64SCRIPT_BUILTIN_COS,
    C64SCRIPT_BUILTIN_TAN,
    C64SCRIPT_BUILTIN_SQRT,
    C64SCRIPT_BUILTIN_LOG,
    C64SCRIPT_BUILTIN_EXP,
    C64SCRIPT_BUILTIN_TIME,
} c64script_builtin_id_t;

// Forward declarations
typedef struct c64script_ast_node c64script_ast_node_t;
typedef struct c64script_ast_expr c64script_ast_expr_t;

// Expression operators
typedef enum {
    // Arithmetic
    EXPR_OP_ADD,
    EXPR_OP_SUBTRACT,
    EXPR_OP_MULTIPLY,
    EXPR_OP_DIVIDE,
    EXPR_OP_NEGATE,

    // Relational
    EXPR_OP_EQ,
    EXPR_OP_NE,
    EXPR_OP_LT,
    EXPR_OP_LE,
    EXPR_OP_GT,
    EXPR_OP_GE,

    // Boolean
    EXPR_OP_NOT,
    EXPR_OP_AND,
    EXPR_OP_XOR,
    EXPR_OP_OR,

} c64script_operator_t;

// Expression node
struct c64script_ast_expr {
    c64script_ast_type_t type;
    int line; // For error reporting

    union {
        double number;
        const char *string;     // Points into string pool
        const char *identifier; // Points into string pool

        struct {
            const char *name;            // Array variable name (without () suffix)
            c64script_ast_expr_t *index; // Index expression
        } array_access;

        struct {
            const char *name;          // Map variable name (without {} suffix)
            c64script_ast_expr_t *key; // Key expression
        } map_access;

        struct {
            c64script_operator_t op;
            c64script_ast_expr_t *operand;
        } unary;

        struct {
            c64script_operator_t op;
            c64script_ast_expr_t *left;
            c64script_ast_expr_t *right;
        } binary;

        struct {
            const char *name; // Points into string pool
            c64script_ast_expr_t **args;
            size_t arg_count;
        } call;
    } as;
};

// Statement node
struct c64script_ast_node {
    c64script_ast_type_t type;
    int line;                   // For error reporting
    c64script_ast_node_t *next; // Linked list of statements

    union {
        // Empty and REM have no data

        struct {
            const char *name; // Points into string pool
        } label;

        struct {
            const char *variable; // Points into string pool
            c64script_ast_expr_t *value;
        } assignment;

        struct {
            const char *array_name;     // Array name (without () suffix)
            c64script_ast_expr_t *size; // Size expression
        } dim_stmt;

        struct {
            const char *array_name;      // Array name
            c64script_ast_expr_t *index; // Index expression
            c64script_ast_expr_t *value; // Value to set
        } array_set;

        struct {
            const char *map_name;        // Map name
            c64script_ast_expr_t *key;   // Key expression
            c64script_ast_expr_t *value; // Value to set
        } map_set;

        struct {
            c64script_ast_expr_t *condition;
            c64script_ast_node_t *then_branch;
            c64script_ast_node_t *else_branch;
        } if_stmt;

        struct {
            const char *variable; // Points into string pool
            c64script_ast_expr_t *start;
            c64script_ast_expr_t *end;
            c64script_ast_expr_t *step; // NULL means 1
            c64script_ast_node_t *body;
        } for_stmt;

        struct {
            c64script_ast_expr_t *condition;
            c64script_ast_node_t *body;
        } while_stmt;

        struct {
            const char *name;           // Function name
            const char **param_names;   // Array of parameter names
            size_t param_count;         // Number of parameters
            c64script_ast_node_t *body; // Function body statements
        } function_def;

        struct {
            const char *label; // Points into string pool
        } goto_stmt;

        struct {
            const char *label;             // Points into string pool
            c64script_ast_expr_t **params; // Array of parameter expressions
            size_t param_count;            // Number of parameters
        } gosub_stmt;

        struct {
            c64script_ast_expr_t *return_value; // NULL means no return value
        } return_stmt;
        // stop_stmt has no data

        struct {
            c64script_ast_expr_t *duration;
            c64script_wait_unit_t unit;
        } wait_stmt;

        struct {
            c64script_ast_expr_t *time_expr;
        } wait_until_stmt;

        struct {
            c64script_ast_expr_t *preset_name;
        } effect_stmt;

        struct {
            c64script_ast_expr_t *param_name;
            c64script_ast_expr_t *param_value;
        } effectparam_stmt;

        struct {
            c64script_ast_expr_t *palette_name;
        } palette_stmt;

        struct {
            c64script_ast_expr_t *index;
            c64script_ast_expr_t *r;
            c64script_ast_expr_t *g;
            c64script_ast_expr_t *b;
        } palettecolor_stmt;

        struct {
            c64script_ast_expr_t *path;
            c64script_ast_expr_t *songnr; // NULL means 0
        } playsid_stmt;

        struct {
            c64script_ast_expr_t *path;
        } runprg_stmt;

        struct {
            c64script_ast_expr_t *path;       // Executable path
            c64script_ast_expr_t *args;       // NULL means no arguments
            c64script_ast_expr_t *status_var; // NULL means don't store status
            c64script_ast_expr_t *output_var; // NULL means don't capture output
        } runlocal_stmt;

        struct {
            c64script_ast_expr_t *path;
        } mountdisk_stmt;

        // autostart_stmt has no data
        // reset_stmt has no data
        // reboot_stmt has no data
        // recordstart_stmt has no data
        // recordstop_stmt has no data

        struct {
            c64script_ast_expr_t *text;
        } type_stmt;

        struct {
            c64script_ast_expr_t *key;
        } key_stmt;

        struct {
            c64script_ast_expr_t *address;
            c64script_ast_expr_t **values;      // NULL means single value
            size_t value_count;                 // 0 means single value in address
            c64script_ast_expr_t *single_value; // Used when value_count == 0
        } poke_stmt;

        struct {
            c64script_ast_expr_t *path;
            bool truncate; // false means append
        } logfile_stmt;

        struct {
            c64script_ast_expr_t *message;
        } log_stmt;

        struct {
            c64script_ast_expr_t *message;
        } print_stmt;

        // tron_stmt has no data
        // troff_stmt has no data

        struct {
            c64script_ast_expr_t *variable; // Variable name to store the content
            c64script_ast_expr_t *path;     // File path to read
        } readfile_stmt;

        struct {
            c64script_ast_expr_t *path;    // File path to write
            c64script_ast_expr_t *content; // Content to write
            bool truncate;                 // false means append, true means truncate
        } writefile_stmt;

        struct {
            int method;                         // HTTP method: 0=GET, 1=POST, 2=PUT, 3=DELETE, 4=PATCH
            c64script_ast_expr_t *url;          // URL expression
            c64script_ast_expr_t *headers;      // NULL means no custom headers (string expression)
            c64script_ast_expr_t *body;         // NULL means no body (string expression)
            c64script_ast_expr_t *status_var;   // NULL means don't store status (string expression - variable name)
            c64script_ast_expr_t *response_var; // NULL means don't store response (string expression - variable name)
        } http_stmt;
    } as;
};

// ============================================================================
// BYTECODE OPCODES
// ============================================================================

typedef enum {
    // Stack operations
    OP_NOP,        // No operation
    OP_PUSH_CONST, // Push constant from pool
    OP_PUSH_NUM,   // Push immediate number
    OP_PUSH_VAR,   // Push variable value
    OP_POP_VAR,    // Pop and store to variable
    OP_POP,        // Pop and discard

    // Arithmetic
    OP_ADD,      // +
    OP_SUBTRACT, // -
    OP_MULTIPLY, // *
    OP_DIVIDE,   // /
    OP_NEGATE,   // unary -

    // Relational
    OP_EQ, // =
    OP_NE, // <>
    OP_LT, // <
    OP_LE, // <=
    OP_GT, // >
    OP_GE, // >=

    // Boolean (bitwise on truncated integers)
    OP_NOT,
    OP_AND,
    OP_XOR,
    OP_OR,

    // Control flow
    OP_JUMP,          // Unconditional jump
    OP_JUMP_IF_FALSE, // Conditional jump (pop condition)
    OP_CALL,          // GOSUB (push return address, jump)
    OP_RETURN,        // POP return address, jump
    OP_CALL_FUNCTION, // User-defined function call
    OP_RETURN_VALUE,  // Return from function with value on stack

    // Arrays
    OP_DIM_ARRAY, // Allocate array (operand = constant pool index for name)
    OP_ARRAY_GET, // Get array element (stack: index → value)
    OP_ARRAY_SET, // Set array element (stack: value, index →)

    // Maps
    OP_MAP_GET, // Get map value (stack: key → value)
    OP_MAP_SET, // Set map value (stack: value, key →)

    // Function scope
    OP_ENTER_SCOPE, // Enter function scope (operand = param count)
    OP_EXIT_SCOPE,  // Exit function scope

    // Loops
    OP_FOR_INIT,    // Initialize FOR loop (push loop state)
    OP_FOR_CHECK,   // Check FOR condition, jump if done
    OP_FOR_INCR,    // Increment loop variable
    OP_WHILE_CHECK, // Check WHILE condition, jump if false

    // Waiting
    OP_WAIT,       // Wait for duration (operand = c64script_wait_unit_t)
    OP_WAIT_UNTIL, // Wait until wall-clock target

    // Built-in functions
    OP_CALL_PEEK,    // PEEK(address) - REST DMA read
    OP_CALL_STR,     // STR$(number) - Convert number to string
    OP_CALL_BUILTIN, // Generic built-in call (future)

    // Plugin actions
    OP_EFFECT,
    OP_EFFECTPARAM,
    OP_PALETTE,
    OP_PALETTECOLOR,
    OP_PLAYSID,
    OP_RUNPRG,
    OP_RUNLOCAL,
    OP_MOUNTDISK,
    OP_AUTOSTART,
    OP_RESET,
    OP_REBOOT,
    OP_RECORDSTART,
    OP_RECORDSTOP,
    OP_TYPE,
    OP_KEY,
    OP_POKE_SINGLE,
    OP_POKE_ARRAY,
    OP_LOG,
    OP_PRINT,
    OP_LOGFILE,
    OP_TRON,
    OP_TROFF,
    OP_READFILE,
    OP_WRITEFILE_APPEND,
    OP_WRITEFILE_TRUNCATE,
    OP_HTTP,

    // Termination
    OP_STOP, // Stop execution
    OP_HALT, // End of script

} c64script_opcode_t;

// Bytecode instruction (fixed size for simplicity)
typedef struct {
    c64script_opcode_t opcode;
    uint32_t operand; // Constant pool index, jump address, or immediate value
    int source_line;  // For tracing and error reporting
} c64script_instruction_t;

// ============================================================================
// RUNTIME VALUES
// ============================================================================

typedef enum {
    VALUE_NUMBER,
    VALUE_STRING,
    VALUE_ARRAY,
    VALUE_MAP,
} c64script_value_type_t;

// Forward declarations for recursive types
struct c64script_value;
typedef struct c64script_value c64script_value_t;
struct c64script_array;
typedef struct c64script_array c64script_array_t;
struct c64script_map;
typedef struct c64script_map c64script_map_t;

// Complete value type definition
struct c64script_value {
    c64script_value_type_t type;
    union {
        double number;
        char *string;             // Owned by runtime, must be freed
        c64script_array_t *array; // Owned by runtime, must be freed
        c64script_map_t *map;     // Owned by runtime, must be freed
    } as;
};

// Array type
struct c64script_array {
    c64script_value_type_t element_type; // Element type (NUMBER or STRING)
    size_t size;                         // Number of elements
    c64script_value_t *elements;         // Array of values
};

// Map entry
typedef struct {
    char *key;               // Key string (owned, must be freed)
    c64script_value_t value; // Value (owned)
    uint32_t hash;           // Cached hash for key
} c64script_map_entry_t;

// Map type (hash table)
struct c64script_map {
    c64script_map_entry_t *entries;    // Array of entries
    size_t count;                      // Number of entries
    size_t capacity;                   // Capacity of entries array
    c64script_value_type_t value_type; // Value type (NUMBER or STRING)
};

// ============================================================================
// EXECUTION CONTEXT
// ============================================================================

// FOR loop state
typedef struct {
    const char *variable; // Loop variable name
    double end_value;
    double step_value;
    size_t loop_start_ip; // Bytecode address of loop body
} c64script_for_state_t;

// WHILE loop state
typedef struct {
    size_t condition_ip;  // Bytecode address of condition check
    size_t loop_start_ip; // Bytecode address of loop body
} c64script_while_state_t;

// GOSUB return state
typedef struct {
    size_t return_ip;   // Bytecode address to return to
    size_t param_count; // Number of parameters passed
} c64script_gosub_state_t;

// Variable storage (simple array for now, could be hash table)
typedef struct {
    char name[64];
    c64script_value_t value;
} c64script_variable_t;

// Function definition
typedef struct {
    char name[64];           // Function name (uppercase)
    size_t bytecode_address; // Entry point in bytecode
    size_t param_count;      // Number of parameters
    char **param_names;      // Parameter names (uppercase)
} c64script_function_def_t;

// Function scope (stack frame for local variables)
typedef struct {
    c64script_variable_t *local_vars; // Local variables
    size_t local_var_count;
    size_t local_var_capacity;
    size_t saved_var_count; // Number of global variables to restore
    size_t return_ip;       // Return address
} c64script_scope_t;

// Execution context
typedef struct {
    // Bytecode
    c64script_instruction_t *bytecode;
    size_t bytecode_size;
    size_t ip; // Instruction pointer

    // Constant pool
    c64script_value_t *constants;
    size_t constant_count;

    // Variable storage
    c64script_variable_t *variables;
    size_t variable_count;
    size_t variable_capacity;

    // Value stack
    c64script_value_t *stack;
    size_t stack_size;
    size_t stack_capacity;

    // Loop and call stacks
    c64script_for_state_t for_stack[C64SCRIPT_MAX_FOR_NESTING];
    size_t for_stack_size;

    c64script_while_state_t while_stack[C64SCRIPT_MAX_WHILE_NESTING];
    size_t while_stack_size;

    c64script_gosub_state_t gosub_stack[C64SCRIPT_MAX_GOSUB_DEPTH];
    size_t gosub_stack_size;

    // Function definitions
    c64script_function_def_t *functions;
    size_t function_count;
    size_t function_capacity;

    // Function call stack (scopes)
    c64script_scope_t *scope_stack;
    size_t scope_stack_size;
    size_t scope_stack_capacity;

    // Execution state (volatile for thread synchronization)
    volatile bool should_stop;  // Cancellation flag
    volatile bool should_pause; // Pause flag (set by debugger)
    volatile bool is_paused;    // Current pause state
    volatile bool step_mode;    // Single-step mode
    bool trace_enabled;         // TRON/TROFF state

    // Iteration limit for testing (0 = unlimited)
    uint64_t max_iterations;
    uint64_t iteration_count;

    // Line tracking for debugging (volatile for thread synchronization)
    volatile int last_executed_line;   // Last source line that completed execution
    volatile int next_line_to_execute; // Next source line to execute

    // Original source text for line display
    char *source_text;
    size_t source_text_size;

    // Log file
    FILE *log_file;
    char log_filename[512];

    // Error reporting
    char error_msg[1024];
    int error_line;

    // Execution trace (for testing/verification)
    bool trace_recording_enabled;
    bool trace_first_entry; // Track if this is the first trace entry
    FILE *trace_file;
    char trace_filename[512];
    size_t trace_step_count; // Number of trace entries recorded
    char *trace_buffer;      // Buffer to collect trace entries
    size_t trace_buffer_size;
    size_t trace_buffer_capacity;

    // Integration points (set by executor)
    void *source_data; // OBS source data
    void *obs_source;  // obs_source_t*
    void *rest_client; // REST client for PEEK/POKE
    void *keyboard;    // Keyboard injection module

} c64script_runtime_t;

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Parse a C64Script source file into an AST
 * Returns NULL on parse error (check error_msg)
 */
c64script_ast_node_t *c64script_parse(const char *source, size_t source_size, char *error_msg, size_t error_msg_size);

typedef struct {
    bool log_errors; // When false, parse errors are only logged if debug logging is enabled
} c64script_parse_options_t;

/**
 * Parse a C64Script source file into an AST with configurable logging behavior.
 */
c64script_ast_node_t *c64script_parse_with_options(const char *source, size_t source_size, char *error_msg,
                                                   size_t error_msg_size, const c64script_parse_options_t *options);

/**
 * Compile an AST into bytecode
 * Returns false on compilation error (check error_msg)
 */
bool c64script_compile(c64script_ast_node_t *ast, c64script_runtime_t *runtime, char *error_msg, size_t error_msg_size);

/**
 * Execute bytecode in a runtime context
 * Returns false on runtime error (check runtime->error_msg)
 */
bool c64script_execute(c64script_runtime_t *runtime);

/**
 * Create a new runtime context
 */
c64script_runtime_t *c64script_runtime_create(void);

/**
 * Destroy a runtime context
 */
void c64script_runtime_destroy(c64script_runtime_t *runtime);

/**
 * Enable execution trace recording to a JSON file
 * Must be called before c64script_execute()
 */
bool c64script_enable_trace_recording(c64script_runtime_t *runtime, const char *filename);

/**
 * Finalize trace recording with execution status and error (if any)
 * Called automatically at end of execution
 */
void c64script_finalize_trace_recording(c64script_runtime_t *runtime, bool success, const char *error_msg);

/**
 * Free an AST
 */
void c64script_ast_free(c64script_ast_node_t *ast);
