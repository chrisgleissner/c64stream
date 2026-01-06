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
    bool had_any_error;
    bool panic_mode;
    char error_msg[1024];
    int error_count; // Track number of errors reported
    int max_errors;  // Maximum errors before stopping (0 = unlimited)
    bool log_errors;
} parser_t;

// Forward declarations
static c64script_ast_node_t *statement(parser_t *p);
static c64script_ast_node_t *dim_statement(parser_t *p);
static c64script_ast_node_t *function_def_statement(parser_t *p);
static void error(parser_t *p, const char *message);
static c64script_ast_expr_t *expression(parser_t *p);
static void free_expr(c64script_ast_expr_t *expr);
// HTTP <method> <url> [HEADERS <expr>] [BODY <expr>] [STATUS <var>] [RESPONSE <var>]
static c64script_ast_node_t *declaration(parser_t *p);

static c64script_token_t peek_token(parser_t *p)
{
    return c64script_tokenizer_peek(p->tokenizer);
}

static char *dup_upper(const char *start, size_t length)
{
    char *out = malloc(length + 1);
    if (!out) {
        return NULL;
    }
    for (size_t i = 0; i < length; i++) {
        out[i] = (char)toupper((unsigned char)start[i]);
    }
    out[length] = '\0';
    return out;
}

static char *dup_token_text(const c64script_token_t *tok)
{
    if (!tok || !tok->start || tok->length == 0) {
        return NULL;
    }
    char *out = malloc(tok->length + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, tok->start, tok->length);
    out[tok->length] = '\0';
    return out;
}

static bool parse_uint32_decimal_token(const c64script_token_t *tok, uint32_t *out_value)
{
    if (!tok || !out_value) {
        return false;
    }

    if (tok->length == 0) {
        return false;
    }

    uint64_t value = 0;
    for (size_t i = 0; i < tok->length; i++) {
        char c = tok->start[i];
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10u + (uint64_t)(c - '0');
        if (value > UINT32_MAX) {
            return false;
        }
    }

    *out_value = (uint32_t)value;
    return true;
}

static char *dup_normalized_label_from_number(const c64script_token_t *tok)
{
    uint32_t value = 0;
    if (!parse_uint32_decimal_token(tok, &value)) {
        return NULL;
    }

    char buf[32];
    int written = snprintf(buf, sizeof(buf), "%u", value);
    if (written < 0 || (size_t)written >= sizeof(buf)) {
        return NULL;
    }

    return strdup(buf);
}

static char *decode_string_literal(parser_t *p, const c64script_token_t *tok)
{
    if (!tok || tok->type != TOKEN_STRING || tok->length < 2) {
        error(p, "Invalid string literal");
        return NULL;
    }

    const char *in = tok->start + 1;
    size_t in_len = tok->length - 2;

    char *out = malloc(in_len + 1);
    if (!out) {
        error(p, "Out of memory");
        return NULL;
    }

    size_t out_len = 0;
    for (size_t i = 0; i < in_len; i++) {
        char c = in[i];

        if (c == '"' && (i + 1) < in_len && in[i + 1] == '"') {
            out[out_len++] = '"';
            i++;
            continue;
        }

        if (c != '\\') {
            out[out_len++] = c;
            continue;
        }

        if ((i + 1) >= in_len) {
            free(out);
            error(p, "Unterminated escape sequence");
            return NULL;
        }

        char esc = in[++i];
        switch (esc) {
        case '\\':
            out[out_len++] = '\\';
            break;
        case '"':
            out[out_len++] = '"';
            break;
        case 'r':
            out[out_len++] = '\r';
            break;
        case 'n':
            out[out_len++] = '\n';
            break;
        case 't':
            out[out_len++] = '\t';
            break;
        case 'x': {
            if ((i + 2) >= in_len) {
                free(out);
                error(p, "Invalid hex escape (expected \\xNN)");
                return NULL;
            }
            char h1 = in[++i];
            char h2 = in[++i];
            int v1 = isxdigit((unsigned char)h1) ? (isdigit((unsigned char)h1) ? (h1 - '0') : (tolower(h1) - 'a' + 10))
                                                 : -1;
            int v2 = isxdigit((unsigned char)h2) ? (isdigit((unsigned char)h2) ? (h2 - '0') : (tolower(h2) - 'a' + 10))
                                                 : -1;
            if (v1 < 0 || v2 < 0) {
                free(out);
                error(p, "Invalid hex escape (expected \\xNN)");
                return NULL;
            }
            out[out_len++] = (char)((v1 << 4) | v2);
            break;
        }
        default:
            free(out);
            error(p, "Unknown escape sequence");
            return NULL;
        }
    }

    out[out_len] = '\0';
    return out;
}

// ============================================================================
// ERROR HANDLING
// ============================================================================

static bool c64script_debug_logging_enabled(void)
{
    if (c64_debug_logging) {
        return true;
    }
    const char *env = getenv("C64SCRIPT_DEBUG_LOGS");
    if (!env || env[0] == '\0' || strcmp(env, "0") == 0) {
        return false;
    }
    return true;
}

