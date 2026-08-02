#include "core/arena.h"
#include "core/debug.h"
#include "os.h"
#include "utils/lexer.h"
#include <core/logger.h>
#include <core/strings.h>
#include <stdio.h>

#define KEYWORD_LIST  \
	X(STRUCT, struct) \
	X(ENUM, enum)     \
	X(TYPEDEF, typedef)

typedef enum {
	TOKEN_0 = TOKEN_KEYWORD_0,
#define X(enum_name, name) TOKEN_##enum_name,
	KEYWORD_LIST
#undef X

		TOKEN_KEYWORD_MAX,
} KeywordEnumerator;

String8 keywords[TOKEN_KEYWORD_MAX - TOKEN_KEYWORD_0] = {
#define X(enum_name, name) [TOKEN_##enum_name - TOKEN_KEYWORD_0] = str_comp(#name),
	KEYWORD_LIST
#undef X
};

typedef enum {
	AST_NODE_TYPEDEF,

	AST_NODE_ENUM,
	AST_NODE_ENUMERATOR,
	AST_NODE_IDENTIFIER,

	AST_NODE_MAX,
} AST_Kind;

typedef struct AST_Node AST_Node;
struct AST_Node {
	AST_Kind kind;
	AST_Node *next;

	union {
		struct {
			String8 id;
			AST_Node *body;
		} named_decl; // typedef, enum

		struct {
			String8 id;
			String8 expr;
		} enumerator;

		String8 id;
	} value;
};

AST_Node *parse_enumarator(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	Token id = lexer_expect(lexer, TOKEN_IDENTIFIER);

	result = arena_push_count(arena, AST_Node, 1);
	result->kind = AST_NODE_ENUMERATOR;
	result->value.enumerator.id = id.lexeme;

	Token peek = lexer_peek(lexer);
	if (peek.type == TOKEN_EQUAL) {
		lexer_next(lexer); // consume equals
		uint8_t *head = lexer->cursor, *tail = lexer->cursor;
		while (lexer_at_end(lexer) == false) {
			tail = lexer->cursor - 1;

			Token look_ahead = lexer_peek(lexer);
			if (look_ahead.type == TOKEN_COMMA || look_ahead.type == TOKEN_CLOSE_BRACE) break;
			lexer_next(lexer);
		}
		while (tail[-1] == '\n' || tail[-1] == ' ' || tail[-1] == '\t')
			tail--;

		result->value.enumerator.expr = str8_range((char *)head, (char *)tail);
	}

	return result;
}

AST_Node *parse_enumarator_list(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	result = arena_push_count(arena, AST_Node, 1);
	result->kind = AST_NODE_ENUMERATOR;

	result = parse_enumarator(arena, lexer);
	AST_Node **tail = &result->next;

	while (lexer_match(lexer, TOKEN_COMMA, 0) && lexer_peek(lexer).type != TOKEN_CLOSE_BRACE) {
		AST_Node *enumerator = parse_enumarator(arena, lexer);

		(*tail) = enumerator;
		tail = &enumerator->next;
	}

	return result;
}

AST_Node *parse_enumerator_body(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	lexer_expect(lexer, TOKEN_OPEN_BRACE);
	result = parse_enumarator_list(arena, lexer);
	lexer_expect(lexer, TOKEN_CLOSE_BRACE);

	return result;
}

AST_Node *parse_enum_declaration(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	lexer_expect(lexer, (TokenType)TOKEN_ENUM);

	result = arena_push_count(arena, AST_Node, 1);
	result->kind = AST_NODE_ENUM;

	Token peek = lexer_peek(lexer);
	if (peek.type == TOKEN_IDENTIFIER) {
		lexer_next(lexer); // consume id
		result->value.named_decl.id = peek.lexeme;
	}

	result->value.named_decl.body = parse_enumerator_body(arena, lexer);

	return result;
}

AST_Node *parse_enum_specifier(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	Token peek = lexer_peek(lexer);
	if ((int32_t)peek.type == TOKEN_ENUM) {
		lexer_next(lexer); // consume enum

		result = arena_push_count(arena, AST_Node, 1);
		result->kind = AST_NODE_ENUM;

		peek = lexer_peek(lexer);
		if (peek.type == TOKEN_OPEN_BRACE) {
			result->value.named_decl.body = parse_enumerator_body(arena, lexer);
		} else if (peek.type == TOKEN_IDENTIFIER) {
			Token identifier = lexer_next(lexer); // consume id
			result->value.named_decl.id = identifier.lexeme;

			peek = lexer_peek(lexer);
			if (peek.type == TOKEN_OPEN_BRACE)
				result->value.named_decl.body = parse_enumerator_body(arena, lexer);
		}
	} else if (peek.type == TOKEN_IDENTIFIER) {
		lexer_next(lexer); // consume id

		result = arena_push_count(arena, AST_Node, 1);
		result->kind = AST_NODE_IDENTIFIER;
		result->value.id = peek.lexeme;
	}

	return result;
}

