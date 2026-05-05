#include "json_parser.h"

#include "assets/asset_types.h"
#include "core/debug.h"
#include "core/lexer.h"
#include "core/logger.h"
#include "core/strings.h"
#include <stdint.h>

uint8_t json_zero_buffer[128] = { 0 };

const char *json_type_string[JSON_TYPE_COUNT] = {
	[JSON_NULL] = "null",
	[JSON_bool] = "bool",
	[JSON_uint32_t] = "uint",
	[JSON_int32_t] = "int",
	[JSON_float] = "float",
	[JSON_String] = "string",
	[JSON_ARRAY] = "array",
	[JSON_OBJECT] = "object",
};

static JsonNode *parse_value(JsonParser *parser);
static JsonNode *parse_error(JsonParser *parser, Token token, const char *message) {
	if (!parser->failed) {
		LOG_WARN("JSON error at %d:%d: %s (got '%.*s')",
			token.line, token.column, message, token.string.length, token.string.chars);
	}
	parser->failed = true;
	return NULL;
}

static JsonNode *parse_object(JsonParser *parser) {
	JsonNode *node = arena_push_struct(parser->arena, JsonNode);
	node->type = JSON_OBJECT;

	ArenaTrie *trie = arena_push_struct(parser->arena, ArenaTrie);
	*trie = arena_trie_make(parser->arena);
	node->value = trie;

	while (!lexer_match(&parser->lexer, TOKEN_RIGHT_BRACE, NULL)) {
		if (parser->failed || lexer_at_end(&parser->lexer))
			break;

		Token key = lexer_expect(&parser->lexer, TOKEN_STRING);
		lexer_expect(&parser->lexer, TOKEN_COLON);
		JsonNode *value = parse_value(parser);

		// Store a JsonNode* in the trie, keyed by the field name
		arena_trie_put(trie, buffer_wrap_string(key.string), JsonNode *, value);

		lexer_match(&parser->lexer, TOKEN_COMMA, NULL);
	}
	return node;
}

static JsonNode *parse_array(JsonParser *parser) {
	JsonNode *node = arena_push_struct(parser->arena, JsonNode);
	node->type = JSON_ARRAY;
	JsonNode *tail = NULL;
	struct json_list {
		uint32_t count;
		JsonNode *first;
	} *list = arena_push(parser->arena, sizeof(struct json_list), alignof(struct json_list), true);
	node->value = list;

	while (!lexer_match(&parser->lexer, TOKEN_RIGHT_BRACKET, NULL)) {
		if (parser->failed || lexer_at_end(&parser->lexer))
			break;

		JsonNode *element = parse_value(parser);
		if (element)
			element->next = NULL;

		if (tail == NULL)
			list->first = element;
		else
			tail->next = element;
		tail = element;
		list->count++;

		lexer_match(&parser->lexer, TOKEN_COMMA, NULL);
	}
	return node;
}

