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
    const char *source = "X = 42\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse number literal assignment");
    assert(ast->type == AST_STMT_ASSIGNMENT);
    assert(ast->as.assignment.value != NULL);
    assert(ast->as.assignment.value->type == AST_EXPR_NUMBER);
    assert(ast->as.assignment.value->as.number == 42.0);
    c64script_ast_free(ast);
}

TEST(parse_string_literal)
{
    const char *source = "X$ = \"hello world\"\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse string literal assignment");
    assert(ast->type == AST_STMT_ASSIGNMENT);
    assert(ast->as.assignment.value != NULL);
    assert(ast->as.assignment.value->type == AST_EXPR_STRING);
    assert(strcmp(ast->as.assignment.value->as.string, "hello world") == 0);
    c64script_ast_free(ast);
}

TEST(parse_identifier)
{
    const char *source = "X = Y\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse identifier assignment");
    assert(ast->type == AST_STMT_ASSIGNMENT);
    assert(ast->as.assignment.value != NULL);
    assert(ast->as.assignment.value->type == AST_EXPR_IDENTIFIER);
    assert(strcmp(ast->as.assignment.value->as.identifier, "Y") == 0);
    c64script_ast_free(ast);
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
    assert(strcmp(ast->as.assignment.variable, "X") == 0);
    c64script_ast_free(ast);
}

TEST(parse_assignment_with_let)
{
    const char *source = "LET x = 42";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse LET assignment");
    assert(ast->type == AST_STMT_ASSIGNMENT);
    assert(strcmp(ast->as.assignment.variable, "X") == 0);
    c64script_ast_free(ast);
}

TEST(parse_label_with_keyword)
{
    const char *source = "LABEL start";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse LABEL statement");
    assert(ast->type == AST_STMT_LABEL);
    assert(strcmp(ast->as.label.name, "START") == 0);
    c64script_ast_free(ast);
}

TEST(parse_label_with_colon)
{
    const char *source = "start:";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse label with colon");
    assert(ast->type == AST_STMT_LABEL);
    assert(strcmp(ast->as.label.name, "START") == 0);
    c64script_ast_free(ast);
}

TEST(parse_label_prefix_same_line_no_colon)
{
    const char *source = "START I = 0\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL);
    assert(ast->type == AST_STMT_LABEL);
    assert(strcmp(ast->as.label.name, "START") == 0);
    assert(ast->next != NULL);
    assert(ast->next->type == AST_STMT_ASSIGNMENT);
    assert(strcmp(ast->next->as.assignment.variable, "I") == 0);
    c64script_ast_free(ast);
}

TEST(parse_line_number_prefix_same_line)
{
    const char *source = "0010 I = 0\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL);
    assert(ast->type == AST_STMT_LABEL);
    assert(strcmp(ast->as.label.name, "10") == 0);
    assert(ast->next != NULL);
    assert(ast->next->type == AST_STMT_ASSIGNMENT);
    c64script_ast_free(ast);
}

TEST(parse_line_number_goto)
{
    const char *source = "GOTO 10\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL);
    assert(ast->type == AST_STMT_GOTO);
    assert(strcmp(ast->as.goto_stmt.label, "10") == 0);
    c64script_ast_free(ast);
}

TEST(parse_goto)
{
    const char *source = "GOTO start";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse GOTO");
    assert(ast->type == AST_STMT_GOTO);
    assert(strcmp(ast->as.goto_stmt.label, "START") == 0);
    c64script_ast_free(ast);
}

TEST(parse_gosub)
{
    const char *source = "GOSUB subroutine";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse GOSUB");
    assert(ast->type == AST_STMT_GOSUB);
    assert(strcmp(ast->as.gosub_stmt.label, "SUBROUTINE") == 0);
    assert(ast->as.gosub_stmt.param_count == 0);
    c64script_ast_free(ast);
}

TEST(parse_gosub_with_params)
{
    const char *source = "GOSUB MULTIPLY(5, 7)";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse GOSUB with params");
    assert(ast->type == AST_STMT_GOSUB);
    assert(strcmp(ast->as.gosub_stmt.label, "MULTIPLY") == 0);
    assert(ast->as.gosub_stmt.param_count == 2);
    c64script_ast_free(ast);
}

