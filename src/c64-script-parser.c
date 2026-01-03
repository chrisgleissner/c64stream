/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#include "c64-script.h"
#include "c64-script-token.h"
#include "c64-logging.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MACRO_LOG_PREFIX "[c64script-parser] "

// Parser state
typedef struct {
    c64script_tokenizer_t *tokenizer;
    c64script_token_t current;
    c64script_token_t previous;
    bool had_error;
    bool panic_mode;
    char error_msg[1024];
} parser_t;

// Forward declarations
static c64script_ast_expr_t *expression(parser_t *p);
static c64script_ast_node_t *statement(parser_t *p);
static c64script_ast_node_t *declaration(parser_t *p);

// ============================================================================
// ERROR HANDLING
// ============================================================================

static void error_at(parser_t *p, c64script_token_t *token, const char *message)
{
    if (p->panic_mode)
        return;
    p->panic_mode = true;
    p->had_error = true;

    snprintf(p->error_msg, sizeof(p->error_msg), "[Line %d:%d] Error at '%.*s': %s", token->line, token->column,
             (int)token->length, token->start, message);
    blog(LOG_ERROR, "%s", p->error_msg);
}

static void error(parser_t *p, const char *message)
{
    error_at(p, &p->previous, message);
}

static void error_at_current(parser_t *p, const char *message)
{
    error_at(p, &p->current, message);
}

// ============================================================================
// TOKEN MANAGEMENT
// ============================================================================

static void advance(parser_t *p)
{
    p->previous = p->current;

    for (;;) {
        p->current = c64script_tokenizer_next(p->tokenizer);
        if (p->current.type != TOKEN_ERROR)
            break;

        error_at_current(p, p->current.start);
    }
}

static bool check(parser_t *p, c64script_token_type_t type)
{
    return p->current.type == type;
}

static bool match(parser_t *p, c64script_token_type_t type)
{
    if (!check(p, type))
        return false;
    advance(p);
    return true;
}

static void consume(parser_t *p, c64script_token_type_t type, const char *message)
{
    if (p->current.type == type) {
        advance(p);
        return;
    }
    error_at_current(p, message);
}

static void synchronize(parser_t *p)
{
    p->panic_mode = false;

    while (p->current.type != TOKEN_EOF) {
        if (p->previous.type == TOKEN_NEWLINE)
            return;

        switch (p->current.type) {
        case TOKEN_IF:
        case TOKEN_FOR:
        case TOKEN_WHILE:
        case TOKEN_GOTO:
        case TOKEN_GOSUB:
        case TOKEN_RETURN:
        case TOKEN_LET:
        case TOKEN_STOP:
        case TOKEN_END:
            return;
        default:; // Do nothing
        }

        advance(p);
    }
}

// ============================================================================
// EXPRESSION PARSING (Pratt parser with proper precedence)
// ============================================================================

typedef enum {
    PREC_NONE,
    PREC_OR,         // OR
    PREC_XOR,        // XOR
    PREC_AND,        // AND
    PREC_EQUALITY,   // = == <> !=
    PREC_COMPARISON, // < <= > >=
    PREC_TERM,       // + -
    PREC_FACTOR,     // * /
    PREC_UNARY,      // NOT - +
    PREC_CALL,       // ()
    PREC_PRIMARY
} precedence_t;

typedef c64script_ast_expr_t *(*parse_fn_t)(parser_t *p, bool can_assign);

typedef struct {
    parse_fn_t prefix;
    parse_fn_t infix;
    precedence_t precedence;
} parse_rule_t;

static c64script_ast_expr_t *parse_precedence(parser_t *p, precedence_t precedence);
static parse_rule_t *get_rule(c64script_token_type_t type);

// Number literal
static c64script_ast_expr_t *number(parser_t *p, bool can_assign)
{
    (void)can_assign;
    c64script_ast_expr_t *expr = calloc(1, sizeof(c64script_ast_expr_t));
    if (!expr)
        return NULL;
    expr->type = AST_EXPR_NUMBER;
    expr->line = p->previous.line;
    expr->as.number = p->previous.value.number;
    return expr;
}

