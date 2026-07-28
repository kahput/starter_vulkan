#include "lexer.h"
#include "common.h"
#include "core/arena.h"
#include "core/debug.h"
#include "core/logger.h"
#include "core/strings.h"
#include <string.h>

// clang-format off
const String8 token_type_to_string[TOKEN_MAX] = {
    [TOKEN_UNKNOWN]       = str_comp("UNKNOWN"),
    [TOKEN_OPEN_PAREN]    = str_comp("("),   [TOKEN_CLOSE_PAREN]   = str_comp(")"),
    [TOKEN_OPEN_BRACE]    = str_comp("{"),   [TOKEN_CLOSE_BRACE]   = str_comp("}"),
    [TOKEN_OPEN_BRACKET]  = str_comp("["),   [TOKEN_CLOSE_BRACKET] = str_comp("]"),
    [TOKEN_COMMA]         = str_comp(","),   [TOKEN_DOT]           = str_comp("."),
    [TOKEN_SEMICOLON]     = str_comp(";"),   [TOKEN_COLON]         = str_comp(":"),
    [TOKEN_SLASH]         = str_comp("/"),   [TOKEN_STAR]          = str_comp("*"),
    [TOKEN_PERCENT]       = str_comp("%"),   [TOKEN_TILDE]         = str_comp("~"),
    [TOKEN_CARET]         = str_comp("^"),   [TOKEN_QUESTION_MARK] = str_comp("?"),
    [TOKEN_MINUS]         = str_comp("-"),   [TOKEN_MINUS_MINUS]   = str_comp("--"),
    [TOKEN_PLUS]          = str_comp("+"),   [TOKEN_PLUS_PLUS]     = str_comp("++"),
    [TOKEN_BANG]          = str_comp("!"),   [TOKEN_BANG_EQUAL]    = str_comp("!="),
    [TOKEN_EQUAL]         = str_comp("="),   [TOKEN_EQUAL_EQUAL]   = str_comp("=="),
    [TOKEN_GREATER]       = str_comp(">"),   [TOKEN_GREATER_EQUAL] = str_comp(">="),
    [TOKEN_LESS]          = str_comp("<"),   [TOKEN_LESS_EQUAL]    = str_comp("<="),
    [TOKEN_AMP]           = str_comp("&"),   [TOKEN_AMP_AMP]       = str_comp("&&"),
    [TOKEN_PIPE]          = str_comp("|"),   [TOKEN_PIPE_PIPE]     = str_comp("||"),
    [TOKEN_IDENTIFIER]    = str_comp("IDENTIFIER"),
    [TOKEN_STRING]        = str_comp("STRING"),
    [TOKEN_INTEGER]       = str_comp("INTEGER"),
    [TOKEN_FLOAT]         = str_comp("FLOAT"),

    [TOKEN_EOF]           = str_comp("EOF"),
};
// clang-format on

/* static bool is_whitespace(char *at) { return at[0] == ' ' || at[0] == '\t' || at[0] == '\r' || at[0] == '\n'; } */
static bool is_newline(char c) { return c == '\r' || c == '\n'; }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_aplha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static bool is_alnum(char c) { return is_aplha(c) || is_digit(c); }

static bool lexer___at_end(Lexer *lexer) { return lexer->at[0] == 0 || (uint32_t)(lexer->at - lexer->source.text) >= lexer->source.length; }
static void lexer__advance_newline(Lexer *lexer) {
	if (lexer->at[0] == '\r' && lexer->at[1] == '\n')
		++lexer->at;
	++lexer->at;
	++lexer->line;
	lexer->column = 1;
}

