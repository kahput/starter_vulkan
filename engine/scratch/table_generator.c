#include "core/arena.h"
#include "core/debug.h"
#include "core/strings.h"
#include "os.h"
#include "utils/lexer.h"
#include <core.h>

#define KEYWORD_LIST  \
	X(TABLE, "table") \
	X(ENUM, "enum")   \
	X(ARRAY, "array") \
	X(FOR, "for")     \
	X(IN, "in")

// clang-format off
typedef enum {
#define X(name, display) KEYWORD_##name,
    KEYWORD_LIST
#undef X

    KEYWORD_MAX,
} Keyword;

String8 keyword_to_string[KEYWORD_MAX] = {
#define X(name, display) [KEYWORD_##name] = str_comp(display),
    KEYWORD_LIST
#undef X
};
// clang-format on

typedef enum {
	AST_NODE_TYPE_TABLE,
	AST_NODE_TYPE_ROW,
	AST_NODE_TYPE_LITERAL,

	AST_NODE_MAX,
} AST_NodeType;

typedef enum {
	LIT_TYPE_NIL,
	LIT_TYPE_INTEGER,
	LIT_TYPE_REAL,
	LIT_TYPE_BOOL,
	LIT_TYPE_STRING,
	LIT_TYPE_IDENTIFIER,
} LiteralType;

typedef struct {
	LiteralType type;

	union {
		String8 id;
		String8 string;
		double real;
		int64_t integer;
	} as;
} Literal;

typedef struct AST_Node AST_Node;
struct AST_Node {
	AST_Node *next;
	AST_NodeType type;

	union {
		struct {
			String8 id;

			AST_Node *columns;
			uint32_t column_count;

			AST_Node *rows;
			uint32_t row_count;
		} table;
		AST_Node *rows;

		struct {
			String8 id;
		} specifier;

		Literal literal;
	} as;
};

Literal ast_parse_literal(Arena *arena, Lexer *lexer) {
	Token lit_token = lexer_expect_multiple(lexer, array_arg(TokenType, TOKEN_INTEGER, TOKEN_FLOAT, TOKEN_STRING, TOKEN_IDENTIFIER));
	Literal lit = { 0 };
	switch (lit_token.type) {
		case TOKEN_FLOAT:
			lit.type = LIT_TYPE_REAL;
			lit.as.real = str8_to_f64(lit_token.lexeme);
			break;

		case TOKEN_INTEGER:
			lit.type = LIT_TYPE_INTEGER;
			lit.as.integer = str8_to_s64(lit_token.lexeme);
			break;

		case TOKEN_IDENTIFIER:
			lit.type = LIT_TYPE_IDENTIFIER;
			lit.as.id = lit_token.lexeme;
			break;

		case TOKEN_STRING:
			lit.type = LIT_TYPE_STRING;
			lit.as.string = lit_token.lexeme;
			break;
		default:
			ASSERT_FORMAT("Invalid literal in table 'TODO' at %d:%d", , lit_token.line, lit_token.column);
			break;
	}

	return lit;
}

AST_Node *ast_parse_rows(Arena *arena, Lexer *lexer, uint32_t expected_column_count, uint32_t *row_count) {
	AST_Node *result = 0;

	AST_Node **tail = 0;
	while (lexer_at_end(lexer) == false && lexer_peek(lexer).type != TOKEN_CLOSE_BRACE) {
		lexer_expect(lexer, TOKEN_OPEN_PAREN);

		AST_Node *row = arena_push_count(arena, AST_Node, 1);
		row->type = AST_NODE_TYPE_ROW;
		row->as.rows = arena_push_count(arena, AST_Node, expected_column_count);
		uint32_t column_count = 0;

		while (lexer_match(lexer, TOKEN_CLOSE_PAREN, 0) == false) {
			row->as.rows[column_count++] = (AST_Node){
				.type = AST_NODE_TYPE_LITERAL,

				.as.literal = ast_parse_literal(arena, lexer),
			};

			ASSERT(column_count <= expected_column_count);
		}

		if (result) {
			*tail = row;
			tail = &row->next;
		} else {
			result = row;
			tail = &result->next;
		}

		*row_count += 1;
	}

	return result;
}

AST_Node *ast_parse_columns(Arena *arena, Lexer *lexer, uint32_t *column_count) {
	AST_Node *result = 0;
	lexer_expect(lexer, TOKEN_OPEN_PAREN);

	AST_Node **tail = 0;
	while (lexer_match(lexer, TOKEN_CLOSE_PAREN, 0) == false) {
		AST_Node *specifier = arena_push_count(arena, AST_Node, 1);
		specifier->as.specifier.id = lexer_expect(lexer, TOKEN_IDENTIFIER).lexeme;

		if (result) {
			*tail = specifier;
			tail = &specifier->next;
		} else {
			result = specifier;
			tail = &result->next;
		}

		*column_count = *column_count + 1;
		lexer_match(lexer, TOKEN_COMMA, 0); // consume comma if not last
	}

	return result;
}

AST_Node *ast_parse_table(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	lexer_expect(lexer, TOKEN_KEYWORD_0 + KEYWORD_TABLE);
	Token id = lexer_expect(lexer, TOKEN_IDENTIFIER);

	result = arena_push_count(arena, AST_Node, 1);
	result->as.table.id = id.lexeme;
	result->as.table.columns = ast_parse_columns(arena, lexer, &result->as.table.column_count);

	lexer_expect(lexer, TOKEN_OPEN_BRACE);
	result->as.table.rows = ast_parse_rows(arena, lexer, result->as.table.column_count, &result->as.table.row_count);
	lexer_expect(lexer, TOKEN_CLOSE_BRACE);

	return result;
}

int main(void) {
	Arena arena[] = { arena_make(MiB(8)) };

	String8 source = os_file_read_entire(arena, s("assets/example.table"));
	Lexer lexer[] = { lexer_make(source, keyword_to_string, KEYWORD_MAX) };
	AST_Node *table = ast_parse_table(arena, lexer);

	LOG_INFO("#%.*s", str_spread(source));

	/*

	table -> "table" IDENTIFIER column_specifier_list? "{" table_row* "}" ;
	column_specifier_list -> "(" IDENTIFIER ("," IDENTIFIER)* ")";
	table_row -> "(" literal+ ")" ;

	literal -> IDENTIFIER | INTEGER | FLOAT | STRING ;
	*/

	arena_destroy(arena);
	return 0;
}
