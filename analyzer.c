#include "analyzer.h"

#include "builtins.h"
#include "files.h"
#include "lexer.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char **names;
    int count;
} NameList;

typedef struct {
    NameList *scopes;
    int scope_count;
    int loop_depth;
    int function_depth;
} AnalysisContext;

typedef struct {
    AnalyzerReport *report;
    char *error_message;
    size_t error_size;
    bool had_error;
    NameList functions;
    NameList global_candidates;
    NameList visited_imports;
} AnalyzerState;

static bool analyze_statement_list(AnalyzerState *state,
                                   const Statement *const *statements,
                                   int count,
                                   AnalysisContext *context);
static bool analyze_statement(AnalyzerState *state,
                              const Statement *statement,
                              AnalysisContext *context);
static bool analyze_expression(AnalyzerState *state,
                               const Expression *expression,
                               AnalysisContext *context);
static bool collect_function_names_from_statements(AnalyzerState *state,
                                                   const Statement *const *statements,
                                                   int count);

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

static char *duplicate_text(const char *text) {
    size_t length = strlen(text);
    char *copy = (char *) malloc(length + 1U);

    if (copy == NULL) {
        return NULL;
    }

    (void) memcpy(copy, text, length + 1U);
    return copy;
}

static void set_error(AnalyzerState *state, const char *format, ...) {
    va_list args;

    if (state->had_error) {
        return;
    }

    state->had_error = true;
    if (state->error_message == NULL || state->error_size == 0) {
        return;
    }

    va_start(args, format);
    (void) vsnprintf(state->error_message, state->error_size, format, args);
    va_end(args);
}

static bool append_owned_name(NameList *list, char *name) {
    char **new_names = (char **) realloc(list->names, (size_t) (list->count + 1) * sizeof(char *));

    if (new_names == NULL) {
        return false;
    }

    list->names = new_names;
    list->names[list->count] = name;
    list->count += 1;
    return true;
}

static bool add_name_copy(NameList *list, const char *name) {
    char *copy = duplicate_text(name);

    if (copy == NULL) {
        return false;
    }

    if (!append_owned_name(list, copy)) {
        free(copy);
        return false;
    }

    return true;
}

static bool name_list_contains(const NameList *list, const char *name) {
    int index;

    for (index = 0; index < list->count; index += 1) {
        if (equals_ignore_case(list->names[index], name)) {
            return true;
        }
    }

    return false;
}

static void free_name_list(NameList *list) {
    int index;

    for (index = 0; index < list->count; index += 1) {
        free(list->names[index]);
    }
    free(list->names);
    list->names = NULL;
    list->count = 0;
}

static bool add_warning(AnalyzerState *state, SourceLocation location, const char *format, ...) {
    AnalyzerWarning *new_warnings;
    char buffer[512];
    va_list args;
    char *message;

    new_warnings = (AnalyzerWarning *) realloc(state->report->warnings,
                                               (size_t) (state->report->count + 1) * sizeof(AnalyzerWarning));
    if (new_warnings == NULL) {
        set_error(state, "I ran out of memory while recording lint warnings.");
        return false;
    }

    state->report->warnings = new_warnings;

    va_start(args, format);
    (void) vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    message = duplicate_text(buffer);
    if (message == NULL) {
        set_error(state, "I ran out of memory while recording lint warnings.");
        return false;
    }

    state->report->warnings[state->report->count].location = location;
    state->report->warnings[state->report->count].message = message;
    state->report->count += 1;
    return true;
}

static bool collect_imported_names_from_statements(AnalyzerState *state,
                                                   const Statement *const *statements,
                                                   int count);

static bool push_scope(AnalyzerState *state, AnalysisContext *context) {
    NameList *new_scopes = (NameList *) realloc(context->scopes,
                                                (size_t) (context->scope_count + 1) * sizeof(NameList));

    if (new_scopes == NULL) {
        set_error(state, "I ran out of memory while linting scopes.");
        return false;
    }

    context->scopes = new_scopes;
    context->scopes[context->scope_count].names = NULL;
    context->scopes[context->scope_count].count = 0;
    context->scope_count += 1;
    return true;
}

