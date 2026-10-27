// engine/resources_internal.h
//
// Shared by the files that make up the pack loader (engine/resources*.c): the
// JSON helpers, and the parsers resources_load calls in the other files. Not
// part of the engine's public API.

#ifndef OB_RESOURCES_INTERNAL_H
#define OB_RESOURCES_INTERNAL_H

#include <stddef.h>
#include <stdlib.h>   // RES_TABLE_ALLOC: free, calloc

#include "resources.h"
#include "cJSON.h"

// Bounded copy of `src` into `dst` (always terminated).
void res_copy_str(char *dst, size_t dst_sz, const char *src);
// `key` of `obj` as an int / a string, or `fallback` when absent or not one.
int res_json_int(const cJSON *obj, const char *key, int fallback);
const char *res_json_str(const cJSON *obj, const char *key, const char *fallback);
// A JSON array of path strings into a fixed-stride table / a heap list.
void res_parse_path_array(cJSON *arr, char *dst, size_t stride, int cap, int *out_count);
void res_parse_path_list(cJSON *arr, char (**dst)[RES_PATH_LEN], int *out_count);
// `key` of `obj` as a fixed-length int array; a missing key or a wrong type
// leaves `out[]` untouched, so callers prime it with defaults.
void res_json_int_array(const cJSON *obj, const char *key, int *out, int n);
// A "#RRGGBB" / "#RRGGBBAA" string, or `fallback`.
unsigned int res_parse_color_hex(const char *s, unsigned int fallback);
void res_json_color(const cJSON *obj, const char *key, unsigned int *out);
// A pack file as a terminated heap string (from the pack stack), or NULL.
char *res_slurp(const char *path);
// A JSON array's or object's entry count (0 when it is neither).
int res_json_len(const cJSON *j);

// Size a heap catalog table for `n` entries, releasing what it held and
// resetting its count. False when there is nothing to hold or no memory.
#define RES_TABLE_ALLOC(field, count, n)                                   \
    (free(field), (field) = NULL, (count) = 0,                             \
     ((n) > 0 && ((field) = calloc((size_t)(n), sizeof *(field))) != NULL))

// The strings half of the load (resources_strings.c) and the Introduction
// (resources_intro.c).
void res_parse_strings(Resources *res, cJSON *obj);
// `jpath` names the script file; `strings_root` holds its strings.
void res_parse_intro(Resources *res, const cJSON *jpath, const cJSON *strings_root);
void res_intro_free(ResIntro *in);
// A villain's animation frames: its own `anim` list, or the <portrait>_NN
// siblings the shell derives. Returns the count; `*out` is a heap list.
int res_villain_frame_paths(const VillainDef *v, char (**out)[RES_PATH_LEN]);

#endif