TEST(parse_return)
{
    const char *source = "RETURN";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse RETURN");
    assert(ast->type == AST_STMT_RETURN);
    assert(ast->as.return_stmt.return_value == NULL);
    c64script_ast_free(ast);
}

TEST(parse_return_with_value)
{
    const char *source = "RETURN X * 2";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse RETURN with value");
    assert(ast->type == AST_STMT_RETURN);
    assert(ast->as.return_stmt.return_value != NULL);
    c64script_ast_free(ast);
}

TEST(parse_stop)
{
    const char *source = "STOP";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse STOP");
    assert(ast->type == AST_STMT_STOP);
    c64script_ast_free(ast);
}

TEST(parse_end)
{
    const char *source = "END";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse END");
    assert(ast->type == AST_STMT_STOP);
    c64script_ast_free(ast);
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
    c64script_ast_free(ast);
}

TEST(parse_empty_lines_and_comments)
{
    const char *source = "REM Comment\n\nx = 1\n\nREM Another comment\ny = 2";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse with comments");

    // Count actual statements (including REM statements)
    int count = 0;
    c64script_ast_node_t *node = ast;
    while (node) {
        count++;
        node = node->next;
    }
    assert(count == 4 && "Expected 4 statements (2 assignments + 2 REM)");
    c64script_ast_free(ast);
}

// ============================================================================
// ERROR HANDLING TESTS
// ============================================================================

TEST(parse_error_unterminated_string)
{
    const char *source = "x = \"unterminated";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast == NULL);
    assert(error_msg[0] != '\0');
}

TEST(parse_error_invalid_operator)
{
    const char *source = "x = 1 @ 2"; // @ is not a valid operator
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast == NULL);
    assert(error_msg[0] != '\0');
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
    c64script_ast_free(ast);
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
    c64script_ast_free(ast);
}

TEST(parse_if_block)
{
    const char *source = "IF x > 0 THEN\ny = 1\nz = 2\nENDIF";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse IF block");
    assert(ast->type == AST_STMT_IF);
    assert(ast->as.if_stmt.then_branch != NULL);
    c64script_ast_free(ast);
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
    c64script_ast_free(ast);
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
    c64script_ast_free(ast);
}

TEST(parse_for_loop_with_step)
{
    const char *source = "FOR i = 10 TO 1 STEP -1\nPRINT i\nNEXT i";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse FOR loop with STEP");
    assert(ast->type == AST_STMT_FOR);
    c64script_ast_free(ast);
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
    c64script_ast_free(ast);
}

TEST(parse_while_endwhile)
{
    const char *source = "WHILE x < 10\nx = x + 1\nENDWHILE";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse WHILE/ENDWHILE");
    assert(ast->type == AST_STMT_WHILE);
    c64script_ast_free(ast);
}

TEST(parse_while_end_while)
{
    const char *source = "WHILE x < 10\nx = x + 1\nEND WHILE";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse WHILE/END WHILE");
    assert(ast->type == AST_STMT_WHILE);
    c64script_ast_free(ast);
}

TEST(parse_wait_duration)
{
    const char *source = "WAIT 500";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse WAIT");
    assert(ast->type == AST_STMT_WAIT);
    assert(ast->as.wait_stmt.duration != NULL);
    assert(ast->as.wait_stmt.unit == C64SCRIPT_WAIT_UNIT_S);
    c64script_ast_free(ast);
}

TEST(parse_wait_until)
{
    const char *source = "WAIT UNTIL x > 0";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse WAIT UNTIL");
    assert(ast->type == AST_STMT_WAIT_UNTIL);
    c64script_ast_free(ast);
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
    c64script_ast_free(ast);
}

TEST(parse_effectparam)
{
    const char *source = "EFFECTPARAM \"intensity\" 0.5";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse EFFECTPARAM");
    assert(ast->type == AST_STMT_EFFECTPARAM);
    c64script_ast_free(ast);
}

