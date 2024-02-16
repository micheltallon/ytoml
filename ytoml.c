#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <yapi.h>
#include <pstdlib.h>
#include "toml.h"

static char errbuf[256];

/*---------------------------------------------------------------------------*/
/* TABLES AND ARRAYS */

typedef struct ytoml_root_ {
    toml_table_t* table;
    long nrefs;
} ytoml_root;

typedef struct ytoml_base_ {
    ytoml_root* root;
} ytoml_base;

typedef struct ytoml_table_ {
    ytoml_root* root;
    toml_table_t* table;
} ytoml_table;

typedef struct ytoml_array_ {
    ytoml_root* root;
    toml_array_t* array;
} ytoml_array;

#define IN_RANGE(x,a,b)  (((a) <= (x)) & ((x) <= (b)))

static ytoml_table* ytoml_table_push(toml_table_t* table, ytoml_root* root);
static ytoml_array* ytoml_array_push(toml_array_t* array, ytoml_root* root);

static void ytoml_timestamp_push(toml_timestamp_t* ts);

static void push_string(const char* str)
{
    char** arr = ypush_q(NULL);
    arr[0] = str == NULL ? NULL : p_strcpy(str);
}

static void ytoml_free(void* addr)
{
    ytoml_base* obj = addr;
    if (obj->root != NULL && --obj->root->nrefs == 0) {
        // This object is the root table.
        fprintf(stderr, "DEBUG: freeing TOML root table...\n");
        toml_free(obj->root->table);
    }
}

static void ytoml_table_print(void* addr)
{
    ytoml_table* obj = addr;
    char buffer[64];
    sprintf(buffer, "%ld", (long)toml_table_len(obj->table));
    y_print("TOML Table (len = ", 0);
    y_print(buffer, 0);
    y_print(")", 1);
}

static void ytoml_array_print(void* addr)
{
    ytoml_array* obj = addr;
    char buffer[64];
    sprintf(buffer, "%ld", (long)toml_array_len(obj->array));
    y_print("TOML Array (len = ", 0);
    y_print(buffer, 0);
    y_print(")", 1);
}

static void ytoml_table_eval(void* addr, int argc)
{
    if (argc != 1) y_error("expecting exactly one argument");
    if (yarg_rank(0) != 0) {
        bad_arg:
        y_error("expecting a scalar integer index or a string key");
    }
    ytoml_table* obj = addr;
    int type = yarg_typeid(0);
    const char* key = NULL;
    if (type == Y_STRING) {
        key = ygets_q(0);
    } else if (IN_RANGE(type, Y_CHAR, Y_LONG)) {
        long idx = ygets_l(0);
        long len = toml_table_len(obj->table);
        if (idx <= 0) {
            // Apply Yorick's indexing rule.
            idx += len;
        }
        if (!IN_RANGE(idx, 1, len)) {
            y_error("index overreach beyond table bounds");
        }
        int keylen;
        key = toml_table_key(obj->table, idx - 1, &keylen);
    } else {
        goto bad_arg;
    }
    if (key == NULL) {
        ypush_nil();
        return;
    }
    // Entry may be a boolean?
    toml_value_t val = toml_table_bool(obj->table, key);
    if (val.ok) {
        ypush_int(val.u.b ? 1 : 0);
        return;
    }
    // Entry may be an integer?
    val = toml_table_int(obj->table, key);
    if (val.ok) {
        ypush_long(val.u.i);
        return;
    }
    // Entry may be a float?
    val = toml_table_double(obj->table, key);
    if (val.ok) {
        ypush_double(val.u.d);
        return;
    }
    // Entry may be a string?
    val = toml_table_string(obj->table, key);
    if (val.ok) {
        push_string(val.u.s);
        if (val.u.s != NULL) free(val.u.s);
        return;
    }
    // Entry may be an array?
    toml_array_t* arr = toml_table_array(obj->table, key);
    if (arr != NULL) {
        ytoml_array_push(arr, obj->root);
        return;
    }
    // Entry may be a table?
    toml_table_t* tbl = toml_table_table(obj->table, key);
    if (tbl != NULL) {
        ytoml_table_push(tbl, obj->root);
        return;
    }
    // Entry may be a timestamp?
    errno = 0;
    val = toml_table_timestamp(obj->table, key);
    if (val.ok) {
        ytoml_timestamp_push(val.u.ts);
        return;
    }
    if (errno == ENOMEM) {
        y_error("insufficient memory for timestamp");
    }
    // Entry is nothing known or does not exist.
    ypush_nil();
}

