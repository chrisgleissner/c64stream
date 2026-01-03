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

#define MACRO_LOG_PREFIX "[c64script-token] "

// ============================================================================
// KEYWORD TABLE
// ============================================================================

typedef struct {
    const char *keyword;
    c64script_token_type_t type;
} keyword_entry_t;

// Keywords are case-insensitive
static const keyword_entry_t keywords[] = {
    // Control flow
    {"IF", TOKEN_IF},
    {"THEN", TOKEN_THEN},
    {"ELSE", TOKEN_ELSE},
    {"ENDIF", TOKEN_ENDIF},
    {"FOR", TOKEN_FOR},
    {"TO", TOKEN_TO},
    {"STEP", TOKEN_STEP},
    {"NEXT", TOKEN_NEXT},
    {"WHILE", TOKEN_WHILE},
    {"WEND", TOKEN_WEND},
    {"ENDWHILE", TOKEN_ENDWHILE},
    {"END", TOKEN_END},
    {"GOTO", TOKEN_GOTO},
    {"GOSUB", TOKEN_GOSUB},
    {"RETURN", TOKEN_RETURN},
    {"STOP", TOKEN_STOP},
    {"LABEL", TOKEN_LABEL_KW},

    // Variables
    {"LET", TOKEN_LET},

    // Comments
    {"REM", TOKEN_REM},

    // Waiting
    {"WAIT", TOKEN_WAIT},
    {"UNTIL", TOKEN_UNTIL},

    // Plugin actions
    {"EFFECT", TOKEN_EFFECT},
    {"EFFECTPARAM", TOKEN_EFFECTPARAM},
    {"PALETTE", TOKEN_PALETTE},
    {"PALETTECOLOR", TOKEN_PALETTECOLOR},
    {"PALETTE_COLOR", TOKEN_PALETTECOLOR}, // Alias
    {"PLAYSID", TOKEN_PLAYSID},
    {"PLAY_SID", TOKEN_PLAYSID}, // Alias
    {"RUNPRG", TOKEN_RUNPRG},
    {"RUN_PRG", TOKEN_RUNPRG}, // Alias
    {"MOUNTDISK", TOKEN_MOUNTDISK},
    {"MOUNT_DISK", TOKEN_MOUNTDISK}, // Alias
    {"AUTOSTART", TOKEN_AUTOSTART},
    {"RESET", TOKEN_RESET},
    {"REBOOT", TOKEN_REBOOT},
    {"RECORDSTART", TOKEN_RECORDSTART},
    {"RECORD_START", TOKEN_RECORDSTART}, // Alias
    {"RECORDSTOP", TOKEN_RECORDSTOP},
    {"RECORD_STOP", TOKEN_RECORDSTOP}, // Alias

    // I/O
    {"TYPE", TOKEN_TYPE_KEYWORD},
    {"KEY", TOKEN_KEY},
    {"POKE", TOKEN_POKE},
    {"PEEK", TOKEN_PEEK},
    {"PRINT", TOKEN_PRINT},
    {"LOG", TOKEN_LOG},
    {"LOGFILE", TOKEN_LOGFILE},
    {"TRON", TOKEN_TRON},
    {"TROFF", TOKEN_TROFF},

    // Parameters
    {"APPEND", TOKEN_APPEND},
    {"TRUNCATE", TOKEN_TRUNCATE},
    {"SONGNR", TOKEN_SONGNR},

    // Boolean operators
    {"NOT", TOKEN_NOT},
    {"AND", TOKEN_AND},
    {"XOR", TOKEN_XOR},
    {"OR", TOKEN_OR},
};

#define KEYWORD_COUNT (sizeof(keywords) / sizeof(keywords[0]))

// ============================================================================
// TOKENIZER STATE
// ============================================================================

typedef struct {
    const char *source;
    size_t source_size;
    size_t pos;
    int line;
    int column;
    int line_start_pos; // Position of start of current line
    char error_msg[1024];
} tokenizer_t;

// ============================================================================
// CHARACTER CLASSIFICATION
// ============================================================================

