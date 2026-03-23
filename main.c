#include "analyzer.h"
#include "dump.h"
#include "files.h"
#include "formatter.h"
#include "interpreter.h"
#include "lexer.h"
#include "parser.h"
#include "runtime.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    char *path;
    char *source;
    LexedProgram *lexed_program;
    Program *program;
} LoadedProgram;

static char *duplicate_text(const char *text) {
    size_t length = strlen(text);
    char *copy = (char *) malloc(length + 1U);

    if (copy == NULL) {
        return NULL;
    }

    (void) memcpy(copy, text, length + 1U);
    return copy;
}

static void free_loaded_program(LoadedProgram *loaded) {
    if (loaded == NULL) {
        return;
    }

    free_program(loaded->program);
    free_lexed_program(loaded->lexed_program);
    free(loaded->source);
    free(loaded->path);

    loaded->path = NULL;
    loaded->source = NULL;
    loaded->lexed_program = NULL;
    loaded->program = NULL;
}

static bool parse_source_text(const char *source,
                              const char *file_path,
                              LoadedProgram *out_program,
                              char *error_message,
                              size_t error_size) {
    out_program->path = file_path != NULL ? duplicate_text(file_path) : NULL;
    out_program->source = duplicate_text(source);
    out_program->lexed_program = NULL;
    out_program->program = NULL;

    if ((file_path != NULL && out_program->path == NULL) || out_program->source == NULL) {
        free_loaded_program(out_program);
        (void) snprintf(error_message, error_size, "Not enough memory to store the source text.");
        return false;
    }

    out_program->lexed_program = lex_source_named(out_program->source,
                                                  file_path != NULL ? file_path : "<memory>",
                                                  error_message,
                                                  error_size);
    if (out_program->lexed_program == NULL) {
        free_loaded_program(out_program);
        return false;
    }

    out_program->program = parse_program(out_program->lexed_program, error_message, error_size);
    if (out_program->program == NULL) {
        free_loaded_program(out_program);
        return false;
    }

    return true;
}

static bool load_program_from_file(const char *path,
                                   LoadedProgram *out_program,
                                   char *error_message,
                                   size_t error_size) {
    char *absolute_path = NULL;
    char *source = NULL;
    bool ok;

    out_program->path = NULL;
    out_program->source = NULL;
    out_program->lexed_program = NULL;
    out_program->program = NULL;

    if (!path_make_absolute_copy(path, &absolute_path, error_message, error_size)) {
        return false;
    }

    if (!read_text_file(absolute_path, &source, error_message, error_size)) {
        free(absolute_path);
        return false;
    }

    ok = parse_source_text(source, absolute_path, out_program, error_message, error_size);
    free(source);
    free(absolute_path);
    return ok;
}

static bool run_loaded_program(const LoadedProgram *loaded,
                               Runtime *runtime,
                               bool trace,
                               char *error_message,
                               size_t error_size) {
    InterpreterOptions options;

    options.trace = trace;
    return interpret_program(loaded->program, runtime, &options, error_message, error_size);
}

static bool run_file(const char *path, bool trace, char *error_message, size_t error_size) {
    LoadedProgram loaded;
    Runtime *runtime;
    bool ok;

    if (!load_program_from_file(path, &loaded, error_message, error_size)) {
        return false;
    }

    runtime = runtime_create();
    if (runtime == NULL) {
        free_loaded_program(&loaded);
        (void) snprintf(error_message, error_size,
                        "Could not allocate memory for the E-Lang runtime.");
        return false;
    }

    ok = run_loaded_program(&loaded, runtime, trace, error_message, error_size);
    runtime_destroy(runtime);
    free_loaded_program(&loaded);
    return ok;
}

static bool path_has_elang_extension(const char *path) {
    size_t length = strlen(path);
    return length >= 6 && strcmp(path + length - 6, ".elang") == 0;
}

