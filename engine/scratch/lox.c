#include "core/arena.h"
#include "core/debug.h"
#include "core/strings.h"
#include "os.h"
#include <core.h>
#include <stdio.h>
#include <utils/lexer.h>

#define KEYWORD_LIST  \
	X(FALSE, "false") \
	X(TRUE, "true")   \
	X(NIL, "nil")     \
	X(PRINT, "print") \
	X(VAR, "var")     \
	X(IF, "if")       \
	X(ELSE, "else")   \
	X(AND, "and")     \
	X(OR, "or")       \
	X(FOR, "for")

typedef enum {
#define X(name, key) TOKEN_##name,
	KEYWORD_LIST
#undef X
		TOKEN_KEYWORD_MAX,
} AST_KeywordToken;

String8 keyword_to_string[TOKEN_KEYWORD_MAX] = {
#define X(name, key) [TOKEN_##name] = str_comp(key),
	KEYWORD_LIST
#undef X
};

TokenType keyword_to_token[TOKEN_KEYWORD_MAX] = {
#define X(name, key) [TOKEN_##name] = TOKEN_KEYWORD_0 + TOKEN_##name,
	KEYWORD_LIST
#undef X
};

typedef enum {
	AST_NODE_EXPR_ASSIGN,
	AST_NODE_EXPR_BINARY,
	AST_NODE_EXPR_LOGICAL,
	AST_NODE_EXPR_UNARY,
	AST_NODE_EXPR_GROUPING,
	AST_NODE_EXPR_PRIMARY,
	AST_NODE_EXPR_MAX,

	AST_NODE_STMT_PRINT = AST_NODE_EXPR_MAX,
	AST_NODE_STMT_IF,
	AST_NODE_STMT_FOR,
	AST_NODE_STMT_BLOCK,
	AST_NODE_STMT_EXPR,

	AST_NODE_DECL_VAR,
	AST_NODE_DECL_LIST,

	AST_NODE_PROGRAM,

	AST_NODE_MAX,
} AST_NodeType;

typedef enum {
	AST_LITERAL_NIL,
	AST_LITERAL_STRING,
	AST_LITERAL_REAL,
	AST_LITERAL_BOOLEAN,
	AST_LITERAL_VARIABLE,

	AST_LITERAL_MAX,
} AST_LiteralType;

typedef struct AST_Literal AST_Literal;
struct AST_Literal {
	AST_LiteralType type;
	union {
		String8 string;
		Token identifier;
		double real;
		bool boolean;
	} as;
};

typedef struct AST_Node AST_Node;
struct AST_Node {
	AST_NodeType type;

	// Tree
	AST_Node *parent;
	AST_Node *first_child, *last_child;
	AST_Node *next_sibling, *prev_sibling;

	Token operator, identifier;
	AST_Literal literal;
};