TEST(parse_palette)
{
    const char *source = "PALETTE \"pepto\"";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse PALETTE");
    assert(ast->type == AST_STMT_PALETTE);
    c64script_ast_free(ast);
}

TEST(parse_playsid)
{
    const char *source = "PLAYSID \"music.sid\"";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse PLAYSID");
    assert(ast->type == AST_STMT_PLAYSID);
    c64script_ast_free(ast);
}

TEST(parse_playsid_songnr)
{
    const char *source = "PLAYSID \"music.sid\" SONGNR=2";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL);
    assert(ast->type == AST_STMT_PLAYSID);
    assert(ast->as.playsid_stmt.songnr != NULL);
    c64script_ast_free(ast);
}

TEST(parse_runprg)
{
    const char *source = "RUNPRG \"demo.prg\"";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse RUNPRG");
    assert(ast->type == AST_STMT_RUNPRG);
    c64script_ast_free(ast);
}

TEST(parse_runlocal)
{
    const char *source = "RUNLOCAL \"script.sh\" ARGS \"input.txt\" STATUS CODE OUTPUT OUT$";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse RUNLOCAL");
    assert(ast->type == AST_STMT_RUNLOCAL);
    assert(ast->as.runlocal_stmt.path != NULL);
    assert(ast->as.runlocal_stmt.args != NULL);
    assert(ast->as.runlocal_stmt.status_var != NULL);
    assert(ast->as.runlocal_stmt.output_var != NULL);
    c64script_ast_free(ast);
}

TEST(parse_mountdisk)
{
    const char *source = "MOUNTDISK \"game.d64\"";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse MOUNTDISK");
    assert(ast->type == AST_STMT_MOUNTDISK);
    c64script_ast_free(ast);
}

TEST(parse_autostart)
{
    const char *source = "AUTOSTART";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse AUTOSTART");
    assert(ast->type == AST_STMT_AUTOSTART);
    c64script_ast_free(ast);
}

TEST(parse_reset)
{
    const char *source = "RESET";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse RESET");
    assert(ast->type == AST_STMT_RESET);
    c64script_ast_free(ast);
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
    const char *source = "REM Simple program\n"
                         "LABEL start\n"
                         "x = 0\n"
                         "LABEL loop\n"
                         "x = x + 1\n"
                         "GOTO loop\n";

    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse full program");

    // Verify structure (first node is now REM statement)
    assert(ast->type == AST_STMT_REM);
    assert(ast->next != NULL);
    assert(ast->next->type == AST_STMT_LABEL);
    assert(ast->next->next != NULL);
    assert(ast->next->next->type == AST_STMT_ASSIGNMENT);
}

// ============================================================================
// ADDITIONAL EDGE CASE TESTS
// ============================================================================

TEST(parse_nested_for_loops)
{
    const char *source = "FOR I = 1 TO 10\n"
                         "  FOR J = 1 TO 5\n"
                         "    X = I * J\n"
                         "  NEXT\n"
                         "NEXT\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse nested FOR loops");
    assert(ast->type == AST_STMT_FOR);
    c64script_ast_free(ast);
}

TEST(parse_nested_if_blocks)
{
    const char *source = "IF X > 0 THEN\n"
                         "  IF Y > 0 THEN\n"
                         "    Z = 1\n"
                         "  ENDIF\n"
                         "ENDIF\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse nested IF blocks");
    assert(ast->type == AST_STMT_IF);
    c64script_ast_free(ast);
}

TEST(parse_complex_boolean_expression)
{
    const char *source = "X = (A > 5 AND B < 10) OR (C = 0 AND NOT D)\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse complex boolean expression");
    assert(ast->type == AST_STMT_ASSIGNMENT);
    c64script_ast_free(ast);
}

TEST(parse_hex_literal)
{
    const char *source = "X = $D020\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse hex literal");
    assert(ast->type == AST_STMT_ASSIGNMENT);
    assert(ast->as.assignment.value != NULL);
    assert(ast->as.assignment.value->type == AST_EXPR_NUMBER);
    assert(ast->as.assignment.value->as.number == 0xD020);
    c64script_ast_free(ast);
}

