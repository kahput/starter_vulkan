#include "core/arena.h"
#include "core/debug.h"
#include "core/strings.h"
#include "os.h"
#include "utils/lexer.h"
#include <core.h>
#include <stdio.h>

#define KEYWORD_LIST  \
	X(TABLE, "table") \
	X(ENUM, "enum")   \
	X(ARRAY, "array") \
	X(FOR, "for")     \
	X(IN, "in")

// clang-format off
typedef enum {
#define X(name, display) TOKEN_##name,
    KEYWORD_LIST
#undef X

    TOKEN_KEYWORD_MAX,
} Keyword;

String8 keyword_to_string[TOKEN_KEYWORD_MAX] = {
#define X(name, display) [TOKEN_##name] = str_comp(display),
    KEYWORD_LIST
#undef X
};
// clang-format on

typedef enum {
	AST_NODE_TABLE,

	AST_NODE_COLUMNS,
	AST_NODE_ROWS,

	AST_NODE_FIELD,
	AST_NODE_ENTRY,
	AST_NODE_LITERAL,

	AST_NODE_GEN_ENUM,
	AST_NODE_BLOCK,
	AST_NODE_FOR,

	AST_NODE_EXPR,

	AST_NODE_MAX,
} AST_NodeType;

typedef enum {
	LIT_TYPE_INTEGER,
	LIT_TYPE_REAL,
	LIT_TYPE_STRING,
	LIT_TYPE_IDENTIFIER,
} LiteralType;

typedef struct {
	LiteralType type;

	union {
		String8 string;
		double real;
		int64_t integer;
	} as;
} Literal;

typedef struct AST_Node AST_Node;
struct AST_Node {
	AST_NodeType type;

	// Tree
	AST_Node *parent;
	AST_Node *first_child, *last_child;
	AST_Node *next_sibling, *prev_sibling;

	String8 name;
	Literal lit;
};

AST_Node *ast_make(Arena *arena, AST_NodeType type) {
	AST_Node *result = arena_push_count(arena, AST_Node, 1);
	result->type = type;

	return result;
}

bool ast_unparent(AST_Node *child) {
	bool ok = child && child->parent;
	if (ok) {
		AST_Node *parent = child->parent;
		if (parent->first_child == parent->last_child) {
			parent->first_child = parent->last_child = 0;
		} else {
			child->prev_sibling->next_sibling = child->next_sibling;
			child->next_sibling->prev_sibling = child->prev_sibling;

			if (parent->first_child == child)
				parent->first_child = child->next_sibling;
			if (parent->last_child == child)
				parent->last_child = child->prev_sibling;
		}

		child->parent = 0;
		child->next_sibling = child->prev_sibling = 0;
	}

	return ok;
}

bool ast_push(AST_Node *parent, AST_Node *child, bool front) {
	bool ok = parent && child && child != parent;
	if (ok) {
		ast_unparent(child);

		if (parent->first_child == 0) {
			parent->first_child = parent->last_child = child;
			child->next_sibling = child->prev_sibling = child;
		} else {
			child->prev_sibling = parent->last_child;
			child->next_sibling = parent->first_child;

			parent->last_child->next_sibling = child;
			parent->first_child->prev_sibling = child;

			if (front)
				parent->first_child = child;
			else
				parent->last_child = child;
		}

		child->parent = parent;
	}

	return ok;
}

bool ast_pushback(AST_Node *parent, AST_Node *child) { return ast_push(parent, child, false); }
bool ast_pushfront(AST_Node *parent, AST_Node *child) { return ast_push(parent, child, true); }

bool match_impl(Lexer *lexer, TokenType *token_types, uint32_t token_count) {
	bool ok = lexer && token_types && token_count;
	if (ok) {
		Token peek = lexer_peek(lexer);

		ok = false;
		for (uint32_t index = 0; index < token_count; ++index) {
			if (peek.type == token_types[index]) {
				ok = true;
				lexer_advance(lexer); // consume token
				break;
			}
		}
	}

	return ok;
}