bool ast_remove_parent(AST_Node *child) {
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
		ast_remove_parent(child);

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

static inline bool ast_pushback(AST_Node *parent, AST_Node *child) { return ast_push(parent, child, false); }
static inline bool ast_pushfront(AST_Node *parent, AST_Node *child) { return ast_push(parent, child, true); }

bool ast_match_impl(Lexer *lexer, TokenType *token_types, uint32_t token_count) {
	bool ok = lexer && token_types && token_count;
	if (ok) {
		Token peek = lexer_peek(lexer);

		ok = false;
		for (uint32_t index = 0; index < token_count; ++index) {
			if (peek.type == token_types[index]) {
				ok = true;
				break;
			}
		}
	}

	return ok;
}

bool ast_match_keyword_impl(Lexer *lexer, AST_KeywordToken *keywords, uint32_t keyword_count) {
	bool ok = lexer && keywords;
	if (ok)
		for (uint32_t index = 0; index < keyword_count; ++index)
			keywords[index] += TOKEN_KEYWORD_0;

	return ast_match_impl(lexer, (TokenType *)keywords, keyword_count);
}

#define ast_match(l, ...) ast_match_impl((l), array_arg(TokenType, __VA_ARGS__))
#define ast_match_keyword(l, ...) ast_match_keyword_impl((l), array_arg(AST_KeywordToken, __VA_ARGS__))

#define MAX_VARIABLES 256
typedef struct {
	String8 key;
	AST_Literal value;
} KeyValue;
typedef struct {
	KeyValue vars[MAX_VARIABLES];
	uint32_t var_count;
} Enviroment;

KeyValue *env_find(Enviroment *env, String8 key, bool ensure) {
	KeyValue *result = 0;

	bool ok = env && key.length;
	if (ok) {
		for (uint32_t index = 0; index < env->var_count; ++index) {
			if (str8_equals(env->vars[index].key, key)) {
				result = &env->vars[index];
				break;
			}
		}

		ok = result || env->var_count < countof(env->vars);
	}

	if (result == 0 && ok && ensure) result = &env->vars[env->var_count++];

	return result;
}

AST_Literal env_find_val(Enviroment *env, String8 key) {
	KeyValue *kv = env_find(env, key, false);
	ASSERT_FORMAT(kv != 0, "Undefined variable '%.*s'", str_spread(key));

	return kv->value;
}

AST_Literal env_assign(Enviroment *env, String8 key, AST_Literal value) {
	KeyValue *var = env_find(env, key, false);
	ASSERT_FORMAT(var != 0, "Undefined variable '%.*s'", str_spread(key));

	var->value = value;
	return var->value;
}

void env_define(Enviroment *env, String8 key, AST_Literal value) {
	KeyValue *var = env_find(env, key, true);
	if (var) {
		var->key = key;
		var->value = value;
	}
}
AST_Node *ast_parse_stmt(Arena *arena, Lexer *lexer);
AST_Node *ast_parse_stmt_expr(Arena *arena, Lexer *lexer);
AST_Node *ast_parse_stmt_block(Arena *arena, Lexer *lexer);

AST_Node *ast_parse_decl(Arena *arena, Lexer *lexer);
AST_Node *ast_parse_decl_list(Arena *arena, Lexer *lexer);

AST_Node *ast_parse_expr(Arena *arena, Lexer *lexer);
AST_Node *ast_parse_expr_comma(Arena *arena, Lexer *lexer); // ","
AST_Node *ast_parse_expr_assignment(Arena *arena, Lexer *lexer); // "="
AST_Node *ast_parse_expr_or(Arena *arena, Lexer *lexer); // "or" "||"
AST_Node *ast_parse_expr_and(Arena *arena, Lexer *lexer); // "and" "&&"
AST_Node *ast_parse_expr_equality(Arena *arena, Lexer *lexer); // "==" "!="
AST_Node *ast_parse_expr_comparison(Arena *arena, Lexer *lexer); // ">" ">=" "<" "<="
AST_Node *ast_parse_expr_term(Arena *arena, Lexer *lexer); // "+" "-"
AST_Node *ast_parse_expr_factor(Arena *arena, Lexer *lexer); // "*" "/"
AST_Node *ast_parse_expr_unary(Arena *arena, Lexer *lexer); // "!" "-"
AST_Node *ast_parse_expr_primary(Arena *arena, Lexer *lexer);

AST_Node *ast_binary(Arena *arena, AST_Node *left, Token operator, AST_Node *right) {
	AST_Node *result = 0;

	bool ok = arena && left && right;
	if (ok) {
		result = arena_push_count(arena, AST_Node, 1);
		result->type = AST_NODE_EXPR_BINARY;
		result->operator = operator;

		ast_pushback(result, left);
		ast_pushback(result, right);
	}

	return result;
}

AST_Node *ast_logical(Arena *arena, AST_Node *left, Token operator, AST_Node *right) {
	AST_Node *result = 0;

	bool ok = arena && left && right;
	if (ok) {
		result = arena_push_count(arena, AST_Node, 1);
		result->type = AST_NODE_EXPR_LOGICAL;
		result->operator = operator;

		ast_pushback(result, left);
		ast_pushback(result, right);
	}

	return result;
}

AST_Node *ast_unary(Arena *arena, Token operator, AST_Node *right) {
	AST_Node *result = 0;

	bool ok = arena && right;
	if (ok) {
		result = arena_push_count(arena, AST_Node, 1);
		result->type = AST_NODE_EXPR_UNARY;
		result->operator = operator;

		ast_pushback(result, right);
	}

	return result;
}

AST_Node *ast_literal(Arena *arena, AST_Literal literal) {
	AST_Node *result = 0;
	bool ok = arena;
	if (ok) {
		result = arena_push_count(arena, AST_Node, 1);
		result->type = AST_NODE_EXPR_PRIMARY;

		result->literal = literal;
	}

	return result;
}

AST_Node *ast_grouping(Arena *arena, AST_Node *expr) {
	AST_Node *result = 0;

	bool ok = arena;
	if (ok) {
		result = arena_push_count(arena, AST_Node, 1);
		result->type = AST_NODE_EXPR_GROUPING;

		ast_pushback(result, expr);
	}

	return result;
}
AST_Node *ast_assign(Arena *arena, Token identifier, AST_Node *value) {
	AST_Node *result = 0;

	bool ok = arena && value;
	if (ok) {
		result = arena_push_count(arena, AST_Node, 1);
		result->type = AST_NODE_EXPR_ASSIGN;
		result->identifier = identifier;

		ast_pushback(result, value);
	}

	return result;
}

AST_Node *ast_parse_expr_primary(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		if (ast_match(lexer, TOKEN_OPEN_PAREN)) {
			lexer_next(lexer);
			result = ast_grouping(arena, ast_parse_expr(arena, lexer));
			lexer_consume(lexer, TOKEN_CLOSE_PAREN, s("Expect ')' after expression"));
		} else if (ast_match(lexer, TOKEN_STRING))
			result = ast_literal(arena, (AST_Literal){ .type = AST_LITERAL_STRING, .as.string = lexer_next(lexer).lexeme });
		else if (ast_match(lexer, TOKEN_FLOAT))
			result = ast_literal(arena, (AST_Literal){ .type = AST_LITERAL_REAL, .as.real = str8_to_f64(lexer_next(lexer).lexeme) });
		else if (ast_match(lexer, TOKEN_INTEGER))
			result = ast_literal(arena, (AST_Literal){ .type = AST_LITERAL_REAL, .as.real = str8_to_s64(lexer_next(lexer).lexeme) });
		else if (ast_match_keyword(lexer, TOKEN_NIL)) {
			lexer_next(lexer);
			result = ast_literal(arena, (AST_Literal){ .type = AST_LITERAL_NIL });
		} else if (ast_match_keyword(lexer, TOKEN_TRUE)) {
			result = ast_literal(arena, (AST_Literal){ .type = AST_LITERAL_BOOLEAN, .as.boolean = true });
			lexer_next(lexer);
		} else if (ast_match_keyword(lexer, TOKEN_FALSE)) {
			lexer_next(lexer);
			result = ast_literal(arena, (AST_Literal){ .type = AST_LITERAL_BOOLEAN, .as.boolean = false });
		} else if (ast_match(lexer, TOKEN_IDENTIFIER)) {
			result = ast_literal(arena, (AST_Literal){ .type = AST_LITERAL_VARIABLE, .as.identifier = lexer_next(lexer) });
		} else
			LOG_ERROR("#Expect expression\n%.*s", str_spread(lexer_error_location_string(arena, lexer_peek(lexer))));
	}

	return result;
}

