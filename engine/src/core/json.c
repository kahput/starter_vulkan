#include "json.h"
#include "common.h"
#include "core/arena.h"
#include "core/debug.h"
#include "core/lexer.h"
#include "core/logger.h"
#include "core/strings.h"
#include "os.h"

typedef enum {
	JSON_KEYWORD_NULL = TOKEN_KEYWORD_0,
	JSON_KEYWORD_TRUE,
	JSON_KEYWORD_FALSE,

	JSON_KEYWORD_MAX,
} JSON_Keyword;

#define keyword(k) k - TOKEN_KEYWORD_0
TokenType json_keyword_to_token_type[keyword(JSON_KEYWORD_MAX)] = {
	[keyword(JSON_KEYWORD_NULL)] = TOKEN_KEYWORD_0,
	[keyword(JSON_KEYWORD_FALSE)] = TOKEN_KEYWORD_1,
	[keyword(JSON_KEYWORD_TRUE)] = TOKEN_KEYWORD_2
};

String8 json_keyword_to_string[keyword(JSON_KEYWORD_MAX)] = {
	[keyword(JSON_KEYWORD_NULL)] = str_comp("null"),
	[keyword(JSON_KEYWORD_FALSE)] = str_comp("false"),
	[keyword(JSON_KEYWORD_TRUE)] = str_comp("true")
};
#undef keyword

JSON_Node json_parse_object(JSON *json, Lexer *lexer);
JSON_Node json_parse_array(JSON *json, Lexer *lexer);

typedef struct {
	uint32_t page_size, page_index, page_local_index;
} JSON_ContainerSlot;
static inline JSON_ContainerSlot json__locate(uint32_t index) {
	JSON_ContainerSlot result = { .page_size = JSON_CONTAINER_INITIAL_PAGE_SIZE };

	uint32_t page_base = 0;
	while (index >= page_base + result.page_size) {
		page_base += result.page_size;
		result.page_size *= 2;
		result.page_index++;
	}
	result.page_local_index = index - page_base;

	return result;
}

static inline JSON_Node json_parse_value(JSON *json, Lexer *lexer) {
	JSON_Node result = { 0 };

	bool ok = lexer;
	if (ok) {
		Token value = lexer_peek(lexer);

		switch (value.type) {
			case TOKEN_OPEN_BRACE: // OBJECT
				result = json_parse_object(json, lexer);
				break;

			case TOKEN_OPEN_BRACKET: // ARRAY
				result = json_parse_array(json, lexer);
				break;

			case TOKEN_PLUS:
			case TOKEN_MINUS:
				lexer_next(lexer);
				double mul = 1.0f - (value.type == TOKEN_MINUS) * 2.0f;

				value = lexer_expect_multiple(lexer, array_arg(TokenType, TOKEN_INTEGER, TOKEN_FLOAT));
				result.type = JSON_TYPE_NUMBER;
				result.value.number = mul * str8_to_f64(value.lexeme);
				break;

			case TOKEN_FLOAT: // NUMBER
			case TOKEN_INTEGER:
				result.type = JSON_TYPE_NUMBER;
				result.value.number = str8_to_f64(value.lexeme);
				lexer_next(lexer);
				break;

			case TOKEN_STRING: {
				result.type = JSON_TYPE_STRING;
				result.value.string = str8_copy(&json->arena, value.lexeme);
				lexer_next(lexer);
			} break;

			case JSON_KEYWORD_TRUE: // BOOL
			case JSON_KEYWORD_FALSE:
				result.type = JSON_TYPE_BOOLEAN;
				result.value.boolean = (uint32_t)value.type == JSON_KEYWORD_TRUE;
				lexer_next(lexer);
				break;

			case JSON_KEYWORD_NULL: // NULL
				result.type = JSON_TYPE_NULL;
				lexer_next(lexer);
			default:

				break;
		}
	}

	return result;
}

JSON_Node *json_append(JSON *json, JSON_Node *parent) {
	JSON_Node *result = 0;

	bool ok = json && parent;

	if (ok) {
		JSON_Container *con = parent->value.children;
		if (con == 0) {
			con = parent->value.children = arena_push_count(&json->arena, JSON_Container, 1);
			parent->value.children->pages[0] = arena_push_count(&json->arena, JSON_Node, JSON_CONTAINER_INITIAL_PAGE_SIZE);
		}

		JSON_ContainerSlot slot = json__locate(con->count);

		JSON_Node *page = parent->value.children->pages[slot.page_index];
		if (page == 0)
			page = parent->value.children->pages[slot.page_index] = arena_push_count(&json->arena, JSON_Node, slot.page_size);

		result = &page[slot.page_local_index];
		con->count++;
	}

	return result;
}

JSON_Node json_parse_array(JSON *json, Lexer *lexer) {
	JSON_Node result = { .type = JSON_TYPE_ARRAY };

	bool ok = json && lexer;
	if (ok) {
		lexer_expect(lexer, TOKEN_OPEN_BRACKET);

		do {
			*json_append(json, &result) = json_parse_value(json, lexer);
		} while (lexer_match(lexer, TOKEN_COMMA, 0));
		Token close_brace = lexer_expect(lexer, TOKEN_CLOSE_BRACKET);
	}

	return result;
}

