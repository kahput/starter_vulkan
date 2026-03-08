#include "lexer.h"
#include "core/debug.h"
#include "core/logger.h"
#include <string.h>

// clang-format off
const char *token_type_names[TOKEN_TYPE_COUNT] = {
    [TOKEN_UNKNOWN]       = "UNKNOWN",
    [TOKEN_LEFT_PAREN]    = "(",   [TOKEN_RIGHT_PAREN]   = ")",
    [TOKEN_LEFT_BRACE]    = "{",   [TOKEN_RIGHT_BRACE]   = "}",
    [TOKEN_LEFT_BRACKET]  = "[",   [TOKEN_RIGHT_BRACKET] = "]",
    [TOKEN_COMMA]         = ",",   [TOKEN_DOT]           = ".",
    [TOKEN_MINUS]         = "-",   [TOKEN_PLUS]          = "+",
    [TOKEN_SEMICOLON]     = ";",   [TOKEN_COLON]         = ":",
    [TOKEN_SLASH]         = "/",   [TOKEN_STAR]          = "*",
    [TOKEN_PERCENT]       = "%",
    [TOKEN_BANG]          = "!",   [TOKEN_BANG_EQUAL]    = "!=",
    [TOKEN_EQUAL]         = "=",   [TOKEN_EQUAL_EQUAL]   = "==",
    [TOKEN_GREATER]       = ">",   [TOKEN_GREATER_EQUAL] = ">=",
    [TOKEN_LESS]          = "<",   [TOKEN_LESS_EQUAL]    = "<=",
    [TOKEN_AMP]           = "&",   [TOKEN_AMP_AMP]       = "&&",
    [TOKEN_PIPE]          = "|",   [TOKEN_PIPE_PIPE]     = "||",
    [TOKEN_IDENTIFIER]    = "IDENTIFIER",
    [TOKEN_STRING]        = "STRING",
    [TOKEN_INTEGER]       = "INTEGER",
    [TOKEN_FLOAT]         = "FLOAT",
    [TOKEN_TRUE]          = "true",
    [TOKEN_FALSE]         = "false",
    [TOKEN_NULL]          = "null",
    [TOKEN_EOF]           = "EOF",
};
// clang-format on

/* static bool is_whitespace(char *at) { return at[0] == ' ' || at[0] == '\t' || at[0] == '\r' || at[0] == '\n'; } */
static bool is_newline(char c) { return c == '\r' || c == '\n'; }
static bool is_at_end(char c) { return c == 0; }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_aplha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static bool is_alnum(char c) { return is_aplha(c) || is_digit(c); }

static void advance_newline(Lexer *lexer) {
	if (lexer->at[0] == '\r' && lexer->at[1] == '\n')
		++lexer->at;
	++lexer->at;
	++lexer->line;
	lexer->column = 1;
}

void skip_whitespace_and_comments(Lexer *lexer) {
	while (lexer->at) {
		char c = lexer->at[0];

		if (c == '\r' || c == '\n')
			advance_newline(lexer);

		else if (c == ' ' || c == '\t') {
			++lexer->at;
			++lexer->column;
		}

		else if (c == '/' && lexer->at[1] == '/')
			while (!is_at_end(lexer->at[0]) && !is_newline(lexer->at[0]))
				++lexer->at;

		else if (c == '/' && lexer->at[1] == '*') {
			lexer->at += 2;
			lexer->column += 2;
			while (!is_at_end(lexer->at[0])) {
				if (lexer->at[0] == '*' && lexer->at[1] == '/') {
					lexer->at += 2;
					lexer->column += 2;
					break;
				}
				if (is_newline(lexer->at[0]))
					advance_newline(lexer);
				else {
					++lexer->at;
					++lexer->column;
				}
			}
		} else
			break;
	}
}

TokenType match_keyword(Token *token) {
	switch (token->string.memory[0]) {
		case 't':
			if (token->string.length == 4)
				if (memcmp(token->string.memory, token_type_names[TOKEN_TRUE], token->string.length) == 0)
					return TOKEN_TRUE;
			break;

		case 'f':
			if (token->string.length == 5)
				if (memcmp(token->string.memory, token_type_names[TOKEN_FALSE], token->string.length) == 0)
					return TOKEN_FALSE;
			break;

		case 'n':
			if (token->string.length == 4)
				if (memcmp(token->string.memory, token_type_names[TOKEN_NULL], token->string.length) == 0)
					return TOKEN_NULL;
			break;
	}

	return TOKEN_IDENTIFIER;
}