// unary -> ( "!" | "-" ) unary | primary;
AST_Node *ast_parse_expr_unary(Arena *arena, Lexer *lexer) {
	AST_Node *right = 0;

	bool ok = arena && lexer;
	if (ok) {
		if (ast_match(lexer, TOKEN_BANG, TOKEN_MINUS)) {
			Token operator = lexer_next(lexer);
			right = ast_unary(arena, operator, ast_parse_expr_unary(arena, lexer));
		} else if (ast_match(lexer, TOKEN_STAR, TOKEN_SLASH, TOKEN_PLUS, TOKEN_GREATER, TOKEN_GREATER_EQUAL, TOKEN_LESS, TOKEN_LESS_EQUAL)) {
			Token op = lexer_next(lexer);
			LOG_ERROR("#Missing left-hand operand for binary operator '%.*s'\n%.*s",
				str_spread(op.lexeme),
				str_spread(lexer_error_location_string(arena, op)) //
			);
			switch (op.type) {
				case TOKEN_GREATER:
				case TOKEN_GREATER_EQUAL:
				case TOKEN_LESS:
				case TOKEN_LESS_EQUAL:
					right = ast_parse_expr_term(arena, lexer);
					break;

				case TOKEN_PLUS:
					right = ast_parse_expr_factor(arena, lexer);
					break;

				case TOKEN_STAR:
				case TOKEN_SLASH:
					right = ast_parse_expr_unary(arena, lexer);
					break;

				default:
					break;
			}
		} else
			right = ast_parse_expr_primary(arena, lexer);
	}

	return right;
}

// factor -> unary ( ( "*" | "/" ) unary )* ;
AST_Node *ast_parse_expr_factor(Arena *arena, Lexer *lexer) {
	AST_Node *left = 0;

	bool ok = arena && lexer;
	if (ok) {
		left = ast_parse_expr_unary(arena, lexer);

		while (ast_match(lexer, TOKEN_STAR, TOKEN_SLASH)) {
			Token operator = lexer_next(lexer);
			AST_Node *right = ast_parse_expr_unary(arena, lexer);

			left = ast_binary(arena, left, operator, right);
		}
	}

	return left;
}

// term -> factor ( ( "+" | "-" ) factor )* ;
AST_Node *ast_parse_expr_term(Arena *arena, Lexer *lexer) {
	AST_Node *left = 0;

	bool ok = arena && lexer;
	if (ok) {
		left = ast_parse_expr_factor(arena, lexer);

		while (ast_match(lexer, TOKEN_MINUS, TOKEN_PLUS)) {
			Token operator = lexer_next(lexer);
			AST_Node *right = ast_parse_expr_factor(arena, lexer);

			left = ast_binary(arena, left, operator, right);
		}
	}

	return left;
}

