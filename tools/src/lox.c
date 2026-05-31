#include "core/arena.h"
#include "core/debug.h"
#include "core/strings.h"
#include "platform/filesystem.h"
#include <core/logger.h>
#include <core/lexer.h>
#include <stdio.h>

typedef enum {
	TOKEN_AND = TOKEN_KEYWORD_0,
	TOKEN_CLASS,
	TOKEN_ELSE,
	TOKEN_FALSE,
	TOKEN_FOR,
	TOKEN_FUN,
	TOKEN_IF,
	TOKEN_NIL,
	TOKEN_OR,
	TOKEN_PRINT,
	TOKEN_RETURN,
	TOKEN_SUPER,
	TOKEN_THIS,
	TOKEN_TRUE,
	TOKEN_VAR,
	TOKEN_WHILE,

	TOKEN_KEYWORD_MAX,
} LoxKeyword;

String lox_keywords[TOKEN_KEYWORD_MAX - TOKEN_KEYWORD_0] = {
	sc("and"), sc("class"), sc("else"), sc("false"), sc("for"), sc("fun"), sc("if"), sc("nil"), sc("or"), sc("print"),
	sc("return"), sc("super"), sc("this"), sc("true"), sc("var"), sc("while")
};

typedef enum {
	EXPR_UNARY,
	EXPR_BINARY,
	EXPR_TERNARY,
	EXPR_GROUP,

	EXPR_VALUE,
	EXPR_VARIABLE,
	EXPR_ASSIGN,
} ExprType;

typedef enum {
	LIT_TYPE_NIL,
	LIT_TYPE_NUMBER,
	LIT_TYPE_BOOL,
	LIT_TYPE_STRING,
} LitType;

typedef enum {
	STMT_INVALID,

	STMT_EXPRESSION,
	STMT_PRINT,
	STMT_VARIABLE,
} StmtType;

typedef struct {
	LitType type;

	union {
		double number;
		bool boolean;
		String string;
	};
} Lit;

typedef struct expr Expr;
struct expr {
	ExprType type;

	union {
		struct {
			Token operator;
			Expr *right;
		} unary;

		struct {
			Expr *left;
			Token operator;
			Expr *right;
		} binary;

		struct {
			Expr *condition;
			Expr *then;
			Expr *otherwise;
		} ternary;

		struct {
			Expr *expression;
		} grouping;

		Lit literal;
		struct {
			Token name;
		} var;

		struct {
			Token name;
			Expr *value;
		} assign;
	};
};

typedef struct {
	StmtType type;

	union {
		Expr *expression;
		struct {
			Expr *expression;
		} print;

		struct {
			Token name;
			Expr *initializer;
		} var;
	};
} Stmt;

#define INVALID_STATMENT (Stmt){ 0 }

typedef struct {
	Arena *arena;
	Lexer *lexer;

	bool panic;
	Token previous;
} Parser;

typedef struct {
	Arena *arena;
	ArenaTrie trie;

	Parser *parser;
} Interpreter;

void check_number_operands(Token operator, Lit left, Lit right) {
	ASSERT(left.type == LIT_TYPE_NUMBER && right.type == LIT_TYPE_NUMBER);
}

bool is_truthy(Lit lit) {
	if (lit.type == LIT_TYPE_BOOL)
		return lit.boolean;
	if (lit.type == LIT_TYPE_NUMBER)
		return lit.number != 0.0;

	return false;
}

bool is_equal(Lit left, Lit right) {
	if (left.type == LIT_TYPE_NUMBER && right.type == LIT_TYPE_NUMBER)
		return left.number == right.number;
	if (left.type == LIT_TYPE_STRING && right.type == LIT_TYPE_STRING)
		return string_equals(left.string, right.string);

	return false;
}