JSON_Node json_parse_object(JSON *json, Lexer *lexer) {
	JSON_Node result = { .type = JSON_TYPE_OBJECT };

	bool ok = json && lexer;
	if (ok) {
		Token open_brace = lexer_expect(lexer, TOKEN_OPEN_BRACE);

		do {
			Token key = lexer_expect(lexer, TOKEN_STRING);
			lexer_expect(lexer, TOKEN_COLON);

			JSON_Node *value = json_append(json, &result);
			*value = json_parse_value(json, lexer);
			value->key = str8_copy(&json->arena, key.lexeme);
		} while (lexer_match(lexer, TOKEN_COMMA, 0));

		Token close_brace = lexer_expect(lexer, TOKEN_CLOSE_BRACE);
	}

	return result;
}

bool json_parse_string(JSON *json, String8 source) {
	bool ok = json;
	if (ok) {
		Lexer lexer = lexer_make(source, json_keyword_to_string, countof(json_keyword_to_string));
		json->root = json_parse_value(json, &lexer);
	}

	return ok;
}

bool json_parse_file(JSON *json, String8 path) {
	bool ok = json;
	if (ok) {
		Lexer lexer = lexer_make(os_file_read_entire(&json->arena, path), json_keyword_to_string, countof(json_keyword_to_string));
		json->root = json_parse_value(json, &lexer);
	}

	return ok;
}

JSON_Node *json_find(JSON_Node *node, String8 key) {
	JSON_Node *result = 0;

	bool ok = node && node->type == JSON_TYPE_OBJECT && node->value.children;
	if (ok) {
		JSON_Container *container = node->value.children;
		for (uint32_t index = 0; index < container->count; ++index) { // TODO: Hashmap
			JSON_ContainerSlot slot = json__locate(index);

			JSON_Node *page = container->pages[slot.page_index];
			if (page) {
				JSON_Node *child = &page[slot.page_local_index];
				if (str8_equals(child->key, key)) {
					result = child;
					break;
				}
			}
		}
	}

	return result;
}

JSON_Node *json_child_at(JSON_Node *node, uint32_t index) {
	JSON_Node *result = 0;

	bool ok = node && node->type == JSON_TYPE_ARRAY && node->value.children;
	if (ok) {
		JSON_Container *container = node->value.children;
		JSON_ContainerSlot slot = json__locate(index);
		JSON_Node *page = container->pages[slot.page_index];
		if (page)
			result = &page[slot.page_local_index];
	}

	return result;
}

void json_write_value(Arena *arena, JSON_Node *node, uint32_t *depth) {
	switch (node->type) {
		case JSON_TYPE_NULL: {
			arena_push_count(arena, uint8_t, 4);
		};
		case JSON_TYPE_BOOLEAN:
		case JSON_TYPE_STRING:
		case JSON_TYPE_OBJECT:
		case JSON_TYPE_ARRAY:
		case JSON_TYPE_NUMBER:
		case JSON_TYPE_MAX:
			break;
	}
}

void json_write_children(Arena *arena, JSON_Container *children, uint32_t *depth) {
	bool ok = children;
	if (ok) {
		for (uint32_t index = 0; index < children->count; ++index) {
			JSON_ContainerSlot slot = json__locate(index);
			JSON_Node *child = &children->pages[slot.page_index][slot.page_local_index];

			bool has_key = child->key.length;

			str8_pushf(arena, s("%*s"), (*depth) * 4, "");
			if (has_key)
				str8_pushf(arena, s("\"%.*s\": "), str_spread(child->key));

			switch (child->type) {
				case JSON_TYPE_NULL:
					str8_pushf(arena, s("%s"), "null");
					break;
				case JSON_TYPE_BOOLEAN:
					str8_pushf(arena, s("%s"), child->value.boolean ? "true" : "false");
					break;
				case JSON_TYPE_STRING:
					str8_pushf(arena, s("\"%.*s\""), str_spread(child->value.string));
					break;
				case JSON_TYPE_NUMBER:
					str8_pushf(arena, s("%g"), child->value.number);
					break;
				case JSON_TYPE_OBJECT: {
					*depth += 1;
					str8_pushf(arena, s("{\n"));
					json_write_children(arena, child->value.children, depth);
					*depth -= 1;
					str8_pushf(arena, s("%*s"), (*depth) * 4, "");
					str8_pushf(arena, s("}"));
				} break;
				case JSON_TYPE_ARRAY: {
					*depth += 1;
					str8_pushf(arena, s("[\n"));
					json_write_children(arena, child->value.children, depth);
					*depth -= 1;
					str8_pushf(arena, s("%*s"), (*depth) * 4, "");
					str8_pushf(arena, s("]"));
				} break;
				case JSON_TYPE_MAX:
					break;
			}

			if (index != children->count - 1)
				str8_pushf(arena, s(","));

			str8_pushf(arena, s("\n"));
		}
	}
}

String8 json_stringify(Arena *arena, JSON *json) {
	String8 result = { 0 };

	bool ok = arena && json;
	if (ok) {
		uint32_t depth = 1;
		JSON_Node *node = &json->root;

		JSON_Container *container = node->value.children;
		result.text = (uint8_t *)arena->base + arena->offset;

		str8_pushf(arena, s("{\n"));
		json_write_children(arena, container, &depth);
		str8_pushf(arena, s("}\n"));

		result.length = ((uint8_t *)arena->base + arena->offset) - result.text;
		arena_push_count(arena, uint8_t, 1);

		result.text[result.length] = '\0';
	}

	return result;
}