static void pop_scope(AnalysisContext *context) {
    if (context->scope_count == 0) {
        return;
    }

    context->scope_count -= 1;
    free_name_list(&context->scopes[context->scope_count]);
    if (context->scope_count == 0) {
        free(context->scopes);
        context->scopes = NULL;
    }
}

static bool define_in_current_scope(AnalyzerState *state, AnalysisContext *context, const char *name) {
    if (context->scope_count == 0 && !push_scope(state, context)) {
        return false;
    }

    if (name_list_contains(&context->scopes[context->scope_count - 1], name)) {
        return true;
    }

    if (!add_name_copy(&context->scopes[context->scope_count - 1], name)) {
        set_error(state, "I ran out of memory while linting names.");
        return false;
    }

    return true;
}

static bool name_exists_in_context(const AnalysisContext *context, const char *name) {
    int scope_index;

    for (scope_index = context->scope_count - 1; scope_index >= 0; scope_index -= 1) {
        if (name_list_contains(&context->scopes[scope_index], name)) {
            return true;
        }
    }

    return false;
}

static bool analyze_function_body(AnalyzerState *state, const Statement *statement) {
    AnalysisContext context;
    int index;

    context.scopes = NULL;
    context.scope_count = 0;
    context.loop_depth = 0;
    context.function_depth = 1;

    if (!push_scope(state, &context)) {
        return false;
    }

    for (index = 0; index < state->global_candidates.count; index += 1) {
        if (!define_in_current_scope(state, &context, state->global_candidates.names[index])) {
            while (context.scope_count > 0) {
                pop_scope(&context);
            }
            return false;
        }
    }

    if (!push_scope(state, &context)) {
        while (context.scope_count > 0) {
            pop_scope(&context);
        }
        return false;
    }

    for (index = 0; index < statement->as.function_stmt.param_count; index += 1) {
        const char *parameter = statement->as.function_stmt.parameters[index];
        if (name_list_contains(&context.scopes[context.scope_count - 1], parameter)) {
            if (!add_warning(state, statement->location,
                             "Function '%s' repeats the parameter name '%s'.",
                             statement->as.function_stmt.name,
                             parameter)) {
                while (context.scope_count > 0) {
                    pop_scope(&context);
                }
                return false;
            }
        } else if (!define_in_current_scope(state, &context, parameter)) {
            while (context.scope_count > 0) {
                pop_scope(&context);
            }
            return false;
        }
    }

    if (!analyze_statement_list(state,
                                (const Statement *const *) statement->as.function_stmt.body,
                                statement->as.function_stmt.body_count,
                                &context)) {
        while (context.scope_count > 0) {
            pop_scope(&context);
        }
        return false;
    }

    while (context.scope_count > 0) {
        pop_scope(&context);
    }

    return true;
}

static bool collect_global_candidates(const Program *program, NameList *globals) {
    int index;

    for (index = 0; index < program->count; index += 1) {
        const Statement *statement = program->statements[index];

        if (statement->type == STMT_LET) {
            if (!name_list_contains(globals, statement->as.let_stmt.name) &&
                !add_name_copy(globals, statement->as.let_stmt.name)) {
                return false;
            }
        } else if (statement->type == STMT_ASK) {
            if (!name_list_contains(globals, statement->as.ask_stmt.name) &&
                !add_name_copy(globals, statement->as.ask_stmt.name)) {
                return false;
            }
        }
    }

    return true;
}