// String literal
static c64script_ast_expr_t *string(parser_t *p, bool can_assign)
{
    (void)can_assign;
    c64script_ast_expr_t *expr = calloc(1, sizeof(c64script_ast_expr_t));
    if (!expr)
        return NULL;
    expr->type = AST_EXPR_STRING;
    expr->line = p->previous.line;
    // Store string from token (skip quotes)
    size_t len = p->previous.length > 2 ? p->previous.length - 2 : 0;
    char *str = malloc(len + 1);
    if (str) {
        memcpy(str, p->previous.start + 1, len);
        str[len] = '\0';
    }
    expr->as.string = str;
    return expr;
}

// Identifier or function call
static c64script_ast_expr_t *variable(parser_t *p, bool can_assign)
{
    (void)can_assign;
    c64script_ast_expr_t *expr = calloc(1, sizeof(c64script_ast_expr_t));
    if (!expr)
        return NULL;
    expr->type = AST_EXPR_IDENTIFIER;
    expr->line = p->previous.line;
    char *name = malloc(p->previous.length + 1);
    if (name) {
        memcpy(name, p->previous.start, p->previous.length);
        name[p->previous.length] = '\0';
    }
    expr->as.identifier = name;
    return expr;
}

// Grouping: (expression)
static c64script_ast_expr_t *grouping(parser_t *p, bool can_assign)
{
    (void)can_assign;
    c64script_ast_expr_t *expr = expression(p);
    consume(p, TOKEN_RPAREN, "Expected ')' after expression");
    return expr;
}

// Unary: - NOT
static c64script_ast_expr_t *unary(parser_t *p, bool can_assign)
{
    (void)can_assign;
    c64script_token_type_t op_type = p->previous.type;
    c64script_ast_expr_t *expr = calloc(1, sizeof(c64script_ast_expr_t));
    if (!expr)
        return NULL;
    expr->type = AST_EXPR_UNARY;
    expr->line = p->previous.line;

    // Parse operand
    expr->as.unary.operand = parse_precedence(p, PREC_UNARY);

    // Determine operator
    switch (op_type) {
    case TOKEN_MINUS:
        expr->as.unary.op = EXPR_OP_NEGATE;
        break;
    case TOKEN_NOT:
        expr->as.unary.op = EXPR_OP_NOT;
        break;
    default:
        error(p, "Invalid unary operator");
        break;
    }

    return expr;
}

// Binary operators
static c64script_ast_expr_t *binary(parser_t *p, bool can_assign)
{
    (void)can_assign;
    c64script_token_type_t op_type = p->previous.type;

    // Get the rule for this operator to determine precedence
    parse_rule_t *rule = get_rule(op_type);

    // Compile right operand with higher precedence
    c64script_ast_expr_t *left_temp = NULL; // Will be set from context
    c64script_ast_expr_t *right = parse_precedence(p, (precedence_t)(rule->precedence + 1));

    c64script_ast_expr_t *expr = calloc(1, sizeof(c64script_ast_expr_t));
    if (!expr)
        return NULL;
    expr->type = AST_EXPR_BINARY;
    expr->line = p->previous.line;
    expr->as.binary.left = left_temp; // Need to track this properly
    expr->as.binary.right = right;

    // Map token to operator
    switch (op_type) {
    case TOKEN_PLUS:
        expr->as.binary.op = EXPR_OP_ADD;
        break;
    case TOKEN_MINUS:
        expr->as.binary.op = EXPR_OP_SUBTRACT;
        break;
    case TOKEN_MULTIPLY:
        expr->as.binary.op = EXPR_OP_MULTIPLY;
        break;
    case TOKEN_DIVIDE:
        expr->as.binary.op = EXPR_OP_DIVIDE;
        break;
    case TOKEN_EQ:
    case TOKEN_EQ_EQ:
        expr->as.binary.op = EXPR_OP_EQ;
        break;
    case TOKEN_NE:
    case TOKEN_NE_ALT:
        expr->as.binary.op = EXPR_OP_NE;
        break;
    case TOKEN_LT:
        expr->as.binary.op = EXPR_OP_LT;
        break;
    case TOKEN_LE:
        expr->as.binary.op = EXPR_OP_LE;
        break;
    case TOKEN_GT:
        expr->as.binary.op = EXPR_OP_GT;
        break;
    case TOKEN_GE:
        expr->as.binary.op = EXPR_OP_GE;
        break;
    case TOKEN_AND:
        expr->as.binary.op = EXPR_OP_AND;
        break;
    case TOKEN_XOR:
        expr->as.binary.op = EXPR_OP_XOR;
        break;
    case TOKEN_OR:
        expr->as.binary.op = EXPR_OP_OR;
        break;
    default:
        error(p, "Unknown operator");
        break;
    }

    return expr;
}

