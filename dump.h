#ifndef ELANG_DUMP_H
#define ELANG_DUMP_H

#include "lexer.h"
#include "parser.h"

#include <stdio.h>

void dump_tokens(const LexedProgram *program, FILE *stream);
void dump_ast(const Program *program, FILE *stream);

#endif
