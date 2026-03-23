#include "parser.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const LexedProgram *program;
    int current_line;
    char *error_message;
    size_t error_size;
    bool had_error;
} Parser;

typedef struct {
    Parser *parser;
    const LexedLine *line;
    int current;
    int end;
} ExpressionParser;

typedef enum {
    BLOCK_TERMINATOR_NONE,
    BLOCK_TERMINATOR_END,
    BLOCK_TERMINATOR_ELSE
} BlockTerminator;

static void free_expression(Expression *expression);
static void free_statement(Statement *statement);
static void free_statement_list(Statement **statements, int count);

static bool equals_ignore_case(const char *left, const char *right) {
    size_t index = 0;

    while (left[index] != '\0' && right[index] != '\0') {
        if (tolower((unsigned char) left[index]) != tolower((unsigned char) right[index])) {
            return false;
        }
        index += 1;
    }

    return left[index] == '\0' && right[index] == '\0';
}

static SourceLocation location_from_line(const LexedLine *line) {
    SourceLocation location;
    location.line = line->line_number;
    location.file_path = line->file_path;
    location.source_text = line->source_text;
    return location;
}

static SourceLocation location_from_token(const Token *token) {
    SourceLocation location;
    location.line = token->line;
    location.file_path = token->file_path;
    location.source_text = token->source_text;
    return location;
}

static bool token_is_word(const Token *token, const char *word) {
    return token != NULL && token->type == TOKEN_WORD && equals_ignore_case(token->lexeme, word);
}

static bool token_is_symbol(const Token *token, const char *symbol) {
    return token != NULL && token->type == TOKEN_SYMBOL && strcmp(token->lexeme, symbol) == 0;
}

static char *duplicate_text(const char *text) {
    size_t length = strlen(text);
    char *copy = (char *) malloc(length + 1);

    if (copy == NULL) {
        return NULL;
    }

    (void) memcpy(copy, text, length + 1);
    return copy;
}

static void append_caret(char *buffer, size_t buffer_size, const char *source_text, const char *anchor) {
    size_t used;
    size_t offset = 0;
    const char *match = NULL;

    if (buffer == NULL || buffer_size == 0 || source_text == NULL || source_text[0] == '\0') {
        return;
    }

    if (anchor != NULL && anchor[0] != '\0') {
        match = strstr(source_text, anchor);
    }

    if (match != NULL) {
        offset = (size_t) (match - source_text);
    }

    used = strlen(buffer);
    if (used >= buffer_size) {
        return;
    }

    (void) snprintf(buffer + used, buffer_size - used, "\n    ");
    used = strlen(buffer);

    while (offset > 0 && used + 1 < buffer_size) {
        buffer[used] = (source_text[(size_t) (match - source_text) - offset] == '\t') ? '\t' : ' ';
        used += 1;
        offset -= 1;
    }

    if (used + 1 < buffer_size) {
        buffer[used] = '^';
        used += 1;
    }
    if (used < buffer_size) {
        buffer[used] = '\0';
    }
}

static void parser_error_at(Parser *parser, SourceLocation location, const char *anchor, const char *format, ...) {
    va_list args;
    size_t used;

    if (parser->had_error) {
        return;
    }

    parser->had_error = true;

    if (parser->error_message == NULL || parser->error_size == 0) {
        return;
    }

    if (location.file_path != NULL) {
        (void) snprintf(parser->error_message, parser->error_size, "%s:%d: ",
                        location.file_path, location.line);
    } else if (location.line > 0) {
        (void) snprintf(parser->error_message, parser->error_size, "Line %d: ", location.line);
    } else {
        parser->error_message[0] = '\0';
    }

    used = strlen(parser->error_message);
    va_start(args, format);
    (void) vsnprintf(parser->error_message + used, parser->error_size - used, format, args);
    va_end(args);

    if (location.source_text != NULL && location.source_text[0] != '\0') {
        used = strlen(parser->error_message);
        (void) snprintf(parser->error_message + used, parser->error_size - used,
                        "\n    %s", location.source_text);
        append_caret(parser->error_message, parser->error_size, location.source_text, anchor);
    }
}

static bool is_reserved_word(const char *word) {
    static const char *reserved_words[] = {
        "use", "let", "be", "set", "to", "say", "ask", "and", "store", "in",
        "if", "then", "else", "repeat", "time", "times", "while", "do", "end",
        "for", "each", "note", "define", "function", "call", "return", "with",
        "break", "continue", "plus", "minus", "times", "divided", "by", "mod",
        "power", "not", "or", "is", "greater", "less", "equal", "at", "least",
        "most", "contains", "true", "false", "nothing", "list", "of", "record"
    };
    size_t index;

    for (index = 0; index < sizeof(reserved_words) / sizeof(reserved_words[0]); index += 1) {
        if (equals_ignore_case(word, reserved_words[index])) {
            return true;
        }
    }

    return false;
}

static char *copy_name_from_token(Parser *parser, const Token *token, bool allow_reserved) {
    char *name;

    if (token == NULL || token->type != TOKEN_WORD) {
        parser_error_at(parser, token != NULL ? location_from_token(token) : (SourceLocation) {0, NULL, NULL},
                        NULL, "I expected a simple one-word name here.");
        return NULL;
    }

    if (!allow_reserved && is_reserved_word(token->lexeme)) {
        parser_error_at(parser, location_from_token(token), token->lexeme,
                        "'%s' is a reserved word in E-Lang. Pick a different name.",
                        token->lexeme);
        return NULL;
    }

    name = duplicate_text(token->lexeme);
    if (name == NULL) {
        parser_error_at(parser, location_from_token(token), token->lexeme,
                        "I ran out of memory while storing a name.");
    }
    return name;
}

static char *copy_key_from_token(Parser *parser, const Token *token) {
    if (token == NULL) {
        parser_error_at(parser, (SourceLocation) {0, NULL, NULL}, NULL,
                        "I expected a record field name here.");
        return NULL;
    }

    if (token->type == TOKEN_WORD || token->type == TOKEN_STRING) {
        char *copy = duplicate_text(token->lexeme);
        if (copy == NULL) {
            parser_error_at(parser, location_from_token(token), token->lexeme,
                            "I ran out of memory while storing a record field name.");
        }
        return copy;
    }

    parser_error_at(parser, location_from_token(token), token->lexeme,
                    "A record field name should be a word or a quoted string.");
    return NULL;
}

static Expression *create_expression(SourceLocation location, ExpressionType type) {
    Expression *expression = (Expression *) calloc(1, sizeof(Expression));

    if (expression == NULL) {
        return NULL;
    }

    expression->location = location;
    expression->type = type;
    return expression;
}

static Statement *create_statement(SourceLocation location, StatementType type) {
    Statement *statement = (Statement *) calloc(1, sizeof(Statement));

    if (statement == NULL) {
        return NULL;
    }

    statement->location = location;
    statement->type = type;
    return statement;
}

static bool append_statement(Statement ***statements, int *count, Statement *statement) {
    Statement **new_items = (Statement **) realloc(*statements, (size_t) (*count + 1) * sizeof(Statement *));

    if (new_items == NULL) {
        return false;
    }

    *statements = new_items;
    (*statements)[*count] = statement;
    *count += 1;
    return true;
}

static bool append_expression(Expression ***items, int *count, Expression *item) {
    Expression **new_items = (Expression **) realloc(*items, (size_t) (*count + 1) * sizeof(Expression *));

    if (new_items == NULL) {
        return false;
    }

    *items = new_items;
    (*items)[*count] = item;
    *count += 1;
    return true;
}

static bool append_name(char ***items, int *count, char *item) {
    char **new_items = (char **) realloc(*items, (size_t) (*count + 1) * sizeof(char *));

    if (new_items == NULL) {
        return false;
    }

    *items = new_items;
    (*items)[*count] = item;
    *count += 1;
    return true;
}

static void free_name_list(char **items, int count) {
    int index;
    for (index = 0; index < count; index += 1) {
        free(items[index]);
    }
    free(items);
}

static const Token *expr_peek(ExpressionParser *expr_parser) {
    if (expr_parser->current >= expr_parser->end) {
        return NULL;
    }
    return &expr_parser->line->tokens[expr_parser->current];
}

static const Token *expr_previous(ExpressionParser *expr_parser) {
    if (expr_parser->current <= 0) {
        return NULL;
    }
    return &expr_parser->line->tokens[expr_parser->current - 1];
}

static bool expr_is_at_end(ExpressionParser *expr_parser) {
    return expr_parser->current >= expr_parser->end;
}

static const Token *expr_advance(ExpressionParser *expr_parser) {
    if (!expr_is_at_end(expr_parser)) {
        expr_parser->current += 1;
    }
    return expr_previous(expr_parser);
}

static bool expr_match_word(ExpressionParser *expr_parser, const char *word) {
    const Token *token = expr_peek(expr_parser);
    if (!token_is_word(token, word)) {
        return false;
    }
    expr_advance(expr_parser);
    return true;
}