TEST(parse_duration_milliseconds)
{
    const char *source = "WAIT 500ms\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse milliseconds duration");
    assert(ast->type == AST_STMT_WAIT);
    assert(ast->as.wait_stmt.unit == C64SCRIPT_WAIT_UNIT_MS);
    c64script_ast_free(ast);
}

TEST(parse_duration_seconds)
{
    const char *source = "WAIT 2.5s\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse seconds duration");
    assert(ast->type == AST_STMT_WAIT);
    // When tokenizer sees "2.5s", it converts to milliseconds, so unit is MS
    assert(ast->as.wait_stmt.unit == C64SCRIPT_WAIT_UNIT_MS);
    c64script_ast_free(ast);
}

TEST(parse_duration_hours)
{
    const char *source = "WAIT 2h\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse hours duration");
    assert(ast->type == AST_STMT_WAIT);
    // When tokenizer sees "2h", it converts to milliseconds
    assert(ast->as.wait_stmt.unit == C64SCRIPT_WAIT_UNIT_MS);
    c64script_ast_free(ast);
}

TEST(parse_duration_days)
{
    const char *source = "WAIT 0.5d\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse days duration");
    assert(ast->type == AST_STMT_WAIT);
    // When tokenizer sees "0.5d", it converts to milliseconds
    assert(ast->as.wait_stmt.unit == C64SCRIPT_WAIT_UNIT_MS);
    c64script_ast_free(ast);
}

TEST(parse_palettecolor)
{
    const char *source = "PALETTECOLOR 0, 255, 128, 64\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse PALETTECOLOR");
    assert(ast->type == AST_STMT_PALETTECOLOR);
    assert(ast->as.palettecolor_stmt.index != NULL);
    assert(ast->as.palettecolor_stmt.r != NULL);
    assert(ast->as.palettecolor_stmt.g != NULL);
    assert(ast->as.palettecolor_stmt.b != NULL);
    c64script_ast_free(ast);
}

TEST(parse_readfile)
{
    const char *source = "READFILE CONTENT$, \"test.txt\"\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse READFILE");
    assert(ast->type == AST_STMT_READFILE);
    assert(ast->as.readfile_stmt.variable != NULL);
    assert(ast->as.readfile_stmt.path != NULL);
    c64script_ast_free(ast);
}

TEST(parse_writefile_append)
{
    const char *source = "WRITEFILE \"log.txt\", \"Hello\\n\" APPEND\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse WRITEFILE APPEND");
    assert(ast->type == AST_STMT_WRITEFILE);
    assert(ast->as.writefile_stmt.path != NULL);
    assert(ast->as.writefile_stmt.content != NULL);
    assert(ast->as.writefile_stmt.truncate == false);
    c64script_ast_free(ast);
}

TEST(parse_writefile_truncate)
{
    const char *source = "WRITEFILE \"output.txt\", DATA$ TRUNCATE\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse WRITEFILE TRUNCATE");
    assert(ast->type == AST_STMT_WRITEFILE);
    assert(ast->as.writefile_stmt.path != NULL);
    assert(ast->as.writefile_stmt.content != NULL);
    assert(ast->as.writefile_stmt.truncate == true);
    c64script_ast_free(ast);
}

TEST(parse_http)
{
    const char *source = "HTTP GET \"http://example.com/api\" HEADERS HDR$ BODY BODY$ STATUS S RESPONSE R$\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse HTTP");
    assert(ast->type == AST_STMT_HTTP);
    assert(ast->as.http_stmt.method == 0); // GET
    assert(ast->as.http_stmt.url != NULL);
    assert(ast->as.http_stmt.headers != NULL);
    assert(ast->as.http_stmt.body != NULL);
    assert(ast->as.http_stmt.status_var != NULL);
    assert(ast->as.http_stmt.response_var != NULL);
    c64script_ast_free(ast);
}

TEST(parse_peek_function)
{
    const char *source = "X = PEEK($D020)\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse PEEK function");
    assert(ast->type == AST_STMT_ASSIGNMENT);
    assert(ast->as.assignment.value != NULL);
    assert(ast->as.assignment.value->type == AST_EXPR_CALL);
    c64script_ast_free(ast);
}