static void ytoml_array_eval(void* addr, int argc)
{
    if (argc != 1) y_error("expecting exactly one argument");
    int type = yarg_typeid(0);
    if (!IN_RANGE(type, Y_CHAR, Y_LONG) || yarg_rank(0) != 0) {
        y_error("expecting a scalar integer index");
    }
    ytoml_array* obj = addr;
    long idx = ygets_l(0);
    long len = toml_array_len(obj->array);
    if (idx <= 0) {
        // Apply Yorick's indexing rule.
        idx += len;
    }
    if (!IN_RANGE(idx, 1, len)) {
        y_error("index overreach beyond array bounds");
    }
    --idx; // 1-based index -> 0-based index
    // Entry may be a boolean?
    toml_value_t val = toml_array_bool(obj->array, idx);
    if (val.ok) {
        ypush_int(val.u.b ? 1 : 0);
        return;
    }
    // Entry may be an integer?
    val = toml_array_int(obj->array, idx);
    if (val.ok) {
        ypush_long(val.u.i);
        return;
    }
    // Entry may be a float?
    val = toml_array_double(obj->array, idx);
    if (val.ok) {
        ypush_double(val.u.d);
        return;
    }
    // Entry may be a string?
    val = toml_array_string(obj->array, idx);
    if (val.ok) {
        push_string(val.u.s);
        if (val.u.s != NULL) free(val.u.s);
        return;
    }
    // Entry may be an array?
    toml_array_t* arr = toml_array_array(obj->array, idx);
    if (arr != NULL) {
        ytoml_array_push(arr, obj->root);
        return;
    }
    // Entry may be a table?
    toml_table_t* tbl = toml_array_table(obj->array, idx);
    if (tbl != NULL) {
        ytoml_table_push(tbl, obj->root);
        return;
    }
    // Entry may be a timestamp?
    errno = 0;
    val = toml_array_timestamp(obj->array, idx);
    if (val.ok) {
        ytoml_timestamp_push(val.u.ts);
        return;
    }
    if (errno == ENOMEM) {
        y_error("insufficient memory for timestamp");
    }
    // Entry is nothing known or does not exist.
    ypush_nil();
}

static void ytoml_table_extract(void* addr, char* name)
{
    ytoml_table* obj = addr;
    int c = name[0];
    switch (c) {
    case 'i':
        if (strcmp("is_root", name) == 0) {
            ypush_int(obj->root != NULL && obj->root->table == obj->table);
            return;
        }
        break;
    case 'l':
        if (strcmp("len", name) == 0) {
            ypush_long(toml_table_len(obj->table));
            return;
        }
        break;
    case 'r':
        if (strcmp("root", name) == 0) {
            ytoml_table_push(obj->root->table, obj->root);
            return;
        }
        break;
    }
    y_error("invalid member of TOML table");
}

static void ytoml_array_extract(void* addr, char* name)
{
    ytoml_array* obj = addr;
    int c = name[0];
    switch (c) {
    case 'i':
        if (strcmp("is_root", name) == 0) {
            ypush_int(0);
            return;
        }
        break;
    case 'l':
        if (strcmp("len", name) == 0) {
            ypush_long(toml_array_len(obj->array));
            return;
        }
        break;
    case 'r':
        if (strcmp("root", name) == 0) {
            ytoml_table_push(obj->root->table, obj->root);
            return;
        }
        break;
    }
    y_error("invalid member of TOML array");
}

static y_userobj_t ytoml_table_type = {
    "toml_table",
    ytoml_free,
    ytoml_table_print,
    ytoml_table_eval,
    ytoml_table_extract,
    NULL
};

static y_userobj_t ytoml_array_type = {
    "toml_table",
    ytoml_free,
    ytoml_array_print,
    ytoml_array_eval,
    ytoml_array_extract,
    NULL
};

static ytoml_table* ytoml_table_push(toml_table_t* table, ytoml_root* root)
{
    if (root == NULL) {
        root = malloc(sizeof(ytoml_root));
        if (root == NULL) {
            toml_free(table);
            y_error("not enough memory");
        }
        root->nrefs = 0;
        root->table = table;
    }
    ytoml_table* obj = ypush_obj(&ytoml_table_type, sizeof(ytoml_table));
    obj->table = table;
    ++root->nrefs;
    obj->root = root;
    return obj;
}

static ytoml_array* ytoml_array_push(toml_array_t* array, ytoml_root* root)
{
    if (root == NULL) y_error("TOML array must have a root table");
    ytoml_array* obj = ypush_obj(&ytoml_array_type, sizeof(ytoml_array));
    obj->array = array;
    ++root->nrefs;
    obj->root = root;
    return obj;
}

/*---------------------------------------------------------------------------*/
/* TIMESTAMPS */

static void ytoml_timestamp_free(void* addr)
{
    // Nothing to do.
}

static void ytoml_timestamp_print(void* addr)
{
   y_print("TOML Timestamp", 1);
}

static void ytoml_timestamp_eval(void* addr, int argc)
{
    y_error("TOML timestamp is not callable");
}