static bool collect_imported_file(AnalyzerState *state,
                                  const char *base_file_path,
                                  const char *import_path) {
    char *resolved_path = NULL;
    char *source = NULL;
    LexedProgram *lexed_program = NULL;
    Program *program = NULL;

    if (!path_resolve_relative_copy(base_file_path,
                                    import_path,
                                    &resolved_path,
                                    state->error_message,
                                    state->error_size)) {
        state->had_error = true;
        return false;
    }

    if (name_list_contains(&state->visited_imports, resolved_path)) {
        free(resolved_path);
        return true;
    }

    if (!append_owned_name(&state->visited_imports, resolved_path)) {
        free(resolved_path);
        set_error(state, "I ran out of memory while following imports for the linter.");
        return false;
    }

    if (!read_text_file(resolved_path, &source, state->error_message, state->error_size)) {
        state->had_error = true;
        return false;
    }

    lexed_program = lex_source_named(source, resolved_path, state->error_message, state->error_size);
    if (lexed_program == NULL) {
        state->had_error = true;
        free(source);
        return false;
    }

    program = parse_program(lexed_program, state->error_message, state->error_size);
    if (program == NULL) {
        state->had_error = true;
        free_lexed_program(lexed_program);
        free(source);
        return false;
    }

    if (!collect_global_candidates(program, &state->global_candidates) ||
        !collect_function_names_from_statements(state,
                                                (const Statement *const *) program->statements,
                                                program->count) ||
        !collect_imported_names_from_statements(state,
                                                (const Statement *const *) program->statements,
                                                program->count)) {
        if (!state->had_error) {
            set_error(state, "I ran out of memory while following imports for the linter.");
        }
        free_program(program);
        free_lexed_program(lexed_program);
        free(source);
        return false;
    }

    free_program(program);
    free_lexed_program(lexed_program);
    free(source);
    return true;
}

static bool collect_function_names_from_statements(AnalyzerState *state,
                                                   const Statement *const *statements,
                                                   int count) {
    int index;

    for (index = 0; index < count; index += 1) {
        const Statement *statement = statements[index];

        if (statement->type == STMT_FUNCTION) {
            if (name_list_contains(&state->functions, statement->as.function_stmt.name)) {
                if (!add_warning(state, statement->location,
                                 "The function '%s' is defined more than once. Later definitions replace earlier ones.",
                                 statement->as.function_stmt.name)) {
                    return false;
                }
            } else if (!add_name_copy(&state->functions, statement->as.function_stmt.name)) {
                set_error(state, "I ran out of memory while linting function names.");
                return false;
            }

            if (!collect_function_names_from_statements(state,
                                                        (const Statement *const *) statement->as.function_stmt.body,
                                                        statement->as.function_stmt.body_count)) {
                return false;
            }
        } else if (statement->type == STMT_IF) {
            if (!collect_function_names_from_statements(state,
                                                        (const Statement *const *) statement->as.if_stmt.then_body,
                                                        statement->as.if_stmt.then_count) ||
                !collect_function_names_from_statements(state,
                                                        (const Statement *const *) statement->as.if_stmt.else_body,
                                                        statement->as.if_stmt.else_count)) {
                return false;
            }
        } else if (statement->type == STMT_REPEAT) {
            if (!collect_function_names_from_statements(state,
                                                        (const Statement *const *) statement->as.repeat_stmt.body,
                                                        statement->as.repeat_stmt.body_count)) {
                return false;
            }
        } else if (statement->type == STMT_WHILE) {
            if (!collect_function_names_from_statements(state,
                                                        (const Statement *const *) statement->as.while_stmt.body,
                                                        statement->as.while_stmt.body_count)) {
                return false;
            }
        } else if (statement->type == STMT_FOR_EACH) {
            if (!collect_function_names_from_statements(state,
                                                        (const Statement *const *) statement->as.for_each_stmt.body,
                                                        statement->as.for_each_stmt.body_count)) {
                return false;
            }
        }
    }

    return true;
}