static void error_at(parser_t *p, c64script_token_t *token, const char *message)
{
    if (p->panic_mode)
        return;

    // Stop reporting errors if we've hit the limit
    if (p->max_errors > 0 && p->error_count >= p->max_errors) {
        return;
    }

    p->panic_mode = true;
    p->had_error = true;
    p->had_any_error = true;
    p->error_count++;

    snprintf(p->error_msg, sizeof(p->error_msg), "[Line %d:%d] Error at '%.*s': %s", token->line, token->column,
             (int)token->length, token->start, message);
    if (p->log_errors) {
        blog(LOG_ERROR, "%s", p->error_msg);
    } else if (c64script_debug_logging_enabled()) {
        blog(LOG_DEBUG, "%s", p->error_msg);
    }

    // If we've hit the error limit, add a final message
    if (p->max_errors > 0 && p->error_count >= p->max_errors) {
        if (p->log_errors) {
            blog(LOG_ERROR, "Error limit reached (%d errors), stopping parse", p->max_errors);
        } else if (c64script_debug_logging_enabled()) {
            blog(LOG_DEBUG, "Error limit reached (%d errors), stopping parse", p->max_errors);
        }
    }
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

    // Don't synchronize if we've hit the error limit
    if (p->max_errors > 0 && p->error_count >= p->max_errors) {
        return;
    }

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

        // Stop synchronizing if we hit the error limit during advance
        if (p->max_errors > 0 && p->error_count >= p->max_errors) {
            return;
        }
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
    if (p->previous.type == TOKEN_DURATION) {
        expr->as.number = (double)p->previous.value.duration_ms;
    } else {
        expr->as.number = p->previous.value.number;
    }
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
    if (p->previous.type == TOKEN_STRING) {
        expr->as.string = decode_string_literal(p, &p->previous);
    } else {
        expr->as.string = dup_token_text(&p->previous);
        if (!expr->as.string) {
            error(p, "Out of memory");
        }
    }
    if (!expr->as.string) {
        free(expr);
        return NULL;
    }
    return expr;
}

// Identifier or function name
// Detects array assignment vs function call by peeking ahead
static c64script_ast_expr_t *variable(parser_t *p, bool can_assign)
{
    (void)can_assign; // We'll detect assignment context by lookahead instead

    c64script_token_t ident_token = p->previous;
    char *name = dup_upper(ident_token.start, ident_token.length);

    // Check for array access: identifier(expr)
    if (match(p, TOKEN_LPAREN)) {
        // Decide whether this is array access or function call
        // If the pattern is: identifier(expr) = ..., it's array assignment
        // Otherwise, it's a function call

        // Create a clone tokenizer for lookahead (doesn't affect parser state)
        c64script_tokenizer_t lookahead_tokenizer = *p->tokenizer;

        // Scan ahead to find matching ) and check if = follows
        int paren_depth = 1;
        bool found_assignment = false;

        while (paren_depth > 0) {
            c64script_token_t t = c64script_tokenizer_next(&lookahead_tokenizer);
            if (t.type == TOKEN_EOF) {
                break;
            }
            if (t.type == TOKEN_LPAREN) {
                paren_depth++;
            } else if (t.type == TOKEN_RPAREN) {
                paren_depth--;
                if (paren_depth == 0) {
                    // Check if next token is =
                    c64script_token_t next = c64script_tokenizer_peek(&lookahead_tokenizer);
                    if (next.type == TOKEN_EQ) {
                        found_assignment = true;
                    }
                }
            }
        }

        // No need to restore - we used a separate tokenizer

        if (found_assignment) {
            // Parse as array access
            c64script_ast_expr_t *expr = calloc(1, sizeof(c64script_ast_expr_t));
            if (!expr) {
                free(name);
                return NULL;
            }
            expr->type = AST_EXPR_ARRAY_ACCESS;
            expr->line = ident_token.line;
            expr->as.array_access.name = name;
            expr->as.array_access.index = expression(p);

            consume(p, TOKEN_RPAREN, "Expected ')' after array index");
            if (p->panic_mode) {
                free_expr(expr);
                return NULL;
            }

            return expr;
        } else {
            // Parse as function call
            c64script_ast_expr_t **args = NULL;
            size_t arg_count = 0;
            size_t arg_capacity = 0;

            if (!check(p, TOKEN_RPAREN)) {
                arg_capacity = 4;
                args = malloc(arg_capacity * sizeof(c64script_ast_expr_t *));
                if (!args) {
                    free(name);
                    return NULL;
                }

                do {
                    if (arg_count >= arg_capacity) {
                        arg_capacity *= 2;
                        c64script_ast_expr_t **new_args = realloc(args, arg_capacity * sizeof(c64script_ast_expr_t *));
                        if (!new_args) {
                            for (size_t i = 0; i < arg_count; i++) {
                                free_expr(args[i]);
                            }
                            free(args);
                            free(name);
                            return NULL;
                        }
                        args = new_args;
                    }

                    args[arg_count++] = expression(p);
                } while (match(p, TOKEN_COMMA));
            }

            consume(p, TOKEN_RPAREN, "Expected ')' after arguments");
            if (p->panic_mode) {
                for (size_t i = 0; i < arg_count; i++) {
                    free_expr(args[i]);
                }
                free(args);
                free(name);
                return NULL;
            }

            // Function call
            c64script_ast_expr_t *expr = calloc(1, sizeof(c64script_ast_expr_t));
            if (!expr) {
                for (size_t i = 0; i < arg_count; i++) {
                    free_expr(args[i]);
                }
                free(args);
                free(name);
                return NULL;
            }
            expr->type = AST_EXPR_CALL;
            expr->line = ident_token.line;
            expr->as.call.name = name;
            expr->as.call.args = args;
            expr->as.call.arg_count = arg_count;
            return expr;
        }
    }

    // Check for map access: identifier{expr}
    if (match(p, TOKEN_LBRACE)) {
        c64script_ast_expr_t *expr = calloc(1, sizeof(c64script_ast_expr_t));
        if (!expr) {
            free(name);
            return NULL;
        }
        expr->type = AST_EXPR_MAP_ACCESS;
        expr->line = ident_token.line;
        expr->as.map_access.name = name;
        expr->as.map_access.key = expression(p);

        consume(p, TOKEN_RBRACE, "Expected '}' after map key");
        if (p->panic_mode) {
            free_expr(expr);
            return NULL;
        }

        return expr;
    }

    // Simple identifier
    c64script_ast_expr_t *expr = calloc(1, sizeof(c64script_ast_expr_t));
    if (!expr) {
        free(name);
        return NULL;
    }
    expr->type = AST_EXPR_IDENTIFIER;
    expr->line = ident_token.line;
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

// Marker function for Pratt call handling. Calls are handled explicitly in parse_precedence()
// so this function should never be invoked directly.
static c64script_ast_expr_t *call(parser_t *p, bool can_assign)
{
    (void)p;
    (void)can_assign;
    return NULL;
}

// Parse rule table
static parse_rule_t rules[] = {
    [TOKEN_LPAREN] = {grouping, call, PREC_CALL},   [TOKEN_RPAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},     [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_MULTIPLY] = {NULL, binary, PREC_FACTOR}, [TOKEN_DIVIDE] = {NULL, binary, PREC_FACTOR},
    [TOKEN_NOT] = {unary, NULL, PREC_NONE},         [TOKEN_AND] = {NULL, binary, PREC_AND},
    [TOKEN_OR] = {NULL, binary, PREC_OR},           [TOKEN_XOR] = {NULL, binary, PREC_XOR},
    [TOKEN_EQ] = {NULL, binary, PREC_EQUALITY},     [TOKEN_EQ_EQ] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_NE] = {NULL, binary, PREC_EQUALITY},     [TOKEN_NE_ALT] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_LT] = {NULL, binary, PREC_COMPARISON},   [TOKEN_LE] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GT] = {NULL, binary, PREC_COMPARISON},   [TOKEN_GE] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},     [TOKEN_HEX_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_DURATION] = {number, NULL, PREC_NONE},   [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_C64U_PATH] = {string, NULL, PREC_NONE},  [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
    [TOKEN_PEEK] = {variable, NULL, PREC_NONE},
};