static JsonNode *parse_value(JsonParser *p) {
	if (p->failed)
		return NULL;
	Token token = lexer_next(&p->lexer);

	if (token.type == TOKEN_EOF)
		return NULL;

	switch (token.type) {
		case TOKEN_LEFT_BRACE:
			return parse_object(p);
		case TOKEN_LEFT_BRACKET:
			return parse_array(p);
		case TOKEN_STRING: {
			JsonNode *node = arena_push_struct(p->arena, JsonNode);
			node->type = JSON_String;

			String *string = arena_push_struct(p->arena, String);
			*string = token.string;
			node->value = string;
			return node;
		}
		case TOKEN_INTEGER: {
			JsonNode *node = arena_push_struct(p->arena, JsonNode);
			node->type = JSON_uint64_t;

			uint64_t *integer = arena_push_struct(p->arena, uint64_t);
			*integer = string_to_u64(token.string);
			node->value = integer;
			return node;
		}
		case TOKEN_FLOAT: {
			JsonNode *node = arena_push_struct(p->arena, JsonNode);
			node->type = JSON_float;

			float *floating_point = arena_push_struct(p->arena, float);
			*floating_point = string_to_f32(token.string);
			node->value = floating_point;
			return node;
		}
		case TOKEN_FALSE: {
			JsonNode *node = arena_push_struct(p->arena, JsonNode);
			node->type = JSON_bool;

			bool *boolean = arena_push_struct(p->arena, bool);
			*boolean = false;
			node->value = boolean;
			return node;
		}
		case TOKEN_TRUE: {
			JsonNode *node = arena_push_struct(p->arena, JsonNode);
			node->type = JSON_bool;

			bool *boolean = arena_push_struct(p->arena, bool);
			*boolean = true;
			node->value = boolean;
			return node;
		}
		case TOKEN_NULL: {
			JsonNode *node = arena_push_struct(p->arena, JsonNode);
			node->type = JSON_NULL;
			return node;
		}
		case TOKEN_IDENTIFIER:
			return parse_error(p, token, "unknown identifier");
		case TOKEN_MINUS: {
			Token num = lexer_next(&p->lexer);
			JsonNode *node = arena_push_struct(p->arena, JsonNode);
			if (num.type == TOKEN_INTEGER) {
				node->type = JSON_int32_t;

				int32_t *integer = arena_push_struct(p->arena, int32_t);
				*integer = -string_to_i32(num.string);
				node->value = integer;
				return node;
			}
			if (num.type == TOKEN_FLOAT) {
				node->type = JSON_float;

				float *floating_point = arena_push_struct(p->arena, float);
				*floating_point = -string_to_f64(num.string);
				node->value = floating_point;
				return node;
			}
			return parse_error(p, num, "expected number after '-'");
		}
		default:
			return parse_error(p, token, "unexpected token");
	}
}

JsonNode *json_parse(Arena *arena, String source) {
	JsonParser parser = { .lexer = lexer_make(source), .arena = arena };
	JsonNode *root = parse_value(&parser);
	return root;
}

JsonNode *json_node(JsonNode *node, String key) {
	if (node == NULL || node->type != JSON_OBJECT)
		return NULL;

	JsonNode **found = arena_trie_find((ArenaTrie *)node->value, buffer_wrap_string(key), JsonNode *);
	return found ? *found : NULL;
}

JsonNode *json_node_where(JsonNode *list, String key, String value) {
	for (JsonNode *it = list; it; it = it->next) {
		JsonNode *prop = json_node(it, key);
		if (prop && prop->type == JSON_String) {
			if (string_equals(*(String *)prop->value, value)) {
				return it;
			}
		}
	}
	return NULL;
}

void *json_value_safe(JsonNode *node, JsonType type) {
	if (node == NULL)
		return json_zero_buffer;
	if (node->type != type) {
		// NOTE: Must cast the value to change the bits to be interpreted correctly
		if (node->type == JSON_uint64_t && type == JSON_float) {
			*(float *)node->value = (float)(*(uint64_t *)node->value);
			node->type = type;
			return node->value;
		}
		if (node->type == JSON_int32_t && type == JSON_float) {
			*(float *)node->value = (float)(*(int32_t *)node->value);
			node->type = type;
			return node->value;
		}
		if (node->type == JSON_float && type == JSON_uint32_t) {
			*(uint32_t *)node->value = (uint32_t)(*(float *)node->value);
			node->type = type;
			return node->value;
		}
		if (node->type == JSON_float && type == JSON_uint64_t) {
			*(uint64_t *)node->value = (uint64_t)(*(float *)node->value);
			node->type = type;
			return node->value;
		}
		if (node->type == JSON_float && type == JSON_int32_t) {
			*(int32_t *)node->value = (int32_t)(*(float *)node->value);
			node->type = type;
			return node->value;
		}

		LOG_WARN("JSON Type Mismatch: expected %s, found %s",
			json_type_string[type], json_type_string[node->type]);
		return json_zero_buffer;
	}

	return node->value ? node->value : json_zero_buffer;
}

JsonNode *json_first(JsonNode *n) {
	if (n == NULL)
		return NULL;
	if (n->type != JSON_ARRAY)
		ASSERT(false);

	struct json_list {
		uint32_t count;
		JsonNode *first;
	};
	return ((struct json_list *)n->value)->first;
}

uint32_t json_count(JsonNode *n) {
	if (n == NULL)
		return 0;
	if (n->type != JSON_ARRAY)
		ASSERT(false);

	struct json_list {
		uint32_t count;
		JsonNode *first;
	};
	return ((struct json_list *)n->value)->count;
}

