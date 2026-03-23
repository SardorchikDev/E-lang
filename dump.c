#include "dump.h"

#include <stdio.h>

static const char *token_type_name(TokenType type) {
    switch (type) {
        case TOKEN_WORD:
            return "word";
        case TOKEN_NUMBER:
            return "number";
        case TOKEN_STRING:
            return "string";
        case TOKEN_SYMBOL:
            return "symbol";
    }

    return "token";
}

static const char *binary_operator_name(BinaryOperator op) {
    switch (op) {
        case BIN_ADD:
            return "plus";
        case BIN_SUBTRACT:
            return "minus";
        case BIN_MULTIPLY:
            return "times";
        case BIN_DIVIDE:
            return "divided";
        case BIN_MODULO:
            return "mod";
        case BIN_POWER:
            return "power";
        case BIN_AND:
            return "and";
        case BIN_OR:
            return "or";
    }

    return "binary";
}

static const char *comparison_operator_name(ComparisonOperator op) {
    switch (op) {
        case CMP_GREATER_THAN:
            return "greater_than";
        case CMP_LESS_THAN:
            return "less_than";
        case CMP_EQUAL:
            return "equal";
        case CMP_NOT_EQUAL:
            return "not_equal";
        case CMP_AT_LEAST:
            return "at_least";
        case CMP_AT_MOST:
            return "at_most";
        case CMP_CONTAINS:
            return "contains";
    }

    return "comparison";
}

static const char *statement_type_name(StatementType type) {
    switch (type) {
        case STMT_USE:
            return "use";
        case STMT_LET:
            return "let";
        case STMT_SET:
            return "set";
        case STMT_APPEND:
            return "append";
        case STMT_SET_ITEM:
            return "set_item";
        case STMT_REMOVE_ITEM:
            return "remove_item";
        case STMT_SET_FIELD:
            return "set_field";
        case STMT_SAY:
            return "say";
        case STMT_ASK:
            return "ask";
        case STMT_IF:
            return "if";
        case STMT_REPEAT:
            return "repeat";
        case STMT_WHILE:
            return "while";
        case STMT_FOR_EACH:
            return "for_each";
        case STMT_FUNCTION:
            return "function";
        case STMT_CALL:
            return "call";
        case STMT_RETURN:
            return "return";
        case STMT_BREAK:
            return "break";
        case STMT_CONTINUE:
            return "continue";
    }

    return "statement";
}

static void print_indent(FILE *stream, int indent) {
    int index;

    for (index = 0; index < indent; index += 1) {
        (void) fputs("  ", stream);
    }
}

static void print_location(FILE *stream, SourceLocation location) {
    if (location.file_path != NULL) {
        (void) fprintf(stream, " @ %s:%d", location.file_path, location.line);
    } else if (location.line > 0) {
        (void) fprintf(stream, " @ line %d", location.line);
    }
}

static void dump_expression(FILE *stream, const Expression *expression, int indent);
static void dump_statement(FILE *stream, const Statement *statement, int indent);

static void dump_expression_list(FILE *stream,
                                 const char *label,
                                 Expression *const *items,
                                 int count,
                                 int indent) {
    int index;

    print_indent(stream, indent);
    (void) fprintf(stream, "%s:\n", label);
    for (index = 0; index < count; index += 1) {
        dump_expression(stream, items[index], indent + 1);
    }
}

static void dump_statement_block(FILE *stream,
                                 const char *label,
                                 Statement *const *items,
                                 int count,
                                 int indent) {
    int index;

    print_indent(stream, indent);
    (void) fprintf(stream, "%s:\n", label);
    for (index = 0; index < count; index += 1) {
        dump_statement(stream, items[index], indent + 1);
    }
}

