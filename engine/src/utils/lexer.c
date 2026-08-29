#include "lexer.h"
#include "common.h"
#include "core/arena.h"
#include "core/debug.h"
#include "core/logger.h"
#include "core/strings.h"
#include <string.h>

// clang-format off
const String8 token_type_to_string[TOKEN_MAX] = {
    [TOKEN_UNKNOWN]       = scomp("unkown"),
    [TOKEN_LPAREN]    = scomp("("),   [TOKEN_RPAREN]   = scomp(")"),
    [TOKEN_LBRACE]    = scomp("{"),   [TOKEN_RBRACE]   = scomp("}"),
    [TOKEN_LBRACKET]  = scomp("["),   [TOKEN_RBRACKET] = scomp("]"),
    [TOKEN_COMMA]         = scomp(","),   [TOKEN_DOT]           = scomp("."),
    [TOKEN_SEMICOLON]     = scomp(";"),   [TOKEN_COLON]         = scomp(":"),
    [TOKEN_SLASH]         = scomp("/"),   [TOKEN_STAR]          = scomp("*"),
    [TOKEN_PERCENT]       = scomp("%"),   [TOKEN_TILDE]         = scomp("~"),
    [TOKEN_DOLLAR] = scomp("$"),
    [TOKEN_CARET]         = scomp("^"),   [TOKEN_QUESTION_MARK] = scomp("?"),
    [TOKEN_MINUS]         = scomp("-"),   [TOKEN_MINUS_MINUS]   = scomp("--"),
    [TOKEN_PLUS]          = scomp("+"),   [TOKEN_PLUS_PLUS]     = scomp("++"),
    [TOKEN_BANG]          = scomp("!"),   [TOKEN_BANG_EQUAL]    = scomp("!="),
    [TOKEN_EQUAL]         = scomp("="),   [TOKEN_EQUAL_EQUAL]   = scomp("=="),
    [TOKEN_GREATER]       = scomp(">"),   [TOKEN_GREATER_EQUAL] = scomp(">="),
    [TOKEN_LESS]          = scomp("<"),   [TOKEN_LESS_EQUAL]    = scomp("<="),
    [TOKEN_AMP]           = scomp("&"),   [TOKEN_AMP_AMP]       = scomp("&&"),
    [TOKEN_PIPE]          = scomp("|"),   [TOKEN_PIPE_PIPE]     = scomp("||"),
    [TOKEN_IDENTIFIER]    = scomp("identifier"),
    [TOKEN_STRING]        = scomp("string"),
    [TOKEN_INTEGER]       = scomp("integer"),
    [TOKEN_REAL]         = scomp("real"),

    [TOKEN_EOF]           = scomp("end of file"),
};
// clang-format on

/* static bool is_whitespace(char *at) { return at[0] == ' ' || at[0] == '\t' || at[0] == '\r' || at[0] == '\n'; } */
static bool is_newline(char c) { return c == '\r' || c == '\n'; }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_aplha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static bool is_alnum(char c) { return is_aplha(c) || is_digit(c); }

static bool lexer___at_end(Lexer *lexer) { return lexer->cursor[0] == 0 || (uint32_t)(lexer->cursor - lexer->source.text) >= lexer->source.length; }
static void lexer__advance_newline(Lexer *lexer) {
	if (lexer->cursor[0] == '\r' && lexer->cursor[1] == '\n')
		++lexer->cursor;
	++lexer->cursor;
	++lexer->line;
	lexer->column = 1;
}