// Function call
static c64script_ast_expr_t *call(parser_t *p, bool can_assign)
{
    (void)can_assign;
    // Previous expression was the function name
    c64script_ast_expr_t *expr = calloc(1, sizeof(c64script_ast_expr_t));
    if (!expr)
        return NULL;
    expr->type = AST_EXPR_CALL;
    expr->line = p->previous.line;

    // Build argument list
    size_t arg_capacity = 4;
    size_t arg_count = 0;
    c64script_ast_expr_t **args = malloc(arg_capacity * sizeof(c64script_ast_expr_t *));

    if (!check(p, TOKEN_RPAREN)) {
        do {
            if (arg_count >= arg_capacity) {
                arg_capacity *= 2;
                args = realloc(args, arg_capacity * sizeof(c64script_ast_expr_t *));
            }
            args[arg_count++] = expression(p);
        } while (match(p, TOKEN_COMMA));
    }

    consume(p, TOKEN_RPAREN, "Expected ')' after arguments");

    expr->as.call.args = args;
    expr->as.call.arg_count = arg_count;

    return expr;
}

// Parse rule table
static parse_rule_t rules[] = {
    [TOKEN_LPAREN] = {grouping, call, PREC_CALL},
    [TOKEN_RPAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_MULTIPLY] = {NULL, binary, PREC_FACTOR},
    [TOKEN_DIVIDE] = {NULL, binary, PREC_FACTOR},
    [TOKEN_NOT] = {unary, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, binary, PREC_AND},
    [TOKEN_OR] = {NULL, binary, PREC_OR},
    [TOKEN_XOR] = {NULL, binary, PREC_XOR},
    [TOKEN_EQ] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_EQ_EQ] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_NE] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_NE_ALT] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_LT] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LE] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GT] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GE] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_HEX_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_DURATION] = {number, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
};

static parse_rule_t *get_rule(c64script_token_type_t type)
{
    if (type >= 0 && type < sizeof(rules) / sizeof(rules[0])) {
        return &rules[type];
    }
    return &rules[TOKEN_ERROR];
}