static parse_rule_t *get_rule(c64script_token_type_t type)
{
    static parse_rule_t default_rule = {NULL, NULL, PREC_NONE};
    if (type >= 0 && type < sizeof(rules) / sizeof(rules[0])) {
        return &rules[type];
    }
    return &default_rule;
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
    if (!left) {
        return NULL;
    }

    while (precedence <= get_rule(p->current.type)->precedence) {
        advance(p);
        parse_fn_t infix_rule = get_rule(p->previous.type)->infix;

        // For binary operators, we need to properly thread the left expression
        if (infix_rule == binary) {
            c64script_token_type_t op = p->previous.type;
            parse_rule_t *rule = get_rule(op);
            c64script_ast_expr_t *right = parse_precedence(p, (precedence_t)(rule->precedence + 1));
            if (!right) {
                free_expr(left);
                return NULL;
            }

            c64script_ast_expr_t *bin_expr = calloc(1, sizeof(c64script_ast_expr_t));
            if (!bin_expr) {
                free_expr(left);
                free_expr(right);
                return NULL;
            }
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
        } else if (infix_rule == call) {
            // Function call - left expression must be an identifier
            if (left->type != AST_EXPR_IDENTIFIER) {
                error(p, "Can only call functions by name");
                free_expr(left);
                return NULL;
            }

            // Build call expression with function name
            c64script_ast_expr_t *call_expr = calloc(1, sizeof(c64script_ast_expr_t));
            if (!call_expr) {
                free_expr(left);
                return NULL;
            }
            call_expr->type = AST_EXPR_CALL;
            call_expr->line = p->previous.line;

            // Build argument list
            size_t arg_capacity = 0;
            size_t arg_count = 0;
            c64script_ast_expr_t **args = NULL;

            if (!check(p, TOKEN_RPAREN)) {
                arg_capacity = 4;
                args = malloc(arg_capacity * sizeof(c64script_ast_expr_t *));
                if (!args) {
                    free(call_expr);
                    free_expr(left);
                    return NULL;
                }
                do {
                    if (arg_count >= arg_capacity) {
                        arg_capacity *= 2;
                        c64script_ast_expr_t **new_args = realloc(args, arg_capacity * sizeof(c64script_ast_expr_t *));
                        if (!new_args) {
                            for (size_t i = 0; i < arg_count; i++) {
                                free_expr(args[i]);
                            }
                            free(args);
                            free(call_expr);
                            free_expr(left);
                            return NULL;
                        }
                        args = new_args;
                    }
                    c64script_ast_expr_t *arg_expr = expression(p);
                    if (!arg_expr) {
                        for (size_t i = 0; i < arg_count; i++) {
                            free_expr(args[i]);
                        }
                        free(args);
                        free(call_expr);
                        free_expr(left);
                        return NULL;
                    }
                    args[arg_count++] = arg_expr;
                } while (match(p, TOKEN_COMMA));
            }

            consume(p, TOKEN_RPAREN, "Expected ')' after arguments");
            if (p->panic_mode) {
                for (size_t i = 0; i < arg_count; i++) {
                    free_expr(args[i]);
                }
                free(args);
                free(call_expr);
                free_expr(left);
                return NULL;
            }

            call_expr->as.call.name = left->as.identifier; // Transfer ownership
            call_expr->as.call.args = args;
            call_expr->as.call.arg_count = arg_count;

            // Free the old identifier expression (we transferred the name)
            free(left);

            left = call_expr;
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

static char *parse_label_ref(parser_t *p)
{
    if (match(p, TOKEN_IDENTIFIER)) {
        return dup_upper(p->previous.start, p->previous.length);
    }

    if (match(p, TOKEN_NUMBER)) {
        if (memchr(p->previous.start, '.', p->previous.length) != NULL) {
            error(p, "Line numbers must be integers");
            return NULL;
        }
        char *normalized = dup_normalized_label_from_number(&p->previous);
        if (!normalized) {
            error(p, "Invalid line number");
            return NULL;
        }
        return normalized;
    }

    error(p, "Expected label");
    return NULL;
}

// Assignment: [LET] variable = expression
// Also handles array[index] = value and map{key} = value
static c64script_ast_node_t *assignment_statement(parser_t *p, c64script_ast_expr_t *target)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node) {
        if (target)
            free_expr(target);
        return NULL;
    }

    // Target can be:
    // - AST_EXPR_IDENTIFIER (simple assignment)
    // - AST_EXPR_ARRAY_ACCESS (array element assignment)
    // - AST_EXPR_MAP_ACCESS (map entry assignment)

    if (target->type == AST_EXPR_IDENTIFIER) {
        // Simple variable assignment
        node->type = AST_STMT_ASSIGNMENT;
        node->line = target->line;
        node->as.assignment.variable = strdup(target->as.identifier);
        free_expr(target);
    } else if (target->type == AST_EXPR_ARRAY_ACCESS) {
        // Array element assignment: arr(index) = value
        node->type = AST_STMT_ARRAY_SET;
        node->line = target->line;
        node->as.array_set.array_name = strdup(target->as.array_access.name);
        node->as.array_set.index = target->as.array_access.index;
        target->as.array_access.index = NULL; // Transfer ownership
        free(target);
    } else if (target->type == AST_EXPR_MAP_ACCESS) {
        // Map entry assignment: map{key} = value
        node->type = AST_STMT_MAP_SET;
        node->line = target->line;
        node->as.map_set.map_name = strdup(target->as.map_access.name);
        node->as.map_set.key = target->as.map_access.key;
        target->as.map_access.key = NULL; // Transfer ownership
        free(target);
    } else {
        error(p, "Invalid assignment target");
        free_expr(target);
        free(node);
        return NULL;
    }

    consume(p, TOKEN_EQ, "Expected '=' in assignment");
    if (p->panic_mode) {
        c64script_ast_free(node);
        return NULL;
    }

    if (node->type == AST_STMT_ASSIGNMENT) {
        node->as.assignment.value = expression(p);
    } else if (node->type == AST_STMT_ARRAY_SET) {
        node->as.array_set.value = expression(p);
    } else if (node->type == AST_STMT_MAP_SET) {
        node->as.map_set.value = expression(p);
    }

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

    node->as.label.name = parse_label_ref(p);

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
    node->as.goto_stmt.label = parse_label_ref(p);

    return node;
}

// GOSUB label[(expr, ...)]
static c64script_ast_node_t *gosub_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_GOSUB;
    node->line = p->previous.line;
    node->as.gosub_stmt.label = parse_label_ref(p);
    node->as.gosub_stmt.params = NULL;
    node->as.gosub_stmt.param_count = 0;

    // Check for optional parameters
    if (match(p, TOKEN_LPAREN)) {
        // Parse parameter list
        size_t capacity = 4;
        node->as.gosub_stmt.params = malloc(capacity * sizeof(c64script_ast_expr_t *));
        if (!node->as.gosub_stmt.params) {
            c64script_ast_free(node);
            return NULL;
        }

        if (!check(p, TOKEN_RPAREN)) {
            do {
                if (node->as.gosub_stmt.param_count >= capacity) {
                    capacity *= 2;
                    c64script_ast_expr_t **new_params =
                        realloc(node->as.gosub_stmt.params, capacity * sizeof(c64script_ast_expr_t *));
                    if (!new_params) {
                        c64script_ast_free(node);
                        return NULL;
                    }
                    node->as.gosub_stmt.params = new_params;
                }
                node->as.gosub_stmt.params[node->as.gosub_stmt.param_count] = expression(p);
                node->as.gosub_stmt.param_count++;
            } while (match(p, TOKEN_COMMA));
        }

        if (!match(p, TOKEN_RPAREN)) {
            error(p, "Expected ')' after GOSUB parameters");
            c64script_ast_free(node);
            return NULL;
        }
    }

    return node;
}