void lexer__skip_whitespace_and_comments(Lexer *lexer) {
	while (lexer___at_end(lexer) == false) {
		char c = lexer->cursor[0];

		if (c == '\r' || c == '\n')
			lexer__advance_newline(lexer);

		else if (c == ' ' || c == '\t') {
			++lexer->cursor;
			++lexer->column;
		}

		else if (c == '/' && lexer->cursor[1] == '/')
			while (lexer___at_end(lexer) == false && is_newline(lexer->cursor[0]) == false)
				++lexer->cursor;

		else if (c == '/' && lexer->cursor[1] == '*') {
			lexer->cursor += 2;
			lexer->column += 2;
			while (lexer___at_end(lexer) == false) {
				if (lexer->cursor[0] == '*' && lexer->cursor[1] == '/') {
					lexer->cursor += 2;
					lexer->column += 2;
					break;
				}
				if (is_newline(lexer->cursor[0]))
					lexer__advance_newline(lexer);
				else {
					++lexer->cursor;
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
restart:

	lexer__skip_whitespace_and_comments(lexer);

	if (lexer->cursor == NULL || lexer___at_end(lexer))
		return (Token){ .type = TOKEN_EOF, .line = lexer->line, .column = lexer->column };

	Token token = {
		.type = TOKEN_UNKNOWN,
		.lexeme = { .text = lexer->cursor, .length = 1 },
		.line = lexer->line,
		.column = lexer->column,
	};

	char c = lexer->cursor[0];
	++lexer->cursor;
	++lexer->column;

	switch (c) {
			// clang-format off
		case '(': token.type = TOKEN_LPAREN; break;
        case ')': token.type = TOKEN_RPAREN;   break;
        case '{': token.type = TOKEN_LBRACE;    break;
        case '}': token.type = TOKEN_RBRACE;   break;
        case '[': token.type = TOKEN_LBRACKET;  break;
        case ']': token.type = TOKEN_RBRACKET; break;
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
        case '$': token.type = TOKEN_DOLLAR; break;
			// clang-format on

		case '-': {
			if (lexer->cursor[0] == '-') {
				++lexer->cursor;
				++lexer->column;
				token.type = TOKEN_MINUS_MINUS;
				token.lexeme.length = 2;
			} else
				token.type = TOKEN_MINUS;
		} break;
		case '+': {
			if (lexer->cursor[0] == '+') {
				++lexer->cursor;
				++lexer->column;
				token.type = TOKEN_PLUS_PLUS;
				token.lexeme.length = 2;
			} else
				token.type = TOKEN_PLUS;
		} break;
		case '!': {
			if (lexer->cursor[0] == '=') {
				++lexer->cursor;
				++lexer->column;
				token.type = TOKEN_BANG_EQUAL;
				token.lexeme.length = 2;
			} else
				token.type = TOKEN_BANG;
		} break;
		case '=': {
			if (lexer->cursor[0] == '=') {
				++lexer->cursor;
				++lexer->column;
				token.type = TOKEN_EQUAL_EQUAL;
				token.lexeme.length = 2;
			} else
				token.type = TOKEN_EQUAL;
		} break;
		case '>': {
			if (lexer->cursor[0] == '=') {
				++lexer->cursor;
				++lexer->column;
				token.type = TOKEN_GREATER_EQUAL;
				token.lexeme.length = 2;
			} else
				token.type = TOKEN_GREATER;
		} break;
		case '<': {
			if (lexer->cursor[0] == '=') {
				++lexer->cursor;
				++lexer->column;
				token.type = TOKEN_LESS_EQUAL;
				token.lexeme.length = 2;
			} else
				token.type = TOKEN_LESS;
		} break;
		case '&': {
			if (lexer->cursor[0] == '&') {
				++lexer->cursor;
				++lexer->column;
				token.type = TOKEN_AMP_AMP;
				token.lexeme.length = 2;
			} else
				token.type = TOKEN_AMP;
		} break;
		case '|': {
			if (lexer->cursor[0] == '|') {
				++lexer->cursor;
				++lexer->column;
				token.type = TOKEN_PIPE_PIPE;
				token.lexeme.length = 2;
			} else
				token.type = TOKEN_PIPE;
		} break;
		case '#': {
			// TODO: Handle macros
			bool newline_break = false;
			if (memory_equals(lexer->cursor, "define WRAPPER", sizeof("define WRAPPER") - 1)) {
				uint32_t y = 0;
			}
			while (lexer___at_end(lexer) == false) {
				++lexer->cursor;
				if (lexer->cursor[0] == '\\')
					newline_break = true;

				if (lexer->cursor[0] == '\n' && newline_break == false)
					break;
				if (lexer->cursor[0] == '\n' && newline_break)
					newline_break = false;
			}
			goto restart;
			break;
		} break;

		case '"': {
			token.type = TOKEN_STRING;
			token.lexeme.text = lexer->cursor;
			while (lexer___at_end(lexer) == false && lexer->cursor[0] != '"') {
				if (lexer->cursor[0] == '\\' && lexer->cursor[1] != '\0') {
					++lexer->cursor;
					++lexer->column;
				}
				++lexer->cursor;
				++lexer->column;
			}

			token.lexeme.length = (int)(lexer->cursor - token.lexeme.text);
			if (lexer->cursor[0] == '"') {
				++lexer->cursor;
				++lexer->column;
			}
		} break;

		case '\0':
			token = (Token){ .type = TOKEN_EOF };
			break;
		default: {
			if (is_digit(c)) {
				bool is_float = false;

				while (is_digit(lexer->cursor[0])) {
					++lexer->cursor;
					++lexer->column;
				}
				if (lexer->cursor[0] == '.' && is_digit(lexer->cursor[1])) {
					is_float = true;
					++lexer->cursor;
					++lexer->column;
					while (is_digit(lexer->cursor[0])) {
						++lexer->cursor;
						++lexer->column;
					}
				}
				if (lexer->cursor[0] == 'e' || lexer->cursor[0] == 'E') {
					is_float = true;
					++lexer->cursor;
					++lexer->column;
					if (lexer->cursor[0] == '+' || lexer->cursor[0] == '-') {
						++lexer->cursor;
						++lexer->column;
					}
					while (is_digit(lexer->cursor[0])) {
						++lexer->cursor;
						++lexer->column;
					}
				}
				token.lexeme.length = (int)(lexer->cursor - token.lexeme.text);
				token.type = is_float ? TOKEN_REAL : TOKEN_INTEGER;

			} else if (is_aplha(c)) {
				while (is_alnum(lexer->cursor[0])) {
					++lexer->cursor;
					++lexer->column;
				}
				token.lexeme.length = lexer->cursor - token.lexeme.text;
				token.type = lexer__match_keyword(lexer, &token);
			} else
				break;
		}
	}

	return token;
}

Token lexer_advance(Lexer *lexer) {
	Token result = { .type = TOKEN_UNKNOWN };
	if (lexer->has_peeked) {
		lexer->has_peeked = false;
		result = lexer->peeked;
	} else
		result = lexer__scan_token(lexer);

	lexer->current = result;
	return result;
}

char lexer_advance_char(Lexer *lexer) {
	lexer->has_peeked = false;

	char result = lexer->cursor[0];
	if (is_newline(result))
		lexer__advance_newline(lexer);
	else {
		++lexer->cursor;
		++lexer->column;
	}

	return result;
}

void lexer_advance_next_line(Lexer *lexer) {
	for (char c = lexer->cursor[0]; lexer_at_end(lexer) == false && is_newline(c) == false; c = lexer_advance_char(lexer)) {}
	lexer__advance_newline(lexer);
}
Token lexer_peek(Lexer *lexer) {
	if (!lexer->has_peeked) {
		lexer->peeked = lexer__scan_token(lexer);
		lexer->has_peeked = true;
	}

	return lexer->peeked;
}

uint8_t *lexer_skip_to_end_of_line(Lexer *lexer) {
	while (lexer___at_end(lexer) == false && is_newline(lexer->cursor[0]) == false)
		++lexer->cursor;
	uint8_t *tail = lexer->cursor - 1;
	lexer__advance_newline(lexer);

	return tail;
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
	Token t = lexer_advance(lexer);
	if (t.type != type) {
		LOG_WARN("expected '%.*s' got '%.*s' (%.*s) at %d:%d",
			sspread(token_type_to_string[type]),
			sspread(token_type_to_string[t.type]),
			sspread(t.lexeme),
			t.line, t.column);
		ASSERT(false);
	}
	return t;
}

String8 lexer_error_location_string(Arena *arena, Token token) {
	String8 result = { 0 };

	bool ok = arena && token.lexeme.text;
	if (ok) {
		uint32_t col = token.type == TOKEN_STRING ? token.column : (token.column - 1);
		String8 line = { .text = token.lexeme.text - col, .length = token.lexeme.length };
		while (line.text[line.length] != '\n' && line.text[line.length] != '\0')
			line.length++;

		String8 error_top = str8_pushf(arena, s("    %d | %.*s\n"), token.line, sspread(line));
		uint32_t error_offset = 0;
		while (error_top.text[error_offset] != '|')
			error_offset++;
		error_offset++;

		error_offset += col;
		String8 indent = str8_indent(arena, s(" "), error_offset);

		result = str8_concat(arena, error_top, str8_concat(arena, indent, s("^-- here")));
	}

	return result;
}

void report(uint64_t line, uint64_t column, String8 where, String8 message) {
	LOG_ERROR("#error[%d:%d]: %.*s\n%.*s", line, column, sspread(message), sspread(where));
}

Token lexer_consume(Lexer *lexer, TokenType type, String8 message) {
	Token token = lexer_peek(lexer);

	bool had_error = false;
	if (token.type != type) {
		if (message.length && lexer->had_error == false) {
			had_error = lexer->had_error = true;

			ArenaTemp scratch = arena_scratch_begin(0);

			report(lexer->current.line, lexer->current.column, lexer_error_location_string(scratch.arena, lexer->current), message);
			arena_scratch_end(scratch);
		}
	}

	return had_error ? token : lexer_advance(lexer);
}

Token lexer_expect_multiple(Lexer *lexer, TokenType *types, uint32_t type_count) {
	Token t = lexer_advance(lexer);
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
			sspread(types_string),
			sspread(token_type_to_string[t.type]),
			sspread(t.lexeme),
			t.line, t.column);
		arena_scratch_end(scratch);
	}

	return t;
}

bool lexer_at_end(Lexer *lexer) {
	return lexer_peek(lexer).type == TOKEN_EOF;
}
