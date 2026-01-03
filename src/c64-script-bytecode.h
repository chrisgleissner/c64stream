/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#pragma once

#include "c64-script.h"

/**
 * Bytecode compiler
 *
 * Compiles AST to bytecode for efficient execution.
 */

typedef struct {
    c64script_instruction_t *bytecode;
    size_t bytecode_size;
    size_t bytecode_capacity;

    c64script_value_t *constants;
    size_t constant_count;
    size_t constant_capacity;

    // Label resolution (name -> bytecode address)
    struct {
        char name[64];
        size_t address;
    } labels[C64SCRIPT_MAX_LABELS];
    size_t label_count;

    // Forward jumps to patch
    struct {
        size_t instruction_index; // Where to patch
        char label[64];           // Target label
    } unresolved_jumps[512];
    size_t unresolved_jump_count;

    char error[1024];
} c64script_compiler_t;

/**
 * Initialize compiler
 */
void c64script_compiler_init(c64script_compiler_t *compiler);

/**
 * Compile AST to bytecode
 */
bool c64script_compiler_compile(c64script_compiler_t *compiler, c64script_ast_node_t *ast);

/**
 * Extract compiled bytecode and constants
 */
bool c64script_compiler_finalize(c64script_compiler_t *compiler, c64script_runtime_t *runtime);

/**
 * Clean up compiler
 */
void c64script_compiler_destroy(c64script_compiler_t *compiler);