// RETURN [expression]
static c64script_ast_node_t *return_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_RETURN;
    node->line = p->previous.line;
    node->as.return_stmt.return_value = NULL;

    // Check if there's an optional return expression
    if (!check(p, TOKEN_EOF) && !check(p, TOKEN_NEWLINE)) {
        node->as.return_stmt.return_value = expression(p);
    }

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

static c64script_ast_node_t *rem_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_REM;
    node->line = p->previous.line;

    // The REM token already contains everything until newline,
    // so we don't need to consume additional tokens
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
    node->as.for_stmt.variable = dup_upper(p->previous.start, p->previous.length);

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
        step->line = node->line;
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

    while (!check(p, TOKEN_WEND) && !check(p, TOKEN_ENDWHILE) && !check(p, TOKEN_EOF) &&
           !(check(p, TOKEN_END) && peek_token(p).type == TOKEN_WHILE)) {
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
    if (match(p, TOKEN_WEND) || match(p, TOKEN_ENDWHILE)) {
        return node;
    }

    if (match(p, TOKEN_END)) {
        consume(p, TOKEN_WHILE, "Expected WHILE after END");
        return node;
    }

    error(p, "Expected WEND or ENDWHILE");
    free(node);
    return NULL;
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
        node->as.wait_stmt.unit = C64SCRIPT_WAIT_UNIT_S;

        if (check(p, TOKEN_DURATION)) {
            advance(p);
            c64script_ast_expr_t *duration = calloc(1, sizeof(c64script_ast_expr_t));
            duration->type = AST_EXPR_NUMBER;
            duration->line = node->line;
            duration->as.number = (double)p->previous.value.duration_ms;
            node->as.wait_stmt.duration = duration;
            node->as.wait_stmt.unit = C64SCRIPT_WAIT_UNIT_MS;
            return node;
        }

        node->as.wait_stmt.duration = expression(p);

        if (check(p, TOKEN_IDENTIFIER)) {
            c64script_token_t unit_tok = p->current;
            if (unit_tok.length == 2 && (unit_tok.start[0] == 'm' || unit_tok.start[0] == 'M') &&
                (unit_tok.start[1] == 's' || unit_tok.start[1] == 'S')) {
                advance(p);
                node->as.wait_stmt.unit = C64SCRIPT_WAIT_UNIT_MS;
            } else if (unit_tok.length == 1 && (unit_tok.start[0] == 's' || unit_tok.start[0] == 'S')) {
                advance(p);
                node->as.wait_stmt.unit = C64SCRIPT_WAIT_UNIT_S;
            } else if (unit_tok.length == 1 && (unit_tok.start[0] == 'm' || unit_tok.start[0] == 'M')) {
                advance(p);
                node->as.wait_stmt.unit = C64SCRIPT_WAIT_UNIT_M;
            }
        }
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

// PALETTECOLOR index, r, g, b
static c64script_ast_node_t *palettecolor_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_PALETTECOLOR;
    node->line = p->previous.line;

    node->as.palettecolor_stmt.index = expression(p);

    if (!match(p, TOKEN_COMMA)) {
        error(p, "Expected comma after palette color index");
        c64script_ast_free(node);
        return NULL;
    }

    node->as.palettecolor_stmt.r = expression(p);

    if (!match(p, TOKEN_COMMA)) {
        error(p, "Expected comma after red component");
        c64script_ast_free(node);
        return NULL;
    }

    node->as.palettecolor_stmt.g = expression(p);

    if (!match(p, TOKEN_COMMA)) {
        error(p, "Expected comma after green component");
        c64script_ast_free(node);
        return NULL;
    }

    node->as.palettecolor_stmt.b = expression(p);

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

    if (match(p, TOKEN_SONGNR)) {
        match(p, TOKEN_EQ);
        node->as.playsid_stmt.songnr = expression(p);
    }

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

// RUNLOCAL path [ARGS args] [STATUS var] [OUTPUT var]
static c64script_ast_node_t *runlocal_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_RUNLOCAL;
    node->line = p->previous.line;

    node->as.runlocal_stmt.path = expression(p);
    node->as.runlocal_stmt.args = NULL;
    node->as.runlocal_stmt.status_var = NULL;
    node->as.runlocal_stmt.output_var = NULL;

    // Parse optional parameters
    while (true) {
        if (match(p, TOKEN_ARGS)) {
            node->as.runlocal_stmt.args = expression(p);
        } else if (match(p, TOKEN_STATUS)) {
            node->as.runlocal_stmt.status_var = expression(p);
        } else if (match(p, TOKEN_OUTPUT)) {
            node->as.runlocal_stmt.output_var = expression(p);
        } else {
            break;
        }
    }

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

    if (match(p, TOKEN_LBRACKET)) {
        size_t capacity = 8;
        size_t count = 0;
        c64script_ast_expr_t **values = calloc(capacity, sizeof(c64script_ast_expr_t *));
        if (!values) {
            error(p, "Out of memory");
            free(node);
            return NULL;
        }

        if (!check(p, TOKEN_RBRACKET)) {
            do {
                if (count >= capacity) {
                    capacity *= 2;
                    c64script_ast_expr_t **new_values = realloc(values, capacity * sizeof(c64script_ast_expr_t *));
                    if (!new_values) {
                        error(p, "Out of memory");
                        free(values);
                        free(node);
                        return NULL;
                    }
                    values = new_values;
                }

                values[count++] = expression(p);
            } while (match(p, TOKEN_COMMA));
        }

        consume(p, TOKEN_RBRACKET, "Expected ']'");
        node->as.poke_stmt.values = values;
        node->as.poke_stmt.value_count = count;
        node->as.poke_stmt.single_value = NULL;
        return node;
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

    if (match(p, TOKEN_TRUNCATE)) {
        node->as.logfile_stmt.truncate = true;
    } else if (match(p, TOKEN_APPEND)) {
        node->as.logfile_stmt.truncate = false;
    }

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

// READFILE variable, path
static c64script_ast_node_t *readfile_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_READFILE;
    node->line = p->previous.line;

    node->as.readfile_stmt.variable = expression(p);

    if (!match(p, TOKEN_COMMA)) {
        error(p, "Expected comma after variable in READFILE");
        c64script_ast_free(node);
        return NULL;
    }

    node->as.readfile_stmt.path = expression(p);

    return node;
}

// WRITEFILE path, content [TRUNCATE|APPEND]
static c64script_ast_node_t *writefile_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_WRITEFILE;
    node->line = p->previous.line;

    node->as.writefile_stmt.path = expression(p);

    if (!match(p, TOKEN_COMMA)) {
        error(p, "Expected comma after path in WRITEFILE");
        c64script_ast_free(node);
        return NULL;
    }

    node->as.writefile_stmt.content = expression(p);
    node->as.writefile_stmt.truncate = false; // Default to append

    if (match(p, TOKEN_TRUNCATE)) {
        node->as.writefile_stmt.truncate = true;
    } else if (match(p, TOKEN_APPEND)) {
        node->as.writefile_stmt.truncate = false;
    }

    return node;
}

