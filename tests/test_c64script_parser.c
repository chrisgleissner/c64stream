/*
C64 Stream - An OBS Studio source plugin for Commodore 64 video and audio streaming
Copyright (C) 2025 Christian Gleissner

Licensed under the GNU General Public License v2.0 or later.
See <https://www.gnu.org/licenses/> for details.
*/

// Ensure asserts are always enabled in tests
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "../src/c64-script.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                                                                 \
    do {                                                                                                               \
        printf("Running test: %s ... ", #name);                                                                        \
        fflush(stdout);                                                                                                \
        test_##name();                                                                                                 \
        printf("OK\n");                                                                                                \
    } while (0)

// ============================================================================
// EXPRESSION PARSING TESTS
// ============================================================================

TEST(parse_number_literal)
{
    const char *source = "42";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));

    // For now, just test that parsing doesn't crash
    // Full AST validation will come once statements are properly handled
    (void)ast;
}

TEST(parse_string_literal)
{
    const char *source = "\"hello world\"";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    (void)ast;
}

TEST(parse_identifier)
{
    const char *source = "x";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    (void)ast;
}

TEST(parse_addition)
{
    const char *source = "x = 1 + 2";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse addition");
    assert(ast->type == AST_STMT_ASSIGNMENT && "Expected assignment statement");
}

TEST(parse_subtraction)
{
    const char *source = "x = 5 - 3";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse subtraction");
    assert(ast->type == AST_STMT_ASSIGNMENT);
}

TEST(parse_multiplication)
{
    const char *source = "x = 4 * 3";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse multiplication");
    assert(ast->type == AST_STMT_ASSIGNMENT);
}

TEST(parse_division)
{
    const char *source = "x = 12 / 4";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse division");
    assert(ast->type == AST_STMT_ASSIGNMENT);
}

TEST(parse_operator_precedence_multiply_before_add)
{
    const char *source = "x = 2 + 3 * 4"; // Should parse as 2 + (3 * 4) = 14, not (2 + 3) * 4 = 20
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse expression with precedence");
    assert(ast->type == AST_STMT_ASSIGNMENT);

    // Verify it's a binary expression with correct structure
    assert(ast->as.assignment.value != NULL);
    assert(ast->as.assignment.value->type == AST_EXPR_BINARY);
    // Left should be number 2, operator ADD, right should be (3 * 4)
    assert(ast->as.assignment.value->as.binary.op == EXPR_OP_ADD);
}

TEST(parse_parentheses)
{
    const char *source = "x = (2 + 3) * 4"; // Should parse as (2 + 3) * 4 = 20
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse parenthesized expression");
    assert(ast->type == AST_STMT_ASSIGNMENT);
}

TEST(parse_unary_minus)
{
    const char *source = "x = -5";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse unary minus");
    assert(ast->type == AST_STMT_ASSIGNMENT);
    assert(ast->as.assignment.value->type == AST_EXPR_UNARY);
    assert(ast->as.assignment.value->as.unary.op == EXPR_OP_NEGATE);
}

TEST(parse_comparison_equal)
{
    const char *source = "x = a = b"; // a = b returns a boolean
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse equality comparison");
    assert(ast->type == AST_STMT_ASSIGNMENT);
}

TEST(parse_comparison_not_equal)
{
    const char *source = "x = a <> b";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse not-equal comparison");
    assert(ast->type == AST_STMT_ASSIGNMENT);
}

TEST(parse_comparison_less_than)
{
    const char *source = "x = a < b";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse less-than comparison");
    assert(ast->type == AST_STMT_ASSIGNMENT);
}

TEST(parse_boolean_and)
{
    const char *source = "x = a AND b";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse AND expression");
    assert(ast->type == AST_STMT_ASSIGNMENT);
}

TEST(parse_boolean_or)
{
    const char *source = "x = a OR b";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse OR expression");
    assert(ast->type == AST_STMT_ASSIGNMENT);
}

TEST(parse_boolean_not)
{
    const char *source = "x = NOT a";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse NOT expression");
    assert(ast->type == AST_STMT_ASSIGNMENT);
    assert(ast->as.assignment.value->type == AST_EXPR_UNARY);
    assert(ast->as.assignment.value->as.unary.op == EXPR_OP_NOT);
}

// ============================================================================
// STATEMENT PARSING TESTS
// ============================================================================