static bool expr_match_symbol(ExpressionParser *expr_parser, const char *symbol) {
    const Token *token = expr_peek(expr_parser);
    if (!token_is_symbol(token, symbol)) {
        return false;
    }
    expr_advance(expr_parser);
    return true;
}

static Expression *parse_expression_internal(ExpressionParser *expr_parser);
static Expression *parse_expression_range(Parser *parser, const LexedLine *line, int start, int end);
static Expression *parse_unary(ExpressionParser *expr_parser);
static Statement *parse_if_statement_from_tokens(Parser *parser, const LexedLine *line, int start_index);

static Expression *make_number_expression(SourceLocation location, double number) {
    Expression *expression = create_expression(location, EXPR_NUMBER);
    if (expression != NULL) {
        expression->as.number = number;
    }
    return expression;
}

static Expression *make_string_expression(SourceLocation location, const char *text) {
    Expression *expression = create_expression(location, EXPR_STRING);

    if (expression == NULL) {
        return NULL;
    }

    expression->as.string = duplicate_text(text);
    if (expression->as.string == NULL) {
        free(expression);
        return NULL;
    }

    return expression;
}

static Expression *make_boolean_expression(SourceLocation location, bool value) {
    Expression *expression = create_expression(location, EXPR_BOOLEAN);
    if (expression != NULL) {
        expression->as.boolean = value;
    }
    return expression;
}

static Expression *make_none_expression(SourceLocation location) {
    return create_expression(location, EXPR_NONE);
}

static Expression *make_variable_expression(SourceLocation location, const char *name) {
    Expression *expression = create_expression(location, EXPR_VARIABLE);

    if (expression == NULL) {
        return NULL;
    }

    expression->as.variable_name = duplicate_text(name);
    if (expression->as.variable_name == NULL) {
        free(expression);
        return NULL;
    }

    return expression;
}

static Expression *make_grouping_expression(SourceLocation location, Expression *inner) {
    Expression *expression = create_expression(location, EXPR_GROUPING);
    if (expression != NULL) {
        expression->as.grouping.inner = inner;
    }
    return expression;
}

static Expression *make_unary_expression(SourceLocation location, UnaryOperator op, Expression *right) {
    Expression *expression = create_expression(location, EXPR_UNARY);
    if (expression != NULL) {
        expression->as.unary.op = op;
        expression->as.unary.right = right;
    }
    return expression;
}

static Expression *make_binary_expression(SourceLocation location, BinaryOperator op,
                                          Expression *left, Expression *right) {
    Expression *expression = create_expression(location, EXPR_BINARY);
    if (expression != NULL) {
        expression->as.binary.op = op;
        expression->as.binary.left = left;
        expression->as.binary.right = right;
    }
    return expression;
}

static Expression *make_comparison_expression(SourceLocation location, ComparisonOperator op,
                                              Expression *left, Expression *right) {
    Expression *expression = create_expression(location, EXPR_COMPARISON);
    if (expression != NULL) {
        expression->as.comparison.op = op;
        expression->as.comparison.left = left;
        expression->as.comparison.right = right;
    }
    return expression;
}

static Expression *make_call_expression(SourceLocation location, char *name,
                                        Expression **arguments, int arg_count) {
    Expression *expression = create_expression(location, EXPR_CALL);
    if (expression != NULL) {
        expression->as.call.name = name;
        expression->as.call.arguments = arguments;
        expression->as.call.arg_count = arg_count;
    }
    return expression;
}

static Expression *make_item_access_expression(SourceLocation location,
                                               Expression *index,
                                               Expression *collection) {
    Expression *expression = create_expression(location, EXPR_ITEM_ACCESS);
    if (expression != NULL) {
        expression->as.item_access.index = index;
        expression->as.item_access.collection = collection;
    }
    return expression;
}

static Expression *make_field_access_expression(SourceLocation location,
                                                char *field_name,
                                                Expression *record) {
    Expression *expression = create_expression(location, EXPR_FIELD_ACCESS);
    if (expression != NULL) {
        expression->as.field_access.field_name = field_name;
        expression->as.field_access.record = record;
    }
    return expression;
}

static Expression *make_list_expression(SourceLocation location, Expression **items, int item_count) {
    Expression *expression = create_expression(location, EXPR_LIST);
    if (expression != NULL) {
        expression->as.list.items = items;
        expression->as.list.item_count = item_count;
    }
    return expression;
}

static Expression *make_record_expression(SourceLocation location, char **keys,
                                          Expression **values, int count) {
    Expression *expression = create_expression(location, EXPR_RECORD);
    if (expression != NULL) {
        expression->as.record.keys = keys;
        expression->as.record.values = values;
        expression->as.record.count = count;
    }
    return expression;
}

static int find_top_level_word_in_range(const LexedLine *line, int start, int end, const char *word) {
    int index;
    int depth = 0;

    for (index = start; index < end; index += 1) {
        const Token *token = &line->tokens[index];

        if (token_is_symbol(token, "(")) {
            depth += 1;
            continue;
        }
        if (token_is_symbol(token, ")")) {
            if (depth > 0) {
                depth -= 1;
            }
            continue;
        }
        if (depth == 0 && token_is_word(token, word)) {
            return index;
        }
    }

    return -1;
}

