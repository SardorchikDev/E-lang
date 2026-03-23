#ifndef ELANG_ANALYZER_H
#define ELANG_ANALYZER_H

#include "parser.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct {
    SourceLocation location;
    char *message;
} AnalyzerWarning;

typedef struct {
    AnalyzerWarning *warnings;
    int count;
} AnalyzerReport;

bool analyze_program(const Program *program, AnalyzerReport *report, char *error_message, size_t error_size);
void print_analyzer_report(const AnalyzerReport *report, FILE *stream);
void free_analyzer_report(AnalyzerReport *report);

#endif
