#ifndef JSON_PARSER_H_
#define JSON_PARSER_H_

#include "core/arena.h"
#include "core/lexer.h"

typedef enum {
	JSON_NULL,
	JSON_bool,
	JSON_int64_t,
	JSON_float64,
	JSON_String,
	JSON_ARRAY,
	JSON_OBJECT,

	JSON_TYPE_COUNT,
} JsonType;

const char *json_type_string[JSON_TYPE_COUNT] = {
	[JSON_NULL] = "null",
	[JSON_bool] = "bool",
	[JSON_int64_t] = "int",
	[JSON_float64] = "float",
	[JSON_String] = "string",
	[JSON_ARRAY] = "array",
	[JSON_OBJECT] = "object",
};

typedef struct JsonNode JsonNode;

struct JsonNode {
	JsonType type;
	JsonNode *next;

	void *value;
};

typedef struct {
	Lexer lexer;
	Arena *arena;
	bool failed;
} JsonParser;

extern uint8_t json_zero_buffer[128];

ENGINE_API JsonNode *json_parse(Arena *arena, String source);
ENGINE_API JsonNode *json_node(JsonNode *node, String key);
ENGINE_API void *json_value_safe(JsonNode *node, JsonType type);
ENGINE_API JsonNode *json_first(JsonNode *n);

#define json_as(node, T) (*(T *)json_value_safe(node, JSON_##T))
#define json_find(node, s, T) (*(T *)json_value_safe(json_node(node, slit(s)), JSON_##T))
#define json_list(node, s) json_first(json_node(node, slit(s)))

#endif /* JSON_PARSER_H_ */
