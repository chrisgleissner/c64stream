/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script.h"
#include "c64-logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MACRO_LOG_PREFIX "[c64script-ast] "

// Forward declaration for recursive freeing
static void free_expr(c64script_ast_expr_t *expr);

// ============================================================================
// AST Functions
// ============================================================================

static void free_expr(c64script_ast_expr_t *expr)
{
    if (!expr) {
        return;
    }

    switch (expr->type) {
    case AST_EXPR_NUMBER:
        break;

    case AST_EXPR_STRING:
        free((char *)expr->as.string);
        break;

    case AST_EXPR_IDENTIFIER:
        free((char *)expr->as.identifier);
        break;

    case AST_EXPR_UNARY:
        free_expr(expr->as.unary.operand);
        break;

    case AST_EXPR_BINARY:
        free_expr(expr->as.binary.left);
        free_expr(expr->as.binary.right);
        break;

    case AST_EXPR_CALL:
        free((char *)expr->as.call.name);
        for (size_t i = 0; i < expr->as.call.arg_count; i++) {
            free_expr(expr->as.call.args[i]);
        }
        if (expr->as.call.args) {
            free(expr->as.call.args);
        }
        break;

    default:
        break;
    }

    free(expr);
}

void c64script_ast_free(c64script_ast_node_t *node)
{
    while (node) {
        c64script_ast_node_t *next = node->next;

        // Free node-specific data based on type
        switch (node->type) {
        case AST_STMT_EMPTY:
        case AST_STMT_REM:
        case AST_STMT_STOP:
        case AST_STMT_TRON:
        case AST_STMT_TROFF:
        case AST_STMT_RESET:
        case AST_STMT_REBOOT:
        case AST_STMT_RECORDSTART:
        case AST_STMT_RECORDSTOP:
        case AST_STMT_AUTOSTART:
            // No sub-structures to free
            break;

        case AST_STMT_LABEL:
            free((char *)node->as.label.name);
            break;

        case AST_STMT_ASSIGNMENT:
            free((char *)node->as.assignment.variable);
            free_expr(node->as.assignment.value);
            break;

        case AST_STMT_IF:
            free_expr(node->as.if_stmt.condition);
            c64script_ast_free(node->as.if_stmt.then_branch);
            c64script_ast_free(node->as.if_stmt.else_branch);
            break;

        case AST_STMT_FOR:
            free((char *)node->as.for_stmt.variable);
            free_expr(node->as.for_stmt.start);
            free_expr(node->as.for_stmt.end);
            free_expr(node->as.for_stmt.step);
            c64script_ast_free(node->as.for_stmt.body);
            break;

        case AST_STMT_WHILE:
            free_expr(node->as.while_stmt.condition);
            c64script_ast_free(node->as.while_stmt.body);
            break;

        case AST_STMT_GOTO:
            free((char *)node->as.goto_stmt.label);
            break;

        case AST_STMT_GOSUB:
            free((char *)node->as.gosub_stmt.label);
            for (size_t i = 0; i < node->as.gosub_stmt.param_count; i++) {
                free_expr(node->as.gosub_stmt.params[i]);
            }
            if (node->as.gosub_stmt.params) {
                free(node->as.gosub_stmt.params);
            }
            break;

        case AST_STMT_RETURN:
            free_expr(node->as.return_stmt.return_value);
            break;

        case AST_STMT_WAIT:
            free_expr(node->as.wait_stmt.duration);
            break;

        case AST_STMT_WAIT_UNTIL:
            free_expr(node->as.wait_until_stmt.time_expr);
            break;

        case AST_STMT_EFFECT:
            free_expr(node->as.effect_stmt.preset_name);
            break;

        case AST_STMT_EFFECTPARAM:
            free_expr(node->as.effectparam_stmt.param_name);
            free_expr(node->as.effectparam_stmt.param_value);
            break;

        case AST_STMT_PALETTE:
            free_expr(node->as.palette_stmt.palette_name);
            break;

        case AST_STMT_PALETTECOLOR:
            free_expr(node->as.palettecolor_stmt.index);
            free_expr(node->as.palettecolor_stmt.r);
            free_expr(node->as.palettecolor_stmt.g);
            free_expr(node->as.palettecolor_stmt.b);
            break;

        case AST_STMT_PLAYSID:
            free_expr(node->as.playsid_stmt.path);
            free_expr(node->as.playsid_stmt.songnr);
            break;

        case AST_STMT_RUNPRG:
            free_expr(node->as.runprg_stmt.path);
            break;

        case AST_STMT_MOUNTDISK:
            free_expr(node->as.mountdisk_stmt.path);
            break;

        case AST_STMT_TYPE:
            free_expr(node->as.type_stmt.text);
            break;

        case AST_STMT_KEY:
            free_expr(node->as.key_stmt.key);
            break;

        case AST_STMT_POKE:
            free_expr(node->as.poke_stmt.address);
            free_expr(node->as.poke_stmt.single_value);
            for (size_t i = 0; i < node->as.poke_stmt.value_count; i++) {
                free_expr(node->as.poke_stmt.values[i]);
            }
            if (node->as.poke_stmt.values) {
                free(node->as.poke_stmt.values);
            }
            break;

        case AST_STMT_LOGFILE:
            free_expr(node->as.logfile_stmt.path);
            break;

        case AST_STMT_LOG:
            free_expr(node->as.log_stmt.message);
            break;

        case AST_STMT_PRINT:
            free_expr(node->as.print_stmt.message);
            break;

        case AST_STMT_READFILE:
            free_expr(node->as.readfile_stmt.variable);
            free_expr(node->as.readfile_stmt.path);
            break;

        case AST_STMT_WRITEFILE:
            free_expr(node->as.writefile_stmt.path);
            free_expr(node->as.writefile_stmt.content);
            break;

        default:
            // Unknown node type - log warning but continue
            blog(LOG_WARNING, "c64script_ast_free: Unknown AST node type %d", node->type);
            break;
        }

        // Free node itself
        free(node);

        // Move to next node in linked list
        node = next;
    }
}