void lexer__skip_whitespace_and_comments(Lexer *lexer) {
	while (lexer___at_end(lexer) == false) {
		char c = lexer->at[0];

		if (c == '\r' || c == '\n')
			lexer__advance_newline(lexer);

		else if (c == ' ' || c == '\t') {
			++lexer->at;
			++lexer->column;
		}

		else if (c == '/' && lexer->at[1] == '/')
			while (lexer___at_end(lexer) == false && is_newline(lexer->at[0]) == false)
				++lexer->at;

		else if (c == '/' && lexer->at[1] == '*') {
			lexer->at += 2;
			lexer->column += 2;
			while (lexer___at_end(lexer) == false) {
				if (lexer->at[0] == '*' && lexer->at[1] == '/') {
					lexer->at += 2;
					lexer->column += 2;
					break;
				}
				if (is_newline(lexer->at[0]))
					lexer__advance_newline(lexer);
				else {
					++lexer->at;
					++lexer->column;
				}
			}
		} else
			break;
	}
}

TokenType lexer__match_keyword(Lexer *lexer, Token *token) {
	for (uint32_t index = 0; index < lexer->keyword_count; ++index)
		if (str8_equals(lexer->keywords[index], token->lexeme))
			return TOKEN_KEYWORD_0 + index;

	/* switch (token->string.chars[0]) { */
	/* 	case 't': */
	/* 		if (token->string.length == 4) */
	/* 			if (memory_equals(token->string.chars, token_type_names[TOKEN_TRUE], token->string.length)) */
	/* 				return TOKEN_TRUE; */
	/* 		if (token->string.length == 7) */
	/* 			if (memory_equals(token->string.chars, token_type_names[TOKEN_TYPEDEF], token->string.length)) */
	/* 				return TOKEN_TYPEDEF; */
	/* 		break; */

	/* 	case 's': */
	/* 		if (token->string.length == 6) */
	/* 			if (memory_equals(token->string.chars, token_type_names[TOKEN_STRUCT], token->string.length)) */
	/* 				return TOKEN_STRUCT; */
	/* 		break; */

	/* 	case 'f': */
	/* 		if (token->string.length == 5) */
	/* 			if (memory_equals(token->string.chars, token_type_names[TOKEN_FALSE], token->string.length)) */
	/* 				return TOKEN_FALSE; */
	/* 		break; */

	/* 	case 'n': */
	/* 		if (token->string.length == 4) */
	/* 			if (memory_equals(token->string.chars, token_type_names[TOKEN_NULL], token->string.length)) */
	/* 				return TOKEN_NULL; */
	/* 		break; */
	/* 	case 'u': */
	/* 		if (token->string.length == 5) */
	/* 			if (memory_equals(token->string.chars, token_type_names[TOKEN_UNION], token->string.length)) */
	/* 				return TOKEN_UNION; */
	/* 		break; */

	/* 	case 'c': */
	/* 		if (token->string.length == 5) */
	/* 			if (memory_equals(token->string.chars, token_type_names[TOKEN_CONST], token->string.length)) */
	/* 				return TOKEN_CONST; */
	/* 		break; */
	/* } */

	return TOKEN_IDENTIFIER;
}