static void dump_expression(FILE *stream, const Expression *expression, int indent) {
    int index;

    print_indent(stream, indent);

    switch (expression->type) {
        case EXPR_NUMBER:
            (void) fprintf(stream, "number %.15g", expression->as.number);
            print_location(stream, expression->location);
            (void) fputc('\n', stream);
            return;

        case EXPR_STRING:
            (void) fprintf(stream, "string \"%s\"", expression->as.string);
            print_location(stream, expression->location);
            (void) fputc('\n', stream);
            return;

        case EXPR_BOOLEAN:
            (void) fprintf(stream, "boolean %s", expression->as.boolean ? "true" : "false");
            print_location(stream, expression->location);
            (void) fputc('\n', stream);
            return;

        case EXPR_NONE:
            (void) fputs("nothing", stream);
            print_location(stream, expression->location);
            (void) fputc('\n', stream);
            return;

        case EXPR_VARIABLE:
            (void) fprintf(stream, "variable %s", expression->as.variable_name);
            print_location(stream, expression->location);
            (void) fputc('\n', stream);
            return;

        case EXPR_LIST:
            (void) fputs("list", stream);
            print_location(stream, expression->location);
            (void) fputc('\n', stream);
            for (index = 0; index < expression->as.list.item_count; index += 1) {
                dump_expression(stream, expression->as.list.items[index], indent + 1);
            }
            return;

        case EXPR_RECORD:
            (void) fputs("record", stream);
            print_location(stream, expression->location);
            (void) fputc('\n', stream);
            for (index = 0; index < expression->as.record.count; index += 1) {
                print_indent(stream, indent + 1);
                (void) fprintf(stream, "field %s:\n", expression->as.record.keys[index]);
                dump_expression(stream, expression->as.record.values[index], indent + 2);
            }
            return;

        case EXPR_GROUPING:
            (void) fputs("grouping", stream);
            print_location(stream, expression->location);
            (void) fputc('\n', stream);
            dump_expression(stream, expression->as.grouping.inner, indent + 1);
            return;

        case EXPR_UNARY:
            (void) fprintf(stream, "unary %s",
                           expression->as.unary.op == UNARY_NEGATE ? "minus" : "not");
            print_location(stream, expression->location);
            (void) fputc('\n', stream);
            dump_expression(stream, expression->as.unary.right, indent + 1);
            return;

        case EXPR_BINARY:
            (void) fprintf(stream, "binary %s", binary_operator_name(expression->as.binary.op));
            print_location(stream, expression->location);
            (void) fputc('\n', stream);
            dump_expression(stream, expression->as.binary.left, indent + 1);
            dump_expression(stream, expression->as.binary.right, indent + 1);
            return;

        case EXPR_COMPARISON:
            (void) fprintf(stream, "comparison %s",
                           comparison_operator_name(expression->as.comparison.op));
            print_location(stream, expression->location);
            (void) fputc('\n', stream);
            dump_expression(stream, expression->as.comparison.left, indent + 1);
            dump_expression(stream, expression->as.comparison.right, indent + 1);
            return;

        case EXPR_CALL:
            (void) fprintf(stream, "call %s", expression->as.call.name);
            print_location(stream, expression->location);
            (void) fputc('\n', stream);
            dump_expression_list(stream,
                                 "arguments",
                                 expression->as.call.arguments,
                                 expression->as.call.arg_count,
                                 indent + 1);
            return;

        case EXPR_ITEM_ACCESS:
            (void) fputs("item_access", stream);
            print_location(stream, expression->location);
            (void) fputc('\n', stream);
            print_indent(stream, indent + 1);
            (void) fputs("index:\n", stream);
            dump_expression(stream, expression->as.item_access.index, indent + 2);
            print_indent(stream, indent + 1);
            (void) fputs("collection:\n", stream);
            dump_expression(stream, expression->as.item_access.collection, indent + 2);
            return;

        case EXPR_FIELD_ACCESS:
            (void) fprintf(stream, "field_access %s", expression->as.field_access.field_name);
            print_location(stream, expression->location);
            (void) fputc('\n', stream);
            dump_expression(stream, expression->as.field_access.record, indent + 1);
            return;
    }
}

