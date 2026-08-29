#include "core/arena.h"
#include "core/debug.h"
#include "core/strings.h"
#include "os.h"
#include <core.h>
#include <stdio.h>
#include <utils/lexer.h>

#define KEYWORD_LIST    \
	X(FALSE, "false")   \
	X(TRUE, "true")     \
	X(NIL, "nil")       \
	X(PRINT, "print")   \
	X(VAR, "var")       \
	X(IF, "if")         \
	X(ELSE, "else")     \
	X(AND, "and")       \
	X(OR, "or")         \
	X(FOR, "for")       \
	X(FN, "fn")         \
	X(RETURN, "return") \
	X(BREAK, "break")   \
	X(CONTINUE, "continue")

typedef enum {
#define X(name, key) TOKEN_##name,
	KEYWORD_LIST
#undef X
		TOKEN_KEYWORD_MAX,
} TokenKeywordType;

String8 keyword_to_string[TOKEN_KEYWORD_MAX] = {
#define X(name, key) [TOKEN_##name] = scomp(key),
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
	AST_NODE_EXPR_CALL,
	AST_NODE_EXPR_LOGICAL,
	AST_NODE_EXPR_UNARY,
	AST_NODE_EXPR_GROUPING,
	AST_NODE_EXPR_PRIMARY,
	AST_NODE_EXPR_MAX,

	AST_NODE_STMT_PRINT = AST_NODE_EXPR_MAX,
	AST_NODE_STMT_IF,
	AST_NODE_STMT_FOR,
	AST_NODE_STMT_BLOCK,
	AST_NODE_STMT_RETURN,
	AST_NODE_STMT_BREAK,
	AST_NODE_STMT_CONTINUE,
	AST_NODE_STMT_EXPR,

	AST_NODE_DECLARATOR,
	AST_NODE_DECL_VAR,
	AST_NODE_DECL_FN,

	AST_NODE_PROGRAM,

	AST_NODE_MAX,
} AST_NodeType;

typedef enum {
	AST_LITERAL_NIL,
	AST_LITERAL_STRING,
	AST_LITERAL_REAL,
	AST_LITERAL_BOOLEAN,
	AST_LITERAL_VARIABLE,
	AST_LITERAL_CALLABLE,

	AST_LITERAL_MAX,
} AST_LiteralType;

typedef struct AST_Literal AST_Literal;
typedef struct AST_Node AST_Node;
typedef AST_Literal (*CallableFn)(uint32_t argc, AST_Literal *argv);

typedef struct {
	String8 name;
	AST_Node *params;
	uint32_t param_count;
	AST_Node *block;
} AST_Callable;

struct AST_Literal {
	AST_LiteralType type;
	union {
		Token name;
		String8 string;
		AST_Callable callable;
		double real;
		bool boolean;
	} as;
};

struct AST_Node {
	AST_NodeType type;

	// Tree
	AST_Node *parent;
	AST_Node *first_child, *last_child;
	AST_Node *next_sibling, *prev_sibling;

	Token operator, name;
	AST_Literal literal;
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

bool ast_match_keyword_impl(Lexer *lexer, TokenKeywordType *keywords, uint32_t keyword_count) {
	bool ok = lexer && keywords;
	if (ok)
		for (uint32_t index = 0; index < keyword_count; ++index)
			keywords[index] += TOKEN_KEYWORD_0;

	return ast_match_impl(lexer, (TokenType *)keywords, keyword_count);
}

#define ast_match(l, ...) ast_match_impl((l), array_arg(TokenType, __VA_ARGS__))
#define ast_match_keyword(l, ...) ast_match_keyword_impl((l), array_arg(TokenKeywordType, __VA_ARGS__))

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
	ASSERT_FORMAT(kv != 0, "Undefined variable '%.*s'", sspread(key));

	return kv->value;
}