Token lexer__scan_token(Lexer *lexer) {
	lexer__skip_whitespace_and_comments(lexer);

	if (lexer->at == NULL || lexer___at_end(lexer))
		return (Token){ .type = TOKEN_EOF, .line = lexer->line, .column = lexer->column };

	Token token = {
		.type = TOKEN_UNKNOWN,
		.lexeme = { .text = lexer->at, .length = 1 },
		.line = lexer->line,
		.column = lexer->column,
	};

	char c = lexer->at[0];
	++lexer->at;
	++lexer->column;

	switch (c) {
			// clang-format off
		case '(': token.type = TOKEN_OPEN_PAREN; break;
        case ')': token.type = TOKEN_CLOSE_PAREN;   break;
        case '{': token.type = TOKEN_OPEN_BRACE;    break;
        case '}': token.type = TOKEN_CLOSE_BRACE;   break;
        case '[': token.type = TOKEN_OPEN_BRACKET;  break;
        case ']': token.type = TOKEN_CLOSE_BRACKET; break;
        case ',': token.type = TOKEN_COMMA;         break;
        case '.': token.type = TOKEN_DOT;           break;
        case ';': token.type = TOKEN_SEMICOLON;     break;
        case ':': token.type = TOKEN_COLON;         break;
        case '/': token.type = TOKEN_SLASH;         break;
        case '*': token.type = TOKEN_STAR;          break;
        case '%': token.type = TOKEN_PERCENT;       break;
        case '~': token.type = TOKEN_TILDE;         break;
        case '^': token.type = TOKEN_CARET;         break;
        case '?': token.type = TOKEN_QUESTION_MARK; break;
			// clang-format on

		case '-': {
			if (lexer->at[0] == '-') {
				++lexer->at;
				++lexer->column;
				token.type = TOKEN_MINUS_MINUS;
				token.lexeme.length = 2;
			} else
				token.type = TOKEN_MINUS;
		} break;
		case '+': {
			if (lexer->at[0] == '+') {
				++lexer->at;
				++lexer->column;
				token.type = TOKEN_PLUS_PLUS;
				token.lexeme.length = 2;
			} else
				token.type = TOKEN_PLUS;
		} break;
		case '!': {
			if (lexer->at[0] == '=') {
				++lexer->at;
				++lexer->column;
				token.type = TOKEN_BANG_EQUAL;
				token.lexeme.length = 2;
			} else
				token.type = TOKEN_BANG;
		} break;
		case '=': {
			if (lexer->at[0] == '=') {
				++lexer->at;
				++lexer->column;
				token.type = TOKEN_EQUAL_EQUAL;
				token.lexeme.length = 2;
			} else
				token.type = TOKEN_EQUAL;
		} break;
		case '>': {
			if (lexer->at[0] == '=') {
				++lexer->at;
				++lexer->column;
				token.type = TOKEN_GREATER_EQUAL;
				token.lexeme.length = 2;
			} else
				token.type = TOKEN_GREATER;
		} break;
		case '<': {
			if (lexer->at[0] == '=') {
				++lexer->at;
				++lexer->column;
				token.type = TOKEN_LESS_EQUAL;
				token.lexeme.length = 2;
			} else
				token.type = TOKEN_LESS;
		} break;
		case '&': {
			if (lexer->at[0] == '&') {
				++lexer->at;
				++lexer->column;
				token.type = TOKEN_AMP_AMP;
				token.lexeme.length = 2;
			} else
				token.type = TOKEN_AMP;
		} break;
		case '|': {
			if (lexer->at[0] == '|') {
				++lexer->at;
				++lexer->column;
				token.type = TOKEN_PIPE_PIPE;
				token.lexeme.length = 2;
			} else
				token.type = TOKEN_PIPE;
		} break;
		case '#': {
			// TODO: Handle macros
			bool newline_break = false;
			if (memory_equals(lexer->at, "define WRAPPER", sizeof("define WRAPPER") - 1)) {
				uint32_t y = 0;
			}
			while (lexer___at_end(lexer) == false) {
				++lexer->at;
				if (lexer->at[0] == '\\')
					newline_break = true;

				if (lexer->at[0] == '\n' && newline_break == false)
					break;
				if (lexer->at[0] == '\n' && newline_break)
					newline_break = false;
			}
			break;
		} break;

		case '"': {
			token.type = TOKEN_STRING;
			token.lexeme.text = lexer->at;
			while (lexer___at_end(lexer) == false && lexer->at[0] != '"') {
				if (lexer->at[0] == '\\' && lexer->at[1] != '\0') {
					++lexer->at;
					++lexer->column;
				}
				++lexer->at;
				++lexer->column;
			}

			token.lexeme.length = (int)(lexer->at - token.lexeme.text);
			if (lexer->at[0] == '"') {
				++lexer->at;
				++lexer->column;
			}
		} break;

		case '\0':
			return (Token){ .type = TOKEN_EOF };
		default: {
			if (is_digit(c)) {
				bool is_float = false;

				while (is_digit(lexer->at[0])) {
					++lexer->at;
					++lexer->column;
				}
				if (lexer->at[0] == '.' && is_digit(lexer->at[1])) {
					is_float = true;
					++lexer->at;
					++lexer->column;
					while (is_digit(lexer->at[0])) {
						++lexer->at;
						++lexer->column;
					}
				}
				if (lexer->at[0] == 'e' || lexer->at[0] == 'E') {
					is_float = true;
					++lexer->at;
					++lexer->column;
					if (lexer->at[0] == '+' || lexer->at[0] == '-') {
						++lexer->at;
						++lexer->column;
					}
					while (is_digit(lexer->at[0])) {
						++lexer->at;
						++lexer->column;
					}
				}
				token.lexeme.length = (int)(lexer->at - token.lexeme.text);
				token.type = is_float ? TOKEN_FLOAT : TOKEN_INTEGER;

			} else if (is_aplha(c)) {
				while (is_alnum(lexer->at[0]))
					++lexer->at;
				token.lexeme.length = lexer->at - token.lexeme.text;
				token.type = lexer__match_keyword(lexer, &token);
			} else
				break;
		}
	}

	return token;
}

