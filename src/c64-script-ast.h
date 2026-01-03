/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#pragma once

#include "c64-script.h"

/**
 * AST node creation and management
 */

/**
 * Create expression nodes
 */
c64script_ast_expr_t *c64script_expr_number(double value);
c64script_ast_expr_t *c64script_expr_string(const char *value);
c64script_ast_expr_t *c64script_expr_identifier(const char *name);
c64script_ast_expr_t *c64script_expr_unary(c64script_operator_t op, c64script_ast_expr_t *operand);
c64script_ast_expr_t *c64script_expr_binary(c64script_operator_t op, c64script_ast_expr_t *left,
                                            c64script_ast_expr_t *right);
c64script_ast_expr_t *c64script_expr_call(const char *name, c64script_ast_expr_t **args, size_t arg_count);

/**
 * Create statement nodes
 */
c64script_ast_node_t *c64script_stmt_label(const char *name);
c64script_ast_node_t *c64script_stmt_assignment(const char *variable, c64script_ast_expr_t *value);
c64script_ast_node_t *c64script_stmt_if(c64script_ast_expr_t *condition, c64script_ast_node_t *then_branch,
                                        c64script_ast_node_t *else_branch);
c64script_ast_node_t *c64script_stmt_for(const char *variable, c64script_ast_expr_t *start, c64script_ast_expr_t *end,
                                         c64script_ast_expr_t *step, c64script_ast_node_t *body);
c64script_ast_node_t *c64script_stmt_while(c64script_ast_expr_t *condition, c64script_ast_node_t *body);
c64script_ast_node_t *c64script_stmt_goto(const char *label);
c64script_ast_node_t *c64script_stmt_gosub(const char *label);
c64script_ast_node_t *c64script_stmt_return(void);
c64script_ast_node_t *c64script_stmt_stop(void);

/**
 * Free AST nodes
 */
void c64script_expr_free(c64script_ast_expr_t *expr);
void c64script_stmt_free(c64script_ast_node_t *stmt);
