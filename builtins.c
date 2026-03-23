#include "builtins.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

typedef struct {
    const char *name;
    int min_args;
    int max_args;
} BuiltinInfo;

static const BuiltinInfo BUILTIN_INFOS[] = {
    {"assert", 1, 2},
    {"assert_equal", 2, 3},
    {"length", 1, 1},
    {"item", 2, 2},
    {"append", 2, 2},
    {"set_item", 3, 3},
    {"insert_item", 3, 3},
    {"remove_item", 2, 2},
    {"slice", 3, 3},
    {"sort", 1, 1},
    {"to_number", 1, 1},
    {"to_text", 1, 1},
    {"type_of", 1, 1},
    {"lowercase", 1, 1},
    {"uppercase", 1, 1},
    {"trim", 1, 1},
    {"split", 2, 2},
    {"join", 2, 2},
    {"sqrt", 1, 1},
    {"random", 0, 0},
    {"random_between", 2, 2},
    {"round", 1, 1},
    {"floor", 1, 1},
    {"ceiling", 1, 1},
    {"absolute", 1, 1},
    {"minimum", 2, 2},
    {"maximum", 2, 2},
    {"read_file", 1, 1},
    {"write_file", 2, 2},
    {"append_file", 2, 2},
    {"file_exists", 1, 1},
    {"get_field", 2, 2},
    {"set_field", 3, 3},
    {"has_field", 2, 2},
    {"keys", 1, 1}
};

static void builtin_error(char *buffer, size_t buffer_size, SourceLocation location, const char *format, ...) {
    va_list args;
    size_t used;

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    if (location.file_path != NULL) {
        (void) snprintf(buffer, buffer_size, "%s:%d: ", location.file_path, location.line);
    } else {
        (void) snprintf(buffer, buffer_size, "Line %d: ", location.line);
    }

    used = strlen(buffer);
    va_start(args, format);
    (void) vsnprintf(buffer + used, buffer_size - used, format, args);
    va_end(args);

    if (location.source_text != NULL && location.source_text[0] != '\0') {
        used = strlen(buffer);
        (void) snprintf(buffer + used, buffer_size - used, "\n    %s", location.source_text);
    }
}

static bool value_to_index(const Value *value, int *out_index) {
    double number;

    if (!value_to_number(value, &number)) {
        return false;
    }
    if (fabs(number - round(number)) > 1e-9 || number < 1.0) {
        return false;
    }
    *out_index = (int) round(number);
    return true;
}

static bool make_text_value(const char *text, Value *out_value, SourceLocation location,
                            char *error_message, size_t error_size) {
    if (!value_make_string_copy(text, out_value)) {
        builtin_error(error_message, error_size, location,
                      "I ran out of memory while building text.");
        return false;
    }
    return true;
}

static bool split_text(const char *text, const char *separator, Value *out_value,
                       SourceLocation location, char *error_message, size_t error_size) {
    Value list = value_none();
    const char *start = text;
    const char *match;
    size_t separator_length = strlen(separator);

    if (!value_make_empty_list(&list)) {
        builtin_error(error_message, error_size, location,
                      "I ran out of memory while building a list.");
        return false;
    }

    if (separator_length == 0) {
        size_t index;
        for (index = 0; text[index] != '\0'; index += 1) {
            char temp[2];
            Value char_value = value_none();
            Value next_list = value_none();
            temp[0] = text[index];
            temp[1] = '\0';
            if (!value_make_string_copy(temp, &char_value) ||
                !value_list_append_copy(&list, &char_value, &next_list)) {
                value_destroy(&char_value);
                value_destroy(&list);
                builtin_error(error_message, error_size, location,
                              "I ran out of memory while splitting text.");
                return false;
            }
            value_destroy(&char_value);
            value_destroy(&list);
            list = next_list;
        }
        *out_value = list;
        return true;
    }

    while (1) {
        Value piece = value_none();
        Value next_list = value_none();
        char *chunk;
        size_t length;

        match = strstr(start, separator);
        length = match != NULL ? (size_t) (match - start) : strlen(start);
        chunk = (char *) malloc(length + 1);
        if (chunk == NULL) {
            value_destroy(&list);
            builtin_error(error_message, error_size, location,
                          "I ran out of memory while splitting text.");
            return false;
        }
        (void) memcpy(chunk, start, length);
        chunk[length] = '\0';
        piece = value_string_owned(chunk);
        if (!value_list_append_copy(&list, &piece, &next_list)) {
            value_destroy(&piece);
            value_destroy(&list);
            builtin_error(error_message, error_size, location,
                          "I ran out of memory while splitting text.");
            return false;
        }
        value_destroy(&piece);
        value_destroy(&list);
        list = next_list;

        if (match == NULL) {
            break;
        }
        start = match + separator_length;
    }

    *out_value = list;
    return true;
}