AST_Literal env_assign(Enviroment *env, String8 key, AST_Literal value) {
	KeyValue *var = env_find(env, key, false);
	ASSERT_FORMAT(var != 0, "Undefined variable '%.*s'", sspread(key));

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

AST_Node *ast_decl(Arena *arena, Lexer *lexer);
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
		result = ast_make(arena, AST_NODE_EXPR_BINARY);
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
		result = ast_make(arena, AST_NODE_EXPR_LOGICAL);
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
		result = ast_make(arena, AST_NODE_EXPR_UNARY);
		result->operator = operator;

		ast_pushback(result, right);
	}

	return result;
}

AST_Node *ast_literal(Arena *arena, AST_Literal literal) {
	AST_Node *result = 0;
	bool ok = arena;
	if (ok) {
		result = ast_make(arena, AST_NODE_EXPR_PRIMARY);

		result->literal = literal;
	}

	return result;
}

AST_Node *ast_grouping(Arena *arena, AST_Node *expr) {
	AST_Node *result = 0;

	bool ok = arena;
	if (ok) {
		result = ast_make(arena, AST_NODE_EXPR_GROUPING);

		ast_pushback(result, expr);
	}

	return result;
}
AST_Node *ast_assign(Arena *arena, Token identifier, AST_Node *value) {
	AST_Node *result = 0;

	bool ok = arena && value;
	if (ok) {
		result = ast_make(arena, AST_NODE_EXPR_ASSIGN);
		result->name = identifier;

		ast_pushback(result, value);
	}

	return result;
}

AST_Node *ast_parse_expr_primary(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		if (ast_match(lexer, TOKEN_LPAREN)) {
			lexer_advance(lexer);
			result = ast_grouping(arena, ast_parse_expr(arena, lexer));
			lexer_consume(lexer, TOKEN_RPAREN, s("Expect ')' after expression"));
		} else if (ast_match(lexer, TOKEN_STRING))
			result = ast_literal(arena, (AST_Literal){ .type = AST_LITERAL_STRING, .as.string = lexer_advance(lexer).lexeme });
		else if (ast_match(lexer, TOKEN_REAL))
			result = ast_literal(arena, (AST_Literal){ .type = AST_LITERAL_REAL, .as.real = str8_to_f64(lexer_advance(lexer).lexeme) });
		else if (ast_match(lexer, TOKEN_INTEGER))
			result = ast_literal(arena, (AST_Literal){ .type = AST_LITERAL_REAL, .as.real = str8_to_s64(lexer_advance(lexer).lexeme) });
		else if (ast_match_keyword(lexer, TOKEN_NIL)) {
			lexer_advance(lexer);
			result = ast_literal(arena, (AST_Literal){ .type = AST_LITERAL_NIL });
		} else if (ast_match_keyword(lexer, TOKEN_TRUE)) {
			result = ast_literal(arena, (AST_Literal){ .type = AST_LITERAL_BOOLEAN, .as.boolean = true });
			lexer_advance(lexer);
		} else if (ast_match_keyword(lexer, TOKEN_FALSE)) {
			lexer_advance(lexer);
			result = ast_literal(arena, (AST_Literal){ .type = AST_LITERAL_BOOLEAN, .as.boolean = false });
		} else if (ast_match(lexer, TOKEN_IDENTIFIER)) {
			result = ast_literal(arena, (AST_Literal){ .type = AST_LITERAL_VARIABLE, .as.name = lexer_advance(lexer) });
		} else
			LOG_ERROR("#Unexpected token '%.*s'.\n%.*s", sspread(lexer_peek(lexer).lexeme), sspread(lexer_error_location_string(arena, lexer_peek(lexer))));
	}

	return result;
}

// call -> primary ( "(" argument_list? ")" )* ;
// argument_list -> expr ( "," expr )* ;
AST_Node *ast_parse_expr_call(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		result = ast_parse_expr_primary(arena, lexer);

		if (ast_match(lexer, TOKEN_LPAREN)) {
			lexer_advance(lexer); // consume '('

			AST_Node *call = ast_make(arena, AST_NODE_EXPR_CALL);
			ast_pushback(call, result);

			if (lexer_peek(lexer).type != TOKEN_RPAREN)
				do {
					if (lexer_peek(lexer).type == TOKEN_COMMA) lexer_advance(lexer);
					ast_pushback(call, ast_parse_expr_assignment(arena, lexer));
				} while (ast_match(lexer, TOKEN_COMMA));

			lexer_consume(lexer, TOKEN_RPAREN, s("Expect ')' after arguments."));
			result = call;
		}
	}

	return result;
}