TEST(parse_assignment)
{
    const char *source = "x = 42";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse assignment");
    assert(ast->type == AST_STMT_ASSIGNMENT);
    assert(strcmp(ast->as.assignment.variable, "x") == 0);
}

TEST(parse_assignment_with_let)
{
    const char *source = "LET x = 42";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse LET assignment");
    assert(ast->type == AST_STMT_ASSIGNMENT);
    assert(strcmp(ast->as.assignment.variable, "x") == 0);
}

TEST(parse_label_with_keyword)
{
    const char *source = "LABEL start";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse LABEL statement");
    assert(ast->type == AST_STMT_LABEL);
    assert(strcmp(ast->as.label.name, "start") == 0);
}

TEST(parse_label_with_colon)
{
    const char *source = "start:";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse label with colon");
    assert(ast->type == AST_STMT_LABEL);
    assert(strcmp(ast->as.label.name, "start") == 0);
}

TEST(parse_goto)
{
    const char *source = "GOTO start";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse GOTO");
    assert(ast->type == AST_STMT_GOTO);
    assert(strcmp(ast->as.goto_stmt.label, "start") == 0);
}

TEST(parse_gosub)
{
    const char *source = "GOSUB subroutine";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse GOSUB");
    assert(ast->type == AST_STMT_GOSUB);
    assert(strcmp(ast->as.gosub_stmt.label, "subroutine") == 0);
}

TEST(parse_return)
{
    const char *source = "RETURN";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse RETURN");
    assert(ast->type == AST_STMT_RETURN);
}

TEST(parse_stop)
{
    const char *source = "STOP";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse STOP");
    assert(ast->type == AST_STMT_STOP);
}

TEST(parse_end)
{
    const char *source = "END";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse END");
    assert(ast->type == AST_STMT_STOP);
}

TEST(parse_multiple_statements)
{
    const char *source = "x = 1\ny = 2\nz = 3";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse multiple statements");

    // Count statements
    int count = 0;
    c64script_ast_node_t *node = ast;
    while (node) {
        count++;
        node = node->next;
    }
    assert(count == 3 && "Expected 3 statements");
}

TEST(parse_empty_lines_and_comments)
{
    const char *source = "# Comment\n\nx = 1\n\n# Another comment\ny = 2";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse with comments");

    // Count actual statements (should skip comments and empty lines)
    int count = 0;
    c64script_ast_node_t *node = ast;
    while (node) {
        count++;
        node = node->next;
    }
    assert(count == 2 && "Expected 2 statements (comments should be skipped)");
}

// ============================================================================
// ERROR HANDLING TESTS
// ============================================================================

TEST(parse_error_unterminated_string)
{
    const char *source = "x = \"unterminated";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    // Should fail to parse
    (void)ast; // May be NULL or have partial tree
}

TEST(parse_error_invalid_operator)
{
    const char *source = "x = 1 @ 2"; // @ is not a valid operator
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    // Should fail or skip the invalid operator
    (void)ast;
}

// ============================================================================
// CONTROL FLOW TESTS
// ============================================================================

TEST(parse_if_then_single_line)
{
    const char *source = "IF x > 0 THEN y = 1";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse IF/THEN");
    assert(ast->type == AST_STMT_IF);
    assert(ast->as.if_stmt.condition != NULL);
    assert(ast->as.if_stmt.then_branch != NULL);
    assert(ast->as.if_stmt.else_branch == NULL);
}

TEST(parse_if_then_else_single_line)
{
    const char *source = "IF x > 0 THEN y = 1 ELSE y = 0";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse IF/THEN/ELSE");
    assert(ast->type == AST_STMT_IF);
    assert(ast->as.if_stmt.condition != NULL);
    assert(ast->as.if_stmt.then_branch != NULL);
    assert(ast->as.if_stmt.else_branch != NULL);
}

TEST(parse_if_block)
{
    const char *source = "IF x > 0 THEN\ny = 1\nz = 2\nENDIF";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse IF block");
    assert(ast->type == AST_STMT_IF);
    assert(ast->as.if_stmt.then_branch != NULL);
}

TEST(parse_if_else_block)
{
    const char *source = "IF x > 0 THEN\ny = 1\nELSE\ny = 0\nENDIF";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse IF/ELSE block");
    assert(ast->type == AST_STMT_IF);
    assert(ast->as.if_stmt.then_branch != NULL);
    assert(ast->as.if_stmt.else_branch != NULL);
}