static void ytoml_timestamp_extract(void* addr, char* name)
{
    toml_timestamp_t* ts = addr;
    int c0 = name[0], c1;
    switch (c0) {
    case 'd':
        if (strcmp("day", name) == 0) {
            ypush_long(ts->day);
            return;
        }
        break;
    case 'h':
        if (strcmp("hour", name) == 0) {
            ypush_long(ts->hour);
            return;
        }
        break;
    case 'k':
        if (strcmp("kind", name) == 0) {
            char *arr = ypush_c(NULL);
            arr[0] = ts->kind;
            return;
        }
        break;
    case 'm':
        c1 = name[1];
        if (c1 == 'o' && strcmp("month", name) == 0) {
            ypush_long(ts->month);
            return;
        }
        if (c1 == 'i' && strcmp("minute", name) == 0) {
            ypush_long(ts->minute);
            return;
        }
        break;
    case 's':
        if (strcmp("second", name) == 0) {
            ypush_double(ts->second + ts->millisec/1000.0);
            return;
        }
        break;
    case 't':
        if (strcmp("tz", name) == 0) {
            push_string(ts->z);
            return;
        }
        break;
    case 'y':
        if (strcmp("year", name) == 0) {
            ypush_long(ts->year);
            return;
        }
        break;
    }
    y_error("invalid member of TOML array");
}

static y_userobj_t ytoml_timestamp_type = {
    "toml_timestamp",
    ytoml_timestamp_free,
    ytoml_timestamp_print,
    ytoml_timestamp_eval,
    ytoml_timestamp_extract,
    NULL
};

static void ytoml_timestamp_push(toml_timestamp_t* ts)
{
    void* obj = ypush_obj(&ytoml_timestamp_type, sizeof(toml_timestamp_t));
    if (ts != NULL) {
        memcpy(obj, ts, sizeof(toml_timestamp_t));
        free(ts);
    }
}

/*---------------------------------------------------------------------------*/
/* BUILTIN FUNCTIONS */

void Y_toml_parse(int argc)
{
    if (argc != 1) y_error("expecting exactly one argument");
    char* buffer = ygets_q(0);
    toml_table_t* table = toml_parse(buffer, errbuf, sizeof(errbuf));
    if (table == NULL) {
        y_error(errbuf);
    }
    ytoml_table_push(table, NULL);
}

void Y_toml_parse_file(int argc)
{
    if (argc != 1) y_error("expecting exactly one argument");
    char* filename = ygets_q(0);
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        y_error("cannot open file for reading");
    }
    toml_table_t* table = toml_parse_file(file, errbuf, sizeof(errbuf));
    fclose(file);
    if (table == NULL) {
        y_error(errbuf);
    }
    ytoml_table_push(table, NULL);
}

void Y_toml_type(int argc)
{
    if (argc != 1) y_error("expecting exactly one argument");
    int res = 0;
    int type = yarg_typeid(0);
    if (type == Y_OPAQUE) {
        const char* name = yget_obj(0, NULL);
        if (name == ytoml_table_type.type_name) {
            res = 1;
        } else if (name == ytoml_array_type.type_name) {
            res = 2;
        } else if (name == ytoml_timestamp_type.type_name) {
            res = 3;
        }
    }
    ypush_int(res);
}

void Y_toml_length(int argc)
{
    if (argc != 1) y_error("expecting exactly one argument");
    long len = -1;
    if (yarg_typeid(0) == Y_OPAQUE) {
        const char* name = yget_obj(0, NULL);
        if (name == ytoml_table_type.type_name) {
            ytoml_table* obj = yget_obj(0, &ytoml_table_type);
            len = toml_table_len(obj->table);
        } else if (name == ytoml_array_type.type_name) {
            ytoml_array* obj = yget_obj(0, &ytoml_array_type);
            len = toml_array_len(obj->array);
        }
    }
    ypush_long(len);
}

void Y_toml_key(int argc)
{
    if (argc != 2) y_error("expecting exactly two arguments");
    ytoml_table* obj = yget_obj(1, &ytoml_table_type);
    long idx = ygets_l(0);
    long len = toml_table_len(obj->table);
    if (idx <= 0) {
        // Apply Yorick's indexing rule.
        idx += len;
    }
    char** arr = ypush_q(NULL);
    if (IN_RANGE(idx, 1, len)) {
        int keylen;
        const char* key = toml_table_key(obj->table, idx - 1, &keylen);
        arr[0] = key == NULL ? NULL : p_strcpy(key);
    }
}

void Y_toml_keys(int argc)
{
    if (argc != 1) y_error("expecting exactly one arguments");
    ytoml_table* obj = yget_obj(0, &ytoml_table_type);
    long len = toml_table_len(obj->table);
    long dims[2] = {1, len};
    char** arr = ypush_q(dims);
    for (long idx = 0; idx < len; ++idx) {
        int keylen;
        const char* key = toml_table_key(obj->table, idx, &keylen);
        arr[idx] = key == NULL ? NULL : p_strcpy(key);
    }
}