Lit interpreter_evaluate(Interpreter *interpreter, Expr *expr) {
	switch (expr->type) {
		case EXPR_UNARY: {
			Lit value = interpreter_evaluate(interpreter, expr->unary.right);
			Lit result = { .type = LIT_TYPE_NUMBER };

			Token operator = expr->unary.operator;
			switch (operator.type) {
				case TOKEN_BANG:
					ASSERT(value.type == LIT_TYPE_BOOL);
					result.boolean = !value.boolean;
					break;
				case TOKEN_MINUS:
					ASSERT(value.type == LIT_TYPE_NUMBER);
					result.number = -value.number;
					break;
				case TOKEN_MINUS_MINUS:
					ASSERT(value.type == LIT_TYPE_NUMBER);
					result.number = value.number - 1;
					break;
				case TOKEN_PLUS_PLUS:
					ASSERT(value.type == LIT_TYPE_NUMBER);
					result.number = value.number + 1;
					break;
				default:
					ASSERT_FORMAT(false, "Unsupported unary operator '%.*s'", sarg(operator.lexeme));
			}

			return result;
		} break;
		case EXPR_BINARY: {
			Lit left = interpreter_evaluate(interpreter, expr->binary.left);
			Lit right = interpreter_evaluate(interpreter, expr->binary.right);
			Lit result = { .type = LIT_TYPE_NUMBER };

			Token operator = expr->binary.operator;
			switch (operator.type) {
				case TOKEN_STAR:
					check_number_operands(operator, left, right);
					result.number = left.number * right.number;
					break;
				case TOKEN_SLASH:
					check_number_operands(operator, left, right);
					result.number = left.number / right.number;
					break;
				case TOKEN_MINUS:
					check_number_operands(operator, left, right);
					result.number = left.number - right.number;
					break;
				case TOKEN_PLUS:
					check_number_operands(operator, left, right);
					result.number = left.number + right.number;
					break;
				case TOKEN_GREATER:
					check_number_operands(operator, left, right);
					result.type = LIT_TYPE_BOOL;
					result.boolean = left.number > right.number;
					break;
				case TOKEN_GREATER_EQUAL:
					check_number_operands(operator, left, right);
					result.type = LIT_TYPE_BOOL;
					result.boolean = left.number >= right.number;
					break;
				case TOKEN_LESS:
					check_number_operands(operator, left, right);
					result.type = LIT_TYPE_BOOL;
					result.boolean = left.number < right.number;
					break;
				case TOKEN_LESS_EQUAL:
					check_number_operands(operator, left, right);
					result.type = LIT_TYPE_BOOL;
					result.boolean = left.number <= right.number;
					break;
				case TOKEN_BANG_EQUAL:
					result.type = LIT_TYPE_BOOL;
					result.boolean = is_equal(left, right) == false;
					break;
				case TOKEN_EQUAL_EQUAL:
					result.type = LIT_TYPE_BOOL;
					result.boolean = is_equal(left, right);
					break;
				case TOKEN_COMMA:
					result = right;
					break;

				default:
					ASSERT_FORMAT(false, "Unsupported binary operator '%.*s'", sarg(operator.lexeme));
			}
			return result;
		} break;
		case EXPR_TERNARY: {
			Lit condition = interpreter_evaluate(interpreter, expr->ternary.condition);

			if (is_truthy(condition))
				return interpreter_evaluate(interpreter, expr->ternary.then);
			else
				return interpreter_evaluate(interpreter, expr->ternary.otherwise);
		} break;
		case EXPR_GROUP:
			return interpreter_evaluate(interpreter, expr->grouping.expression);
		case EXPR_VALUE:
			return expr->literal;
			break;
		case EXPR_VARIABLE: {
			Lit *lit = arena_trie_find(&interpreter->trie, buffer_wrap_string(expr->var.name.lexeme), Lit);
			ASSERT_FORMAT(lit, "undefined variable '%.*s'.", sarg(expr->var.name.lexeme));
			return *lit;
		} break;
		case EXPR_ASSIGN: {
			Lit value = interpreter_evaluate(interpreter, expr->assign.value);

			Lit *assign = NULL;
			if ((assign = arena_trie_find(&interpreter->trie, buffer_wrap_string(expr->assign.name.lexeme), Lit)))
				*assign = value;
			else {
				ASSERT_FORMAT(false, "undefined variable '%.*s'", sarg(expr->assign.name.lexeme));
			}

			return *assign;
		} break;
	}

	ASSERT(false);
	return (Lit){ 0 };
}