static Expression *parse_primary(ExpressionParser *expr_parser) {
    const Token *token = expr_peek(expr_parser);

    if (token == NULL) {
        parser_error_at(expr_parser->parser, location_from_line(expr_parser->line), NULL,
                        "I expected a value here, but the expression ended too soon.");
        return NULL;
    }

    if (token->type == TOKEN_NUMBER) {
        expr_advance(expr_parser);
        return make_number_expression(location_from_token(token), strtod(token->lexeme, NULL));
    }

    if (token->type == TOKEN_STRING) {
        Expression *expression;
        expr_advance(expr_parser);
        expression = make_string_expression(location_from_token(token), token->lexeme);
        if (expression == NULL) {
            parser_error_at(expr_parser->parser, location_from_token(token), token->lexeme,
                            "I ran out of memory while storing a string.");
        }
        return expression;
    }

    if (token_is_word(token, "true")) {
        expr_advance(expr_parser);
        return make_boolean_expression(location_from_token(token), true);
    }

    if (token_is_word(token, "false")) {
        expr_advance(expr_parser);
        return make_boolean_expression(location_from_token(token), false);
    }

    if (token_is_word(token, "nothing")) {
        expr_advance(expr_parser);
        return make_none_expression(location_from_token(token));
    }

    if (token_is_symbol(token, "(")) {
        Expression *inner;
        Expression *grouping;
        expr_advance(expr_parser);
        inner = parse_expression_internal(expr_parser);
        if (inner == NULL) {
            return NULL;
        }
        if (!expr_match_symbol(expr_parser, ")")) {
            free_expression(inner);
            parser_error_at(expr_parser->parser, location_from_token(token), token->lexeme,
                            "I expected a closing ')' to finish this grouped expression.");
            return NULL;
        }
        grouping = make_grouping_expression(location_from_token(token), inner);
        if (grouping == NULL) {
            free_expression(inner);
            parser_error_at(expr_parser->parser, location_from_token(token), token->lexeme,
                            "I ran out of memory while building a grouped expression.");
        }
        return grouping;
    }

    if (token_is_word(token, "call")) {
        char *name;
        Expression **arguments = NULL;
        int arg_count = 0;
        Expression *call_expression;
        SourceLocation location = location_from_token(token);

        expr_advance(expr_parser);
        token = expr_peek(expr_parser);
        name = copy_name_from_token(expr_parser->parser, token, true);
        if (name == NULL) {
            return NULL;
        }
        expr_advance(expr_parser);

        if (expr_match_word(expr_parser, "with")) {
            while (!expr_is_at_end(expr_parser)) {
                Expression *argument = parse_expression_internal(expr_parser);
                if (argument == NULL) {
                    int i;
                    free(name);
                    for (i = 0; i < arg_count; i += 1) {
                        free_expression(arguments[i]);
                    }
                    free(arguments);
                    return NULL;
                }
                if (!append_expression(&arguments, &arg_count, argument)) {
                    int i;
                    free_expression(argument);
                    free(name);
                    for (i = 0; i < arg_count; i += 1) {
                        free_expression(arguments[i]);
                    }
                    free(arguments);
                    parser_error_at(expr_parser->parser, location, NULL,
                                    "I ran out of memory while storing function arguments.");
                    return NULL;
                }
                if (!expr_match_symbol(expr_parser, ",")) {
                    break;
                }
            }
        }

        call_expression = make_call_expression(location, name, arguments, arg_count);
        if (call_expression == NULL) {
            int i;
            free(name);
            for (i = 0; i < arg_count; i += 1) {
                free_expression(arguments[i]);
            }
            free(arguments);
            parser_error_at(expr_parser->parser, location, NULL,
                            "I ran out of memory while building a function call.");
        }
        return call_expression;
    }

    if (token_is_word(token, "item") &&
        find_top_level_word_in_range(expr_parser->line,
                                     expr_parser->current + 1,
                                     expr_parser->end,
                                     "of") >= 0) {
        Expression *index_expression;
        Expression *collection_expression;
        Expression *item_expression;
        SourceLocation location = location_from_token(token);

        expr_advance(expr_parser);
        index_expression = parse_unary(expr_parser);
        if (index_expression == NULL) {
            return NULL;
        }

        if (!expr_match_word(expr_parser, "of")) {
            free_expression(index_expression);
            parser_error_at(expr_parser->parser, location, token->lexeme,
                            "An item access should look like: item position of collection");
            return NULL;
        }

        collection_expression = parse_primary(expr_parser);
        if (collection_expression == NULL) {
            free_expression(index_expression);
            return NULL;
        }

        item_expression = make_item_access_expression(location, index_expression, collection_expression);
        if (item_expression == NULL) {
            free_expression(index_expression);
            free_expression(collection_expression);
            parser_error_at(expr_parser->parser, location, token->lexeme,
                            "I ran out of memory while building an item access.");
            return NULL;
        }

        return item_expression;
    }

    if (token_is_word(token, "field") &&
        expr_parser->current + 2 < expr_parser->end &&
        (expr_parser->line->tokens[expr_parser->current + 1].type == TOKEN_WORD ||
         expr_parser->line->tokens[expr_parser->current + 1].type == TOKEN_STRING) &&
        token_is_word(&expr_parser->line->tokens[expr_parser->current + 2], "of")) {
        char *field_name;
        Expression *record_expression;
        Expression *field_expression;
        SourceLocation location = location_from_token(token);

        expr_advance(expr_parser);
        field_name = copy_key_from_token(expr_parser->parser, expr_peek(expr_parser));
        if (field_name == NULL) {
            return NULL;
        }

        expr_advance(expr_parser);
        if (!expr_match_word(expr_parser, "of")) {
            free(field_name);
            parser_error_at(expr_parser->parser, location, token->lexeme,
                            "A field access should look like: field name of record");
            return NULL;
        }

        record_expression = parse_primary(expr_parser);
        if (record_expression == NULL) {
            free(field_name);
            return NULL;
        }

        field_expression = make_field_access_expression(location, field_name, record_expression);
        if (field_expression == NULL) {
            free(field_name);
            free_expression(record_expression);
            parser_error_at(expr_parser->parser, location, token->lexeme,
                            "I ran out of memory while building a field access.");
            return NULL;
        }

        return field_expression;
    }

    if (token_is_word(token, "list")) {
        Expression **items = NULL;
        int item_count = 0;
        Expression *list_expression;
        SourceLocation location = location_from_token(token);

        expr_advance(expr_parser);
        if (expr_match_word(expr_parser, "of")) {
            while (!expr_is_at_end(expr_parser)) {
                Expression *item = parse_expression_internal(expr_parser);
                if (item == NULL) {
                    int i;
                    for (i = 0; i < item_count; i += 1) {
                        free_expression(items[i]);
                    }
                    free(items);
                    return NULL;
                }
                if (!append_expression(&items, &item_count, item)) {
                    int i;
                    free_expression(item);
                    for (i = 0; i < item_count; i += 1) {
                        free_expression(items[i]);
                    }
                    free(items);
                    parser_error_at(expr_parser->parser, location, NULL,
                                    "I ran out of memory while building a list.");
                    return NULL;
                }
                if (!expr_match_symbol(expr_parser, ",")) {
                    break;
                }
            }
        }

        list_expression = make_list_expression(location, items, item_count);
        if (list_expression == NULL) {
            int i;
            for (i = 0; i < item_count; i += 1) {
                free_expression(items[i]);
            }
            free(items);
            parser_error_at(expr_parser->parser, location, NULL,
                            "I ran out of memory while building a list.");
        }
        return list_expression;
    }

    if (token_is_word(token, "record")) {
        char **keys = NULL;
        Expression **values = NULL;
        int count = 0;
        Expression *record_expression;
        SourceLocation location = location_from_token(token);

        expr_advance(expr_parser);
        if (expr_match_word(expr_parser, "of")) {
            while (!expr_is_at_end(expr_parser)) {
                char *key;
                Expression *value;
                token = expr_peek(expr_parser);
                key = copy_key_from_token(expr_parser->parser, token);
                if (key == NULL) {
                    free_name_list(keys, count);
                    free(values);
                    return NULL;
                }
                expr_advance(expr_parser);

                if (!expr_match_word(expr_parser, "is")) {
                    free(key);
                    free_name_list(keys, count);
                    free(values);
                    parser_error_at(expr_parser->parser, location, NULL,
                                    "Record fields should look like: name is value");
                    return NULL;
                }

                value = parse_expression_internal(expr_parser);
                if (value == NULL) {
                    free(key);
                    free_name_list(keys, count);
                    free(values);
                    return NULL;
                }

                {
                    char **expanded_keys = (char **) realloc(keys, (size_t) (count + 1) * sizeof(char *));
                    Expression **expanded_values;

                    if (expanded_keys == NULL) {
                        free(key);
                        free_expression(value);
                        free_name_list(keys, count);
                        free(values);
                        parser_error_at(expr_parser->parser, location, NULL,
                                        "I ran out of memory while building a record.");
                        return NULL;
                    }
                    keys = expanded_keys;

                    expanded_values = (Expression **) realloc(values,
                                                              (size_t) (count + 1) * sizeof(Expression *));
                    if (expanded_values == NULL) {
                        free(key);
                        free_expression(value);
                        free_name_list(keys, count);
                        free(values);
                        parser_error_at(expr_parser->parser, location, NULL,
                                        "I ran out of memory while building a record.");
                        return NULL;
                    }
                    values = expanded_values;
                }

                keys[count] = key;
                values[count] = value;
                count += 1;

                if (!expr_match_symbol(expr_parser, ",")) {
                    break;
                }
            }
        }

        record_expression = make_record_expression(location, keys, values, count);
        if (record_expression == NULL) {
            int i;
            for (i = 0; i < count; i += 1) {
                free(keys[i]);
                free_expression(values[i]);
            }
            free(keys);
            free(values);
            parser_error_at(expr_parser->parser, location, NULL,
                            "I ran out of memory while building a record.");
        }
        return record_expression;
    }

    if (token->type == TOKEN_WORD) {
        Expression *expression = make_variable_expression(location_from_token(token), token->lexeme);
        expr_advance(expr_parser);
        if (expression == NULL) {
            parser_error_at(expr_parser->parser, location_from_token(token), token->lexeme,
                            "I ran out of memory while storing a variable name.");
        }
        return expression;
    }

    parser_error_at(expr_parser->parser, location_from_token(token), token->lexeme,
                    "I do not understand this value yet.");
    return NULL;
}

static Expression *parse_unary(ExpressionParser *expr_parser) {
    const Token *token = expr_peek(expr_parser);
    UnaryOperator op;
    Expression *right;
    Expression *expression;

    if (token_is_word(token, "minus")) {
        op = UNARY_NEGATE;
        expr_advance(expr_parser);
    } else if (token_is_word(token, "not")) {
        op = UNARY_NOT;
        expr_advance(expr_parser);
    } else {
        return parse_primary(expr_parser);
    }

    right = parse_unary(expr_parser);
    if (right == NULL) {
        return NULL;
    }

    expression = make_unary_expression(location_from_token(token), op, right);
    if (expression == NULL) {
        free_expression(right);
        parser_error_at(expr_parser->parser, location_from_token(token), token->lexeme,
                        "I ran out of memory while building this expression.");
    }
    return expression;
}

static Expression *parse_power(ExpressionParser *expr_parser) {
    Expression *left = parse_unary(expr_parser);

    if (left == NULL) {
        return NULL;
    }

    if (!expr_is_at_end(expr_parser) && token_is_word(expr_peek(expr_parser), "power")) {
        const Token *token = expr_advance(expr_parser);
        Expression *right = parse_power(expr_parser);
        Expression *combined;

        if (right == NULL) {
            free_expression(left);
            return NULL;
        }

        combined = make_binary_expression(location_from_token(token), BIN_POWER, left, right);
        if (combined == NULL) {
            free_expression(left);
            free_expression(right);
            parser_error_at(expr_parser->parser, location_from_token(token), token->lexeme,
                            "I ran out of memory while building a math expression.");
            return NULL;
        }
        return combined;
    }

    return left;
}

