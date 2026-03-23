#include "formatter.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *text;
    size_t length;
    size_t capacity;
} StringBuilder;

static bool equals_ignore_case_prefix(const char *text, const char *prefix) {
    size_t index = 0;

    while (prefix[index] != '\0') {
        if (text[index] == '\0') {
            return false;
        }
        if (tolower((unsigned char) text[index]) != tolower((unsigned char) prefix[index])) {
            return false;
        }
        index += 1;
    }

    return true;
}

static bool is_word_boundary(char ch) {
    return ch == '\0' || isspace((unsigned char) ch) || ch == '#';
}

static bool builder_ensure_capacity(StringBuilder *builder, size_t extra) {
    size_t needed = builder->length + extra + 1U;
    size_t new_capacity;
    char *new_text;

    if (needed <= builder->capacity) {
        return true;
    }

    new_capacity = builder->capacity == 0 ? 128U : builder->capacity;
    while (new_capacity < needed) {
        new_capacity *= 2U;
    }

    new_text = (char *) realloc(builder->text, new_capacity);
    if (new_text == NULL) {
        return false;
    }

    builder->text = new_text;
    builder->capacity = new_capacity;
    return true;
}

static bool builder_append_text(StringBuilder *builder, const char *text) {
    size_t length = strlen(text);

    if (!builder_ensure_capacity(builder, length)) {
        return false;
    }

    (void) memcpy(builder->text + builder->length, text, length + 1U);
    builder->length += length;
    return true;
}

static bool builder_append_spaces(StringBuilder *builder, int count) {
    int index;

    if (!builder_ensure_capacity(builder, (size_t) count)) {
        return false;
    }

    for (index = 0; index < count; index += 1) {
        builder->text[builder->length] = ' ';
        builder->length += 1;
    }
    builder->text[builder->length] = '\0';
    return true;
}

static bool builder_append_char(StringBuilder *builder, char ch) {
    if (!builder_ensure_capacity(builder, 1U)) {
        return false;
    }

    builder->text[builder->length] = ch;
    builder->length += 1;
    builder->text[builder->length] = '\0';
    return true;
}

static char *trimmed_copy(const char *text, size_t length) {
    size_t start = 0;
    size_t end = length;
    char *copy;

    while (start < length && isspace((unsigned char) text[start])) {
        start += 1;
    }
    while (end > start && isspace((unsigned char) text[end - 1])) {
        end -= 1;
    }

    copy = (char *) malloc(end - start + 1U);
    if (copy == NULL) {
        return NULL;
    }

    if (end > start) {
        (void) memcpy(copy, text + start, end - start);
    }
    copy[end - start] = '\0';
    return copy;
}

static bool line_starts_block(const char *line) {
    if (equals_ignore_case_prefix(line, "if ") ||
        equals_ignore_case_prefix(line, "repeat ") ||
        equals_ignore_case_prefix(line, "while ") ||
        equals_ignore_case_prefix(line, "define function ") ||
        equals_ignore_case_prefix(line, "for each ")) {
        return true;
    }

    if (equals_ignore_case_prefix(line, "else if ") && strstr(line, " then") != NULL) {
        return true;
    }

    if (equals_ignore_case_prefix(line, "else") && is_word_boundary(line[4])) {
        return true;
    }

    return false;
}

static bool line_closes_before_indent(const char *line) {
    if (equals_ignore_case_prefix(line, "end") && is_word_boundary(line[3])) {
        return true;
    }

    if (equals_ignore_case_prefix(line, "else") && is_word_boundary(line[4])) {
        return true;
    }

    return false;
}

bool format_source_text(const char *source, char **out_text, char *error_message, size_t error_size) {
    const char *line_start = source;
    const char *cursor = source;
    StringBuilder builder;
    int indent_level = 0;

    builder.text = NULL;
    builder.length = 0;
    builder.capacity = 0;

    while (1) {
        if (*cursor == '\n' || *cursor == '\0') {
            size_t raw_length = (size_t) (cursor - line_start);
            char *trimmed;
            int line_indent = indent_level;

            if (raw_length > 0 && line_start[raw_length - 1] == '\r') {
                raw_length -= 1;
            }

            trimmed = trimmed_copy(line_start, raw_length);
            if (trimmed == NULL) {
                free(builder.text);
                (void) snprintf(error_message, error_size,
                                "I ran out of memory while formatting the source.");
                return false;
            }

            if (trimmed[0] != '\0' && line_closes_before_indent(trimmed)) {
                line_indent -= 1;
                if (line_indent < 0) {
                    line_indent = 0;
                }
            }

            if (trimmed[0] != '\0') {
                if (!builder_append_spaces(&builder, line_indent * 4) ||
                    !builder_append_text(&builder, trimmed)) {
                    free(trimmed);
                    free(builder.text);
                    (void) snprintf(error_message, error_size,
                                    "I ran out of memory while formatting the source.");
                    return false;
                }
            }

            if (!builder_append_char(&builder, '\n')) {
                free(trimmed);
                free(builder.text);
                (void) snprintf(error_message, error_size,
                                "I ran out of memory while formatting the source.");
                return false;
            }

            if (trimmed[0] != '\0' && line_starts_block(trimmed)) {
                indent_level = line_indent + 1;
            } else {
                indent_level = line_indent;
            }

            free(trimmed);

            if (*cursor == '\0') {
                break;
            }

            line_start = cursor + 1;
        }

        cursor += 1;
    }

    if (builder.text == NULL) {
        builder.text = (char *) malloc(1U);
        if (builder.text == NULL) {
            (void) snprintf(error_message, error_size,
                            "I ran out of memory while formatting the source.");
            return false;
        }
        builder.text[0] = '\0';
    }

    *out_text = builder.text;
    return true;
}