static void print_usage(const char *program_name) {
    (void) fprintf(stderr, "Usage:\n");
    (void) fprintf(stderr, "  %s <file.elang>\n", program_name);
    (void) fprintf(stderr, "  %s --trace <file.elang>\n", program_name);
    (void) fprintf(stderr, "  %s --tokens <file.elang>\n", program_name);
    (void) fprintf(stderr, "  %s --ast <file.elang>\n", program_name);
    (void) fprintf(stderr, "  %s --lint <file.elang>\n", program_name);
    (void) fprintf(stderr, "  %s --format <file.elang>\n", program_name);
    (void) fprintf(stderr, "  %s --repl\n", program_name);
    (void) fprintf(stderr, "  %s --test [file-or-directory]\n", program_name);
}

static bool command_is_exit(const char *line) {
    return strcmp(line, ":quit") == 0 || strcmp(line, ":exit") == 0;
}

static bool starts_with_keyword(const char *line, const char *keyword) {
    size_t index = 0;

    while (keyword[index] != '\0') {
        char left = line[index];
        char right = keyword[index];
        if (left == '\0') {
            return false;
        }
        if (left >= 'A' && left <= 'Z') {
            left = (char) (left - 'A' + 'a');
        }
        if (right >= 'A' && right <= 'Z') {
            right = (char) (right - 'A' + 'a');
        }
        if (left != right) {
            return false;
        }
        index += 1;
    }

    return line[index] == '\0' || line[index] == ' ' || line[index] == '\t';
}

static bool command_is_block_open(const char *line) {
    return starts_with_keyword(line, "if") ||
           starts_with_keyword(line, "repeat") ||
           starts_with_keyword(line, "while") ||
           starts_with_keyword(line, "for each") ||
           starts_with_keyword(line, "define function");
}

static bool command_is_end(const char *line) {
    return starts_with_keyword(line, "end");
}

static bool command_is_else(const char *line) {
    return starts_with_keyword(line, "else");
}

static char *trim_line(char *line) {
    char *start = line;
    char *end;

    while (*start != '\0' && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')) {
        start += 1;
    }

    end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        end -= 1;
    }
    *end = '\0';
    return start;
}

static bool append_text(char **buffer, size_t *length, size_t *capacity, const char *text) {
    size_t text_length = strlen(text);
    size_t needed = *length + text_length + 1U;
    char *new_buffer;

    if (needed > *capacity) {
        size_t new_capacity = *capacity == 0 ? 128U : *capacity;
        while (new_capacity < needed) {
            new_capacity *= 2U;
        }
        new_buffer = (char *) realloc(*buffer, new_capacity);
        if (new_buffer == NULL) {
            return false;
        }
        *buffer = new_buffer;
        *capacity = new_capacity;
    }

    (void) memcpy(*buffer + *length, text, text_length + 1U);
    *length += text_length;
    return true;
}

