#ifndef ELANG_FILES_H
#define ELANG_FILES_H

#include <stdbool.h>
#include <stddef.h>

bool read_text_file(const char *path, char **out_text, char *error_message, size_t error_size);
bool path_make_absolute_copy(const char *path, char **out_path, char *error_message, size_t error_size);
bool path_resolve_relative_copy(const char *base_file_path, const char *path,
                                char **out_path, char *error_message, size_t error_size);

#endif