TEST(parse_string_with_escapes)
{
    const char *source = "X$ = \"Line 1\\nLine 2\\tTabbed\\rReturn\"\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse string with escapes");
    assert(ast->type == AST_STMT_ASSIGNMENT);
    c64script_ast_free(ast);
}

TEST(parse_string_with_hex_escape)
{
    const char *source = "X$ = \"Character \\x41 is A\"\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse string with hex escape");
    assert(ast->type == AST_STMT_ASSIGNMENT);
    c64script_ast_free(ast);
}

TEST(parse_for_loop_negative_step)
{
    const char *source = "FOR I = 10 TO 1 STEP -1\nNEXT\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse FOR loop with negative step");
    assert(ast->type == AST_STMT_FOR);
    c64script_ast_free(ast);
}

TEST(parse_xor_operator)
{
    const char *source = "X = A XOR B\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse XOR operator");
    assert(ast->type == AST_STMT_ASSIGNMENT);
    c64script_ast_free(ast);
}

TEST(parse_not_equal_both_forms)
{
    const char *source1 = "X = A <> B\n";
    const char *source2 = "X = A != B\n";
    char error_msg[1024];

    c64script_ast_node_t *ast1 = c64script_parse(source1, strlen(source1), error_msg, sizeof(error_msg));
    assert(ast1 != NULL && "Failed to parse <> operator");
    assert(ast1->type == AST_STMT_ASSIGNMENT);
    c64script_ast_free(ast1);

    c64script_ast_node_t *ast2 = c64script_parse(source2, strlen(source2), error_msg, sizeof(error_msg));
    assert(ast2 != NULL && "Failed to parse != operator");
    assert(ast2->type == AST_STMT_ASSIGNMENT);
    c64script_ast_free(ast2);
}

TEST(parse_equality_both_forms)
{
    const char *source1 = "X = A = B\n";
    const char *source2 = "X = A == B\n";
    char error_msg[1024];

    c64script_ast_node_t *ast1 = c64script_parse(source1, strlen(source1), error_msg, sizeof(error_msg));
    assert(ast1 != NULL && "Failed to parse = operator");
    assert(ast1->type == AST_STMT_ASSIGNMENT);
    c64script_ast_free(ast1);

    c64script_ast_node_t *ast2 = c64script_parse(source2, strlen(source2), error_msg, sizeof(error_msg));
    assert(ast2 != NULL && "Failed to parse == operator");
    assert(ast2->type == AST_STMT_ASSIGNMENT);
    c64script_ast_free(ast2);
}

TEST(parse_rem_comment)
{
    const char *source = "REM This is a comment\nX = 1\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse REM comment");
    assert(ast->type == AST_STMT_REM); // REM creates an AST node
    assert(ast->next != NULL);
    assert(ast->next->type == AST_STMT_ASSIGNMENT);
    c64script_ast_free(ast);
}

TEST(parse_case_insensitive_keywords)
{
    const char *source = "for i = 1 to 10\nnext\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse lowercase keywords");
    assert(ast->type == AST_STMT_FOR);
    c64script_ast_free(ast);
}

// ============================================================================
// ARRAY TESTS
// ============================================================================

TEST(parse_dim_array)
{
    const char *source = "DIM VALUES(10)\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse DIM array");
    assert(ast->type == AST_STMT_DIM);
    assert(strcmp(ast->as.dim_stmt.array_name, "VALUES") == 0);
    assert(ast->as.dim_stmt.size != NULL);
    assert(ast->as.dim_stmt.size->type == AST_EXPR_NUMBER);
    assert(ast->as.dim_stmt.size->as.number == 10.0);
    c64script_ast_free(ast);
}

TEST(parse_dim_string_array)
{
    const char *source = "DIM NAMES$(5)\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse DIM string array");
    assert(ast->type == AST_STMT_DIM);
    assert(strcmp(ast->as.dim_stmt.array_name, "NAMES$") == 0);
    c64script_ast_free(ast);
}