static c64script_ast_expr_t *parse_precedence(parser_t *p, precedence_t precedence)
{
    advance(p);
    parse_fn_t prefix_rule = get_rule(p->previous.type)->prefix;
    if (prefix_rule == NULL) {
        error(p, "Expected expression");
        return NULL;
    }

    bool can_assign = precedence <= PREC_OR;
    c64script_ast_expr_t *left = prefix_rule(p, can_assign);

    while (precedence <= get_rule(p->current.type)->precedence) {
        advance(p);
        parse_fn_t infix_rule = get_rule(p->previous.type)->infix;

        // For binary operators, we need to properly thread the left expression
        if (infix_rule == binary) {
            c64script_token_type_t op = p->previous.type;
            parse_rule_t *rule = get_rule(op);
            c64script_ast_expr_t *right = parse_precedence(p, (precedence_t)(rule->precedence + 1));

            c64script_ast_expr_t *bin_expr = calloc(1, sizeof(c64script_ast_expr_t));
            bin_expr->type = AST_EXPR_BINARY;
            bin_expr->line = p->previous.line;
            bin_expr->as.binary.left = left;
            bin_expr->as.binary.right = right;

            // Map operator
            switch (op) {
            case TOKEN_PLUS:
                bin_expr->as.binary.op = EXPR_OP_ADD;
                break;
            case TOKEN_MINUS:
                bin_expr->as.binary.op = EXPR_OP_SUBTRACT;
                break;
            case TOKEN_MULTIPLY:
                bin_expr->as.binary.op = EXPR_OP_MULTIPLY;
                break;
            case TOKEN_DIVIDE:
                bin_expr->as.binary.op = EXPR_OP_DIVIDE;
                break;
            case TOKEN_EQ:
            case TOKEN_EQ_EQ:
                bin_expr->as.binary.op = EXPR_OP_EQ;
                break;
            case TOKEN_NE:
            case TOKEN_NE_ALT:
                bin_expr->as.binary.op = EXPR_OP_NE;
                break;
            case TOKEN_LT:
                bin_expr->as.binary.op = EXPR_OP_LT;
                break;
            case TOKEN_LE:
                bin_expr->as.binary.op = EXPR_OP_LE;
                break;
            case TOKEN_GT:
                bin_expr->as.binary.op = EXPR_OP_GT;
                break;
            case TOKEN_GE:
                bin_expr->as.binary.op = EXPR_OP_GE;
                break;
            case TOKEN_AND:
                bin_expr->as.binary.op = EXPR_OP_AND;
                break;
            case TOKEN_XOR:
                bin_expr->as.binary.op = EXPR_OP_XOR;
                break;
            case TOKEN_OR:
                bin_expr->as.binary.op = EXPR_OP_OR;
                break;
            default:
                break;
            }

            left = bin_expr;
        } else if (infix_rule) {
            left = infix_rule(p, can_assign);
        }
    }

    return left;
}

static c64script_ast_expr_t *expression(parser_t *p)
{
    return parse_precedence(p, PREC_OR);
}

// ============================================================================
// STATEMENT PARSING
// ============================================================================

// Assignment: [LET] variable = expression
static c64script_ast_node_t *assignment_statement(parser_t *p, const char *var_name)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_ASSIGNMENT;
    node->line = p->previous.line;
    node->as.assignment.variable = strdup(var_name);

    consume(p, TOKEN_EQ, "Expected '=' in assignment");
    node->as.assignment.value = expression(p);

    return node;
}

// Label: LABEL name or name:
static c64script_ast_node_t *label_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_LABEL;
    node->line = p->previous.line;

    if (match(p, TOKEN_IDENTIFIER)) {
        char *name = malloc(p->previous.length + 1);
        if (name) {
            memcpy(name, p->previous.start, p->previous.length);
            name[p->previous.length] = '\0';
        }
        node->as.label.name = name;
        match(p, TOKEN_COLON); // Optional colon
    } else {
        error(p, "Expected label name");
    }

    return node;
}

// GOTO label
static c64script_ast_node_t *goto_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_GOTO;
    node->line = p->previous.line;

    if (match(p, TOKEN_IDENTIFIER)) {
        char *label = malloc(p->previous.length + 1);
        if (label) {
            memcpy(label, p->previous.start, p->previous.length);
            label[p->previous.length] = '\0';
        }
        node->as.goto_stmt.label = label;
    } else {
        error(p, "Expected label after GOTO");
    }

    return node;
}

// GOSUB label
static c64script_ast_node_t *gosub_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_GOSUB;
    node->line = p->previous.line;

    if (match(p, TOKEN_IDENTIFIER)) {
        char *label = malloc(p->previous.length + 1);
        if (label) {
            memcpy(label, p->previous.start, p->previous.length);
            label[p->previous.length] = '\0';
        }
        node->as.gosub_stmt.label = label;
    } else {
        error(p, "Expected label after GOSUB");
    }

    return node;
}