static bool is_alpha(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool is_hex_digit(char c)
{
    return is_digit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

static bool is_whitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static bool is_identifier_start(char c)
{
    return is_alpha(c);
}

static bool is_identifier_continue(char c)
{
    return is_alpha(c) || is_digit(c) || c == '_';
}

// ============================================================================
// TOKENIZER HELPERS
// ============================================================================

static char peek(tokenizer_t *t)
{
    if (t->pos >= t->source_size) {
        return '\0';
    }
    return t->source[t->pos];
}

static char peek_next(tokenizer_t *t)
{
    if (t->pos + 1 >= t->source_size) {
        return '\0';
    }
    return t->source[t->pos + 1];
}

static char advance(tokenizer_t *t)
{
    if (t->pos >= t->source_size) {
        return '\0';
    }
    char c = t->source[t->pos++];
    t->column++;
    if (c == '\n') {
        t->line++;
        t->column = 1;
        t->line_start_pos = (int)t->pos;
    }
    return c;
}

static void skip_whitespace(tokenizer_t *t)
{
    while (is_whitespace(peek(t))) {
        advance(t);
    }
}

static void skip_line(tokenizer_t *t)
{
    while (peek(t) != '\n' && peek(t) != '\0') {
        advance(t);
    }
}

// Case-insensitive string comparison
static bool str_eq_ci(const char *a, const char *b, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return false;
        }
    }
    return true;
}

// Check if identifier matches a keyword (case-insensitive)
static c64script_token_type_t lookup_keyword(const char *start, size_t length)
{
    for (size_t i = 0; i < KEYWORD_COUNT; i++) {
        size_t kw_len = strlen(keywords[i].keyword);
        if (kw_len == length && str_eq_ci(start, keywords[i].keyword, length)) {
            return keywords[i].type;
        }
    }
    return TOKEN_IDENTIFIER;
}

// ============================================================================
// TOKEN CREATION
// ============================================================================

static c64script_token_t make_token(tokenizer_t *t, c64script_token_type_t type, const char *start, size_t length)
{
    c64script_token_t token;
    token.type = type;
    token.line = t->line;
    token.column = t->column - (int)length;
    token.start = start;
    token.length = length;
    token.value.number = 0; // Default
    return token;
}

static c64script_token_t error_token_at(tokenizer_t *t, int column, const char *message)
{
    snprintf(t->error_msg, sizeof(t->error_msg), "Line %d: %s", t->line, message);
    c64script_token_t token;
    token.type = TOKEN_ERROR;
    token.line = t->line;
    token.column = column;
    token.start = message;
    token.length = strlen(message);
    token.value.number = 0;
    return token;
}

static c64script_token_t error_token(tokenizer_t *t, const char *message)
{
    return error_token_at(t, t->column, message);
}

// ============================================================================
// NUMBER TOKENIZATION
// ============================================================================

static c64script_token_t tokenize_number(tokenizer_t *t)
{
    const char *start = &t->source[t->pos];
    size_t length = 0;
    bool has_decimal = false;

    // Integer part
    while (is_digit(peek(t))) {
        advance(t);
        length++;
    }

    // Decimal part
    if (peek(t) == '.' && is_digit(peek_next(t))) {
        has_decimal = true;
        advance(t); // Skip '.'
        length++;
        while (is_digit(peek(t))) {
            advance(t);
            length++;
        }
    }

    // Duration suffix?
    char c = peek(t);
    if (c == 'm' && peek_next(t) == 's') {
        // milliseconds
        advance(t);
        advance(t);
        c64script_token_t token = make_token(t, TOKEN_DURATION, start, length + 2);
        char num_str[64];
        memcpy(num_str, start, length);
        num_str[length] = '\0';
        double value = atof(num_str);
        token.value.duration_ms = (uint32_t)(value);
        return token;
    } else if (c == 's') {
        // seconds
        advance(t);
        c64script_token_t token = make_token(t, TOKEN_DURATION, start, length + 1);
        char num_str[64];
        memcpy(num_str, start, length);
        num_str[length] = '\0';
        double value = atof(num_str);
        token.value.duration_ms = (uint32_t)(value * 1000.0);
        return token;
    } else if (c == 'm' && peek_next(t) != 's') {
        // minutes
        advance(t);
        c64script_token_t token = make_token(t, TOKEN_DURATION, start, length + 1);
        char num_str[64];
        memcpy(num_str, start, length);
        num_str[length] = '\0';
        double value = atof(num_str);
        token.value.duration_ms = (uint32_t)(value * 60000.0);
        return token;
    } else if (c == 'h') {
        // hours
        advance(t);
        c64script_token_t token = make_token(t, TOKEN_DURATION, start, length + 1);
        char num_str[64];
        memcpy(num_str, start, length);
        num_str[length] = '\0';
        double value = atof(num_str);
        token.value.duration_ms = (uint32_t)(value * 3600000.0);
        return token;
    } else if (c == 'd') {
        // days
        advance(t);
        c64script_token_t token = make_token(t, TOKEN_DURATION, start, length + 1);
        char num_str[64];
        memcpy(num_str, start, length);
        num_str[length] = '\0';
        double value = atof(num_str);
        token.value.duration_ms = (uint32_t)(value * 86400000.0);
        return token;
    }

    // Regular number
    c64script_token_t token = make_token(t, TOKEN_NUMBER, start, length);
    char num_str[64];
    memcpy(num_str, start, length);
    num_str[length] = '\0';
    token.value.number = atof(num_str);
    return token;
}