TEST(parse_for_loop)
{
    const char *source = "FOR i = 1 TO 10\nPRINT i\nNEXT";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse FOR loop");
    assert(ast->type == AST_STMT_FOR);
    assert(ast->as.for_stmt.variable != NULL);
    assert(ast->as.for_stmt.start != NULL);
    assert(ast->as.for_stmt.end != NULL);
    assert(ast->as.for_stmt.step != NULL);
}

TEST(parse_for_loop_with_step)
{
    const char *source = "FOR i = 10 TO 1 STEP -1\nPRINT i\nNEXT i";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse FOR loop with STEP");
    assert(ast->type == AST_STMT_FOR);
}

TEST(parse_while_loop)
{
    const char *source = "WHILE x < 10\nx = x + 1\nWEND";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse WHILE loop");
    assert(ast->type == AST_STMT_WHILE);
    assert(ast->as.while_stmt.condition != NULL);
    assert(ast->as.while_stmt.body != NULL);
}

TEST(parse_while_endwhile)
{
    const char *source = "WHILE x < 10\nx = x + 1\nENDWHILE";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse WHILE/ENDWHILE");
    assert(ast->type == AST_STMT_WHILE);
}

TEST(parse_wait_duration)
{
    const char *source = "WAIT 500";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse WAIT");
    assert(ast->type == AST_STMT_WAIT);
}

TEST(parse_wait_until)
{
    const char *source = "WAIT UNTIL x > 0";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse WAIT UNTIL");
    assert(ast->type == AST_STMT_WAIT_UNTIL);
}

// ============================================================================
// PLUGIN ACTION TESTS
// ============================================================================

TEST(parse_effect)
{
    const char *source = "EFFECT \"scanlines\"";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse EFFECT");
    assert(ast->type == AST_STMT_EFFECT);
}

TEST(parse_effectparam)
{
    const char *source = "EFFECTPARAM \"intensity\" 0.5";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse EFFECTPARAM");
    assert(ast->type == AST_STMT_EFFECTPARAM);
}

TEST(parse_palette)
{
    const char *source = "PALETTE \"pepto\"";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse PALETTE");
    assert(ast->type == AST_STMT_PALETTE);
}

TEST(parse_playsid)
{
    const char *source = "PLAYSID \"music.sid\"";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse PLAYSID");
    assert(ast->type == AST_STMT_PLAYSID);
}

TEST(parse_runprg)
{
    const char *source = "RUNPRG \"demo.prg\"";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse RUNPRG");
    assert(ast->type == AST_STMT_RUNPRG);
}

TEST(parse_mountdisk)
{
    const char *source = "MOUNTDISK \"game.d64\"";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse MOUNTDISK");
    assert(ast->type == AST_STMT_MOUNTDISK);
}

TEST(parse_autostart)
{
    const char *source = "AUTOSTART \"demo.prg\"";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse AUTOSTART");
    assert(ast->type == AST_STMT_AUTOSTART);
}

TEST(parse_reset)
{
    const char *source = "RESET";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse RESET");
    assert(ast->type == AST_STMT_RESET);
}

TEST(parse_reboot)
{
    const char *source = "REBOOT";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse REBOOT");
    assert(ast->type == AST_STMT_REBOOT);
}

TEST(parse_recordstart)
{
    const char *source = "RECORDSTART";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse RECORDSTART");
    assert(ast->type == AST_STMT_RECORDSTART);
}

TEST(parse_recordstop)
{
    const char *source = "RECORDSTOP";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse RECORDSTOP");
    assert(ast->type == AST_STMT_RECORDSTOP);
}

TEST(parse_type)
{
    const char *source = "TYPE \"LOAD\\\"*\\\",8,1\\r\"";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse TYPE");
    assert(ast->type == AST_STMT_TYPE);
}

TEST(parse_key)
{
    const char *source = "KEY 13";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse KEY");
    assert(ast->type == AST_STMT_KEY);
}

TEST(parse_poke)
{
    const char *source = "POKE $D020, 0";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse POKE");
    assert(ast->type == AST_STMT_POKE);
}

TEST(parse_logfile)
{
    const char *source = "LOGFILE \"script.log\"";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse LOGFILE");
    assert(ast->type == AST_STMT_LOGFILE);
}