// RETURN
static c64script_ast_node_t *return_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_RETURN;
    node->line = p->previous.line;
    return node;
}

// STOP or END
static c64script_ast_node_t *stop_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_STOP;
    node->line = p->previous.line;
    return node;
}

// IF/THEN/ELSE/ENDIF
static c64script_ast_node_t *if_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_IF;
    node->line = p->previous.line;

    // Parse condition
    node->as.if_stmt.condition = expression(p);

    // Require THEN
    if (!match(p, TOKEN_THEN)) {
        error(p, "Expected THEN after IF condition");
        free(node);
        return NULL;
    }

    // Check if this is single-line or block form
    bool is_block = match(p, TOKEN_NEWLINE);

    if (is_block) {
        // Block form: IF cond THEN\n statements ENDIF or IF cond THEN\n statements ELSE\n statements ENDIF
        c64script_ast_node_t *then_branch = NULL;
        c64script_ast_node_t *then_tail = NULL;

        // Parse THEN branch statements until ELSE or ENDIF
        while (!check(p, TOKEN_ELSE) && !check(p, TOKEN_ENDIF) && !check(p, TOKEN_EOF)) {
            c64script_ast_node_t *stmt = statement(p);
            if (stmt) {
                if (!then_branch) {
                    then_branch = stmt;
                    then_tail = stmt;
                } else {
                    then_tail->next = stmt;
                    then_tail = stmt;
                }
            }
            while (match(p, TOKEN_NEWLINE))
                ;
        }
        node->as.if_stmt.then_branch = then_branch;

        // Check for ELSE
        if (match(p, TOKEN_ELSE)) {
            match(p, TOKEN_NEWLINE);

            c64script_ast_node_t *else_branch = NULL;
            c64script_ast_node_t *else_tail = NULL;

            // Parse ELSE branch statements until ENDIF
            while (!check(p, TOKEN_ENDIF) && !check(p, TOKEN_EOF)) {
                c64script_ast_node_t *stmt = statement(p);
                if (stmt) {
                    if (!else_branch) {
                        else_branch = stmt;
                        else_tail = stmt;
                    } else {
                        else_tail->next = stmt;
                        else_tail = stmt;
                    }
                }
                while (match(p, TOKEN_NEWLINE))
                    ;
            }
            node->as.if_stmt.else_branch = else_branch;
        }

        // Require ENDIF
        if (!match(p, TOKEN_ENDIF)) {
            error(p, "Expected ENDIF");
            free(node);
            return NULL;
        }
    } else {
        // Single-line form: IF cond THEN statement [ELSE statement]
        node->as.if_stmt.then_branch = statement(p);

        if (match(p, TOKEN_ELSE)) {
            node->as.if_stmt.else_branch = statement(p);
        }
    }

    return node;
}

// FOR variable = start TO end [STEP step]
static c64script_ast_node_t *for_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_FOR;
    node->line = p->previous.line;

    // Parse variable name
    if (!match(p, TOKEN_IDENTIFIER)) {
        error(p, "Expected variable name after FOR");
        free(node);
        return NULL;
    }
    node->as.for_stmt.variable = strndup(p->previous.start, p->previous.length);

    // Expect '='
    if (!match(p, TOKEN_EQ)) {
        error(p, "Expected '=' in FOR statement");
        free(node);
        return NULL;
    }

    // Parse start expression
    node->as.for_stmt.start = expression(p);

    // Expect TO
    if (!match(p, TOKEN_TO)) {
        error(p, "Expected TO in FOR statement");
        free(node);
        return NULL;
    }

    // Parse end expression
    node->as.for_stmt.end = expression(p);

    // Optional STEP
    if (match(p, TOKEN_STEP)) {
        node->as.for_stmt.step = expression(p);
    } else {
        // Default step is 1
        c64script_ast_expr_t *step = calloc(1, sizeof(c64script_ast_expr_t));
        step->type = AST_EXPR_NUMBER;
        step->as.number = 1.0;
        node->as.for_stmt.step = step;
    }

    // Expect newline
    match(p, TOKEN_NEWLINE);

    // Parse body until NEXT
    c64script_ast_node_t *body = NULL;
    c64script_ast_node_t *body_tail = NULL;

    while (!check(p, TOKEN_NEXT) && !check(p, TOKEN_EOF)) {
        c64script_ast_node_t *stmt = statement(p);
        if (stmt) {
            if (!body) {
                body = stmt;
                body_tail = stmt;
            } else {
                body_tail->next = stmt;
                body_tail = stmt;
            }
        }
        while (match(p, TOKEN_NEWLINE))
            ;
    }
    node->as.for_stmt.body = body;

    // Require NEXT
    if (!match(p, TOKEN_NEXT)) {
        error(p, "Expected NEXT");
        free(node);
        return NULL;
    }

    // Optional variable name after NEXT (for clarity, ignored)
    match(p, TOKEN_IDENTIFIER);

    return node;
}

