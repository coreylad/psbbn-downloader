#ifndef JSON_H
#define JSON_H

/*
 * json.h — minimal hand-rolled JSON reader for archive.org responses.
 *
 * Does NOT build a full parse tree.  Instead, it provides a cursor-based
 * streaming interface that lets callers navigate objects and arrays
 * without allocating tokens.
 *
 * Supported value types: string, number (read as string), object, array.
 * `null` and `false`/`true` are skipped or treated as empty strings.
 *
 * Usage example:
 *
 *   JsonCursor cur = {0};
 *   json_enter_object(json, &cur);
 *   char key[32];
 *   while (json_next_key(json, &cur, key, sizeof(key))) {
 *       if (strcmp(key, "title") == 0)
 *           json_read_string(json, &cur, title, sizeof(title));
 *       else
 *           json_skip_value(json, &cur);
 *   }
 */

typedef enum {
    JSON_T_NONE = 0,
    JSON_T_STRING,
    JSON_T_NUMBER,
    JSON_T_OBJECT,
    JSON_T_ARRAY,
    JSON_T_TRUE,
    JSON_T_FALSE,
    JSON_T_NULL
} JsonType;

typedef struct {
    int pos;     /* current byte offset into the JSON string */
    int depth;   /* nesting depth (informational)             */
} JsonCursor;

/* ── API ─────────────────────────────────────────────────────────────────── */

/* Peek at the type of the next value without consuming it. */
JsonType json_peek(const char *json, JsonCursor *cur);

/* Enter an object '{'.  Returns 1 on success. */
int json_enter_object(const char *json, JsonCursor *cur);

/* Enter an array '['.  Returns 1 on success. */
int json_enter_array(const char *json, JsonCursor *cur);

/* Read the next key in an object (advances past the ':').
   Returns 1 if a key was found; 0 if end '}' reached. */
int json_next_key(const char *json, JsonCursor *cur,
                  char *key_buf, int key_len);

/* Read a string (or number) value into buf.  Returns 1 on success. */
int json_read_string(const char *json, JsonCursor *cur,
                     char *buf, int buf_len);

/* Skip the current value entirely (object/array/string/number). */
void json_skip_value(const char *json, JsonCursor *cur);

/* Skip the rest of the current array or object (until closing bracket). */
void json_skip_rest(const char *json, JsonCursor *cur);

#endif /* JSON_H */