static c64script_token_t tokenize_hex_number(tokenizer_t *t)
{
    const char *start = &t->source[t->pos];
    advance(t); // Skip '$'

    if (!is_hex_digit(peek(t))) {
        return error_token(t, "Invalid hex literal: expected hex digit after '$'");
    }

    size_t length = 1; // '$'
    uint32_t value = 0;

    while (is_hex_digit(peek(t))) {
        char c = advance(t);
        length++;
        value = value * 16;
        if (c >= '0' && c <= '9') {
            value += (c - '0');
        } else if (c >= 'A' && c <= 'F') {
            value += (c - 'A' + 10);
        } else if (c >= 'a' && c <= 'f') {
            value += (c - 'a' + 10);
        }
    }

    c64script_token_t token = make_token(t, TOKEN_HEX_NUMBER, start, length);
    token.value.number = (double)value;
    return token;
}

// ============================================================================
// STRING TOKENIZATION
// ============================================================================

static c64script_token_t tokenize_string(tokenizer_t *t)
{
    const char *start = &t->source[t->pos];
    advance(t); // Skip opening '"'

    size_t length = 1;

    while (true) {
        char c = peek(t);

        if (c == '\0' || c == '\n') {
            return error_token(t, "Unterminated string literal");
        }

        advance(t);
        length++;

        if (c == '"') {
            // Check for doubled quote (BASIC-style escape)
            if (peek(t) == '"') {
                advance(t);
                length++;
                continue;
            }
            // End of string
            break;
        }

        if (c == '\\') {
            // Backslash escape
            char next = peek(t);
            if (next == '\0' || next == '\n') {
                return error_token(t, "Unterminated escape sequence");
            }
            advance(t);
            length++;
        }
    }

    return make_token(t, TOKEN_STRING, start, length);
}

// ============================================================================
// IDENTIFIER TOKENIZATION
// ============================================================================

static c64script_token_t tokenize_identifier(tokenizer_t *t)
{
    const char *start = &t->source[t->pos];
    size_t length = 0;

    while (is_identifier_continue(peek(t))) {
        advance(t);
        length++;
    }

    // Type suffix?
    char c = peek(t);
    if (c == '$' || c == '%') {
        advance(t);
        length++;
    }

    // Check if it's a keyword
    c64script_token_type_t type = lookup_keyword(start, length);
    return make_token(t, type, start, length);
}

// ============================================================================
// MAIN TOKENIZER
// ============================================================================