// WHILE condition
static c64script_ast_node_t *while_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_WHILE;
    node->line = p->previous.line;

    // Parse condition
    node->as.while_stmt.condition = expression(p);

    // Expect newline
    match(p, TOKEN_NEWLINE);

    // Parse body until WEND or ENDWHILE
    c64script_ast_node_t *body = NULL;
    c64script_ast_node_t *body_tail = NULL;

    while (!check(p, TOKEN_WEND) && !check(p, TOKEN_ENDWHILE) && !check(p, TOKEN_EOF)) {
        c64script_ast_node_t *stmt = statement(p);
        if (stmt) {
            if (!body) {
                body = stmt;
                body_tail = stmt;
            } else {
                body_tail->next = stmt;
                body_tail = stmt;
            }
        }
        while (match(p, TOKEN_NEWLINE))
            ;
    }
    node->as.while_stmt.body = body;

    // Require WEND or ENDWHILE
    if (!match(p, TOKEN_WEND) && !match(p, TOKEN_ENDWHILE)) {
        error(p, "Expected WEND or ENDWHILE");
        free(node);
        return NULL;
    }

    return node;
}

// WAIT duration or WAIT UNTIL condition
static c64script_ast_node_t *wait_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->line = p->previous.line;

    if (match(p, TOKEN_UNTIL)) {
        // WAIT UNTIL condition
        node->type = AST_STMT_WAIT_UNTIL;
        node->as.wait_until_stmt.time_expr = expression(p);
    } else {
        // WAIT duration
        node->type = AST_STMT_WAIT;
        // Duration will be evaluated at runtime
        // For now we just store 0; bytecode compiler will handle the expression
        node->as.wait_stmt.duration_ms = 0;
    }

    return node;
}

// ============================================================================
// PLUGIN ACTION STATEMENTS
// ============================================================================

// EFFECT name [preset]
static c64script_ast_node_t *effect_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_EFFECT;
    node->line = p->previous.line;

    node->as.effect_stmt.preset_name = expression(p);

    return node;
}

// EFFECTPARAM name value
static c64script_ast_node_t *effectparam_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_EFFECTPARAM;
    node->line = p->previous.line;

    node->as.effectparam_stmt.param_name = expression(p);
    node->as.effectparam_stmt.param_value = expression(p);

    return node;
}

// PALETTE name
static c64script_ast_node_t *palette_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_PALETTE;
    node->line = p->previous.line;

    node->as.palette_stmt.palette_name = expression(p);

    return node;
}

// PLAYSID filename
static c64script_ast_node_t *playsid_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_PLAYSID;
    node->line = p->previous.line;

    node->as.playsid_stmt.path = expression(p);
    node->as.playsid_stmt.songnr = NULL;

    return node;
}

// RUNPRG filename
static c64script_ast_node_t *runprg_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_RUNPRG;
    node->line = p->previous.line;

    node->as.runprg_stmt.path = expression(p);

    return node;
}

