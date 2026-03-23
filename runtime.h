#ifndef ELANG_RUNTIME_H
#define ELANG_RUNTIME_H

#include <stdbool.h>
#include <stdio.h>

struct Statement;

typedef struct List List;
typedef struct Record Record;

typedef enum {
    VALUE_NONE,
    VALUE_NUMBER,
    VALUE_STRING,
    VALUE_BOOLEAN,
    VALUE_LIST,
    VALUE_RECORD
} ValueType;

typedef struct {
    ValueType type;
    double number;
    bool boolean;
    char *string;
    List *list;
    Record *record;
} Value;

typedef struct Runtime Runtime;

Runtime *runtime_create(void);
void runtime_destroy(Runtime *runtime);

bool runtime_push_scope(Runtime *runtime);
bool runtime_pop_scope(Runtime *runtime);

Value value_none(void);
Value value_number(double number);
Value value_boolean(bool boolean);
Value value_string_owned(char *text);
bool value_make_string_copy(const char *text, Value *out_value);
bool value_make_empty_list(Value *out_value);
bool value_make_empty_record(Value *out_value);
bool value_copy(const Value *source, Value *out_value);
void value_destroy(Value *value);
void value_fprint(FILE *stream, const Value *value);
bool value_to_text_copy(const Value *value, char **out_text);
bool value_try_parse_number(const char *text, double *out_number);
bool value_try_parse_boolean(const char *text, bool *out_boolean);
bool value_to_number(const Value *value, double *out_number);
bool value_is_truthy(const Value *value);
bool value_equals(const Value *left, const Value *right, bool *out_equal);
const char *value_type_name(const Value *value);

bool value_list_append_copy(const Value *list_value, const Value *item, Value *out_value);
bool value_list_get(const Value *list_value, int one_based_index, Value *out_value);
bool value_list_set_copy(const Value *list_value, int one_based_index, const Value *item, Value *out_value);
bool value_list_insert_copy(const Value *list_value, int one_based_index, const Value *item, Value *out_value);
bool value_list_remove_copy(const Value *list_value, int one_based_index, Value *out_value, Value *removed_item);
bool value_list_slice_copy(const Value *list_value, int start_index, int end_index, Value *out_value);
bool value_list_sort_copy(const Value *list_value, Value *out_value);
int value_list_length(const Value *list_value);

bool value_record_get(const Value *record_value, const char *key, Value *out_value);
bool value_record_set_copy(const Value *record_value, const char *key, const Value *item, Value *out_value);
bool value_record_has(const Value *record_value, const char *key);
bool value_record_keys(const Value *record_value, Value *out_value);
int value_record_length(const Value *record_value);

/*
 * runtime_define_variable and runtime_assign_variable take ownership of value on success.
 * If they return false, the caller still owns value and must clean it up.
 */
bool runtime_define_variable(Runtime *runtime, const char *name, Value value);
bool runtime_assign_variable(Runtime *runtime, const char *name, Value value);
bool runtime_get_variable(const Runtime *runtime, const char *name, Value *out_value);
bool runtime_define_function(Runtime *runtime, const char *name, const struct Statement *function_statement);
const struct Statement *runtime_get_function(const Runtime *runtime, const char *name);

#endif