static Expression *parse_multiplicative(ExpressionParser *expr_parser) {
    Expression *expression = parse_power(expr_parser);

    if (expression == NULL) {
        return NULL;
    }

    while (!expr_is_at_end(expr_parser)) {
        const Token *token = expr_peek(expr_parser);
        BinaryOperator op;
        Expression *right;
        Expression *combined;

        if (token_is_word(token, "times")) {
            op = BIN_MULTIPLY;
            expr_advance(expr_parser);
        } else if (token_is_word(token, "divided")) {
            op = BIN_DIVIDE;
            expr_advance(expr_parser);
            (void) expr_match_word(expr_parser, "by");
        } else if (token_is_word(token, "mod")) {
            op = BIN_MODULO;
            expr_advance(expr_parser);
        } else {
            break;
        }

        right = parse_power(expr_parser);
        if (right == NULL) {
            free_expression(expression);
            return NULL;
        }

        combined = make_binary_expression(location_from_token(token), op, expression, right);
        if (combined == NULL) {
            free_expression(expression);
            free_expression(right);
            parser_error_at(expr_parser->parser, location_from_token(token), token->lexeme,
                            "I ran out of memory while building a math expression.");
            return NULL;
        }

        expression = combined;
    }

    return expression;
}

static Expression *parse_additive(ExpressionParser *expr_parser) {
    Expression *expression = parse_multiplicative(expr_parser);

    if (expression == NULL) {
        return NULL;
    }

    while (!expr_is_at_end(expr_parser)) {
        const Token *token = expr_peek(expr_parser);
        BinaryOperator op;
        Expression *right;
        Expression *combined;

        if (token_is_word(token, "plus")) {
            op = BIN_ADD;
            expr_advance(expr_parser);
        } else if (token_is_word(token, "minus")) {
            op = BIN_SUBTRACT;
            expr_advance(expr_parser);
        } else {
            break;
        }

        right = parse_multiplicative(expr_parser);
        if (right == NULL) {
            free_expression(expression);
            return NULL;
        }

        combined = make_binary_expression(location_from_token(token), op, expression, right);
        if (combined == NULL) {
            free_expression(expression);
            free_expression(right);
            parser_error_at(expr_parser->parser, location_from_token(token), token->lexeme,
                            "I ran out of memory while building a math expression.");
            return NULL;
        }

        expression = combined;
    }

    return expression;
}

static Expression *parse_comparison(ExpressionParser *expr_parser) {
    Expression *expression = parse_additive(expr_parser);

    if (expression == NULL) {
        return NULL;
    }

    while (!expr_is_at_end(expr_parser)) {
        const Token *token = expr_peek(expr_parser);
        ComparisonOperator op;
        Expression *right;
        Expression *combined;

        if (token_is_word(token, "contains")) {
            expr_advance(expr_parser);
            op = CMP_CONTAINS;
        } else if (token_is_word(token, "is")) {
            expr_advance(expr_parser);
            if (expr_match_word(expr_parser, "greater")) {
                if (!expr_match_word(expr_parser, "than")) {
                    free_expression(expression);
                    parser_error_at(expr_parser->parser, location_from_token(token), token->lexeme,
                                    "I expected 'than' after 'is greater'.");
                    return NULL;
                }
                op = CMP_GREATER_THAN;
            } else if (expr_match_word(expr_parser, "less")) {
                if (!expr_match_word(expr_parser, "than")) {
                    free_expression(expression);
                    parser_error_at(expr_parser->parser, location_from_token(token), token->lexeme,
                                    "I expected 'than' after 'is less'.");
                    return NULL;
                }
                op = CMP_LESS_THAN;
            } else if (expr_match_word(expr_parser, "equal")) {
                if (!expr_match_word(expr_parser, "to")) {
                    free_expression(expression);
                    parser_error_at(expr_parser->parser, location_from_token(token), token->lexeme,
                                    "I expected 'to' after 'is equal'.");
                    return NULL;
                }
                op = CMP_EQUAL;
            } else if (expr_match_word(expr_parser, "not")) {
                if (!expr_match_word(expr_parser, "equal") || !expr_match_word(expr_parser, "to")) {
                    free_expression(expression);
                    parser_error_at(expr_parser->parser, location_from_token(token), token->lexeme,
                                    "I expected 'equal to' after 'is not'.");
                    return NULL;
                }
                op = CMP_NOT_EQUAL;
            } else if (expr_match_word(expr_parser, "at")) {
                if (expr_match_word(expr_parser, "least")) {
                    op = CMP_AT_LEAST;
                } else if (expr_match_word(expr_parser, "most")) {
                    op = CMP_AT_MOST;
                } else {
                    free_expression(expression);
                    parser_error_at(expr_parser->parser, location_from_token(token), token->lexeme,
                                    "I expected 'least' or 'most' after 'is at'.");
                    return NULL;
                }
            } else {
                free_expression(expression);
                parser_error_at(expr_parser->parser, location_from_token(token), token->lexeme,
                                "I expected a comparison like 'is equal to' or 'is greater than'.");
                return NULL;
            }
        } else {
            break;
        }

        right = parse_additive(expr_parser);
        if (right == NULL) {
            free_expression(expression);
            return NULL;
        }

        combined = make_comparison_expression(location_from_token(token), op, expression, right);
        if (combined == NULL) {
            free_expression(expression);
            free_expression(right);
            parser_error_at(expr_parser->parser, location_from_token(token), token->lexeme,
                            "I ran out of memory while building a comparison.");
            return NULL;
        }

        expression = combined;
    }

    return expression;
}

static Expression *parse_and(ExpressionParser *expr_parser) {
    Expression *expression = parse_comparison(expr_parser);

    if (expression == NULL) {
        return NULL;
    }

    while (!expr_is_at_end(expr_parser) && token_is_word(expr_peek(expr_parser), "and")) {
        const Token *token = expr_advance(expr_parser);
        Expression *right = parse_comparison(expr_parser);
        Expression *combined;

        if (right == NULL) {
            free_expression(expression);
            return NULL;
        }

        combined = make_binary_expression(location_from_token(token), BIN_AND, expression, right);
        if (combined == NULL) {
            free_expression(expression);
            free_expression(right);
            parser_error_at(expr_parser->parser, location_from_token(token), token->lexeme,
                            "I ran out of memory while building a logical expression.");
            return NULL;
        }

        expression = combined;
    }

    return expression;
}

static Expression *parse_or(ExpressionParser *expr_parser) {
    Expression *expression = parse_and(expr_parser);

    if (expression == NULL) {
        return NULL;
    }

    while (!expr_is_at_end(expr_parser) && token_is_word(expr_peek(expr_parser), "or")) {
        const Token *token = expr_advance(expr_parser);
        Expression *right = parse_and(expr_parser);
        Expression *combined;

        if (right == NULL) {
            free_expression(expression);
            return NULL;
        }

        combined = make_binary_expression(location_from_token(token), BIN_OR, expression, right);
        if (combined == NULL) {
            free_expression(expression);
            free_expression(right);
            parser_error_at(expr_parser->parser, location_from_token(token), token->lexeme,
                            "I ran out of memory while building a logical expression.");
            return NULL;
        }

        expression = combined;
    }

    return expression;
}

static Expression *parse_expression_internal(ExpressionParser *expr_parser) {
    return parse_or(expr_parser);
}

static Expression *parse_expression_range(Parser *parser, const LexedLine *line, int start, int end) {
    ExpressionParser expr_parser;
    Expression *expression;

    if (start >= end) {
        parser_error_at(parser, location_from_line(line), NULL,
                        "I expected an expression here, but the line ended.");
        return NULL;
    }

    expr_parser.parser = parser;
    expr_parser.line = line;
    expr_parser.current = start;
    expr_parser.end = end;

    expression = parse_expression_internal(&expr_parser);
    if (expression == NULL) {
        return NULL;
    }

    if (expr_parser.current < expr_parser.end) {
        parser_error_at(parser, location_from_line(line), line->tokens[expr_parser.current].lexeme,
                        "I got confused near '%s'. Check the expression on this line.",
                        line->tokens[expr_parser.current].lexeme);
        free_expression(expression);
        return NULL;
    }

    return expression;
}

static int trim_optional_last_word(const LexedLine *line, int start, int end, const char *word) {
    if (end > start && token_is_word(&line->tokens[end - 1], word)) {
        return end - 1;
    }
    return end;
}

static int find_and_store_in(const LexedLine *line, int start, int end) {
    int index;
    for (index = start; index + 2 < end; index += 1) {
        if (token_is_word(&line->tokens[index], "and") &&
            token_is_word(&line->tokens[index + 1], "store") &&
            token_is_word(&line->tokens[index + 2], "in")) {
            return index;
        }
    }
    return -1;
}

static bool parse_parameter_list(Parser *parser, const LexedLine *line, int start,
                                 char ***out_parameters, int *out_count) {
    char **parameters = NULL;
    int count = 0;
    int index = start;

    while (index < line->count) {
        char *name;

        name = copy_name_from_token(parser, &line->tokens[index], false);
        if (name == NULL) {
            free_name_list(parameters, count);
            return false;
        }

        if (!append_name(&parameters, &count, name)) {
            free(name);
            free_name_list(parameters, count);
            parser_error_at(parser, location_from_line(line), NULL,
                            "I ran out of memory while storing function parameters.");
            return false;
        }

        index += 1;
        if (index >= line->count) {
            break;
        }

        if (!token_is_symbol(&line->tokens[index], ",")) {
            free_name_list(parameters, count);
            parser_error_at(parser, location_from_line(line), line->tokens[index].lexeme,
                            "Function parameters should be separated with commas.");
            return false;
        }

        index += 1;
        if (index >= line->count) {
            free_name_list(parameters, count);
            parser_error_at(parser, location_from_line(line), NULL,
                            "I expected another parameter name after the comma.");
            return false;
        }
    }

    *out_parameters = parameters;
    *out_count = count;
    return true;
}

