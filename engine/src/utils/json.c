#include "json.h"

#include "core/debug.h"
#include "core/logger.h"

#include "core/strings.h"
#include "utils/lexer.h"

#include "os.h"

JSON_Node JSON_NIL = { 0 };

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

JSON_Value json_parse_object(Arena *arena, Lexer *lexer);
JSON_Value json_parse_array(Arena *arena, Lexer *lexer);

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

static inline JSON_Node *json_append(Arena *arena, JSON_Container **container) {
	JSON_Node *result = &JSON_NIL;

	bool ok = arena && container;
	if (ok) {
		JSON_Container *con = *container;
		if (con == 0) {
			con = *container = arena_push_count(arena, JSON_Container, 1);
			con->pages[0] = arena_push_count(arena, JSON_Node, JSON_CONTAINER_INITIAL_PAGE_SIZE);
		}

		JSON_ContainerSlot slot = json__locate(con->count);

		JSON_Node *page = con->pages[slot.page_index];
		if (page == 0)
			page = con->pages[slot.page_index] = arena_push_count(arena, JSON_Node, slot.page_size);

		result = &page[slot.page_local_index];
		con->count++;
	}

	return result;
}

static inline JSON_Value json_parse_value(Arena *arena, Lexer *lexer) {
	JSON_Value result = { 0 };

	bool ok = lexer;
	if (ok) {
		Token value = lexer_peek(lexer);

		switch (value.type) {
			case TOKEN_OPEN_BRACE: // OBJECT
				result = json_parse_object(arena, lexer);
				break;

			case TOKEN_OPEN_BRACKET: // ARRAY
				result = json_parse_array(arena, lexer);
				break;

			case TOKEN_PLUS:
			case TOKEN_MINUS:
				lexer_next(lexer);
				double mul = 1.0f - (value.type == TOKEN_MINUS) * 2.0f;

				value = lexer_expect_multiple(lexer, array_arg(TokenType, TOKEN_INTEGER, TOKEN_FLOAT));
				result = json_number(mul * str8_to_f64(value.lexeme));
				break;

			case TOKEN_FLOAT: // NUMBER
			case TOKEN_INTEGER:
				result = json_number(str8_to_f64(value.lexeme));
				lexer_next(lexer);
				break;

			case TOKEN_STRING: {
				result = json_string(arena, value.lexeme);
				lexer_next(lexer);
			} break;

			case JSON_KEYWORD_TRUE: // BOOL
			case JSON_KEYWORD_FALSE:
				result = json_bool((uint32_t)value.type == JSON_KEYWORD_TRUE);
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

JSON_Node *json_append_item(Arena *arena, JSON_Node *parent) {
	JSON_Node *result = &JSON_NIL;

	bool ok = arena && json_valid(parent) && parent->value.type == JSON_TYPE_ARRAY;
	if (ok)
		result = json_append(arena, &parent->value.as.children);

	return result;
}

JSON_Node *json_append_field(Arena *arena, JSON_Node *parent, String8 key) {
	JSON_Node *result = &JSON_NIL;

	bool ok = arena && json_valid(parent) && parent->value.type == JSON_TYPE_OBJECT;
	if (ok) {
		result = json_append(arena, &parent->value.as.children);
		result->key = str8_copy(arena, key);
	}

	return result;
}

JSON_Value json_parse_array(Arena *arena, Lexer *lexer) {
	JSON_Value result = { .type = JSON_TYPE_ARRAY }; // dummy node

	bool ok = arena && lexer;
	if (ok) {
		lexer_expect(lexer, TOKEN_OPEN_BRACKET);

		if (lexer_peek(lexer).type != TOKEN_CLOSE_BRACKET)
			do {
				json_append(arena, &result.as.children)->value = json_parse_value(arena, lexer);
			} while (lexer_match(lexer, TOKEN_COMMA, 0));
		Token close_brace = lexer_expect(lexer, TOKEN_CLOSE_BRACKET);
	}

	return result;
}

JSON_Value json_parse_object(Arena *arena, Lexer *lexer) {
	JSON_Value result = { .type = JSON_TYPE_OBJECT };

	bool ok = arena && lexer;
	if (ok) {
		Token open_brace = lexer_expect(lexer, TOKEN_OPEN_BRACE);

		if (lexer_peek(lexer).type != TOKEN_CLOSE_BRACE)
			do {
				Token key = lexer_expect(lexer, TOKEN_STRING);
				lexer_expect(lexer, TOKEN_COLON);

				JSON_Node *node = json_append(arena, &result.as.children);
				node->key = str8_copy(arena, key.lexeme);
				node->value = json_parse_value(arena, lexer);

			} while (lexer_match(lexer, TOKEN_COMMA, 0));

		Token close_brace = lexer_expect(lexer, TOKEN_CLOSE_BRACE);
	}

	return result;
}

JSON_Node *json_parse_string(Arena *arena, String8 source) {
	JSON_Node *result = &JSON_NIL;

	bool ok = arena;
	if (ok) {
		Lexer lexer = lexer_make(source, json_keyword_to_string, countof(json_keyword_to_string));
		result = arena_push_count(arena, JSON_Node, 1);
		result->value = json_parse_value(arena, &lexer);
	}

	return result;
}

JSON_Node *json_parse_file(Arena *arena, String8 path) {
	JSON_Node *result = &JSON_NIL;

	bool ok = arena;
	if (ok) {
		Lexer lexer = lexer_make(os_file_read_entire(arena, path), json_keyword_to_string, countof(json_keyword_to_string));
		result = arena_push_count(arena, JSON_Node, 1);
		result->value = json_parse_value(arena, &lexer);
	}

	return result;
}

JSON_Node *json_find(JSON_Node *node, String8 key) {
	JSON_Node *result = &JSON_NIL;

	bool ok = json_valid(node) && node->value.type == JSON_TYPE_OBJECT && node->value.as.children;
	if (ok) {
		JSON_Container *container = node->value.as.children;
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

JSON_Node *json_find_path(JSON_Node *node, String8 path) {
	JSON_Node *result = &JSON_NIL;

	bool ok = json_is_container(node) && node->value.as.children;
	if (ok) {
		Lexer lexer = lexer_make(path, 0, 0);
		Token token = { 0 };

		JSON_Node *cur = node;
		while ((token = lexer_next(&lexer)).type != TOKEN_EOF) {
			if (token.type == TOKEN_OPEN_BRACKET) {
				Token num = lexer_expect(&lexer, TOKEN_INTEGER);
				cur = json_child_at(cur, str8_to_u64(num.lexeme));
				lexer_expect(&lexer, TOKEN_CLOSE_BRACKET);
			} else if (token.type == TOKEN_IDENTIFIER)
				cur = json_find(cur, token.lexeme);
			else if (token.type == TOKEN_DOT) {
				Token key = lexer_expect(&lexer, TOKEN_IDENTIFIER);
				cur = json_find(cur, key.lexeme);
			}

			if (cur == &JSON_NIL) {
				cur = 0;
				break;
			}
		}

		if (cur)
			result = cur;
	}
	return result;
}

JSON_Node *json_child_at(JSON_Node *node, uint32_t index) {
	JSON_Node *result = &JSON_NIL;

	bool ok = json_valid(node) && node->value.type == JSON_TYPE_ARRAY && node->value.as.children;
	if (ok) {
		JSON_Container *container = node->value.as.children;
		JSON_ContainerSlot slot = json__locate(index);
		JSON_Node *page = container->pages[slot.page_index];
		if (page)
			result = &page[slot.page_local_index];
	}

	return result;
}

void json_write_children(Arena *arena, JSON_Container *children, String8 indent, bool keyed, uint32_t *depth) {
	bool ok = children;
	if (ok) {
		for (uint32_t index = 0; index < children->count; ++index) {
			JSON_ContainerSlot slot = json__locate(index);
			JSON_Node *child = &children->pages[slot.page_index][slot.page_local_index];

			str8_indent(arena, indent, *depth);
			if (keyed)
				str8_pushf(arena, s("\"%.*s\": "), str_spread(child->key));

			switch (child->value.type) {
				case JSON_TYPE_NULL:
					str8_pushf(arena, s("%s"), "null");
					break;
				case JSON_TYPE_BOOLEAN:
					str8_pushf(arena, s("%s"), child->value.as.boolean ? "true" : "false");
					break;
				case JSON_TYPE_STRING:
					str8_pushf(arena, s("\"%.*s\""), str_spread(child->value.as.string));
					break;
				case JSON_TYPE_NUMBER:
					str8_pushf(arena, s("%g"), child->value.as.number);
					break;
				case JSON_TYPE_OBJECT: {
					*depth += 1;
					str8_pushf(arena, s("{"));
					if (indent.length)
						str8_pushf(arena, s("\n"));
					json_write_children(arena, child->value.as.children, indent, true, depth);
					*depth -= 1;
					str8_indent(arena, indent, *depth);
					str8_pushf(arena, s("}"));
				} break;
				case JSON_TYPE_ARRAY: {
					*depth += 1;
					str8_pushf(arena, s("["));
					if (indent.length)
						str8_pushf(arena, s("\n"));
					json_write_children(arena, child->value.as.children, indent, false, depth);
					*depth -= 1;
					str8_indent(arena, indent, *depth);
					str8_pushf(arena, s("]"));
				} break;

				case JSON_TYPE_MAX:
					break;
			}

			if (index != children->count - 1)
				str8_pushf(arena, s(","));

			if (indent.length)
				str8_pushf(arena, s("\n"));
		}
	}
}

String8 json_stringify(Arena *arena, JSON_Node *root, String8 indent) {
	String8 result = { 0 };

	bool ok = arena && json_valid(root) && root->value.as.children;
	if (ok) {
		uint32_t depth = 1;

		result.text = (uint8_t *)arena->base + arena->offset;

		bool keyed = root->value.type == JSON_TYPE_OBJECT;

		str8_pushf(arena, s("%s"), keyed ? "{" : "[");
		if (indent.length)
			str8_pushf(arena, s("\n"));
		json_write_children(arena, root->value.as.children, indent, keyed, &depth);
		str8_pushf(arena, s("%s"), keyed ? "}" : "]");

		result.length = ((uint8_t *)arena->base + arena->offset) - result.text;

		arena_push_count(arena, uint8_t, 1);
		result.text[result.length] = '\0';
	}

	return result;
}
