#ifndef JSON_PARSER_H_
#define JSON_PARSER_H_

#include "common.h"
#include "core/arena.h"
#include "core/lexer.h"

typedef enum {
	JSON_NULL,
	JSON_bool,
	JSON_uint32_t,
	JSON_uint64_t,
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
ENGINE_API JsonNode *json_node_where(JsonNode *list, String key, String value);
ENGINE_API void *json_value_safe(JsonNode *node, JsonType type);

ENGINE_API JsonNode *json_first(JsonNode *n);
ENGINE_API uint32_t json_count(JsonNode *n);

#define json_as(node, T) (*(T *)json_value_safe(node, JSON_##T))
#define json_find(node, s, T) (*(T *)json_value_safe(json_node(node, s), JSON_##T))

#define json_list(node, s) json_first(json_node(node, s))
#define json_list_count(node, s) json_count(json_node(node, s))

#define JSON_MAX_DEPTH 64
typedef struct {
	Arena *arena;

	uint32_t map_depth, array_depth;
	bool is_first_item[JSON_MAX_DEPTH];
	size_t start_offset;
} JsonExporter;

static inline JsonExporter json_exporter_make(Arena *arena) { return (JsonExporter){ .arena = arena, .start_offset = arena->offset, .is_first_item[0] = true }; }

ENGINE_API void json_begin_map(JsonExporter *exporter, String key);
ENGINE_API void json_end_map(JsonExporter *exporter);

ENGINE_API void json_begin_array(JsonExporter *exporter, String key);
ENGINE_API void json_end_array(JsonExporter *exporter);

ENGINE_API void json_write_pair_(JsonExporter *exporter, String key, JsonType type, Buffer buffer);
ENGINE_API void json_write_float(JsonExporter *exporter, float value);
ENGINE_API void json_write_float3(JsonExporter *exporter, float32x3 value);

#define json_write_pair(exporter, key, T, ...)                                   \
	do {                                                                         \
		T _val = __VA_ARGS__;                                                    \
		json_write_pair_((exporter), (key), JSON_##T, buffer_wrap_struct(_val)); \
	} while (0)

#endif /* JSON_PARSER_H_ */