static Statement *parse_statement(Parser *parser);

static Statement **parse_statement_list(Parser *parser, bool inside_block, bool allow_else,
                                        const char *block_name, SourceLocation block_location,
                                        int *out_count, BlockTerminator *out_terminator) {
    Statement **statements = NULL;
    int count = 0;

    while (parser->current_line < parser->program->count) {
        const LexedLine *line = &parser->program->lines[parser->current_line];
        Statement *statement;

        if (line->count == 0) {
            parser->current_line += 1;
            continue;
        }

        if (token_is_word(&line->tokens[0], "end")) {
            if (!inside_block) {
                parser_error_at(parser, location_from_line(line), line->tokens[0].lexeme,
                                "I found 'end', but there is no open block to close here.");
                free_statement_list(statements, count);
                return NULL;
            }
            *out_count = count;
            *out_terminator = BLOCK_TERMINATOR_END;
            return statements;
        }

        if (token_is_word(&line->tokens[0], "else")) {
            if (!inside_block || !allow_else) {
                parser_error_at(parser, location_from_line(line), line->tokens[0].lexeme,
                                "I found 'else', but it does not belong to an if block here.");
                free_statement_list(statements, count);
                return NULL;
            }
            *out_count = count;
            *out_terminator = BLOCK_TERMINATOR_ELSE;
            return statements;
        }

        statement = parse_statement(parser);
        if (parser->had_error) {
            free_statement_list(statements, count);
            return NULL;
        }

        if (statement != NULL) {
            if (!append_statement(&statements, &count, statement)) {
                free_statement(statement);
                free_statement_list(statements, count);
                parser_error_at(parser, location_from_line(line), NULL,
                                "I ran out of memory while storing statements.");
                return NULL;
            }
        }
    }

    if (inside_block) {
        parser_error_at(parser, block_location, NULL,
                        "The %s block that started here needs a matching 'end'.",
                        block_name);
        free_statement_list(statements, count);
        return NULL;
    }

    *out_count = count;
    *out_terminator = BLOCK_TERMINATOR_NONE;
    return statements;
}

static Statement *parse_use_statement(Parser *parser, const LexedLine *line) {
    Statement *statement;
    char *path;

    if (line->count != 2 || line->tokens[1].type != TOKEN_STRING) {
        parser_error_at(parser, location_from_line(line), NULL,
                        "A use statement should look like: use \"path/to/file.elang\"");
        return NULL;
    }

    path = duplicate_text(line->tokens[1].lexeme);
    if (path == NULL) {
        parser_error_at(parser, location_from_line(line), NULL,
                        "I ran out of memory while storing an import path.");
        return NULL;
    }

    statement = create_statement(location_from_line(line), STMT_USE);
    if (statement == NULL) {
        free(path);
        parser_error_at(parser, location_from_line(line), NULL,
                        "I ran out of memory while building a use statement.");
        return NULL;
    }

    statement->as.use_stmt.path = path;
    parser->current_line += 1;
    return statement;
}

static Statement *parse_let_statement(Parser *parser, const LexedLine *line) {
    Statement *statement;
    char *name;
    Expression *value;

    if (line->count < 4) {
        parser_error_at(parser, location_from_line(line), NULL,
                        "A let statement should look like: let name be value");
        return NULL;
    }

    name = copy_name_from_token(parser, &line->tokens[1], false);
    if (name == NULL) {
        return NULL;
    }

    if (!token_is_word(&line->tokens[2], "be")) {
        free(name);
        parser_error_at(parser, location_from_line(line), line->tokens[2].lexeme,
                        "After the variable name, I expected the word 'be'.");
        return NULL;
    }

    value = parse_expression_range(parser, line, 3, line->count);
    if (value == NULL) {
        free(name);
        return NULL;
    }

    statement = create_statement(location_from_line(line), STMT_LET);
    if (statement == NULL) {
        free(name);
        free_expression(value);
        parser_error_at(parser, location_from_line(line), NULL,
                        "I ran out of memory while building a let statement.");
        return NULL;
    }

    statement->as.let_stmt.name = name;
    statement->as.let_stmt.value = value;
    parser->current_line += 1;
    return statement;
}

static Statement *parse_append_statement(Parser *parser, const LexedLine *line) {
    Statement *statement;
    char *name;
    Expression *value;

    if (line->count < 4 ||
        !token_is_word(&line->tokens[line->count - 2], "to") ||
        line->tokens[line->count - 1].type != TOKEN_WORD) {
        parser_error_at(parser, location_from_line(line), NULL,
                        "An append statement should look like: append value to list_name");
        return NULL;
    }

    value = parse_expression_range(parser, line, 1, line->count - 2);
    if (value == NULL) {
        return NULL;
    }

    name = copy_name_from_token(parser, &line->tokens[line->count - 1], false);
    if (name == NULL) {
        free_expression(value);
        return NULL;
    }

    statement = create_statement(location_from_line(line), STMT_APPEND);
    if (statement == NULL) {
        free(name);
        free_expression(value);
        parser_error_at(parser, location_from_line(line), NULL,
                        "I ran out of memory while building an append statement.");
        return NULL;
    }

    statement->as.append_stmt.name = name;
    statement->as.append_stmt.value = value;
    parser->current_line += 1;
    return statement;
}

static Statement *parse_set_item_statement(Parser *parser, const LexedLine *line) {
    int index;
    int to_index = -1;
    int depth = 0;
    Statement *statement;
    char *name;
    Expression *item_index;
    Expression *value;

    if (line->count < 7) {
        parser_error_at(parser, location_from_line(line), NULL,
                        "A set item statement should look like: set item position of list_name to value");
        return NULL;
    }

    for (index = 2; index < line->count; index += 1) {
        const Token *token = &line->tokens[index];

        if (token_is_symbol(token, "(")) {
            depth += 1;
            continue;
        }
        if (token_is_symbol(token, ")")) {
            if (depth > 0) {
                depth -= 1;
            }
            continue;
        }
        if (depth == 0 &&
            token_is_word(token, "to") &&
            index >= 5 &&
            token_is_word(&line->tokens[index - 2], "of") &&
            line->tokens[index - 1].type == TOKEN_WORD) {
            to_index = index;
            break;
        }
    }

    if (to_index < 0 || to_index + 1 >= line->count) {
        parser_error_at(parser, location_from_line(line), NULL,
                        "A set item statement should look like: set item position of list_name to value");
        return NULL;
    }

    item_index = parse_expression_range(parser, line, 2, to_index - 2);
    if (item_index == NULL) {
        return NULL;
    }

    name = copy_name_from_token(parser, &line->tokens[to_index - 1], false);
    if (name == NULL) {
        free_expression(item_index);
        return NULL;
    }

    value = parse_expression_range(parser, line, to_index + 1, line->count);
    if (value == NULL) {
        free(name);
        free_expression(item_index);
        return NULL;
    }

    statement = create_statement(location_from_line(line), STMT_SET_ITEM);
    if (statement == NULL) {
        free(name);
        free_expression(item_index);
        free_expression(value);
        parser_error_at(parser, location_from_line(line), NULL,
                        "I ran out of memory while building a set item statement.");
        return NULL;
    }

    statement->as.set_item_stmt.name = name;
    statement->as.set_item_stmt.index = item_index;
    statement->as.set_item_stmt.value = value;
    parser->current_line += 1;
    return statement;
}

static Statement *parse_set_field_statement(Parser *parser, const LexedLine *line) {
    Statement *statement;
    char *field_name;
    char *name;
    Expression *value;

    if (line->count < 7 ||
        (line->tokens[2].type != TOKEN_WORD && line->tokens[2].type != TOKEN_STRING) ||
        !token_is_word(&line->tokens[3], "of") ||
        line->tokens[4].type != TOKEN_WORD ||
        !token_is_word(&line->tokens[5], "to")) {
        parser_error_at(parser, location_from_line(line), NULL,
                        "A set field statement should look like: set field name of record_name to value");
        return NULL;
    }

    field_name = copy_key_from_token(parser, &line->tokens[2]);
    if (field_name == NULL) {
        return NULL;
    }

    name = copy_name_from_token(parser, &line->tokens[4], false);
    if (name == NULL) {
        free(field_name);
        return NULL;
    }

    value = parse_expression_range(parser, line, 6, line->count);
    if (value == NULL) {
        free(field_name);
        free(name);
        return NULL;
    }

    statement = create_statement(location_from_line(line), STMT_SET_FIELD);
    if (statement == NULL) {
        free(field_name);
        free(name);
        free_expression(value);
        parser_error_at(parser, location_from_line(line), NULL,
                        "I ran out of memory while building a set field statement.");
        return NULL;
    }

    statement->as.set_field_stmt.name = name;
    statement->as.set_field_stmt.field_name = field_name;
    statement->as.set_field_stmt.value = value;
    parser->current_line += 1;
    return statement;
}

