#include "runtime.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    Value value;
} VariableSlot;

typedef struct {
    VariableSlot *slots;
    int capacity;
    int count;
} Scope;

typedef struct {
    char *name;
    const struct Statement *function_statement;
} FunctionSlot;

typedef struct {
    char *key;
    Value value;
} RecordSlot;

struct List {
    Value *items;
    int count;
};

struct Record {
    RecordSlot *slots;
    int capacity;
    int count;
};

struct Runtime {
    Scope *scopes;
    int scope_count;
    FunctionSlot *functions;
    int function_capacity;
    int function_count;
};

typedef struct {
    char *text;
    size_t length;
    size_t capacity;
} StringBuilder;

static bool value_copy_into(const Value *source, Value *out_value);
static void list_destroy(List *list);
static bool list_copy(const List *source, List **out_list);
static void record_destroy(Record *record);
static bool record_copy(const Record *source, Record **out_record);

static unsigned long hash_text_case_sensitive(const char *text) {
    unsigned long hash = 5381;

    while (*text != '\0') {
        hash = ((hash << 5) + hash) + (unsigned char) *text;
        text += 1;
    }

    return hash;
}

static unsigned long hash_text_case_insensitive(const char *text) {
    unsigned long hash = 5381;

    while (*text != '\0') {
        hash = ((hash << 5) + hash) + (unsigned char) tolower((unsigned char) *text);
        text += 1;
    }

    return hash;
}

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

static char *copy_text(const char *text) {
    size_t length = strlen(text);
    char *copy = (char *) malloc(length + 1);

    if (copy == NULL) {
        return NULL;
    }

    (void) memcpy(copy, text, length + 1);
    return copy;
}

static char *normalize_name(const char *name) {
    size_t length = strlen(name);
    size_t index;
    char *normalized = (char *) malloc(length + 1);

    if (normalized == NULL) {
        return NULL;
    }

    for (index = 0; index < length; index += 1) {
        normalized[index] = (char) tolower((unsigned char) name[index]);
    }
    normalized[length] = '\0';
    return normalized;
}

