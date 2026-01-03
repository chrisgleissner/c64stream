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

    // TODO: Implement remaining statements (IF, FOR, WHILE, plugin actions, etc.)

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
