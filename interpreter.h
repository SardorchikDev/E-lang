#ifndef ELANG_INTERPRETER_H
#define ELANG_INTERPRETER_H

#include "parser.h"
#include "runtime.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool trace;
} InterpreterOptions;

bool interpret_program(const Program *program,
                       Runtime *runtime,
                       const InterpreterOptions *options,
                       char *error_message,
                       size_t error_size);

#endif