static void dump_statement(FILE *stream, const Statement *statement, int indent) {
    int index;

    print_indent(stream, indent);
    (void) fprintf(stream, "%s", statement_type_name(statement->type));
    print_location(stream, statement->location);
    (void) fputc('\n', stream);

    switch (statement->type) {
        case STMT_USE:
            print_indent(stream, indent + 1);
            (void) fprintf(stream, "path: %s\n", statement->as.use_stmt.path);
            return;

        case STMT_LET:
        case STMT_SET:
            print_indent(stream, indent + 1);
            (void) fprintf(stream, "name: %s\n",
                           statement->type == STMT_LET
                               ? statement->as.let_stmt.name
                               : statement->as.set_stmt.name);
            dump_expression(stream,
                            statement->type == STMT_LET
                                ? statement->as.let_stmt.value
                                : statement->as.set_stmt.value,
                            indent + 1);
            return;

        case STMT_APPEND:
            print_indent(stream, indent + 1);
            (void) fprintf(stream, "name: %s\n", statement->as.append_stmt.name);
            dump_expression(stream, statement->as.append_stmt.value, indent + 1);
            return;

        case STMT_SET_ITEM:
            print_indent(stream, indent + 1);
            (void) fprintf(stream, "name: %s\n", statement->as.set_item_stmt.name);
            print_indent(stream, indent + 1);
            (void) fputs("index:\n", stream);
            dump_expression(stream, statement->as.set_item_stmt.index, indent + 2);
            print_indent(stream, indent + 1);
            (void) fputs("value:\n", stream);
            dump_expression(stream, statement->as.set_item_stmt.value, indent + 2);
            return;

        case STMT_REMOVE_ITEM:
            print_indent(stream, indent + 1);
            (void) fprintf(stream, "name: %s\n", statement->as.remove_item_stmt.name);
            print_indent(stream, indent + 1);
            (void) fputs("index:\n", stream);
            dump_expression(stream, statement->as.remove_item_stmt.index, indent + 2);
            return;

        case STMT_SET_FIELD:
            print_indent(stream, indent + 1);
            (void) fprintf(stream, "name: %s\n", statement->as.set_field_stmt.name);
            print_indent(stream, indent + 1);
            (void) fprintf(stream, "field: %s\n", statement->as.set_field_stmt.field_name);
            dump_expression(stream, statement->as.set_field_stmt.value, indent + 1);
            return;

        case STMT_SAY:
            dump_expression(stream, statement->as.say_stmt.value, indent + 1);
            return;

        case STMT_ASK:
            print_indent(stream, indent + 1);
            (void) fprintf(stream, "store_in: %s\n", statement->as.ask_stmt.name);
            dump_expression(stream, statement->as.ask_stmt.prompt, indent + 1);
            return;

        case STMT_IF:
            print_indent(stream, indent + 1);
            (void) fputs("condition:\n", stream);
            dump_expression(stream, statement->as.if_stmt.condition, indent + 2);
            dump_statement_block(stream,
                                 "then",
                                 statement->as.if_stmt.then_body,
                                 statement->as.if_stmt.then_count,
                                 indent + 1);
            if (statement->as.if_stmt.else_count > 0) {
                dump_statement_block(stream,
                                     "else",
                                     statement->as.if_stmt.else_body,
                                     statement->as.if_stmt.else_count,
                                     indent + 1);
            }
            return;

        case STMT_REPEAT:
            print_indent(stream, indent + 1);
            (void) fputs("count:\n", stream);
            dump_expression(stream, statement->as.repeat_stmt.count, indent + 2);
            dump_statement_block(stream,
                                 "body",
                                 statement->as.repeat_stmt.body,
                                 statement->as.repeat_stmt.body_count,
                                 indent + 1);
            return;

        case STMT_WHILE:
            print_indent(stream, indent + 1);
            (void) fputs("condition:\n", stream);
            dump_expression(stream, statement->as.while_stmt.condition, indent + 2);
            dump_statement_block(stream,
                                 "body",
                                 statement->as.while_stmt.body,
                                 statement->as.while_stmt.body_count,
                                 indent + 1);
            return;

        case STMT_FOR_EACH:
            print_indent(stream, indent + 1);
            (void) fprintf(stream, "item_name: %s\n", statement->as.for_each_stmt.item_name);
            print_indent(stream, indent + 1);
            (void) fputs("collection:\n", stream);
            dump_expression(stream, statement->as.for_each_stmt.collection, indent + 2);
            dump_statement_block(stream,
                                 "body",
                                 statement->as.for_each_stmt.body,
                                 statement->as.for_each_stmt.body_count,
                                 indent + 1);
            return;

        case STMT_FUNCTION:
            print_indent(stream, indent + 1);
            (void) fprintf(stream, "name: %s\n", statement->as.function_stmt.name);
            print_indent(stream, indent + 1);
            (void) fputs("parameters:", stream);
            if (statement->as.function_stmt.param_count == 0) {
                (void) fputs(" none", stream);
            }
            (void) fputc('\n', stream);
            for (index = 0; index < statement->as.function_stmt.param_count; index += 1) {
                print_indent(stream, indent + 2);
                (void) fprintf(stream, "%s\n", statement->as.function_stmt.parameters[index]);
            }
            dump_statement_block(stream,
                                 "body",
                                 statement->as.function_stmt.body,
                                 statement->as.function_stmt.body_count,
                                 indent + 1);
            return;

        case STMT_CALL:
            dump_expression(stream, statement->as.call_stmt.call, indent + 1);
            return;

        case STMT_RETURN:
            if (statement->as.return_stmt.value != NULL) {
                dump_expression(stream, statement->as.return_stmt.value, indent + 1);
            }
            return;

        case STMT_BREAK:
        case STMT_CONTINUE:
            return;
    }
}

void dump_tokens(const LexedProgram *program, FILE *stream) {
    int line_index;

    for (line_index = 0; line_index < program->count; line_index += 1) {
        const LexedLine *line = &program->lines[line_index];
        int token_index;

        (void) fprintf(stream, "line %d:\n", line->line_number);
        if (line->count == 0) {
            (void) fputs("  <empty>\n", stream);
            continue;
        }

        for (token_index = 0; token_index < line->count; token_index += 1) {
            (void) fprintf(stream,
                           "  %-6s %s\n",
                           token_type_name(line->tokens[token_index].type),
                           line->tokens[token_index].lexeme);
        }
    }
}

void dump_ast(const Program *program, FILE *stream) {
    int index;

    (void) fprintf(stream, "program (%d statements)\n", program->count);
    for (index = 0; index < program->count; index += 1) {
        dump_statement(stream, program->statements[index], 1);
    }
}