static bool join_list(const Value *list_value, const char *separator, Value *out_value,
                      SourceLocation location, char *error_message, size_t error_size) {
    int index;
    size_t total = 1;
    char **parts;
    char *joined;

    if (list_value->type != VALUE_LIST) {
        builtin_error(error_message, error_size, location,
                      "'join' needs a list as its first argument.");
        return false;
    }

    parts = (char **) calloc((size_t) value_list_length(list_value), sizeof(char *));
    if (parts == NULL) {
        builtin_error(error_message, error_size, location,
                      "I ran out of memory while joining text.");
        return false;
    }

    for (index = 0; index < value_list_length(list_value); index += 1) {
        Value item = value_none();
        if (!value_list_get(list_value, index + 1, &item) ||
            !value_to_text_copy(&item, &parts[index])) {
            int cleanup_index;
            value_destroy(&item);
            for (cleanup_index = 0; cleanup_index <= index; cleanup_index += 1) {
                free(parts[cleanup_index]);
            }
            free(parts);
            builtin_error(error_message, error_size, location,
                          "I ran out of memory while joining text.");
            return false;
        }
        total += strlen(parts[index]);
        if (index > 0) {
            total += strlen(separator);
        }
        value_destroy(&item);
    }

    joined = (char *) malloc(total);
    if (joined == NULL) {
        for (index = 0; index < value_list_length(list_value); index += 1) {
            free(parts[index]);
        }
        free(parts);
        builtin_error(error_message, error_size, location,
                      "I ran out of memory while joining text.");
        return false;
    }
    joined[0] = '\0';

    for (index = 0; index < value_list_length(list_value); index += 1) {
        if (index > 0) {
            (void) strcat(joined, separator);
        }
        (void) strcat(joined, parts[index]);
        free(parts[index]);
    }
    free(parts);

    *out_value = value_string_owned(joined);
    return true;
}

static bool read_entire_file(const char *path, char *error_message, size_t error_size,
                             SourceLocation location, char **out_text) {
    FILE *file = fopen(path, "rb");
    long length;
    size_t bytes_read;
    char *buffer;

    if (file == NULL) {
        builtin_error(error_message, error_size, location,
                      "Could not open '%s'.", path);
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        builtin_error(error_message, error_size, location,
                      "Could not read the size of '%s'.", path);
        return false;
    }
    length = ftell(file);
    if (length < 0) {
        fclose(file);
        builtin_error(error_message, error_size, location,
                      "Could not read the size of '%s'.", path);
        return false;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        builtin_error(error_message, error_size, location,
                      "Could not rewind '%s'.", path);
        return false;
    }
    buffer = (char *) malloc((size_t) length + 1);
    if (buffer == NULL) {
        fclose(file);
        builtin_error(error_message, error_size, location,
                      "I ran out of memory while reading '%s'.", path);
        return false;
    }
    bytes_read = fread(buffer, 1, (size_t) length, file);
    fclose(file);
    if (bytes_read != (size_t) length) {
        free(buffer);
        builtin_error(error_message, error_size, location,
                      "Could not read all of '%s'.", path);
        return false;
    }
    buffer[length] = '\0';
    *out_text = buffer;
    return true;
}

