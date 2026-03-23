#include "lexer.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error_with_line(char *buffer, size_t buffer_size, const char *file_path,
                                int line, const char *source_text, const char *format, ...) {
    va_list args;
    size_t used;

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    if (file_path != NULL) {
        (void) snprintf(buffer, buffer_size, "%s:%d: ", file_path, line);
    } else if (line > 0) {
        (void) snprintf(buffer, buffer_size, "Line %d: ", line);
    } else {
        buffer[0] = '\0';
    }

    used = strlen(buffer);
    va_start(args, format);
    (void) vsnprintf(buffer + used, buffer_size - used, format, args);
    va_end(args);

    if (source_text != NULL && source_text[0] != '\0') {
        used = strlen(buffer);
        (void) snprintf(buffer + used, buffer_size - used, "\n    %s", source_text);
    }
}

static char *copy_text(const char *start, size_t length) {
    char *copy = (char *) malloc(length + 1);

    if (copy == NULL) {
        return NULL;
    }

    if (length > 0) {
        (void) memcpy(copy, start, length);
    }
    copy[length] = '\0';
    return copy;
}

static bool append_token(LexedLine *line, Token token) {
    Token *new_tokens = (Token *) realloc(line->tokens, (size_t) (line->count + 1) * sizeof(Token));

    if (new_tokens == NULL) {
        return false;
    }

    line->tokens = new_tokens;
    line->tokens[line->count] = token;
    line->count += 1;
    return true;
}

static bool append_line(LexedProgram *program, LexedLine line) {
    LexedLine *new_lines = (LexedLine *) realloc(program->lines, (size_t) (program->count + 1) * sizeof(LexedLine));

    if (new_lines == NULL) {
        return false;
    }

    program->lines = new_lines;
    program->lines[program->count] = line;
    program->count += 1;
    return true;
}

static bool text_is_number(const char *text) {
    char *end = NULL;

    if (text == NULL || *text == '\0') {
        return false;
    }

    (void) strtod(text, &end);
    return end != text && *end == '\0';
}

static bool is_symbol_character(char ch) {
    return ch == '(' || ch == ')' || ch == ',' || ch == '.';
}

static char decode_escape(char escaped) {
    switch (escaped) {
        case 'n':
            return '\n';
        case 't':
            return '\t';
        case '"':
            return '"';
        case '\\':
            return '\\';
        default:
            return escaped;
    }
}

static char *strip_inline_comment(const char *line_text) {
    size_t index = 0;
    bool in_string = false;

    while (line_text[index] != '\0') {
        if (line_text[index] == '"' && (index == 0 || line_text[index - 1] != '\\')) {
            in_string = !in_string;
        } else if (!in_string && line_text[index] == '#') {
            while (index > 0 && isspace((unsigned char) line_text[index - 1])) {
                index -= 1;
            }
            return copy_text(line_text, index);
        }
        index += 1;
    }

    return copy_text(line_text, strlen(line_text));
}

static char *read_string_literal(const char *line_text, size_t *index, const char *file_path,
                                 int line_number, const char *source_text,
                                 char *error_message, size_t error_size) {
    size_t capacity = 16;
    size_t length = 0;
    char *buffer = (char *) malloc(capacity);

    if (buffer == NULL) {
        set_error_with_line(error_message, error_size, file_path, line_number, source_text,
                            "I ran out of memory while reading a string.");
        return NULL;
    }

    *index += 1;

    while (line_text[*index] != '\0') {
        char current = line_text[*index];

        if (current == '"') {
            buffer[length] = '\0';
            *index += 1;
            return buffer;
        }

        if (current == '\\' && line_text[*index + 1] != '\0') {
            current = decode_escape(line_text[*index + 1]);
            *index += 2;
        } else {
            *index += 1;
        }

        if (length + 1 >= capacity) {
            char *new_buffer;
            capacity *= 2;
            new_buffer = (char *) realloc(buffer, capacity);
            if (new_buffer == NULL) {
                free(buffer);
                set_error_with_line(error_message, error_size, file_path, line_number, source_text,
                                    "I ran out of memory while reading a string.");
                return NULL;
            }
            buffer = new_buffer;
        }

        buffer[length] = current;
        length += 1;
    }

    free(buffer);
    set_error_with_line(error_message, error_size, file_path, line_number, source_text,
                        "A string started with a quote but never ended. Add another \".");
    return NULL;
}

static bool append_simple_token(LexedLine *line, TokenType type, const char *text,
                                char *error_message, size_t error_size) {
    Token token;

    token.type = type;
    token.line = line->line_number;
    token.file_path = line->file_path;
    token.source_text = line->source_text;
    token.lexeme = copy_text(text, strlen(text));
    if (token.lexeme == NULL) {
        set_error_with_line(error_message, error_size, line->file_path, line->line_number, line->source_text,
                            "I ran out of memory while tokenizing this line.");
        return false;
    }

    if (!append_token(line, token)) {
        free(token.lexeme);
        set_error_with_line(error_message, error_size, line->file_path, line->line_number, line->source_text,
                            "I ran out of memory while tokenizing this line.");
        return false;
    }

    return true;
}

