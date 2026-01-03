/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script.h"
#include "c64-script-bytecode.h"
#include "c64-logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MACRO_LOG_PREFIX "[c64script-bytecode] "

// ============================================================================
// COMPILER CONTEXT
// ============================================================================

typedef struct {
    // Bytecode buffer
    c64script_instruction_t *instructions;
    size_t instruction_count;
    size_t instruction_capacity;

    // Constant pool
    c64script_value_t *constants;
    size_t constant_count;
    size_t constant_capacity;

    // Label address mapping
    struct {
        char name[64];
        size_t address; // Bytecode address, or SIZE_MAX if not yet defined
    } labels[C64SCRIPT_MAX_LABELS];
    size_t label_count;

    // Forward jump patches (jumps to labels not yet defined)
    struct {
        size_t instruction_index; // Instruction to patch
        char label[64];           // Target label
    } patches[C64SCRIPT_MAX_LABELS];
    size_t patch_count;

    // Error reporting
    char *error_msg;
    size_t error_msg_size;

} compiler_context_t;

static void compiler_context_destroy(compiler_context_t *ctx)
{
    if (!ctx) {
        return;
    }

    if (ctx->constants) {
        for (size_t i = 0; i < ctx->constant_count; i++) {
            if (ctx->constants[i].type == VALUE_STRING) {
                free(ctx->constants[i].as.string);
                ctx->constants[i].as.string = NULL;
            }
        }
        free(ctx->constants);
        ctx->constants = NULL;
    }

    free(ctx->instructions);
    ctx->instructions = NULL;

    ctx->instruction_count = 0;
    ctx->instruction_capacity = 0;
    ctx->constant_count = 0;
    ctx->constant_capacity = 0;
    ctx->label_count = 0;
    ctx->patch_count = 0;
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Emit a bytecode instruction
static void emit(compiler_context_t *ctx, c64script_opcode_t opcode, uint32_t operand, int source_line)
{
    // Grow buffer if needed
    if (ctx->instruction_count >= ctx->instruction_capacity) {
        size_t new_cap = ctx->instruction_capacity == 0 ? 256 : ctx->instruction_capacity * 2;
        c64script_instruction_t *new_instructions =
            realloc(ctx->instructions, new_cap * sizeof(c64script_instruction_t));
        if (!new_instructions) {
            blog(LOG_ERROR, "Failed to grow instruction buffer");
            return;
        }
        ctx->instructions = new_instructions;
        ctx->instruction_capacity = new_cap;
    }

    ctx->instructions[ctx->instruction_count++] =
        (c64script_instruction_t){.opcode = opcode, .operand = operand, .source_line = source_line};
}

// Add constant to pool, return index
static uint32_t add_constant(compiler_context_t *ctx, c64script_value_t value)
{
    // Check if constant already exists
    for (size_t i = 0; i < ctx->constant_count; i++) {
        c64script_value_t *existing = &ctx->constants[i];
        if (existing->type == value.type) {
            if (value.type == VALUE_NUMBER && existing->as.number == value.as.number) {
                return (uint32_t)i;
            }
            if (value.type == VALUE_STRING && strcmp(existing->as.string, value.as.string) == 0) {
                return (uint32_t)i;
            }
        }
    }

    // Grow constant pool if needed
    if (ctx->constant_count >= ctx->constant_capacity) {
        size_t new_cap = ctx->constant_capacity == 0 ? 128 : ctx->constant_capacity * 2;
        c64script_value_t *new_constants = realloc(ctx->constants, new_cap * sizeof(c64script_value_t));
        if (!new_constants) {
            blog(LOG_ERROR, "Failed to grow constant pool");
            return UINT32_MAX;
        }
        ctx->constants = new_constants;
        ctx->constant_capacity = new_cap;
    }

    // Add new constant (string needs to be duplicated)
    c64script_value_t new_const = value;
    if (value.type == VALUE_STRING) {
        new_const.as.string = strdup(value.as.string);
        if (!new_const.as.string) {
            return UINT32_MAX;
        }
    }
    ctx->constants[ctx->constant_count] = new_const;
    return (uint32_t)(ctx->constant_count++);
}

// Define a label at current bytecode address
static bool define_label(compiler_context_t *ctx, const char *label_name, int source_line)
{
    for (size_t i = 0; i < ctx->label_count; i++) {
        if (strcmp(ctx->labels[i].name, label_name) == 0) {
            if (ctx->error_msg) {
                snprintf(ctx->error_msg, ctx->error_msg_size, "Duplicate label: %s (line %d)", label_name, source_line);
            }
            return false;
        }
    }
    // Add new label
    if (ctx->label_count < C64SCRIPT_MAX_LABELS) {
        strncpy(ctx->labels[ctx->label_count].name, label_name, 63);
        ctx->labels[ctx->label_count].name[63] = '\0';
        ctx->labels[ctx->label_count].address = ctx->instruction_count;
        ctx->label_count++;
        return true;
    }

    if (ctx->error_msg) {
        snprintf(ctx->error_msg, ctx->error_msg_size, "Too many labels");
    }
    return false;
}

// Get label address (returns SIZE_MAX if not found/not yet defined)
static size_t get_label_address(compiler_context_t *ctx, const char *label_name)
{
    for (size_t i = 0; i < ctx->label_count; i++) {
        if (strcmp(ctx->labels[i].name, label_name) == 0) {
            return ctx->labels[i].address;
        }
    }
    return SIZE_MAX;
}

// Emit a jump instruction (may need patching later)
static void emit_jump(compiler_context_t *ctx, c64script_opcode_t opcode, const char *label, int source_line)
{
    size_t addr = get_label_address(ctx, label);
    if (addr == SIZE_MAX) {
        // Label not yet defined - record for patching
        if (ctx->patch_count < C64SCRIPT_MAX_LABELS) {
            ctx->patches[ctx->patch_count].instruction_index = ctx->instruction_count;
            strncpy(ctx->patches[ctx->patch_count].label, label, 63);
            ctx->patches[ctx->patch_count].label[63] = '\0';
            ctx->patch_count++;
        }
        emit(ctx, opcode, 0, source_line); // Placeholder address
    } else {
        emit(ctx, opcode, (uint32_t)addr, source_line);
    }
}

// Patch forward jumps after all labels are defined
static bool patch_jumps(compiler_context_t *ctx)
{
    for (size_t i = 0; i < ctx->patch_count; i++) {
        size_t addr = get_label_address(ctx, ctx->patches[i].label);
        if (addr == SIZE_MAX) {
            if (ctx->error_msg) {
                snprintf(ctx->error_msg, ctx->error_msg_size, "Undefined label: %s", ctx->patches[i].label);
            }
            return false;
        }
        ctx->instructions[ctx->patches[i].instruction_index].operand = (uint32_t)addr;
    }
    return true;
}

// ============================================================================
// COMPILATION FUNCTIONS (Forward declarations)
// ============================================================================

static bool compile_expression(compiler_context_t *ctx, c64script_ast_expr_t *expr);
static bool compile_statement(compiler_context_t *ctx, c64script_ast_node_t *stmt);
static bool compile_program(compiler_context_t *ctx, c64script_ast_node_t *first_stmt);

// ============================================================================
// EXPRESSION COMPILATION
// ============================================================================

static bool compile_expression(compiler_context_t *ctx, c64script_ast_expr_t *expr)
{
    if (!expr)
        return false;

    switch (expr->type) {
    case AST_EXPR_NUMBER: {
        c64script_value_t value = {.type = VALUE_NUMBER, .as.number = expr->as.number};
        uint32_t idx = add_constant(ctx, value);
        if (idx == UINT32_MAX)
            return false;
        emit(ctx, OP_PUSH_CONST, idx, expr->line);
        return true;
    }

    case AST_EXPR_STRING: {
        c64script_value_t value = {.type = VALUE_STRING, .as.string = (char *)expr->as.string};
        uint32_t idx = add_constant(ctx, value);
        if (idx == UINT32_MAX)
            return false;
        emit(ctx, OP_PUSH_CONST, idx, expr->line);
        return true;
    }

    case AST_EXPR_IDENTIFIER: {
        // Push variable name as constant, then PUSH_VAR will load its value
        c64script_value_t name = {.type = VALUE_STRING, .as.string = (char *)expr->as.identifier};
        uint32_t idx = add_constant(ctx, name);
        if (idx == UINT32_MAX)
            return false;
        emit(ctx, OP_PUSH_VAR, idx, expr->line);
        return true;
    }

    case AST_EXPR_UNARY: {
        // Compile operand
        if (!compile_expression(ctx, expr->as.unary.operand))
            return false;
        // Emit unary operator
        if (expr->as.unary.op == EXPR_OP_NEGATE) {
            emit(ctx, OP_NEGATE, 0, expr->line);
        } else if (expr->as.unary.op == EXPR_OP_NOT) {
            emit(ctx, OP_NOT, 0, expr->line);
        }
        return true;
    }

    case AST_EXPR_BINARY: {
        // Compile left and right operands
        if (!compile_expression(ctx, expr->as.binary.left))
            return false;
        if (!compile_expression(ctx, expr->as.binary.right))
            return false;

        // Emit binary operator
        switch (expr->as.binary.op) {
        case EXPR_OP_ADD:
            emit(ctx, OP_ADD, 0, expr->line);
            break;
        case EXPR_OP_SUBTRACT:
            emit(ctx, OP_SUBTRACT, 0, expr->line);
            break;
        case EXPR_OP_MULTIPLY:
            emit(ctx, OP_MULTIPLY, 0, expr->line);
            break;
        case EXPR_OP_DIVIDE:
            emit(ctx, OP_DIVIDE, 0, expr->line);
            break;
        case EXPR_OP_EQ:
            emit(ctx, OP_EQ, 0, expr->line);
            break;
        case EXPR_OP_NE:
            emit(ctx, OP_NE, 0, expr->line);
            break;
        case EXPR_OP_LT:
            emit(ctx, OP_LT, 0, expr->line);
            break;
        case EXPR_OP_LE:
            emit(ctx, OP_LE, 0, expr->line);
            break;
        case EXPR_OP_GT:
            emit(ctx, OP_GT, 0, expr->line);
            break;
        case EXPR_OP_GE:
            emit(ctx, OP_GE, 0, expr->line);
            break;
        case EXPR_OP_AND:
            emit(ctx, OP_AND, 0, expr->line);
            break;
        case EXPR_OP_OR:
            emit(ctx, OP_OR, 0, expr->line);
            break;
        case EXPR_OP_XOR:
            emit(ctx, OP_XOR, 0, expr->line);
            break;
        default:
            return false;
        }
        return true;
    }

    case AST_EXPR_CALL: {
        if (!expr->as.call.name) {
            if (ctx->error_msg) {
                snprintf(ctx->error_msg, ctx->error_msg_size, "Invalid function call");
            }
            return false;
        }

        if (strcmp(expr->as.call.name, "PEEK") == 0) {
            if (expr->as.call.arg_count != 1) {
                if (ctx->error_msg) {
                    snprintf(ctx->error_msg, ctx->error_msg_size, "PEEK expects 1 argument");
                }
                return false;
            }
            if (!compile_expression(ctx, expr->as.call.args[0])) {
                return false;
            }
            emit(ctx, OP_CALL_PEEK, 0, expr->line);
            return true;
        }

        if (ctx->error_msg) {
            snprintf(ctx->error_msg, ctx->error_msg_size, "Undefined function: %s", expr->as.call.name);
        }
        return false;
    }

    default:
        return false;
    }
}

// ============================================================================
// STATEMENT COMPILATION
// ============================================================================

static bool compile_statement(compiler_context_t *ctx, c64script_ast_node_t *stmt)
{
    if (!stmt)
        return false;

    switch (stmt->type) {
    case AST_STMT_EMPTY:
    case AST_STMT_REM:
        // No code generation
        return true;

    case AST_STMT_LABEL:
        // Define label at current bytecode address
        return define_label(ctx, stmt->as.label.name, stmt->line);

    case AST_STMT_ASSIGNMENT: {
        // Compile RHS expression
        if (!compile_expression(ctx, stmt->as.assignment.value))
            return false;
        // Store result in variable
        c64script_value_t varname = {.type = VALUE_STRING, .as.string = (char *)stmt->as.assignment.variable};
        uint32_t idx = add_constant(ctx, varname);
        if (idx == UINT32_MAX)
            return false;
        emit(ctx, OP_POP_VAR, idx, stmt->line);
        return true;
    }

    case AST_STMT_GOTO:
        emit_jump(ctx, OP_JUMP, stmt->as.goto_stmt.label, stmt->line);
        return true;

    case AST_STMT_GOSUB:
        // Push parameter count first
        emit(ctx, OP_PUSH_NUM, 0, stmt->line);
        ctx->instructions[ctx->instruction_count - 1].operand = stmt->as.gosub_stmt.param_count;

        // Push each parameter value
        for (size_t i = 0; i < stmt->as.gosub_stmt.param_count; i++) {
            if (!compile_expression(ctx, stmt->as.gosub_stmt.params[i]))
                return false;
        }

        // Call the subroutine
        emit_jump(ctx, OP_CALL, stmt->as.gosub_stmt.label, stmt->line);
        return true;

    case AST_STMT_RETURN:
        // If there's a return value, evaluate and push it
        if (stmt->as.return_stmt.return_value) {
            if (!compile_expression(ctx, stmt->as.return_stmt.return_value))
                return false;
            emit(ctx, OP_RETURN, 1, stmt->line); // operand = 1 means has return value
        } else {
            emit(ctx, OP_RETURN, 0, stmt->line); // operand = 0 means no return value
        }
        return true;

    case AST_STMT_STOP:
        emit(ctx, OP_STOP, 0, stmt->line);
        return true;

    case AST_STMT_IF: {
        // Compile condition
        if (!compile_expression(ctx, stmt->as.if_stmt.condition))
            return false;

        // JUMP_IF_FALSE to else_branch or end
        size_t else_jump = ctx->instruction_count;
        emit(ctx, OP_JUMP_IF_FALSE, 0, stmt->line); // Placeholder

        // Compile then_branch
        if (stmt->as.if_stmt.then_branch) {
            for (c64script_ast_node_t *s = stmt->as.if_stmt.then_branch; s != NULL; s = s->next) {
                if (!compile_statement(ctx, s))
                    return false;
            }
        }

        if (stmt->as.if_stmt.else_branch) {
            // Jump over else branch at end of then
            size_t end_jump = ctx->instruction_count;
            emit(ctx, OP_JUMP, 0, stmt->line); // Placeholder

            // Patch else_jump to here
            ctx->instructions[else_jump].operand = (uint32_t)ctx->instruction_count;

            // Compile else_branch
            for (c64script_ast_node_t *s = stmt->as.if_stmt.else_branch; s != NULL; s = s->next) {
                if (!compile_statement(ctx, s))
                    return false;
            }

            // Patch end_jump to here
            ctx->instructions[end_jump].operand = (uint32_t)ctx->instruction_count;
        } else {
            // No else - patch else_jump to here
            ctx->instructions[else_jump].operand = (uint32_t)ctx->instruction_count;
        }
        return true;
    }

    case AST_STMT_FOR: {
        // Initialize loop variable
        if (!compile_expression(ctx, stmt->as.for_stmt.start))
            return false;
        c64script_value_t varname = {.type = VALUE_STRING, .as.string = (char *)stmt->as.for_stmt.variable};
        uint32_t var_idx = add_constant(ctx, varname);
        if (var_idx == UINT32_MAX)
            return false;
        emit(ctx, OP_POP_VAR, var_idx, stmt->line);

        // Push loop parameters (variable, end, step) onto FOR stack
        emit(ctx, OP_PUSH_VAR, var_idx, stmt->line); // current value
        if (!compile_expression(ctx, stmt->as.for_stmt.end))
            return false;
        if (stmt->as.for_stmt.step) {
            if (!compile_expression(ctx, stmt->as.for_stmt.step))
                return false;
        } else {
            // Default step = 1
            c64script_value_t step_one = {.type = VALUE_NUMBER, .as.number = 1.0};
            uint32_t idx = add_constant(ctx, step_one);
            if (idx == UINT32_MAX)
                return false;
            emit(ctx, OP_PUSH_CONST, idx, stmt->line);
        }
        emit(ctx, OP_FOR_INIT, var_idx, stmt->line); // Initialize FOR state

        // Loop start address
        size_t loop_start = ctx->instruction_count;

        // Check loop condition
        size_t check_jump = ctx->instruction_count;
        emit(ctx, OP_FOR_CHECK, 0, stmt->line); // Jumps to end if done

        // Compile loop body
        if (stmt->as.for_stmt.body) {
            for (c64script_ast_node_t *s = stmt->as.for_stmt.body; s != NULL; s = s->next) {
                if (!compile_statement(ctx, s))
                    return false;
            }
        }

        // Increment loop variable
        emit(ctx, OP_FOR_INCR, var_idx, stmt->line);

        // Jump back to loop start
        emit(ctx, OP_JUMP, (uint32_t)loop_start, stmt->line);

        // Patch check_jump to here (loop end)
        ctx->instructions[check_jump].operand = (uint32_t)ctx->instruction_count;
        return true;
    }

    case AST_STMT_WHILE: {
        // Loop start address
        size_t loop_start = ctx->instruction_count;

        // Compile condition
        if (!compile_expression(ctx, stmt->as.while_stmt.condition))
            return false;

        // WHILE_CHECK - pops condition, jumps to end if false
        size_t check_jump = ctx->instruction_count;
        emit(ctx, OP_WHILE_CHECK, 0, stmt->line); // Placeholder

        // Compile loop body
        if (stmt->as.while_stmt.body) {
            for (c64script_ast_node_t *s = stmt->as.while_stmt.body; s != NULL; s = s->next) {
                if (!compile_statement(ctx, s))
                    return false;
            }
        }

        // Jump back to loop start
        emit(ctx, OP_JUMP, (uint32_t)loop_start, stmt->line);

        // Patch check_jump to here (loop end)
        ctx->instructions[check_jump].operand = (uint32_t)ctx->instruction_count;
        return true;
    }

    case AST_STMT_WAIT: {
        if (!compile_expression(ctx, stmt->as.wait_stmt.duration))
            return false;
        emit(ctx, OP_WAIT, (uint32_t)stmt->as.wait_stmt.unit, stmt->line);
        return true;
    }

    case AST_STMT_WAIT_UNTIL: {
        if (!compile_expression(ctx, stmt->as.wait_until_stmt.time_expr))
            return false;
        emit(ctx, OP_WAIT_UNTIL, 0, stmt->line);
        return true;
    }

    // Plugin action statements
    case AST_STMT_EFFECT: {
        if (!compile_expression(ctx, stmt->as.effect_stmt.preset_name))
            return false;
        emit(ctx, OP_EFFECT, 0, stmt->line);
        return true;
    }

    case AST_STMT_EFFECTPARAM: {
        if (!compile_expression(ctx, stmt->as.effectparam_stmt.param_name))
            return false;
        if (!compile_expression(ctx, stmt->as.effectparam_stmt.param_value))
            return false;
        emit(ctx, OP_EFFECTPARAM, 0, stmt->line);
        return true;
    }

    case AST_STMT_PALETTE: {
        if (!compile_expression(ctx, stmt->as.palette_stmt.palette_name))
            return false;
        emit(ctx, OP_PALETTE, 0, stmt->line);
        return true;
    }

    case AST_STMT_PALETTECOLOR: {
        if (!compile_expression(ctx, stmt->as.palettecolor_stmt.index))
            return false;
        if (!compile_expression(ctx, stmt->as.palettecolor_stmt.r))
            return false;
        if (!compile_expression(ctx, stmt->as.palettecolor_stmt.g))
            return false;
        if (!compile_expression(ctx, stmt->as.palettecolor_stmt.b))
            return false;
        emit(ctx, OP_PALETTECOLOR, 0, stmt->line);
        return true;
    }

    case AST_STMT_PLAYSID: {
        if (!compile_expression(ctx, stmt->as.playsid_stmt.path))
            return false;
        if (stmt->as.playsid_stmt.songnr) {
            if (!compile_expression(ctx, stmt->as.playsid_stmt.songnr))
                return false;
        } else {
            // Default song = 0
            c64script_value_t song_zero = {.type = VALUE_NUMBER, .as.number = 0.0};
            uint32_t idx = add_constant(ctx, song_zero);
            if (idx == UINT32_MAX)
                return false;
            emit(ctx, OP_PUSH_CONST, idx, stmt->line);
        }
        emit(ctx, OP_PLAYSID, 0, stmt->line);
        return true;
    }

    case AST_STMT_RUNPRG: {
        if (!compile_expression(ctx, stmt->as.runprg_stmt.path))
            return false;
        emit(ctx, OP_RUNPRG, 0, stmt->line);
        return true;
    }

    case AST_STMT_RUNLOCAL: {
        // Stack order: path, args (or NULL string), status_var (or NULL string), output_var (or NULL string)
        if (!compile_expression(ctx, stmt->as.runlocal_stmt.path))
            return false;

        if (stmt->as.runlocal_stmt.args) {
            if (!compile_expression(ctx, stmt->as.runlocal_stmt.args))
                return false;
        } else {
            // Push empty string for no args
            c64script_value_t empty_str = {.type = VALUE_STRING, .as.string = ""};
            uint32_t idx = add_constant(ctx, empty_str);
            if (idx == UINT32_MAX)
                return false;
            emit(ctx, OP_PUSH_CONST, idx, stmt->line);
        }

        if (stmt->as.runlocal_stmt.status_var) {
            if (!compile_expression(ctx, stmt->as.runlocal_stmt.status_var))
                return false;
        } else {
            // Push empty string for no status var
            c64script_value_t empty_str = {.type = VALUE_STRING, .as.string = ""};
            uint32_t idx = add_constant(ctx, empty_str);
            if (idx == UINT32_MAX)
                return false;
            emit(ctx, OP_PUSH_CONST, idx, stmt->line);
        }

        if (stmt->as.runlocal_stmt.output_var) {
            if (!compile_expression(ctx, stmt->as.runlocal_stmt.output_var))
                return false;
        } else {
            // Push empty string for no output var
            c64script_value_t empty_str = {.type = VALUE_STRING, .as.string = ""};
            uint32_t idx = add_constant(ctx, empty_str);
            if (idx == UINT32_MAX)
                return false;
            emit(ctx, OP_PUSH_CONST, idx, stmt->line);
        }

        emit(ctx, OP_RUNLOCAL, 0, stmt->line);
        return true;
    }

    case AST_STMT_MOUNTDISK: {
        if (!compile_expression(ctx, stmt->as.mountdisk_stmt.path))
            return false;
        emit(ctx, OP_MOUNTDISK, 0, stmt->line);
        return true;
    }

    case AST_STMT_AUTOSTART:
        emit(ctx, OP_AUTOSTART, 0, stmt->line);
        return true;

    case AST_STMT_RESET:
        emit(ctx, OP_RESET, 0, stmt->line);
        return true;

    case AST_STMT_REBOOT:
        emit(ctx, OP_REBOOT, 0, stmt->line);
        return true;

    case AST_STMT_RECORDSTART:
        emit(ctx, OP_RECORDSTART, 0, stmt->line);
        return true;

    case AST_STMT_RECORDSTOP:
        emit(ctx, OP_RECORDSTOP, 0, stmt->line);
        return true;

    case AST_STMT_TYPE: {
        if (!compile_expression(ctx, stmt->as.type_stmt.text))
            return false;
        emit(ctx, OP_TYPE, 0, stmt->line);
        return true;
    }

    case AST_STMT_KEY: {
        if (!compile_expression(ctx, stmt->as.key_stmt.key))
            return false;
        emit(ctx, OP_KEY, 0, stmt->line);
        return true;
    }

    case AST_STMT_POKE: {
        if (!compile_expression(ctx, stmt->as.poke_stmt.address))
            return false;
        if (stmt->as.poke_stmt.value_count > 0) {
            // Multi-value POKE
            for (size_t i = 0; i < stmt->as.poke_stmt.value_count; i++) {
                if (!compile_expression(ctx, stmt->as.poke_stmt.values[i]))
                    return false;
            }
            emit(ctx, OP_POKE_ARRAY, (uint32_t)stmt->as.poke_stmt.value_count, stmt->line);
        } else {
            // Single-value POKE
            if (!compile_expression(ctx, stmt->as.poke_stmt.single_value))
                return false;
            emit(ctx, OP_POKE_SINGLE, 0, stmt->line);
        }
        return true;
    }

    case AST_STMT_LOGFILE: {
        if (!compile_expression(ctx, stmt->as.logfile_stmt.path))
            return false;
        emit(ctx, OP_LOGFILE, stmt->as.logfile_stmt.truncate ? 1 : 0, stmt->line);
        return true;
    }

    case AST_STMT_LOG: {
        if (!compile_expression(ctx, stmt->as.log_stmt.message))
            return false;
        emit(ctx, OP_LOG, 0, stmt->line);
        return true;
    }

    case AST_STMT_PRINT: {
        if (!compile_expression(ctx, stmt->as.print_stmt.message))
            return false;
        emit(ctx, OP_PRINT, 0, stmt->line);
        return true;
    }

    case AST_STMT_TRON:
        emit(ctx, OP_TRON, 0, stmt->line);
        return true;

    case AST_STMT_TROFF:
        emit(ctx, OP_TROFF, 0, stmt->line);
        return true;

    case AST_STMT_READFILE: {
        // READFILE needs: variable name (as identifier), path
        if (!compile_expression(ctx, stmt->as.readfile_stmt.variable))
            return false;
        if (!compile_expression(ctx, stmt->as.readfile_stmt.path))
            return false;
        emit(ctx, OP_READFILE, 0, stmt->line);
        return true;
    }

    case AST_STMT_WRITEFILE: {
    }

    case AST_STMT_HTTP: {
        // HTTP needs: response_var, status_var, body, headers, url (strings, empty for NULL)
        // Then method as operand
        c64script_value_t empty_val = {.type = VALUE_STRING, .as.string = (char *)""};
        uint32_t empty_idx = (uint32_t)add_constant(ctx, empty_val);

        // Push response_var name (or empty)
        if (stmt->as.http_stmt.response_var) {
            if (!compile_expression(ctx, stmt->as.http_stmt.response_var))
                return false;
        } else {
            emit(ctx, OP_PUSH_CONST, empty_idx, stmt->line);
        }

        // Push status_var name (or empty)
        if (stmt->as.http_stmt.status_var) {
            if (!compile_expression(ctx, stmt->as.http_stmt.status_var))
                return false;
        } else {
            emit(ctx, OP_PUSH_CONST, empty_idx, stmt->line);
        }

        // Push body (or empty)
        if (stmt->as.http_stmt.body) {
            if (!compile_expression(ctx, stmt->as.http_stmt.body))
                return false;
        } else {
            emit(ctx, OP_PUSH_CONST, empty_idx, stmt->line);
        }

        // Push headers (or empty)
        if (stmt->as.http_stmt.headers) {
            if (!compile_expression(ctx, stmt->as.http_stmt.headers))
                return false;
        } else {
            emit(ctx, OP_PUSH_CONST, empty_idx, stmt->line);
        }

        // Push URL
        if (!compile_expression(ctx, stmt->as.http_stmt.url))
            return false;

        // Emit OP_HTTP with method as operand (0=GET, 1=POST, 2=PUT, 3=DELETE, 4=PATCH)
        emit(ctx, OP_HTTP, (uint32_t)stmt->as.http_stmt.method, stmt->line);
        return true;
    }
    default:
        blog(LOG_ERROR, "Unknown statement type: %d", stmt->type);
        return false;
    }
}

static bool compile_program(compiler_context_t *ctx, c64script_ast_node_t *first_stmt)
{
    if (!first_stmt)
        return false;

    // Compile each statement in the linked list
    for (c64script_ast_node_t *stmt = first_stmt; stmt != NULL; stmt = stmt->next) {
        if (!compile_statement(ctx, stmt))
            return false;
    }

    // Emit HALT at end of program
    emit(ctx, OP_HALT, 0, -1);

    return true;
}

// ============================================================================
// PUBLIC API
// ============================================================================

bool c64script_compile(c64script_ast_node_t *ast, c64script_runtime_t *runtime, char *error_msg, size_t error_msg_size)
{
    if (!ast || !runtime) {
        if (error_msg && error_msg_size > 0) {
            snprintf(error_msg, error_msg_size, "NULL AST or runtime provided");
        }
        return false;
    }

    // Initialize compiler context
    compiler_context_t ctx = {0};
    ctx.error_msg = error_msg;
    ctx.error_msg_size = error_msg_size;

    // Compile AST to bytecode
    if (!compile_program(&ctx, ast)) {
        compiler_context_destroy(&ctx);
        return false;
    }

    // Patch forward jumps
    if (!patch_jumps(&ctx)) {
        compiler_context_destroy(&ctx);
        return false;
    }

    // Store bytecode in runtime
    runtime->bytecode = ctx.instructions;
    runtime->bytecode_size = ctx.instruction_count;
    runtime->constants = ctx.constants;
    runtime->constant_count = ctx.constant_count;
    runtime->ip = 0;

    return true;
}
