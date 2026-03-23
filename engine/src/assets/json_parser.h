#ifndef JSON_PARSER_H_
#define JSON_PARSER_H_

#include "common.h"
#include "core/arena.h"
#include "core/lexer.h"

typedef enum {
	JSON_NULL,
	JSON_bool,
	JSON_uint32_t,
	JSON_int32_t,
	JSON_float,
	JSON_String,
	JSON_ARRAY,
	JSON_OBJECT,

	JSON_TYPE_COUNT,
} JsonType;

extern const char *json_type_string[JSON_TYPE_COUNT];

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
ENGINE_API JsonNode *json_find_where(JsonNode *list, String key, String value);
ENGINE_API void *json_value_safe(JsonNode *node, JsonType type);

ENGINE_API JsonNode *json_first(JsonNode *n);
ENGINE_API uint32_t json_count(JsonNode *n);

#define json_as(node, T) (*(T *)json_value_safe(node, JSON_##T))
#define json_find(node, s, T) (*(T *)json_value_safe(json_node(node, s), JSON_##T))

#define json_list(node, s) json_first(json_node(node, s))
#define json_list_count(node, s) json_count(json_node(node, s))

#endif /* JSON_PARSER_H_ */