static int run_repl(bool trace) {
    Runtime *runtime = runtime_create();
    char error_message[1024];
    char line_buffer[2048];
    char *source_buffer = NULL;
    size_t source_length = 0;
    size_t source_capacity = 0;
    int block_depth = 0;

    if (runtime == NULL) {
        (void) fprintf(stderr, "Could not allocate memory for the E-Lang runtime.\n");
        return 1;
    }

    (void) fprintf(stderr, "E-Lang REPL. Type :quit to leave.\n");

    while (1) {
        LoadedProgram loaded;
        char *trimmed;
        bool should_run;

        (void) fprintf(stdout, "%s", block_depth > 0 ? "... " : "e-lang> ");
        (void) fflush(stdout);

        if (fgets(line_buffer, sizeof(line_buffer), stdin) == NULL) {
            (void) fputc('\n', stdout);
            break;
        }

        trimmed = trim_line(line_buffer);
        if (source_length == 0 && command_is_exit(trimmed)) {
            break;
        }
        if (source_length == 0 && strcmp(trimmed, ":help") == 0) {
            (void) fprintf(stdout, "Commands: :help, :quit, :exit\n");
            continue;
        }

        if (!append_text(&source_buffer, &source_length, &source_capacity, trimmed) ||
            !append_text(&source_buffer, &source_length, &source_capacity, "\n")) {
            (void) fprintf(stderr, "Not enough memory while collecting REPL input.\n");
            runtime_destroy(runtime);
            free(source_buffer);
            return 1;
        }

        if (trimmed[0] != '\0') {
            if (command_is_end(trimmed)) {
                if (block_depth > 0) {
                    block_depth -= 1;
                }
            } else if (!command_is_else(trimmed) && command_is_block_open(trimmed)) {
                block_depth += 1;
            }
        }

        should_run = block_depth == 0 && trimmed[0] != '\0';
        if (!should_run) {
            continue;
        }

        if (!parse_source_text(source_buffer, "<repl>", &loaded, error_message, sizeof(error_message))) {
            (void) fprintf(stderr, "%s\n", error_message);
        } else if (!run_loaded_program(&loaded, runtime, trace, error_message, sizeof(error_message))) {
            (void) fprintf(stderr, "%s\n", error_message);
        }

        free_loaded_program(&loaded);
        source_length = 0;
        if (source_buffer != NULL) {
            source_buffer[0] = '\0';
        }
    }

    runtime_destroy(runtime);
    free(source_buffer);
    return 0;
}

static int compare_text_ptrs(const void *left, const void *right) {
    const char *const *left_text = (const char *const *) left;
    const char *const *right_text = (const char *const *) right;
    return strcmp(*left_text, *right_text);
}

static bool path_is_directory(const char *path) {
    struct stat status;

    if (stat(path, &status) != 0) {
        return false;
    }

    return S_ISDIR(status.st_mode);
}

static int run_test_target(const char *path, bool trace, char *error_message, size_t error_size);

static int run_test_directory(const char *path, bool trace, char *error_message, size_t error_size) {
    DIR *directory = opendir(path);
    struct dirent *entry;
    char **entries = NULL;
    int entry_count = 0;
    int index;
    int failures = 0;

    if (directory == NULL) {
        (void) snprintf(error_message, error_size,
                        "Could not open the test directory '%s'.", path);
        return -1;
    }

    while ((entry = readdir(directory)) != NULL) {
        char *full_path;
        size_t path_length;

        if (entry->d_name[0] == '.' || entry->d_name[0] == '_') {
            continue;
        }
        if (!path_has_elang_extension(entry->d_name)) {
            continue;
        }

        path_length = strlen(path) + strlen(entry->d_name) + 2U;
        full_path = (char *) malloc(path_length);
        if (full_path == NULL) {
            closedir(directory);
            (void) snprintf(error_message, error_size,
                            "Not enough memory while collecting test files.");
            for (index = 0; index < entry_count; index += 1) {
                free(entries[index]);
            }
            free(entries);
            return -1;
        }

        (void) snprintf(full_path, path_length, "%s/%s", path, entry->d_name);
        {
            char **new_entries = (char **) realloc(entries, (size_t) (entry_count + 1) * sizeof(char *));
            if (new_entries == NULL) {
                free(full_path);
                closedir(directory);
                (void) snprintf(error_message, error_size,
                                "Not enough memory while collecting test files.");
                for (index = 0; index < entry_count; index += 1) {
                    free(entries[index]);
                }
                free(entries);
                return -1;
            }
            entries = new_entries;
            entries[entry_count] = full_path;
            entry_count += 1;
        }
    }

    closedir(directory);
    qsort(entries, (size_t) entry_count, sizeof(char *), compare_text_ptrs);

    for (index = 0; index < entry_count; index += 1) {
        int result = run_test_target(entries[index], trace, error_message, error_size);
        if (result != 0) {
            failures += 1;
        }
        free(entries[index]);
    }
    free(entries);

    return failures;
}