static bool collect_imported_names_from_statements(AnalyzerState *state,
                                                   const Statement *const *statements,
                                                   int count) {
    int index;

    for (index = 0; index < count; index += 1) {
        const Statement *statement = statements[index];

        if (statement->type == STMT_USE) {
            if (!collect_imported_file(state, statement->location.file_path, statement->as.use_stmt.path)) {
                return false;
            }
        } else if (statement->type == STMT_IF) {
            if (!collect_imported_names_from_statements(state,
                                                        (const Statement *const *) statement->as.if_stmt.then_body,
                                                        statement->as.if_stmt.then_count) ||
                !collect_imported_names_from_statements(state,
                                                        (const Statement *const *) statement->as.if_stmt.else_body,
                                                        statement->as.if_stmt.else_count)) {
                return false;
            }
        } else if (statement->type == STMT_REPEAT) {
            if (!collect_imported_names_from_statements(state,
                                                        (const Statement *const *) statement->as.repeat_stmt.body,
                                                        statement->as.repeat_stmt.body_count)) {
                return false;
            }
        } else if (statement->type == STMT_WHILE) {
            if (!collect_imported_names_from_statements(state,
                                                        (const Statement *const *) statement->as.while_stmt.body,
                                                        statement->as.while_stmt.body_count)) {
                return false;
            }
        } else if (statement->type == STMT_FOR_EACH) {
            if (!collect_imported_names_from_statements(state,
                                                        (const Statement *const *) statement->as.for_each_stmt.body,
                                                        statement->as.for_each_stmt.body_count)) {
                return false;
            }
        } else if (statement->type == STMT_FUNCTION) {
            if (!collect_imported_names_from_statements(state,
                                                        (const Statement *const *) statement->as.function_stmt.body,
                                                        statement->as.function_stmt.body_count)) {
                return false;
            }
        }
    }

    return true;
}

static bool analyze_expression(AnalyzerState *state,
                               const Expression *expression,
                               AnalysisContext *context) {
    int index;

    switch (expression->type) {
        case EXPR_NUMBER:
        case EXPR_STRING:
        case EXPR_BOOLEAN:
            return true;

        case EXPR_VARIABLE:
            if (!name_exists_in_context(context, expression->as.variable_name)) {
                if (!add_warning(state, expression->location,
                                 "The variable '%s' is used before it is defined in this scope.",
                                 expression->as.variable_name)) {
                    return false;
                }
            }
            return true;

        case EXPR_LIST:
            for (index = 0; index < expression->as.list.item_count; index += 1) {
                if (!analyze_expression(state, expression->as.list.items[index], context)) {
                    return false;
                }
            }
            return true;

        case EXPR_RECORD:
            for (index = 0; index < expression->as.record.count; index += 1) {
                if (!analyze_expression(state, expression->as.record.values[index], context)) {
                    return false;
                }
            }
            return true;

        case EXPR_GROUPING:
            return analyze_expression(state, expression->as.grouping.inner, context);

        case EXPR_UNARY:
            return analyze_expression(state, expression->as.unary.right, context);

        case EXPR_BINARY:
            return analyze_expression(state, expression->as.binary.left, context) &&
                   analyze_expression(state, expression->as.binary.right, context);

        case EXPR_COMPARISON:
            return analyze_expression(state, expression->as.comparison.left, context) &&
                   analyze_expression(state, expression->as.comparison.right, context);

        case EXPR_CALL:
            for (index = 0; index < expression->as.call.arg_count; index += 1) {
                if (!analyze_expression(state, expression->as.call.arguments[index], context)) {
                    return false;
                }
            }
            if (!builtins_is_name(expression->as.call.name) &&
                !name_list_contains(&state->functions, expression->as.call.name)) {
                if (!add_warning(state, expression->location,
                                 "The function '%s' is called, but no definition was found in this file.",
                                 expression->as.call.name)) {
                    return false;
                }
            }
            return true;
    }

    return true;
}

