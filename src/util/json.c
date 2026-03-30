/*
 * json.c — cursor-based JSON stream reader
 *
 * Designed for archive.org API responses on PS2 (no malloc, no tree).
 */

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "json.h"

/* ── Internal helpers ─────────────────────────────────────────────────────── */

static void skip_ws(const char *j, JsonCursor *c)
{
    while (j[c->pos] && isspace((unsigned char)j[c->pos]))
        c->pos++;
}

static void skip_string(const char *j, JsonCursor *c)
{
    /* cursor is ON the opening quote */
    if (j[c->pos] != '"') return;
    c->pos++;
    while (j[c->pos]) {
        if (j[c->pos] == '\\') { c->pos += 2; continue; }
        if (j[c->pos] == '"')  { c->pos++; return; }
        c->pos++;
    }
}

static void read_string_body(const char *j, JsonCursor *c,
                              char *buf, int buf_len)
{
    /* cursor is AFTER the opening quote */
    int w = 0;
    while (j[c->pos] && w < buf_len - 1) {
        if (j[c->pos] == '\\') {
            c->pos++;
            char esc = j[c->pos++];
            switch (esc) {
                case 'n':  if (w < buf_len-1) buf[w++] = '\n'; break;
                case 't':  if (w < buf_len-1) buf[w++] = '\t'; break;
                case 'r':  break;
                case '"':  if (w < buf_len-1) buf[w++] = '"';  break;
                case '\\': if (w < buf_len-1) buf[w++] = '\\'; break;
                case '/':  if (w < buf_len-1) buf[w++] = '/';  break;
                case 'u': {
                    /* skip 4 hex digits — store '?' as placeholder */
                    c->pos += 4;
                    if (w < buf_len-1) buf[w++] = '?';
                    break;
                }
                default: break;
            }
        } else if (j[c->pos] == '"') {
            c->pos++;
            break;
        } else {
            buf[w++] = j[c->pos++];
        }
    }
    buf[w] = '\0';
    /* If we exited without hitting the closing quote, advance past it */
    if (j[c->pos-1] != '"') {
        while (j[c->pos] && j[c->pos] != '"') {
            if (j[c->pos] == '\\') c->pos++;
            c->pos++;
        }
        if (j[c->pos] == '"') c->pos++;
    }
}

/* ── Public API ───────────────────────────────────────────────────────────── */

JsonType json_peek(const char *j, JsonCursor *c)
{
    skip_ws(j, c);
    char ch = j[c->pos];
    if (ch == '"')  return JSON_T_STRING;
    if (ch == '{')  return JSON_T_OBJECT;
    if (ch == '[')  return JSON_T_ARRAY;
    if (ch == 't')  return JSON_T_TRUE;
    if (ch == 'f')  return JSON_T_FALSE;
    if (ch == 'n')  return JSON_T_NULL;
    if (ch == '-' || isdigit((unsigned char)ch)) return JSON_T_NUMBER;
    return JSON_T_NONE;
}

int json_enter_object(const char *j, JsonCursor *c)
{
    skip_ws(j, c);
    if (j[c->pos] != '{') return 0;
    c->pos++;
    c->depth++;
    return 1;
}

int json_enter_array(const char *j, JsonCursor *c)
{
    skip_ws(j, c);
    if (j[c->pos] != '[') return 0;
    c->pos++;
    c->depth++;
    return 1;
}

int json_next_key(const char *j, JsonCursor *c,
                  char *key_buf, int key_len)
{
    skip_ws(j, c);
    /* Skip commas between pairs */
    if (j[c->pos] == ',') { c->pos++; skip_ws(j, c); }
    /* End of object? */
    if (j[c->pos] == '}') { c->pos++; c->depth--; return 0; }
    /* End of string / missing key */
    if (j[c->pos] != '"') return 0;

    /* Read key */
    c->pos++;   /* skip opening quote */
    read_string_body(j, c, key_buf, key_len);

    /* Skip ':' */
    skip_ws(j, c);
    if (j[c->pos] == ':') c->pos++;
    skip_ws(j, c);
    return 1;
}

int json_read_string(const char *j, JsonCursor *c,
                     char *buf, int buf_len)
{
    skip_ws(j, c);
    buf[0] = '\0';

    /* Array element — skip leading comma */
    if (j[c->pos] == ',') { c->pos++; skip_ws(j, c); }
    /* End of array */
    if (j[c->pos] == ']') return 0;

    if (j[c->pos] == '"') {
        c->pos++;
        read_string_body(j, c, buf, buf_len);
        return 1;
    }
    if (j[c->pos] == '-' || isdigit((unsigned char)j[c->pos])) {
        /* Number — copy raw digits */
        int w = 0;
        while ((j[c->pos] == '-' || isdigit((unsigned char)j[c->pos]) ||
                j[c->pos] == '.' || j[c->pos] == 'e' || j[c->pos] == 'E' ||
                j[c->pos] == '+') && w < buf_len - 1)
            buf[w++] = j[c->pos++];
        buf[w] = '\0';
        return 1;
    }
    /* null / true / false — treat as empty */
    if (strncmp(j + c->pos, "null",  4) == 0) { c->pos += 4; return 1; }
    if (strncmp(j + c->pos, "true",  4) == 0) {
        c->pos += 4;
        strncpy(buf, "true", (size_t)buf_len);
        return 1;
    }
    if (strncmp(j + c->pos, "false", 5) == 0) {
        c->pos += 5;
        strncpy(buf, "false", (size_t)buf_len);
        return 1;
    }
    return 0;
}

void json_skip_value(const char *j, JsonCursor *c)
{
    skip_ws(j, c);
    if (j[c->pos] == ',') { c->pos++; skip_ws(j, c); }

    char ch = j[c->pos];
    if (ch == '"') { skip_string(j, c); return; }

    if (ch == '{' || ch == '[') {
        char open  = ch;
        char close = (ch == '{') ? '}' : ']';
        int  depth = 1;
        c->pos++;
        while (j[c->pos] && depth > 0) {
            if (j[c->pos] == '"') { skip_string(j, c); continue; }
            if (j[c->pos] == open)  depth++;
            if (j[c->pos] == close) depth--;
            c->pos++;
        }
        return;
    }

    /* number / keyword */
    while (j[c->pos] && j[c->pos] != ',' &&
           j[c->pos] != '}' && j[c->pos] != ']' &&
           !isspace((unsigned char)j[c->pos]))
        c->pos++;
}

void json_skip_rest(const char *j, JsonCursor *c)
{
    /* Skip until closed by matching bracket at the current nesting level */
    int depth = 1;
    while (j[c->pos] && depth > 0) {
        if (j[c->pos] == '"') { skip_string(j, c); continue; }
        if (j[c->pos] == '{' || j[c->pos] == '[') depth++;
        if (j[c->pos] == '}' || j[c->pos] == ']') {
            depth--;
            if (depth == 0) { c->pos++; return; }
        }
        c->pos++;
    }
}
