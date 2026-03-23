#include "files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *duplicate_text(const char *text) {
    size_t length = strlen(text);
    char *copy = (char *) malloc(length + 1);

    if (copy == NULL) {
        return NULL;
    }

    (void) memcpy(copy, text, length + 1);
    return copy;
}

static bool path_is_absolute(const char *path) {
    return path != NULL && path[0] == '/';
}

static char *join_paths(const char *left, const char *right) {
    size_t left_length = strlen(left);
    size_t right_length = strlen(right);
    bool need_separator = left_length > 0 && left[left_length - 1] != '/';
    char *joined = (char *) malloc(left_length + right_length + (need_separator ? 2U : 1U));

    if (joined == NULL) {
        return NULL;
    }

    (void) memcpy(joined, left, left_length);
    if (need_separator) {
        joined[left_length] = '/';
        left_length += 1;
    }
    (void) memcpy(joined + left_length, right, right_length + 1);
    return joined;
}

static bool append_component(char ***components, int *count, char *component) {
    char **new_components = (char **) realloc(*components, (size_t) (*count + 1) * sizeof(char *));

    if (new_components == NULL) {
        return false;
    }

    *components = new_components;
    (*components)[*count] = component;
    *count += 1;
    return true;
}

static void free_components(char **components, int count) {
    int index;

    for (index = 0; index < count; index += 1) {
        free(components[index]);
    }
    free(components);
}

static bool normalize_path_copy(const char *path, char **out_path) {
    char *path_copy = duplicate_text(path);
    char **components = NULL;
    int component_count = 0;
    bool absolute = path_is_absolute(path);
    char *part;
    size_t total_length;
    char *normalized;
    int index;

    if (path_copy == NULL) {
        return false;
    }

    part = strtok(path_copy, "/");
    while (part != NULL) {
        if (strcmp(part, ".") == 0 || part[0] == '\0') {
            part = strtok(NULL, "/");
            continue;
        }

        if (strcmp(part, "..") == 0) {
            if (component_count > 0 && strcmp(components[component_count - 1], "..") != 0) {
                free(components[component_count - 1]);
                component_count -= 1;
            } else if (!absolute) {
                char *copy = duplicate_text(part);
                if (copy == NULL || !append_component(&components, &component_count, copy)) {
                    free(copy);
                    free_components(components, component_count);
                    free(path_copy);
                    return false;
                }
            }
            part = strtok(NULL, "/");
            continue;
        }

        {
            char *copy = duplicate_text(part);
            if (copy == NULL || !append_component(&components, &component_count, copy)) {
                free(copy);
                free_components(components, component_count);
                free(path_copy);
                return false;
            }
        }

        part = strtok(NULL, "/");
    }

    total_length = absolute ? 1U : 0U;
    if (!absolute && component_count == 0) {
        total_length += 1U;
    }
    for (index = 0; index < component_count; index += 1) {
        total_length += strlen(components[index]) + (index > 0 ? 1U : 0U);
    }

    normalized = (char *) malloc(total_length + 1U);
    if (normalized == NULL) {
        free_components(components, component_count);
        free(path_copy);
        return false;
    }

    normalized[0] = '\0';
    if (absolute) {
        (void) strcpy(normalized, "/");
    } else if (component_count == 0) {
        (void) strcpy(normalized, ".");
    }

    for (index = 0; index < component_count; index += 1) {
        if ((absolute && index > 0) || (!absolute && normalized[0] != '\0' && strcmp(normalized, ".") != 0)) {
            (void) strcat(normalized, "/");
        } else if (!absolute && strcmp(normalized, ".") == 0) {
            normalized[0] = '\0';
        }
        (void) strcat(normalized, components[index]);
    }

    if (!absolute && normalized[0] == '\0') {
        (void) strcpy(normalized, ".");
    }

    free_components(components, component_count);
    free(path_copy);
    *out_path = normalized;
    return true;
}

bool read_text_file(const char *path, char **out_text, char *error_message, size_t error_size) {
    FILE *file = fopen(path, "rb");
    long length;
    size_t bytes_read;
    char *buffer;

    if (file == NULL) {
        (void) snprintf(error_message, error_size,
                        "Could not open '%s'. Make sure the file exists and can be read.",
                        path);
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        (void) snprintf(error_message, error_size, "Could not read the size of '%s'.", path);
        return false;
    }

    length = ftell(file);
    if (length < 0) {
        fclose(file);
        (void) snprintf(error_message, error_size, "Could not read the size of '%s'.", path);
        return false;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        (void) snprintf(error_message, error_size, "Could not rewind '%s'.", path);
        return false;
    }

    buffer = (char *) malloc((size_t) length + 1U);
    if (buffer == NULL) {
        fclose(file);
        (void) snprintf(error_message, error_size, "Not enough memory to read '%s'.", path);
        return false;
    }

    bytes_read = fread(buffer, 1, (size_t) length, file);
    fclose(file);
    if (bytes_read != (size_t) length) {
        free(buffer);
        (void) snprintf(error_message, error_size, "Could not read all of '%s'.", path);
        return false;
    }

    buffer[length] = '\0';
    *out_text = buffer;
    return true;
}

bool path_make_absolute_copy(const char *path, char **out_path, char *error_message, size_t error_size) {
    char current_directory[4096];
    char *joined;
    bool ok;

    if (path_is_absolute(path)) {
        if (!normalize_path_copy(path, out_path)) {
            (void) snprintf(error_message, error_size,
                            "Not enough memory to store the path '%s'.", path);
            return false;
        }
        return true;
    }

    if (getcwd(current_directory, sizeof(current_directory)) == NULL) {
        (void) snprintf(error_message, error_size,
                        "Could not figure out the current working directory.");
        return false;
    }

    joined = join_paths(current_directory, path);
    if (joined == NULL) {
        (void) snprintf(error_message, error_size,
                        "Not enough memory to store the path '%s'.", path);
        return false;
    }

    ok = normalize_path_copy(joined, out_path);
    free(joined);
    if (!ok) {
        (void) snprintf(error_message, error_size,
                        "Not enough memory to store the path '%s'.", path);
        return false;
    }

    return true;
}

bool path_resolve_relative_copy(const char *base_file_path, const char *path,
                                char **out_path, char *error_message, size_t error_size) {
    char *base_directory;
    char *joined;
    bool ok;
    const char *last_slash;

    if (path_is_absolute(path)) {
        return path_make_absolute_copy(path, out_path, error_message, error_size);
    }

    if (base_file_path == NULL) {
        return path_make_absolute_copy(path, out_path, error_message, error_size);
    }

    last_slash = strrchr(base_file_path, '/');
    if (last_slash == NULL) {
        return path_make_absolute_copy(path, out_path, error_message, error_size);
    }

    base_directory = (char *) malloc((size_t) (last_slash - base_file_path) + 1U);
    if (base_directory == NULL) {
        (void) snprintf(error_message, error_size,
                        "Not enough memory to build the import path '%s'.", path);
        return false;
    }

    (void) memcpy(base_directory, base_file_path, (size_t) (last_slash - base_file_path));
    base_directory[last_slash - base_file_path] = '\0';

    joined = join_paths(base_directory, path);
    free(base_directory);
    if (joined == NULL) {
        (void) snprintf(error_message, error_size,
                        "Not enough memory to build the import path '%s'.", path);
        return false;
    }

    ok = normalize_path_copy(joined, out_path);
    free(joined);
    if (!ok) {
        (void) snprintf(error_message, error_size,
                        "Not enough memory to build the import path '%s'.", path);
        return false;
    }

    return true;
}