static bool analyze_block(AnalyzerState *state,
                          const Statement *const *statements,
                          int count,
                          AnalysisContext *context,
                          bool is_loop) {
    if (!push_scope(state, context)) {
        return false;
    }

    if (is_loop) {
        context->loop_depth += 1;
    }

    if (!analyze_statement_list(state, statements, count, context)) {
        if (is_loop) {
            context->loop_depth -= 1;
        }
        pop_scope(context);
        return false;
    }

    if (is_loop) {
        context->loop_depth -= 1;
    }
    pop_scope(context);
    return true;
}

static bool analyze_statement(AnalyzerState *state,
                              const Statement *statement,
                              AnalysisContext *context) {
    switch (statement->type) {
        case STMT_USE:
            return true;

        case STMT_LET:
            if (!analyze_expression(state, statement->as.let_stmt.value, context)) {
                return false;
            }
            if (context->scope_count > 0 &&
                name_list_contains(&context->scopes[context->scope_count - 1], statement->as.let_stmt.name)) {
                if (!add_warning(state, statement->location,
                                 "The name '%s' is defined again in the same scope.",
                                 statement->as.let_stmt.name)) {
                    return false;
                }
            }
            return define_in_current_scope(state, context, statement->as.let_stmt.name);

        case STMT_SET:
            if (!analyze_expression(state, statement->as.set_stmt.value, context)) {
                return false;
            }
            if (!name_exists_in_context(context, statement->as.set_stmt.name)) {
                if (!add_warning(state, statement->location,
                                 "The variable '%s' is changed with 'set' before it exists.",
                                 statement->as.set_stmt.name)) {
                    return false;
                }
            }
            return true;

        case STMT_SAY:
            return analyze_expression(state, statement->as.say_stmt.value, context);

        case STMT_ASK:
            if (!analyze_expression(state, statement->as.ask_stmt.prompt, context)) {
                return false;
            }
            if (!name_exists_in_context(context, statement->as.ask_stmt.name)) {
                return define_in_current_scope(state, context, statement->as.ask_stmt.name);
            }
            return true;

        case STMT_IF:
            if (!analyze_expression(state, statement->as.if_stmt.condition, context)) {
                return false;
            }
            return analyze_block(state,
                                 (const Statement *const *) statement->as.if_stmt.then_body,
                                 statement->as.if_stmt.then_count,
                                 context,
                                 false) &&
                   analyze_block(state,
                                 (const Statement *const *) statement->as.if_stmt.else_body,
                                 statement->as.if_stmt.else_count,
                                 context,
                                 false);

        case STMT_REPEAT:
            if (!analyze_expression(state, statement->as.repeat_stmt.count, context)) {
                return false;
            }
            return analyze_block(state,
                                 (const Statement *const *) statement->as.repeat_stmt.body,
                                 statement->as.repeat_stmt.body_count,
                                 context,
                                 true);

        case STMT_WHILE:
            if (!analyze_expression(state, statement->as.while_stmt.condition, context)) {
                return false;
            }
            return analyze_block(state,
                                 (const Statement *const *) statement->as.while_stmt.body,
                                 statement->as.while_stmt.body_count,
                                 context,
                                 true);

        case STMT_FOR_EACH:
            if (!analyze_expression(state, statement->as.for_each_stmt.collection, context)) {
                return false;
            }
            if (!push_scope(state, context)) {
                return false;
            }
            context->loop_depth += 1;
            if (!define_in_current_scope(state, context, statement->as.for_each_stmt.item_name) ||
                !analyze_statement_list(state,
                                        (const Statement *const *) statement->as.for_each_stmt.body,
                                        statement->as.for_each_stmt.body_count,
                                        context)) {
                context->loop_depth -= 1;
                pop_scope(context);
                return false;
            }
            context->loop_depth -= 1;
            pop_scope(context);
            return true;

        case STMT_FUNCTION:
            return analyze_function_body(state, statement);

        case STMT_CALL:
            return analyze_expression(state, statement->as.call_stmt.call, context);

        case STMT_RETURN:
            if (context->function_depth == 0) {
                if (!add_warning(state, statement->location,
                                 "This return statement is outside of a function.")) {
                    return false;
                }
            }
            if (statement->as.return_stmt.value != NULL) {
                return analyze_expression(state, statement->as.return_stmt.value, context);
            }
            return true;

        case STMT_BREAK:
            if (context->loop_depth == 0) {
                return add_warning(state, statement->location,
                                   "This break statement is outside of a loop.");
            }
            return true;

        case STMT_CONTINUE:
            if (context->loop_depth == 0) {
                return add_warning(state, statement->location,
                                   "This continue statement is outside of a loop.");
            }
            return true;
    }

    return true;
}