// unary -> ( "!" | "-" ) unary | primary;
AST_Node *ast_parse_expr_unary(Arena *arena, Lexer *lexer) {
	AST_Node *right = 0;

	bool ok = arena && lexer;
	if (ok) {
		if (ast_match(lexer, TOKEN_BANG, TOKEN_MINUS)) {
			Token operator = lexer_advance(lexer);
			right = ast_unary(arena, operator, ast_parse_expr_unary(arena, lexer));
		} else if (ast_match(lexer, TOKEN_STAR, TOKEN_SLASH, TOKEN_PLUS, TOKEN_GREATER, TOKEN_GREATER_EQUAL, TOKEN_LESS, TOKEN_LESS_EQUAL)) {
			Token op = lexer_advance(lexer);
			LOG_ERROR("#Missing left-hand operand for binary operator '%.*s'\n%.*s",
				sspread(op.lexeme),
				sspread(lexer_error_location_string(arena, op)) //
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
			right = ast_parse_expr_call(arena, lexer);
	}

	return right;
}

// factor -> unary ( ( "*" | "/" ) unary )* ;
AST_Node *ast_parse_expr_factor(Arena *arena, Lexer *lexer) {
	AST_Node *left = 0;

	bool ok = arena && lexer;
	if (ok) {
		left = ast_parse_expr_unary(arena, lexer);

		while (ast_match(lexer, TOKEN_STAR, TOKEN_SLASH, TOKEN_PERCENT)) {
			Token operator = lexer_advance(lexer);
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
			Token operator = lexer_advance(lexer);
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
			Token operator = lexer_advance(lexer);
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
			Token operator = lexer_advance(lexer);
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
			Token operator = lexer_advance(lexer);
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
			Token operator = lexer_advance(lexer);
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
			lexer_advance(lexer); // consume '='
			AST_Node *value = ast_parse_expr_assignment(arena, lexer);

			if (left->type == AST_NODE_EXPR_PRIMARY && left->literal.type == AST_LITERAL_VARIABLE)
				left = ast_assign(arena, left->literal.as.name, value);
			else {
				String8 where = lexer_error_location_string(arena, s);
				LOG_ERROR("#Invalid l-value for assignment\n%.*s", sspread(where));
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
			Token operator = lexer_advance(lexer);
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
		result = ast_make(arena, AST_NODE_STMT_EXPR);
		ast_pushback(result, ast_parse_expr(arena, lexer));
		lexer_consume(lexer, TOKEN_SEMICOLON, s("Expect ';' after value."));
	}

	return result;
}

AST_Node *ast_parse_stmt_print(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		lexer_advance(lexer); // consume print
		result = ast_make(arena, AST_NODE_STMT_PRINT);

		ast_pushback(result, ast_parse_expr(arena, lexer));
		lexer_consume(lexer, TOKEN_SEMICOLON, s("Expect ';' after value."));
	}

	return result;
}

AST_Node *ast_parse_stmt_break(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		lexer_advance(lexer); // consume 'break'

		result = ast_make(arena, AST_NODE_STMT_BREAK);

		lexer_consume(lexer, TOKEN_SEMICOLON, s("Expect ';' after break."));
	}

	return result;
}

AST_Node *ast_parse_stmt_continue(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		lexer_advance(lexer); // consume 'continue'

		result = ast_make(arena, AST_NODE_STMT_CONTINUE);

		lexer_consume(lexer, TOKEN_SEMICOLON, s("Expect ';' after continue."));
	}

	return result;
}

// return_stmt -> "return" expr? ";" ;
AST_Node *ast_parse_stmt_return(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		lexer_advance(lexer); // consume 'return'

		result = ast_make(arena, AST_NODE_STMT_RETURN);

		if (ast_match(lexer, TOKEN_SEMICOLON) == false)
			ast_pushback(result, ast_parse_expr(arena, lexer));

		lexer_consume(lexer, TOKEN_SEMICOLON, s("Expect ';' after return value."));
	}

	return result;
}

