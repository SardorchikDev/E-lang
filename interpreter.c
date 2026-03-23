#include "interpreter.h"

#include "builtins.h"
#include "files.h"
#include "lexer.h"

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    EXEC_OK,
    EXEC_RETURN,
    EXEC_BREAK,
    EXEC_CONTINUE,
    EXEC_ERROR
} ExecutionStatus;

typedef struct {
    ExecutionStatus status;
    Value value;
    SourceLocation location;
} ExecutionResult;

typedef struct {
    char *path;
    char *source;
    LexedProgram *lexed_program;
    Program *program;
} LoadedModule;

typedef struct {
    Runtime *runtime;
    const InterpreterOptions *options;
    char *error_message;
    size_t error_size;
    bool had_error;
    int call_depth;
    LoadedModule *loaded_modules;
    int loaded_module_count;
    char **active_modules;
    int active_module_count;
} Interpreter;

static bool evaluate_expression(Interpreter *interpreter, const Expression *expression, Value *out_value);
static ExecutionResult execute_statement_list(Interpreter *interpreter,
                                              const Statement *const *statements,
                                              int count);

static void append_caret(char *buffer, size_t buffer_size, const char *source_text, const char *anchor) {
    const char *match;
    size_t used;
    size_t offset;
    size_t index;

    if (buffer == NULL || buffer_size == 0 || source_text == NULL || anchor == NULL || anchor[0] == '\0') {
        return;
    }

    match = strstr(source_text, anchor);
    if (match == NULL) {
        return;
    }

    used = strlen(buffer);
    if (used >= buffer_size) {
        return;
    }

    (void) snprintf(buffer + used, buffer_size - used, "\n    ");
    used = strlen(buffer);
    offset = (size_t) (match - source_text);

    for (index = 0; index < offset && used + 1 < buffer_size; index += 1) {
        buffer[used] = source_text[index] == '\t' ? '\t' : ' ';
        used += 1;
    }

    if (used + 1 < buffer_size) {
        buffer[used] = '^';
        used += 1;
    }
    if (used < buffer_size) {
        buffer[used] = '\0';
    }
}

static void runtime_error_at(Interpreter *interpreter,
                             SourceLocation location,
                             const char *anchor,
                             const char *format,
                             ...) {
    va_list args;
    size_t used;

    if (interpreter->had_error) {
        return;
    }

    interpreter->had_error = true;

    if (interpreter->error_message == NULL || interpreter->error_size == 0) {
        return;
    }

    if (location.file_path != NULL) {
        (void) snprintf(interpreter->error_message, interpreter->error_size,
                        "%s:%d: ", location.file_path, location.line);
    } else if (location.line > 0) {
        (void) snprintf(interpreter->error_message, interpreter->error_size,
                        "Line %d: ", location.line);
    } else {
        interpreter->error_message[0] = '\0';
    }

    used = strlen(interpreter->error_message);
    va_start(args, format);
    (void) vsnprintf(interpreter->error_message + used,
                     interpreter->error_size - used,
                     format,
                     args);
    va_end(args);

    if (location.source_text != NULL && location.source_text[0] != '\0') {
        used = strlen(interpreter->error_message);
        (void) snprintf(interpreter->error_message + used,
                        interpreter->error_size - used,
                        "\n    %s",
                        location.source_text);
        append_caret(interpreter->error_message, interpreter->error_size, location.source_text, anchor);
    }
}

static ExecutionResult exec_ok(void) {
    ExecutionResult result;
    result.status = EXEC_OK;
    result.value = value_none();
    result.location = (SourceLocation) {0, NULL, NULL};
    return result;
}

static ExecutionResult exec_error(void) {
    ExecutionResult result;
    result.status = EXEC_ERROR;
    result.value = value_none();
    result.location = (SourceLocation) {0, NULL, NULL};
    return result;
}

static ExecutionResult exec_simple(ExecutionStatus status, SourceLocation location) {
    ExecutionResult result;
    result.status = status;
    result.value = value_none();
    result.location = location;
    return result;
}

static ExecutionResult exec_return(Value value, SourceLocation location) {
    ExecutionResult result;
    result.status = EXEC_RETURN;
    result.value = value;
    result.location = location;
    return result;
}

static bool is_whole_number(double number) {
    return fabs(number - round(number)) < 1e-9;
}

static char *read_input_line(void) {
    size_t capacity = 64;
    size_t length = 0;
    int ch;
    char *buffer = (char *) malloc(capacity);

    if (buffer == NULL) {
        return NULL;
    }

    while ((ch = getchar()) != EOF && ch != '\n') {
        if (length + 1 >= capacity) {
            char *new_buffer;
            capacity *= 2;
            new_buffer = (char *) realloc(buffer, capacity);
            if (new_buffer == NULL) {
                free(buffer);
                return NULL;
            }
            buffer = new_buffer;
        }

        buffer[length] = (char) ch;
        length += 1;
    }

    buffer[length] = '\0';
    return buffer;
}