// comparison -> term ( ( ">" | ">=" | "<" | "<=" ) term )* ;
AST_Node *ast_parse_expr_comparison(Arena *arena, Lexer *lexer) {
	AST_Node *left = 0;

	bool ok = arena && lexer;
	if (ok) {
		left = ast_parse_expr_term(arena, lexer);

		while (ast_match(lexer, TOKEN_GREATER, TOKEN_GREATER_EQUAL, TOKEN_LESS, TOKEN_LESS_EQUAL)) {
			Token operator = lexer_next(lexer);
			AST_Node *right = ast_parse_expr_term(arena, lexer);

			left = ast_binary(arena, left, operator, right);
		}
	}

	return left;
}

// equality -> comparison ( ( "!=" | "==" ) comparison )* ;
AST_Node *ast_parse_expr_equality(Arena *arena, Lexer *lexer) {
	AST_Node *left = 0;

	bool ok = arena && lexer;
	if (ok) {
		left = ast_parse_expr_comparison(arena, lexer);

		while (ast_match(lexer, TOKEN_EQUAL_EQUAL, TOKEN_BANG_EQUAL)) {
			Token operator = lexer_next(lexer);
			AST_Node *right = ast_parse_expr_comparison(arena, lexer);

			left = ast_binary(arena, left, operator, right);
		}
	}

	return left;
}

// and -> equality ( ( "&&" | "and" ) equality )* ;
AST_Node *ast_parse_expr_and(Arena *arena, Lexer *lexer) {
	AST_Node *left = 0;

	bool ok = arena && lexer;
	if (ok) {
		left = ast_parse_expr_equality(arena, lexer);

		while (ast_match(lexer, TOKEN_EQUAL_EQUAL, TOKEN_BANG_EQUAL)) {
			Token operator = lexer_next(lexer);
			AST_Node *right = ast_parse_expr_equality(arena, lexer);

			left = ast_logical(arena, left, operator, right);
		}
	}

	return left;
}

// or -> and ( ( "||" | "or" ) and )* ;
AST_Node *ast_parse_expr_or(Arena *arena, Lexer *lexer) {
	AST_Node *left = 0;

	bool ok = arena && lexer;
	if (ok) {
		left = ast_parse_expr_and(arena, lexer);

		while (ast_match(lexer, TOKEN_PIPE_PIPE) || ast_match_keyword(lexer, TOKEN_AND)) {
			Token operator = lexer_next(lexer);
			AST_Node *right = ast_parse_expr_and(arena, lexer);

			left = ast_logical(arena, left, operator, right);
		}
	}

	return left;
}

// assignment -> IDENTIFIER "=" assignment | or;
AST_Node *ast_parse_expr_assignment(Arena *arena, Lexer *lexer) {
	AST_Node *left = 0;

	bool ok = arena && lexer;
	if (ok) {
		Token s = lexer->current;

		left = ast_parse_expr_or(arena, lexer);
		if (ast_match(lexer, TOKEN_EQUAL)) {
			lexer_next(lexer); // consume '='
			AST_Node *value = ast_parse_expr_assignment(arena, lexer);

			if (left->type == AST_NODE_EXPR_PRIMARY && left->literal.type == AST_LITERAL_VARIABLE)
				left = ast_assign(arena, left->literal.as.identifier, value);
			else {
				String8 where = lexer_error_location_string(arena, s);
				LOG_ERROR("#Invalid l-value for assignment\n%.*s", str_spread(where));
			}
		}
	}

	return left;
}

// comma -> assignment ( ( "," ) assignment )* ;
AST_Node *ast_parse_expr_comma(Arena *arena, Lexer *lexer) {
	AST_Node *left = 0;

	bool ok = arena && lexer;
	if (ok) {
		left = ast_parse_expr_assignment(arena, lexer);

		while (ast_match(lexer, TOKEN_COMMA)) {
			Token operator = lexer_next(lexer);
			AST_Node *right = ast_parse_expr_assignment(arena, lexer);

			left = ast_binary(arena, left, operator, right);
		}
	}

	return left;
}

// expression -> comma
AST_Node *ast_parse_expr(Arena *arena, Lexer *lexer) {
	return ast_parse_expr_comma(arena, lexer);
}

AST_Node *ast_parse_stmt_expr(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		result = arena_push_count(arena, AST_Node, 1);
		result->type = AST_NODE_STMT_EXPR;
		ast_pushback(result, ast_parse_expr(arena, lexer));
		lexer_consume(lexer, TOKEN_SEMICOLON, s("Expect ';' after value."));
	}

	return result;
}