void print_lit(Lit lit) {
	switch (lit.type) {
		case LIT_TYPE_NUMBER:
			fprintf(stdout, "%g\n", lit.number);
			break;
		case LIT_TYPE_BOOL:
			fprintf(stdout, "%s\n", lit.boolean ? "true" : "false");
			break;
		case LIT_TYPE_STRING:
			fprintf(stdout, "%.*s\n", sarg(lit.string));
			break;
		case LIT_TYPE_NIL:
			fprintf(stdout, "<nil>\n");
			break;
	}
}

void interpreter_interpret(Interpreter *interpreter, Stmt *stmts) {
	for (uint32_t index = 0; index < arena_array_count(stmts); ++index) {
		Stmt *stmt = &stmts[index];
		switch (stmt->type) {
			case STMT_EXPRESSION: {
				interpreter_evaluate(interpreter, stmt->expression);
			} break;
			case STMT_PRINT: {
				print_lit(interpreter_evaluate(interpreter, stmt->expression));
			} break;
			case STMT_VARIABLE: {
				Lit value = { 0 };
				if (stmt->var.initializer)
					value = interpreter_evaluate(interpreter, stmt->var.initializer);

				arena_trie_put(&interpreter->trie, buffer_wrap_string(stmt->var.name.lexeme), Lit, value);
			}
			default:
				break;
		}
	}
}

Expr *expr_unary(Arena *arena, Token operator, Expr *right) {
	Expr *expr = arena_push_struct(arena, Expr);

	expr->type = EXPR_UNARY;
	expr->unary.operator = operator;
	expr->unary.right = right;

	return expr;
}

Expr *expr_binary(Arena *arena, Expr *left, Token operator, Expr *right) {
	Expr *expr = arena_push_struct(arena, Expr);

	expr->type = EXPR_BINARY;
	expr->binary.left = left;
	expr->binary.operator = operator;
	expr->binary.right = right;

	return expr;
}

Expr *expr_ternary(Arena *arena, Expr *condition, Expr *then, Expr *otherwise) {
	Expr *expr = arena_push_struct(arena, Expr);

	expr->type = EXPR_TERNARY;
	expr->ternary.condition = condition;
	expr->ternary.then = then;
	expr->ternary.otherwise = otherwise;

	return expr;
}

Expr *expr_grouping(Arena *arena, Expr *expression) {
	Expr *expr = arena_push_struct(arena, Expr);

	expr->type = EXPR_GROUP;
	expr->grouping.expression = expression;

	return expr;
}

Expr *expr_literal(Arena *arena, Lit lit) {
	Expr *expr = arena_push_struct(arena, Expr);

	expr->type = EXPR_VALUE;
	expr->literal = lit;

	return expr;
}

Expr *expr_variable(Arena *arena, Token name) {
	Expr *expr = arena_push_struct(arena, Expr);

	expr->type = EXPR_VARIABLE;
	expr->var.name = name;

	return expr;
}

Expr *expr_assign(Arena *arena, Token name, Expr *value) {
	Expr *expr = arena_push_struct(arena, Expr);

	expr->type = EXPR_ASSIGN;
	expr->assign.name = name;
	expr->assign.value = value;

	return expr;
}

#define literal(T, ...)                       \
	(Lit) {                                   \
		.type = LIT_TYPE_##T, { __VA_ARGS__ } \
	}

Parser parser_make(Arena *arena, Lexer *lexer) { return (Parser){ .arena = arena, .lexer = lexer }; }
Interpreter interpreter_make(Arena *arena, Parser *parser) { return (Interpreter){ .arena = arena, .trie = arena_trie_make(arena), .parser = parser }; }