static Statement *parse_set_statement(Parser *parser, const LexedLine *line) {
    Statement *statement;
    char *name;
    Expression *value;

    if (line->count > 1 && token_is_word(&line->tokens[1], "item")) {
        return parse_set_item_statement(parser, line);
    }

    if (line->count > 1 && token_is_word(&line->tokens[1], "field")) {
        return parse_set_field_statement(parser, line);
    }

    if (line->count < 4) {
        parser_error_at(parser, location_from_line(line), NULL,
                        "A set statement should look like: set name to value");
        return NULL;
    }

    name = copy_name_from_token(parser, &line->tokens[1], false);
    if (name == NULL) {
        return NULL;
    }

    if (!token_is_word(&line->tokens[2], "to")) {
        free(name);
        parser_error_at(parser, location_from_line(line), line->tokens[2].lexeme,
                        "After the variable name, I expected the word 'to'.");
        return NULL;
    }

    value = parse_expression_range(parser, line, 3, line->count);
    if (value == NULL) {
        free(name);
        return NULL;
    }

    statement = create_statement(location_from_line(line), STMT_SET);
    if (statement == NULL) {
        free(name);
        free_expression(value);
        parser_error_at(parser, location_from_line(line), NULL,
                        "I ran out of memory while building a set statement.");
        return NULL;
    }

    statement->as.set_stmt.name = name;
    statement->as.set_stmt.value = value;
    parser->current_line += 1;
    return statement;
}

static Statement *parse_remove_statement(Parser *parser, const LexedLine *line) {
    Statement *statement;
    char *name;
    Expression *item_index;

    if (line->count < 5 ||
        !token_is_word(&line->tokens[1], "item") ||
        !token_is_word(&line->tokens[line->count - 2], "from") ||
        line->tokens[line->count - 1].type != TOKEN_WORD) {
        parser_error_at(parser, location_from_line(line), NULL,
                        "A remove statement should look like: remove item position from list_name");
        return NULL;
    }

    item_index = parse_expression_range(parser, line, 2, line->count - 2);
    if (item_index == NULL) {
        return NULL;
    }

    name = copy_name_from_token(parser, &line->tokens[line->count - 1], false);
    if (name == NULL) {
        free_expression(item_index);
        return NULL;
    }

    statement = create_statement(location_from_line(line), STMT_REMOVE_ITEM);
    if (statement == NULL) {
        free(name);
        free_expression(item_index);
        parser_error_at(parser, location_from_line(line), NULL,
                        "I ran out of memory while building a remove statement.");
        return NULL;
    }

    statement->as.remove_item_stmt.name = name;
    statement->as.remove_item_stmt.index = item_index;
    parser->current_line += 1;
    return statement;
}

static Statement *parse_say_statement(Parser *parser, const LexedLine *line) {
    Statement *statement;
    Expression *value;

    if (line->count < 2) {
        parser_error_at(parser, location_from_line(line), NULL,
                        "A say statement needs something to print.");
        return NULL;
    }

    value = parse_expression_range(parser, line, 1, line->count);
    if (value == NULL) {
        return NULL;
    }

    statement = create_statement(location_from_line(line), STMT_SAY);
    if (statement == NULL) {
        free_expression(value);
        parser_error_at(parser, location_from_line(line), NULL,
                        "I ran out of memory while building a say statement.");
        return NULL;
    }

    statement->as.say_stmt.value = value;
    parser->current_line += 1;
    return statement;
}

static Statement *parse_ask_statement(Parser *parser, const LexedLine *line) {
    int marker_index;
    Statement *statement;
    Expression *prompt;
    char *name;

    if (line->count < 6) {
        parser_error_at(parser, location_from_line(line), NULL,
                        "An ask statement should look like: ask \"Question\" and store in name");
        return NULL;
    }

    marker_index = find_and_store_in(line, 1, line->count);
    if (marker_index < 0) {
        parser_error_at(parser, location_from_line(line), NULL,
                        "I expected 'and store in' after the question.");
        return NULL;
    }

    if (marker_index == 1) {
        parser_error_at(parser, location_from_line(line), NULL,
                        "An ask statement needs a prompt before 'and store in'.");
        return NULL;
    }

    if (marker_index + 4 != line->count) {
        parser_error_at(parser, location_from_line(line), NULL,
                        "The variable name after 'and store in' should be a single word.");
        return NULL;
    }

    prompt = parse_expression_range(parser, line, 1, marker_index);
    if (prompt == NULL) {
        return NULL;
    }

    name = copy_name_from_token(parser, &line->tokens[marker_index + 3], false);
    if (name == NULL) {
        free_expression(prompt);
        return NULL;
    }

    statement = create_statement(location_from_line(line), STMT_ASK);
    if (statement == NULL) {
        free_expression(prompt);
        free(name);
        parser_error_at(parser, location_from_line(line), NULL,
                        "I ran out of memory while building an ask statement.");
        return NULL;
    }

    statement->as.ask_stmt.prompt = prompt;
    statement->as.ask_stmt.name = name;
    parser->current_line += 1;
    return statement;
}

static Statement *parse_if_statement_from_tokens(Parser *parser, const LexedLine *line, int start_index) {
    int condition_end = trim_optional_last_word(line, start_index + 1, line->count, "then");
    Statement *statement;
    Expression *condition;
    Statement **then_body;
    Statement **else_body = NULL;
    int then_count = 0;
    int else_count = 0;
    BlockTerminator terminator;
    bool used_else_if = false;

    if (condition_end <= start_index + 1) {
        parser_error_at(parser, location_from_line(line), line->tokens[start_index].lexeme,
                        "An if statement needs a condition after the word 'if'.");
        return NULL;
    }

    condition = parse_expression_range(parser, line, start_index + 1, condition_end);
    if (condition == NULL) {
        return NULL;
    }

    parser->current_line += 1;
    then_body = parse_statement_list(parser, true, true, "if", location_from_line(line),
                                     &then_count, &terminator);
    if (parser->had_error) {
        free_expression(condition);
        return NULL;
    }

    if (terminator == BLOCK_TERMINATOR_ELSE) {
        const LexedLine *else_line = &parser->program->lines[parser->current_line];

        if (else_line->count == 1) {
            parser->current_line += 1;
            else_body = parse_statement_list(parser, true, false, "if", location_from_line(line),
                                             &else_count, &terminator);
            if (parser->had_error) {
                free_expression(condition);
                free_statement_list(then_body, then_count);
                return NULL;
            }
        } else if (else_line->count > 1 && token_is_word(&else_line->tokens[1], "if")) {
            Statement *nested_if = parse_if_statement_from_tokens(parser, else_line, 1);
            if (nested_if == NULL) {
                free_expression(condition);
                free_statement_list(then_body, then_count);
                return NULL;
            }
            if (!append_statement(&else_body, &else_count, nested_if)) {
                free_expression(condition);
                free_statement_list(then_body, then_count);
                free_statement(nested_if);
                parser_error_at(parser, location_from_line(else_line), NULL,
                                "I ran out of memory while storing an else-if block.");
                return NULL;
            }
            terminator = BLOCK_TERMINATOR_END;
            used_else_if = true;
        } else {
            free_expression(condition);
            free_statement_list(then_body, then_count);
            parser_error_at(parser, location_from_line(else_line), else_line->tokens[0].lexeme,
                            "An else line should be either 'else' or 'else if ... then'.");
            return NULL;
        }
    }

    if (terminator != BLOCK_TERMINATOR_END) {
        free_expression(condition);
        free_statement_list(then_body, then_count);
        free_statement_list(else_body, else_count);
        parser_error_at(parser, location_from_line(line), NULL,
                        "The if block needs an 'end' line.");
        return NULL;
    }

    if (!used_else_if &&
        parser->current_line < parser->program->count &&
        token_is_word(&parser->program->lines[parser->current_line].tokens[0], "end")) {
        parser->current_line += 1;
    }

    statement = create_statement(location_from_line(line), STMT_IF);
    if (statement == NULL) {
        free_expression(condition);
        free_statement_list(then_body, then_count);
        free_statement_list(else_body, else_count);
        parser_error_at(parser, location_from_line(line), NULL,
                        "I ran out of memory while building an if statement.");
        return NULL;
    }

    statement->as.if_stmt.condition = condition;
    statement->as.if_stmt.then_body = then_body;
    statement->as.if_stmt.then_count = then_count;
    statement->as.if_stmt.else_body = else_body;
    statement->as.if_stmt.else_count = else_count;
    return statement;
}

static Statement *parse_if_statement(Parser *parser, const LexedLine *line) {
    return parse_if_statement_from_tokens(parser, line, 0);
}