// HTTP <method> <url> [HEADERS <expr>] [BODY <expr>] [STATUS <var>] [RESPONSE <var>]
static c64script_ast_node_t *http_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_HTTP;
    node->line = p->previous.line;

    // Parse HTTP method
    int method = -1;
    if (match(p, TOKEN_GET)) {
        method = 0;
    } else if (match(p, TOKEN_POST)) {
        method = 1;
    } else if (match(p, TOKEN_PUT)) {
        method = 2;
    } else if (match(p, TOKEN_DELETE)) {
        method = 3;
    } else if (match(p, TOKEN_PATCH)) {
        method = 4;
    } else {
        error(p, "Expected HTTP method (GET, POST, PUT, DELETE, PATCH)");
        c64script_ast_free(node);
        return NULL;
    }
    node->as.http_stmt.method = method;

    // Parse URL expression
    node->as.http_stmt.url = expression(p);
    node->as.http_stmt.headers = NULL;
    node->as.http_stmt.body = NULL;
    node->as.http_stmt.status_var = NULL;
    node->as.http_stmt.response_var = NULL;

    // Parse optional parameters in any order
    while (true) {
        if (match(p, TOKEN_HEADERS)) {
            node->as.http_stmt.headers = expression(p);
        } else if (match(p, TOKEN_BODY)) {
            node->as.http_stmt.body = expression(p);
        } else if (match(p, TOKEN_STATUS)) {
            node->as.http_stmt.status_var = expression(p);
        } else if (match(p, TOKEN_RESPONSE)) {
            node->as.http_stmt.response_var = expression(p);
        } else {
            break;
        }
    }

    return node;
}