static bool tokenize_line(const char *line_text, LexedLine *line, char *error_message, size_t error_size) {
    size_t index = 0;

    while (line_text[index] != '\0') {
        Token token;
        size_t start;
        char *text;

        if (isspace((unsigned char) line_text[index])) {
            index += 1;
            continue;
        }

        if (line_text[index] == '"') {
            text = read_string_literal(line_text, &index, line->file_path, line->line_number,
                                       line->source_text, error_message, error_size);
            if (text == NULL) {
                return false;
            }

            token.type = TOKEN_STRING;
            token.line = line->line_number;
            token.file_path = line->file_path;
            token.source_text = line->source_text;
            token.lexeme = text;
            if (!append_token(line, token)) {
                free(text);
                set_error_with_line(error_message, error_size, line->file_path, line->line_number, line->source_text,
                                    "I ran out of memory while tokenizing this line.");
                return false;
            }
            continue;
        }

        if (is_symbol_character(line_text[index])) {
            char symbol_text[2];
            symbol_text[0] = line_text[index];
            symbol_text[1] = '\0';
            if (!append_simple_token(line, TOKEN_SYMBOL, symbol_text, error_message, error_size)) {
                return false;
            }
            index += 1;
            continue;
        }

        start = index;
        while (line_text[index] != '\0' &&
               !isspace((unsigned char) line_text[index]) &&
               !is_symbol_character(line_text[index])) {
            index += 1;
        }

        text = copy_text(line_text + start, index - start);
        if (text == NULL) {
            set_error_with_line(error_message, error_size, line->file_path, line->line_number, line->source_text,
                                "I ran out of memory while tokenizing this line.");
            return false;
        }

        token.type = text_is_number(text) ? TOKEN_NUMBER : TOKEN_WORD;
        token.line = line->line_number;
        token.file_path = line->file_path;
        token.source_text = line->source_text;
        token.lexeme = text;
        if (!append_token(line, token)) {
            free(text);
            set_error_with_line(error_message, error_size, line->file_path, line->line_number, line->source_text,
                                "I ran out of memory while tokenizing this line.");
            return false;
        }
    }

    return true;
}

LexedProgram *lex_source_named(const char *source, const char *file_path,
                               char *error_message, size_t error_size) {
    const char *line_start = source;
    const char *cursor = source;
    int line_number = 1;
    LexedProgram *program = (LexedProgram *) calloc(1, sizeof(LexedProgram));

    if (program == NULL) {
        set_error_with_line(error_message, error_size, file_path, 0, NULL,
                            "I could not allocate memory for the lexer.");
        return NULL;
    }

    while (1) {
        if (*cursor == '\n' || *cursor == '\0') {
            size_t length = (size_t) (cursor - line_start);
            char *raw_text;
            char *line_text;
            LexedLine line;

            if (length > 0 && line_start[length - 1] == '\r') {
                length -= 1;
            }

            raw_text = copy_text(line_start, length);
            if (raw_text == NULL) {
                free_lexed_program(program);
                set_error_with_line(error_message, error_size, file_path, line_number, NULL,
                                    "I ran out of memory while reading the source file.");
                return NULL;
            }

            line_text = strip_inline_comment(raw_text);
            if (line_text == NULL) {
                free(raw_text);
                free_lexed_program(program);
                set_error_with_line(error_message, error_size, file_path, line_number, NULL,
                                    "I ran out of memory while reading comments.");
                return NULL;
            }

            line.tokens = NULL;
            line.count = 0;
            line.line_number = line_number;
            line.source_text = raw_text;
            line.file_path = copy_text(file_path != NULL ? file_path : "<memory>",
                                       strlen(file_path != NULL ? file_path : "<memory>"));
            if (line.file_path == NULL) {
                free(raw_text);
                free(line_text);
                free_lexed_program(program);
                set_error_with_line(error_message, error_size, file_path, line_number, NULL,
                                    "I ran out of memory while storing source locations.");
                return NULL;
            }

            if (!tokenize_line(line_text, &line, error_message, error_size)) {
                int token_index;
                free(line_text);
                for (token_index = 0; token_index < line.count; token_index += 1) {
                    free(line.tokens[token_index].lexeme);
                }
                free(line.tokens);
                free(line.source_text);
                free(line.file_path);
                free_lexed_program(program);
                return NULL;
            }

            free(line_text);

            if (!append_line(program, line)) {
                int token_index;
                for (token_index = 0; token_index < line.count; token_index += 1) {
                    free(line.tokens[token_index].lexeme);
                }
                free(line.tokens);
                free(line.source_text);
                free(line.file_path);
                free_lexed_program(program);
                set_error_with_line(error_message, error_size, file_path, line_number, raw_text,
                                    "I ran out of memory while storing tokenized lines.");
                return NULL;
            }

            if (*cursor == '\0') {
                break;
            }

            line_start = cursor + 1;
            line_number += 1;
        }

        cursor += 1;
    }

    return program;
}

LexedProgram *lex_source(const char *source, char *error_message, size_t error_size) {
    return lex_source_named(source, "<memory>", error_message, error_size);
}

void free_lexed_program(LexedProgram *program) {
    int line_index;

    if (program == NULL) {
        return;
    }

    for (line_index = 0; line_index < program->count; line_index += 1) {
        int token_index;
        for (token_index = 0; token_index < program->lines[line_index].count; token_index += 1) {
            free(program->lines[line_index].tokens[token_index].lexeme);
        }
        free(program->lines[line_index].tokens);
        free(program->lines[line_index].source_text);
        free(program->lines[line_index].file_path);
    }

    free(program->lines);
    free(program);
}
