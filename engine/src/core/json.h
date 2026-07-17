#pragma once

#include "arena.h"
#include "strings.h"

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

struct JSON_Node {
	String8 key;
	JSON_Type type;
	union {
		double number;
		String8 string;
		bool boolean;
		JSON_Container *children;
	} value;
};

typedef struct {
	Arena arena;
	JSON_Node root;
} JSON;

bool json_parse_string(JSON *json, String8 source);
bool json_parse_file(JSON *json, String8 source);

JSON_Node *json_find(JSON_Node *node, String8 key);
JSON_Node *json_child_at(JSON_Node *node, uint32_t index);


String8 json_stringify(Arena* arena, JSON *json);