AST_Node *ast_parse_stmt_print(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		lexer_next(lexer); // consume print
		result = arena_push_count(arena, AST_Node, 1);
		result->type = AST_NODE_STMT_PRINT;

		ast_pushback(result, ast_parse_expr(arena, lexer));
		lexer_consume(lexer, TOKEN_SEMICOLON, s("Expect ';' after value."));
	}

	return result;
}

AST_Node *ast_parse_stmt_if(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		lexer_next(lexer); // consume 'if'

		result = arena_push_count(arena, AST_Node, 1);
		result->type = AST_NODE_STMT_IF;

		ast_pushback(result, ast_parse_expr(arena, lexer)); // cond
		ast_pushback(result, ast_parse_stmt_block(arena, lexer)); // then
		if (ast_match_keyword(lexer, TOKEN_ELSE)) {
			lexer_next(lexer); // consume 'else'
			ast_pushback(result, ast_parse_stmt_block(arena, lexer));
		}
	}

	return result;
}

AST_Node *ast_parse_stmt_block(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		lexer_consume(lexer, TOKEN_OPEN_BRACE, s("Expect '{' before block.")); // consume '{'
		result = arena_push_count(arena, AST_Node, 1);
		result->type = AST_NODE_STMT_BLOCK;

		while (ast_match(lexer, TOKEN_CLOSE_BRACE) == false && lexer_at_end(lexer) == false)
			ast_pushback(result, ast_parse_decl(arena, lexer));

		lexer_consume(lexer, TOKEN_CLOSE_BRACE, s("Expect '}' after block."));
	}

	return result;
}

// // for_stmt -> "for" ( expr? | "(" ( decl_list | expr_stmt | ";" ) expr? ";" expr? ")" ) block
AST_Node *ast_parse_stmt_for(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		lexer_next(lexer); // consume 'for'

		result = arena_push_count(arena, AST_Node, 1);
		result->type = AST_NODE_STMT_FOR;

		AST_Node *initializer = 0;
		AST_Node *condition = 0;
		AST_Node *increment = 0;
		AST_Node *body = 0;

		if (lexer_peek(lexer).type == TOKEN_OPEN_PAREN) {
			lexer_next(lexer); // consume '('

			if (ast_match(lexer, TOKEN_SEMICOLON))
				lexer_next(lexer); // consume ';'
			else if (ast_match_keyword(lexer, TOKEN_VAR))
				initializer = ast_parse_decl_list(arena, lexer);
			else
				initializer = ast_parse_stmt_expr(arena, lexer);

			if (ast_match(lexer, TOKEN_SEMICOLON) == false)
				condition = ast_parse_expr(arena, lexer);
			lexer_consume(lexer, TOKEN_SEMICOLON, s("Expect ';' after loop condition."));

			if (ast_match(lexer, TOKEN_CLOSE_PAREN) == false)
				increment = ast_parse_expr(arena, lexer);
			lexer_consume(lexer, TOKEN_CLOSE_PAREN, s("Expect ')' after for clause."));

			body = ast_parse_stmt_block(arena, lexer);
		} else { // simple for loop
			if (lexer_peek(lexer).type != TOKEN_OPEN_BRACE)
				condition = ast_parse_expr(arena, lexer);
			body = ast_parse_stmt_block(arena, lexer);
		}

		if (condition == 0)
			condition = ast_literal(arena, (AST_Literal){ .type = AST_LITERAL_BOOLEAN, .as.boolean = true });
		if (increment) {
            AST_Node *expr_stmt = arena_push_count(arena, AST_Node, 1);
            expr_stmt->type = AST_NODE_STMT_EXPR;
            ast_pushback(expr_stmt, increment);

			ast_pushback(body, expr_stmt);
        }

		ASSERT(body);
		ast_pushback(result, condition);
		ast_pushback(result, body);

		if (initializer) {
			AST_Node *wrapper = arena_push_count(arena, AST_Node, 1);
			wrapper->type = AST_NODE_STMT_BLOCK;
			ast_pushback(wrapper, initializer);
			ast_pushback(wrapper, result);
			result = wrapper;
		}
	}

	return result;
}

AST_Node *ast_parse_stmt(Arena *arena, Lexer *lexer) {
	if (ast_match_keyword(lexer, TOKEN_PRINT)) return ast_parse_stmt_print(arena, lexer);
	if (ast_match(lexer, TOKEN_OPEN_BRACE)) return ast_parse_stmt_block(arena, lexer);
	if (ast_match_keyword(lexer, TOKEN_IF)) return ast_parse_stmt_if(arena, lexer);
	if (ast_match_keyword(lexer, TOKEN_FOR)) return ast_parse_stmt_for(arena, lexer);

	return ast_parse_stmt_expr(arena, lexer);
}