// MOUNTDISK filename
static c64script_ast_node_t *mountdisk_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_MOUNTDISK;
    node->line = p->previous.line;

    node->as.mountdisk_stmt.path = expression(p);

    return node;
}

// AUTOSTART filename
static c64script_ast_node_t *autostart_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_AUTOSTART;
    node->line = p->previous.line;

    // AUTOSTART has no data in the AST structure

    return node;
}

// RESET
static c64script_ast_node_t *reset_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_RESET;
    node->line = p->previous.line;

    return node;
}

// REBOOT
static c64script_ast_node_t *reboot_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_REBOOT;
    node->line = p->previous.line;

    return node;
}

// RECORDSTART
static c64script_ast_node_t *recordstart_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_RECORDSTART;
    node->line = p->previous.line;

    return node;
}

// RECORDSTOP
static c64script_ast_node_t *recordstop_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_RECORDSTOP;
    node->line = p->previous.line;

    return node;
}

// TYPE text
static c64script_ast_node_t *type_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_TYPE;
    node->line = p->previous.line;

    node->as.type_stmt.text = expression(p);

    return node;
}

// KEY keycode
static c64script_ast_node_t *key_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_KEY;
    node->line = p->previous.line;

    node->as.key_stmt.key = expression(p);

    return node;
}

// POKE address, value
static c64script_ast_node_t *poke_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_POKE;
    node->line = p->previous.line;

    node->as.poke_stmt.address = expression(p);
    if (!match(p, TOKEN_COMMA)) {
        error(p, "Expected ',' in POKE statement");
        free(node);
        return NULL;
    }
    node->as.poke_stmt.single_value = expression(p);
    node->as.poke_stmt.values = NULL;
    node->as.poke_stmt.value_count = 0;

    return node;
}

// LOGFILE filename
static c64script_ast_node_t *logfile_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_LOGFILE;
    node->line = p->previous.line;

    node->as.logfile_stmt.path = expression(p);
    node->as.logfile_stmt.truncate = false; // Default to append

    return node;
}

// LOG message
static c64script_ast_node_t *log_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_LOG;
    node->line = p->previous.line;

    node->as.log_stmt.message = expression(p);

    return node;
}

// PRINT expression
static c64script_ast_node_t *print_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_PRINT;
    node->line = p->previous.line;

    node->as.print_stmt.message = expression(p);

    return node;
}

// TRON
static c64script_ast_node_t *tron_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_TRON;
    node->line = p->previous.line;

    return node;
}

// TROFF
static c64script_ast_node_t *troff_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_TROFF;
    node->line = p->previous.line;

    return node;
}