AST_Node *ast_parse_stmt_if(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		lexer_advance(lexer); // consume 'if'

		result = ast_make(arena, AST_NODE_STMT_IF);

		ast_pushback(result, ast_parse_expr(arena, lexer)); // cond
		ast_pushback(result, ast_parse_stmt_block(arena, lexer)); // then
		if (ast_match_keyword(lexer, TOKEN_ELSE)) {
			lexer_advance(lexer); // consume 'else'
			ast_pushback(result, ast_parse_stmt_block(arena, lexer));
		}
	}

	return result;
}

AST_Node *ast_parse_stmt_block(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		lexer_consume(lexer, TOKEN_LBRACE, s("Expect '{' before block.")); // consume '{'
		result = ast_make(arena, AST_NODE_STMT_BLOCK);

		while (ast_match(lexer, TOKEN_RBRACE) == false && lexer_at_end(lexer) == false)
			ast_pushback(result, ast_decl(arena, lexer));

		lexer_consume(lexer, TOKEN_RBRACE, s("Expect '}' after block."));
	}

	return result;
}

// // for_stmt -> "for" ( expr? | "(" ( decl_list | expr_stmt | ";" ) expr? ";" expr? ")" ) block
AST_Node *ast_parse_stmt_for(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		lexer_advance(lexer); // consume 'for'

		result = ast_make(arena, AST_NODE_STMT_FOR);

		AST_Node *initializer = 0;
		AST_Node *condition = 0;
		AST_Node *increment = 0;
		AST_Node *body = 0;

		if (lexer_peek(lexer).type == TOKEN_LPAREN) {
			lexer_advance(lexer); // consume '('

			if (ast_match(lexer, TOKEN_SEMICOLON))
				lexer_advance(lexer); // consume ';'
			else if (ast_match_keyword(lexer, TOKEN_VAR))
				initializer = ast_parse_decl_list(arena, lexer);
			else
				initializer = ast_parse_stmt_expr(arena, lexer);

			if (ast_match(lexer, TOKEN_SEMICOLON) == false)
				condition = ast_parse_expr(arena, lexer);
			lexer_consume(lexer, TOKEN_SEMICOLON, s("Expect ';' after loop condition."));

			if (ast_match(lexer, TOKEN_RPAREN) == false)
				increment = ast_parse_expr(arena, lexer);
			lexer_consume(lexer, TOKEN_RPAREN, s("Expect ')' after for clause."));

			body = ast_parse_stmt_block(arena, lexer);
		} else { // simple for loop
			if (lexer_peek(lexer).type != TOKEN_LBRACE)
				condition = ast_parse_expr(arena, lexer);
			body = ast_parse_stmt_block(arena, lexer);
		}

		if (condition == 0)
			condition = ast_literal(arena, (AST_Literal){ .type = AST_LITERAL_BOOLEAN, .as.boolean = true });
		if (increment) {
			AST_Node *expr_stmt = ast_make(arena, AST_NODE_STMT_EXPR);
			ast_pushback(expr_stmt, increment);

			increment = expr_stmt;
		}

		ASSERT(body);
		ast_pushback(result, condition);
		ast_pushback(result, increment);
		ast_pushback(result, body);

		if (initializer) {
			AST_Node *wrapper = ast_make(arena, AST_NODE_STMT_BLOCK);
			ast_pushback(wrapper, initializer);
			ast_pushback(wrapper, result);
			result = wrapper;
		}
	}

	return result;
}