AST_Node *ast_parse_decl_var(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		Token identifier = lexer_consume(lexer, TOKEN_IDENTIFIER, s("Expect variable name."));

		result = arena_push_count(arena, AST_Node, 1);
		result->type = AST_NODE_DECL_VAR;
		result->identifier = identifier;

		if (ast_match(lexer, TOKEN_EQUAL)) { // assignment
			lexer_next(lexer); // consume '='
			ast_pushback(result, ast_parse_expr_assignment(arena, lexer));
		}
	}

	return result;
}

AST_Node *ast_parse_decl_list(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		lexer_next(lexer); // consume 'var'

		result = arena_push_count(arena, AST_Node, 1);
		result->type = AST_NODE_DECL_LIST;

		do {
			if (lexer_peek(lexer).type == TOKEN_COMMA)
				lexer_next(lexer); // consume ','

			ast_pushback(result, ast_parse_decl_var(arena, lexer));
		} while (ast_match(lexer, TOKEN_COMMA));

		lexer_consume(lexer, TOKEN_SEMICOLON, s("Expect ';' after variable declaration."));
	}

	return result;
}

void ast_sync(Lexer *lexer) {
	while (lexer_at_end(lexer) == false) {
		if (lexer_peek(lexer).type == TOKEN_SEMICOLON) return;

		switch (lexer_peek(lexer).type) {
			case TOKEN_KEYWORD_0 + TOKEN_VAR:
				return;
			case TOKEN_KEYWORD_0 + TOKEN_PRINT:
				return;
			default:
				break;
		}

		lexer_next(lexer);
	}
}

AST_Node *ast_parse_decl(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		if (ast_match_keyword(lexer, TOKEN_VAR))
			result = ast_parse_decl_list(arena, lexer);
		else
			result = ast_parse_stmt(arena, lexer);

		if (result == 0) {
			ast_sync(lexer);
			lexer->had_error = false;
		}
	}

	return result;
}

AST_Node *ast_parse(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		result = arena_push_count(arena, AST_Node, 1);
		result->type = AST_NODE_PROGRAM; // Could be just AST_NODE_STMT_BLOCK

		while (ast_match(lexer, TOKEN_EOF) == false)
			ast_pushback(result, ast_parse_decl(arena, lexer));
	}

	return result;
}

void ast_print_literal(AST_Literal literal) {
	switch (literal.type) {
		case AST_LITERAL_STRING:
			printf("%.*s", str_spread(literal.as.string));
			break;
		case AST_LITERAL_REAL:
			printf("%g", literal.as.real);
			break;
		case AST_LITERAL_BOOLEAN:
			printf("%s", literal.as.boolean ? "true" : "false");
			break;
		case AST_LITERAL_NIL:
			printf("nil");
			break;
		case AST_LITERAL_VARIABLE:
			printf("%.*s", str_spread(literal.as.identifier.lexeme));
		default:
			break;
	}
}

void ast_print(AST_Node *node) {
	bool ok = node;
	if (ok) {
		String8 result = { 0 };
		switch (node->type) {
			case AST_NODE_EXPR_ASSIGN: {
				printf("(");
				printf("%.*s ", str_spread(node->identifier.lexeme));
				ast_print(node->first_child);
				printf(")");
			} break;
			case AST_NODE_EXPR_GROUPING: {
				ast_print(node->first_child);
			} break;
			case AST_NODE_EXPR_BINARY: {
				printf("(");
				printf("%.*s ", str_spread(node->operator.lexeme));
				ast_print(node->first_child);
				printf(" ");
				ast_print(node->last_child);
				printf(")");
			} break;
			case AST_NODE_EXPR_UNARY: {
				printf("( ");
				printf("%.*s ", str_spread(node->operator.lexeme));
				ast_print(node->first_child);
				printf(")");
			} break;
			case AST_NODE_EXPR_PRIMARY:
				ast_print_literal(node->literal);
				break;
			case AST_NODE_DECL_VAR: {
				printf("(");
				printf("= %.*s", str_spread(node->identifier.lexeme));
				if (node->first_child) {
					printf(" ");
					ast_print(node->first_child);
				}
				printf(")");
			} break;
			case AST_NODE_STMT_PRINT: {
				printf("(");
				printf("print ");
				ast_print(node->first_child);
				printf(")");
			} break;
			case AST_NODE_STMT_EXPR:
				ast_print(node->first_child);
				break;
			case AST_NODE_PROGRAM: {
				AST_Node *stmt = node->first_child;
				do {
					ast_print(stmt);
					printf("\n");

					stmt = stmt->next_sibling;
				} while (stmt != node->first_child);
			} break;
			default:
				break;
		}
	}
}

