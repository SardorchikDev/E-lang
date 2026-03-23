#ifndef ELANG_PARSER_H
#define ELANG_PARSER_H

#include "lexer.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    int line;
    const char *file_path;
    const char *source_text;
} SourceLocation;

typedef enum {
    EXPR_NUMBER,
    EXPR_STRING,
    EXPR_BOOLEAN,
    EXPR_VARIABLE,
    EXPR_LIST,
    EXPR_RECORD,
    EXPR_GROUPING,
    EXPR_UNARY,
    EXPR_BINARY,
    EXPR_COMPARISON,
    EXPR_CALL
} ExpressionType;

typedef enum {
    UNARY_NEGATE,
    UNARY_NOT
} UnaryOperator;

typedef enum {
    BIN_ADD,
    BIN_SUBTRACT,
    BIN_MULTIPLY,
    BIN_DIVIDE,
    BIN_MODULO,
    BIN_POWER,
    BIN_AND,
    BIN_OR
} BinaryOperator;

typedef enum {
    CMP_GREATER_THAN,
    CMP_LESS_THAN,
    CMP_EQUAL,
    CMP_NOT_EQUAL,
    CMP_AT_LEAST,
    CMP_AT_MOST,
    CMP_CONTAINS
} ComparisonOperator;

typedef struct Expression Expression;

struct Expression {
    SourceLocation location;
    ExpressionType type;
    union {
        double number;
        bool boolean;
        char *string;
        char *variable_name;
        struct {
            Expression **items;
            int item_count;
        } list;
        struct {
            char **keys;
            Expression **values;
            int count;
        } record;
        struct {
            Expression *inner;
        } grouping;
        struct {
            UnaryOperator op;
            Expression *right;
        } unary;
        struct {
            BinaryOperator op;
            Expression *left;
            Expression *right;
        } binary;
        struct {
            ComparisonOperator op;
            Expression *left;
            Expression *right;
        } comparison;
        struct {
            char *name;
            Expression **arguments;
            int arg_count;
        } call;
    } as;
};

typedef enum {
    STMT_USE,
    STMT_LET,
    STMT_SET,
    STMT_SAY,
    STMT_ASK,
    STMT_IF,
    STMT_REPEAT,
    STMT_WHILE,
    STMT_FOR_EACH,
    STMT_FUNCTION,
    STMT_CALL,
    STMT_RETURN,
    STMT_BREAK,
    STMT_CONTINUE
} StatementType;

typedef struct Statement Statement;

struct Statement {
    SourceLocation location;
    StatementType type;
    union {
        struct {
            char *path;
        } use_stmt;
        struct {
            char *name;
            Expression *value;
        } let_stmt;
        struct {
            char *name;
            Expression *value;
        } set_stmt;
        struct {
            Expression *value;
        } say_stmt;
        struct {
            Expression *prompt;
            char *name;
        } ask_stmt;
        struct {
            Expression *condition;
            Statement **then_body;
            int then_count;
            Statement **else_body;
            int else_count;
        } if_stmt;
        struct {
            Expression *count;
            Statement **body;
            int body_count;
        } repeat_stmt;
        struct {
            Expression *condition;
            Statement **body;
            int body_count;
        } while_stmt;
        struct {
            char *item_name;
            Expression *collection;
            Statement **body;
            int body_count;
        } for_each_stmt;
        struct {
            char *name;
            char **parameters;
            int param_count;
            Statement **body;
            int body_count;
        } function_stmt;
        struct {
            Expression *call;
        } call_stmt;
        struct {
            Expression *value;
        } return_stmt;
    } as;
};

typedef struct {
    Statement **statements;
    int count;
} Program;

Program *parse_program(const LexedProgram *lexed_program, char *error_message, size_t error_size);
void free_program(Program *program);

#endif