static const char *statement_type_name(StatementType type) {
    switch (type) {
        case STMT_USE:
            return "use";
        case STMT_LET:
            return "let";
        case STMT_SET:
            return "set";
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
            return "for each";
        case STMT_FUNCTION:
            return "define function";
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

static void trace_statement(Interpreter *interpreter, const Statement *statement) {
    if (interpreter->options == NULL || !interpreter->options->trace) {
        return;
    }

    if (statement->location.file_path != NULL) {
        (void) fprintf(stderr, "[trace] %s:%d %s\n",
                       statement->location.file_path,
                       statement->location.line,
                       statement_type_name(statement->type));
    } else {
        (void) fprintf(stderr, "[trace] line %d %s\n",
                       statement->location.line,
                       statement_type_name(statement->type));
    }
}

static bool append_owned_text(char ***items, int *count, char *text) {
    char **new_items = (char **) realloc(*items, (size_t) (*count + 1) * sizeof(char *));

    if (new_items == NULL) {
        return false;
    }

    *items = new_items;
    (*items)[*count] = text;
    *count += 1;
    return true;
}

static bool active_module_list_contains(char *const *items, int count, const char *path) {
    int index;

    for (index = 0; index < count; index += 1) {
        if (strcmp(items[index], path) == 0) {
            return true;
        }
    }

    return false;
}

static bool loaded_module_list_contains(const LoadedModule *items, int count, const char *path) {
    int index;

    for (index = 0; index < count; index += 1) {
        if (strcmp(items[index].path, path) == 0) {
            return true;
        }
    }

    return false;
}

static bool push_active_module(Interpreter *interpreter, char *path) {
    return append_owned_text(&interpreter->active_modules, &interpreter->active_module_count, path);
}

static char *pop_active_module(Interpreter *interpreter) {
    char *path;

    if (interpreter->active_module_count == 0) {
        return NULL;
    }

    interpreter->active_module_count -= 1;
    path = interpreter->active_modules[interpreter->active_module_count];
    if (interpreter->active_module_count == 0) {
        free(interpreter->active_modules);
        interpreter->active_modules = NULL;
    }
    return path;
}

static bool append_loaded_module(Interpreter *interpreter,
                                 char *path,
                                 char *source,
                                 LexedProgram *lexed_program,
                                 Program *program) {
    LoadedModule *new_modules = (LoadedModule *) realloc(interpreter->loaded_modules,
                                                         (size_t) (interpreter->loaded_module_count + 1) *
                                                             sizeof(LoadedModule));

    if (new_modules == NULL) {
        return false;
    }

    interpreter->loaded_modules = new_modules;
    interpreter->loaded_modules[interpreter->loaded_module_count].path = path;
    interpreter->loaded_modules[interpreter->loaded_module_count].source = source;
    interpreter->loaded_modules[interpreter->loaded_module_count].lexed_program = lexed_program;
    interpreter->loaded_modules[interpreter->loaded_module_count].program = program;
    interpreter->loaded_module_count += 1;
    return true;
}

static void free_module_lists(Interpreter *interpreter) {
    int index;

    for (index = 0; index < interpreter->loaded_module_count; index += 1) {
        free_program(interpreter->loaded_modules[index].program);
        free_lexed_program(interpreter->loaded_modules[index].lexed_program);
        free(interpreter->loaded_modules[index].source);
        free(interpreter->loaded_modules[index].path);
    }
    free(interpreter->loaded_modules);

    for (index = 0; index < interpreter->active_module_count; index += 1) {
        free(interpreter->active_modules[index]);
    }
    free(interpreter->active_modules);

    interpreter->loaded_modules = NULL;
    interpreter->active_modules = NULL;
    interpreter->loaded_module_count = 0;
    interpreter->active_module_count = 0;
}

static bool concatenate_values(const Value *left, const Value *right, Value *out_value) {
    char *left_text = NULL;
    char *right_text = NULL;
    char *combined = NULL;
    size_t total_length;

    if (!value_to_text_copy(left, &left_text)) {
        return false;
    }

    if (!value_to_text_copy(right, &right_text)) {
        free(left_text);
        return false;
    }

    total_length = strlen(left_text) + strlen(right_text) + 1U;
    combined = (char *) malloc(total_length);
    if (combined == NULL) {
        free(left_text);
        free(right_text);
        return false;
    }

    (void) strcpy(combined, left_text);
    (void) strcat(combined, right_text);

    free(left_text);
    free(right_text);

    *out_value = value_string_owned(combined);
    return true;
}

static bool evaluate_list_expression(Interpreter *interpreter, const Expression *expression, Value *out_value) {
    int index;
    Value list = value_none();

    if (!value_make_empty_list(&list)) {
        runtime_error_at(interpreter, expression->location, NULL,
                         "I ran out of memory while creating a list.");
        return false;
    }

    for (index = 0; index < expression->as.list.item_count; index += 1) {
        Value item = value_none();
        Value next_list = value_none();

        if (!evaluate_expression(interpreter, expression->as.list.items[index], &item)) {
            value_destroy(&list);
            return false;
        }

        if (!value_list_append_copy(&list, &item, &next_list)) {
            value_destroy(&item);
            value_destroy(&list);
            runtime_error_at(interpreter, expression->location, NULL,
                             "I ran out of memory while building a list.");
            return false;
        }

        value_destroy(&item);
        value_destroy(&list);
        list = next_list;
    }

    *out_value = list;
    return true;
}

static bool evaluate_record_expression(Interpreter *interpreter, const Expression *expression, Value *out_value) {
    int index;
    Value record = value_none();

    if (!value_make_empty_record(&record)) {
        runtime_error_at(interpreter, expression->location, NULL,
                         "I ran out of memory while creating a record.");
        return false;
    }

    for (index = 0; index < expression->as.record.count; index += 1) {
        Value field_value = value_none();
        Value next_record = value_none();

        if (!evaluate_expression(interpreter, expression->as.record.values[index], &field_value)) {
            value_destroy(&record);
            return false;
        }

        if (!value_record_set_copy(&record,
                                   expression->as.record.keys[index],
                                   &field_value,
                                   &next_record)) {
            value_destroy(&field_value);
            value_destroy(&record);
            runtime_error_at(interpreter, expression->location, expression->as.record.keys[index],
                             "I ran out of memory while building a record.");
            return false;
        }

        value_destroy(&field_value);
        value_destroy(&record);
        record = next_record;
    }

    *out_value = record;
    return true;
}

static bool evaluate_unary_expression(Interpreter *interpreter, const Expression *expression, Value *out_value) {
    Value right = value_none();
    double number;

    if (!evaluate_expression(interpreter, expression->as.unary.right, &right)) {
        return false;
    }

    switch (expression->as.unary.op) {
        case UNARY_NEGATE:
            if (!value_to_number(&right, &number)) {
                value_destroy(&right);
                runtime_error_at(interpreter, expression->location, "minus",
                                 "The word 'minus' needs a number after it.");
                return false;
            }
            value_destroy(&right);
            *out_value = value_number(-number);
            return true;

        case UNARY_NOT:
            *out_value = value_boolean(!value_is_truthy(&right));
            value_destroy(&right);
            return true;
    }

    value_destroy(&right);
    runtime_error_at(interpreter, expression->location, NULL,
                     "I do not understand this unary expression.");
    return false;
}

static bool evaluate_contains_expression(Interpreter *interpreter,
                                         const Expression *expression,
                                         const Value *left,
                                         const Value *right,
                                         Value *out_value) {
    if (left->type == VALUE_STRING) {
        char *needle = NULL;
        bool found;

        if (!value_to_text_copy(right, &needle)) {
            runtime_error_at(interpreter, expression->location, "contains",
                             "I ran out of memory while checking text.");
            return false;
        }

        found = strstr(left->string != NULL ? left->string : "", needle) != NULL;
        free(needle);
        *out_value = value_boolean(found);
        return true;
    }

    if (left->type == VALUE_LIST) {
        int index;
        for (index = 1; index <= value_list_length(left); index += 1) {
            Value item = value_none();
            bool equal = false;

            if (!value_list_get(left, index, &item)) {
                runtime_error_at(interpreter, expression->location, "contains",
                                 "I could not read an item while checking the list.");
                return false;
            }

            if (!value_equals(&item, right, &equal)) {
                value_destroy(&item);
                runtime_error_at(interpreter, expression->location, "contains",
                                 "I ran out of memory while comparing list items.");
                return false;
            }

            value_destroy(&item);
            if (equal) {
                *out_value = value_boolean(true);
                return true;
            }
        }

        *out_value = value_boolean(false);
        return true;
    }

    if (left->type == VALUE_RECORD) {
        char *field_name = NULL;
        bool has_field;

        if (!value_to_text_copy(right, &field_name)) {
            runtime_error_at(interpreter, expression->location, "contains",
                             "I ran out of memory while checking a record.");
            return false;
        }

        has_field = value_record_has(left, field_name);
        free(field_name);
        *out_value = value_boolean(has_field);
        return true;
    }

    runtime_error_at(interpreter, expression->location, "contains",
                     "The word 'contains' works with text, lists, or records.");
    return false;
}

static bool evaluate_binary_expression(Interpreter *interpreter, const Expression *expression, Value *out_value) {
    Value left = value_none();
    Value right = value_none();
    double left_number;
    double right_number;

    if (expression->as.binary.op == BIN_AND) {
        if (!evaluate_expression(interpreter, expression->as.binary.left, &left)) {
            return false;
        }
        if (!value_is_truthy(&left)) {
            value_destroy(&left);
            *out_value = value_boolean(false);
            return true;
        }
        value_destroy(&left);
        if (!evaluate_expression(interpreter, expression->as.binary.right, &right)) {
            return false;
        }
        *out_value = value_boolean(value_is_truthy(&right));
        value_destroy(&right);
        return true;
    }

    if (expression->as.binary.op == BIN_OR) {
        if (!evaluate_expression(interpreter, expression->as.binary.left, &left)) {
            return false;
        }
        if (value_is_truthy(&left)) {
            value_destroy(&left);
            *out_value = value_boolean(true);
            return true;
        }
        value_destroy(&left);
        if (!evaluate_expression(interpreter, expression->as.binary.right, &right)) {
            return false;
        }
        *out_value = value_boolean(value_is_truthy(&right));
        value_destroy(&right);
        return true;
    }

    if (!evaluate_expression(interpreter, expression->as.binary.left, &left)) {
        return false;
    }

    if (!evaluate_expression(interpreter, expression->as.binary.right, &right)) {
        value_destroy(&left);
        return false;
    }

    switch (expression->as.binary.op) {
        case BIN_ADD:
            if (left.type == VALUE_NUMBER && right.type == VALUE_NUMBER) {
                *out_value = value_number(left.number + right.number);
            } else {
                if (!concatenate_values(&left, &right, out_value)) {
                    value_destroy(&left);
                    value_destroy(&right);
                    runtime_error_at(interpreter, expression->location, "plus",
                                     "I ran out of memory while joining text together.");
                    return false;
                }
            }
            break;

        case BIN_SUBTRACT:
            if (!value_to_number(&left, &left_number) || !value_to_number(&right, &right_number)) {
                value_destroy(&left);
                value_destroy(&right);
                runtime_error_at(interpreter, expression->location, "minus",
                                 "The word 'minus' only works with numbers.");
                return false;
            }
            *out_value = value_number(left_number - right_number);
            break;

        case BIN_MULTIPLY:
            if (!value_to_number(&left, &left_number) || !value_to_number(&right, &right_number)) {
                value_destroy(&left);
                value_destroy(&right);
                runtime_error_at(interpreter, expression->location, "times",
                                 "The word 'times' only works with numbers.");
                return false;
            }
            *out_value = value_number(left_number * right_number);
            break;

        case BIN_DIVIDE:
            if (!value_to_number(&left, &left_number) || !value_to_number(&right, &right_number)) {
                value_destroy(&left);
                value_destroy(&right);
                runtime_error_at(interpreter, expression->location, "divided",
                                 "The phrase 'divided by' only works with numbers.");
                return false;
            }
            if (fabs(right_number) < 1e-12) {
                value_destroy(&left);
                value_destroy(&right);
                runtime_error_at(interpreter, expression->location, "divided",
                                 "I cannot divide by zero.");
                return false;
            }
            *out_value = value_number(left_number / right_number);
            break;

        case BIN_MODULO:
            if (!value_to_number(&left, &left_number) || !value_to_number(&right, &right_number)) {
                value_destroy(&left);
                value_destroy(&right);
                runtime_error_at(interpreter, expression->location, "mod",
                                 "The word 'mod' only works with numbers.");
                return false;
            }
            if (fabs(right_number) < 1e-12) {
                value_destroy(&left);
                value_destroy(&right);
                runtime_error_at(interpreter, expression->location, "mod",
                                 "I cannot take a remainder with zero.");
                return false;
            }
            *out_value = value_number(fmod(left_number, right_number));
            break;

        case BIN_POWER:
            if (!value_to_number(&left, &left_number) || !value_to_number(&right, &right_number)) {
                value_destroy(&left);
                value_destroy(&right);
                runtime_error_at(interpreter, expression->location, "power",
                                 "The word 'power' only works with numbers.");
                return false;
            }
            *out_value = value_number(pow(left_number, right_number));
            break;

        case BIN_AND:
        case BIN_OR:
            value_destroy(&left);
            value_destroy(&right);
            runtime_error_at(interpreter, expression->location, NULL,
                             "I do not understand this logical expression.");
            return false;
    }

    value_destroy(&left);
    value_destroy(&right);
    return true;
}

static bool evaluate_comparison_expression(Interpreter *interpreter, const Expression *expression, Value *out_value) {
    Value left = value_none();
    Value right = value_none();
    bool equal = false;
    double left_number;
    double right_number;

    if (!evaluate_expression(interpreter, expression->as.comparison.left, &left)) {
        return false;
    }

    if (!evaluate_expression(interpreter, expression->as.comparison.right, &right)) {
        value_destroy(&left);
        return false;
    }

    switch (expression->as.comparison.op) {
        case CMP_EQUAL:
            if (!value_equals(&left, &right, &equal)) {
                value_destroy(&left);
                value_destroy(&right);
                runtime_error_at(interpreter, expression->location, "equal",
                                 "I ran out of memory while comparing two values.");
                return false;
            }
            *out_value = value_boolean(equal);
            break;

        case CMP_NOT_EQUAL:
            if (!value_equals(&left, &right, &equal)) {
                value_destroy(&left);
                value_destroy(&right);
                runtime_error_at(interpreter, expression->location, "equal",
                                 "I ran out of memory while comparing two values.");
                return false;
            }
            *out_value = value_boolean(!equal);
            break;

        case CMP_GREATER_THAN:
        case CMP_LESS_THAN:
        case CMP_AT_LEAST:
        case CMP_AT_MOST:
            if (!value_to_number(&left, &left_number) || !value_to_number(&right, &right_number)) {
                value_destroy(&left);
                value_destroy(&right);
                runtime_error_at(interpreter, expression->location, "is",
                                 "This comparison needs values that can be treated as numbers.");
                return false;
            }

            if (expression->as.comparison.op == CMP_GREATER_THAN) {
                *out_value = value_boolean(left_number > right_number);
            } else if (expression->as.comparison.op == CMP_LESS_THAN) {
                *out_value = value_boolean(left_number < right_number);
            } else if (expression->as.comparison.op == CMP_AT_LEAST) {
                *out_value = value_boolean(left_number >= right_number);
            } else {
                *out_value = value_boolean(left_number <= right_number);
            }
            break;

        case CMP_CONTAINS:
            if (!evaluate_contains_expression(interpreter, expression, &left, &right, out_value)) {
                value_destroy(&left);
                value_destroy(&right);
                return false;
            }
            break;
    }

    value_destroy(&left);
    value_destroy(&right);
    return true;
}

static bool evaluate_call_expression(Interpreter *interpreter, const Expression *expression, Value *out_value) {
    int arg_count = expression->as.call.arg_count;
    Value *arguments = NULL;
    int index;
    const Statement *function_statement;

    if (arg_count > 0) {
        arguments = (Value *) calloc((size_t) arg_count, sizeof(Value));
        if (arguments == NULL) {
            runtime_error_at(interpreter, expression->location, expression->as.call.name,
                             "I ran out of memory while preparing a function call.");
            return false;
        }
        for (index = 0; index < arg_count; index += 1) {
            arguments[index] = value_none();
        }
    }

    for (index = 0; index < arg_count; index += 1) {
        if (!evaluate_expression(interpreter, expression->as.call.arguments[index], &arguments[index])) {
            int cleanup_index;
            for (cleanup_index = 0; cleanup_index < arg_count; cleanup_index += 1) {
                value_destroy(&arguments[cleanup_index]);
            }
            free(arguments);
            return false;
        }
    }

    if (builtins_is_name(expression->as.call.name)) {
        bool ok = builtins_execute_call(expression,
                                        arguments,
                                        arg_count,
                                        out_value,
                                        interpreter->error_message,
                                        interpreter->error_size);
        for (index = 0; index < arg_count; index += 1) {
            value_destroy(&arguments[index]);
        }
        free(arguments);
        if (!ok) {
            interpreter->had_error = true;
        }
        return ok;
    }

    function_statement = runtime_get_function(interpreter->runtime, expression->as.call.name);
    if (function_statement == NULL) {
        for (index = 0; index < arg_count; index += 1) {
            value_destroy(&arguments[index]);
        }
        free(arguments);
        runtime_error_at(interpreter, expression->location, expression->as.call.name,
                         "I do not know the function '%s'. Define it first or import it with use.",
                         expression->as.call.name);
        return false;
    }

    if (function_statement->as.function_stmt.param_count != arg_count) {
        for (index = 0; index < arg_count; index += 1) {
            value_destroy(&arguments[index]);
        }
        free(arguments);
        runtime_error_at(interpreter, expression->location, expression->as.call.name,
                         "The function '%s' expects %d argument%s, but you gave %d.",
                         expression->as.call.name,
                         function_statement->as.function_stmt.param_count,
                         function_statement->as.function_stmt.param_count == 1 ? "" : "s",
                         arg_count);
        return false;
    }

    if (interpreter->call_depth >= 256) {
        for (index = 0; index < arg_count; index += 1) {
            value_destroy(&arguments[index]);
        }
        free(arguments);
        runtime_error_at(interpreter, expression->location, expression->as.call.name,
                         "This function call chain is too deep. Check for recursion that never ends.");
        return false;
    }

    if (!runtime_push_scope(interpreter->runtime)) {
        for (index = 0; index < arg_count; index += 1) {
            value_destroy(&arguments[index]);
        }
        free(arguments);
        runtime_error_at(interpreter, expression->location, expression->as.call.name,
                         "I could not create a new function scope.");
        return false;
    }

    interpreter->call_depth += 1;

    for (index = 0; index < arg_count; index += 1) {
        if (!runtime_define_variable(interpreter->runtime,
                                     function_statement->as.function_stmt.parameters[index],
                                     arguments[index])) {
            int cleanup_index;
            arguments[index] = value_none();
            for (cleanup_index = index + 1; cleanup_index < arg_count; cleanup_index += 1) {
                value_destroy(&arguments[cleanup_index]);
            }
            free(arguments);
            interpreter->call_depth -= 1;
            (void) runtime_pop_scope(interpreter->runtime);
            runtime_error_at(interpreter, expression->location, expression->as.call.name,
                             "I could not store the function argument '%s'.",
                             function_statement->as.function_stmt.parameters[index]);
            return false;
        }
        arguments[index] = value_none();
    }

    free(arguments);

    {
        ExecutionResult result = execute_statement_list(interpreter,
                                                        (const Statement *const *) function_statement->as.function_stmt.body,
                                                        function_statement->as.function_stmt.body_count);

        interpreter->call_depth -= 1;
        (void) runtime_pop_scope(interpreter->runtime);

        if (result.status == EXEC_ERROR) {
            value_destroy(&result.value);
            return false;
        }

        if (result.status == EXEC_BREAK || result.status == EXEC_CONTINUE) {
            value_destroy(&result.value);
            runtime_error_at(interpreter, result.location, NULL,
                             "Break and continue only make sense inside loops.");
            return false;
        }

        if (result.status == EXEC_RETURN) {
            *out_value = result.value;
            result.value = value_none();
            return true;
        }

        *out_value = value_none();
        return true;
    }
}

static bool evaluate_expression(Interpreter *interpreter, const Expression *expression, Value *out_value) {
    switch (expression->type) {
        case EXPR_NUMBER:
            *out_value = value_number(expression->as.number);
            return true;

        case EXPR_STRING:
            if (!value_make_string_copy(expression->as.string, out_value)) {
                runtime_error_at(interpreter, expression->location, NULL,
                                 "I ran out of memory while copying a string.");
                return false;
            }
            return true;

        case EXPR_BOOLEAN:
            *out_value = value_boolean(expression->as.boolean);
            return true;

        case EXPR_NONE:
            *out_value = value_none();
            return true;

        case EXPR_VARIABLE:
            if (!runtime_get_variable(interpreter->runtime, expression->as.variable_name, out_value)) {
                runtime_error_at(interpreter, expression->location, expression->as.variable_name,
                                 "I do not know the variable '%s' yet. Create it with 'let' first.",
                                 expression->as.variable_name);
                return false;
            }
            return true;

        case EXPR_LIST:
            return evaluate_list_expression(interpreter, expression, out_value);

        case EXPR_RECORD:
            return evaluate_record_expression(interpreter, expression, out_value);

        case EXPR_GROUPING:
            return evaluate_expression(interpreter, expression->as.grouping.inner, out_value);

        case EXPR_UNARY:
            return evaluate_unary_expression(interpreter, expression, out_value);

        case EXPR_BINARY:
            return evaluate_binary_expression(interpreter, expression, out_value);

        case EXPR_COMPARISON:
            return evaluate_comparison_expression(interpreter, expression, out_value);

        case EXPR_CALL:
            return evaluate_call_expression(interpreter, expression, out_value);
    }

    runtime_error_at(interpreter, expression->location, NULL,
                     "I do not understand this expression.");
    return false;
}

static ExecutionResult execute_scoped_block(Interpreter *interpreter,
                                            const Statement *const *statements,
                                            int count) {
    ExecutionResult result;

    if (!runtime_push_scope(interpreter->runtime)) {
        runtime_error_at(interpreter, (SourceLocation) {0, NULL, NULL}, NULL,
                         "I could not create a new block scope.");
        return exec_error();
    }

    result = execute_statement_list(interpreter, statements, count);
    if (!runtime_pop_scope(interpreter->runtime)) {
        if (!interpreter->had_error) {
            runtime_error_at(interpreter, (SourceLocation) {0, NULL, NULL}, NULL,
                             "I could not close a block scope cleanly.");
        }
        value_destroy(&result.value);
        return exec_error();
    }

    return result;
}

static bool register_top_level_functions(Interpreter *interpreter, const Program *program) {
    int index;

    for (index = 0; index < program->count; index += 1) {
        if (program->statements[index]->type == STMT_FUNCTION) {
            if (!runtime_define_function(interpreter->runtime,
                                         program->statements[index]->as.function_stmt.name,
                                         program->statements[index])) {
                runtime_error_at(interpreter, program->statements[index]->location,
                                 program->statements[index]->as.function_stmt.name,
                                 "I could not remember the function '%s'.",
                                 program->statements[index]->as.function_stmt.name);
                return false;
            }
        }
    }

    return true;
}

static ExecutionResult execute_use_statement(Interpreter *interpreter, const Statement *statement) {
    char *resolved_path = NULL;
    char *source = NULL;
    LexedProgram *lexed_program = NULL;
    Program *program = NULL;
    char import_error[1024];
    ExecutionResult result = exec_ok();

    if (!path_resolve_relative_copy(statement->location.file_path,
                                    statement->as.use_stmt.path,
                                    &resolved_path,
                                    import_error,
                                    sizeof(import_error))) {
        runtime_error_at(interpreter, statement->location, statement->as.use_stmt.path, "%s", import_error);
        return exec_error();
    }

    if (loaded_module_list_contains(interpreter->loaded_modules, interpreter->loaded_module_count, resolved_path)) {
        free(resolved_path);
        return exec_ok();
    }

    if (active_module_list_contains(interpreter->active_modules, interpreter->active_module_count, resolved_path)) {
        runtime_error_at(interpreter, statement->location, statement->as.use_stmt.path,
                         "I found a circular import involving '%s'.", resolved_path);
        free(resolved_path);
        return exec_error();
    }

    if (!push_active_module(interpreter, resolved_path)) {
        runtime_error_at(interpreter, statement->location, statement->as.use_stmt.path,
                         "I ran out of memory while tracking imports.");
        free(resolved_path);
        return exec_error();
    }

    if (!read_text_file(resolved_path, &source, import_error, sizeof(import_error))) {
        runtime_error_at(interpreter, statement->location, statement->as.use_stmt.path, "%s", import_error);
        (void) pop_active_module(interpreter);
        free(resolved_path);
        return exec_error();
    }

    lexed_program = lex_source_named(source, resolved_path, interpreter->error_message, interpreter->error_size);
    if (lexed_program == NULL) {
        interpreter->had_error = true;
        result = exec_error();
        goto cleanup;
    }

    program = parse_program(lexed_program, interpreter->error_message, interpreter->error_size);
    if (program == NULL) {
        interpreter->had_error = true;
        result = exec_error();
        goto cleanup;
    }

    if (!register_top_level_functions(interpreter, program)) {
        result = exec_error();
        goto cleanup;
    }

    result = execute_statement_list(interpreter, (const Statement *const *) program->statements, program->count);
    if (result.status == EXEC_RETURN) {
        value_destroy(&result.value);
        runtime_error_at(interpreter, result.location, NULL,
                         "I found a return statement outside of a function.");
        result = exec_error();
        goto cleanup;
    }
    if (result.status == EXEC_BREAK || result.status == EXEC_CONTINUE) {
        value_destroy(&result.value);
        runtime_error_at(interpreter, result.location, NULL,
                         "I found break or continue outside of a loop.");
        result = exec_error();
        goto cleanup;
    }

    {
        char *active_path = pop_active_module(interpreter);

        if (active_path == NULL) {
            runtime_error_at(interpreter, statement->location, statement->as.use_stmt.path,
                             "I lost track of an import while loading '%s'.", resolved_path);
            result = exec_error();
            goto cleanup;
        }

        if (!append_loaded_module(interpreter, active_path, source, lexed_program, program)) {
            free(active_path);
            runtime_error_at(interpreter, statement->location, statement->as.use_stmt.path,
                             "I ran out of memory while finishing an import.");
            result = exec_error();
            goto cleanup;
        }

        resolved_path = NULL;
        source = NULL;
        lexed_program = NULL;
        program = NULL;
    }

cleanup:
    if (resolved_path != NULL &&
        interpreter->active_module_count > 0 &&
        strcmp(interpreter->active_modules[interpreter->active_module_count - 1], resolved_path) == 0) {
        char *active_path = pop_active_module(interpreter);
        free(active_path);
    }

    free_program(program);
    free_lexed_program(lexed_program);
    free(source);
    return result;
}

static ExecutionResult execute_let_statement(Interpreter *interpreter, const Statement *statement) {
    Value value = value_none();

    if (!evaluate_expression(interpreter, statement->as.let_stmt.value, &value)) {
        return exec_error();
    }

    if (!runtime_define_variable(interpreter->runtime, statement->as.let_stmt.name, value)) {
        value_destroy(&value);
        runtime_error_at(interpreter, statement->location, statement->as.let_stmt.name,
                         "I could not store the variable '%s'.",
                         statement->as.let_stmt.name);
        return exec_error();
    }

    return exec_ok();
}

static ExecutionResult execute_set_statement(Interpreter *interpreter, const Statement *statement) {
    Value value = value_none();

    if (!evaluate_expression(interpreter, statement->as.set_stmt.value, &value)) {
        return exec_error();
    }

    if (!runtime_assign_variable(interpreter->runtime, statement->as.set_stmt.name, value)) {
        value_destroy(&value);
        runtime_error_at(interpreter, statement->location, statement->as.set_stmt.name,
                         "I do not know the variable '%s' yet. Create it with 'let' first.",
                         statement->as.set_stmt.name);
        return exec_error();
    }

    return exec_ok();
}

static ExecutionResult execute_say_statement(Interpreter *interpreter, const Statement *statement) {
    Value value = value_none();

    if (!evaluate_expression(interpreter, statement->as.say_stmt.value, &value)) {
        return exec_error();
    }

    value_fprint(stdout, &value);
    (void) fputc('\n', stdout);
    value_destroy(&value);
    return exec_ok();
}

static ExecutionResult execute_ask_statement(Interpreter *interpreter, const Statement *statement) {
    Value prompt = value_none();
    char *input = NULL;
    Value stored_value = value_none();
    double number;
    bool boolean;

    if (!evaluate_expression(interpreter, statement->as.ask_stmt.prompt, &prompt)) {
        return exec_error();
    }

    value_fprint(stdout, &prompt);
    (void) fputc(' ', stdout);
    (void) fflush(stdout);
    value_destroy(&prompt);

    input = read_input_line();
    if (input == NULL) {
        runtime_error_at(interpreter, statement->location, NULL,
                         "I could not read input from the user.");
        return exec_error();
    }

    if (value_try_parse_number(input, &number)) {
        stored_value = value_number(number);
        free(input);
    } else if (value_try_parse_boolean(input, &boolean)) {
        stored_value = value_boolean(boolean);
        free(input);
    } else {
        stored_value = value_string_owned(input);
    }

    if (runtime_assign_variable(interpreter->runtime, statement->as.ask_stmt.name, stored_value)) {
        return exec_ok();
    }

    if (!runtime_define_variable(interpreter->runtime, statement->as.ask_stmt.name, stored_value)) {
        value_destroy(&stored_value);
        runtime_error_at(interpreter, statement->location, statement->as.ask_stmt.name,
                         "I could not store the answer in '%s'.",
                         statement->as.ask_stmt.name);
        return exec_error();
    }

    return exec_ok();
}

static ExecutionResult execute_if_statement(Interpreter *interpreter, const Statement *statement) {
    Value condition = value_none();

    if (!evaluate_expression(interpreter, statement->as.if_stmt.condition, &condition)) {
        return exec_error();
    }

    if (value_is_truthy(&condition)) {
        value_destroy(&condition);
        return execute_scoped_block(interpreter,
                                    (const Statement *const *) statement->as.if_stmt.then_body,
                                    statement->as.if_stmt.then_count);
    }

    value_destroy(&condition);
    return execute_scoped_block(interpreter,
                                (const Statement *const *) statement->as.if_stmt.else_body,
                                statement->as.if_stmt.else_count);
}

static ExecutionResult execute_repeat_statement(Interpreter *interpreter, const Statement *statement) {
    Value count_value = value_none();
    double number;
    int iteration_count;
    int index;

    if (!evaluate_expression(interpreter, statement->as.repeat_stmt.count, &count_value)) {
        return exec_error();
    }

    if (!value_to_number(&count_value, &number) || !is_whole_number(number) || number < 0.0) {
        value_destroy(&count_value);
        runtime_error_at(interpreter, statement->location, NULL,
                         "The repeat count should be a whole number that is zero or more.");
        return exec_error();
    }

    iteration_count = (int) round(number);
    value_destroy(&count_value);

    for (index = 0; index < iteration_count; index += 1) {
        ExecutionResult result = execute_scoped_block(interpreter,
                                                      (const Statement *const *) statement->as.repeat_stmt.body,
                                                      statement->as.repeat_stmt.body_count);
        if (result.status == EXEC_BREAK) {
            value_destroy(&result.value);
            return exec_ok();
        }
        if (result.status == EXEC_CONTINUE) {
            value_destroy(&result.value);
            continue;
        }
        if (result.status != EXEC_OK) {
            return result;
        }
    }

    return exec_ok();
}

static ExecutionResult execute_while_statement(Interpreter *interpreter, const Statement *statement) {
    while (1) {
        Value condition = value_none();
        ExecutionResult result;

        if (!evaluate_expression(interpreter, statement->as.while_stmt.condition, &condition)) {
            return exec_error();
        }

        if (!value_is_truthy(&condition)) {
            value_destroy(&condition);
            break;
        }
        value_destroy(&condition);

        result = execute_scoped_block(interpreter,
                                      (const Statement *const *) statement->as.while_stmt.body,
                                      statement->as.while_stmt.body_count);
        if (result.status == EXEC_BREAK) {
            value_destroy(&result.value);
            return exec_ok();
        }
        if (result.status == EXEC_CONTINUE) {
            value_destroy(&result.value);
            continue;
        }
        if (result.status != EXEC_OK) {
            return result;
        }
    }

    return exec_ok();
}

static ExecutionResult execute_for_each_statement(Interpreter *interpreter, const Statement *statement) {
    Value collection = value_none();
    int index;
    int length;

    if (!evaluate_expression(interpreter, statement->as.for_each_stmt.collection, &collection)) {
        return exec_error();
    }

    if (collection.type == VALUE_RECORD) {
        Value keys = value_none();

        if (!value_record_keys(&collection, &keys)) {
            value_destroy(&collection);
            runtime_error_at(interpreter, statement->location, statement->as.for_each_stmt.item_name,
                             "I could not read the record fields for this loop.");
            return exec_error();
        }

        value_destroy(&collection);
        collection = keys;
    }

    if (collection.type == VALUE_LIST) {
        length = value_list_length(&collection);
        for (index = 1; index <= length; index += 1) {
            Value item = value_none();
            ExecutionResult result;

            if (!value_list_get(&collection, index, &item)) {
                value_destroy(&collection);
                runtime_error_at(interpreter, statement->location, statement->as.for_each_stmt.item_name,
                                 "I could not read an item while looping.");
                return exec_error();
            }

            if (!runtime_push_scope(interpreter->runtime)) {
                value_destroy(&item);
                value_destroy(&collection);
                runtime_error_at(interpreter, statement->location, NULL,
                                 "I could not create a new loop scope.");
                return exec_error();
            }

            if (!runtime_define_variable(interpreter->runtime, statement->as.for_each_stmt.item_name, item)) {
                value_destroy(&item);
                value_destroy(&collection);
                (void) runtime_pop_scope(interpreter->runtime);
                runtime_error_at(interpreter, statement->location, statement->as.for_each_stmt.item_name,
                                 "I could not store the loop item '%s'.",
                                 statement->as.for_each_stmt.item_name);
                return exec_error();
            }

            result = execute_statement_list(interpreter,
                                            (const Statement *const *) statement->as.for_each_stmt.body,
                                            statement->as.for_each_stmt.body_count);
            if (!runtime_pop_scope(interpreter->runtime)) {
                value_destroy(&collection);
                value_destroy(&result.value);
                runtime_error_at(interpreter, statement->location, NULL,
                                 "I could not close a loop scope cleanly.");
                return exec_error();
            }

            if (result.status == EXEC_BREAK) {
                value_destroy(&result.value);
                value_destroy(&collection);
                return exec_ok();
            }
            if (result.status == EXEC_CONTINUE) {
                value_destroy(&result.value);
                continue;
            }
            if (result.status != EXEC_OK) {
                value_destroy(&collection);
                return result;
            }
        }

        value_destroy(&collection);
        return exec_ok();
    }

    if (collection.type == VALUE_STRING) {
        const char *text = collection.string != NULL ? collection.string : "";
        size_t text_length = strlen(text);

        for (index = 0; (size_t) index < text_length; index += 1) {
            char character_text[2];
            Value item = value_none();
            ExecutionResult result;

            character_text[0] = text[index];
            character_text[1] = '\0';
            if (!value_make_string_copy(character_text, &item)) {
                value_destroy(&collection);
                runtime_error_at(interpreter, statement->location, statement->as.for_each_stmt.item_name,
                                 "I ran out of memory while looping over text.");
                return exec_error();
            }

            if (!runtime_push_scope(interpreter->runtime)) {
                value_destroy(&item);
                value_destroy(&collection);
                runtime_error_at(interpreter, statement->location, NULL,
                                 "I could not create a new loop scope.");
                return exec_error();
            }

            if (!runtime_define_variable(interpreter->runtime, statement->as.for_each_stmt.item_name, item)) {
                value_destroy(&item);
                value_destroy(&collection);
                (void) runtime_pop_scope(interpreter->runtime);
                runtime_error_at(interpreter, statement->location, statement->as.for_each_stmt.item_name,
                                 "I could not store the loop item '%s'.",
                                 statement->as.for_each_stmt.item_name);
                return exec_error();
            }

            result = execute_statement_list(interpreter,
                                            (const Statement *const *) statement->as.for_each_stmt.body,
                                            statement->as.for_each_stmt.body_count);
            if (!runtime_pop_scope(interpreter->runtime)) {
                value_destroy(&collection);
                value_destroy(&result.value);
                runtime_error_at(interpreter, statement->location, NULL,
                                 "I could not close a loop scope cleanly.");
                return exec_error();
            }

            if (result.status == EXEC_BREAK) {
                value_destroy(&result.value);
                value_destroy(&collection);
                return exec_ok();
            }
            if (result.status == EXEC_CONTINUE) {
                value_destroy(&result.value);
                continue;
            }
            if (result.status != EXEC_OK) {
                value_destroy(&collection);
                return result;
            }
        }

        value_destroy(&collection);
        return exec_ok();
    }

    value_destroy(&collection);
    runtime_error_at(interpreter, statement->location, statement->as.for_each_stmt.item_name,
                     "A for each loop works with a list, text, or record.");
    return exec_error();
}

static ExecutionResult execute_function_statement(Interpreter *interpreter, const Statement *statement) {
    if (!runtime_define_function(interpreter->runtime, statement->as.function_stmt.name, statement)) {
        runtime_error_at(interpreter, statement->location, statement->as.function_stmt.name,
                         "I could not remember the function '%s'.",
                         statement->as.function_stmt.name);
        return exec_error();
    }

    return exec_ok();
}

static ExecutionResult execute_call_statement(Interpreter *interpreter, const Statement *statement) {
    Value value = value_none();

    if (!evaluate_expression(interpreter, statement->as.call_stmt.call, &value)) {
        return exec_error();
    }

    value_destroy(&value);
    return exec_ok();
}

static ExecutionResult execute_return_statement(Interpreter *interpreter, const Statement *statement) {
    Value value = value_none();

    (void) interpreter;

    if (statement->as.return_stmt.value != NULL) {
        if (!evaluate_expression(interpreter, statement->as.return_stmt.value, &value)) {
            return exec_error();
        }
    }

    return exec_return(value, statement->location);
}

static ExecutionResult execute_statement(Interpreter *interpreter, const Statement *statement) {
    trace_statement(interpreter, statement);

    switch (statement->type) {
        case STMT_USE:
            return execute_use_statement(interpreter, statement);
        case STMT_LET:
            return execute_let_statement(interpreter, statement);
        case STMT_SET:
            return execute_set_statement(interpreter, statement);
        case STMT_SAY:
            return execute_say_statement(interpreter, statement);
        case STMT_ASK:
            return execute_ask_statement(interpreter, statement);
        case STMT_IF:
            return execute_if_statement(interpreter, statement);
        case STMT_REPEAT:
            return execute_repeat_statement(interpreter, statement);
        case STMT_WHILE:
            return execute_while_statement(interpreter, statement);
        case STMT_FOR_EACH:
            return execute_for_each_statement(interpreter, statement);
        case STMT_FUNCTION:
            return execute_function_statement(interpreter, statement);
        case STMT_CALL:
            return execute_call_statement(interpreter, statement);
        case STMT_RETURN:
            return execute_return_statement(interpreter, statement);
        case STMT_BREAK:
            return exec_simple(EXEC_BREAK, statement->location);
        case STMT_CONTINUE:
            return exec_simple(EXEC_CONTINUE, statement->location);
    }

    runtime_error_at(interpreter, statement->location, NULL,
                     "I do not know how to run this statement.");
    return exec_error();
}

static ExecutionResult execute_statement_list(Interpreter *interpreter,
                                              const Statement *const *statements,
                                              int count) {
    int index;

    for (index = 0; index < count; index += 1) {
        ExecutionResult result = execute_statement(interpreter, statements[index]);
        if (result.status != EXEC_OK) {
            return result;
        }
    }

    return exec_ok();
}

bool interpret_program(const Program *program,
                       Runtime *runtime,
                       const InterpreterOptions *options,
                       char *error_message,
                       size_t error_size) {
    Interpreter interpreter;
    ExecutionResult result;

    interpreter.runtime = runtime;
    interpreter.options = options;
    interpreter.error_message = error_message;
    interpreter.error_size = error_size;
    interpreter.had_error = false;
    interpreter.call_depth = 0;
    interpreter.loaded_modules = NULL;
    interpreter.loaded_module_count = 0;
    interpreter.active_modules = NULL;
    interpreter.active_module_count = 0;

    if (!register_top_level_functions(&interpreter, program)) {
        free_module_lists(&interpreter);
        return false;
    }

    result = execute_statement_list(&interpreter, (const Statement *const *) program->statements, program->count);
    if (result.status == EXEC_OK) {
        free_module_lists(&interpreter);
        return true;
    }

    value_destroy(&result.value);

    if (result.status == EXEC_RETURN) {
        runtime_error_at(&interpreter, result.location, NULL,
                         "I found a return statement outside of a function.");
    } else if (result.status == EXEC_BREAK || result.status == EXEC_CONTINUE) {
        runtime_error_at(&interpreter, result.location, NULL,
                         "I found break or continue outside of a loop.");
    }

    free_module_lists(&interpreter);
    return false;
}