static c64script_ast_node_t *statement(parser_t *p)
{
    bool skipped_newlines = false;
    while (match(p, TOKEN_NEWLINE)) {
        skipped_newlines = true;
    }

    if (check(p, TOKEN_EOF))
        return NULL;

    bool allow_line_label = skipped_newlines || p->previous.type == TOKEN_EOF;

    // Optional line prefix label/line-number, possibly followed by a statement on the same line.
    if (allow_line_label && (check(p, TOKEN_IDENTIFIER) || check(p, TOKEN_NUMBER))) {
        c64script_token_t first = p->current;
        c64script_token_t second = peek_token(p);

        bool is_label = false;
        if (first.type == TOKEN_NUMBER) {
            is_label = memchr(first.start, '.', first.length) == NULL;
        } else if (first.type == TOKEN_IDENTIFIER) {
            // Check if this is a label or an assignment
            // It's a label if followed by : or not followed by = or (
            // It's an assignment if followed by = or (
            if (second.type == TOKEN_COLON) {
                is_label = true;
            } else if (second.type == TOKEN_EQ || second.type == TOKEN_LPAREN || second.type == TOKEN_LBRACE) {
                // Could be assignment: x = val, arr(i) = val, map{k} = val
                is_label = false;
            } else {
                // Otherwise assume it's a label
                is_label = true;
            }
        }

        if (is_label) {
            c64script_ast_node_t *label_node = calloc(1, sizeof(c64script_ast_node_t));
            if (!label_node)
                return NULL;
            label_node->type = AST_STMT_LABEL;
            label_node->line = first.line;

            advance(p); // consume label token
            if (first.type == TOKEN_NUMBER) {
                label_node->as.label.name = dup_normalized_label_from_number(&first);
                if (!label_node->as.label.name) {
                    error(p, "Invalid line number");
                    free(label_node);
                    return NULL;
                }
            } else {
                label_node->as.label.name = dup_upper(first.start, first.length);
            }

            match(p, TOKEN_COLON);

            if (check(p, TOKEN_NEWLINE) || check(p, TOKEN_EOF)) {
                return label_node;
            }

            c64script_ast_node_t *next_stmt = statement(p);
            if (next_stmt) {
                label_node->next = next_stmt;
            }
            return label_node;
        }
    }

    // Label (compatibility statement)
    if (match(p, TOKEN_LABEL_KW)) {
        return label_statement(p);
    }

    // Keywords
    if (match(p, TOKEN_LET)) {
        // LET variable = value (including array/map assignments)
        if (!match(p, TOKEN_IDENTIFIER)) {
            error(p, "Expected identifier after LET");
            return NULL;
        }
        c64script_ast_expr_t *target = variable(p, false);
        if (!target) {
            error(p, "Expected variable, array, or map access after LET");
            return NULL;
        }
        if (!check(p, TOKEN_EQ)) {
            error(p, "Expected '=' after LET target");
            free_expr(target);
            return NULL;
        }
        return assignment_statement(p, target);
    }

    if (match(p, TOKEN_REM)) {
        return rem_statement(p);
    }

    if (match(p, TOKEN_DIM)) {
        return dim_statement(p);
    }

    if (match(p, TOKEN_FUN)) {
        return function_def_statement(p);
    }

    // Check for assignment: identifier = value OR identifier(...) = value OR identifier{...} = value
    // We need to handle: x=val, arr(i)=val, map{k}=val
    if (check(p, TOKEN_IDENTIFIER)) {
        // Save position before trying to parse
        c64script_token_t saved_current = p->current;
        c64script_token_t saved_previous = p->previous;

        // Consume the identifier
        advance(p);

        // Parse the left-hand side (variable, array access, or map access)
        c64script_ast_expr_t *target = variable(p, false);
        if (target && check(p, TOKEN_EQ)) {
            // It's an assignment
            return assignment_statement(p, target);
        }

        // Not an assignment - restore parser state and continue
        if (target) {
            free_expr(target);
        }
        p->current = saved_current;
        p->previous = saved_previous;
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
    if (match(p, TOKEN_PALETTECOLOR))
        return palettecolor_statement(p);
    if (match(p, TOKEN_PLAYSID))
        return playsid_statement(p);
    if (match(p, TOKEN_RUNPRG))
        return runprg_statement(p);
    if (match(p, TOKEN_RUNLOCAL))
        return runlocal_statement(p);
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
    if (match(p, TOKEN_TYPE_KEYWORD))
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
    if (match(p, TOKEN_READFILE))
        return readfile_statement(p);
    if (match(p, TOKEN_WRITEFILE))
        return writefile_statement(p);
    if (match(p, TOKEN_HTTP))
        return http_statement(p);

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
    return c64script_parse_with_options(source, source_size, error_msg, error_msg_size, NULL);
}

c64script_ast_node_t *c64script_parse_with_options(const char *source, size_t source_size, char *error_msg,
                                                   size_t error_msg_size, const c64script_parse_options_t *options)
{
    c64script_tokenizer_t tokenizer_obj;
    c64script_tokenizer_init(&tokenizer_obj, source, source_size);

    parser_t parser = {0};
    parser.tokenizer = &tokenizer_obj;
    parser.had_error = false;
    parser.had_any_error = false;
    parser.panic_mode = false;
    parser.error_count = 0;
    parser.max_errors = 50; // Limit to 50 errors to prevent runaway scenarios
    parser.log_errors = options ? options->log_errors : true;

    // Get first token
    advance(&parser);

    // Parse program as list of statements
    c64script_ast_node_t *root = NULL;
    c64script_ast_node_t *tail = NULL;

    while (!match(&parser, TOKEN_EOF)) {
        // Stop parsing if we've hit the error limit
        if (parser.max_errors > 0 && parser.error_count >= parser.max_errors) {
            break;
        }

        c64script_ast_node_t *decl = declaration(&parser);
        if (decl) {
            if (!root) {
                root = decl;
                tail = decl;
            } else {
                tail->next = decl;
                tail = decl;
            }

            while (tail->next) {
                tail = tail->next;
            }
        }

        if (parser.had_error) {
            // Don't synchronize if we've hit the error limit
            if (parser.max_errors == 0 || parser.error_count < parser.max_errors) {
                synchronize(&parser);
            }
            parser.had_error = false;
            parser.panic_mode = false;

            // Double-check error limit after synchronize
            if (parser.max_errors > 0 && parser.error_count >= parser.max_errors) {
                break;
            }
        }
    }

    if (parser.had_any_error && error_msg) {
        snprintf(error_msg, error_msg_size, "%s", parser.error_msg);
    }

    // Tokenizer is stack-allocated, no cleanup needed

    if (parser.had_any_error) {
        c64script_ast_free(root);
        return NULL;
    }

    return root;
}
static c64script_ast_node_t *dim_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_DIM;
    node->line = p->previous.line;

    if (!match(p, TOKEN_IDENTIFIER)) {
        error(p, "Expected array name after DIM");
        free(node);
        return NULL;
    }

    node->as.dim_stmt.array_name = dup_upper(p->previous.start, p->previous.length);

    consume(p, TOKEN_LPAREN, "Expected '(' after array name");
    if (p->panic_mode) {
        free(node);
        return NULL;
    }

    node->as.dim_stmt.size = expression(p);

    consume(p, TOKEN_RPAREN, "Expected ')' after array size");
    if (p->panic_mode) {
        free(node);
        return NULL;
    }

    return node;
}