AST_Node *ast_parse_stmt(Arena *arena, Lexer *lexer) {
	if (str8_equals(lexer_peek(lexer).lexeme, s("continue"))) {
		uint32_t x = 0;
		(void)x;
	}
	if (ast_match_keyword(lexer, TOKEN_PRINT)) return ast_parse_stmt_print(arena, lexer);
	if (ast_match(lexer, TOKEN_LBRACE)) return ast_parse_stmt_block(arena, lexer);
	if (ast_match_keyword(lexer, TOKEN_IF)) return ast_parse_stmt_if(arena, lexer);
	if (ast_match_keyword(lexer, TOKEN_RETURN)) return ast_parse_stmt_return(arena, lexer);
	if (ast_match_keyword(lexer, TOKEN_CONTINUE)) return ast_parse_stmt_continue(arena, lexer);
	if (ast_match_keyword(lexer, TOKEN_BREAK)) return ast_parse_stmt_break(arena, lexer);
	if (ast_match_keyword(lexer, TOKEN_FOR)) return ast_parse_stmt_for(arena, lexer);

	return ast_parse_stmt_expr(arena, lexer);
}

AST_Node *ast_parse_decl_var(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		Token identifier = lexer_consume(lexer, TOKEN_IDENTIFIER, s("Expect variable name."));

		result = ast_make(arena, AST_NODE_DECLARATOR);
		result->name = identifier;

		if (ast_match(lexer, TOKEN_EQUAL)) { // assignment
			lexer_advance(lexer); // consume '='
			ast_pushback(result, ast_parse_expr_assignment(arena, lexer));
		}
	}

	return result;
}

AST_Node *ast_parse_decl_list(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		lexer_advance(lexer); // consume 'var'

		result = ast_make(arena, AST_NODE_DECL_VAR);

		do {
			if (lexer_peek(lexer).type == TOKEN_COMMA)
				lexer_advance(lexer); // consume ','

			ast_pushback(result, ast_parse_decl_var(arena, lexer));
		} while (ast_match(lexer, TOKEN_COMMA));

		lexer_consume(lexer, TOKEN_SEMICOLON, s("Expect ';' after variable declaration."));
	}

	return result;
}

AST_Node *ast_parse_decl_fn(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		lexer_advance(lexer); // consume 'fn'

		Token fn_id = lexer_consume(lexer, TOKEN_IDENTIFIER, s("Expect function name."));

		result = ast_make(arena, AST_NODE_DECL_FN);
		result->name = fn_id;

		lexer_consume(lexer, TOKEN_LPAREN, s("Expect '(' after function name."));
		if (lexer_peek(lexer).type != TOKEN_RPAREN) {
			do {
				if (lexer_peek(lexer).type == TOKEN_COMMA) lexer_advance(lexer);
				ast_pushback(result, ast_parse_decl_var(arena, lexer));
			} while (ast_match(lexer, TOKEN_COMMA));
		}
		lexer_consume(lexer, TOKEN_RPAREN, s("Expect ')' after parameters"));

		ast_pushback(result, ast_parse_stmt_block(arena, lexer));
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

		lexer_advance(lexer);
	}
}

AST_Node *ast_decl(Arena *arena, Lexer *lexer) {
	AST_Node *result = 0;

	bool ok = arena && lexer;
	if (ok) {
		if (ast_match_keyword(lexer, TOKEN_FN)) result = ast_parse_decl_fn(arena, lexer);
		if (ast_match_keyword(lexer, TOKEN_VAR)) result = ast_parse_decl_list(arena, lexer);
		if (result == 0) result = ast_parse_stmt(arena, lexer);

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
		result = ast_make(arena, AST_NODE_PROGRAM);

		while (ast_match(lexer, TOKEN_EOF) == false)
			ast_pushback(result, ast_decl(arena, lexer));
	}

	return result;
}