bool match_keyword_impl(Lexer *lexer, const Keyword *in_keywords, uint32_t keyword_count) {
	bool ok = lexer && in_keywords;
	if (ok) {
		ArenaTemp scratch = arena_scratch_begin(0);
		Keyword *keywords = arena_push_count(scratch.arena, Keyword, keyword_count);
		memory_copy_count(keywords, in_keywords, keyword_count);

		for (uint32_t index = 0; index < keyword_count; ++index)
			keywords[index] += TOKEN_KEYWORD_0;

		ok = match_impl(lexer, (TokenType *)keywords, keyword_count);
		arena_scratch_end(scratch);
	}

	return ok;
}

#define match(...) match_impl(lexer, array_arg(TokenType, __VA_ARGS__))
#define match_keyword(...) match_keyword_impl(lexer, array_arg(Keyword, __VA_ARGS__))

AST_Node *ast_parse_literal(Arena *arena, Lexer *lexer) {
	AST_Node *result = ast_make(arena, AST_NODE_LITERAL);

	Token lit_token = lexer_expect_multiple(lexer, array_arg(TokenType, TOKEN_INTEGER, TOKEN_REAL, TOKEN_STRING, TOKEN_IDENTIFIER));
	switch (lit_token.type) {
		case TOKEN_REAL:
			result->lit.type = LIT_TYPE_REAL;
			result->lit.as.real = str8_to_f64(lit_token.lexeme);
			break;

		case TOKEN_INTEGER:
			result->lit.type = LIT_TYPE_INTEGER;
			result->lit.as.integer = str8_to_s64(lit_token.lexeme);
			break;

		case TOKEN_STRING:
			result->lit.type = LIT_TYPE_STRING;
			result->lit.as.string = lit_token.lexeme;
			break;

		case TOKEN_IDENTIFIER:
			result->lit.type = LIT_TYPE_IDENTIFIER;
			result->lit.as.string = lit_token.lexeme;
			break;

		default:
			ASSERT_FORMAT("Invalid literal in table 'TODO' at %d:%d", , lit_token.line, lit_token.column);
			break;
	}

	return result;
}

AST_Node *ast_parse_entry(Arena *arena, Lexer *lexer) {
	AST_Node *result = ast_make(arena, AST_NODE_ENTRY);

	lexer_consume(lexer, TOKEN_LPAREN, s("Expect '(' before row."));
	do {
		ast_pushback(result, ast_parse_literal(arena, lexer));
	} while (match(TOKEN_RPAREN) == false);

	return result;
}

AST_Node *ast_parse_columns(Arena *arena, Lexer *lexer) {
	AST_Node *result = ast_make(arena, AST_NODE_COLUMNS);
	lexer_consume(lexer, TOKEN_LPAREN, s("Expect '(' after table name."));

	do {
		AST_Node *spec = ast_make(arena, AST_NODE_FIELD);
		spec->name = lexer_consume(lexer, TOKEN_IDENTIFIER, s("Expect column name.")).lexeme;
		ast_pushback(result, spec);
	} while (match(TOKEN_COMMA));

	lexer_consume(lexer, TOKEN_RPAREN, s("Expect ')' after table column specifiers."));

	return result;
}

AST_Node *ast_parse_table(Arena *arena, Lexer *lexer) {
	AST_Node *result = ast_make(arena, AST_NODE_TABLE);

	result->name = lexer_consume(lexer, TOKEN_IDENTIFIER, s("Expect table name after column specifiers.")).lexeme;
	ast_pushback(result, ast_parse_columns(arena, lexer));
	ast_pushback(result, ast_make(arena, AST_NODE_ROWS));

	lexer_consume(lexer, TOKEN_LBRACE, s("Expect '{' after column specifiers."));
	while (match(TOKEN_RBRACE) == false) {
		ast_pushback(result->last_child, ast_parse_entry(arena, lexer));
	}

	return result;
}

AST_Node *ast_parse_block(Arena *arena, Lexer *lexer);