static c64script_ast_node_t *statement(parser_t *p)
{
    // Skip newlines
    while (match(p, TOKEN_NEWLINE))
        ;

    if (check(p, TOKEN_EOF))
        return NULL;

    // Label (can be LABEL keyword or just identifier with optional colon)
    if (match(p, TOKEN_LABEL_KW)) {
        return label_statement(p);
    }

    // Check for identifier (could be label or assignment)
    if (check(p, TOKEN_IDENTIFIER)) {
        // Peek ahead for colon or =
        c64script_token_t saved = p->current;
        advance(p);
        if (check(p, TOKEN_COLON)) {
            // It's a label
            char *name = malloc(p->previous.length + 1);
            if (name) {
                memcpy(name, p->previous.start, p->previous.length);
                name[p->previous.length] = '\0';
            }
            advance(p); // Skip colon
            c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
            node->type = AST_STMT_LABEL;
            node->line = p->previous.line;
            node->as.label.name = name;
            return node;
        } else if (check(p, TOKEN_EQ)) {
            // It's an assignment
            char *name = malloc(p->previous.length + 1);
            if (name) {
                memcpy(name, p->previous.start, p->previous.length);
                name[p->previous.length] = '\0';
            }
            return assignment_statement(p, name);
        } else {
            // Restore and continue
            p->current = saved;
        }
    }

    // Keywords
    if (match(p, TOKEN_LET)) {
        if (match(p, TOKEN_IDENTIFIER)) {
            char *name = malloc(p->previous.length + 1);
            if (name) {
                memcpy(name, p->previous.start, p->previous.length);
                name[p->previous.length] = '\0';
            }
            return assignment_statement(p, name);
        } else {
            error(p, "Expected variable name after LET");
            return NULL;
        }
    }

    if (match(p, TOKEN_GOTO))
        return goto_statement(p);
    if (match(p, TOKEN_GOSUB))
        return gosub_statement(p);
    if (match(p, TOKEN_RETURN))
        return return_statement(p);
    if (match(p, TOKEN_STOP) || match(p, TOKEN_END))
        return stop_statement(p);

    // Control flow
    if (match(p, TOKEN_IF))
        return if_statement(p);
    if (match(p, TOKEN_FOR))
        return for_statement(p);
    if (match(p, TOKEN_WHILE))
        return while_statement(p);
    if (match(p, TOKEN_WAIT))
        return wait_statement(p);

    // Plugin actions
    if (match(p, TOKEN_EFFECT))
        return effect_statement(p);
    if (match(p, TOKEN_EFFECTPARAM))
        return effectparam_statement(p);
    if (match(p, TOKEN_PALETTE))
        return palette_statement(p);
    if (match(p, TOKEN_PLAYSID))
        return playsid_statement(p);
    if (match(p, TOKEN_RUNPRG))
        return runprg_statement(p);
    if (match(p, TOKEN_MOUNTDISK))
        return mountdisk_statement(p);
    if (match(p, TOKEN_AUTOSTART))
        return autostart_statement(p);
    if (match(p, TOKEN_RESET))
        return reset_statement(p);
    if (match(p, TOKEN_REBOOT))
        return reboot_statement(p);
    if (match(p, TOKEN_RECORDSTART))
        return recordstart_statement(p);
    if (match(p, TOKEN_RECORDSTOP))
        return recordstop_statement(p);
    if (match(p, TOKEN_TYPE))
        return type_statement(p);
    if (match(p, TOKEN_KEY))
        return key_statement(p);
    if (match(p, TOKEN_POKE))
        return poke_statement(p);
    if (match(p, TOKEN_LOGFILE))
        return logfile_statement(p);
    if (match(p, TOKEN_LOG))
        return log_statement(p);
    if (match(p, TOKEN_PRINT))
        return print_statement(p);
    if (match(p, TOKEN_TRON))
        return tron_statement(p);
    if (match(p, TOKEN_TROFF))
        return troff_statement(p);

    error(p, "Unexpected statement");
    return NULL;
}

static c64script_ast_node_t *declaration(parser_t *p)
{
    return statement(p);
}

// ============================================================================
// PUBLIC API
// ============================================================================

c64script_ast_node_t *c64script_parse(const char *source, size_t source_size, char *error_msg, size_t error_msg_size)
{
    c64script_tokenizer_t tokenizer_obj;
    c64script_tokenizer_init(&tokenizer_obj, source, source_size);

    parser_t parser = {0};
    parser.tokenizer = &tokenizer_obj;
    parser.had_error = false;
    parser.panic_mode = false;

    // Get first token
    advance(&parser);

    // Parse program as list of statements
    c64script_ast_node_t *root = NULL;
    c64script_ast_node_t *tail = NULL;

    while (!match(&parser, TOKEN_EOF)) {
        c64script_ast_node_t *decl = declaration(&parser);
        if (decl) {
            if (!root) {
                root = decl;
                tail = decl;
            } else {
                tail->next = decl;
                tail = decl;
            }
        }

        if (parser.had_error) {
            synchronize(&parser);
            parser.had_error = false;
            parser.panic_mode = false;
        }
    }

    if (parser.had_error && error_msg) {
        snprintf(error_msg, error_msg_size, "%s", parser.error_msg);
    }

    // Tokenizer is stack-allocated, no cleanup needed

    return parser.had_error ? NULL : root;
}