static bool write_text_file(const char *path, const char *text, const char *mode,
                            char *error_message, size_t error_size, SourceLocation location) {
    FILE *file = fopen(path, mode);
    if (file == NULL) {
        builtin_error(error_message, error_size, location,
                      "Could not open '%s' for writing.", path);
        return false;
    }
    if (fputs(text, file) == EOF) {
        fclose(file);
        builtin_error(error_message, error_size, location,
                      "Could not write to '%s'.", path);
        return false;
    }
    fclose(file);
    return true;
}

bool builtins_is_name(const char *name) {
    size_t index;

    for (index = 0; index < sizeof(BUILTIN_INFOS) / sizeof(BUILTIN_INFOS[0]); index += 1) {
        if (equals_ignore_case(name, BUILTIN_INFOS[index].name)) {
            return true;
        }
    }

    return false;
}

bool builtins_get_arity(const char *name, int *out_min_args, int *out_max_args) {
    size_t index;

    for (index = 0; index < sizeof(BUILTIN_INFOS) / sizeof(BUILTIN_INFOS[0]); index += 1) {
        if (equals_ignore_case(name, BUILTIN_INFOS[index].name)) {
            if (out_min_args != NULL) {
                *out_min_args = BUILTIN_INFOS[index].min_args;
            }
            if (out_max_args != NULL) {
                *out_max_args = BUILTIN_INFOS[index].max_args;
            }
            return true;
        }
    }

    return false;
}