TEST(parse_array_access)
{
    const char *source = "X = VALUES(5)\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse array access");
    assert(ast->type == AST_STMT_ASSIGNMENT);
    // Array access with single arg is parsed as function call (distinction at runtime)
    c64script_ast_free(ast);
}

// TODO: Array assignment syntax (VALUES(3) = 42) not yet implemented in parser
// Parser currently treats identifier(expr) as function call, not array access
// TEST(parse_array_assignment)
// {
//     const char *source = "VALUES(3) = 42\n";
//     char error_msg[1024];
//     c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
//     assert(ast != NULL && "Failed to parse array assignment");
//     assert(ast->type == AST_STMT_ARRAY_SET);
//     assert(strcmp(ast->as.array_set.array_name, "VALUES") == 0);
//     assert(ast->as.array_set.index != NULL);
//     assert(ast->as.array_set.value != NULL);
//     c64script_ast_free(ast);
// }

TEST(parse_nested_array_access)
{
    const char *source = "X = VALUES(INDEX(2))\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse nested array access");
    assert(ast->type == AST_STMT_ASSIGNMENT);
    c64script_ast_free(ast);
}

// ============================================================================
// MAP TESTS
// ============================================================================

TEST(parse_map_access)
{
    const char *source = "X = CONFIG{\"port\"}\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse map access");
    assert(ast->type == AST_STMT_ASSIGNMENT);
    assert(ast->as.assignment.value->type == AST_EXPR_MAP_ACCESS);
    assert(strcmp(ast->as.assignment.value->as.map_access.name, "CONFIG") == 0);
    c64script_ast_free(ast);
}

// TODO: Map assignment syntax (CONFIG{"key"} = value) not yet implemented in parser
// Parser needs to handle map{} in assignment position
// TEST(parse_map_assignment)
// {
//     const char *source = "CONFIG{\"host\"} = \"localhost\"\n";
//     char error_msg[1024];
//     c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
//     assert(ast != NULL && "Failed to parse map assignment");
//     assert(ast->type == AST_STMT_MAP_SET);
//     assert(strcmp(ast->as.map_set.map_name, "CONFIG") == 0);
//     assert(ast->as.map_set.key != NULL);
//     assert(ast->as.map_set.value != NULL);
//     c64script_ast_free(ast);
// }

// TODO: Map assignment not implemented
// TEST(parse_map_string_key)
// {
//     const char *source = "DATA{\"key\"} = 100\n";
//     char error_msg[1024];
//     c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
//     assert(ast != NULL && "Failed to parse map with string key");
//     assert(ast->type == AST_STMT_MAP_SET);
//     c64script_ast_free(ast);
// }

TEST(parse_map_in_expression)
{
    const char *source = "X = CONFIG{\"port\"} + 10\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse map in expression");
    assert(ast->type == AST_STMT_ASSIGNMENT);
    assert(ast->as.assignment.value->type == AST_EXPR_BINARY);
    c64script_ast_free(ast);
}

// ============================================================================
// FUNCTION TESTS
// ============================================================================

TEST(parse_function_no_params)
{
    const char *source = "FUN GET_VALUE()\n    RETURN 42\nENDFUN\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse function with no params");
    assert(ast->type == AST_STMT_FUNCTION_DEF);
    assert(strcmp(ast->as.function_def.name, "GET_VALUE") == 0);
    assert(ast->as.function_def.param_count == 0);
    assert(ast->as.function_def.body != NULL);
    c64script_ast_free(ast);
}

TEST(parse_function_one_param)
{
    const char *source = "FUN DOUBLE(X)\n    RETURN X * 2\nENDFUN\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse function with one param");
    assert(ast->type == AST_STMT_FUNCTION_DEF);
    assert(ast->as.function_def.param_count == 1);
    assert(strcmp(ast->as.function_def.param_names[0], "X") == 0);
    c64script_ast_free(ast);
}