static inline bool ast_truthy(AST_Literal lit) {
	bool result = false;

	switch (lit.type) {
		case AST_LITERAL_STRING:
			result = lit.as.string.length > 0;
			break;
		case AST_LITERAL_REAL:
			result = lit.as.real != 0.0;
			break;
		case AST_LITERAL_BOOLEAN:
			result = lit.as.boolean;
			break;
		default:
			break;
	}

	return result;
}

static inline bool ast_equal(AST_Literal left, AST_Literal right) {
	if (left.type == AST_LITERAL_NIL && right.type == AST_LITERAL_NIL) return true;
	if (left.type != right.type) return false;

	if (left.type == AST_LITERAL_REAL) return left.as.real == right.as.real;
	if (left.type == AST_LITERAL_STRING) return str8_equals(left.as.string, right.as.string);
	if (left.type == AST_LITERAL_BOOLEAN) return left.as.boolean == right.as.boolean;

	return false;
}

String8 ast_lit_to_string(Arena *arena, AST_Literal lit) {
	String8 result = { 0 };

	bool ok = arena;
	if (ok) {
		switch (lit.type) {
			case AST_LITERAL_STRING:
				result = lit.as.string;
				break;
			case AST_LITERAL_REAL:
				result = str8_pushf(arena, s("%g"), lit.as.real);
				break;
			case AST_LITERAL_BOOLEAN:
				result = lit.as.boolean ? s("true") : s("false");
				break;
			default:
				break;
		}
	}

	return result;
}

double ast_lit_to_real(AST_Literal lit) {
	double result = 0.0f;

	switch (lit.type) {
		case AST_LITERAL_REAL:
			result = lit.as.real;
			break;
		case AST_LITERAL_BOOLEAN:
			result = lit.as.boolean ? 1.0 : 0.0;
			break;
		default:
			break;
	}

	return result;
}

AST_Literal ast_evaluate(Arena *arena, Enviroment *env, AST_Node *expr) {
	AST_Literal result = { .type = AST_LITERAL_NIL };
	ASSERT(expr->type < AST_NODE_EXPR_MAX);

	bool ok = arena && expr;
	if (ok) {
		switch (expr->type) {
			case AST_NODE_EXPR_ASSIGN: {
				result = env_assign(env, expr->identifier.lexeme, ast_evaluate(arena, env, expr->first_child));
			} break;
			case AST_NODE_EXPR_LOGICAL: {
				AST_Literal left = ast_evaluate(arena, env, expr->first_child);

				if (expr->operator.type == TOKEN_PIPE_PIPE || expr->operator.type == keyword_to_token[TOKEN_OR]) {
					if (ast_truthy(left))
						result = left;
					else
						result = ast_evaluate(arena, env, expr->last_child);
				} else {
					if (!ast_truthy(left))
						result = left;
					else
						result = ast_evaluate(arena, env, expr->last_child);
				}

			} break;
			case AST_NODE_EXPR_BINARY: {
				result.type = AST_LITERAL_REAL; // assume default number

				AST_Literal left = ast_evaluate(arena, env, expr->first_child);
				AST_Literal right = ast_evaluate(arena, env, expr->last_child);

				switch (expr->operator.type) {
					case TOKEN_MINUS:
						ASSERT(left.type == AST_LITERAL_REAL && right.type == AST_LITERAL_REAL);
						result.as.real = left.as.real - right.as.real;
						break;

					case TOKEN_SLASH:
						ASSERT(left.type == AST_LITERAL_REAL && right.type == AST_LITERAL_REAL);
						result.as.real = left.as.real / right.as.real;
						break;
					case TOKEN_STAR:
						ASSERT(left.type == AST_LITERAL_REAL && right.type == AST_LITERAL_REAL);
						result.as.real = left.as.real * right.as.real;
						break;
					case TOKEN_PLUS:
						if (left.type == AST_LITERAL_STRING || right.type == AST_LITERAL_STRING)
							result = (AST_Literal){ .type = AST_LITERAL_STRING, .as.string = str8_concat(arena, ast_lit_to_string(arena, left), ast_lit_to_string(arena, right)) };
						else if (left.type == AST_LITERAL_REAL)
							result = (AST_Literal){ .type = AST_LITERAL_REAL, .as.real = left.as.real + ast_lit_to_real(right) };
						break;
					case TOKEN_GREATER:
						ASSERT(left.type == AST_LITERAL_REAL && right.type == AST_LITERAL_REAL);
						result.type = AST_LITERAL_BOOLEAN;
						result.as.boolean = left.as.real > right.as.real;
						break;
					case TOKEN_GREATER_EQUAL:
						ASSERT(left.type == AST_LITERAL_REAL && right.type == AST_LITERAL_REAL);
						result.type = AST_LITERAL_BOOLEAN;
						result.as.boolean = left.as.real >= right.as.real;
						break;
					case TOKEN_LESS:
						ASSERT(left.type == AST_LITERAL_REAL && right.type == AST_LITERAL_REAL);
						result.type = AST_LITERAL_BOOLEAN;
						result.as.boolean = left.as.real < right.as.real;
						break;
					case TOKEN_LESS_EQUAL:
						ASSERT(left.type == AST_LITERAL_REAL && right.type == AST_LITERAL_REAL);
						result.type = AST_LITERAL_BOOLEAN;
						result.as.boolean = left.as.real <= right.as.real;
						break;
					case TOKEN_EQUAL_EQUAL:
						result.type = AST_LITERAL_BOOLEAN;
						result.as.boolean = ast_equal(left, right);
						break;
					case TOKEN_BANG_EQUAL:
						result.type = AST_LITERAL_BOOLEAN;
						result.as.boolean = !ast_equal(left, right);
						break;

					case TOKEN_COMMA:
						result = right;
						break;

					default:
						break;
				}
			} break;
			case AST_NODE_EXPR_UNARY: {
				result = ast_evaluate(arena, env, expr->first_child);

				Token operator = expr->operator;
				switch (operator.type) {
					case TOKEN_MINUS:
						ASSERT(result.type == AST_LITERAL_REAL);
						result.as.real *= -1;
						break;
					case TOKEN_BANG:
						bool val = !ast_truthy(result);
						result.type = AST_LITERAL_BOOLEAN;
						result.as.boolean = val;
						break;
					default:
						break;
				}
			} break;
			case AST_NODE_EXPR_GROUPING:
				result = ast_evaluate(arena, env, expr->first_child);
				break;
			case AST_NODE_EXPR_PRIMARY:
				if (expr->literal.type == AST_LITERAL_VARIABLE)
					result = env_find_val(env, expr->literal.as.identifier.lexeme);
				else
					result = expr->literal;
				break;
			default:
				break;
		}
	}

	return result;
}