static bool analyze_statement_list(AnalyzerState *state,
                                   const Statement *const *statements,
                                   int count,
                                   AnalysisContext *context) {
    int index;

    for (index = 0; index < count; index += 1) {
        if (!analyze_statement(state, statements[index], context)) {
            return false;
        }
    }

    return true;
}

bool analyze_program(const Program *program, AnalyzerReport *report, char *error_message, size_t error_size) {
    AnalyzerState state;
    AnalysisContext context;

    report->warnings = NULL;
    report->count = 0;

    state.report = report;
    state.error_message = error_message;
    state.error_size = error_size;
    state.had_error = false;
    state.functions.names = NULL;
    state.functions.count = 0;
    state.global_candidates.names = NULL;
    state.global_candidates.count = 0;
    state.visited_imports.names = NULL;
    state.visited_imports.count = 0;

    context.scopes = NULL;
    context.scope_count = 0;
    context.loop_depth = 0;
    context.function_depth = 0;

    if (!collect_global_candidates(program, &state.global_candidates)) {
        set_error(&state, "I ran out of memory while linting global names.");
    }

    if (!state.had_error &&
        !collect_function_names_from_statements(&state,
                                                (const Statement *const *) program->statements,
                                                program->count)) {
        if (!state.had_error) {
            set_error(&state, "I ran out of memory while linting functions.");
        }
    }

    if (!state.had_error &&
        !collect_imported_names_from_statements(&state,
                                                (const Statement *const *) program->statements,
                                                program->count)) {
        if (!state.had_error) {
            set_error(&state, "I ran out of memory while following imports for the linter.");
        }
    }

    if (!state.had_error && !push_scope(&state, &context)) {
        set_error(&state, "I ran out of memory while linting scopes.");
    }

    if (!state.had_error &&
        !analyze_statement_list(&state,
                                (const Statement *const *) program->statements,
                                program->count,
                                &context)) {
        if (!state.had_error) {
            set_error(&state, "I ran out of memory while linting the program.");
        }
    }

    while (context.scope_count > 0) {
        pop_scope(&context);
    }
    free_name_list(&state.functions);
    free_name_list(&state.global_candidates);
    free_name_list(&state.visited_imports);

    return !state.had_error;
}

void print_analyzer_report(const AnalyzerReport *report, FILE *stream) {
    int index;

    if (report->count == 0) {
        (void) fputs("No lint warnings found.\n", stream);
        return;
    }

    for (index = 0; index < report->count; index += 1) {
        const AnalyzerWarning *warning = &report->warnings[index];
        if (warning->location.file_path != NULL) {
            (void) fprintf(stream, "%s:%d: warning: %s\n",
                           warning->location.file_path,
                           warning->location.line,
                           warning->message);
        } else if (warning->location.line > 0) {
            (void) fprintf(stream, "Line %d: warning: %s\n",
                           warning->location.line,
                           warning->message);
        } else {
            (void) fprintf(stream, "warning: %s\n", warning->message);
        }
    }
}

void free_analyzer_report(AnalyzerReport *report) {
    int index;

    for (index = 0; index < report->count; index += 1) {
        free(report->warnings[index].message);
    }
    free(report->warnings);
    report->warnings = NULL;
    report->count = 0;
}