AST_Node *ast_parse_for(Arena *arena, Lexer *lexer) {
	AST_Node *result = ast_make(arena, AST_NODE_FOR);

	ast_pushback(result, ast_make(arena, AST_NODE_LITERAL));
	result->last_child->lit = (Literal){
		.type = LIT_TYPE_IDENTIFIER,
		.as.string = lexer_consume(lexer, TOKEN_IDENTIFIER, s("Expect binding after 'for'.")).lexeme
	};

	lexer_consume(lexer, TOKEN_IN + TOKEN_KEYWORD_0, s("Expect 'in' after binding."));
	ast_pushback(result, ast_make(arena, AST_NODE_LITERAL));
	result->last_child->lit = (Literal){
		.type = LIT_TYPE_IDENTIFIER,
		.as.string = lexer_consume(lexer, TOKEN_IDENTIFIER, s("Expect table identifier after 'in'.")).lexeme
	};

	if (match(TOKEN_LBRACE)) {
		ast_pushback(result, ast_parse_block(arena, lexer));
	}

	return result;
}

AST_Node *ast_parse_interop(Arena *arena, Lexer *lexer) {
	AST_Node *result = ast_make(arena, AST_NODE_EXPR);

	ast_pushback(result, ast_make(arena, AST_NODE_LITERAL));
	result->last_child->lit = (Literal){
		.type = LIT_TYPE_IDENTIFIER,
		.as.string = lexer_consume(lexer, TOKEN_IDENTIFIER, s("Expect variable identifier.")).lexeme
	};

	lexer_consume(lexer, TOKEN_DOT, s("Expecgt '.' after identifier."));
	ast_pushback(result, ast_make(arena, AST_NODE_LITERAL));
	result->last_child->lit = (Literal){
		.type = LIT_TYPE_IDENTIFIER,
		.as.string = lexer_consume(lexer, TOKEN_IDENTIFIER, s("Expect column identifier.")).lexeme
	};
	return result;
}

AST_Node *ast_parse_block(Arena *arena, Lexer *lexer) {
	AST_Node *result = ast_make(arena, AST_NODE_BLOCK);

	uint32_t brace_depth = 1;
	while (lexer_at_end(lexer) == false) {
		if (match(TOKEN_LBRACE)) brace_depth++;
		if (match(TOKEN_RBRACE)) brace_depth--;
		if (brace_depth == 0) break;

		bool parsed = false;
		if (match(TOKEN_DOLLAR)) {
			if (match_keyword(TOKEN_FOR)) {
				ast_pushback(result, ast_parse_for(arena, lexer));
				parsed = true;
			} else if (match(TOKEN_LBRACE)) {
				ast_pushback(result, ast_parse_interop(arena, lexer));
				lexer_consume(lexer, TOKEN_RBRACE, s("Expect '}' after interop."));
				parsed = true;
			}
		}

		if (parsed == false) {
			ast_pushback(result, ast_make(arena, AST_NODE_LITERAL));
			result->last_child->lit = (Literal){
				.type = LIT_TYPE_STRING,
				.as.string = lexer_advance(lexer).lexeme,
			};
		}
	}

	return result;
}

AST_Node *ast_parse_enum(Arena *arena, Lexer *lexer) {
	AST_Node *result = ast_make(arena, AST_NODE_GEN_ENUM);

	result->name = lexer_consume(lexer, TOKEN_IDENTIFIER, s("Expect name after 'enum'.")).lexeme;
	if (match(TOKEN_LBRACE))
		ast_pushback(result, ast_parse_block(arena, lexer));

	return result;
}