static int run_test_target(const char *path, bool trace, char *error_message, size_t error_size) {
    if (path_is_directory(path)) {
        return run_test_directory(path, trace, error_message, error_size);
    }

    if (!run_file(path, trace, error_message, error_size)) {
        (void) fprintf(stderr, "FAIL %s\n%s\n", path, error_message);
        return 1;
    }

    (void) fprintf(stdout, "PASS %s\n", path);
    return 0;
}

int main(int argc, char **argv) {
    char error_message[1024];
    bool trace = false;
    int arg_index = 1;

    while (arg_index < argc && strcmp(argv[arg_index], "--trace") == 0) {
        trace = true;
        arg_index += 1;
    }

    if (arg_index >= argc) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[arg_index], "--help") == 0 || strcmp(argv[arg_index], "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[arg_index], "--repl") == 0) {
        if (arg_index + 1 != argc) {
            print_usage(argv[0]);
            return 1;
        }
        return run_repl(trace);
    }

    if (strcmp(argv[arg_index], "--tokens") == 0 ||
        strcmp(argv[arg_index], "--ast") == 0 ||
        strcmp(argv[arg_index], "--lint") == 0 ||
        strcmp(argv[arg_index], "--format") == 0) {
        LoadedProgram loaded;

        if (arg_index + 2 != argc) {
            print_usage(argv[0]);
            return 1;
        }

        if (strcmp(argv[arg_index], "--format") == 0) {
            char *absolute_path = NULL;
            char *source = NULL;
            char *formatted = NULL;

            if (!path_make_absolute_copy(argv[arg_index + 1], &absolute_path, error_message, sizeof(error_message)) ||
                !read_text_file(absolute_path, &source, error_message, sizeof(error_message)) ||
                !format_source_text(source, &formatted, error_message, sizeof(error_message))) {
                free(absolute_path);
                free(source);
                free(formatted);
                (void) fprintf(stderr, "%s\n", error_message);
                return 1;
            }

            (void) fputs(formatted, stdout);
            free(absolute_path);
            free(source);
            free(formatted);
            return 0;
        }

        if (!load_program_from_file(argv[arg_index + 1], &loaded, error_message, sizeof(error_message))) {
            (void) fprintf(stderr, "%s\n", error_message);
            return 1;
        }

        if (strcmp(argv[arg_index], "--tokens") == 0) {
            dump_tokens(loaded.lexed_program, stdout);
        } else if (strcmp(argv[arg_index], "--ast") == 0) {
            dump_ast(loaded.program, stdout);
        } else {
            AnalyzerReport report;
            if (!analyze_program(loaded.program, &report, error_message, sizeof(error_message))) {
                free_loaded_program(&loaded);
                (void) fprintf(stderr, "%s\n", error_message);
                return 1;
            }
            print_analyzer_report(&report, stdout);
            free_analyzer_report(&report);
        }

        free_loaded_program(&loaded);
        return 0;
    }

    if (strcmp(argv[arg_index], "--test") == 0) {
        const char *target = arg_index + 1 < argc ? argv[arg_index + 1] : "tests";
        int failures;

        if (arg_index + 2 < argc) {
            print_usage(argv[0]);
            return 1;
        }

        failures = run_test_target(target, trace, error_message, sizeof(error_message));
        if (failures < 0) {
            (void) fprintf(stderr, "%s\n", error_message);
            return 1;
        }
        if (failures > 0) {
            (void) fprintf(stderr, "%d test file(s) failed.\n", failures);
            return 1;
        }

        (void) fprintf(stdout, "All test files passed.\n");
        return 0;
    }

    if (argv[arg_index][0] == '-') {
        print_usage(argv[0]);
        return 1;
    }

    if (arg_index + 1 != argc) {
        print_usage(argv[0]);
        return 1;
    }

    if (!run_file(argv[arg_index], trace, error_message, sizeof(error_message))) {
        (void) fprintf(stderr, "%s\n", error_message);
        return 1;
    }

    return 0;
}