AST_Node *parse_typedef(Arena *arena, Lexer *lexer) {
	AST_Node *head = 0;
	AST_Node *tail = 0;

	while (lexer_match(lexer, TOKEN_EOF, 0) == false) {
		AST_Node *node = 0;

		Token peek = lexer_peek(lexer);
		if ((int32_t)peek.type == TOKEN_TYPEDEF) {
			lexer_next(lexer); // consume typedef

			peek = lexer_peek(lexer);
			if ((int32_t)peek.type == TOKEN_ENUM) {
				node = arena_push_count(arena, AST_Node, 1);
				node->kind = AST_NODE_TYPEDEF;
				node->value.named_decl.body = parse_enum_specifier(arena, lexer);

				// NOTE: This might be more than one. I never use this feature, so doesn't really matter,
				// but maybe handle that later.
				node->value.named_decl.id = lexer_expect(lexer, TOKEN_IDENTIFIER).lexeme;
			}
		} else if ((int32_t)peek.type == TOKEN_ENUM) { // ignore qualifiers
			node = parse_enum_declaration(arena, lexer);
		} else {
			lexer_next(lexer);
		}

		if (node && head) {
			ASSERT(tail);

			tail->next = node;
			tail = node;
		} else if (node)
			head = tail = node;
	}

	return head;
}

int main(void) {
	Arena arena[] = { arena_make(MiB(8)) };

	String8 source_file = os_file_read_entire(arena, s("engine/src/gfx/gfx_types.h"));
	Lexer lexer = lexer_make(source_file, keywords, TOKEN_KEYWORD_MAX - TOKEN_KEYWORD_0);

	AST_Node *tree = parse_typedef(arena, &lexer);
	LOG_INFO("Parsed");

	String8 indent_string = s("  ");
	for (AST_Node *node = tree; node; node = node->next) { // all typedefs
		ASSERT(node->kind == AST_NODE_TYPEDEF || node->kind == AST_NODE_ENUM);

		AST_Node *e = node;
		uint32_t depth = 0;

		if (node->kind == AST_NODE_TYPEDEF) {
			LOG_INFO("#typedef [%.*s] {", str_spread(node->value.id));
			e = node->value.named_decl.body;

			depth++;
		}

		for (uint32_t index = 0; index < depth; ++index)
			printf("%.*s", str_spread(indent_string));
		LOG_INFO("#enum [%.*s] {", str_spread(e->value.id));

		depth++;
		for (AST_Node *child = e->value.named_decl.body; child; child = child->next) {
			ASSERT(child->kind = AST_NODE_ENUMERATOR);

			for (uint32_t index = 0; index < depth; ++index)
				printf("%.*s", str_spread(indent_string));
			LOG_INFO("#%.*s -> %.*s", str_spread(child->value.enumerator.id), str_spread(child->value.enumerator.expr));
			/* if (child->as.enumerator.expr.length) { */
			/*     ASSERT(child->as.enumerator.expr.text[child->as.enumerator.expr.length - 1] != '\n'); */
			/* } */
		}
		depth--;

		for (uint32_t index = 0; index < depth; ++index)
			printf("%.*s", str_spread(indent_string));
		LOG_INFO("#}");

		if (e != node)
			LOG_INFO("#}");
		depth--;
	}

	/*
		   type_declaration   -> "typedef" enum_specifier IDENTIFIER ";" | enum_definition ";" ;
		   enum_specifier     -> "enum" IDENTIFIER? enum_body | "enum" IDENTIFIER | IDENTIFIER ;
		   enum_definition    -> "enum" IDENTIFIER? enum_body;
		   enum_body          -> "{" enumerator_list }"
		   enumerator_list    -> enumerator ( "," enumerator )* ","?;
		   enumerator         -> IDENTIFIER ( "=" constant_expression )? ;
	*/
	arena_destroy(arena);
	return 0;
}