static Statement *parse_repeat_statement(Parser *parser, const LexedLine *line) {
    int count_end = line->count;
    Statement *statement;
    Expression *count_expression;
    Statement **body;
    int body_count = 0;
    BlockTerminator terminator;

    if (line->count < 3) {
        parser_error_at(parser, location_from_line(line), NULL,
                        "A repeat statement should look like: repeat 5 times");
        return NULL;
    }

    if (token_is_word(&line->tokens[line->count - 1], "times") ||
        token_is_word(&line->tokens[line->count - 1], "time")) {
        count_end -= 1;
    } else {
        parser_error_at(parser, location_from_line(line), NULL,
                        "A repeat statement should end with 'time' or 'times'.");
        return NULL;
    }

    count_expression = parse_expression_range(parser, line, 1, count_end);
    if (count_expression == NULL) {
        return NULL;
    }

    parser->current_line += 1;
    body = parse_statement_list(parser, true, false, "repeat", location_from_line(line),
                                &body_count, &terminator);
    if (parser->had_error) {
        free_expression(count_expression);
        return NULL;
    }

    if (terminator != BLOCK_TERMINATOR_END) {
        free_expression(count_expression);
        free_statement_list(body, body_count);
        parser_error_at(parser, location_from_line(line), NULL,
                        "The repeat block needs an 'end' line.");
        return NULL;
    }
    parser->current_line += 1;

    statement = create_statement(location_from_line(line), STMT_REPEAT);
    if (statement == NULL) {
        free_expression(count_expression);
        free_statement_list(body, body_count);
        parser_error_at(parser, location_from_line(line), NULL,
                        "I ran out of memory while building a repeat statement.");
        return NULL;
    }

    statement->as.repeat_stmt.count = count_expression;
    statement->as.repeat_stmt.body = body;
    statement->as.repeat_stmt.body_count = body_count;
    return statement;
}

static Statement *parse_while_statement(Parser *parser, const LexedLine *line) {
    int condition_end = trim_optional_last_word(line, 1, line->count, "do");
    Statement *statement;
    Expression *condition;
    Statement **body;
    int body_count = 0;
    BlockTerminator terminator;

    if (condition_end <= 1) {
        parser_error_at(parser, location_from_line(line), NULL,
                        "A while statement needs a condition after the word 'while'.");
        return NULL;
    }

    condition = parse_expression_range(parser, line, 1, condition_end);
    if (condition == NULL) {
        return NULL;
    }

    parser->current_line += 1;
    body = parse_statement_list(parser, true, false, "while", location_from_line(line),
                                &body_count, &terminator);
    if (parser->had_error) {
        free_expression(condition);
        return NULL;
    }

    if (terminator != BLOCK_TERMINATOR_END) {
        free_expression(condition);
        free_statement_list(body, body_count);
        parser_error_at(parser, location_from_line(line), NULL,
                        "The while block needs an 'end' line.");
        return NULL;
    }
    parser->current_line += 1;

    statement = create_statement(location_from_line(line), STMT_WHILE);
    if (statement == NULL) {
        free_expression(condition);
        free_statement_list(body, body_count);
        parser_error_at(parser, location_from_line(line), NULL,
                        "I ran out of memory while building a while statement.");
        return NULL;
    }

    statement->as.while_stmt.condition = condition;
    statement->as.while_stmt.body = body;
    statement->as.while_stmt.body_count = body_count;
    return statement;
}

static Statement *parse_for_each_statement(Parser *parser, const LexedLine *line) {
    Statement *statement;
    char *item_name;
    Expression *collection;
    Statement **body;
    int body_count = 0;
    BlockTerminator terminator;
    int in_index = -1;
    int index;

    if (line->count < 5 || !token_is_word(&line->tokens[1], "each")) {
        parser_error_at(parser, location_from_line(line), NULL,
                        "A for each loop should look like: for each item in collection");
        return NULL;
    }

    for (index = 3; index < line->count; index += 1) {
        if (token_is_word(&line->tokens[index], "in")) {
            in_index = index;
            break;
        }
    }

    if (in_index != 3) {
        parser_error_at(parser, location_from_line(line), NULL,
                        "A for each loop should look like: for each item in collection");
        return NULL;
    }

    item_name = copy_name_from_token(parser, &line->tokens[2], false);
    if (item_name == NULL) {
        return NULL;
    }

    collection = parse_expression_range(parser, line, 4, line->count);
    if (collection == NULL) {
        free(item_name);
        return NULL;
    }

    parser->current_line += 1;
    body = parse_statement_list(parser, true, false, "for each", location_from_line(line),
                                &body_count, &terminator);
    if (parser->had_error) {
        free(item_name);
        free_expression(collection);
        return NULL;
    }

    if (terminator != BLOCK_TERMINATOR_END) {
        free(item_name);
        free_expression(collection);
        free_statement_list(body, body_count);
        parser_error_at(parser, location_from_line(line), NULL,
                        "The for each block needs an 'end' line.");
        return NULL;
    }
    parser->current_line += 1;

    statement = create_statement(location_from_line(line), STMT_FOR_EACH);
    if (statement == NULL) {
        free(item_name);
        free_expression(collection);
        free_statement_list(body, body_count);
        parser_error_at(parser, location_from_line(line), NULL,
                        "I ran out of memory while building a for each statement.");
        return NULL;
    }

    statement->as.for_each_stmt.item_name = item_name;
    statement->as.for_each_stmt.collection = collection;
    statement->as.for_each_stmt.body = body;
    statement->as.for_each_stmt.body_count = body_count;
    return statement;
}

static Statement *parse_function_statement(Parser *parser, const LexedLine *line) {
    Statement *statement;
    char *name;
    char **parameters = NULL;
    int param_count = 0;
    Statement **body;
    int body_count = 0;
    BlockTerminator terminator;

    if (line->count < 3 || !token_is_word(&line->tokens[1], "function")) {
        parser_error_at(parser, location_from_line(line), NULL,
                        "A function definition should start with: define function name");
        return NULL;
    }

    name = copy_name_from_token(parser, &line->tokens[2], false);
    if (name == NULL) {
        return NULL;
    }

    if (line->count > 3) {
        if (!token_is_word(&line->tokens[3], "with")) {
            free(name);
            parser_error_at(parser, location_from_line(line), line->tokens[3].lexeme,
                            "After the function name, I expected 'with' before the parameter list.");
            return NULL;
        }

        if (!parse_parameter_list(parser, line, 4, &parameters, &param_count)) {
            free(name);
            return NULL;
        }
    }

    parser->current_line += 1;
    body = parse_statement_list(parser, true, false, "function", location_from_line(line),
                                &body_count, &terminator);
    if (parser->had_error) {
        free(name);
        free_name_list(parameters, param_count);
        return NULL;
    }

    if (terminator != BLOCK_TERMINATOR_END) {
        free(name);
        free_name_list(parameters, param_count);
        free_statement_list(body, body_count);
        parser_error_at(parser, location_from_line(line), NULL,
                        "The function block needs an 'end' line.");
        return NULL;
    }
    parser->current_line += 1;

    statement = create_statement(location_from_line(line), STMT_FUNCTION);
    if (statement == NULL) {
        free(name);
        free_name_list(parameters, param_count);
        free_statement_list(body, body_count);
        parser_error_at(parser, location_from_line(line), NULL,
                        "I ran out of memory while building a function.");
        return NULL;
    }

    statement->as.function_stmt.name = name;
    statement->as.function_stmt.parameters = parameters;
    statement->as.function_stmt.param_count = param_count;
    statement->as.function_stmt.body = body;
    statement->as.function_stmt.body_count = body_count;
    return statement;
}

static Statement *parse_call_statement(Parser *parser, const LexedLine *line) {
    Statement *statement;
    Expression *call_expression = parse_expression_range(parser, line, 0, line->count);

    if (call_expression == NULL) {
        return NULL;
    }

    if (call_expression->type != EXPR_CALL) {
        free_expression(call_expression);
        parser_error_at(parser, location_from_line(line), NULL,
                        "A call statement should start with 'call function_name'.");
        return NULL;
    }

    statement = create_statement(location_from_line(line), STMT_CALL);
    if (statement == NULL) {
        free_expression(call_expression);
        parser_error_at(parser, location_from_line(line), NULL,
                        "I ran out of memory while building a call statement.");
        return NULL;
    }

    statement->as.call_stmt.call = call_expression;
    parser->current_line += 1;
    return statement;
}

static Statement *parse_return_statement(Parser *parser, const LexedLine *line) {
    Statement *statement;
    Expression *value = NULL;
    int start = 1;

    if (line->count > 1 && token_is_word(&line->tokens[1], "with")) {
        start = 2;
    }

    if (start < line->count) {
        value = parse_expression_range(parser, line, start, line->count);
        if (value == NULL) {
            return NULL;
        }
    }

    statement = create_statement(location_from_line(line), STMT_RETURN);
    if (statement == NULL) {
        free_expression(value);
        parser_error_at(parser, location_from_line(line), NULL,
                        "I ran out of memory while building a return statement.");
        return NULL;
    }

    statement->as.return_stmt.value = value;
    parser->current_line += 1;
    return statement;
}