TEST(parse_function_multiple_params)
{
    const char *source = "FUN ADD(A, B, C)\n    RETURN A + B + C\nENDFUN\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse function with multiple params");
    assert(ast->type == AST_STMT_FUNCTION_DEF);
    assert(ast->as.function_def.param_count == 3);
    assert(strcmp(ast->as.function_def.param_names[0], "A") == 0);
    assert(strcmp(ast->as.function_def.param_names[1], "B") == 0);
    assert(strcmp(ast->as.function_def.param_names[2], "C") == 0);
    c64script_ast_free(ast);
}

TEST(parse_function_string_param)
{
    const char *source = "FUN GREET(NAME$)\n    RETURN \"Hello, \" + NAME$\nENDFUN\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse function with string param");
    assert(ast->type == AST_STMT_FUNCTION_DEF);
    assert(strcmp(ast->as.function_def.param_names[0], "NAME$") == 0);
    c64script_ast_free(ast);
}

TEST(parse_function_call_no_args)
{
    const char *source = "X = GET_VALUE()\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse function call with no args");
    assert(ast->type == AST_STMT_ASSIGNMENT);
    assert(ast->as.assignment.value->type == AST_EXPR_CALL);
    assert(strcmp(ast->as.assignment.value->as.call.name, "GET_VALUE") == 0);
    assert(ast->as.assignment.value->as.call.arg_count == 0);
    c64script_ast_free(ast);
}

TEST(parse_function_call_with_args)
{
    const char *source = "RESULT = ADD(10, 20, 30)\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse function call with args");
    assert(ast->type == AST_STMT_ASSIGNMENT);
    assert(ast->as.assignment.value->type == AST_EXPR_CALL);
    assert(ast->as.assignment.value->as.call.arg_count == 3);
    c64script_ast_free(ast);
}

TEST(parse_function_nested_calls)
{
    const char *source = "X = ADD(DOUBLE(5), DOUBLE(10))\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse nested function calls");
    assert(ast->type == AST_STMT_ASSIGNMENT);
    assert(ast->as.assignment.value->type == AST_EXPR_CALL);
    c64script_ast_free(ast);
}

TEST(parse_function_return_expr)
{
    const char *source = "FUN TEST()\n    RETURN 1 + 2\nENDFUN\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse function with return expr");
    assert(ast->type == AST_STMT_FUNCTION_DEF);
    assert(ast->as.function_def.body->type == AST_STMT_RETURN);
    assert(ast->as.function_def.body->as.return_stmt.return_value != NULL);
    c64script_ast_free(ast);
}

TEST(parse_function_return_no_expr)
{
    const char *source = "FUN TEST()\n    X = 1\n    RETURN\nENDFUN\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse function with return no expr");
    assert(ast->type == AST_STMT_FUNCTION_DEF);
    c64script_ast_free(ast);
}

TEST(parse_function_end_fun_two_words)
{
    const char *source = "FUN TEST()\n    RETURN 1\nEND FUN\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse function with END FUN");
    assert(ast->type == AST_STMT_FUNCTION_DEF);
    c64script_ast_free(ast);
}

TEST(parse_function_local_variables)
{
    const char *source = "FUN CALC(X)\n    Y = X * 2\n    Z = Y + 1\n    RETURN Z\nENDFUN\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse function with local variables");
    assert(ast->type == AST_STMT_FUNCTION_DEF);
    assert(ast->as.function_def.body != NULL);
    c64script_ast_free(ast);
}

TEST(parse_function_recursive)
{
    const char *source =
        "FUN FACTORIAL(N)\n    IF N <= 1 THEN\n        RETURN 1\n    ENDIF\n    RETURN N * FACTORIAL(N - 1)\nENDFUN\n";
    char error_msg[1024];
    c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));
    assert(ast != NULL && "Failed to parse recursive function");
    assert(ast->type == AST_STMT_FUNCTION_DEF);
    c64script_ast_free(ast);
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================

