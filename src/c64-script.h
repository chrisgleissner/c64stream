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
 * This is a complete rewrite of the C64Script language with:
 * - Case-insensitive keywords and identifiers
 * - BASIC-style control flow (IF/FOR/WHILE/GOSUB/RETURN)
 * - Expressions with proper operator precedence
 * - Optional BASIC-style line numbers
 * - Built-in functions (PEEK/POKE for DMA)
 * - Keyboard injection (TYPE/KEY)
 * - Logging and tracing (LOG/TRON/TROFF)
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
    AST_STMT_IF,
    AST_STMT_FOR,
    AST_STMT_WHILE,
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

    // Expressions
    AST_EXPR_NUMBER,
    AST_EXPR_STRING,
    AST_EXPR_IDENTIFIER,
    AST_EXPR_UNARY,
    AST_EXPR_BINARY,
    AST_EXPR_CALL,

} c64script_ast_type_t;

typedef enum {
    C64SCRIPT_WAIT_UNIT_MS,
    C64SCRIPT_WAIT_UNIT_S,
    C64SCRIPT_WAIT_UNIT_M,
} c64script_wait_unit_t;

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
            const char *label; // Points into string pool
        } goto_stmt;

        struct {
            const char *label; // Points into string pool
        } gosub_stmt;

        // return_stmt has no data
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
    OP_CALL_BUILTIN, // Generic built-in call (future)

    // Plugin actions
    OP_EFFECT,
    OP_EFFECTPARAM,
    OP_PALETTE,
    OP_PALETTECOLOR,
    OP_PLAYSID,
    OP_RUNPRG,
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
} c64script_value_type_t;

typedef struct {
    c64script_value_type_t type;
    union {
        double number;
        char *string; // Owned by runtime, must be freed
    } as;
} c64script_value_t;

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
    size_t return_ip; // Bytecode address to return to
} c64script_gosub_state_t;

// Variable storage (simple array for now, could be hash table)
typedef struct {
    char name[64];
    c64script_value_t value;
} c64script_variable_t;

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

    // Execution state
    bool should_stop;   // Cancellation flag
    bool trace_enabled; // TRON/TROFF state

    // Log file
    FILE *log_file;
    char log_filename[512];

    // Error reporting
    char error_msg[1024];
    int error_line;

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
 * Free an AST
 */
void c64script_ast_free(c64script_ast_node_t *ast);