static void json_prepare_next_item(JsonExporter *exporter) {
	uint32_t depth = exporter->array_depth + exporter->map_depth;

	if (exporter->is_first_item[depth] == false) {
		string_format_non_terminated(exporter->arena, ",\n");
	} else {
		string_format_non_terminated(exporter->arena, "\n");
		exporter->is_first_item[depth] = false;
	}

	string_format_non_terminated(exporter->arena, "%*s", depth * 4, "");
}

void json_begin_map(JsonExporter *exporter, String key) {
	json_prepare_next_item(exporter);

	if (key.length > 0)
		string_format_non_terminated(exporter->arena, "\"" SFMT "\": {", SARG(key));
	else
		string_format_non_terminated(exporter->arena, "{");

	exporter->map_depth++;
	exporter->is_first_item[exporter->map_depth + exporter->array_depth] = true;
}

void json_end_map(JsonExporter *exporter) {
	ASSERT(exporter->map_depth > 0);
	exporter->map_depth--;
	uint32_t depth = exporter->map_depth + exporter->array_depth;
	string_format_non_terminated(exporter->arena, "\n%*s}", depth * 4, "");
}

void json_begin_array(JsonExporter *exporter, String key) {
	json_prepare_next_item(exporter);

	if (key.length > 0)
		string_format_non_terminated(exporter->arena, "\"" SFMT "\": [", SARG(key));
	else
		string_format_non_terminated(exporter->arena, "[");

	exporter->array_depth++;
	exporter->is_first_item[exporter->map_depth + exporter->array_depth] = true;
}
void json_end_array(JsonExporter *exporter) {
	ASSERT(exporter->array_depth > 0);
	exporter->array_depth--;
	uint32_t depth = exporter->map_depth + exporter->array_depth;
	string_format_non_terminated(exporter->arena, "\n%*s]", depth * 4, "");
}

void json_write_pair_(JsonExporter *exporter, String key, JsonType type, Buffer buffer) {
	json_prepare_next_item(exporter);
	switch (type) {
		case JSON_bool: {
			String value = *buffer.pointer ? S("true") : S("false");
			string_format_non_terminated(exporter->arena, "\"" SFMT "\": " SFMT "", SARG(key), SARG(value));
		} break;
		case JSON_uint32_t: {
			uint32_t value = *(uint32_t *)buffer.pointer;
			string_format_non_terminated(exporter->arena, "\"" SFMT "\": %u", SARG(key), value);
		} break;
		case JSON_uint64_t: {
			uint64_t value = *(uint64_t *)buffer.pointer;
			string_format_non_terminated(exporter->arena, "\"" SFMT "\": %llu", SARG(key), value);
		} break;
		case JSON_int32_t: {
			int32_t value = *(int32_t *)buffer.pointer;
			string_format_non_terminated(exporter->arena, "\"" SFMT "\": %d", SARG(key), value);
		} break;
		case JSON_float: {
			float value = *(float *)buffer.pointer;
			string_format_non_terminated(exporter->arena, "\"" SFMT "\": %.3f", SARG(key), value);
		} break;
		case JSON_String: {
			String value = *(String *)buffer.pointer;
			string_format_non_terminated(exporter->arena, "\"" SFMT "\": \"" SFMT "\"", SARG(key), SARG(value));
		} break;
		case JSON_ARRAY:
		case JSON_OBJECT:
		case JSON_TYPE_COUNT:
		case JSON_NULL:
			ASSERT(false);
			break;
	}
}

/* void json_write_pair(JsonExporter *exporter, String key, String value) { */
/* 	json_prepare_next_item(exporter); */
/* 	string_format_non_terminated(exporter->arena, "\"" SFMT "\": \"" SFMT "\"", SARG(key), SARG(value)); */
/* } */

void json_write_float(JsonExporter *exporter, float value) {
	json_prepare_next_item(exporter);
	string_format_non_terminated(exporter->arena, "%.3f", value);
}

void json_write_float3(JsonExporter *exporter, float32x3 value) {
	json_prepare_next_item(exporter);
	string_format_non_terminated(exporter->arena, "%.3f, %.3f, %.3f", value.x, value.y, value.z);
}