bool builtins_execute_call(const Expression *expression,
                           Value *arguments,
                           int arg_count,
                           Value *out_value,
                           char *error_message,
                           size_t error_size) {
    const char *name = expression->as.call.name;
    SourceLocation location = expression->location;
    static bool random_seeded = false;

    if (equals_ignore_case(name, "assert")) {
        if (arg_count < 1 || arg_count > 2) {
            builtin_error(error_message, error_size, location,
                          "'assert' needs 1 or 2 arguments.");
            return false;
        }
        if (!value_is_truthy(&arguments[0])) {
            if (arg_count == 2) {
                char *message = NULL;
                if (!value_to_text_copy(&arguments[1], &message)) {
                    builtin_error(error_message, error_size, location,
                                  "Assertion failed.");
                    return false;
                }
                builtin_error(error_message, error_size, location, "Assertion failed: %s", message);
                free(message);
            } else {
                builtin_error(error_message, error_size, location, "Assertion failed.");
            }
            return false;
        }
        *out_value = value_boolean(true);
        return true;
    }

    if (equals_ignore_case(name, "assert_equal")) {
        bool equal = false;
        if (arg_count < 2 || arg_count > 3) {
            builtin_error(error_message, error_size, location,
                          "'assert_equal' needs 2 or 3 arguments.");
            return false;
        }
        if (!value_equals(&arguments[0], &arguments[1], &equal)) {
            builtin_error(error_message, error_size, location,
                          "I ran out of memory while comparing values for assert_equal.");
            return false;
        }
        if (!equal) {
            if (arg_count == 3) {
                char *message = NULL;
                (void) value_to_text_copy(&arguments[2], &message);
                builtin_error(error_message, error_size, location,
                              "assert_equal failed%s%s",
                              message != NULL ? ": " : "",
                              message != NULL ? message : "");
                free(message);
            } else {
                builtin_error(error_message, error_size, location,
                              "assert_equal failed.");
            }
            return false;
        }
        *out_value = value_boolean(true);
        return true;
    }

    if (equals_ignore_case(name, "length")) {
        if (arg_count != 1) {
            builtin_error(error_message, error_size, location,
                          "'length' needs exactly 1 argument.");
            return false;
        }
        if (arguments[0].type == VALUE_STRING) {
            *out_value = value_number((double) strlen(arguments[0].string != NULL ? arguments[0].string : ""));
            return true;
        }
        if (arguments[0].type == VALUE_LIST) {
            *out_value = value_number((double) value_list_length(&arguments[0]));
            return true;
        }
        if (arguments[0].type == VALUE_RECORD) {
            *out_value = value_number((double) value_record_length(&arguments[0]));
            return true;
        }
        if (arguments[0].type == VALUE_NONE) {
            *out_value = value_number(0.0);
            return true;
        }
        builtin_error(error_message, error_size, location,
                      "'length' works with text, lists, or records.");
        return false;
    }

    if (equals_ignore_case(name, "item")) {
        int index;
        if (arg_count != 2) {
            builtin_error(error_message, error_size, location,
                          "'item' needs exactly 2 arguments.");
            return false;
        }
        if (!value_to_index(&arguments[1], &index)) {
            builtin_error(error_message, error_size, location,
                          "The item position should be a whole number starting at 1.");
            return false;
        }
        if (arguments[0].type == VALUE_LIST) {
            if (!value_list_get(&arguments[0], index, out_value)) {
                builtin_error(error_message, error_size, location,
                              "That item position is outside the list.");
                return false;
            }
            return true;
        }
        if (arguments[0].type == VALUE_STRING) {
            size_t length = strlen(arguments[0].string != NULL ? arguments[0].string : "");
            char *text;
            if (index < 1 || (size_t) index > length) {
                builtin_error(error_message, error_size, location,
                              "That item position is outside the text.");
                return false;
            }
            text = (char *) malloc(2);
            if (text == NULL) {
                builtin_error(error_message, error_size, location,
                              "I ran out of memory while reading a character.");
                return false;
            }
            text[0] = arguments[0].string[index - 1];
            text[1] = '\0';
            *out_value = value_string_owned(text);
            return true;
        }
        builtin_error(error_message, error_size, location,
                      "'item' works with lists or text.");
        return false;
    }

    if (equals_ignore_case(name, "append")) {
        if (arg_count != 2) {
            builtin_error(error_message, error_size, location,
                          "'append' needs exactly 2 arguments.");
            return false;
        }
        if (!value_list_append_copy(&arguments[0], &arguments[1], out_value)) {
            builtin_error(error_message, error_size, location,
                          "'append' needs a list as its first argument.");
            return false;
        }
        return true;
    }

    if (equals_ignore_case(name, "set_item")) {
        int index;
        if (arg_count != 3) {
            builtin_error(error_message, error_size, location,
                          "'set_item' needs exactly 3 arguments.");
            return false;
        }
        if (!value_to_index(&arguments[1], &index) ||
            !value_list_set_copy(&arguments[0], index, &arguments[2], out_value)) {
            builtin_error(error_message, error_size, location,
                          "'set_item' needs a valid list and item position.");
            return false;
        }
        return true;
    }

    if (equals_ignore_case(name, "insert_item")) {
        int index;
        if (arg_count != 3) {
            builtin_error(error_message, error_size, location,
                          "'insert_item' needs exactly 3 arguments.");
            return false;
        }
        if (!value_to_index(&arguments[1], &index) ||
            !value_list_insert_copy(&arguments[0], index, &arguments[2], out_value)) {
            builtin_error(error_message, error_size, location,
                          "'insert_item' needs a valid list and insert position.");
            return false;
        }
        return true;
    }

    if (equals_ignore_case(name, "remove_item")) {
        int index;
        if (arg_count != 2) {
            builtin_error(error_message, error_size, location,
                          "'remove_item' needs exactly 2 arguments.");
            return false;
        }
        if (!value_to_index(&arguments[1], &index) ||
            !value_list_remove_copy(&arguments[0], index, out_value, NULL)) {
            builtin_error(error_message, error_size, location,
                          "'remove_item' needs a valid list and item position.");
            return false;
        }
        return true;
    }

    if (equals_ignore_case(name, "slice")) {
        int start_index;
        int end_index;
        if (arg_count != 3) {
            builtin_error(error_message, error_size, location,
                          "'slice' needs exactly 3 arguments.");
            return false;
        }
        if (!value_to_index(&arguments[1], &start_index) ||
            !value_to_index(&arguments[2], &end_index) ||
            !value_list_slice_copy(&arguments[0], start_index, end_index, out_value)) {
            builtin_error(error_message, error_size, location,
                          "'slice' needs a list and valid start/end positions.");
            return false;
        }
        return true;
    }

    if (equals_ignore_case(name, "sort")) {
        if (arg_count != 1) {
            builtin_error(error_message, error_size, location,
                          "'sort' needs exactly 1 argument.");
            return false;
        }
        if (!value_list_sort_copy(&arguments[0], out_value)) {
            builtin_error(error_message, error_size, location,
                          "'sort' needs a list.");
            return false;
        }
        return true;
    }

    if (equals_ignore_case(name, "to_number")) {
        double number;
        if (arg_count != 1) {
            builtin_error(error_message, error_size, location,
                          "'to_number' needs exactly 1 argument.");
            return false;
        }
        if (!value_to_number(&arguments[0], &number)) {
            builtin_error(error_message, error_size, location,
                          "I could not turn that value into a number.");
            return false;
        }
        *out_value = value_number(number);
        return true;
    }

    if (equals_ignore_case(name, "to_text")) {
        char *text = NULL;
        if (arg_count != 1) {
            builtin_error(error_message, error_size, location,
                          "'to_text' needs exactly 1 argument.");
            return false;
        }
        if (!value_to_text_copy(&arguments[0], &text)) {
            builtin_error(error_message, error_size, location,
                          "I ran out of memory while turning a value into text.");
            return false;
        }
        *out_value = value_string_owned(text);
        return true;
    }

    if (equals_ignore_case(name, "type_of")) {
        if (arg_count != 1) {
            builtin_error(error_message, error_size, location,
                          "'type_of' needs exactly 1 argument.");
            return false;
        }
        return make_text_value(value_type_name(&arguments[0]), out_value, location, error_message, error_size);
    }

    if (equals_ignore_case(name, "lowercase") || equals_ignore_case(name, "uppercase") || equals_ignore_case(name, "trim")) {
        char *text = NULL;
        size_t index;
        if (arg_count != 1) {
            builtin_error(error_message, error_size, location,
                          "'%s' needs exactly 1 argument.", name);
            return false;
        }
        if (!value_to_text_copy(&arguments[0], &text)) {
            builtin_error(error_message, error_size, location,
                          "I ran out of memory while changing text.");
            return false;
        }
        if (equals_ignore_case(name, "trim")) {
            char *start = text;
            char *end = text + strlen(text);
            while (*start != '\0' && isspace((unsigned char) *start)) {
                start += 1;
            }
            while (end > start && isspace((unsigned char) end[-1])) {
                end -= 1;
            }
            memmove(text, start, (size_t) (end - start));
            text[end - start] = '\0';
        } else {
            for (index = 0; text[index] != '\0'; index += 1) {
                text[index] = equals_ignore_case(name, "lowercase")
                    ? (char) tolower((unsigned char) text[index])
                    : (char) toupper((unsigned char) text[index]);
            }
        }
        *out_value = value_string_owned(text);
        return true;
    }

    if (equals_ignore_case(name, "split")) {
        char *text = NULL;
        char *separator = NULL;
        bool ok;
        if (arg_count != 2) {
            builtin_error(error_message, error_size, location,
                          "'split' needs exactly 2 arguments.");
            return false;
        }
        if (!value_to_text_copy(&arguments[0], &text) || !value_to_text_copy(&arguments[1], &separator)) {
            free(text);
            free(separator);
            builtin_error(error_message, error_size, location,
                          "I ran out of memory while splitting text.");
            return false;
        }
        ok = split_text(text, separator, out_value, location, error_message, error_size);
        free(text);
        free(separator);
        return ok;
    }

    if (equals_ignore_case(name, "join")) {
        char *separator = NULL;
        bool ok;
        if (arg_count != 2) {
            builtin_error(error_message, error_size, location,
                          "'join' needs exactly 2 arguments.");
            return false;
        }
        if (!value_to_text_copy(&arguments[1], &separator)) {
            builtin_error(error_message, error_size, location,
                          "I ran out of memory while joining text.");
            return false;
        }
        ok = join_list(&arguments[0], separator, out_value, location, error_message, error_size);
        free(separator);
        return ok;
    }

    if (equals_ignore_case(name, "sqrt")) {
        double number;
        if (arg_count != 1) {
            builtin_error(error_message, error_size, location,
                          "'sqrt' needs exactly 1 argument.");
            return false;
        }
        if (!value_to_number(&arguments[0], &number) || number < 0.0) {
            builtin_error(error_message, error_size, location,
                          "'sqrt' needs a non-negative number.");
            return false;
        }
        *out_value = value_number(sqrt(number));
        return true;
    }

    if (equals_ignore_case(name, "round") ||
        equals_ignore_case(name, "floor") ||
        equals_ignore_case(name, "ceiling") ||
        equals_ignore_case(name, "absolute")) {
        double number;

        if (arg_count != 1) {
            builtin_error(error_message, error_size, location,
                          "'%s' needs exactly 1 argument.", name);
            return false;
        }
        if (!value_to_number(&arguments[0], &number)) {
            builtin_error(error_message, error_size, location,
                          "'%s' needs a number.", name);
            return false;
        }

        if (equals_ignore_case(name, "round")) {
            *out_value = value_number(round(number));
        } else if (equals_ignore_case(name, "floor")) {
            *out_value = value_number(floor(number));
        } else if (equals_ignore_case(name, "ceiling")) {
            *out_value = value_number(ceil(number));
        } else {
            *out_value = value_number(fabs(number));
        }
        return true;
    }

    if (equals_ignore_case(name, "minimum") || equals_ignore_case(name, "maximum")) {
        double left_number;
        double right_number;

        if (arg_count != 2) {
            builtin_error(error_message, error_size, location,
                          "'%s' needs exactly 2 arguments.", name);
            return false;
        }
        if (!value_to_number(&arguments[0], &left_number) ||
            !value_to_number(&arguments[1], &right_number)) {
            builtin_error(error_message, error_size, location,
                          "'%s' needs numbers.", name);
            return false;
        }

        *out_value = value_number(equals_ignore_case(name, "minimum")
                                      ? (left_number < right_number ? left_number : right_number)
                                      : (left_number > right_number ? left_number : right_number));
        return true;
    }

    if (equals_ignore_case(name, "random")) {
        if (arg_count != 0) {
            builtin_error(error_message, error_size, location,
                          "'random' does not take any arguments.");
            return false;
        }
        if (!random_seeded) {
            srand((unsigned int) time(NULL));
            random_seeded = true;
        }
        *out_value = value_number((double) rand() / (double) RAND_MAX);
        return true;
    }

    if (equals_ignore_case(name, "random_between")) {
        double start_number;
        double end_number;
        long start_value;
        long end_value;
        long span;
        long result;

        if (arg_count != 2) {
            builtin_error(error_message, error_size, location,
                          "'random_between' needs exactly 2 arguments.");
            return false;
        }
        if (!value_to_number(&arguments[0], &start_number) ||
            !value_to_number(&arguments[1], &end_number) ||
            fabs(start_number - round(start_number)) > 1e-9 ||
            fabs(end_number - round(end_number)) > 1e-9) {
            builtin_error(error_message, error_size, location,
                          "'random_between' needs whole numbers.");
            return false;
        }

        start_value = (long) round(start_number);
        end_value = (long) round(end_number);
        if (start_value > end_value) {
            builtin_error(error_message, error_size, location,
                          "'random_between' needs the first number to be less than or equal to the second.");
            return false;
        }

        if (!random_seeded) {
            srand((unsigned int) time(NULL));
            random_seeded = true;
        }

        span = end_value - start_value + 1L;
        result = start_value + (long) (rand() % span);
        *out_value = value_number((double) result);
        return true;
    }

    if (equals_ignore_case(name, "read_file")) {
        char *path = NULL;
        char *contents = NULL;
        bool ok;
        if (arg_count != 1) {
            builtin_error(error_message, error_size, location,
                          "'read_file' needs exactly 1 argument.");
            return false;
        }
        if (!value_to_text_copy(&arguments[0], &path)) {
            builtin_error(error_message, error_size, location,
                          "I ran out of memory while reading a file path.");
            return false;
        }
        ok = read_entire_file(path, error_message, error_size, location, &contents);
        free(path);
        if (!ok) {
            return false;
        }
        *out_value = value_string_owned(contents);
        return true;
    }

    if (equals_ignore_case(name, "write_file") || equals_ignore_case(name, "append_file")) {
        char *path = NULL;
        char *text = NULL;
        bool ok;
        if (arg_count != 2) {
            builtin_error(error_message, error_size, location,
                          "'%s' needs exactly 2 arguments.", name);
            return false;
        }
        if (!value_to_text_copy(&arguments[0], &path) || !value_to_text_copy(&arguments[1], &text)) {
            free(path);
            free(text);
            builtin_error(error_message, error_size, location,
                          "I ran out of memory while preparing file output.");
            return false;
        }
        ok = write_text_file(path, text,
                             equals_ignore_case(name, "append_file") ? "a" : "w",
                             error_message, error_size, location);
        free(path);
        free(text);
        if (!ok) {
            return false;
        }
        *out_value = value_boolean(true);
        return true;
    }

    if (equals_ignore_case(name, "file_exists")) {
        char *path = NULL;
        FILE *file;
        if (arg_count != 1) {
            builtin_error(error_message, error_size, location,
                          "'file_exists' needs exactly 1 argument.");
            return false;
        }
        if (!value_to_text_copy(&arguments[0], &path)) {
            builtin_error(error_message, error_size, location,
                          "I ran out of memory while reading a file path.");
            return false;
        }
        file = fopen(path, "rb");
        *out_value = value_boolean(file != NULL);
        if (file != NULL) {
            fclose(file);
        }
        free(path);
        return true;
    }

    if (equals_ignore_case(name, "get_field")) {
        char *key = NULL;
        bool ok;
        if (arg_count != 2) {
            builtin_error(error_message, error_size, location,
                          "'get_field' needs exactly 2 arguments.");
            return false;
        }
        if (!value_to_text_copy(&arguments[1], &key)) {
            builtin_error(error_message, error_size, location,
                          "I ran out of memory while reading a record field name.");
            return false;
        }
        ok = value_record_get(&arguments[0], key, out_value);
        free(key);
        if (!ok) {
            builtin_error(error_message, error_size, location,
                          "'get_field' needs a record and an existing field name.");
            return false;
        }
        return true;
    }

    if (equals_ignore_case(name, "set_field")) {
        char *key = NULL;
        bool ok;
        if (arg_count != 3) {
            builtin_error(error_message, error_size, location,
                          "'set_field' needs exactly 3 arguments.");
            return false;
        }
        if (!value_to_text_copy(&arguments[1], &key)) {
            builtin_error(error_message, error_size, location,
                          "I ran out of memory while reading a record field name.");
            return false;
        }
        ok = value_record_set_copy(&arguments[0], key, &arguments[2], out_value);
        free(key);
        if (!ok) {
            builtin_error(error_message, error_size, location,
                          "'set_field' needs a record as its first argument.");
            return false;
        }
        return true;
    }

    if (equals_ignore_case(name, "has_field")) {
        char *key = NULL;
        if (arg_count != 2) {
            builtin_error(error_message, error_size, location,
                          "'has_field' needs exactly 2 arguments.");
            return false;
        }
        if (!value_to_text_copy(&arguments[1], &key)) {
            builtin_error(error_message, error_size, location,
                          "I ran out of memory while reading a record field name.");
            return false;
        }
        *out_value = value_boolean(value_record_has(&arguments[0], key));
        free(key);
        return true;
    }

    if (equals_ignore_case(name, "keys")) {
        if (arg_count != 1) {
            builtin_error(error_message, error_size, location,
                          "'keys' needs exactly 1 argument.");
            return false;
        }
        if (!value_record_keys(&arguments[0], out_value)) {
            builtin_error(error_message, error_size, location,
                          "'keys' needs a record.");
            return false;
        }
        return true;
    }

    builtin_error(error_message, error_size, location,
                  "I do not know the built-in function '%s'.", name);
    return false;
}
