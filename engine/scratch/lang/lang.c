#include "os.h"
#include <core.h>
#include <stdio.h>

typedef enum {
	TOKEN_UNKNOWN,

	TOKEN_LPAREN, // (
	TOKEN_RPAREN, // )
	TOKEN_LBRACE, // {
	TOKEN_RBRACE, // }
	TOKEN_LBRACKET, // [
	TOKEN_RBRACKET, // ]
	TOKEN_COMMA, // ,
	TOKEN_DOT, // .
	TOKEN_SEMICOLON, // ;
	TOKEN_COLON, // :
	TOKEN_SLASH, // /
	TOKEN_STAR, // *
	TOKEN_PERCENT, // %
	TOKEN_DOLLAR, // $
	TOKEN_TILDE, // ~
	TOKEN_CARET, // ^
	TOKEN_QUESTION_MARK, // ?

	TOKEN_MINUS, // -
	TOKEN_MINUS_MINUS, // --
	TOKEN_PLUS, // +
	TOKEN_PLUS_PLUS, // ++
	TOKEN_BANG, // !
	TOKEN_BANG_EQUAL, // !=
	TOKEN_EQUAL, // =
	TOKEN_EQUAL_EQUAL, // ==
	TOKEN_GREATER, // >
	TOKEN_GREATER_EQUAL, // >=
	TOKEN_LESS, //
	TOKEN_LESS_EQUAL, // <=
	TOKEN_AMP, // &
	TOKEN_AMP_AMP, // &&
	TOKEN_PIPE, // |
	TOKEN_PIPE_PIPE, // ||

	TOKEN_IDENTIFIER,
	TOKEN_STRING,
	TOKEN_REAL,
	TOKEN_INTEGER,

	TOKEN_ERROR,
	TOKEN_EOF,
} TokenType;

typedef struct {
	TokenType type;
	string8 lexeme;

	uint32_t line, column;
} Token;

string8 pretty_error_string(Arena *arena, Token token) {
	string8 result = { 0 };

	bool ok = arena && token.lexeme.bytes;
	if (ok) {
		ASSERT(token.column);

		uint32_t col = token.column - 1;
		string8 line = { .bytes = token.lexeme.bytes - col, .length = token.lexeme.length + col };
		while (line.bytes[line.length] != '\n' && line.bytes[line.length] != '\0')
			line.length++;

		string8 prefix = fmt8(arena, "    %u | ", token.line);
		string8 top = fmt8(arena, "%.*s%.*s\n", arg8(prefix), arg8(line));

		uint32_t arrow_offset = prefix.length + col;
		string8 indent = indent8(arena, s(" "), arrow_offset);

		string8 bottom = fmt8(arena, "%.*s^-- here", arg8(indent));
		result = concat8(arena, top, bottom);
	}

	return result;
}

void report(uint64_t line, uint64_t column, string8 where, string8 message) {
	printf("error[%lu:%lu]: %.*s\n%.*s", line, column, arg8(message), arg8(where));
}

typedef struct {
	string8 source;

	uint32_t line, column;
} Scanner;

void run(string8 source) {
	printf("%.*s\n", arg8(source));
}

int main(int argc, const char **argv) {
	Arena arena[] = { arena_make(MiB(8)) };

	string8 hello = s("hello world");
	Token token = {
		.line = 1,
		.column = 7,
		.lexeme = str8(hello.bytes + 6, 5),
	};

	report(token.line, token.column, pretty_error_string(arena, token), s("Unexpected token 'world'"));
	return 0;

	string8 invalid = str8(0, 58);

	switch (argc) {
		case 1: {
			while (true) {
				printf("> ");
				char buffer[256] = { 0 };

				if (fgets(buffer, sizeof buffer, stdin) == 0)
					break;

				string8 line = str8z(buffer);
				if (line.length && line.bytes[line.length - 1] == '\n') line = chop8(line, 1);

				if (eq8(line, s("exit"))) break;
				run(line);
			}
		}
		case 2: {
			string8 source = os_file_read(arena, str8z(argv[1]));
			run(source);
		} break;
	}

	Token t = { 0 };
}