void ast_visit(AST_Node *node, uint32_t indent_level) {
	for (uint32_t index = 0; index < indent_level; ++index)
		printf("  ");

	switch (node->type) {
		case AST_NODE_TABLE:
			printf("TABLE(%.*s)\n", str_spread(node->name));
			ast_visit(node->first_child, indent_level + 1);
			ast_visit(node->last_child, indent_level + 1);
			break;
		case AST_NODE_COLUMNS:
			printf("COLUMNS\n");
			AST_Node *column = node->first_child;
			if (column) do {
					ast_visit(column, indent_level + 1);
					column = column->next_sibling;
				} while (column != node->first_child);
			break;
		case AST_NODE_ROWS: {
			printf("ROWS\n");
			AST_Node *entry = node->first_child;
			if (entry) do {
					ast_visit(entry, indent_level + 1);
					entry = entry->next_sibling;
				} while (entry != node->first_child);
		} break;
		case AST_NODE_FIELD:
			printf("FIELD(%.*s)\n", str_spread(node->name));
			break;
		case AST_NODE_ENTRY: {
			printf("ENTRY\n");
			AST_Node *val = node->first_child;
			if (val) do {
					ast_visit(val, indent_level + 1);
					val = val->next_sibling;
				} while (val != node->first_child);
		} break;
		case AST_NODE_LITERAL: {
			// clang-format off
			switch (node->lit.type) {
				case LIT_TYPE_INTEGER: printf("INTEGER(%ld)", node->lit.as.integer); break;
				case LIT_TYPE_REAL: printf("REAL(%g)", node->lit.as.real); break;
				case LIT_TYPE_STRING: printf("STRING(%.*s)", str_spread(node->lit.as.string)); break;
                case LIT_TYPE_IDENTIFIER: printf("IDENTIFER(%.*s)", str_spread(node->lit.as.string)); break;
					break;
			}
			// clang-format on
			printf("\n");
		} break;
		case AST_NODE_GEN_ENUM: {
			printf("GEN_ENUM\n");
			AST_Node *val = node->first_child;
			if (node->first_child) ast_visit(node->first_child, indent_level + 1);
		} break;
		case AST_NODE_BLOCK: {
			printf("BLOCK\n");
			AST_Node *stmt = node->first_child;
			if (stmt) do {
					ast_visit(stmt, indent_level + 1);
					stmt = stmt->next_sibling;
				} while (stmt != node->first_child);

		} break;
		case AST_NODE_FOR: {
			printf("FOR_STMT\n");
			AST_Node *c = node->first_child;
			if (c) do {
					ast_visit(c, indent_level + 1);
					c = c->next_sibling;
				} while (c != node->first_child);
		} break;
		case AST_NODE_EXPR: {
			printf("EXPR\n");
			ast_visit(node->first_child, indent_level + 1);
			ast_visit(node->last_child, indent_level + 1);
		} break;

		default:
			break;
	}
}

bool ast_validate_table(AST_Node *table) {
	uint32_t column_count = 0;

	bool ok = table && table->type == AST_NODE_TABLE;
	if (ok) {
		AST_Node *columns = table->first_child;

		AST_Node *column = columns->first_child;
		if (column) do {
				column_count++;
				column = column->next_sibling;
			} while (column != columns->first_child);

		ok = column_count > 0;
	}

	if (ok) {
		AST_Node *rows = table->last_child;

		AST_Node *row = rows->first_child;
		uint32_t row_index = 0;
		if (row) do {
				uint32_t row_entry_count = 0;
				AST_Node *entry = row->first_child;
				if (entry) do {
						row_entry_count++;
						entry = entry->next_sibling;
					} while (entry != row->first_child);

				if (row_entry_count != column_count) {
					LOG_ERROR("table '%.*s' invalid entry at row %d", str_spread(table->name), row_index);
					ok = false;
					break;
				}

				row = row->next_sibling;
				row_index += 1;
			} while (row != rows->first_child);
	}

	return ok;
}

int main(void) {
	Arena arena[] = { arena_make(MiB(8)) };

	String8 source = os_file_read_entire(arena, s("assets/example.table"));
	Lexer lexer[] = { lexer_make(source, keyword_to_string, TOKEN_KEYWORD_MAX) };

	/* AST_Node *tables[256] = { 0 }; */
	/* uint32_t table_count = 0; */

	while (lexer_at_end(lexer) == false) {
		if (match_keyword(TOKEN_TABLE)) {
			AST_Node *table = ast_parse_table(arena, lexer);
			if (ast_validate_table(table)) {
				/* tables[table_count++] = table; */
				ast_visit(table, 0);
			}
		}
		if (match_keyword(TOKEN_ENUM)) {
			AST_Node *enumeration = ast_parse_enum(arena, lexer);
			ast_visit(enumeration, 0);
		}

		lexer_advance(lexer);
	}

	/*

	table -> "table" IDENTIFIER column_specifier_list? "{" table_list? "}" ;
	column_specifier_list -> "(" IDENTIFIER ("," IDENTIFIER)* ")";
	table_list -> table_row+
	table_row -> "(" literal+ ")" ;

	literal -> INTEGER | FLOAT | TEXT ;
	*/

	arena_destroy(arena);
	return 0;
}
