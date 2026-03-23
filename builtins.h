#ifndef ELANG_BUILTINS_H
#define ELANG_BUILTINS_H

#include "parser.h"
#include "runtime.h"

#include <stdbool.h>
#include <stddef.h>

bool builtins_is_name(const char *name);
bool builtins_execute_call(const Expression *expression,
                           Value *arguments,
                           int arg_count,
                           Value *out_value,
                           char *error_message,
                           size_t error_size);

#endif
