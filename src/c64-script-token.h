/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

#pragma once

#include "c64-script.h"

/**
 * Tokenizer for C64Script language
 *
 * Converts source text into tokens for parsing.
 */

typedef struct {
    const char *source;
    size_t source_size;
    size_t current;
    int line;
    int column;
    size_t line_start_pos;
    char error[512];
} c64script_tokenizer_t;

/**
 * Initialize a tokenizer
 */
void c64script_tokenizer_init(c64script_tokenizer_t *tokenizer, const char *source, size_t source_size);

/**
 * Get the next token
 * Returns TOKEN_EOF at end of input
 * Returns TOKEN_ERROR on tokenization error
 */
c64script_token_t c64script_tokenizer_next(c64script_tokenizer_t *tokenizer);

/**
 * Peek at the next token without consuming it
 */
c64script_token_t c64script_tokenizer_peek(c64script_tokenizer_t *tokenizer);
