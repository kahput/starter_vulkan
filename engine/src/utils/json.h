#pragma once

#include "common.h"
#include "core/arena.h"
#include "core/strings.h"

typedef enum {
	JSON_TYPE_NULL,
	JSON_TYPE_BOOLEAN,
	JSON_TYPE_STRING,
	JSON_TYPE_OBJECT,
	JSON_TYPE_ARRAY,
	JSON_TYPE_NUMBER,

	JSON_TYPE_MAX,
} JSON_Type;

typedef struct JSON_Node JSON_Node;

#define JSON_CONTAINER_INITIAL_PAGE_SIZE 4
typedef struct {
	JSON_Node *pages[32];
	uint32_t count;
} JSON_Container;

typedef struct JSON_Value {
	JSON_Type type;
	union {
		float64 number;
		String8 string;
		bool boolean;
		JSON_Container *children;
	} as;
} JSON_Value;

struct JSON_Node {
	String8 key;
	JSON_Value value;
};

#define json_object() ((JSON_Value){ .type = JSON_TYPE_OBJECT })
#define json_array() ((JSON_Value){ .type = JSON_TYPE_ARRAY })
#define json_number(v) ((JSON_Value){ .type = JSON_TYPE_NUMBER, .as.number = (v) })
#define json_string(a, s) ((JSON_Value){ .type = JSON_TYPE_STRING, .as.string = str8_copy((a), (s)) })
#define json_bool(v) ((JSON_Value){ .type = JSON_TYPE_BOOLEAN, .as.boolean = (v) })
#define json_null() ((JSON_Value){ .type = JSON_TYPE_NULL })

extern JSON_Node JSON_NIL;
static inline bool json_valid(JSON_Node *node) { return node && node != &JSON_NIL; }

JSON_Node *json_parse_string(Arena *arena, String8 source);
JSON_Node *json_parse_file(Arena *arena, String8 source);

JSON_Node *json_append(Arena *arena, JSON_Container **container);
JSON_Node *json_append_item(Arena *arena, JSON_Node *parent);
JSON_Node *json_append_field(Arena *arena, JSON_Node *parent, String8 key);

JSON_Node *json_find(JSON_Node *node, String8 key);
JSON_Node *json_find_path(JSON_Node *node, String8 path);
JSON_Node *json_child_at(JSON_Node *node, uint32_t index);

static inline bool json_is_container(JSON_Node *node) { return json_valid(node) && (node->value.type == JSON_TYPE_OBJECT || node->value.type == JSON_TYPE_ARRAY); }
static inline uint32_t json_count(JSON_Node *node) {
	return json_is_container(node) && node->value.as.children ? node->value.as.children->count : 0;
}

static inline float64 json_num_or(JSON_Node *n, float64 fallback) {
	return json_valid(n) && n->value.type == JSON_TYPE_NUMBER ? n->value.as.number : fallback;
}
static inline String8 json_str_or(JSON_Node *n, String8 fallback) {
	return json_valid(n) && n->value.type == JSON_TYPE_STRING ? n->value.as.string : fallback;
}
static inline bool json_bool_or(JSON_Node *n, bool fallback) {
	return json_valid(n) && n->value.type == JSON_TYPE_BOOLEAN ? n->value.as.boolean : fallback;
}

String8 json_stringify(Arena *arena, JSON_Node *root, String8 indent);

// TEMPORARY
static inline bool json_append_float4(Arena *arena, JSON_Node *target, String8 key, float4 v) {
	bool ok = arena && target && json_is_container(target);
	if (ok) {
		JSON_Node *f4 = json_append_field(arena, target, key);
		f4->value = json_array();
		json_append_item(arena, f4)->value = json_number(v.x);
		json_append_item(arena, f4)->value = json_number(v.y);
		json_append_item(arena, f4)->value = json_number(v.z);
		json_append_item(arena, f4)->value = json_number(v.w);
	}

	return ok;
}

static inline bool json_append_float3(Arena *arena, JSON_Node *target, String8 key, float3 v) {
	bool ok = arena && target && json_is_container(target);
	if (ok) {
		JSON_Node *f3 = json_append_field(arena, target, key);
		f3->value = json_array();
		json_append_item(arena, f3)->value = json_number(v.x);
		json_append_item(arena, f3)->value = json_number(v.y);
		json_append_item(arena, f3)->value = json_number(v.z);
	}

	return ok;
}