Token lexer_next(Lexer *lexer) {
	if (lexer->has_peeked) {
		lexer->has_peeked = false;
		return lexer->peeked;
	}
	return lexer__scan_token(lexer);
}
Token lexer_peek(Lexer *lexer) {
	if (!lexer->has_peeked) {
		lexer->peeked = lexer__scan_token(lexer);
		lexer->has_peeked = true;
	}

	return lexer->peeked;
}

bool lexer_match(Lexer *lexer, TokenType type, Token *out) {
	if (lexer_peek(lexer).type != type)
		return false;
	if (out)
		*out = lexer->peeked;
	lexer->has_peeked = false;
	return true;
}

Token lexer_expect(Lexer *lexer, TokenType type) {
	Token t = lexer_next(lexer);
	if (t.type != type) {
		ASSERT(false);
		LOG_WARN("Lexer: expected '%.*s' got '%.*s' (%.*s) at %d:%d",
			str_spread(token_type_to_string[type]),
			str_spread(token_type_to_string[t.type]),
			str_spread(t.lexeme),
			t.line, t.column);
	}
	return t;
}

Token lexer_expect_multiple(Lexer *lexer, TokenType *types, uint32_t type_count) {
	Token t = lexer_next(lexer);
	bool found = false;
	for (uint32_t index = 0; index < type_count; ++index) {
		if (t.type == types[index]) {
			found = true;
			break;
		}
	}

	if (found == false) {
		ArenaTemp scratch = arena_scratch_begin(0);
		String8 types_string = { scratch.arena->base, 0 };
		for (uint32_t index = 0; index < type_count; ++index) {
			String8 type_string = token_type_to_string[types[index]];
			bool last = index == type_count - 1;

			char *concat = arena_push_count(scratch.arena, char, type_string.length + (last ? 0 : 3));
			types_string.length += type_string.length + (last ? 0 : 3);

			memory_copy(concat, type_string.text, type_string.length);
			if (last == false) {
				concat[type_string.length] = ' ';
				concat[type_string.length + 1] = '|';
				concat[type_string.length + 2] = ' ';
			}
		}

		LOG_WARN("Lexer: expected '%.*s' got '%.*s' (%.*s) at %d:%d",
			str_spread(types_string),
			str_spread(token_type_to_string[t.type]),
			str_spread(t.lexeme),
			t.line, t.column);
		arena_scratch_end(scratch);
	}

	return t;
}

bool lexer_at_end(Lexer *lexer) {
	return lexer_peek(lexer).type == TOKEN_EOF;
}
