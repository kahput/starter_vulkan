#include "json_parser.h"

#include "core/debug.h"
#include "core/lexer.h"
#include "core/logger.h"

uint8_t json_zero_buffer[128] = { 0 };

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
	node->value = NULL;

	while (!lexer_match(&parser->lexer, TOKEN_RIGHT_BRACKET, NULL)) {
		if (parser->failed || lexer_at_end(&parser->lexer))
			break;

		JsonNode *element = parse_value(parser);
		if (element)
			element->next = NULL;

		if (tail == NULL)
			node->value = element;
		else
			tail->next = element;
		tail = element;
		/* node->as.array.count++; */

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
			node->type = JSON_int64_t;

			int64_t *integer = arena_push_struct(p->arena, int64_t);
			*integer = string_to_i64(token.string);
			node->value = integer;
			return node;
		}
		case TOKEN_FLOAT: {
			JsonNode *node = arena_push_struct(p->arena, JsonNode);
			node->type = JSON_float64;

			float64 *floating_point = arena_push_struct(p->arena, float64);
			*floating_point = string_to_f64(token.string);
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
				node->type = JSON_int64_t;

				int64_t *integer = arena_push_struct(p->arena, int64_t);
				*integer = -string_to_i64(num.string);
				node->value = integer;
				return node;
			}
			if (num.type == TOKEN_FLOAT) {
				node->type = JSON_float64;

				float64 *floating_point = arena_push_struct(p->arena, float64);
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

void *json_value_safe(JsonNode *node, JsonType type) {
	if (node == NULL)
		return json_zero_buffer;
	if (node->type != type) {
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

	return (JsonNode *)n->value;
}