void ast_print_literal(AST_Literal literal) {
	switch (literal.type) {
		case AST_LITERAL_STRING:
			printf("%.*s", sspread(literal.as.string));
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
			printf("%.*s", sspread(literal.as.name.lexeme));
			break;
		case AST_LITERAL_CALLABLE:
			printf("<fn %.*s>", sspread(literal.as.callable.name));
			break;
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
				printf("%.*s ", sspread(node->name.lexeme));
				ast_print(node->first_child);
				printf(")");
			} break;
			case AST_NODE_EXPR_GROUPING: {
				ast_print(node->first_child);
			} break;
			case AST_NODE_EXPR_BINARY: {
				printf("(");
				printf("%.*s ", sspread(node->operator.lexeme));
				ast_print(node->first_child);
				printf(" ");
				ast_print(node->last_child);
				printf(")");
			} break;
			case AST_NODE_EXPR_UNARY: {
				printf("( ");
				printf("%.*s ", sspread(node->operator.lexeme));
				ast_print(node->first_child);
				printf(")");
			} break;
			case AST_NODE_EXPR_PRIMARY:
				ast_print_literal(node->literal);
				break;
			case AST_NODE_DECLARATOR: {
				printf("(");
				printf("= %.*s", sspread(node->name.lexeme));
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
typedef enum {
	AST_STATUS_OK,
	AST_STATUS_BREAK,
	AST_STATUS_CONTINUE,
	AST_STATUS_RETURN,
} AST_ResultStatus;

typedef struct {
	AST_ResultStatus status;
	AST_Literal value;
} AST_Result;

AST_Literal ast_evaluate(Arena *arena, Enviroment *env, AST_Node *expr);
AST_Result ast_execute(Arena *arena, Enviroment *env, AST_Node *node);

static Enviroment *globals = 0;

AST_Literal ast_evaluate(Arena *arena, Enviroment *env, AST_Node *expr) {
	AST_Literal result = { .type = AST_LITERAL_NIL };
	ASSERT(expr->type < AST_NODE_EXPR_MAX);

	bool ok = arena && expr;
	if (ok) {
		switch (expr->type) {
			case AST_NODE_EXPR_CALL: {
				AST_Literal fn_val = ast_evaluate(arena, env, expr->first_child);
				ASSERT(fn_val.type == AST_LITERAL_CALLABLE && "Target is not a callable function.");
				AST_Callable fn = fn_val.as.callable;

				Enviroment local_env = { 0 };
				for (uint32_t index = 0; index < globals->var_count; ++index)
					local_env.vars[local_env.var_count++] = globals->vars[index];

				AST_Node *arg = expr->first_child->next_sibling;
				AST_Node *param = fn.params;
				for (uint32_t index = 0; index < fn.param_count; ++index) {
					ASSERT(arg != expr->first_child);
					env_define(&local_env, param->name.lexeme, ast_evaluate(arena, env, arg));

					arg = arg->next_sibling, param = param->next_sibling;
				}
				ASSERT(arg == expr->first_child && "Too many arguments passed to fn");

				AST_Result res = ast_execute(arena, &local_env, fn.block);
				result = res.value;
			} break;
			case AST_NODE_EXPR_ASSIGN: {
				result = env_assign(env, expr->name.lexeme, ast_evaluate(arena, env, expr->first_child));
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
					case TOKEN_PERCENT:
						ASSERT(left.type == AST_LITERAL_REAL && right.type == AST_LITERAL_REAL);
						result.as.real = fmod(left.as.real, right.as.real);
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
					result = env_find_val(env, expr->literal.as.name.lexeme);
				else
					result = expr->literal;
				break;
			default:
				break;
		}
	}

	return result;
}

AST_Result ast_execute(Arena *arena, Enviroment *env, AST_Node *node) {
	AST_Result result = (AST_Result){ .status = AST_STATUS_OK, .value = { .type = AST_LITERAL_NIL } };
	bool ok = arena && node;
	if (ok) {
		switch (node->type) {
			case AST_NODE_STMT_RETURN: {
				AST_Literal lit = { .type = AST_LITERAL_NIL };
				if (node->first_child)
					lit = ast_evaluate(arena, env, node->first_child);

				result.status = AST_STATUS_RETURN;
				result.value = lit;
			} break;
			case AST_NODE_STMT_BREAK:
				result.status = AST_STATUS_BREAK;
				break;
			case AST_NODE_STMT_CONTINUE:
				result.status = AST_STATUS_CONTINUE;
				break;
			case AST_NODE_STMT_PRINT: {
				AST_Literal lit = ast_evaluate(arena, env, node->first_child);
				if (lit.type == AST_LITERAL_VARIABLE)
					lit = env_find_val(env, lit.as.name.lexeme);

				ast_print_literal(lit);
				LOG_INFO("#");
			} break;
			case AST_NODE_STMT_EXPR:
				ast_evaluate(arena, env, node->first_child);
				break;
			case AST_NODE_DECLARATOR: {
				AST_Literal lit = { .type = AST_LITERAL_NIL };
				if (node->first_child)
					lit = ast_evaluate(arena, env, node->first_child);
				env_define(env, node->name.lexeme, lit);
			} break;
			case AST_NODE_DECL_VAR: {
				AST_Node *decl = node->first_child;
				if (decl) do // TODO: scoping
						ast_execute(arena, env, decl), decl = decl->next_sibling;
					while (decl != node->first_child);
			} break;
			case AST_NODE_DECL_FN: {
				AST_Literal lit = { .type = AST_LITERAL_CALLABLE };
				AST_Callable *fn = &lit.as.callable;
				fn->name = node->name.lexeme;

				if (node->first_child != node->last_child) { // has parameters
					fn->params = node->first_child;

					AST_Node *param = node->first_child;
					while (param != node->last_child)
						fn->param_count++, param = param->next_sibling;
				}

				fn->block = node->last_child;
				env_define(env, fn->name, lit);
			} break;
			case AST_NODE_STMT_BLOCK: {
				AST_Node *stmt = node->first_child;
				if (stmt) do { // TODO: scoping
						AST_Result res = ast_execute(arena, env, stmt);
						if (res.status != AST_STATUS_OK) {
							result = res;
							break;
						}

						stmt = stmt->next_sibling;
					} while (stmt != node->first_child);
			} break;
			case AST_NODE_STMT_IF: {
				if (ast_truthy(ast_evaluate(arena, env, node->first_child)))
					result = ast_execute(arena, env, node->first_child->next_sibling);
				else if (node->last_child != node->first_child->next_sibling)
					result = ast_execute(arena, env, node->last_child);
			} break;
			case AST_NODE_STMT_FOR: {
				while (ast_truthy(ast_evaluate(arena, env, node->first_child))) {
					AST_Result res = ast_execute(arena, env, node->last_child);

					if (node->first_child->next_sibling != node->last_child)
						ast_execute(arena, env, node->first_child->next_sibling);
					if (res.status == AST_STATUS_CONTINUE)
						continue;

					if (res.status != AST_STATUS_OK) {
						result = res;
						break;
					}
				}
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

	return result;
}

void ast_visit(AST_Node *node, uint32_t indent_level) {
	for (uint32_t index = 0; index < indent_level; ++index) {
		printf("  ");
	}

	switch (node->type) {
		case AST_NODE_EXPR_ASSIGN:
			printf("ASSIGN(%.*s)\n", sspread(node->name.lexeme));
			ast_visit(node->first_child, indent_level + 1);
			break;
		case AST_NODE_EXPR_BINARY:
			printf("BINARY(%.*s)\n", sspread(node->operator.lexeme));
			ast_visit(node->first_child, indent_level + 1);
			ast_visit(node->last_child, indent_level + 1);
			break;
		case AST_NODE_EXPR_CALL:
			printf("CALL\n");
			ast_visit(node->first_child, indent_level + 1);
			AST_Node *args = node->first_child->next_sibling;
			while (args != node->first_child) {
				ast_visit(args, indent_level + 1);
				args = args->next_sibling;
			}
			break;
		case AST_NODE_EXPR_LOGICAL:
			printf("LOGICAL(%.*s)\n", sspread(node->operator.lexeme));
			ast_visit(node->first_child, indent_level + 1);
			ast_visit(node->last_child, indent_level + 1);
			break;
		case AST_NODE_EXPR_UNARY:
			printf("LOGICAL(%.*s)\n", sspread(node->operator.lexeme));
			ast_visit(node->first_child, indent_level + 1);
			break;
		case AST_NODE_EXPR_GROUPING:
			printf("GROUPING\n");
			ast_visit(node->first_child, indent_level + 1);
			break;

		case AST_NODE_EXPR_PRIMARY:
			switch (node->literal.type) {
				case AST_LITERAL_NIL:
					printf("NIL");
					break;
				case AST_LITERAL_STRING:
					printf("STRING(%.*s)", sspread(node->literal.as.string));
					break;
				case AST_LITERAL_REAL:
					printf("NUMBER(%g)", node->literal.as.real);
					break;
				case AST_LITERAL_BOOLEAN:
					printf("BOOLEAN(%s)", node->literal.as.boolean ? "true" : "false");
					break;
				case AST_LITERAL_VARIABLE:
					printf("VARIABLE(%.*s)", sspread(node->literal.as.name.lexeme));
					break;
				case AST_LITERAL_CALLABLE:
					printf("CALLABLE");
					break;
				default:
					break;
			}
			printf("\n");
			break;

		case AST_NODE_STMT_PRINT:
			printf("PRINT\n");
			ast_visit(node->first_child, indent_level + 1);
			break;
		case AST_NODE_STMT_IF:
			printf("IF\n");
			ast_visit(node->first_child, indent_level + 1); // cond
			ast_visit(node->first_child->next_sibling, indent_level + 1); // then
			if (node->last_child != node->first_child->next_sibling)
				ast_visit(node->last_child, indent_level + 1); // else
			break;
		case AST_NODE_STMT_FOR:
			printf("FOR\n");
			ast_visit(node->first_child, indent_level + 1); // cond
			if (node->first_child->next_sibling != node->last_child)
				ast_visit(node->first_child->next_sibling, indent_level + 1);
			ast_visit(node->last_child, indent_level + 1); // body
			break;
		case AST_NODE_STMT_BLOCK:
			printf("BLOCK\n");
			AST_Node *decl = node->first_child;
			if (decl) do {
					ast_visit(decl, indent_level + 1);
					decl = decl->next_sibling;
				} while (decl != node->first_child);
			break;
		case AST_NODE_STMT_RETURN:
			printf("RETURN\n");
			if (node->first_child) ast_visit(node->first_child, indent_level + 1);
			break;
		case AST_NODE_STMT_BREAK:
			printf("BREAK\n");
			break;
		case AST_NODE_STMT_CONTINUE:
			printf("CONTINUE\n");
			break;
		case AST_NODE_STMT_EXPR:
			printf("EXPR_STMT\n");
			ast_visit(node->first_child, indent_level + 1);
			break;
		case AST_NODE_DECLARATOR:
			printf("DECLARATOR(%.*s)\n", sspread(node->name.lexeme));
			if (node->first_child) ast_visit(node->first_child, indent_level + 1);
			break;
		case AST_NODE_DECL_VAR: {
			printf("VAR_DECL\n");
			AST_Node *var = node->first_child;
			if (var) do {
					ast_visit(var, indent_level + 1);
					var = var->next_sibling;
				} while (var != node->first_child);
		} break;
		case AST_NODE_DECL_FN:
			printf("FN_DECL(%.*s)\n", sspread(node->name.lexeme));
			AST_Node *param = node->first_child;
			if (param)
				while (param != node->last_child) {
					ast_visit(param, indent_level + 1);
					param = param->next_sibling;
				}
			ast_visit(node->last_child, indent_level + 1); // body
			break;
		case AST_NODE_PROGRAM: {
			printf("PROGRAM\n");
			AST_Node *decl = node->first_child;
			if (decl) do {
					ast_visit(decl, indent_level + 1);
					decl = decl->next_sibling;
				} while (decl != node->first_child);
		} break;
		default:
			break;
	}
}

AST_Literal native_clock(uint32_t argc, AST_Literal *argv) {
	return (AST_Literal){ .type = AST_LITERAL_REAL, .as.real = os_time_ns() * 1e-9 };
}

int main(void) {
	Arena arena[] = { arena_make(MiB(8)) };

	String8 source = os_file_read_entire(arena, s("engine/scratch/example.lox"));
	AST_Node *program = ast_parse(arena, (Lexer[]){ lexer_make(source, keyword_to_string, TOKEN_KEYWORD_MAX) });

	ast_visit(program, 0);

	Enviroment env = { 0 };
	globals = &env;
	ast_execute(arena, &env, program);

	arena_destroy(arena);
	return 0;
}
