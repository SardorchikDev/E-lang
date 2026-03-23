#ifndef ELANG_FORMATTER_H
#define ELANG_FORMATTER_H

#include <stdbool.h>
#include <stddef.h>

bool format_source_text(const char *source, char **out_text, char *error_message, size_t error_size);

#endif