static bool builder_ensure_capacity(StringBuilder *builder, size_t extra) {
    size_t needed = builder->length + extra + 1;
    size_t new_capacity;
    char *new_text;

    if (needed <= builder->capacity) {
        return true;
    }

    new_capacity = builder->capacity == 0 ? 32 : builder->capacity;
    while (new_capacity < needed) {
        new_capacity *= 2;
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

    (void) memcpy(builder->text + builder->length, text, length + 1);
    builder->length += length;
    return true;
}

static bool builder_append_char(StringBuilder *builder, char ch) {
    if (!builder_ensure_capacity(builder, 1)) {
        return false;
    }

    builder->text[builder->length] = ch;
    builder->length += 1;
    builder->text[builder->length] = '\0';
    return true;
}

static bool append_value_text(StringBuilder *builder, const Value *value);

static List *list_create(void) {
    return (List *) calloc(1, sizeof(List));
}

static bool list_append_owned(List *list, Value item) {
    Value *new_items = (Value *) realloc(list->items, (size_t) (list->count + 1) * sizeof(Value));

    if (new_items == NULL) {
        return false;
    }

    list->items = new_items;
    list->items[list->count] = item;
    list->count += 1;
    return true;
}

static bool list_copy(const List *source, List **out_list) {
    int index;
    List *copy = list_create();

    if (copy == NULL) {
        return false;
    }

    for (index = 0; index < source->count; index += 1) {
        Value item_copy = value_none();
        if (!value_copy_into(&source->items[index], &item_copy)) {
            list_destroy(copy);
            return false;
        }
        if (!list_append_owned(copy, item_copy)) {
            value_destroy(&item_copy);
            list_destroy(copy);
            return false;
        }
    }

    *out_list = copy;
    return true;
}

static void list_destroy(List *list) {
    int index;

    if (list == NULL) {
        return;
    }

    for (index = 0; index < list->count; index += 1) {
        value_destroy(&list->items[index]);
    }

    free(list->items);
    free(list);
}

static Record *record_create(void) {
    Record *record = (Record *) calloc(1, sizeof(Record));
    if (record == NULL) {
        return NULL;
    }
    record->capacity = 8;
    record->slots = (RecordSlot *) calloc((size_t) record->capacity, sizeof(RecordSlot));
    if (record->slots == NULL) {
        free(record);
        return NULL;
    }
    return record;
}

static bool record_ensure_capacity(Record *record) {
    int old_capacity;
    RecordSlot *old_slots;
    int index;

    if ((record->count + 1) * 10 < record->capacity * 7) {
        return true;
    }

    old_capacity = record->capacity;
    old_slots = record->slots;
    record->capacity *= 2;
    record->slots = (RecordSlot *) calloc((size_t) record->capacity, sizeof(RecordSlot));
    if (record->slots == NULL) {
        record->capacity = old_capacity;
        record->slots = old_slots;
        return false;
    }
    record->count = 0;

    for (index = 0; index < old_capacity; index += 1) {
        if (old_slots[index].key != NULL) {
            size_t slot = hash_text_case_sensitive(old_slots[index].key) % (unsigned long) record->capacity;
            while (record->slots[slot].key != NULL) {
                slot = (slot + 1U) % (unsigned long) record->capacity;
            }
            record->slots[slot] = old_slots[index];
            record->count += 1;
        }
    }

    free(old_slots);
    return true;
}

static RecordSlot *record_find_slot(Record *record, const char *key) {
    size_t slot = hash_text_case_sensitive(key) % (unsigned long) record->capacity;

    while (record->slots[slot].key != NULL) {
        if (strcmp(record->slots[slot].key, key) == 0) {
            return &record->slots[slot];
        }
        slot = (slot + 1U) % (unsigned long) record->capacity;
    }

    return &record->slots[slot];
}

static bool record_set_owned(Record *record, const char *key, Value value) {
    RecordSlot *slot;

    if (!record_ensure_capacity(record)) {
        return false;
    }

    slot = record_find_slot(record, key);
    if (slot->key != NULL) {
        value_destroy(&slot->value);
        slot->value = value;
        return true;
    }

    slot->key = copy_text(key);
    if (slot->key == NULL) {
        return false;
    }
    slot->value = value;
    record->count += 1;
    return true;
}

static bool record_copy(const Record *source, Record **out_record) {
    int index;
    Record *copy = record_create();

    if (copy == NULL) {
        return false;
    }

    for (index = 0; index < source->capacity; index += 1) {
        if (source->slots[index].key != NULL) {
            Value value_copy = value_none();
            if (!value_copy_into(&source->slots[index].value, &value_copy)) {
                record_destroy(copy);
                return false;
            }
            if (!record_set_owned(copy, source->slots[index].key, value_copy)) {
                value_destroy(&value_copy);
                record_destroy(copy);
                return false;
            }
        }
    }

    *out_record = copy;
    return true;
}

static void record_destroy(Record *record) {
    int index;

    if (record == NULL) {
        return;
    }

    for (index = 0; index < record->capacity; index += 1) {
        if (record->slots[index].key != NULL) {
            free(record->slots[index].key);
            value_destroy(&record->slots[index].value);
        }
    }

    free(record->slots);
    free(record);
}

static bool append_value_text(StringBuilder *builder, const Value *value) {
    switch (value->type) {
        case VALUE_NONE:
            return builder_append_text(builder, "nothing");

        case VALUE_NUMBER: {
            char buffer[64];
            int written = snprintf(buffer, sizeof(buffer), "%.15g", value->number);
            if (written < 0) {
                return false;
            }
            return builder_append_text(builder, buffer);
        }

        case VALUE_STRING:
            return builder_append_text(builder, value->string != NULL ? value->string : "");

        case VALUE_BOOLEAN:
            return builder_append_text(builder, value->boolean ? "true" : "false");

        case VALUE_LIST: {
            int index;
            if (!builder_append_char(builder, '[')) {
                return false;
            }
            for (index = 0; index < value->list->count; index += 1) {
                if (index > 0 && !builder_append_text(builder, ", ")) {
                    return false;
                }
                if (!append_value_text(builder, &value->list->items[index])) {
                    return false;
                }
            }
            return builder_append_char(builder, ']');
        }

        case VALUE_RECORD: {
            int index;
            bool first = true;
            if (!builder_append_char(builder, '{')) {
                return false;
            }
            for (index = 0; index < value->record->capacity; index += 1) {
                if (value->record->slots[index].key == NULL) {
                    continue;
                }
                if (!first && !builder_append_text(builder, ", ")) {
                    return false;
                }
                if (!builder_append_text(builder, value->record->slots[index].key) ||
                    !builder_append_text(builder, ": ")) {
                    return false;
                }
                if (!append_value_text(builder, &value->record->slots[index].value)) {
                    return false;
                }
                first = false;
            }
            return builder_append_char(builder, '}');
        }
    }

    return false;
}

static bool variable_table_ensure_capacity(VariableSlot **slots_ptr, int *capacity_ptr, int count) {
    VariableSlot *old_slots = *slots_ptr;
    int old_capacity = *capacity_ptr;
    int new_capacity;
    int index;
    VariableSlot *new_slots;

    if ((count + 1) * 10 < (*capacity_ptr) * 7) {
        return true;
    }

    new_capacity = *capacity_ptr == 0 ? 8 : (*capacity_ptr) * 2;
    new_slots = (VariableSlot *) calloc((size_t) new_capacity, sizeof(VariableSlot));
    if (new_slots == NULL) {
        return false;
    }

    for (index = 0; index < old_capacity; index += 1) {
        if (old_slots[index].name != NULL) {
            size_t slot = hash_text_case_insensitive(old_slots[index].name) % (unsigned long) new_capacity;
            while (new_slots[slot].name != NULL) {
                slot = (slot + 1U) % (unsigned long) new_capacity;
            }
            new_slots[slot] = old_slots[index];
        }
    }

    free(old_slots);
    *slots_ptr = new_slots;
    *capacity_ptr = new_capacity;
    return true;
}

static VariableSlot *scope_find_slot(Scope *scope, const char *normalized_name) {
    size_t slot;

    if (scope->capacity == 0) {
        return NULL;
    }

    slot = hash_text_case_insensitive(normalized_name) % (unsigned long) scope->capacity;
    while (scope->slots[slot].name != NULL) {
        if (equals_ignore_case(scope->slots[slot].name, normalized_name)) {
            return &scope->slots[slot];
        }
        slot = (slot + 1U) % (unsigned long) scope->capacity;
    }

    return &scope->slots[slot];
}

static bool function_table_ensure_capacity(Runtime *runtime) {
    FunctionSlot *old_slots = runtime->functions;
    int old_capacity = runtime->function_capacity;
    int index;

    if ((runtime->function_count + 1) * 10 < runtime->function_capacity * 7) {
        return true;
    }

    runtime->function_capacity = runtime->function_capacity == 0 ? 8 : runtime->function_capacity * 2;
    runtime->functions = (FunctionSlot *) calloc((size_t) runtime->function_capacity, sizeof(FunctionSlot));
    if (runtime->functions == NULL) {
        runtime->functions = old_slots;
        runtime->function_capacity = old_capacity;
        return false;
    }

    for (index = 0; index < old_capacity; index += 1) {
        if (old_slots[index].name != NULL) {
            size_t slot = hash_text_case_insensitive(old_slots[index].name) %
                          (unsigned long) runtime->function_capacity;
            while (runtime->functions[slot].name != NULL) {
                slot = (slot + 1U) % (unsigned long) runtime->function_capacity;
            }
            runtime->functions[slot] = old_slots[index];
        }
    }

    free(old_slots);
    return true;
}

static FunctionSlot *function_find_slot(Runtime *runtime, const char *normalized_name) {
    size_t slot;

    if (runtime->function_capacity == 0) {
        return NULL;
    }

    slot = hash_text_case_insensitive(normalized_name) % (unsigned long) runtime->function_capacity;
    while (runtime->functions[slot].name != NULL) {
        if (equals_ignore_case(runtime->functions[slot].name, normalized_name)) {
            return &runtime->functions[slot];
        }
        slot = (slot + 1U) % (unsigned long) runtime->function_capacity;
    }

    return &runtime->functions[slot];
}

Runtime *runtime_create(void) {
    Runtime *runtime = (Runtime *) calloc(1, sizeof(Runtime));

    if (runtime == NULL) {
        return NULL;
    }

    if (!runtime_push_scope(runtime)) {
        runtime_destroy(runtime);
        return NULL;
    }

    return runtime;
}

bool runtime_push_scope(Runtime *runtime) {
    Scope *new_scopes = (Scope *) realloc(runtime->scopes, (size_t) (runtime->scope_count + 1) * sizeof(Scope));

    if (new_scopes == NULL) {
        return false;
    }

    runtime->scopes = new_scopes;
    runtime->scopes[runtime->scope_count].slots = NULL;
    runtime->scopes[runtime->scope_count].capacity = 0;
    runtime->scopes[runtime->scope_count].count = 0;
    runtime->scope_count += 1;
    return true;
}

bool runtime_pop_scope(Runtime *runtime) {
    Scope *scope;
    int index;

    if (runtime->scope_count <= 1) {
        return false;
    }

    scope = &runtime->scopes[runtime->scope_count - 1];
    for (index = 0; index < scope->capacity; index += 1) {
        if (scope->slots[index].name != NULL) {
            free(scope->slots[index].name);
            value_destroy(&scope->slots[index].value);
        }
    }
    free(scope->slots);
    runtime->scope_count -= 1;
    return true;
}

Value value_none(void) {
    Value value;
    value.type = VALUE_NONE;
    value.number = 0.0;
    value.boolean = false;
    value.string = NULL;
    value.list = NULL;
    value.record = NULL;
    return value;
}

Value value_number(double number) {
    Value value = value_none();
    value.type = VALUE_NUMBER;
    value.number = number;
    return value;
}

Value value_boolean(bool boolean) {
    Value value = value_none();
    value.type = VALUE_BOOLEAN;
    value.boolean = boolean;
    return value;
}

Value value_string_owned(char *text) {
    Value value = value_none();
    value.type = VALUE_STRING;
    value.string = text;
    return value;
}

bool value_make_string_copy(const char *text, Value *out_value) {
    char *copy = copy_text(text);
    if (copy == NULL) {
        return false;
    }
    *out_value = value_string_owned(copy);
    return true;
}

bool value_make_empty_list(Value *out_value) {
    List *list = list_create();
    if (list == NULL) {
        return false;
    }
    *out_value = value_none();
    out_value->type = VALUE_LIST;
    out_value->list = list;
    return true;
}

bool value_make_empty_record(Value *out_value) {
    Record *record = record_create();
    if (record == NULL) {
        return false;
    }
    *out_value = value_none();
    out_value->type = VALUE_RECORD;
    out_value->record = record;
    return true;
}

static bool value_copy_into(const Value *source, Value *out_value) {
    switch (source->type) {
        case VALUE_NONE:
            *out_value = value_none();
            return true;
        case VALUE_NUMBER:
            *out_value = value_number(source->number);
            return true;
        case VALUE_STRING:
            return value_make_string_copy(source->string != NULL ? source->string : "", out_value);
        case VALUE_BOOLEAN:
            *out_value = value_boolean(source->boolean);
            return true;
        case VALUE_LIST: {
            List *copy = NULL;
            if (!list_copy(source->list, &copy)) {
                return false;
            }
            *out_value = value_none();
            out_value->type = VALUE_LIST;
            out_value->list = copy;
            return true;
        }
        case VALUE_RECORD: {
            Record *copy = NULL;
            if (!record_copy(source->record, &copy)) {
                return false;
            }
            *out_value = value_none();
            out_value->type = VALUE_RECORD;
            out_value->record = copy;
            return true;
        }
    }

    return false;
}

bool value_copy(const Value *source, Value *out_value) {
    return value_copy_into(source, out_value);
}

void value_destroy(Value *value) {
    if (value == NULL) {
        return;
    }

    if (value->type == VALUE_STRING) {
        free(value->string);
    } else if (value->type == VALUE_LIST) {
        list_destroy(value->list);
    } else if (value->type == VALUE_RECORD) {
        record_destroy(value->record);
    }

    *value = value_none();
}

void value_fprint(FILE *stream, const Value *value) {
    char *text = NULL;

    if (!value_to_text_copy(value, &text)) {
        (void) fputs("<value>", stream);
        return;
    }

    (void) fputs(text, stream);
    free(text);
}

bool value_to_text_copy(const Value *value, char **out_text) {
    StringBuilder builder;

    builder.text = NULL;
    builder.length = 0;
    builder.capacity = 0;

    if (!append_value_text(&builder, value)) {
        free(builder.text);
        return false;
    }

    if (builder.text == NULL) {
        builder.text = copy_text("");
        if (builder.text == NULL) {
            return false;
        }
    }

    *out_text = builder.text;
    return true;
}

bool value_try_parse_number(const char *text, double *out_number) {
    char *end = NULL;

    while (*text != '\0' && isspace((unsigned char) *text)) {
        text += 1;
    }
    if (*text == '\0') {
        return false;
    }

    *out_number = strtod(text, &end);
    if (end == text) {
        return false;
    }
    while (*end != '\0' && isspace((unsigned char) *end)) {
        end += 1;
    }
    return *end == '\0';
}

bool value_try_parse_boolean(const char *text, bool *out_boolean) {
    while (*text != '\0' && isspace((unsigned char) *text)) {
        text += 1;
    }
    if (equals_ignore_case(text, "true")) {
        *out_boolean = true;
        return true;
    }
    if (equals_ignore_case(text, "false")) {
        *out_boolean = false;
        return true;
    }
    return false;
}

bool value_to_number(const Value *value, double *out_number) {
    switch (value->type) {
        case VALUE_NUMBER:
            *out_number = value->number;
            return true;
        case VALUE_BOOLEAN:
            *out_number = value->boolean ? 1.0 : 0.0;
            return true;
        case VALUE_STRING:
            return value_try_parse_number(value->string != NULL ? value->string : "", out_number);
        case VALUE_NONE:
        case VALUE_LIST:
        case VALUE_RECORD:
            return false;
    }

    return false;
}

bool value_is_truthy(const Value *value) {
    switch (value->type) {
        case VALUE_NONE:
            return false;
        case VALUE_NUMBER:
            return value->number != 0.0;
        case VALUE_STRING:
            return value->string != NULL && value->string[0] != '\0';
        case VALUE_BOOLEAN:
            return value->boolean;
        case VALUE_LIST:
            return value->list != NULL && value->list->count > 0;
        case VALUE_RECORD:
            return value->record != NULL && value->record->count > 0;
    }

    return false;
}

bool value_equals(const Value *left, const Value *right, bool *out_equal) {
    int index;

    if (left->type != right->type) {
        if ((left->type == VALUE_NUMBER || left->type == VALUE_BOOLEAN || left->type == VALUE_STRING) &&
            (right->type == VALUE_NUMBER || right->type == VALUE_BOOLEAN || right->type == VALUE_STRING)) {
            char *left_text = NULL;
            char *right_text = NULL;
            if (!value_to_text_copy(left, &left_text)) {
                return false;
            }
            if (!value_to_text_copy(right, &right_text)) {
                free(left_text);
                return false;
            }
            *out_equal = strcmp(left_text, right_text) == 0;
            free(left_text);
            free(right_text);
            return true;
        }
        *out_equal = false;
        return true;
    }

    switch (left->type) {
        case VALUE_NONE:
            *out_equal = true;
            return true;
        case VALUE_NUMBER:
            *out_equal = left->number == right->number;
            return true;
        case VALUE_STRING:
            *out_equal = strcmp(left->string != NULL ? left->string : "",
                                right->string != NULL ? right->string : "") == 0;
            return true;
        case VALUE_BOOLEAN:
            *out_equal = left->boolean == right->boolean;
            return true;
        case VALUE_LIST:
            if (left->list->count != right->list->count) {
                *out_equal = false;
                return true;
            }
            for (index = 0; index < left->list->count; index += 1) {
                bool item_equal = false;
                if (!value_equals(&left->list->items[index], &right->list->items[index], &item_equal)) {
                    return false;
                }
                if (!item_equal) {
                    *out_equal = false;
                    return true;
                }
            }
            *out_equal = true;
            return true;
        case VALUE_RECORD:
            if (left->record->count != right->record->count) {
                *out_equal = false;
                return true;
            }
            for (index = 0; index < left->record->capacity; index += 1) {
                if (left->record->slots[index].key != NULL) {
                    bool field_equal = false;
                    Value right_value = value_none();
                    if (!value_record_get(right, left->record->slots[index].key, &right_value)) {
                        *out_equal = false;
                        return true;
                    }
                    if (!value_equals(&left->record->slots[index].value, &right_value, &field_equal)) {
                        value_destroy(&right_value);
                        return false;
                    }
                    value_destroy(&right_value);
                    if (!field_equal) {
                        *out_equal = false;
                        return true;
                    }
                }
            }
            *out_equal = true;
            return true;
    }

    return false;
}

const char *value_type_name(const Value *value) {
    switch (value->type) {
        case VALUE_NONE:
            return "nothing";
        case VALUE_NUMBER:
            return "number";
        case VALUE_STRING:
            return "text";
        case VALUE_BOOLEAN:
            return "boolean";
        case VALUE_LIST:
            return "list";
        case VALUE_RECORD:
            return "record";
    }

    return "unknown";
}

int value_list_length(const Value *list_value) {
    if (list_value->type != VALUE_LIST || list_value->list == NULL) {
        return -1;
    }
    return list_value->list->count;
}

bool value_list_get(const Value *list_value, int one_based_index, Value *out_value) {
    int zero_based_index = one_based_index - 1;
    if (list_value->type != VALUE_LIST || list_value->list == NULL) {
        return false;
    }
    if (zero_based_index < 0 || zero_based_index >= list_value->list->count) {
        return false;
    }
    return value_copy_into(&list_value->list->items[zero_based_index], out_value);
}

bool value_list_append_copy(const Value *list_value, const Value *item, Value *out_value) {
    List *copy;
    Value item_copy = value_none();

    if (list_value->type != VALUE_LIST || list_value->list == NULL) {
        return false;
    }
    if (!list_copy(list_value->list, &copy)) {
        return false;
    }
    if (!value_copy_into(item, &item_copy)) {
        list_destroy(copy);
        return false;
    }
    if (!list_append_owned(copy, item_copy)) {
        value_destroy(&item_copy);
        list_destroy(copy);
        return false;
    }
    *out_value = value_none();
    out_value->type = VALUE_LIST;
    out_value->list = copy;
    return true;
}

bool value_list_set_copy(const Value *list_value, int one_based_index, const Value *item, Value *out_value) {
    int zero_based_index = one_based_index - 1;
    List *copy;

    if (list_value->type != VALUE_LIST || list_value->list == NULL) {
        return false;
    }
    if (zero_based_index < 0 || zero_based_index >= list_value->list->count) {
        return false;
    }
    if (!list_copy(list_value->list, &copy)) {
        return false;
    }
    value_destroy(&copy->items[zero_based_index]);
    if (!value_copy_into(item, &copy->items[zero_based_index])) {
        list_destroy(copy);
        return false;
    }
    *out_value = value_none();
    out_value->type = VALUE_LIST;
    out_value->list = copy;
    return true;
}

bool value_list_insert_copy(const Value *list_value, int one_based_index, const Value *item, Value *out_value) {
    int zero_based_index = one_based_index - 1;
    List *copy;
    Value item_copy = value_none();
    Value *new_items;
    int index;

    if (list_value->type != VALUE_LIST || list_value->list == NULL) {
        return false;
    }
    if (zero_based_index < 0 || zero_based_index > list_value->list->count) {
        return false;
    }
    if (!list_copy(list_value->list, &copy)) {
        return false;
    }
    if (!value_copy_into(item, &item_copy)) {
        list_destroy(copy);
        return false;
    }
    new_items = (Value *) realloc(copy->items, (size_t) (copy->count + 1) * sizeof(Value));
    if (new_items == NULL) {
        value_destroy(&item_copy);
        list_destroy(copy);
        return false;
    }
    copy->items = new_items;
    for (index = copy->count; index > zero_based_index; index -= 1) {
        copy->items[index] = copy->items[index - 1];
    }
    copy->items[zero_based_index] = item_copy;
    copy->count += 1;
    *out_value = value_none();
    out_value->type = VALUE_LIST;
    out_value->list = copy;
    return true;
}

bool value_list_remove_copy(const Value *list_value, int one_based_index, Value *out_value, Value *removed_item) {
    int zero_based_index = one_based_index - 1;
    List *copy;
    int index;

    if (removed_item != NULL) {
        *removed_item = value_none();
    }
    if (list_value->type != VALUE_LIST || list_value->list == NULL) {
        return false;
    }
    if (zero_based_index < 0 || zero_based_index >= list_value->list->count) {
        return false;
    }
    if (!list_copy(list_value->list, &copy)) {
        return false;
    }
    if (removed_item != NULL && !value_copy_into(&copy->items[zero_based_index], removed_item)) {
        list_destroy(copy);
        return false;
    }
    value_destroy(&copy->items[zero_based_index]);
    for (index = zero_based_index; index < copy->count - 1; index += 1) {
        copy->items[index] = copy->items[index + 1];
    }
    copy->count -= 1;
    *out_value = value_none();
    out_value->type = VALUE_LIST;
    out_value->list = copy;
    return true;
}

bool value_list_slice_copy(const Value *list_value, int start_index, int end_index, Value *out_value) {
    int start = start_index - 1;
    int end = end_index - 1;
    int index;
    Value list = value_none();

    if (list_value->type != VALUE_LIST || list_value->list == NULL) {
        return false;
    }
    if (start < 0 || end < start || end >= list_value->list->count) {
        return false;
    }
    if (!value_make_empty_list(&list)) {
        return false;
    }
    for (index = start; index <= end; index += 1) {
        Value next_list = value_none();
        if (!value_list_append_copy(&list, &list_value->list->items[index], &next_list)) {
            value_destroy(&list);
            return false;
        }
        value_destroy(&list);
        list = next_list;
    }
    *out_value = list;
    return true;
}

static int compare_values_for_sort(const void *left_ptr, const void *right_ptr, void *mode_ptr) {
    const Value *left = (const Value *) left_ptr;
    const Value *right = (const Value *) right_ptr;
    bool numeric = *(const bool *) mode_ptr;

    if (numeric) {
        if (left->number < right->number) {
            return -1;
        }
        if (left->number > right->number) {
            return 1;
        }
        return 0;
    }

    {
        char *left_text = NULL;
        char *right_text = NULL;
        int result = 0;
        (void) value_to_text_copy(left, &left_text);
        (void) value_to_text_copy(right, &right_text);
        if (left_text == NULL && right_text == NULL) {
            result = 0;
        } else if (left_text == NULL) {
            result = -1;
        } else if (right_text == NULL) {
            result = 1;
        } else {
            result = strcmp(left_text, right_text);
        }
        free(left_text);
        free(right_text);
        return result;
    }
}

bool value_list_sort_copy(const Value *list_value, Value *out_value) {
    List *copy;
    bool numeric = true;
    int index;

    if (list_value->type != VALUE_LIST || list_value->list == NULL) {
        return false;
    }
    if (!list_copy(list_value->list, &copy)) {
        return false;
    }
    for (index = 0; index < copy->count; index += 1) {
        if (copy->items[index].type != VALUE_NUMBER) {
            numeric = false;
            break;
        }
    }
    /* Portable fallback: insertion sort keeps the code standard C compatible. */
    for (index = 1; index < copy->count; index += 1) {
        int inner = index;
        while (inner > 0 &&
               compare_values_for_sort(&copy->items[inner - 1], &copy->items[inner], &numeric) > 0) {
            Value temp = copy->items[inner - 1];
            copy->items[inner - 1] = copy->items[inner];
            copy->items[inner] = temp;
            inner -= 1;
        }
    }
    *out_value = value_none();
    out_value->type = VALUE_LIST;
    out_value->list = copy;
    return true;
}

int value_record_length(const Value *record_value) {
    if (record_value->type != VALUE_RECORD || record_value->record == NULL) {
        return -1;
    }
    return record_value->record->count;
}

bool value_record_get(const Value *record_value, const char *key, Value *out_value) {
    Record *record;
    RecordSlot *slot;

    if (record_value->type != VALUE_RECORD || record_value->record == NULL) {
        return false;
    }

    record = record_value->record;
    slot = record_find_slot(record, key);
    if (slot == NULL || slot->key == NULL) {
        return false;
    }

    return value_copy_into(&slot->value, out_value);
}

bool value_record_set_copy(const Value *record_value, const char *key, const Value *item, Value *out_value) {
    Record *copy;
    Value item_copy = value_none();

    if (record_value->type != VALUE_RECORD || record_value->record == NULL) {
        return false;
    }
    if (!record_copy(record_value->record, &copy)) {
        return false;
    }
    if (!value_copy_into(item, &item_copy)) {
        record_destroy(copy);
        return false;
    }
    if (!record_set_owned(copy, key, item_copy)) {
        value_destroy(&item_copy);
        record_destroy(copy);
        return false;
    }
    *out_value = value_none();
    out_value->type = VALUE_RECORD;
    out_value->record = copy;
    return true;
}

bool value_record_has(const Value *record_value, const char *key) {
    Record *record;
    RecordSlot *slot;

    if (record_value->type != VALUE_RECORD || record_value->record == NULL) {
        return false;
    }

    record = record_value->record;
    slot = record_find_slot(record, key);
    return slot != NULL && slot->key != NULL;
}

bool value_record_keys(const Value *record_value, Value *out_value) {
    int index;
    Value list = value_none();

    if (record_value->type != VALUE_RECORD || record_value->record == NULL) {
        return false;
    }

    if (!value_make_empty_list(&list)) {
        return false;
    }

    for (index = 0; index < record_value->record->capacity; index += 1) {
        if (record_value->record->slots[index].key != NULL) {
            Value key_value = value_none();
            Value next_list = value_none();
            if (!value_make_string_copy(record_value->record->slots[index].key, &key_value)) {
                value_destroy(&list);
                return false;
            }
            if (!value_list_append_copy(&list, &key_value, &next_list)) {
                value_destroy(&key_value);
                value_destroy(&list);
                return false;
            }
            value_destroy(&key_value);
            value_destroy(&list);
            list = next_list;
        }
    }

    *out_value = list;
    return true;
}

static Scope *runtime_current_scope(Runtime *runtime) {
    if (runtime->scope_count == 0) {
        return NULL;
    }
    return &runtime->scopes[runtime->scope_count - 1];
}

bool runtime_define_variable(Runtime *runtime, const char *name, Value value) {
    Scope *scope = runtime_current_scope(runtime);
    char *normalized = normalize_name(name);
    VariableSlot *slot;

    if (scope == NULL || normalized == NULL) {
        free(normalized);
        return false;
    }

    if (!variable_table_ensure_capacity(&scope->slots, &scope->capacity, scope->count)) {
        free(normalized);
        return false;
    }

    slot = scope_find_slot(scope, normalized);
    if (slot->name != NULL) {
        free(normalized);
        value_destroy(&slot->value);
        slot->value = value;
        return true;
    }

    slot->name = normalized;
    slot->value = value;
    scope->count += 1;
    return true;
}

bool runtime_assign_variable(Runtime *runtime, const char *name, Value value) {
    char *normalized = normalize_name(name);
    int scope_index;

    if (normalized == NULL) {
        return false;
    }

    for (scope_index = runtime->scope_count - 1; scope_index >= 0; scope_index -= 1) {
        Scope *scope = &runtime->scopes[scope_index];
        VariableSlot *slot = scope_find_slot(scope, normalized);
        if (slot != NULL && slot->name != NULL) {
            free(normalized);
            value_destroy(&slot->value);
            slot->value = value;
            return true;
        }
    }

    free(normalized);
    return false;
}

bool runtime_get_variable(const Runtime *runtime, const char *name, Value *out_value) {
    char *normalized = normalize_name(name);
    int scope_index;

    if (normalized == NULL) {
        return false;
    }

    for (scope_index = runtime->scope_count - 1; scope_index >= 0; scope_index -= 1) {
        Scope *scope = &runtime->scopes[scope_index];
        size_t slot;
        if (scope->capacity == 0) {
            continue;
        }
        slot = hash_text_case_insensitive(normalized) % (unsigned long) scope->capacity;
        while (scope->slots[slot].name != NULL) {
            if (equals_ignore_case(scope->slots[slot].name, normalized)) {
                free(normalized);
                return value_copy_into(&scope->slots[slot].value, out_value);
            }
            slot = (slot + 1U) % (unsigned long) scope->capacity;
        }
    }

    free(normalized);
    return false;
}

bool runtime_define_function(Runtime *runtime, const char *name, const struct Statement *function_statement) {
    char *normalized = normalize_name(name);
    FunctionSlot *slot;

    if (normalized == NULL) {
        return false;
    }

    if (!function_table_ensure_capacity(runtime)) {
        free(normalized);
        return false;
    }

    slot = function_find_slot(runtime, normalized);
    if (slot->name != NULL) {
        free(normalized);
        slot->function_statement = function_statement;
        return true;
    }

    slot->name = normalized;
    slot->function_statement = function_statement;
    runtime->function_count += 1;
    return true;
}

const struct Statement *runtime_get_function(const Runtime *runtime, const char *name) {
    char *normalized = normalize_name(name);
    const struct Statement *result = NULL;
    size_t slot;

    if (normalized == NULL) {
        return NULL;
    }

    if (runtime->function_capacity == 0) {
        free(normalized);
        return NULL;
    }

    slot = hash_text_case_insensitive(normalized) % (unsigned long) runtime->function_capacity;
    while (runtime->functions[slot].name != NULL) {
        if (equals_ignore_case(runtime->functions[slot].name, normalized)) {
            result = runtime->functions[slot].function_statement;
            break;
        }
        slot = (slot + 1U) % (unsigned long) runtime->function_capacity;
    }

    free(normalized);
    return result;
}

void runtime_destroy(Runtime *runtime) {
    int scope_index;
    int function_index;

    if (runtime == NULL) {
        return;
    }

    for (scope_index = 0; scope_index < runtime->scope_count; scope_index += 1) {
        Scope *scope = &runtime->scopes[scope_index];
        int index;
        for (index = 0; index < scope->capacity; index += 1) {
            if (scope->slots[index].name != NULL) {
                free(scope->slots[index].name);
                value_destroy(&scope->slots[index].value);
            }
        }
        free(scope->slots);
    }

    for (function_index = 0; function_index < runtime->function_capacity; function_index += 1) {
        free(runtime->functions[function_index].name);
    }

    free(runtime->scopes);
    free(runtime->functions);
    free(runtime);
}