// ============================================================================
// NEW: FUNCTION DEFINITION PARSING
// ============================================================================

// FUN identifier[(param, ...)] ... ENDFUN
static c64script_ast_node_t *function_def_statement(parser_t *p)
{
    c64script_ast_node_t *node = calloc(1, sizeof(c64script_ast_node_t));
    if (!node)
        return NULL;
    node->type = AST_STMT_FUNCTION_DEF;
    node->line = p->previous.line;

    if (!match(p, TOKEN_IDENTIFIER)) {
        error(p, "Expected function name after FUN");
        free(node);
        return NULL;
    }

    node->as.function_def.name = dup_upper(p->previous.start, p->previous.length);
    node->as.function_def.param_names = NULL;
    node->as.function_def.param_count = 0;
    node->as.function_def.body = NULL;

    // Optional parameters
    if (match(p, TOKEN_LPAREN)) {
        size_t capacity = 4;
        char **params = malloc(capacity * sizeof(char *));
        if (!params) {
            c64script_ast_free(node);
            return NULL;
        }

        // Parse parameters
        if (!check(p, TOKEN_RPAREN)) {
            do {
                if (!match(p, TOKEN_IDENTIFIER)) {
                    error(p, "Expected parameter name");
                    for (size_t i = 0; i < node->as.function_def.param_count; i++) {
                        free(params[i]);
                    }
                    free(params);
                    c64script_ast_free(node);
                    return NULL;
                }

                if (node->as.function_def.param_count >= capacity) {
                    capacity *= 2;
                    char **new_params = realloc(params, capacity * sizeof(char *));
                    if (!new_params) {
                        for (size_t i = 0; i < node->as.function_def.param_count; i++) {
                            free(params[i]);
                        }
                        free(params);
                        c64script_ast_free(node);
                        return NULL;
                    }
                    params = new_params;
                }

                params[node->as.function_def.param_count++] = dup_upper(p->previous.start, p->previous.length);

            } while (match(p, TOKEN_COMMA));
        }

        node->as.function_def.param_names = (const char **)params;

        consume(p, TOKEN_RPAREN, "Expected ')' after parameters");
        if (p->panic_mode) {
            c64script_ast_free(node);
            return NULL;
        }
    }

    // Expect newline after function header
    if (!match(p, TOKEN_NEWLINE)) {
        error(p, "Expected newline after function header");
        c64script_ast_free(node);
        return NULL;
    }

    // Parse function body
    c64script_ast_node_t *body_head = NULL;
    c64script_ast_node_t *body_tail = NULL;

    while (!check(p, TOKEN_ENDFUN) && !check(p, TOKEN_EOF)) {
        // Check for "END FUN"
        if (check(p, TOKEN_END)) {
            c64script_token_t next = peek_token(p);
            if (next.type == TOKEN_FUN) {
                break;
            }
        }

        // Skip empty lines
        if (match(p, TOKEN_NEWLINE)) {
            continue;
        }

        c64script_ast_node_t *stmt = statement(p);
        if (stmt) {
            if (!body_head) {
                body_head = stmt;
                body_tail = stmt;
            } else {
                body_tail->next = stmt;
                body_tail = stmt;
            }

            while (body_tail->next) {
                body_tail = body_tail->next;
            }
        }

        if (p->had_error) {
            c64script_ast_free(node);
            c64script_ast_free(body_head);
            return NULL;
        }
    }

    node->as.function_def.body = body_head;

    // Consume ENDFUN or END FUN
    if (match(p, TOKEN_ENDFUN)) {
        // Done
    } else if (match(p, TOKEN_END)) {
        consume(p, TOKEN_FUN, "Expected FUN after END");
        if (p->panic_mode) {
        }
    } else {
        error(p, "Expected ENDFUN or END FUN");
        c64script_ast_free(node);
        return NULL;
    }

    return node;
}

// ============================================================================
// Expression memory management
// ============================================================================

static void free_expr(c64script_ast_expr_t *expr)
{
    if (!expr)
        return;

    switch (expr->type) {
    case AST_EXPR_STRING:
        // String is in string pool, don't free
        break;
    case AST_EXPR_IDENTIFIER:
        free((char *)expr->as.identifier);
        break;
    case AST_EXPR_ARRAY_ACCESS:
        free((char *)expr->as.array_access.name);
        free_expr(expr->as.array_access.index);
        break;
    case AST_EXPR_MAP_ACCESS:
        free((char *)expr->as.map_access.name);
        free_expr(expr->as.map_access.key);
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
        free(expr->as.call.args);
        break;
    default:
        break;
    }
    free(expr);
}