int main(int argc, char **argv)
{
    printf("=== C64Script Parser Tests ===\n\n");

    // If a script file is provided as argument, parse and validate it
    if (argc > 1) {
        const char *script_path = argv[1];
        printf("--- Testing Script File: %s ---\n", script_path);

        FILE *f = fopen(script_path, "r");
        if (!f) {
            fprintf(stderr, "ERROR: Failed to open script file: %s\n", script_path);
            return 1;
        }

        // Read entire file
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        char *source = malloc(size + 1);
        if (!source) {
            fprintf(stderr, "ERROR: Failed to allocate memory for script\n");
            fclose(f);
            return 1;
        }

        size_t read = fread(source, 1, size, f);
        source[read] = '\0';
        fclose(f);

        // Parse the script
        char error_msg[1024];
        c64script_ast_node_t *ast = c64script_parse(source, strlen(source), error_msg, sizeof(error_msg));

        if (!ast) {
            fprintf(stderr, "ERROR: Failed to parse %s: %s\n", script_path, error_msg);
            free(source);
            return 1;
        }

        printf("✅ Successfully parsed %s (%ld bytes)\n", script_path, size);
        c64script_ast_free(ast);
        free(source);
        return 0;
    }

    // Otherwise run built-in tests
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
    RUN_TEST(parse_gosub_with_params);
    RUN_TEST(parse_return);
    RUN_TEST(parse_return_with_value);
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
    RUN_TEST(parse_runlocal);
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
    RUN_TEST(parse_readfile);
    RUN_TEST(parse_writefile_append);
    RUN_TEST(parse_writefile_truncate);
    RUN_TEST(parse_http);

    printf("\n--- Error Handling Tests ---\n");
    RUN_TEST(parse_error_unterminated_string);
    RUN_TEST(parse_error_invalid_operator);

    printf("\n--- Integration Tests ---\n");
    RUN_TEST(parse_full_program);

    printf("\n--- Edge Case Tests ---\n");
    RUN_TEST(parse_nested_for_loops);
    RUN_TEST(parse_nested_if_blocks);
    RUN_TEST(parse_complex_boolean_expression);
    RUN_TEST(parse_hex_literal);
    RUN_TEST(parse_duration_milliseconds);
    RUN_TEST(parse_duration_seconds);
    RUN_TEST(parse_duration_hours);
    RUN_TEST(parse_duration_days);
    RUN_TEST(parse_palettecolor);
    RUN_TEST(parse_peek_function);
    RUN_TEST(parse_string_with_escapes);
    RUN_TEST(parse_string_with_hex_escape);
    RUN_TEST(parse_for_loop_negative_step);
    RUN_TEST(parse_xor_operator);
    RUN_TEST(parse_not_equal_both_forms);
    RUN_TEST(parse_equality_both_forms);
    RUN_TEST(parse_rem_comment);
    RUN_TEST(parse_case_insensitive_keywords);
    RUN_TEST(parse_label_prefix_same_line_no_colon);
    RUN_TEST(parse_line_number_prefix_same_line);
    RUN_TEST(parse_line_number_goto);
    RUN_TEST(parse_while_end_while);
    RUN_TEST(parse_playsid_songnr);

    printf("\n--- Array Tests ---\n");
    RUN_TEST(parse_dim_array);
    RUN_TEST(parse_dim_string_array);
    RUN_TEST(parse_array_access);
    // RUN_TEST(parse_array_assignment); // TODO: Not implemented yet
    RUN_TEST(parse_nested_array_access);

    printf("\n--- Map Tests ---\n");
    RUN_TEST(parse_map_access);
    // RUN_TEST(parse_map_assignment); // TODO: Not implemented yet
    // RUN_TEST(parse_map_string_key); // TODO: Not implemented
    RUN_TEST(parse_map_in_expression);

    printf("\n--- Function Tests ---\n");
    RUN_TEST(parse_function_no_params);
    RUN_TEST(parse_function_one_param);
    RUN_TEST(parse_function_multiple_params);
    RUN_TEST(parse_function_string_param);
    RUN_TEST(parse_function_call_no_args);
    RUN_TEST(parse_function_call_with_args);
    RUN_TEST(parse_function_nested_calls);
    RUN_TEST(parse_function_return_expr);
    RUN_TEST(parse_function_return_no_expr);
    RUN_TEST(parse_function_end_fun_two_words);
    RUN_TEST(parse_function_local_variables);
    RUN_TEST(parse_function_recursive);

    printf("\n=== All Parser Tests Passed! ===\n");
    return 0;
}
