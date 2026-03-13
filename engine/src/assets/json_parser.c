#include "json_parser.h"

#include "core/debug.h"
#include "core/lexer.h"
#include "core/logger.h"

uint8_t json_zero_buffer[128] = { 0 };

const char *json_type_string[JSON_TYPE_COUNT] = {
	[JSON_NULL] = "null",
	[JSON_bool] = "bool",
	[JSON_uint32_t] = "uint",
	[JSON_int32_t] = "int",
	[JSON_float32] = "float",
	[JSON_String] = "string",
	[JSON_ARRAY] = "array",
	[JSON_OBJECT] = "object",
};

static JsonNode *parse_value(JsonParser *parser);
static JsonNode *parse_error(JsonParser *parser, Token token, const char *message) {
	if (!parser->failed)
		LOG_WARN("JSON error at %d:%d: %s (got '%.*s')",
			token.line, token.column, message, token.string.length, token.string.memory);
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
		arena_trie_put(trie, string_hash64(key.string), JsonNode *, value);

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
			node->type = JSON_uint32_t;

			uint32_t *integer = arena_push_struct(p->arena, uint32_t);
			*integer = string_to_u32(token.string);
			node->value = integer;
			return node;
		}
		case TOKEN_FLOAT: {
			JsonNode *node = arena_push_struct(p->arena, JsonNode);
			node->type = JSON_float32;

			float32 *floating_point = arena_push_struct(p->arena, float32);
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
				node->type = JSON_float32;

				float32 *floating_point = arena_push_struct(p->arena, float32);
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

	JsonNode **found = arena_trie_find((ArenaTrie *)node->value, string_hash64(key), JsonNode *);
	return found ? *found : NULL;
}

JsonNode *json_find_where(JsonNode *list, String key, String value) {
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
		if (node->type == JSON_uint32_t && type == JSON_float32) {
			*(float32 *)node->value = (float32)(*(uint32_t *)node->value);
			node->type = type;
			return node->value;
		}
		if (node->type == JSON_int32_t && type == JSON_float32) {
			*(float32 *)node->value = (float32)(*(int32_t *)node->value);
			node->type = type;
			return node->value;
		}
		if (node->type == JSON_float32 && type == JSON_uint32_t) {
			*(uint32_t *)node->value = (uint32_t)(*(float32 *)node->value);
			node->type = type;
			return node->value;
		}
		if (node->type == JSON_float32 && type == JSON_int32_t) {
			*(int32_t *)node->value = (int32_t)(*(float32 *)node->value);
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
