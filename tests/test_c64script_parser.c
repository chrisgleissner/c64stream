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
// INTEGRATION TESTS
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

    printf("\n--- Error Handling Tests ---\n");
    RUN_TEST(parse_error_unterminated_string);
    RUN_TEST(parse_error_invalid_operator);

    printf("\n--- Integration Tests ---\n");
    RUN_TEST(parse_full_program);

    printf("\n=== All Parser Tests Passed! ===\n");
    return 0;
}