void ast_execute(Arena *arena, Enviroment *env, AST_Node *node) {
	bool ok = arena && node;

	if (ok) {
		switch (node->type) {
			case AST_NODE_STMT_PRINT: {
				AST_Literal lit = ast_evaluate(arena, env, node->first_child);
				if (lit.type == AST_LITERAL_VARIABLE)
					lit = env_find_val(env, lit.as.identifier.lexeme);

				ast_print_literal(lit);
				LOG_INFO("#");
			} break;
			case AST_NODE_STMT_EXPR:
				ast_evaluate(arena, env, node->first_child);
				break;
			case AST_NODE_DECL_VAR: {
				AST_Literal lit = { .type = AST_LITERAL_NIL };
				if (node->first_child)
					lit = ast_evaluate(arena, env, node->first_child);
				env_define(env, node->identifier.lexeme, lit);
			} break;
			case AST_NODE_DECL_LIST: {
				AST_Node *decl = node->first_child;
				if (decl) do // TODO: scoping
						ast_execute(arena, env, decl), decl = decl->next_sibling;
					while (decl != node->first_child);
			} break;
			case AST_NODE_STMT_BLOCK: {
				AST_Node *stmt = node->first_child;
				if (stmt) do // TODO: scoping
						ast_execute(arena, env, stmt), stmt = stmt->next_sibling;
					while (stmt != node->first_child);
			} break;
			case AST_NODE_STMT_IF: {
				if (ast_truthy(ast_evaluate(arena, env, node->first_child)))
					ast_execute(arena, env, node->first_child->next_sibling);
				else if (node->last_child != node->first_child->next_sibling)
					ast_execute(arena, env, node->last_child);
			} break;
			case AST_NODE_STMT_FOR: {
				while (ast_truthy(ast_evaluate(arena, env, node->first_child)))
					ast_execute(arena, env, node->last_child);
			} break;
			case AST_NODE_PROGRAM: {
				AST_Node *stmt = node->first_child;
				if (stmt) do
						ast_execute(arena, env, stmt), stmt = stmt->next_sibling;
					while (stmt != node->first_child);
			} break;
			default:
				break;
		}
	}
}

int main(void) {
	Arena arena[] = { arena_make(MiB(8)) };

	String8 source = os_file_read_entire(arena, s("engine/scratch/example.lox"));
	AST_Node *program = ast_parse(arena, (Lexer[]){ lexer_make(source, keyword_to_string, TOKEN_KEYWORD_MAX) });

	Enviroment env = { 0 };
	ast_execute(arena, &env, program);

	arena_destroy(arena);
	return 0;
}