static Statement *parse_break_or_continue_statement(Parser *parser, const LexedLine *line, StatementType type) {
    Statement *statement;

    if (line->count != 1) {
        parser_error_at(parser, location_from_line(line), NULL,
                        "This statement should be on its own line.");
        return NULL;
    }

    statement = create_statement(location_from_line(line), type);
    if (statement == NULL) {
        parser_error_at(parser, location_from_line(line), NULL,
                        "I ran out of memory while building this statement.");
        return NULL;
    }

    parser->current_line += 1;
    return statement;
}

static Statement *parse_statement(Parser *parser) {
    const LexedLine *line = &parser->program->lines[parser->current_line];

    if (line->count == 0) {
        parser->current_line += 1;
        return NULL;
    }

    if (token_is_word(&line->tokens[0], "note")) {
        parser->current_line += 1;
        return NULL;
    }

    if (token_is_word(&line->tokens[0], "use")) {
        return parse_use_statement(parser, line);
    }

    if (token_is_word(&line->tokens[0], "let")) {
        return parse_let_statement(parser, line);
    }

    if (token_is_word(&line->tokens[0], "set")) {
        return parse_set_statement(parser, line);
    }

    if (token_is_word(&line->tokens[0], "append")) {
        return parse_append_statement(parser, line);
    }

    if (token_is_word(&line->tokens[0], "remove")) {
        return parse_remove_statement(parser, line);
    }

    if (token_is_word(&line->tokens[0], "say")) {
        return parse_say_statement(parser, line);
    }

    if (token_is_word(&line->tokens[0], "ask")) {
        return parse_ask_statement(parser, line);
    }

    if (token_is_word(&line->tokens[0], "if")) {
        return parse_if_statement(parser, line);
    }

    if (token_is_word(&line->tokens[0], "repeat")) {
        return parse_repeat_statement(parser, line);
    }

    if (token_is_word(&line->tokens[0], "while")) {
        return parse_while_statement(parser, line);
    }

    if (token_is_word(&line->tokens[0], "for")) {
        return parse_for_each_statement(parser, line);
    }

    if (token_is_word(&line->tokens[0], "define")) {
        return parse_function_statement(parser, line);
    }

    if (token_is_word(&line->tokens[0], "call")) {
        return parse_call_statement(parser, line);
    }

    if (token_is_word(&line->tokens[0], "return")) {
        return parse_return_statement(parser, line);
    }

    if (token_is_word(&line->tokens[0], "break")) {
        return parse_break_or_continue_statement(parser, line, STMT_BREAK);
    }

    if (token_is_word(&line->tokens[0], "continue")) {
        return parse_break_or_continue_statement(parser, line, STMT_CONTINUE);
    }

    parser_error_at(parser, location_from_line(line), line->tokens[0].lexeme,
                    "I do not understand how this line starts. Try use, let, set, append, remove, say, ask, if, repeat, while, for each, define function, call, return, break, continue, or note.");
    return NULL;
}

Program *parse_program(const LexedProgram *lexed_program, char *error_message, size_t error_size) {
    Parser parser;
    Program *program = (Program *) calloc(1, sizeof(Program));
    BlockTerminator terminator;

    if (program == NULL) {
        if (error_message != NULL && error_size > 0) {
            (void) snprintf(error_message, error_size, "I could not allocate memory for the parser.");
        }
        return NULL;
    }

    parser.program = lexed_program;
    parser.current_line = 0;
    parser.error_message = error_message;
    parser.error_size = error_size;
    parser.had_error = false;

    program->statements = parse_statement_list(&parser, false, false, NULL,
                                               (SourceLocation) {0, NULL, NULL},
                                               &program->count, &terminator);
    if (parser.had_error) {
        free_program(program);
        return NULL;
    }

    return program;
}

static void free_expression(Expression *expression) {
    int index;

    if (expression == NULL) {
        return;
    }

    switch (expression->type) {
        case EXPR_STRING:
            free(expression->as.string);
            break;
        case EXPR_VARIABLE:
            free(expression->as.variable_name);
            break;
        case EXPR_LIST:
            for (index = 0; index < expression->as.list.item_count; index += 1) {
                free_expression(expression->as.list.items[index]);
            }
            free(expression->as.list.items);
            break;
        case EXPR_RECORD:
            for (index = 0; index < expression->as.record.count; index += 1) {
                free(expression->as.record.keys[index]);
                free_expression(expression->as.record.values[index]);
            }
            free(expression->as.record.keys);
            free(expression->as.record.values);
            break;
        case EXPR_GROUPING:
            free_expression(expression->as.grouping.inner);
            break;
        case EXPR_UNARY:
            free_expression(expression->as.unary.right);
            break;
        case EXPR_BINARY:
            free_expression(expression->as.binary.left);
            free_expression(expression->as.binary.right);
            break;
        case EXPR_COMPARISON:
            free_expression(expression->as.comparison.left);
            free_expression(expression->as.comparison.right);
            break;
        case EXPR_CALL:
            free(expression->as.call.name);
            for (index = 0; index < expression->as.call.arg_count; index += 1) {
                free_expression(expression->as.call.arguments[index]);
            }
            free(expression->as.call.arguments);
            break;
        case EXPR_ITEM_ACCESS:
            free_expression(expression->as.item_access.index);
            free_expression(expression->as.item_access.collection);
            break;
        case EXPR_FIELD_ACCESS:
            free(expression->as.field_access.field_name);
            free_expression(expression->as.field_access.record);
            break;
        case EXPR_NUMBER:
        case EXPR_BOOLEAN:
        case EXPR_NONE:
            break;
    }

    free(expression);
}

static void free_statement(Statement *statement) {
    int index;

    if (statement == NULL) {
        return;
    }

    switch (statement->type) {
        case STMT_USE:
            free(statement->as.use_stmt.path);
            break;
        case STMT_LET:
            free(statement->as.let_stmt.name);
            free_expression(statement->as.let_stmt.value);
            break;
        case STMT_SET:
            free(statement->as.set_stmt.name);
            free_expression(statement->as.set_stmt.value);
            break;
        case STMT_APPEND:
            free(statement->as.append_stmt.name);
            free_expression(statement->as.append_stmt.value);
            break;
        case STMT_SET_ITEM:
            free(statement->as.set_item_stmt.name);
            free_expression(statement->as.set_item_stmt.index);
            free_expression(statement->as.set_item_stmt.value);
            break;
        case STMT_REMOVE_ITEM:
            free(statement->as.remove_item_stmt.name);
            free_expression(statement->as.remove_item_stmt.index);
            break;
        case STMT_SET_FIELD:
            free(statement->as.set_field_stmt.name);
            free(statement->as.set_field_stmt.field_name);
            free_expression(statement->as.set_field_stmt.value);
            break;
        case STMT_SAY:
            free_expression(statement->as.say_stmt.value);
            break;
        case STMT_ASK:
            free_expression(statement->as.ask_stmt.prompt);
            free(statement->as.ask_stmt.name);
            break;
        case STMT_IF:
            free_expression(statement->as.if_stmt.condition);
            free_statement_list(statement->as.if_stmt.then_body, statement->as.if_stmt.then_count);
            free_statement_list(statement->as.if_stmt.else_body, statement->as.if_stmt.else_count);
            break;
        case STMT_REPEAT:
            free_expression(statement->as.repeat_stmt.count);
            free_statement_list(statement->as.repeat_stmt.body, statement->as.repeat_stmt.body_count);
            break;
        case STMT_WHILE:
            free_expression(statement->as.while_stmt.condition);
            free_statement_list(statement->as.while_stmt.body, statement->as.while_stmt.body_count);
            break;
        case STMT_FOR_EACH:
            free(statement->as.for_each_stmt.item_name);
            free_expression(statement->as.for_each_stmt.collection);
            free_statement_list(statement->as.for_each_stmt.body, statement->as.for_each_stmt.body_count);
            break;
        case STMT_FUNCTION:
            free(statement->as.function_stmt.name);
            for (index = 0; index < statement->as.function_stmt.param_count; index += 1) {
                free(statement->as.function_stmt.parameters[index]);
            }
            free(statement->as.function_stmt.parameters);
            free_statement_list(statement->as.function_stmt.body, statement->as.function_stmt.body_count);
            break;
        case STMT_CALL:
            free_expression(statement->as.call_stmt.call);
            break;
        case STMT_RETURN:
            free_expression(statement->as.return_stmt.value);
            break;
        case STMT_BREAK:
        case STMT_CONTINUE:
            break;
    }

    free(statement);
}

static void free_statement_list(Statement **statements, int count) {
    int index;
    for (index = 0; index < count; index += 1) {
        free_statement(statements[index]);
    }
    free(statements);
}

void free_program(Program *program) {
    if (program == NULL) {
        return;
    }

    free_statement_list(program->statements, program->count);
    free(program);
}