Token scan_token(Lexer *lexer) {
	skip_whitespace_and_comments(lexer);

	if (lexer->at == NULL || is_at_end(lexer->at[0]))
		return (Token){ .type = TOKEN_EOF, .line = lexer->line, .column = lexer->column };

	Token token = {
		.type = TOKEN_UNKNOWN,
		.string = { .memory = lexer->at, .length = 1 },
		.line = lexer->line,
		.column = lexer->column,
	};

	char c = lexer->at[0];
	++lexer->at;
	++lexer->column;

	switch (c) {
			// clang-format off
		case '(': token.type = TOKEN_LEFT_PAREN; break;
        case ')': token.type = TOKEN_RIGHT_PAREN;   break;
        case '{': token.type = TOKEN_LEFT_BRACE;    break;
        case '}': token.type = TOKEN_RIGHT_BRACE;   break;
        case '[': token.type = TOKEN_LEFT_BRACKET;  break;
        case ']': token.type = TOKEN_RIGHT_BRACKET; break;
        case ',': token.type = TOKEN_COMMA;         break;
        case '.': token.type = TOKEN_DOT;           break;
        case '-': token.type = TOKEN_MINUS;         break;
        case '+': token.type = TOKEN_PLUS;          break;
        case ';': token.type = TOKEN_SEMICOLON;     break;
        case ':': token.type = TOKEN_COLON;         break;
        case '/': token.type = TOKEN_SLASH;         break;
        case '*': token.type = TOKEN_STAR;          break;
        case '%': token.type = TOKEN_PERCENT;       break;
			// clang-format on

		case '!': {
			if (lexer->at[0] == '=') {
				++lexer->at;
				++lexer->column;
				token.type = TOKEN_BANG_EQUAL;
				token.string.length = 2;
			} else
				token.type = TOKEN_BANG;
		} break;
		case '=': {
			if (lexer->at[0] == '=') {
				++lexer->at;
				++lexer->column;
				token.type = TOKEN_EQUAL_EQUAL;
				token.string.length = 2;
			} else
				token.type = TOKEN_EQUAL;
		} break;
		case '>': {
			if (lexer->at[0] == '=') {
				++lexer->at;
				++lexer->column;
				token.type = TOKEN_GREATER_EQUAL;
				token.string.length = 2;
			} else
				token.type = TOKEN_GREATER;
		} break;
		case '<': {
			if (lexer->at[0] == '=') {
				++lexer->at;
				++lexer->column;
				token.type = TOKEN_LESS_EQUAL;
				token.string.length = 2;
			} else
				token.type = TOKEN_LESS;
		} break;
		case '&': {
			if (lexer->at[0] == '&') {
				++lexer->at;
				++lexer->column;
				token.type = TOKEN_AMP_AMP;
				token.string.length = 2;
			} else
				token.type = TOKEN_AMP;
		} break;
		case '|': {
			if (lexer->at[0] == '|') {
				++lexer->at;
				++lexer->column;
				token.type = TOKEN_PIPE_PIPE;
				token.string.length = 2;
			} else
				token.type = TOKEN_PIPE;
		} break;

		case '"': {
			token.type = TOKEN_STRING;
			token.string.memory = lexer->at;
			while (!is_at_end(lexer->at[0]) && lexer->at[0] != '"') {
				if (lexer->at[0] == '\\' && lexer->at[1] != '\0') {
					++lexer->at;
					++lexer->column;
				}
				++lexer->at;
				++lexer->column;
			}

			token.string.length = (int)(lexer->at - token.string.memory);
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
				token.string.length = (int)(lexer->at - token.string.memory);
				token.type = is_float ? TOKEN_FLOAT : TOKEN_INTEGER;
			} else if (is_aplha(c)) {
				while (is_alnum(lexer->at[0]))
					++lexer->at;
				token.string.length = lexer->at - token.string.memory;
				token.type = match_keyword(&token);
			} else {
				ASSERT(0);
			}
		}
	}

	return token;
}

Token lexer_next(Lexer *lexer) {
	if (lexer->has_peeked) {
		lexer->has_peeked = false;
		return lexer->peeked;
	}
	return scan_token(lexer);
}
Token lexer_peek(Lexer *lexer) {
	if (!lexer->has_peeked) {
		lexer->peeked = scan_token(lexer);
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
	if (t.type != type)
		LOG_WARN("Lexer: expected '%s' got '%s' (%.*s) at %d:%d",
			token_type_names[type], token_type_names[t.type],
			t.string.length, t.string.memory, t.line, t.column);
	return t;
}

bool lexer_at_end(Lexer *lexer) {
	return lexer_peek(lexer).type == TOKEN_EOF;
}