TEST(parse_log)
{
    const char *source = "LOG \"Starting script\"";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse LOG");
    assert(ast->type == AST_STMT_LOG);
}

TEST(parse_print)
{
    const char *source = "PRINT x";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse PRINT");
    assert(ast->type == AST_STMT_PRINT);
}

TEST(parse_tron)
{
    const char *source = "TRON";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse TRON");
    assert(ast->type == AST_STMT_TRON);
}

TEST(parse_troff)
{
    const char *source = "TROFF";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse TROFF");
    assert(ast->type == AST_STMT_TROFF);
}

// ============================================================================
// ERROR HANDLING TESTS
// ============================================================================

TEST(parse_full_program)
{
    const char *source = "# Simple program\n"
                         "LABEL start\n"
                         "x = 0\n"
                         "LABEL loop\n"
                         "x = x + 1\n"
                         "GOTO loop\n";

    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse full program");

    // Verify structure
    assert(ast->type == AST_STMT_LABEL);
    assert(ast->next != NULL);
    assert(ast->next->type == AST_STMT_ASSIGNMENT);
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================

int main(void)
{
    printf("=== C64Script Parser Tests ===\n\n");

    printf("--- Expression Parsing Tests ---\n");
    RUN_TEST(parse_number_literal);
    RUN_TEST(parse_string_literal);
    RUN_TEST(parse_identifier);
    RUN_TEST(parse_addition);
    RUN_TEST(parse_subtraction);
    RUN_TEST(parse_multiplication);
    RUN_TEST(parse_division);
    RUN_TEST(parse_operator_precedence_multiply_before_add);
    RUN_TEST(parse_parentheses);
    RUN_TEST(parse_unary_minus);
    RUN_TEST(parse_comparison_equal);
    RUN_TEST(parse_comparison_not_equal);
    RUN_TEST(parse_comparison_less_than);
    RUN_TEST(parse_boolean_and);
    RUN_TEST(parse_boolean_or);
    RUN_TEST(parse_boolean_not);

    printf("\n--- Statement Parsing Tests ---\n");
    RUN_TEST(parse_assignment);
    RUN_TEST(parse_assignment_with_let);
    RUN_TEST(parse_label_with_keyword);
    RUN_TEST(parse_label_with_colon);
    RUN_TEST(parse_goto);
    RUN_TEST(parse_gosub);
    RUN_TEST(parse_return);
    RUN_TEST(parse_stop);
    RUN_TEST(parse_end);
    RUN_TEST(parse_multiple_statements);
    RUN_TEST(parse_empty_lines_and_comments);

    printf("\n--- Control Flow Tests ---\n");
    RUN_TEST(parse_if_then_single_line);
    RUN_TEST(parse_if_then_else_single_line);
    RUN_TEST(parse_if_block);
    RUN_TEST(parse_if_else_block);
    RUN_TEST(parse_for_loop);
    RUN_TEST(parse_for_loop_with_step);
    RUN_TEST(parse_while_loop);
    RUN_TEST(parse_while_endwhile);
    RUN_TEST(parse_wait_duration);
    RUN_TEST(parse_wait_until);

    printf("\n--- Plugin Action Tests ---\n");
    RUN_TEST(parse_effect);
    RUN_TEST(parse_effectparam);
    RUN_TEST(parse_palette);
    RUN_TEST(parse_playsid);
    RUN_TEST(parse_runprg);
    RUN_TEST(parse_mountdisk);
    RUN_TEST(parse_autostart);
    RUN_TEST(parse_reset);
    RUN_TEST(parse_reboot);
    RUN_TEST(parse_recordstart);
    RUN_TEST(parse_recordstop);
    RUN_TEST(parse_type);
    RUN_TEST(parse_key);
    RUN_TEST(parse_poke);
    RUN_TEST(parse_logfile);
    RUN_TEST(parse_log);
    RUN_TEST(parse_print);
    RUN_TEST(parse_tron);
    RUN_TEST(parse_troff);

    printf("\n--- Error Handling Tests ---\n");
    RUN_TEST(parse_error_unterminated_string);
    RUN_TEST(parse_error_invalid_operator);

    printf("\n--- Integration Tests ---\n");
    RUN_TEST(parse_full_program);

    printf("\n=== All Parser Tests Passed! ===\n");
    return 0;
}
