#ifndef ELANG_LEXER_H
#define ELANG_LEXER_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    TOKEN_WORD,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_SYMBOL
} TokenType;

typedef struct {
    TokenType type;
    char *lexeme;
    int line;
    const char *file_path;
    const char *source_text;
} Token;

typedef struct {
    Token *tokens;
    int count;
    int line_number;
    char *source_text;
    char *file_path;
} LexedLine;

typedef struct {
    LexedLine *lines;
    int count;
} LexedProgram;

LexedProgram *lex_source_named(const char *source, const char *file_path,
                               char *error_message, size_t error_size);
LexedProgram *lex_source(const char *source, char *error_message, size_t error_size);
void free_lexed_program(LexedProgram *program);

#endif