#define token(T) \
	(Token) { .type = (TOKEN_##T), .lexeme = string_wrap(token_type_names[TOKEN_##T]) }

bool parser_at_end(Parser *parser) {
	return lexer_peek(parser->lexer).type == TOKEN_EOF;
}

Token parser_peek(Parser *parser) {
	return lexer_peek(parser->lexer);
}

Token parser_advance(Parser *parser) {
	if (parser_at_end(parser) == false)
		parser->previous = lexer_next(parser->lexer);

	return parser->previous;
}

bool parser_consume(Parser *parser, TokenType type, String message) {
	Token token = lexer_peek(parser->lexer);
	if (token.type == type) {
		parser->previous = token;
		lexer_next(parser->lexer);
		return true;
	}

	LOG_ERROR("[ %u:%u ] %.*s", token.line, token.column, sarg(message));
	parser->panic = true;
	return false;
}

bool __parser_match(Parser *parser, ...) {
	va_list args;
	va_start(args, parser);
	TokenType type = 0;
	while ((type = va_arg(args, TokenType))) {
		if (parser_peek(parser).type == type) {
			parser_advance(parser);
			return true;
		}
	}
	va_end(args);

	return false;
}
#define parser_match(p, ...) __parser_match((p), __VA_ARGS__, 0)

Expr *parse_expression(Parser *parser);

void parser_synchronize(Parser *parser) {
}

Expr *parse_primary(Parser *parser) { // TODO: Proper literals
	if (parser_match(parser, TOKEN_FALSE))
		return expr_literal(parser->arena, literal(BOOL, .boolean = false));
	if (parser_match(parser, TOKEN_TRUE))
		return expr_literal(parser->arena, literal(BOOL, .boolean = true));
	if (parser_match(parser, TOKEN_NIL))
		return expr_literal(parser->arena, literal(NIL, .boolean = true));

	if (parser_match(parser, TOKEN_INTEGER, TOKEN_FLOAT))
		return expr_literal(parser->arena, literal(NUMBER, .number = string_to_f64(parser->previous.lexeme)));
	if (parser_match(parser, TOKEN_STRING))
		return expr_literal(parser->arena, literal(STRING, .string = parser->previous.lexeme));
	if (parser_match(parser, TOKEN_IDENTIFIER)) {
		return expr_variable(parser->arena, parser->previous);
	}

	if (parser_match(parser, TOKEN_OPEN_PAREN)) {
		Expr *expr = parse_expression(parser);
		lexer_expect(parser->lexer, TOKEN_CLOSE_PAREN);

		return expr_grouping(parser->arena, expr);
	}

	parser->panic = true;
	ASSERT_FORMAT(false, "Unhandled token '%.*s' parsed", sarg(parser_peek(parser).lexeme));
	return NULL;
}

Expr *parse_unary(Parser *parser) {
	if (parser_match(parser, TOKEN_BANG, TOKEN_MINUS, TOKEN_MINUS_MINUS, TOKEN_PLUS_PLUS)) {
		Token operator = parser->previous;
		Expr *right = parse_unary(parser);
		return expr_unary(parser->arena, operator, right);
	}

	return parse_primary(parser);
}

Expr *parse_factor(Parser *parser) {
	Expr *expr = parse_unary(parser);

	while (parser_match(parser, TOKEN_STAR, TOKEN_SLASH)) {
		Token operator = parser->previous;
		Expr *right = parse_unary(parser);
		expr = expr_binary(parser->arena, expr, operator, right);
	}

	return expr;
}

Expr *parse_term(Parser *parser) {
	Expr *expr = parse_factor(parser);

	while (parser_match(parser, TOKEN_MINUS, TOKEN_PLUS)) {
		Token operator = parser->previous;
		Expr *right = parse_factor(parser);
		expr = expr_binary(parser->arena, expr, operator, right);
	}

	return expr;
}

Expr *parse_comparison(Parser *parser) {
	Expr *expr = parse_term(parser);

	while (parser_match(parser, TOKEN_GREATER, TOKEN_GREATER_EQUAL, TOKEN_LESS, TOKEN_LESS_EQUAL)) {
		Token operator = parser->previous;
		Expr *right = parse_term(parser);

		expr = expr_binary(parser->arena, expr, operator, right);
	}

	return expr;
}

Expr *parse_equality(Parser *parser) {
	Expr *expr = parse_comparison(parser);

	while (parser_match(parser, TOKEN_BANG_EQUAL, TOKEN_EQUAL_EQUAL)) {
		Token operator = parser->previous;
		Expr *right = parse_comparison(parser);
		expr = expr_binary(parser->arena, expr, operator, right);
	}

	return expr;
}

Expr *parse_ternary(Parser *parser) {
	Expr *expr = parse_equality(parser);

	if (parser_match(parser, TOKEN_QUESTION_MARK)) {
		Expr *then = parse_ternary(parser);
		if (parser_consume(parser, TOKEN_COLON, S("expected ':' after ternary")) == false)
			return NULL;
		Expr *otherwise = parse_ternary(parser);
		return expr_ternary(parser->arena, expr, then, otherwise);
	}

	return expr;
}

Expr *parse_assignment(Parser *parser) {
	Expr *expr = parse_ternary(parser);

	if (parser_match(parser, TOKEN_EQUAL)) {
		Token equals = parser->previous;
		Expr *value = parse_assignment(parser);

		if (expr->type == EXPR_VARIABLE) {
			Token name = expr->var.name;
			return expr_assign(parser->arena, name, value);
		}

		ASSERT_FORMAT(false, "invalid assignment target '%.*s'.", sarg(equals.lexeme));
	}

	return expr;
}

Expr *parse_comma(Parser *parser) {
	Expr *expr = parse_assignment(parser);

	while (parser_match(parser, TOKEN_COMMA)) {
		Token operator = parser->previous;
		Expr *right = parse_assignment(parser);
		expr = expr_binary(parser->arena, expr, operator, right);
	}
	return expr;
}

Expr *parse_expression(Parser *parser) {
	return parse_comma(parser);
}

Stmt parse_expression_statement(Parser *parser) {
	Stmt result = { .type = STMT_EXPRESSION };
	result.expression = parse_expression(parser);

	if (parser_consume(parser, TOKEN_SEMICOLON, S("expected ';' after expression statement.")) == false)
		return INVALID_STATMENT;
	return result;
}

Stmt parse_print_statement(Parser *parser) {
	Stmt result = { .type = STMT_PRINT };
	result.expression = parse_expression(parser);
	if (parser_consume(parser, TOKEN_SEMICOLON, S("expected ';' after value.")) == false)
		return INVALID_STATMENT;

	return result;
}

Stmt parse_statement(Parser *parser) {
	if (parser_match(parser, TOKEN_PRINT))
		return parse_print_statement(parser);

	return parse_expression_statement(parser);
}

Stmt parse_variable_declaration(Parser *parser) {
	Stmt result = { .type = STMT_VARIABLE };
	if (parser_consume(parser, TOKEN_IDENTIFIER, S("expected variable name.")) == false)
		return INVALID_STATMENT;
	result.var.name = parser->previous;

	result.var.initializer = NULL;
	if (parser_match(parser, TOKEN_EQUAL))
		result.var.initializer = parse_expression(parser);

	if (parser_consume(parser, TOKEN_SEMICOLON, S("expected ';' after variable declaration.")) == false)
		return INVALID_STATMENT;

	return result;
}

Stmt parse_declaration(Parser *parser) {
	Stmt result = { 0 };
	if (parser_match(parser, TOKEN_VAR)) {
		result = parse_variable_declaration(parser);

	} else {
		result = parse_statement(parser);
	}

	if (parser->panic) {
		parser_synchronize(parser);
		return INVALID_STATMENT;
	}

	return result;
}

Stmt *parser_parse(Parser *parser) {
	Stmt *statements = NULL;

	while (parser_at_end(parser) == false)
		arena_darray_put(parser->arena, statements, Stmt, parse_declaration(parser));

	return statements;
}

int main(void) {
	ArenaTemp scratch = arena_scratch_begin(NULL);
	Bytes file = filesystem_read(scratch.arena, S("../tools/src/test.lox"));

	Lexer lexer = lexer_make(file, countof(lox_keywords), lox_keywords);

	Parser parser = parser_make(scratch.arena, &lexer);
	Stmt *stmts = parser_parse(&parser);

	Interpreter interpreter = interpreter_make(scratch.arena, &parser);
	interpreter_interpret(&interpreter, stmts);

	Token token = { 0 };
	while ((token = lexer_next(&lexer)).type != TOKEN_EOF) {
		LOG_INFO("'%.*s' -> %u", sarg(token.lexeme), token.type);
	}

	LOG_INFO("Hello world!");

	arena_scratch_end(scratch);
	return 0;
}