c64script_token_t c64script_tokenize_next(tokenizer_t *t)
{
    skip_whitespace(t);

    if (t->pos >= t->source_size) {
        return make_token(t, TOKEN_EOF, &t->source[t->pos], 0);
    }

    char c = peek(t);

    // Newline
    if (c == '\n') {
        const char *start = &t->source[t->pos];
        advance(t);
        return make_token(t, TOKEN_NEWLINE, start, 1);
    }

    // Comment (# at start of line or after whitespace)
    if (c == '#') {
        skip_line(t);
        return c64script_tokenize_next(t); // Skip comment, get next token
    }

    // Numbers
    if (is_digit(c)) {
        return tokenize_number(t);
    }

    // Hex numbers ($C000)
    if (c == '$') {
        return tokenize_hex_number(t);
    }

    // Strings
    if (c == '"') {
        return tokenize_string(t);
    }

    // Identifiers and keywords
    if (is_identifier_start(c)) {
        return tokenize_identifier(t);
    }

    // Operators and delimiters
    const char *start = &t->source[t->pos];
    int start_column = t->column;
    advance(t);

    switch (c) {
    case '+':
        return make_token(t, TOKEN_PLUS, start, 1);
    case '-':
        return make_token(t, TOKEN_MINUS, start, 1);
    case '*':
        return make_token(t, TOKEN_MULTIPLY, start, 1);
    case '/':
        return make_token(t, TOKEN_DIVIDE, start, 1);
    case '(':
        return make_token(t, TOKEN_LPAREN, start, 1);
    case ')':
        return make_token(t, TOKEN_RPAREN, start, 1);
    case '[':
        return make_token(t, TOKEN_LBRACKET, start, 1);
    case ']':
        return make_token(t, TOKEN_RBRACKET, start, 1);
    case ',':
        return make_token(t, TOKEN_COMMA, start, 1);
    case ':':
        return make_token(t, TOKEN_COLON, start, 1);

    case '=':
        if (peek(t) == '=') {
            advance(t);
            return make_token(t, TOKEN_EQ_EQ, start, 2);
        }
        return make_token(t, TOKEN_EQ, start, 1);

    case '<':
        if (peek(t) == '>') {
            advance(t);
            return make_token(t, TOKEN_NE, start, 2);
        }
        if (peek(t) == '=') {
            advance(t);
            return make_token(t, TOKEN_LE, start, 2);
        }
        return make_token(t, TOKEN_LT, start, 1);

    case '>':
        if (peek(t) == '=') {
            advance(t);
            return make_token(t, TOKEN_GE, start, 2);
        }
        return make_token(t, TOKEN_GT, start, 1);

    case '!':
        if (peek(t) == '=') {
            advance(t);
            return make_token(t, TOKEN_NE_ALT, start, 2);
        }
        return error_token_at(t, start_column, "Unexpected character '!'");

    default:
        return error_token_at(t, start_column, "Unexpected character");
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================

tokenizer_t *c64script_tokenizer_create(const char *source, size_t source_size)
{
    tokenizer_t *t = calloc(1, sizeof(tokenizer_t));
    if (!t) {
        return NULL;
    }
    t->source = source;
    t->source_size = source_size;
    t->pos = 0;
    t->line = 1;
    t->column = 1;
    t->line_start_pos = 0;
    return t;
}

void c64script_tokenizer_destroy(tokenizer_t *t)
{
    free(t);
}

const char *c64script_tokenizer_error(tokenizer_t *t)
{
    return t->error_msg[0] ? t->error_msg : NULL;
}

// ============================================================================
// PUBLIC API WRAPPERS
// ============================================================================

void c64script_tokenizer_init(c64script_tokenizer_t *tokenizer, const char *source, size_t source_size)
{
    tokenizer->source = source;
    tokenizer->source_size = source_size;
    tokenizer->current = 0;
    tokenizer->line = 1;
    tokenizer->column = 1;
    tokenizer->error[0] = '\0';
}

c64script_token_t c64script_tokenizer_next(c64script_tokenizer_t *tokenizer)
{
    // Create a temporary internal tokenizer from the public struct
    tokenizer_t internal = {.source = tokenizer->source,
                            .source_size = tokenizer->source_size,
                            .pos = tokenizer->current,
                            .line = tokenizer->line,
                            .column = tokenizer->column,
                            .line_start_pos = 0, // Will be recalculated
                            .error_msg = {0}};

    c64script_token_t token = c64script_tokenize_next(&internal);

    // Update public tokenizer state
    tokenizer->current = internal.pos;
    tokenizer->line = internal.line;
    tokenizer->column = internal.column;
    if (internal.error_msg[0]) {
        snprintf(tokenizer->error, sizeof(tokenizer->error), "%s", internal.error_msg);
    }

    return token;
}

c64script_token_t c64script_tokenizer_peek(c64script_tokenizer_t *tokenizer)
{
    // Save current state
    size_t saved_current = tokenizer->current;
    int saved_line = tokenizer->line;
    int saved_column = tokenizer->column;

    // Get next token
    c64script_token_t token = c64script_tokenizer_next(tokenizer);

    // Restore state
    tokenizer->current = saved_current;
    tokenizer->line = saved_line;
    tokenizer->column = saved_column;

    return token;
}
